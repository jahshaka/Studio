// gi.skin_rays — THE GPU SKIN CACHE AND THE SKINNED BLAS (PHOTON-SKIN-1, RY-R4;
// SPECS/photon/C4_RAYS_BEYOND_REFLECTIONS_DESIGN.md §2).
//
// WHAT WAS WRONG: a rigged Item is drawn by Ogre's vertex shader, which skins
// the bind-pose vertex buffer per pass, so the posed triangles exist nowhere in
// memory. A bottom-level acceleration structure built from the mesh reads the
// BIND POSE, so a walking character would have reflected and cast ray shadows
// as a T-pose — which is why every skinned item was OUT of the traced set
// (audit C-5): characters neither reflected nor ray-shadowed.
//
// WHAT IS PROVED HERE, every number in frames and metres, never time:
//
//   1. THE POSED SHAPE IS WHAT A RAY HITS. The fixture is a two-bone COLUMN
//      (a 0.4 m square section, 2 m tall: the lower metre on bone 0, the upper
//      on bone 1, jointed at y = 1) standing on a floor, and a clip "Bend" that
//      turns the upper bone -90 degrees about Z over one second, so at t = 1 the
//      upper box lies along +X at y 0.8..1.2. Rays straight DOWN at x = 0.6 hit
//      the bent arm's top at y = 1.2 (t = 1.8 from y = 3) and at the BIND pose
//      would reach the floor (t = 3); at x = 0 the bind pose's top is y = 2
//      (t = 1.0) and the bent pose's y = 1.2. Analytic to a millimetre.
//   2. ZERO FRAMES LATE. A clip time pushed before a frame is traced in THAT
//      frame: the skin pass runs after updateSceneGraph, before the gather.
//   3. A WALK COSTS NO SKIN PASS. Moving the node without changing the pose
//      re-skins nothing (the cache is in local space; the TLAS instance moves)
//      and the moved arm is hit where it moved to.
//   4. THE CONTACT RAY SEES THE POSE: a ray from the floor under the bent arm
//      towards the sun, cast with the sun-contact job's own mask (the casters'
//      near copies) and range, is occluded; under the bind pose's arm it is not.
//   5. THE ROW OVERRIDE: the GPU scene's entry for the character names its skin
//      row (GpuInstance::raster[2]); a static item's names none.
//   6. THE MIRROR (`--mirror`, its own row): the reflection of the posed
//      character in a perfect mirror against the raster through the mirror's
//      reflected camera. SINCE PHOTON-HIT-SHADE-1 THE POSED CHARACTER IS SHADED
//      in the reflection: a hit on a rigged item always goes to the hit decode
//      (HlmsAtom, over the instance's skin row — the posed triangles), as does a
//      hit on a MOVER. Asserted: no T-pose ghost; the rigged and the mover arms
//      cover at least the static control's fraction of their raster silhouettes
//      (gi.hit_shade's arm (c)). Owed elsewhere: the voxel feed reading the
//      override (SKIN-2's diffuse).
//   7. A SHARED SKELETON (a multi-piece character's armour) re-skins with its
//      MASTER's clip, in the same frame; a RE-ATTACH in place to a different mesh
//      rebuilds the cache (identity = Item, Mesh, rig generation); a blend index
//      past the rig is refused at attach.
//
// `--cost` (not a ctest row): N characters of V vertices each, every one
// re-posed every frame — the skin dispatch's and the refits' GPU milliseconds
// per item, under scripts/gpu-exclusive.sh.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

static const float kPi = 3.14159265358979f;
int mirrorMain(Engine *e);
static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

// ---------------------------------------------------------------------------
// THE COLUMN: two boxes, each its own 24 vertices (flat normals), the lower on
// bone 0, the upper on bone 1. `rings` > 1 subdivides every side face along Y
// (the cost fixture's vertex count); the geometry is the same box.
static void addBox(MeshData &d, float x0, float x1, float y0, float y1, float z0, float z1,
                   unsigned char bone, unsigned rings)
{
    struct Face { float n[3]; };
    auto quad = [&](const float p[4][3], const float n[3]) {
        // A (rings+1) x 2 grid along the edge p0->p3 / p1->p2.
        const unsigned base = unsigned(d.positions.size() / 3u);
        for (unsigned r = 0; r <= rings; ++r) {
            const float t = float(r) / float(rings);
            for (int side = 0; side < 2; ++side) {
                const float *a = side == 0 ? p[0] : p[1];
                const float *b = side == 0 ? p[3] : p[2];
                d.positions.insert(d.positions.end(),
                                   { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t,
                                     a[2] + (b[2] - a[2]) * t });
                d.normals.insert(d.normals.end(), { n[0], n[1], n[2] });
                d.uvs.insert(d.uvs.end(), { float(side), t });
                d.blendIndices.insert(d.blendIndices.end(), { bone, 0, 0, 0 });
                d.blendWeights.insert(d.blendWeights.end(), { 1.0f, 0.0f, 0.0f, 0.0f });
            }
        }
        for (unsigned r = 0; r < rings; ++r) {
            const unsigned v0 = base + r * 2u, v1 = v0 + 1u, v2 = v0 + 3u, v3 = v0 + 2u;
            d.indices.insert(d.indices.end(), { v0, v1, v2, v0, v2, v3 });
        }
    };
    const float fz[4][3] = { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } };
    const float bz[4][3] = { { x1, y0, z0 }, { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 } };
    const float px[4][3] = { { x1, y0, z1 }, { x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 } };
    const float nx[4][3] = { { x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 } };
    const float py[4][3] = { { x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 }, { x0, y1, z0 } };
    const float ny[4][3] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 } };
    const float nFz[3] = { 0, 0, 1 }, nBz[3] = { 0, 0, -1 }, nPx[3] = { 1, 0, 0 },
                nNx[3] = { -1, 0, 0 }, nPy[3] = { 0, 1, 0 }, nNy[3] = { 0, -1, 0 };
    quad(fz, nFz);
    quad(bz, nBz);
    quad(px, nPx);
    quad(nx, nNx);
    const unsigned keep = rings;
    rings = 1;          // the caps are one quad
    quad(py, nPy);
    quad(ny, nNy);
    rings = keep;
}

