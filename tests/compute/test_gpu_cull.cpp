// engine.gpu_cull — ATOM P3's SUBSTRATE: the generic cull, compaction and
// indirect draw chain over the GPU scene (SPECS/atom/A4_SUBSTRATE_CULL_DESIGN.md
// section 4).
//
// WHAT IS ASSERTED, in the design's own order:
//   1. MODE 0 — the survivor SET equals a CPU cull of the same table, instance by
//      instance, on the frustum alone. The CPU reference reads the same table
//      through `gpuSceneEntry`, so the two answer the same question from the same
//      bytes and a disagreement is the shader's and nothing else's.
//   2. THE HZB MODE — with an occluding wall in front of the camera, every
//      instance fully behind it is gone and no instance the wall does not cover
//      is. THE FIXTURE IS CHECKED BEFORE THE CULL IS BLAMED: the pyramid's own
//      mip 0 is read back and the texel at the wall's centre must be nearer than
//      the far plane. That check exists because the first version of this suite
//      read "nothing is ever culled" as a shader defect when the cause was the
//      fixture — the wall's triangles were wound backwards and it drew nothing
//      at all (see `chainedMesh`). A depth test can only be tested against a
//      depth buffer that has something in it.
//      The pyramid a cull reads must keep the FARTHEST depth per footprint
//      (PostFxDesc::hzbFarthest): the closest-depth chain makes a texel that is
//      half wall and half sky report the wall, and an object visible through the
//      sky half is then culled.
//   3. MODE 1 — the per-slot levels equal the CPU level rule's, which is the
//      quality currency evaluated on the host (`allowedWorldError` +
//      `lodLevelForWorldError`). `engine.lod_rule_parity` is the wide version of
//      this over 10,000 triples; here it runs on the real scene.
//   4. MODE 2 — the draw commands' index ranges are the table's, the instance
//      count is 1, `firstInstance` is the survivor's own slot, and the COUNT the
//      GPU wrote equals the survivor count.
//   5. THE CHAIN IS GPU-DRIVEN AFTER JOB 1. Job 3's thread-group count is
//      written by job 2 and read by `vkCmdDispatchIndirect`, and it is asserted
//      at 0, 7 and 4,096 survivors — an empty set, an arbitrary small count no
//      host arithmetic could have predicted, and the worst case: the three shapes
//      `compute.indirect_dispatch` asserted, which is what lets that probe and
//      its three jobs be deleted. The seven is a cut placed between two sorted
//      depths of the real table, and it is CHECKED to leave seven before the arm
//      runs. The host's own share is `requestMs` and it is printed.
//   6. THE COST at 8,001 instances, per job.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

// ---------------------------------------------------------------------------
// A mesh with a KNOWN level chain: three extra levels whose measured bounds are
// stated rather than derived, so every level boundary in this file is exact.
static const float kB1 = 0.004f, kB2 = 0.02f, kB3 = 0.09f;

static MeshData chainedMesh()
{
    const int N = 16;
    const float span = 1.0f;
    MeshData d;
    for (int z = 0; z <= N; ++z)
        for (int x = 0; x <= N; ++x) {
            const float fx = float(x) / float(N), fz = float(z) / float(N);
            d.positions.insert(d.positions.end(),
                               { (fx - 0.5f) * span, 0.0f, (fz - 0.5f) * span });
            d.normals.insert(d.normals.end(), { 0.0f, 1.0f, 0.0f });
        }
    auto build = [&](int step) {
        std::vector<unsigned> idx;
        for (int z = 0; z < N; z += step)
            for (int x = 0; x < N; x += step) {
                const unsigned a = unsigned(z * (N + 1) + x);
                const unsigned b = unsigned(z * (N + 1) + x + step);
                const unsigned c = unsigned((z + step) * (N + 1) + x + step);
                const unsigned e = unsigned((z + step) * (N + 1) + x);
                // WINDING: a,c,b and a,e,c — counter-clockwise seen from +Y,
                // which is the normal this mesh declares. The first version of
                // this fixture wound a,b,c (CCW from BELOW), so every triangle
                // faced the wrong way; Ogre's front face is CCW in clip space
                // and a one-sided datablock culls clockwise, so the occluding
                // wall built from it DREW NOTHING and the depth pyramid was
                // all-far. The cull then had nothing to occlude with, which read
                // exactly like a broken shader. (ATOM-SUBSTRATE-1 fix round.)
                idx.insert(idx.end(), { a, c, b, a, e, c });
            }
        return idx;
    };
    d.indices = build(1);
    d.lodIndices.push_back(build(2));
    d.lodIndices.push_back(build(4));
    d.lodIndices.push_back(build(8));
    d.lodBounds = { kB1, kB2, kB3 };
    d.lodErrors = { kB1, kB2, kB3 };
    return d;
}

