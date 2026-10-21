// atom.cluster_occlusion — THE ID PASS CULLS HIDDEN CLUSTERS, NOT ONLY HIDDEN OBJECTS (ATOM-8 stage 2; SPECS/ATOM_8_SPEC.md).
//
// Bistro's two measured classes as one fixture: a FRONT facade (an Atom wall, 20 x 10 m, 20 m in front of the eye at
// street height) hides a BACK facade (24 x 13 m at 30 m) whose box reaches the SKY above the front one's roofline and
// whose box OVERLAPS the front one's in depth (a balcony quad far to the side, outside the view, 12 m from the eye) -
// so the per-OBJECT test can never reject it. Both facades are flat-DAG meshes cut into 8 x 8-quad tiles (one
// 128-triangle cluster a tile, each with its own box). Three worlds in lockstep, built alike (every slot and cluster
// the same id in each): `on` (the occlusion), `ref` (its history thrown away before every frame: no pyramid, the
// frustum-only frame of the same chain), `shut` (the occlusion door shut).
//   (a) AT REST (the hidden view, from the second frame): the back facade shows no pixel, and the FIRST list draws of it
//       only clusters the depth test kept with the sky under their rectangle (the roofline row; printed) - the rest of
//       it is in no list; the front facade is drawn;
//   (b) THE REVEAL (B: the camera teleported every 30 frames between the hidden view and one past the front wall's
//       edge): on the reveal frame the late pass draws rejected clusters (cutLateClusters > 0 within the frames the
//       counters take to read back) and `on`'s picture equals `shut`'s byte for byte on that very frame - no hole -
//       and on every frame of the cycle;
//   (c) wherever an Atom surface is the picture's, `on`'s id equals `ref`'s word for word;
//   (d) the rejected list never overflows (cutRejectedOverflow 0).
// THE BASE (printed, not asserted): with engine.arm atom.clusterOcclusion 0 - the objects' own test alone, the base of
// this spec - the whole back facade is drawn in the first list. Frames, never time: the engine's fixed clock.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/EngineInspect.h"
#include "../support/enginetesthelpers.h"

#include "EnginePrivate.h"

#include <Compositor/OgreCompositorNode.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <OgreImage2.h>
#include <OgreTextureBox.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using jahshaka::engine::detail::OgreView;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
        std::fflush(stdout);                                                    \
    } while (0)
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[720];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static const unsigned kW = 960, kH = 540;

/// A facade w x h metres in the z = 0 plane facing +z, nx x ny quads, its triangles ordered in 8 x 8-quad TILES (the
/// flat DAG cuts 128-triangle runs: one tile a cluster); `balcony` adds one quad at (bx, 0, bz) facing +z.
static MeshData facade(float w, float h, int nx, int ny, bool balcony, float bx, float bz)
{
    MeshData d;
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i) {
            d.positions.insert(d.positions.end(), { -0.5f * w + w * float(i) / float(nx), h * float(j) / float(ny), 0.0f });
            d.normals.insert(d.normals.end(), { 0.0f, 0.0f, 1.0f });
            d.uvs.insert(d.uvs.end(), { float(i) / float(nx), float(j) / float(ny) });
        }
    for (int tj = 0; tj < ny; tj += 8)
        for (int ti = 0; ti < nx; ti += 8)
            for (int j = tj; j < std::min(tj + 8, ny); ++j)
                for (int i = ti; i < std::min(ti + 8, nx); ++i) {
                    const unsigned a = unsigned(j * (nx + 1) + i), b = a + 1u;
                    const unsigned c = unsigned((j + 1) * (nx + 1) + i), e = c + 1u;
                    d.indices.insert(d.indices.end(), { a, b, e, a, e, c });   // counter-clockwise seen from +z
                }
    if (balcony) {
        const unsigned base = unsigned(d.positions.size() / 3u);
        const float q[4][2] = { { -0.5f, 0.0f }, { 0.5f, 0.0f }, { 0.5f, 1.0f }, { -0.5f, 1.0f } };
        for (const auto &p : q) {
            d.positions.insert(d.positions.end(), { bx + p[0], p[1], bz });
            d.normals.insert(d.normals.end(), { 0.0f, 0.0f, 1.0f });
            d.uvs.insert(d.uvs.end(), { 0.0f, 0.0f });
        }
        d.indices.insert(d.indices.end(), { base, base + 1u, base + 2u, base, base + 2u, base + 3u });
    }
    return d;
}