static MeshData columnMesh(unsigned rings = 1, float top = 2.0f, float z = 0.0f)
{
    MeshData d;
    addBox(d, -0.2f, 0.2f, 0.0f, 1.0f, z - 0.2f, z + 0.2f, 0, rings);
    addBox(d, -0.2f, 0.2f, 1.0f, top, z - 0.2f, z + 0.2f, 1, rings);
    return d;
}

static SkeletonDesc columnRig()
{
    SkeletonDesc rig;
    rig.id = "gi.skin_rays column rig v1";
    BoneDesc root;
    root.name = "root";
    rig.bones.push_back(root);
    BoneDesc tip;
    tip.name = "tip";
    tip.parent = 0;
    tip.bindPosition = Vec3(0.0f, 1.0f, 0.0f);
    rig.bones.push_back(tip);
    return rig;
}

/// "Bend": the tip bone turns `degrees` about Z over one second (absolute
/// parent-local TRS keys, as the boundary speaks them).
static ClipDesc bendClip(const SkeletonDesc &rig, float degrees)
{
    ClipDesc c;
    c.id = rig.id + " / bend " + std::to_string(degrees);
    c.name = "Bend";
    c.length = 1.0f;
    BoneTrack t;
    t.bone = 1;
    const int keys = 16;
    for (int i = 0; i <= keys; ++i) {
        BoneKey k;
        k.time = float(i) / float(keys);
        k.position = Vec3(0.0f, 1.0f, 0.0f);
        const float a = degrees * kPi / 180.0f * k.time;
        k.rotation = Quat(0.0f, 0.0f, std::sin(0.5f * a), std::cos(0.5f * a));
        t.keys.push_back(k);
    }
    c.tracks.push_back(t);
    return c;
}

static bool pose(Scene *s, NodeId n, float t)
{
    ClipState st;
    st.name = "Bend";
    st.enabled = true;
    st.time = t;
    st.weight = 1.0f;
    st.looping = false;
    return s->setClipStates(n, &st, 1);
}

struct Hit { float t = -1.0f; int slot = -1; };

/// One ray: origin, direction, range, mask.
static std::vector<Hit> trace(Scene *s, const std::vector<std::array<float, 8>> &rays)
{
    std::vector<float> in, out;
    for (const auto &r : rays) {
        // The mask word is read as an unsigned: BIT-COPIED through the float array
        // (gi.rayquery's pushRay), never converted.
        const unsigned m = unsigned(r[7]);
        float bits;
        std::memcpy(&bits, &m, sizeof(bits));
        in.insert(in.end(), { r[0], r[1], r[2], 0.0f, r[3], r[4], r[5], r[6], bits, 0, 0, 0 });
    }
    std::vector<Hit> hits(rays.size());
    if (!s->traceRays(in, out) || out.size() < rays.size() * 4u) return hits;
    for (size_t i = 0; i < rays.size(); ++i) {
        hits[i].t = out[i * 4u];
        hits[i].slot = out[i * 4u + 3u] > 0.5f ? int(out[i * 4u + 1u]) : -1;
    }
    return hits;
}

static std::array<float, 8> down(float x, float z, unsigned mask = kRayMaskNearField)
{
    return { x, 3.0f, z, 0.0f, -1.0f, 0.0f, 10.0f, float(mask) };
}