// ---------------------------------------------------------------------------
// THE CPU REFERENCE. It reads the same table the shader does and applies the
// same rules, in the same order — the point of the suite is the shader, so the
// reference must not be a second interpretation of the scene.
struct Reference {
    std::vector<unsigned> survivors;   ///< slots, ascending
    std::vector<unsigned> levels;      ///< per slot
};

static Reference cpuCull(const std::vector<GpuSceneEntry> &table, const GpuCullRequest &r,
                         const std::vector<float> &bounds, unsigned levelsAvailable)
{
    Reference out;
    out.levels.assign(table.size(), 0u);
    for (size_t i = 0; i < table.size(); ++i) {
        const GpuSceneEntry &e = table[i];
        if (e.meshIndex == 0xFFFFFFFFu) continue;
        if ((e.flags & r.flagsRequired) != r.flagsRequired) continue;
        if ((e.flags & r.flagsForbidden) != 0u) continue;
        bool in = true;
        for (int p = 0; p < 6 && in; ++p) {
            const float *pl = &r.planes[p * 4];
            const float px = pl[0] >= 0.0f ? e.boundsMax[0] : e.boundsMin[0];
            const float py = pl[1] >= 0.0f ? e.boundsMax[1] : e.boundsMin[1];
            const float pz = pl[2] >= 0.0f ? e.boundsMax[2] : e.boundsMin[2];
            if (pl[0] * px + pl[1] * py + pl[2] * pz + pl[3] < 0.0f) in = false;
        }
        if (!in) continue;
        out.survivors.push_back(unsigned(i));
        if (!(r.pixelTolerance > 0.0f)) continue;
        // THE LEVEL, in the currency's words, from the same quantities the
        // shader derives: the AABB centre, the instance's largest axis scale out
        // of the world rows, and the mesh's local sphere radius.
        float scale = 0.0f;
        for (int row = 0; row < 3; ++row) {
            const float *w = &e.world[row * 4];
            scale = std::max(scale, std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]));
        }
        if (!(scale > 0.0f)) continue;
        const float cx = 0.5f * (e.boundsMin[0] + e.boundsMax[0]);
        const float cy = 0.5f * (e.boundsMin[1] + e.boundsMax[1]);
        const float cz = 0.5f * (e.boundsMin[2] + e.boundsMax[2]);
        const float dx = cx - r.eye[0], dy = cy - r.eye[1], dz = cz - r.eye[2];
        // The local radius the shader reads is the mesh AABB's half-diagonal —
        // which is exactly what `Mesh::_setBoundingSphereRadius(aabb.getRadius())`
        // stores for a mesh this engine builds. The fixture's mesh is a 1 m
        // square in xz with no height, so it is stated here rather than read
        // back through an accessor the boundary does not have.
        const float localRadius = 0.5f * std::sqrt(2.0f);
        const float radius = localRadius * scale;
        const float dist = std::max(0.0f, std::sqrt(dx * dx + dy * dy + dz * dz) - radius);
        const float allowed = allowedWorldError(
            r.pixelTolerance,
            sampleFootprintPerspective(dist, r.projScaleY, r.viewportHeight), scale);
        out.levels[i] = unsigned(lodLevelForWorldError(bounds, allowed, levelsAvailable));
    }
    return out;
}

static std::vector<GpuSceneEntry> readTable(Scene *scene, unsigned count)
{
    std::vector<GpuSceneEntry> out;
    out.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        GpuSceneEntry e;
        if (!scene->gpuSceneEntry(i, e)) break;
        out.push_back(e);
    }
    return out;
}