struct World {
    const char *name = "";
    Scene *scene = nullptr;
    View *view = nullptr;
    OgreView *ov = nullptr;
    NodeId front = 0, back = 0;
};

static void buildWorld(World &w)
{
    Scene *s = w.scene;
    s->setLodBias(1.0f);
    s->setAmbient(Colour(0.10f, 0.11f, 0.13f), Colour(0.05f, 0.05f, 0.05f));
    PbrParams fp;
    fp.albedo = Colour(0.6f, 0.55f, 0.5f);
    fp.roughness = 0.6f;
    PbrParams bp;
    bp.albedo = Colour(0.7f, 0.3f, 0.2f);
    bp.roughness = 0.4f;
    const MaterialId fm = s->createPbrMaterial(fp), bm = s->createPbrMaterial(bp);
    const MeshId frontMesh = s->createMesh(facade(20.0f, 10.0f, 64, 32, false, 0.0f, 0.0f));
    // THE BACK FACADE: taller than the front one (its rectangle reaches the sky over the front roofline), its balcony
    // quad 40 m to the side and 18 m in front of it (its box's nearest point in front of the front wall).
    const MeshId backMesh = s->createMesh(facade(24.0f, 13.0f, 64, 32, true, 40.0f, 18.0f));
    w.front = s->createNode();
    s->setNodeTransform(w.front, Vec3(0.0f, 0.0f, -20.0f), Quat(), Vec3(1.0f, 1.0f, 1.0f));
    s->attachMesh(w.front, frontMesh, fm);
    w.back = s->createNode();
    s->setNodeTransform(w.back, Vec3(0.0f, 0.0f, -30.0f), Quat(), Vec3(1.0f, 1.0f, 1.0f));
    s->attachMesh(w.back, backMesh, bm);
    enginetest::addDirectionalLight(s, Vec3(-0.45f, -0.8f, -0.4f), 2.2f);
}

static bool readTex(OgreView *v, const char *name, unsigned words, std::vector<uint32_t> &out)
{
    Ogre::CompositorWorkspace *ws = v->workspace();
    if (!ws) return false;
    Ogre::TextureGpu *tex = nullptr;
    for (Ogre::CompositorNode *n : ws->getNodeSequence())
        if ((tex = n->getDefinedTexture(Ogre::IdString(name))) != nullptr) break;
    if (!tex) return false;
    Ogre::Image2 img;
    img.convertFromTexture(tex, 0u, 0u);
    const Ogre::TextureBox box = img.getData(0u);
    const unsigned w = tex->getWidth(), h = tex->getHeight();
    out.assign(size_t(w) * h * words, 0u);
    for (unsigned r = 0; r < h; ++r)
        std::memcpy(&out[size_t(r) * w * words], box.at(0, r, 0), size_t(w) * words * sizeof(uint32_t));
    return true;
}

/// The pixels where an Atom surface is the picture's. The fixture holds NO stock item, so the frame is the no-stock
/// arm (atom.hidden_cut's third shape): nothing but Atom draws before the opaque pass, every id is the picture's and
/// the Atom view's depth is copied only for its readers - the ids alone decide.
static bool shown(OgreView *v, std::vector<uint32_t> &ids, std::vector<unsigned char> &vis)
{
    if (!readTex(v, detail::kAtomIdTexture, 2u, ids)) return false;
    vis.assign(ids.size() / 2u, 0u);
    for (size_t i = 0; i < vis.size(); ++i) vis[i] = ids[2 * i] != 0xFFFFFFFFu;
    return true;
}

static Engine *gE = nullptr;

static void step(World &ref)
{
    ref.ov->atomOcclusionHistory().valid = false;
    enginetest::frames(gE, 1);
}