static int slotOfNode(Scene *s, NodeId n)
{
    const GpuSceneStatus st = s->gpuSceneStatus();
    for (unsigned i = 0; i < st.slotCount; ++i) {
        GpuSceneEntry e;
        if (s->gpuSceneEntry(i, e) && e.nodeId == n) return int(i);
    }
    return -1;
}

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// ---------------------------------------------------------------------------
static int costMain(Engine *e, int characters, unsigned rings)
{
    View *view = e->createOffscreenView("skincost", 1920, 1080, Colour(0, 0, 0));
    Scene *s = e->createScene("skincost");
    view->setScene(s);
    s->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.2f), 3.0f);
    const SkeletonDesc rig = columnRig();
    const MeshData md = columnMesh(rings);
    const MeshId mesh = s->createMesh(md);
    PbrParams p;
    p.albedo = Colour(0.6f, 0.5f, 0.4f);
    const MaterialId mat = s->createPbrMaterial(p);
    const ClipDesc clip = bendClip(rig, -90.0f);
    std::vector<NodeId> nodes;
    for (int i = 0; i < characters; ++i) {
        const NodeId n = s->createNode();
        s->attachSkinnedMesh(n, mesh, mat, rig);
        s->attachClips(n, &clip, 1);
        enginetest::setNodePosition(s, n, Vec3(-6.0f + 1.5f * float(i % 9), 0.0f, -4.0f - 1.5f * float(i / 9)));
        nodes.push_back(n);
    }
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, 6.0f), Vec3(0.0f, 1.0f, -4.0f));
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    view->setPostFx(fx);
    render(e, 30);
    // TWO ARMS, PAIRED IN ONE PROCESS (the measurement law), alternated: every
    // character re-posed EVERY frame (a crowd, all animating — the worst case),
    // and the same scene held still. The difference in the frame's GPU time is
    // the skin cache's whole cost; the tier's own timestamps split it.
    e->setFrameMonitor(MonitorLevel::Review);
    double skinMs = 0.0, refitMs = 0.0, frameAnim = 0.0, frameStill = 0.0, cpuMs = 0.0;
    int skinN = 0, animN = 0, stillN = 0;
    const unsigned long long p0 = s->rayQueryStatus().skinPasses;
    int frame = 0;
    for (int round = 0; round < 4; ++round) {
        for (int arm = 0; arm < 2; ++arm) {
            for (int f = 0; f < 60; ++f, ++frame) {
                if (arm == 0)
                    for (NodeId n : nodes) pose(s, n, 0.5f + 0.5f * std::sin(0.05f * float(frame) + float(n)));
                e->renderOneFrame();
                std::vector<FrameRecord> recs;
                e->takeFrameRecords(recs);
                if (f < 20) continue;                  // settle: the queries come back 3 frames late
                for (const FrameRecord &r : recs) {
                    if (r.gpuMs <= 0.0f) continue;
                    if (arm == 0) { frameAnim += r.gpuMs; ++animN; }
                    else { frameStill += r.gpuMs; ++stillN; }
                }
                const RayQueryStatus rq = s->rayQueryStatus();
                if (arm == 0 && rq.skinMs >= 0.0f) {
                    skinMs += rq.skinMs;
                    refitMs += rq.skinRefitMs;
                    cpuMs += rq.skinCpuMs;
                    ++skinN;
                }
            }
        }
    }
    e->setFrameMonitor(MonitorLevel::Off);
    const RayQueryStatus rq = s->rayQueryStatus();
    const double items = double(characters);
    const double verts = double(md.positions.size() / 3u);
    std::printf("cost: %d characters x %.0f vertices (%zu triangles each), re-posed every frame in the "
                "animated arm\n",
                characters, verts, md.indices.size() / 3u);
    std::printf("cost: skinPasses %llu over the run, skinnedInstances %d, refits %llu\n",
                rq.skinPasses - p0, rq.skinnedInstances, rq.skinRefits);
    if (skinN && animN && stillN) {
        const double sm = skinMs / skinN, rm = refitMs / skinN;
        const double fa = frameAnim / animN, fs = frameStill / stillN;
        std::printf("cost: skin dispatch %.4f ms/frame = %.4f ms per item = %.4f ms per 10k vertices\n",
                    sm, sm / items, sm / items / verts * 1e4);
        std::printf("cost: skinned refits %.4f ms/frame = %.4f ms per item\n", rm, rm / items);
        std::printf("cost: passes GPU ms animated %.4f / still %.4f (delta %.4f)\n", fa, fs, fa - fs);
        std::printf("cost: skin pass CPU %.4f ms/frame = %.4f ms per item (this build's config)\n",
                    cpuMs / skinN, cpuMs / skinN / items);
        std::printf("cost: RATIOS  skin/frame %.4f  refit/frame %.4f  (skin+refit)/16.667ms %.4f\n",
                    sm / fa, rm / fa, (sm + rm) / 16.667);
    } else {
        std::printf("cost: timestamps not read (skin %d anim %d still %d)\n", skinN, animN, stillN);
    }
    return 0;
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv)
{
    bool wantCost = false, wantMirror = false;
    int costChars = 9;
    unsigned costRings = 256;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--cost")) wantCost = true;
        else if (!std::strcmp(argv[i], "--mirror")) wantMirror = true;
        else if (!std::strcmp(argv[i], "--chars") && i + 1 < argc) costChars = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--rings") && i + 1 < argc) costRings = unsigned(std::atoi(argv[++i]));
    }
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = wantCost ? "test-gi-skin-rays-cost-ogre.log"
                  : getenv("JAHSHAKA_NO_RAY_QUERY") ? "test-gi-skin-rays-norays-ogre.log"
                                                    : "test-gi-skin-rays-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    if (wantCost) return costMain(e, costChars, costRings);
    if (wantMirror) return mirrorMain(e);

    View *view = e->createOffscreenView("skinrays", 256, 256, Colour(0, 0, 0));
    Scene *s = e->createScene("skinrays");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    const bool raysWanted = !getenv("JAHSHAKA_NO_RAY_QUERY");
    const bool haveRays = e->rayQueryAvailable() && e->rayTracing();

    s->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
    const NodeId floor = enginetest::addTestCube(s, Colour(0.7f, 0.7f, 0.7f), 0.0f, 1.0f);
    enginetest::setNodeScale(s, floor, Vec3(20.0f, 0.2f, 20.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    // THE SUN, straight down (a whisker off so the directional light is well formed):
    // the contact ray from the floor under the arm goes straight up at it.
    enginetest::addDirectionalLight(s, Vec3(0.0f, -1.0f, 0.001f), 3.0f);

    const SkeletonDesc rig = columnRig();
    const MeshId mesh = s->createMesh(columnMesh());
    PbrParams cp;
    cp.albedo = Colour(0.8f, 0.2f, 0.2f);
    cp.roughness = 0.6f;
    const MaterialId mat = s->createPbrMaterial(cp);
    const NodeId ch = s->createNode();
    CHECK_MSG(mesh && mat && ch && s->attachSkinnedMesh(ch, mesh, mat, rig),
              "the two-bone column attaches skinned (%s)", e->lastError().c_str());
    const ClipDesc clip = bendClip(rig, -90.0f);
    CHECK_MSG(s->attachClips(ch, &clip, 1), "the Bend clip attaches (%s)", e->lastError().c_str());
    CHECK(pose(s, ch, 0.0f), "the clip poses the column STRAIGHT (t = 0)");
    // THE FOLLOWER (a multi-piece character's armour): a second piece — the same
    // column shifted to z = 0.5, beside the body — SHARING the first's skeleton,
    // so it is posed by the MASTER's clip and never by one of its own. A shared
    // instance's bones carry the MASTER's node (Bone: node x derived x reverse
    // bind), so the piece draws where the master stands, as the raster does.
    const float kFz = 0.5f;
    const NodeId fo = s->createNode();
    CHECK_MSG(s->attachSkinnedMesh(fo, s->createMesh(columnMesh(1, 2.0f, kFz)), mat, rig) &&
                  s->shareSkeleton(fo, ch),
              "a follower piece shares the master's skeleton (%s)", e->lastError().c_str());
    // THE MALFORMED ASSET: a blend index past the rig (bone 5 of 2) is refused at
    // attach with a reason, so the raster and the cache never draw two different
    // wrong pictures (OgreSkeleton.cpp's check; the skin job's clamp is the
    // second lock).
    {
        MeshData bad = columnMesh();
        bad.blendIndices[0] = 5;
        const MeshId badMesh = s->createMesh(bad);
        const NodeId bn = s->createNode();
        CHECK_MSG(badMesh && !s->attachSkinnedMesh(bn, badMesh, mat, rig) &&
                      e->lastError().find("bone the rig does not have") != std::string::npos,
                  "a mesh whose blend index names bone 5 of a 2-bone rig is REFUSED (%s)",
                  e->lastError().c_str());
        s->removeNode(bn);
    }
    enginetest::testCameraLookAt(view, Vec3(3.0f, 2.5f, 5.0f), Vec3(0.3f, 1.0f, 0.0f));
    view->setShadows(true);
    SunContactDesc sc;
    sc.enabled = true;
    s->setSunContact(sc);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    view->setPostFx(fx);
    render(e, 4);

    if (!raysWanted) {
        // THE NO-RAYS ARM: nothing of the tier exists, and a rigged scene draws.
        const RayQueryStatus rq = s->rayQueryStatus();
        CHECK(!rq.enabled && rq.skinCaches == 0, "no rays: no skin cache, no structure");
        const int slot = slotOfNode(s, ch);
        GpuSceneEntry en;
        CHECK(slot >= 0 && s->gpuSceneEntry(unsigned(slot), en) && en.skinRow == 0xFFFFFFFFu,
              "no rays: the character's entry carries NO row override");
        std::printf("%s\n", failures ? "FAILED" : "PASSED");
        return failures ? 1 : 0;
    }
    if (!haveRays) {
        std::printf("ok: no ray queries on this machine — gi.skin_rays skips cleanly\n");
        return 0;
    }

    const int chSlot = slotOfNode(s, ch);
    const int floorSlot = slotOfNode(s, floor);
    CHECK_MSG(chSlot >= 0 && floorSlot >= 0, "the character (slot %d) and the floor (slot %d) are in the GPU scene",
              chSlot, floorSlot);
    {
        const RayQueryStatus rq = s->rayQueryStatus();
        std::printf("    status: instances %d skinned %d caches %d passes %llu dispatches %llu builds %llu "
                    "refits %llu reason '%s'\n",
                    rq.instances, rq.skinnedInstances, rq.skinCaches, rq.skinPasses, rq.skinDispatches,
                    rq.skinBlasBuilds, rq.skinRefits, rq.skinReason.c_str());
        CHECK(rq.skinnedInstances == 2 && rq.skinCaches == 2,
              "5a. the rigged column and its follower ARE traced, each through its own skin cache "
              "(they used to be excluded)");
        CHECK(rq.skinBlasBuilds == 2, "each structure was built once");
        GpuSceneEntry en, fl;
        CHECK(s->gpuSceneEntry(unsigned(chSlot), en) && en.skinRow != 0xFFFFFFFFu,
              "5b. the character's GPU-scene entry names its SKIN ROW (the per-instance override)");
        CHECK(s->gpuSceneEntry(unsigned(floorSlot), fl) && fl.skinRow == 0xFFFFFFFFu,
              "5c. the floor's names none (its mesh's rows are its geometry)");
    }

    // ---- 1: straight ------------------------------------------------------
    {
        const auto h = trace(s, { down(0.6f, 0.0f), down(0.0f, 0.0f), down(0.6f, kFz) });
        CHECK_MSG(h[2].slot == floorSlot && near(h[2].t, 3.0f, 2e-3f),
                  "1a'. straight: at x = 0.6 the FOLLOWER's ray passes it too (t %.4f)", h[2].t);
        CHECK_MSG(h[0].slot == floorSlot && near(h[0].t, 3.0f, 2e-3f),
                  "1a. straight: a ray down at x = 0.6 passes the column and meets the floor (t %.4f, slot %d)",
                  h[0].t, h[0].slot);
        CHECK_MSG(h[1].slot == chSlot && near(h[1].t, 1.0f, 2e-3f),
                  "1b. straight: a ray down at x = 0 meets the column's top at y = 2 (t %.4f, slot %d)",
                  h[1].t, h[1].slot);
    }
    // ---- 2: bend, ONE frame -------------------------------------------------
    const unsigned long long passesBefore = s->rayQueryStatus().skinPasses;
    const unsigned long long refitsBefore = s->rayQueryStatus().skinRefits;
    CHECK(pose(s, ch, 1.0f), "the clip bends the column (t = 1)");
    render(e, 1);
    {
        const auto h = trace(s, { down(0.6f, 0.0f), down(0.0f, 0.0f), down(0.95f, 0.0f),
                                  down(0.6f, kFz) });
        const int foSlot = slotOfNode(s, fo);
        CHECK_MSG(h[3].slot == foSlot && near(h[3].t, 1.8f, 2e-3f),
                  "1c'. the FOLLOWER, posed only by the MASTER's clip, is hit on its bent arm in the "
                  "SAME frame (t %.4f, slot %d of %d) — its own pose serial never moved",
                  h[3].t, h[3].slot, foSlot);
        CHECK_MSG(h[0].slot == chSlot && near(h[0].t, 1.8f, 2e-3f),
                  "1c+2. ONE frame after the pose push, a ray down at x = 0.6 meets the BENT arm's top "
                  "at y = 1.2 (t %.4f, slot %d; the bind pose would reach the floor at t 3)",
                  h[0].t, h[0].slot);
        CHECK_MSG(h[1].slot == chSlot && near(h[1].t, 1.8f, 2e-3f),
                  "1d. at x = 0 it meets y = 1.2 too — not the bind pose's top at y = 2 (t %.4f)", h[1].t);
        CHECK_MSG(h[2].slot == chSlot && near(h[2].t, 1.8f, 2e-3f),
                  "1e. and at x = 0.95, the bent arm's far end (t %.4f)", h[2].t);
        // 1g. EVERY HIT IS ON THE POSED SHAPE: a 64 x 64 sheet of rays towards -Z
        // over the whole character; a hit on it must land inside the bent
        // column's two boxes (an unwritten or garbage cache vertex would put a
        // triangle anywhere), and every ray aimed through the bent arm must hit.
        {
            std::vector<std::array<float, 8>> sheet;
            for (int j = 0; j < 64; ++j)
                for (int i = 0; i < 64; ++i)
                    sheet.push_back({ -1.5f + 3.0f * (float(i) + 0.5f) / 64.0f,
                                      -0.2f + 2.6f * (float(j) + 0.5f) / 64.0f, 3.0f, 0.0f, 0.0f, -1.0f,
                                      10.0f, float(kRayMaskNearField) });
            const auto hs = trace(s, sheet);
            int onChar = 0, outside = 0, armMissed = 0;
            for (size_t k = 0; k < sheet.size(); ++k) {
                const float x = sheet[k][0], y = sheet[k][1];
                const bool inLower = x > -0.2f && x < 0.2f && y > 0.0f && y < 1.0f;
                const bool inArm = x > 0.0f && x < 1.0f && y > 0.8f && y < 1.2f;
                // The follower piece stands in front (z to kFz + 0.2) and is posed
                // by the same bones: a hit on either must land on the bent shape.
                if (hs[k].slot != chSlot && hs[k].slot != foSlot) {
                    if (inArm && x > 0.25f && y > 0.85f && y < 1.15f) ++armMissed;
                    continue;
                }
                ++onChar;
                const float z = 3.0f - hs[k].t;
                const float front = hs[k].slot == foSlot ? kFz + 0.2f : 0.2f;
                const bool zOk = z > front - 2e-3f && z < front + 2e-3f;
                if (!zOk || (!inLower && !inArm)) ++outside;
            }
            CHECK_MSG(onChar > 100 && outside == 0 && armMissed == 0,
                      "1g. a 64x64 sheet of rays: %d hit the character, %d of them OFF the bent shape "
                      "(front faces z = 0.2 / 0.7), %d rays through the bent arm missed it",
                      onChar, outside, armMissed);
        }
        const RayQueryStatus rq = s->rayQueryStatus();
        CHECK_MSG(rq.skinPasses == passesBefore + 2 && rq.skinRefits == refitsBefore + 2,
                  "2b. the pose change cost ONE skin pass and ONE refit per piece, master and "
                  "follower (%llu, %llu)",
                  rq.skinPasses - passesBefore, rq.skinRefits - refitsBefore);
    }
    // ---- 3: still frames and a walk ----------------------------------------
    {
        const unsigned long long p0 = s->rayQueryStatus().skinPasses;
        const unsigned long long d0 = s->rayQueryStatus().skinDispatches;
        render(e, 5);
        CHECK_MSG(s->rayQueryStatus().skinPasses == p0 && s->rayQueryStatus().skinDispatches == d0,
                  "3a. five still frames re-skin nothing (%llu passes)", s->rayQueryStatus().skinPasses - p0);
        enginetest::setNodePosition(s, ch, Vec3(2.0f, 0.0f, 0.0f));
        render(e, 1);
        CHECK_MSG(s->rayQueryStatus().skinPasses == p0,
                  "3b. a WALK (the node moved, the pose did not) re-skins nothing (%llu passes)",
                  s->rayQueryStatus().skinPasses - p0);
        const auto h = trace(s, { down(2.6f, 0.0f), down(0.6f, 0.0f) });
        CHECK_MSG(h[0].slot == chSlot && near(h[0].t, 1.8f, 2e-3f),
                  "3c. the walked character's bent arm is hit where it walked to (x = 2.6: t %.4f)", h[0].t);
        CHECK_MSG(h[1].slot == floorSlot, "3d. and no longer where it was (x = 0.6 meets the floor)");
        enginetest::setNodePosition(s, ch, Vec3(0.0f, 0.0f, 0.0f));
        render(e, 1);
    }
    // ---- 4: the contact ray -------------------------------------------------
    {
        const SunContactStatus sst = s->sunContactStatus();
        std::printf("    sun contact: on %d running %d range %.2f toSun (%.3f %.3f %.3f) reason '%s'\n",
                    int(sst.on), int(sst.running), sst.range, sst.toSun[0], sst.toSun[1], sst.toSun[2],
                    sst.reason.c_str());
        CHECK(sst.running, "4a. the sun-contact job is running over this fixture");
        const float tx = sst.toSun[0], ty = sst.toSun[1], tz = sst.toSun[2];
        const float range = sst.range > 0.0f ? sst.range : 2.0f;
        auto up = [&](float x) {
            return std::array<float, 8>{ x, 0.001f, 0.0f, tx, ty, tz, range, float(kRayMaskCaster) };
        };
        const auto h = trace(s, { up(0.6f), up(-0.6f) });
        CHECK_MSG(h[0].slot == chSlot && near(h[0].t, 0.8f, 0.02f),
                  "4b. a contact ray from the floor under the BENT arm (x = 0.6) is occluded by it at "
                  "y = 0.8 (t %.4f, slot %d) — the posed shape casts; the bind pose's arm was not there",
                  h[0].t, h[0].slot);
        CHECK_MSG(h[1].t < 0.0f, "4c. beside the column (x = -0.6) the contact ray escapes (t %.4f)", h[1].t);
    }
    // ---- straighten again: the refit follows back ---------------------------
    CHECK(pose(s, ch, 0.0f), "the clip straightens the column again");
    render(e, 1);
    {
        const auto h = trace(s, { down(0.6f, 0.0f), down(0.0f, 0.0f) });
        CHECK_MSG(h[0].slot == floorSlot && near(h[1].t, 1.0f, 2e-3f),
                  "1f. straight again one frame later: x = 0.6 reaches the floor, x = 0 the top at y = 2 "
                  "(t %.4f / %.4f)", h[0].t, h[1].t);
    }
    // ---- a RE-ATTACH in place: a new mesh, a new vertex count, the same node --
    // attachSkinnedMesh detaches and re-creates the Item at the same node, and a new
    // Item may land at the old one's address: the cache must follow the MESH and
    // the rig generation, never the pointer (a stale cache would over-read the new
    // source and refit over the old mesh's released indices).
    {
        s->removeNode(fo);       // the follower's share would die with the master's Item
        render(e, 1);
        const MeshId tall = s->createMesh(columnMesh(3, 2.5f));
        const bool reattached = s->attachSkinnedMesh(ch, tall, mat, rig);
        CHECK_MSG(reattached, "the character re-attaches in place to a TALLER column with a different "
                              "vertex count (%s)", reattached ? "" : e->lastError().c_str());
        render(e, 1);
        const auto h = trace(s, { down(0.0f, 0.0f) });
        const RayQueryStatus rq = s->rayQueryStatus();
        CHECK_MSG(h[0].slot == slotOfNode(s, ch) && near(h[0].t, 0.5f, 2e-3f) && rq.skinCaches == 1,
                  "the next frame's ray meets the NEW shape's top at y = 2.5 (t %.4f) and the "
                  "character still holds ONE cache (%d)", h[0].t, rq.skinCaches);
    }
    // ---- a removed character gives its cache back --------------------------
    {
        s->removeNode(ch);
        render(e, 2);
        const RayQueryStatus rq = s->rayQueryStatus();
        CHECK_MSG(rq.skinCaches == 0 && rq.skinnedInstances == 0,
                  "a removed character's cache and structure are released (%d caches)", rq.skinCaches);
    }

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// `--mirror`: THE POSED CHARACTER IN A PERFECT MIRROR, AGAINST THE RASTER.
//
// A mirror wall the camera faces (its front face the plane z = kMirrorZ) and the
// character BEHIND the camera, where no screen-space march can see it. The
// reflection's silhouette is the difference between the mirror with the
// character and the mirror without it; the RASTER's silhouette is the same
// difference seen DIRECTLY from the camera reflected in the mirror plane (the
// wall hidden), flipped left-right — a perfect mirror seen head-on is exactly
// that picture. Two characters are measured the same way: the rigged column
// bent by its clip, and a STATIC mesh of the same bent shape (the control: every
// object the tier traced before this lane).
static const unsigned kMirrorSize = 384;
static const float kMirrorZ = 4.85f;

static void savePpm(const Image &img, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (size_t i = 0; i < size_t(img.width) * img.height; ++i) std::fwrite(&img.rgba[i * 4u], 1, 3, f);
    std::fclose(f);
}

static std::vector<unsigned char> diffMask(const Image &a, const Image &b, bool flip, int thr)
{
    std::vector<unsigned char> m(size_t(a.width) * a.height, 0u);
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const size_t i = (size_t(y) * a.width + x) * 4u;
            int d = 0;
            for (int c = 0; c < 3; ++c) d = std::max(d, std::abs(int(a.rgba[i + c]) - int(b.rgba[i + c])));
            const unsigned ox = flip ? a.width - 1u - x : x;
            if (d > thr) m[size_t(y) * a.width + ox] = 1u;
        }
    return m;
}

struct MaskCompare { unsigned a = 0, b = 0, both = 0; float worstAtoB = 0, worstBtoA = 0, iou = 0; };

/// Pixel counts, the overlap, and the worst distance (px) from a pixel of one
/// mask to the nearest pixel of the other — the silhouette tolerance N.
static MaskCompare compareMasks(const std::vector<unsigned char> &A, const std::vector<unsigned char> &B,
                                unsigned w, unsigned h)
{
    MaskCompare r;
    auto nearest = [&](const std::vector<unsigned char> &M, int x, int y) {
        for (int rad = 0; rad < int(w); ++rad) {
            for (int dy = -rad; dy <= rad; ++dy)
                for (int dx = -rad; dx <= rad; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != rad) continue;
                    const int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= int(w) || yy >= int(h)) continue;
                    if (M[size_t(yy) * w + xx]) return float(std::sqrt(float(dx * dx + dy * dy)));
                }
        }
        return float(w);
    };
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            const bool a = A[size_t(y) * w + x] != 0, b = B[size_t(y) * w + x] != 0;
            r.a += a; r.b += b; r.both += (a && b);
            if (a && !b) r.worstAtoB = std::max(r.worstAtoB, nearest(B, int(x), int(y)));
            if (b && !a) r.worstBtoA = std::max(r.worstBtoA, nearest(A, int(x), int(y)));
        }
    const unsigned uni = r.a + r.b - r.both;
    r.iou = uni ? float(r.both) / float(uni) : 0.0f;
    return r;
}