/// The view half from the engine (every convention in one place), plus this
/// arm's own predicates. `hzbLevels` is cleared: the arms that want the depth
/// test set it back explicitly, so no arm gets it by accident.
static GpuCullRequest requestFor(Engine *e, View *view, float tolerance, unsigned mode)
{
    GpuCullRequest r;
    if (!e->fillCullView(view, r)) std::printf("FAIL: fillCullView refused\n");
    r.pixelTolerance = tolerance;
    r.mode = mode;
    r.hzbLevels = 0u;
    return r;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-engine-gpu-cull-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("cull", 640u, 480u, Colour(0, 0, 0));
    Scene *scene = e->createScene("cull");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    PostFxDesc fx;
    fx.hzb = true;                  // the suite is the one view that asks
    view->setPostFx(fx);
    scene->setAmbient(Colour(0.4f, 0.4f, 0.4f), Colour(0.2f, 0.2f, 0.2f));

    // ---- the field: 4,096 instances of three meshes over 200 m -------------
    const MeshId mesh = scene->createMesh(chainedMesh());
    PbrParams p; p.albedo = Colour(0.7f, 0.7f, 0.7f); p.roughness = 0.6f;
    const MaterialId mat = scene->createPbrMaterial(p);
    const int side = 64;            // 64 x 64 = 4,096
    for (int i = 0; i < side * side; ++i) {
        const NodeId n = scene->createNode();
        if (!scene->attachMesh(n, mesh, mat)) { std::printf("FAIL: attach %d\n", i); return 1; }
        const float fx2 = float(i % side) / float(side - 1) - 0.5f;
        const float fz = float(i / side) / float(side - 1) - 0.5f;
        // A PER-INSTANCE JITTER IN z, so that no two instances share a depth. It
        // is what makes a "leave exactly N of them" cut expressible at all: on
        // the unjittered grid 64 instances shared every z value, so no plane
        // could ever leave seven and the arm below quietly asserted zero.
        enginetest::setNodePosition(scene, n,
                                    Vec3(fx2 * 200.0f, 0.0f, fz * 200.0f + float(i) * 1.0e-3f));
        const float s = 1.0f + 3.0f * float((i * 37) % 7) / 6.0f;   // 1..4, deterministic
        enginetest::setNodeScale(scene, n, Vec3(s, s, s));
    }
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.35f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 12.0f, 60.0f), Vec3(0.0f, 0.0f, -20.0f));
    for (int i = 0; i < 3; ++i) e->renderOneFrame();

    GpuSceneStatus gs = scene->gpuSceneStatus();
    std::printf("\n== the table ==\n   live %d, %u slots, %u mesh entries\n", gs.live ? 1 : 0,
                gs.slotCount, gs.meshEntries);
    CHECK(gs.live && gs.slotCount == unsigned(side * side), "the table holds every instance");

    const std::vector<GpuSceneEntry> table = readTable(scene, gs.slotCount);
    CHECK(table.size() == gs.slotCount, "the table reads back");
    const std::vector<float> bounds = { kB1, kB2, kB3 };

    // ---- 1. mode 0 against a CPU cull -------------------------------------
    std::printf("\n== mode 0: the survivor set ==\n");
    GpuCullRequest r0 = requestFor(e, view, 0.0f, 0u);
    r0.flagsRequired = 1u;          // kGpuVisible
    GpuCullResult res;
    if (!e->gpuCull(scene, view, r0, /*readBack=*/true, res)) {
        std::printf("FAIL: gpuCull refused (supported=%d): %s\n", res.supported ? 1 : 0,
                    e->takeLastError().c_str());
        return 1;
    }
    Reference ref = cpuCull(table, r0, bounds, 4u);
    std::vector<unsigned> got = res.survivorSlots;
    std::sort(got.begin(), got.end());
    std::printf("   GPU %u survivors of %u, CPU %zu; request %.4f ms\n", res.survivors,
                res.instances, ref.survivors.size(), res.requestMs);
    CHECK(res.survivors == unsigned(got.size()), "the count the GPU wrote is the list's length");
    CHECK(got == ref.survivors, "the survivor set is the CPU cull's, instance by instance");
    CHECK(res.requestMs < 1.0, "the host's whole share of a request is under a millisecond");

    // ---- 3. mode 1: the levels --------------------------------------------
    std::printf("\n== mode 1: the level rule ==\n");
    unsigned levelMismatch = 0, histogram[8] = {};
    for (float tol : { 0.5f, 1.0f, 2.0f }) {
        GpuCullRequest r1 = requestFor(e, view, tol, 1u);
        r1.flagsRequired = 1u;
        GpuCullResult r1out;
        if (!e->gpuCull(scene, view, r1, true, r1out)) {
            std::printf("FAIL: mode 1 at tolerance %.2f: %s\n", tol,
                        e->takeLastError().c_str());
            ++failures;
            continue;
        }
        Reference ref1 = cpuCull(table, r1, bounds, 4u);
        for (unsigned slot : r1out.survivorSlots) {
            if (slot >= r1out.levels.size()) continue;
            if (r1out.levels[slot] != ref1.levels[slot]) ++levelMismatch;
            if (r1out.levels[slot] < 8u) ++histogram[r1out.levels[slot]];
        }
        std::printf("   tolerance %.2f px: %u survivors\n", tol, r1out.survivors);
    }
    std::printf("   levels taken over the three tolerances: %u/%u/%u/%u (0..3)\n", histogram[0],
                histogram[1], histogram[2], histogram[3]);
    CHECK(levelMismatch == 0, "every level equals the CPU rule's answer");
    CHECK(histogram[1] + histogram[2] + histogram[3] > 0,
          "the tolerance actually selects coarser levels (the arm is not trivially level 0)");

    // ---- 4. mode 2: the draw commands -------------------------------------
    std::printf("\n== mode 2: the draw commands ==\n");
    GpuCullRequest r2 = requestFor(e, view, 1.0f, 2u);
    r2.flagsRequired = 1u;
    GpuCullResult res2;
    if (!e->gpuCull(scene, view, r2, true, res2)) {
        std::printf("FAIL: mode 2: %s\n", e->takeLastError().c_str());
        ++failures;
    } else {
        std::printf("   %u survivors, %u draws, %u indirect groups (ceil(%u/64) = %u), "
                    "%u multi-submesh\n",
                    res2.survivors, res2.draws, res2.indirectGroups, res2.survivors,
                    (res2.survivors + 63u) / 64u, res2.multiSubmeshSurvivors);
        CHECK(res2.draws == res2.survivors, "one draw command per survivor");
        CHECK(res2.indirectGroups == (res2.survivors + 63u) / 64u,
              "job 2 wrote job 3's thread-group count");
        unsigned bad = 0, badInstance = 0, badFirst = 0;
        for (unsigned i = 0; i < res2.survivors && (i + 1) * 5u <= res2.drawCommands.size(); ++i) {
            const unsigned *c = &res2.drawCommands[i * 5u];
            const unsigned slot = res2.survivorSlots[i];
            const unsigned level = res2.levels[slot];
            // The index range this level should name, taken from the level the
            // fixture built: level L draws (N/step)^2 * 6 indices with step 2^L.
            const unsigned step = 1u << level;
            const unsigned quads = (16u / step) * (16u / step);
            if (c[0] != quads * 6u) ++bad;
            if (c[1] != 1u) ++badInstance;
            if (c[4] != slot) ++badFirst;
        }
        CHECK(bad == 0, "every command's index count is the level's own");
        CHECK(badInstance == 0, "every command draws exactly one instance");
        CHECK(badFirst == 0, "firstInstance is the survivor's slot in the table");
    }

    // ---- 2. the HZB mode --------------------------------------------------
    // A WALL, off centre vertically, with a row of instances hidden behind it.
    std::printf("\n== the HZB mode: an occluding wall ==\n");
    {
        View *hv = e->createOffscreenView("cullhzb", 640u, 480u, Colour(0, 0, 0));
        Scene *hs = e->createScene("cullhzb");
        hv->setScene(hs);
        PostFxDesc hfx; hfx.hzb = true; hv->setPostFx(hfx);
        hs->setAmbient(Colour(0.4f, 0.4f, 0.4f), Colour(0.2f, 0.2f, 0.2f));
        const MeshId hm = hs->createMesh(chainedMesh());
        const MaterialId hmat = hs->createPbrMaterial(p);
        // THE WALL: a flat instance stood up across the view at z = -10, 60 m
        // wide and 6 m TALL, its top edge at y = 0. Sixty metres tall (the first
        // version's scale) would have covered the "exposed" row as well, so that
        // row's assertion would have gone red the moment the depth read worked.
        const NodeId wall = hs->createNode();
        hs->attachMesh(wall, hm, hmat);
        hs->setNodeTransform(wall, Vec3(0.0f, -3.0f, -10.0f),
                             Quat(0.7071068f, 0.0f, 0.0f, 0.7071068f),
                             Vec3(60.0f, 1.0f, 6.0f));
        // Twelve instances well behind the wall and LOW, so they project inside
        // its rectangle (the camera sits at y = 0 looking level, so a hidden row
        // at y = -6 at z = -40 falls in the wall's lower half)...
        std::vector<NodeId> hidden, exposed;
        for (int i = 0; i < 12; ++i) {
            const NodeId n = hs->createNode();
            hs->attachMesh(n, hm, hmat);
            enginetest::setNodePosition(hs, n, Vec3(float(i - 6) * 2.0f, -6.0f, -40.0f));
            enginetest::setNodeScale(hs, n, Vec3(1.5f, 1.5f, 1.5f));
            hidden.push_back(n);
        }
        // ...and twelve well ABOVE its top edge, which it cannot cover.
        for (int i = 0; i < 12; ++i) {
            const NodeId n = hs->createNode();
            hs->attachMesh(n, hm, hmat);
            enginetest::setNodePosition(hs, n, Vec3(float(i - 6) * 2.0f, 12.0f, -40.0f));
            enginetest::setNodeScale(hs, n, Vec3(1.5f, 1.5f, 1.5f));
            exposed.push_back(n);
        }
        enginetest::addDirectionalLight(hs, Vec3(-0.4f, -1.0f, -0.35f), 3.0f);
        enginetest::testCameraLookAt(hv, Vec3(0.0f, 0.0f, 10.0f), Vec3(0.0f, 0.0f, -40.0f));
        for (int i = 0; i < 4; ++i) e->renderOneFrame();

        HzbStatus hst;
        CHECK(e->hzbStatus(hv, hst) && hst.built, "the fixture's view builds a pyramid");
        CHECK(hst.primed, "and a presented frame has written it (not an uninitialised allocation)");
        CHECK(hst.farthest, "the cull's pyramid keeps the FARTHEST depth of each footprint");

        // THE FIXTURE IS PROVEN BEFORE THE CULL IS JUDGED. Mip 0 is the depth
        // buffer: the wall must be IN it. Its centre projects to the middle
        // column, a little below the middle row (the wall spans y = -6..0 with
        // the camera level at y = 0), and that texel must read nearer than the
        // far plane — otherwise the wall drew nothing and every assertion below
        // would be about a fixture, not about a shader.
        std::vector<float> mip0;
        unsigned m0w = 0, m0h = 0;
        CHECK(e->readHzbLevel(hv, 0u, mip0, m0w, m0h) && m0w && m0h, "mip 0 reads back");
        const float farValue = hst.reverseDepth ? 0.0f : 1.0f;
        unsigned nearTexels = 0;
        if (!mip0.empty()) {
            // A band across the middle of the lower half, where the wall is.
            for (unsigned y = m0h / 2u; y < (m0h * 3u) / 4u; ++y)
                for (unsigned x = m0w / 4u; x < (m0w * 3u) / 4u; ++x)
                    if (std::fabs(mip0[size_t(y) * m0w + x] - farValue) > 1e-6f) ++nearTexels;
        }
        std::printf("   mip 0: %u of %u texels under the wall's band are nearer than the far "
                    "plane\n", nearTexels, (m0h / 4u) * (m0w / 2u));
        CHECK(nearTexels > 1000u, "THE WALL IS IN THE DEPTH BUFFER (the fixture really occludes)");

        const unsigned n = hs->gpuSceneStatus().slotCount;
        const std::vector<GpuSceneEntry> htable = readTable(hs, n);
        auto slotsOf = [&](const std::vector<NodeId> &ids) {
            std::vector<unsigned> out;
            for (unsigned i = 0; i < htable.size(); ++i)
                if (std::find(ids.begin(), ids.end(), NodeId(htable[i].nodeId)) != ids.end())
                    out.push_back(i);
            return out;
        };
        const std::vector<unsigned> hiddenSlots = slotsOf(hidden);
        const std::vector<unsigned> exposedSlots = slotsOf(exposed);

        GpuCullRequest rf = requestFor(e, hv, 0.0f, 0u);
        rf.flagsRequired = 1u;
        GpuCullResult frustumOnly, withHzb;
        e->gpuCull(hs, hv, rf, true, frustumOnly);
        GpuCullRequest rh = rf;
        rh.hzbLevels = hst.levels;
        const bool okH = e->gpuCull(hs, hv, rh, true, withHzb);
        if (!okH) { std::printf("FAIL: the HZB cull: %s\n", e->takeLastError().c_str()); ++failures; }
        auto has = [](const GpuCullResult &r, unsigned slot) {
            return std::find(r.survivorSlots.begin(), r.survivorSlots.end(), slot) !=
                   r.survivorSlots.end();
        };
        unsigned hiddenIn = 0, hiddenGone = 0, exposedIn = 0, exposedGone = 0;
        for (unsigned s : hiddenSlots) (has(withHzb, s) ? hiddenIn : hiddenGone)++;
        for (unsigned s : exposedSlots) (has(withHzb, s) ? exposedIn : exposedGone)++;
        std::printf("   frustum only %u survivors; with %u HZB levels %u\n", frustumOnly.survivors,
                    hst.levels, withHzb.survivors);
        std::printf("   behind the wall: %u culled, %u kept (of %zu); above it: %u kept, %u culled "
                    "(of %zu)\n", hiddenGone, hiddenIn, hiddenSlots.size(), exposedIn,
                    exposedGone, exposedSlots.size());
        CHECK(!hiddenSlots.empty() && !exposedSlots.empty(), "both groups are in the table");
        CHECK(okH, "a request carrying the pyramid runs to an answer");
        CHECK(withHzb.survivors < frustumOnly.survivors, "the depth test removed something");
        CHECK(hiddenIn == 0u, "every instance fully behind the wall is gone");
        // THE ERROR A HIERARCHICAL TEST MAY NEVER MAKE. Wrong in this direction
        // loses visible geometry; wrong the other way merely fails to save work.
        CHECK(exposedGone == 0u, "and no instance the wall does not cover is ever rejected");
    }

    // ---- 5. the chain is GPU-driven: 0, 7 and 4,096 survivors --------------
    // The three sizes compute.indirect_dispatch asserted, now with real work
    // behind them: the count comes out of a compute shader and the next dispatch
    // is sized from it. A forbidden flag nobody has keeps everything; requiring
    // a flag nobody has keeps nothing; and a plane pushed through the field
    // leaves an arbitrary count no host arithmetic could have predicted.
    std::printf("\n== the GPU sizes the chain (0, 7, 4096 survivors) ==\n");
    {
        struct Arm { const char *what; unsigned expect; GpuCullRequest req; };
        std::vector<Arm> arms;
        {
            GpuCullRequest r = requestFor(e, view, 1.0f, 2u);
            r.flagsRequired = 1u << 30;     // a bit no instance carries
            arms.push_back({ "no survivor", 0u, r });
        }
        {
            GpuCullRequest r = requestFor(e, view, 1.0f, 2u);
            // SEVEN, and seven exactly. Every plane infinitely permissive except
            // one half-space `z >= C`, with C placed BETWEEN the 7th and 8th
            // largest positive-vertex z in the table — the positive vertex of
            // that plane being each instance's own boundsMax.z. Sorting the
            // table's real values is what makes the count a FACT about the
            // fixture rather than a hope: a bisection on a field where 64
            // instances share each depth can only ever land on a multiple of 64,
            // which is how this arm came to assert 0 and call it 7.
            for (int i = 0; i < 24; ++i) r.planes[i] = 0.0f;
            for (int i = 0; i < 6; ++i) r.planes[i * 4 + 3] = 1.0e9f;
            std::vector<float> zs;
            zs.reserve(table.size());
            for (const GpuSceneEntry &en : table) zs.push_back(en.boundsMax[2]);
            std::sort(zs.begin(), zs.end(), std::greater<float>());
            const float cut = 0.5f * (zs[6] + zs[7]);
            r.planes[0] = 0.0f; r.planes[1] = 0.0f; r.planes[2] = 1.0f; r.planes[3] = -cut;
            const Reference cutRef = cpuCull(table, r, bounds, 4u);
            std::printf("   the cut at z >= %.6f (between %.6f and %.6f) leaves %zu on the CPU\n",
                        cut, zs[7], zs[6], cutRef.survivors.size());
            CHECK(zs[6] > zs[7], "the 7th and 8th depths are distinct (the jitter did its job)");
            CHECK(cutRef.survivors.size() == 7u, "the cut really does leave SEVEN instances");
            arms.push_back({ "a seven-instance cut", 7u, r });
        }
        {
            GpuCullRequest r = requestFor(e, view, 1.0f, 2u);
            for (int i = 0; i < 24; ++i) r.planes[i] = 0.0f;
            for (int i = 0; i < 6; ++i) r.planes[i * 4 + 3] = 1.0e9f;
            arms.push_back({ "the whole table", 4096u, r });
        }
        for (Arm &a : arms) {
            GpuCullResult ar;
            if (!e->gpuCull(scene, view, a.req, true, ar)) {
                std::printf("FAIL: arm '%s': %s\n", a.what, e->takeLastError().c_str());
                ++failures;
                continue;
            }
            char msg[192];
            std::printf("   %s: %u survivors, %u draws, %u groups\n", a.what, ar.survivors,
                        ar.draws, ar.indirectGroups);
            std::snprintf(msg, sizeof(msg), "'%s' — the GPU counted %u (expected %u)", a.what,
                          ar.survivors, a.expect);
            CHECK(ar.survivors == a.expect, msg);
            std::snprintf(msg, sizeof(msg), "'%s' — the indirect dispatch ran ceil(%u/64) groups",
                          a.what, a.expect);
            CHECK(ar.indirectGroups == (a.expect + 63u) / 64u, msg);
            std::snprintf(msg, sizeof(msg), "'%s' — one command per survivor", a.what);
            CHECK(ar.draws == a.expect, msg);
        }
    }

    // ---- 6. the cost at 8,001 instances ------------------------------------
    std::printf("\n== the cost at 8,001 instances ==\n");
    {
        for (int i = int(side * side); i < 8001; ++i) {
            const NodeId n = scene->createNode();
            scene->attachMesh(n, mesh, mat);
            enginetest::setNodePosition(
                scene, n, Vec3(float((i * 17) % 400) - 200.0f, 0.0f, float((i * 31) % 400) - 200.0f));
        }
        for (int i = 0; i < 3; ++i) e->renderOneFrame();
        const unsigned n = scene->gpuSceneStatus().slotCount;
        GpuCullRequest r = requestFor(e, view, 1.0f, 2u);
        r.flagsRequired = 1u;
        r.measureIterations = 64u;
        GpuCullResult big;
        if (!e->gpuCull(scene, view, r, false, big)) {
            std::printf("FAIL: the 8,001 arm: %s\n", e->takeLastError().c_str());
            ++failures;
        } else {
            std::printf("   %u instances, %u survivors\n", n, big.survivors);
            std::printf("   test %.4f ms · compact %.4f ms · draws %.4f ms (slope of 64 "
                        "dispatches)\n", big.testMs, big.compactMs, big.drawsMs);
            std::printf("   the host's share: %.4f ms\n", big.requestMs);
            CHECK(n >= 8001u, "the table grew to 8,001 instances");
            CHECK(big.requestMs < 1.0, "the request stays a small write at 8,001 instances");
        }
    }

    // ---- the ray level's refits (AT-A8r's hysteresis) ----------------------
    std::printf("\n== the ray level's 2x hysteresis ==\n");
    {
        GpuSceneStatus st0 = scene->gpuSceneStatus();
        // A minute of a camera flying in and out, at the engine's fixed 1/60 s.
        for (int f = 0; f < 3600; ++f) {
            const float t = float(f) / 3600.0f;
            const float z = 20.0f + 120.0f * (0.5f - 0.5f * std::cos(t * 6.2831853f * 4.0f));
            enginetest::testCameraLookAt(view, Vec3(0.0f, 12.0f, z), Vec3(0.0f, 0.0f, -20.0f));
            e->renderOneFrame();
        }
        GpuSceneStatus st1 = scene->gpuSceneStatus();
        const unsigned long long evals = st1.rayLevelEvals - st0.rayLevelEvals;
        const unsigned long long refits = st1.rayLevelRefits - st0.rayLevelRefits;
        const unsigned long long walks = st1.rayLevelWalks - st0.rayLevelWalks;
        std::printf("   3,600 frames (one minute at 1/60 s), %u instances, four dolly cycles:\n"
                    "   %llu walks, %llu re-evaluations, %llu REFITS\n",
                    st1.slotCount, walks, evals, refits);
        CHECK(walks > 0ull, "the pass ran while the camera moved");
        CHECK(refits <= evals, "a refit is a re-evaluation whose answer changed");
        // The claim the design makes is that the band keeps refits RARE — far
        // below one per instance per frame, which is what a rule without
        // hysteresis would cost on a dolly.
        const double perFrame = double(refits) / 3600.0;
        std::printf("   = %.2f refits per frame, %.5f per instance per frame\n", perFrame,
                    perFrame / double(std::max(1u, st1.slotCount)));
        CHECK(perFrame < double(st1.slotCount) * 0.01,
              "the 2x band keeps refits under 1% of the instances per frame");
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
