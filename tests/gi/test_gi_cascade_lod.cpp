// gi.cascade_lod — ATOM STAGE 1'S CONSUMER: A CASCADE VOXELISES THE BAKED LEVEL
// THAT FITS ITS OWN CELL (SPECS/NANITE_SPEC.md §7 stage 1's hand-off row,
// SPECS/PHOTON_SPEC.md §7 E2; ogre-patch 0064).
//
// THE CLAIM UNDER TEST, in one sentence: the coarse cascades of Photon's chain
// hand the voxeliser a SIMPLIFIED mesh — the coarsest baked level whose error is
// below half that cascade's own cell — and the fine cascades hand it the
// authored one, which is less geometry for the same voxels.
//
// WHY THE FIXTURE IS HAND-BUILT AND NOT AN IMPORT. The consumer needs exactly
// two things from the bake: N index lists over ONE vertex buffer, and a
// world-space error per level. This suite provides them directly (a grid mesh
// sampled at four densities), which makes the level boundaries EXACT arithmetic
// against the tier's cell table instead of whatever meshoptimizer happened to
// achieve on a model — and it is the engine boundary's own contract
// (MeshData::lodIndices / lodBounds), so it tests the shipped path.
// `scripting.e2e.atom_lods` covers the real bake end to end.
//
// THE CASES:
//   1. THE LEVEL PER CASCADE IS THE ARITHMETIC. Four cascades, cells
//      0.078 / 0.156 / 0.469 / 1.875 m at the High tier, times the MEASURED LOD
//      fraction (1/256 — ATOM-3 A2, `kCascadeLodCellFraction`, whose picture
//      evidence is beside the constant): 0.000305 / 0.00061 / 0.00183 /
//      0.00732 m. The fixture's errors are 0.0005 / 0.001 / 0.004, so the
//      levels must be 0 / 1 / 2 / 3. (Until ATOM-3 the fraction was a HALF of
//      the cell and the fixture's errors were 100x these; the arithmetic under
//      test is the same, the constant it is done with is not.)
//   2. THE MESH'S UNITS ARE NOT THE WORLD'S. A second mesh with the same data,
//      instanced at scale 4, has four times the world-space error per level, so
//      the outer cascade takes a FINER level for it — two levels in one
//      histogram. It needs its OWN Ogre mesh to do so, which is case 2b:
//   2b. THE LEVEL BELONGS TO THE MESH, NOT TO THE ITEM (round-1 F1). The
//      voxeliser converts a mesh's geometry once for every item that shares it
//      (ogre-patch 0064), so two instances of ONE mesh asking for different
//      levels are both voxelised at the FINER — and the histogram has to report
//      that, because a reading that claims a level the dispatch never spent is
//      worse than no reading.
//   3. A MESH WITH NO CHAIN IS UNTOUCHED: the ground cube is level 0 in every
//      cascade, which is what every scene built from primitives is today.
//   4. IT IS CHEAPER, AND THE A/B IS IN ONE PROCESS: with
//      `GiParams::cascadeVoxelLod` false the same chain reports the authored
//      triangle count in every cascade; with it true the outer cascades report
//      less. Triangles, not milliseconds — the raster dispatch is sized by the
//      index count, and a timing assertion on a shared box is a flake.
//   5. THE LOD BIAS MUST NOT REACH IT. `Scene::setLodBias` is a dial over what
//      is DRAWN; a voxel volume that followed it would make the bounce depend on
//      a debugging control. The levels are identical at bias 0 and bias 200.
//   6. AND THE PICTURE STILL RENDERS: the chain is bound and the scene is lit,
//      so a "cheaper" that voxelised nothing would not pass.
//
// Its own binary like every GI suite (the voxel lighting binds process-wide).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
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

static const unsigned kSize = 128;

// ---------------------------------------------------------------------------
// THE FIXTURE: an 8x8 m grid of 64x64 quads with a gentle sine relief, plus
// three coarser index lists over THE SAME VERTICES (32x32, 16x16, 8x8) — which
// is the rule ATOM stage 1's bake obeys and the reason a level change costs no
// draw call. The errors are the deviations a consumer would measure, stated
// rather than derived so the level boundaries are exact.
static const float kErr1 = 0.0005f, kErr2 = 0.001f, kErr3 = 0.004f;