struct Probe {
    bool valid = false;
    unsigned backPixels = 0, frontPixels = 0;
    unsigned long long backFirst = 0, backSky = 0, frontDrawn = 0;
};
static Probe probe(World &w, World &ref)
{
    Probe out;
    if (!w.scene->armAtomCutProbe()) return out;
    AtomCutProbe p;
    for (int i = 0; i < 4 && !out.valid; ++i) {
        step(ref);
        if (w.scene->atomCutProbe(p) && p.valid) out.valid = true;
    }
    if (!out.valid) {
        std::printf("   %s: the cut probe did not read back (%s)\n", w.name, p.error.c_str());
        w.scene->disarmAtomCutProbe();
        return out;
    }
    for (const AtomCutProbeObject &o : p.objects) {
        if (o.node == w.back) {
            out.backPixels = o.pixels;
            out.backFirst = o.drawnTriangles - o.drawnTrianglesLate;
            out.backSky = o.skyTriangles;
        }
        if (o.node == w.front) {
            out.frontPixels = o.pixels;
            out.frontDrawn = o.drawnTriangles;
        }
    }
    return out;
}

int main()
{
    std::printf("== atom.cluster_occlusion: a front facade hiding a taller, overlapping back one\n");
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-atom-cluster-occlusion-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(FrameInput::delta(1.0f / 60.0f));
    gE = engine.get();

    World on, ref, shut;
    on.name = "on";
    ref.name = "ref";
    shut.name = "shut";
    for (World *w : { &on, &ref, &shut })
        w->view = gE->createOffscreenView(std::string("cluster-occl-") + w->name, kW, kH, Colour(0.2f, 0.3f, 0.5f));
    for (World *w : { &on, &ref, &shut }) {
        w->scene = gE->createScene(std::string("cluster-occl-") + w->name);
        buildWorld(*w);
    }
    shut.scene->setAtomOcclusionEnabled(false);
    // THE TWO VIEWS: hidden (street height, looking a little up at the front facade: sky above its roofline) and
    // revealed (past the front facade's right edge, the back one in sight).
    const CameraDesc hidden = enginetest::testCameraDescLookAt(Vec3(0.0f, 1.7f, 0.0f), Vec3(0.0f, 4.0f, -20.0f));
    const CameraDesc revealed = enginetest::testCameraDescLookAt(Vec3(18.0f, 1.7f, -8.0f), Vec3(0.0f, 5.0f, -30.0f));
    for (World *w : { &on, &ref, &shut }) {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        w->view->setPostFx(fx);
        w->view->setOffscreenContract(OffscreenContract::StillPicture);
        w->view->setScene(w->scene);
        w->view->setCamera(hidden);
        w->ov = static_cast<OgreView *>(w->view);
    }

    // ---- THE BASE (printed): the objects' own test alone ----
    gE->setArm("atom.clusterOcclusion", 0.0);
    for (int i = 0; i < 8; ++i) step(ref);
    {
        const Probe b = probe(on, ref);
        std::printf("   BASE (atom.clusterOcclusion 0): the back facade's first-list triangles %llu, its pixels %u\n",
                    b.backFirst, b.backPixels);
    }
    gE->setArm("atom.clusterOcclusion", 1.0);
    for (int i = 0; i < 8; ++i) step(ref);

    // ---- (a) AT REST ----
    {
        const Probe a = probe(on, ref);
        const AtomDrawStatus st = on.scene->atomDrawStatus();
        std::printf("   at rest: the back facade's first-list triangles %llu (sky class %llu), pixels %u; the front facade "
                    "drawn %llu, pixels %u; rejected %u, late dropped %u, sky clusters %u\n",
                    a.backFirst, a.backSky, a.backPixels, a.frontDrawn, a.frontPixels, st.cutRejected, st.cutLateDropped,
                    st.cutSkyClusters);
        CHECK_MSG(a.valid && a.frontPixels > 0u && a.frontDrawn > 0u,
                  "the fixture: the front facade draws (%u px)", a.frontPixels);
        CHECK_MSG(a.valid && a.backPixels == 0u, "the fixture: the back facade shows no pixel (%u)", a.backPixels);
        CHECK_MSG(a.valid && st.cutRejected > 0u, "(a) the first cut rejects back clusters (%u)", st.cutRejected);
        CHECK_MSG(a.valid && a.backFirst == a.backSky,
                  "(a) the first list draws of the back facade only its sky-class clusters (%llu of which sky %llu)",
                  a.backFirst, a.backSky);
    }

    // ---- (b) (c) (d) THE REVEAL CYCLE ----
    int compared = 0, picDiffer = 0, idsDiffer = 0, reveals = 0, revealsLate = 0;
    unsigned overflow = 0;
    for (int cycle = 0; cycle < 4; ++cycle) {
        const bool reveal = (cycle % 2) == 0;
        for (World *w : { &on, &ref, &shut }) w->view->setCamera(reveal ? revealed : hidden);
        unsigned late = 0;
        for (int f = 0; f < 30; ++f) {
            step(ref);
            const AtomDrawStatus st = on.scene->atomDrawStatus();
            if (f < 6) late = std::max(late, st.cutLateClusters);
            overflow = std::max(overflow, st.cutRejectedOverflow);
            if (f > 3 && f != 15) continue;   // the switch frame, the next few and one at rest
            std::vector<uint32_t> a, b;
            std::vector<unsigned char> va, vb;
            Image pa, pb;
            if (!shown(on.ov, a, va) || !shown(ref.ov, b, vb) || a.size() != b.size() || !on.view->readPixels(pa) ||
                !shut.view->readPixels(pb) || pa.rgba.size() != pb.rgba.size())
                continue;
            ++compared;
            size_t d = 0;
            for (size_t i = 0; i < va.size(); ++i)
                if (va[i] != vb[i] || (vb[i] && (a[2 * i] != b[2 * i] || a[2 * i + 1] != b[2 * i + 1]))) {
                    if (d == 0)
                        std::printf("   first differing pixel (%zu, %zu): on %08x %08x (shown %d) | ref %08x %08x (shown %d)\n",
                                    i % kW, i / kW, a[2 * i], a[2 * i + 1], int(va[i]), b[2 * i], b[2 * i + 1], int(vb[i]));
                    ++d;
                }
            if (d) {
                ++idsDiffer;
                std::printf("   cycle %d frame %d: %zu pixels' ids differ from ref\n", cycle, f, d);
            }
            size_t q = 0;
            for (size_t i = 0; i < pa.rgba.size(); i += 4)
                if (std::memcmp(&pa.rgba[i], &pb.rgba[i], 3) != 0) ++q;
            if (q) {
                ++picDiffer;
                std::printf("   cycle %d frame %d: %zu pixels differ from shut\n", cycle, f, q);
            }
        }
        if (reveal) {
            ++reveals;
            if (late > 0u) ++revealsLate;
            std::printf("   reveal %d: the late pass drew %u rejected clusters\n", cycle / 2, late);
        }
    }
    CHECK_MSG(reveals == 2 && revealsLate == reveals,
              "(b) every reveal frame's late pass draws rejected clusters (%d of %d)", revealsLate, reveals);
    CHECK_MSG(compared == 20, "(b) the switch frames and the rest frames read (%d of 20)", compared);
    CHECK_MSG(picDiffer == 0, "(b) on's picture equals shut's byte for byte on every read frame, the reveals included "
              "(%d differ)", picDiffer);
    CHECK_MSG(idsDiffer == 0, "(c) wherever an Atom surface is the picture's, on's id equals ref's word for word (%d "
              "frames differ)", idsDiffer);
    CHECK_MSG(overflow == 0u, "(d) the rejected list never overflowed (%u)", overflow);

    for (World *w : { &on, &ref, &shut }) w->view->setScene(nullptr);
    for (World *w : { &on, &ref, &shut }) gE->destroyView(w->view);
    for (World *w : { &on, &ref, &shut }) gE->destroyScene(w->scene);
    engine.reset();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