/// The bent column as a STATIC mesh: the lower box, and the upper box turned
/// -90 degrees about Z at the joint (x 0..1, y 0.8..1.2).
static MeshData bentStaticMesh()
{
    MeshData d;
    addBox(d, -0.2f, 0.2f, 0.0f, 1.0f, -0.2f, 0.2f, 0, 1);
    addBox(d, 0.0f, 1.0f, 0.8f, 1.2f, -0.2f, 0.2f, 0, 1);
    d.blendIndices.clear();
    d.blendWeights.clear();
    return d;
}

int mirrorMain(Engine *e)
{
    View *view = e->createOffscreenView("skinmirror", kMirrorSize, kMirrorSize, Colour(0, 0, 0));
    Scene *s = e->createScene("skinmirror");
    view->setScene(s);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries — the mirror arm skips\n");
        return 0;
    }
    s->setAmbient(Colour(0.25f, 0.25f, 0.28f), Colour(0.2f, 0.2f, 0.2f));
    const NodeId wall = s->createNode();
    PbrParams wp;
    wp.albedo = Colour(1, 1, 1);
    wp.metalness = 1.0f;
    wp.roughness = 0.0f;
    s->attachMesh(wall, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(wp));
    enginetest::setNodeScale(s, wall, Vec3(14.0f, 9.0f, 0.3f));
    enginetest::setNodePosition(s, wall, Vec3(0.0f, 1.0f, kMirrorZ + 0.15f));
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.4f), 3.0f);

    const SkeletonDesc rig = columnRig();
    PbrParams cp;
    cp.albedo = Colour(0.9f, 0.9f, 0.9f);
    cp.emissive = Colour(0.9f, 0.1f, 0.1f);
    cp.roughness = 0.6f;
    const MaterialId mat = s->createPbrMaterial(cp);
    const NodeId ch = s->createNode();
    s->attachSkinnedMesh(ch, s->createMesh(columnMesh()), mat, rig);
    const ClipDesc clip = bendClip(rig, -90.0f);
    s->attachClips(ch, &clip, 1);
    pose(s, ch, 1.0f);
    const Vec3 at(0.0f, 0.0f, -8.0f);
    enginetest::setNodePosition(s, ch, at);
    const NodeId st = s->createNode();
    s->attachMesh(st, s->createMesh(bentStaticMesh()), mat);
    enginetest::setNodePosition(s, st, at);
    // ...and the same static shape as a MOVER (kMovableBit: not voxelised), the
    // third arm: what a traced hit on an object the voxels do not hold shows.
    const NodeId mv = s->createNode();
    s->setNodeMovable(mv, true);
    s->attachMesh(mv, s->createMesh(bentStaticMesh()), mat);
    enginetest::setNodePosition(s, mv, at);

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.testBoundsMin = Vec3(-8.0f, -2.0f, -12.0f);
    gi.testBoundsMax = Vec3(8.0f, 8.0f, 6.0f);
    s->setGlobalIllumination(gi);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    view->setPostFx(fx);

    const Vec3 cam(0.0f, 1.0f, -3.0f);
    const Vec3 camR(0.0f, 1.0f, 2.0f * kMirrorZ - cam.z);        // reflected in the mirror plane
    int arm = 0;
    auto shot = [&](bool mirrorArm, bool showSkinned, bool showStatic, const char *tag) {
        s->setNodeVisible(wall, mirrorArm);
        s->setNodeVisible(ch, showSkinned);
        s->setNodeVisible(st, showStatic && arm == 1);
        s->setNodeVisible(mv, showStatic && arm == 2);
        if (mirrorArm) enginetest::testCameraLookAt(view, cam, Vec3(0.0f, 1.0f, kMirrorZ));
        else enginetest::testCameraLookAt(view, camR, Vec3(0.0f, 1.0f, -20.0f));
        render(e, 40);
        Image img;
        view->readPixels(img);
        savePpm(img, std::string("skin-mirror-") + tag + ".ppm");
        return img;
    };
    float fraction[3] = { 0.0f, 0.0f, 0.0f };
    for (int which = 0; which < 3; ++which) {
        arm = which;
        const bool skinned = which == 0;
        const char *name = skinned ? "RIGGED (skin cache)" : which == 1 ? "STATIC control" : "STATIC MOVER";
        const char *mt = which == 0 ? "m-rig" : which == 1 ? "m-static" : "m-mover";
        const char *rt = which == 0 ? "r-rig" : which == 1 ? "r-static" : "r-mover";
        const Image mOn = shot(true, skinned, !skinned, mt);
        const Image mOff = shot(true, false, false, "m-none");
        const Image rOn = shot(false, skinned, !skinned, rt);
        const Image rOff = shot(false, false, false, "r-none");
        const auto refl = diffMask(mOn, mOff, false, 6);
        const auto rast = diffMask(rOn, rOff, true, 6);
        const MaskCompare c = compareMasks(refl, rast, kMirrorSize, kMirrorSize);
        std::printf("mirror %-20s: reflection %u px, raster %u px, both %u (%.1f %% of the raster), IoU %.3f, "
                    "worst reflection->raster %.1f px, worst raster->reflection %.1f px\n",
                    name, c.a, c.b, c.both, c.b ? 100.0 * c.both / c.b : 0.0, c.iou, c.worstAtoB, c.worstBtoA);
        fraction[which] = c.b ? float(c.both) / float(c.b) : 0.0f;
        if (which == 1) {
            // THE TIER'S OWN BASELINE — an object the voxels hold: its reflection is
            // its raster silhouette to within the voxel shading's edge.
            CHECK_MSG(c.b > 200 && c.worstBtoA <= 3.0f,
                      "mirror: a STATIC object's reflection covers its raster silhouette to within %.1f px",
                      c.worstBtoA);
        }
        if (skinned) {
            // NO T-POSE GHOST: the raster of the STRAIGHT (bind-like) pose from the
            // same reflected camera; a reflection pixel inside it and more than 2 px
            // from the bent raster would be the bind pose showing through.
            // WHY 2 px: a MIRROR is not spatially filtered at all (radius 0 at the
            // mirror alpha, rq_reflect_filter.comp), so the tolerance is the shaded
            // edge the STATIC control measures on this fixture (worst raster-to-
            // reflection 1.4-2.2 px: the voxel cell a hit is shaded from). A filter
            // that ever reaches a mirror must be read against this bar.
            pose(s, ch, 0.0f);
            const Image rStraight = shot(false, true, false, "r-rig-straight");
            pose(s, ch, 1.0f);
            const auto straight = diffMask(rStraight, rOff, true, 6);
            unsigned ghost = 0, beyond = 0, beyondShown = 0;
            for (unsigned y = 0; y < kMirrorSize; ++y)
                for (unsigned x = 0; x < kMirrorSize; ++x) {
                    const size_t i = size_t(y) * kMirrorSize + x;
                    bool nearBent = false;
                    for (int dy = -2; dy <= 2 && !nearBent; ++dy)
                        for (int dx = -2; dx <= 2 && !nearBent; ++dx) {
                            const int xx = int(x) + dx, yy = int(y) + dy;
                            if (xx >= 0 && yy >= 0 && xx < int(kMirrorSize) && yy < int(kMirrorSize) &&
                                rast[size_t(yy) * kMirrorSize + xx])
                                nearBent = true;
                        }
                    if (refl[i] && straight[i] && !nearBent) ++ghost;
                    // THE BENT ARM BEYOND THE STRAIGHT POSE'S ENVELOPE: where only the
                    // posed geometry is.
                    if (rast[i] && !straight[i]) {
                        ++beyond;
                        if (refl[i]) ++beyondShown;
                    }
                }
            CHECK_MSG(ghost == 0, "mirror: NO T-POSE GHOST — %u reflection pixels where only the straight pose "
                                  "would be", ghost);
            std::printf("mirror: the bent arm beyond the straight pose's silhouette: %u raster px, %u of them "
                        "shown in the reflection (%.1f %%) — shaded by the hit decode on the POSED triangles\n",
                        beyond, beyondShown, beyond ? 100.0 * beyondShown / beyond : 0.0);
        }
    }
    // THE RIGGED AND THE MOVER ARMS AGAINST THE CONTROL (PHOTON-HIT-SHADE-1; the
    // print this replaced said "a MOVER shows 0 of 435 reflection px ... for EVERY
    // mover"): each covers at least the static control's fraction of its raster
    // silhouette, less one point for the silhouette's edge (gi.hit_shade (a)).
    CHECK_MSG(fraction[0] >= fraction[1] - 0.01f,
              "mirror: the POSED character's reflection is SHADED — %.1f %% of its raster silhouette, the static "
              "control's %.1f %%", 100.0f * fraction[0], 100.0f * fraction[1]);
    CHECK_MSG(fraction[2] >= fraction[1] - 0.01f,
              "mirror: a MOVER's reflection is SHADED — %.1f %% of its raster silhouette, the static control's "
              "%.1f %%", 100.0f * fraction[2], 100.0f * fraction[1]);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