static MeshData reliefMesh()
{
    const int N = 64;                 // quads per side at level 0
    const float span = 8.0f;
    MeshData d;
    for (int z = 0; z <= N; ++z) {
        for (int x = 0; x <= N; ++x) {
            const float fx = float(x) / float(N), fz = float(z) / float(N);
            const float px = (fx - 0.5f) * span, pz = (fz - 0.5f) * span;
            const float py = 0.15f * std::sin(fx * 6.2831853f) * std::cos(fz * 6.2831853f);
            d.positions.insert(d.positions.end(), { px, py, pz });
            d.normals.insert(d.normals.end(), { 0.0f, 1.0f, 0.0f });
        }
    }
    auto build = [&](int step) {
        std::vector<unsigned> idx;
        for (int z = 0; z < N; z += step)
            for (int x = 0; x < N; x += step) {
                const unsigned a = unsigned(z * (N + 1) + x);
                const unsigned b = unsigned(z * (N + 1) + x + step);
                const unsigned c = unsigned((z + step) * (N + 1) + x + step);
                const unsigned e = unsigned((z + step) * (N + 1) + x);
                idx.insert(idx.end(), { a, b, c, a, c, e });
            }
        return idx;
    };
    d.indices = build(1);
    d.lodIndices.push_back(build(2));
    d.lodIndices.push_back(build(4));
    d.lodIndices.push_back(build(8));
    // BOTH ARRAYS, and `lodBounds` is the one that is READ (ATOM P1's AT-A5):
    // the cascade's rule selects on the MEASURED two-sided distance, and the
    // simplifier's own number is carried beside it as a diagnostic nothing reads.
    // The fixture's three lengths ARE the bounds — they are what the suite's
    // arithmetic is written against — and `lodErrors` is given the same values so
    // the fixture states a chain that a real bake could have produced.
    d.lodBounds = { kErr1, kErr2, kErr3 };
    d.lodErrors = { kErr1, kErr2, kErr3 };
    return d;
}

static GiParams cascadeGi(bool lods)
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;
    gi.cascades = true;
    gi.cascadeVoxelLod = lods;
    return gi;
}

static void render(Engine *e, int frames = 1)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

/// The level the histogram says MOST items took, and the highest one present.
static int maxLevel(const std::vector<int> &hist)
{
    int m = 0;
    for (size_t i = 0; i < hist.size(); ++i) if (hist[i] > 0) m = int(i);
    return m;
}
static int countAt(const std::vector<int> &hist, size_t level)
{
    return level < hist.size() ? hist[level] : 0;
}
static std::string histText(const std::vector<int> &h)
{
    std::string s = "{";
    for (size_t i = 0; i < h.size(); ++i) { if (i) s += ","; s += std::to_string(h[i]); }
    return s + "}";
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-cascade-lod-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("clod", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("clod");
    view->setScene(scene);
    scene->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.3f, 0.3f, 0.3f));

    // A ground cube with NO chain — the "every scene today" control.
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.55f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(200.0f, 0.1f, 200.0f));

    // THE RELIEF, FIVE INSTANCES OVER THREE MESHES, and which mesh each one
    // holds is the whole of cases 2 and 2b:
    //   mesh A — `small` (scale 1) AND `twin` (scale 4). They ask for different
    //            levels, because the baked error is in MESH units and the 4x
    //            instance's world-space error is four times as large; one mesh
    //            entry carries one level, so BOTH are voxelised at the finer.
    //   mesh B — `solo` (scale 1), alone: free to take the unscaled answer.
    //   mesh C — `big` (scale 4), alone: free to take the scaled one, which is
    //            finer. B and C in one histogram are the scale term reaching
    //            the DISPATCH and not merely the request.
    const MeshId meshA = scene->createMesh(reliefMesh());
    const MeshId meshB = scene->createMesh(reliefMesh());
    const MeshId meshC = scene->createMesh(reliefMesh());
    CHECK(meshA && meshB && meshC && meshA != meshB && meshB != meshC,
          "three relief meshes, each with its three extra levels, were created");
    PbrParams p; p.albedo = Colour(0.85f, 0.2f, 0.2f); p.metalness = 0.0f; p.roughness = 0.8f;
    const MaterialId mat = scene->createPbrMaterial(p);
    const NodeId small = scene->createNode();
    CHECK(scene->attachMesh(small, meshA, mat), "mesh A at the authored scale is attached");
    enginetest::setNodePosition(scene, small, Vec3(0.0f, 0.5f, 0.0f));
    const NodeId twin = scene->createNode();
    CHECK(scene->attachMesh(twin, meshA, mat), "and a 4x instance SHARING mesh A");
    enginetest::setNodePosition(scene, twin, Vec3(-24.0f, 2.0f, 0.0f));
    enginetest::setNodeScale(scene, twin, Vec3(4.0f, 4.0f, 4.0f));
    const NodeId solo = scene->createNode();
    CHECK(scene->attachMesh(solo, meshB, mat), "mesh B at the authored scale is attached");
    enginetest::setNodePosition(scene, solo, Vec3(12.0f, 0.5f, 0.0f));
    const NodeId big = scene->createNode();
    CHECK(scene->attachMesh(big, meshC, mat), "mesh C at 4x is attached");
    enginetest::setNodePosition(scene, big, Vec3(24.0f, 2.0f, 0.0f));
    enginetest::setNodeScale(scene, big, Vec3(4.0f, 4.0f, 4.0f));

    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.35f), 3.0f);

    enginetest::testCameraLookAt(view, Vec3(0.0f, 4.0f, 12.0f), Vec3(0.0f, 0.5f, 0.0f));
    render(e, 2);

    // =====================================================================
    // THE CHAIN, WITH THE LEVELS ON
    // =====================================================================
    CHECK(scene->setGlobalIllumination(cascadeGi(true)), "the four-cascade chain built");
    render(e, 8);
    GiStatus st = scene->giStatus();
    CHECK(st.cascades.size() == 4, ("the chain has four cascades (" +
          std::to_string(st.cascades.size()) + ")").c_str());
    if (st.cascades.size() != 4) { std::printf("FAIL: no chain, nothing else can be judged\n"); return 1; }
    CHECK(st.vctBound, "and it is bound (the picture is coming from it)");

    for (size_t i = 0; i < st.cascades.size(); ++i)
        std::printf("   cascade %zu: cell %.4f m, half %.1f m, items %d, attached %d, "
                    "levels %s, voxel triangles %lld\n",
                    i, st.cascades[i].cell, st.cascades[i].halfSize, st.cascades[i].items,
                    st.cascades[i].attached, histText(st.cascades[i].lodLevels).c_str(),
                    st.cascades[i].voxelTriangles);

    // ---- 1. THE LEVEL PER CASCADE IS THE ARITHMETIC --------------------
    // The rule is `error < cell * kCascadeLodCellFraction`
    // (lodLevelForWorldError at OgreScene::cascadeVoxelLod), so the expected
    // level of the unscaled instance is computable from the cascade's own
    // reported cell. THE FRACTION IS PINNED HERE ON PURPOSE: it is a measured
    // number (ATOM-3 A2), and a suite that re-derived it from the engine could
    // not tell a re-measurement from a typo.
    // (ATOM-3-FIX re-measured it on two further fixtures and left it exactly
    // here — what changed is the CLAIM the constant carries, not its value;
    // the comment beside it in OgreGi.cpp and FINDINGS.md §6 say why.)
    const float kLodFraction = 1.0f / 256.0f;
    for (size_t i = 0; i < st.cascades.size(); ++i) {
        const float budget = st.cascades[i].cell * kLodFraction;
        int expect = 0;
        const float errs[3] = { kErr1, kErr2, kErr3 };
        for (int L = 0; L < 3; ++L) { if (!(errs[L] < budget)) break; expect = L + 1; }
        // Mesh B is the unscaled, unshared instance — the one whose level is
        // exactly this arithmetic; the histogram must contain it.
        const bool present = countAt(st.cascades[i].lodLevels, size_t(expect)) > 0;
        CHECK(present, ("cascade " + std::to_string(i) + " voxelises a level-" +
                        std::to_string(expect) + " item (budget " +
                        std::to_string(budget) + " m), " +
                        histText(st.cascades[i].lodLevels)).c_str());
    }
    CHECK(maxLevel(st.cascades[0].lodLevels) == 0,
          "CASCADE 0 TAKES THE AUTHORED GEOMETRY — nothing simplified at the finest cell");
    CHECK(maxLevel(st.cascades.back().lodLevels) == 3,
          ("THE OUTERMOST CASCADE TAKES THE COARSEST LEVEL THE CHAIN HAS (max level " +
           std::to_string(maxLevel(st.cascades.back().lodLevels)) + ")").c_str());
    CHECK(maxLevel(st.cascades[1].lodLevels) == 1 && maxLevel(st.cascades[2].lodLevels) == 2,
          "and the two middle cascades take exactly the levels their cells allow");

    // ---- 2. THE SCALE TERM REACHES THE DISPATCH -------------------------
    // Mesh C is instanced at 4x and carries 4x the world-space error per level,
    // so in the outermost cascade (budget 0.00732 m) it takes level 2 while mesh
    // B, unscaled and alone, takes level 3: two levels in ONE histogram.
    const std::vector<int> &outer = st.cascades.back().lodLevels;
    CHECK(countAt(outer, 2) >= 1 && countAt(outer, 3) == 1,
          ("A SCALED INSTANCE TAKES A FINER LEVEL — one item at the coarse end, the "
           "scaled ones below it: " + histText(outer)).c_str());

    // ---- 2b. THE LEVEL BELONGS TO THE MESH (round-1 F1) ------------------
    // Mesh A is held by TWO items that ask for different levels (scale 1 and
    // scale 4). The voxeliser converts a mesh once, finest request wins, so
    // BOTH are voxelised at the 4x instance's level — the same level mesh C
    // took. The histogram therefore has exactly one item at the coarse end
    // (mesh B's) and THREE at the fine one (mesh A twice plus mesh C), and NOT
    // two levels for mesh A.
    CHECK(countAt(outer, 2) == 3,
          ("THE FINEST REQUEST WINS FOR A SHARED MESH — both of mesh A's items and mesh C "
           "at one level: " + histText(outer)).c_str());
    // And it is what the voxeliser HOLDS, not what was asked — and since
    // ogre-patch 0088 that sentence is literally true rather than nearly true
    // (ATOM inventory row AT-A12). `voxelTriangles` used to be a CPU walk of each
    // mesh's VAOs at the level just requested, i.e. this case asserted the host's
    // own prediction against the host's own arithmetic; it is now
    // `VctVoxelizer::getQueuedIndexCount()`, the sum of the `numIndices` that size
    // the raster dispatches. The number below is UNCHANGED by that switch, which
    // is the evidence the patch is worth having: the reading and the prediction
    // agree exactly on this fixture, so from here on a disagreement means the
    // voxeliser really did bind something else.
    // (the ground is 12, each relief level is 8192 / 2048 / 512 / 128)
    const long long expectOuter = 12 + 3 * 512 + 128;
    CHECK(st.cascades.back().voxelTriangles == expectOuter,
          ("and the triangle reading is that histogram, exactly (" +
           std::to_string(st.cascades.back().voxelTriangles) + " == " +
           std::to_string(expectOuter) + ")").c_str());

    // ---- 3. A MESH WITH NO CHAIN IS UNTOUCHED --------------------------
    // The ground cube has one VAO; it is counted at level 0 in every cascade,
    // which is why every cascade's histogram has a non-zero slot 0.
    bool groundEverywhere = true;
    for (const auto &c : st.cascades) groundEverywhere = groundEverywhere && countAt(c.lodLevels, 0) > 0;
    CHECK(groundEverywhere, "a mesh with NO chain stays at level 0 in every cascade");

    // ---- 5. THE LOD BIAS MUST NOT REACH THE VOXELS ---------------------
    std::vector<std::vector<int>> levelsAtBiasOne;
    std::vector<long long> trianglesAtBiasOne;
    for (const auto &c : st.cascades) {
        levelsAtBiasOne.push_back(c.lodLevels);
        trianglesAtBiasOne.push_back(c.voxelTriangles);
    }
    scene->setLodBias(200.0f);      // every drawn instance drops to its coarsest level
    render(e, 2);
    CHECK(scene->setGlobalIllumination(cascadeGi(true)), "the chain is rebuilt at LOD bias 200");
    render(e, 8);
    GiStatus biased = scene->giStatus();
    bool biasMoved = false;
    for (size_t i = 0; i < biased.cascades.size() && i < levelsAtBiasOne.size(); ++i)
        if (biased.cascades[i].lodLevels != levelsAtBiasOne[i] ||
            biased.cascades[i].voxelTriangles != trianglesAtBiasOne[i]) biasMoved = true;
    CHECK(!biasMoved, "THE LOD BIAS DOES NOT MOVE THE VOXELISER'S PICK (a dial over the picture)");
    scene->setLodBias(0.0f);        // and 0 pins the drawn level, which must not move it either
    render(e, 2);
    CHECK(scene->setGlobalIllumination(cascadeGi(true)), "the chain is rebuilt at LOD bias 0");
    render(e, 8);
    GiStatus pinned = scene->giStatus();
    bool pinnedMoved = false;
    for (size_t i = 0; i < pinned.cascades.size() && i < levelsAtBiasOne.size(); ++i)
        if (pinned.cascades[i].lodLevels != levelsAtBiasOne[i]) pinnedMoved = true;
    CHECK(!pinnedMoved, "and neither does bias 0, which pins what is DRAWN to the finest level");
    scene->setLodBias(1.0f);
    render(e, 2);

    // ---- 4. THE A/B, IN ONE PROCESS ------------------------------------
    CHECK(scene->setGlobalIllumination(cascadeGi(false)), "the same chain with the levels OFF");
    render(e, 8);
    GiStatus off = scene->giStatus();
    CHECK(off.cascades.size() == st.cascades.size(), "the same four cascades");
    bool allZero = true;
    for (const auto &c : off.cascades) allZero = allZero && maxLevel(c.lodLevels) == 0;
    CHECK(allZero, "with the levels off EVERY cascade voxelises the authored geometry");
    long long onOuter = st.cascades.back().voxelTriangles;
    long long offOuter = off.cascades.back().voxelTriangles;
    std::printf("   outer cascade triangles: %lld with the chain, %lld without (%.1f%%)\n",
                onOuter, offOuter, offOuter ? 100.0 * double(onOuter) / double(offOuter) : 0.0);
    CHECK(onOuter < offOuter,
          ("THE OUTER CASCADE VOXELISES LESS GEOMETRY (" + std::to_string(onOuter) + " < " +
           std::to_string(offOuter) + " triangles)").c_str());
    CHECK(onOuter * 4 <= offOuter,
          "and it is a REDUCTION worth having — at most a quarter of the authored count");
    CHECK(off.cascades.back().voxelTriangles == 12 + 4 * 8192,
          ("with the levels off the outer cascade is the authored total (" +
           std::to_string(off.cascades.back().voxelTriangles) + ")").c_str());
    CHECK(off.cascadeVoxelLod == false && st.cascadeVoxelLod == true,
          "and giStatus NAMES the arm it is on (cascadeVoxelLod)");
    CHECK(st.cascades[0].voxelTriangles == off.cascades[0].voxelTriangles,
          "while cascade 0 is UNCHANGED, byte for byte, by the whole feature");

    // ---- 6. AND THE PICTURE IS STILL THERE ------------------------------
    CHECK(scene->setGlobalIllumination(cascadeGi(true)), "back to the levels on");
    render(e, 8);
    Image img;
    CHECK(view->readPixels(img), "the view reads back");
    float sum = 0.0f;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Colour c = img.at(x, y);
            sum += 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
        }
    const float mean = sum / float(kSize * kSize);
    CHECK(mean > 0.02f, ("the scene is lit with the chain up (mean luminance " +
                         std::to_string(mean) + ")").c_str());

    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
