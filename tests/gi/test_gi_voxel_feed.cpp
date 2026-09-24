// gi.voxel_feed — THE VOXELISER IS FED ON THE DEVICE, AND WHAT IT IS FED IS
// EXACTLY WHAT THE CPU WOULD HAVE CHOSEN (ATOM P4b, SPECS/atom/
// A5b_VOXELISER_FEED_AND_FAR_BLAS_DESIGN.md §2-§3).
//
// The cascade chain's voxelisers no longer take items. Three compute jobs
// (Jahshaka/VoxelGatherCount, Scan, Write) read every instance of the GPU scene
// — transform, bounds, flags, mesh, material word — apply the predicates the CPU
// walk used to apply, cull each 2,001-index PARTITION of the chosen level against
// the cascade's box, and write the voxeliser's records; the same jobs count what
// they wrote into a small readout that giStatus reports. This suite computes the
// same answer on the CPU, from the fixture it built, and holds the device to it.
//
// THE CASES:
//   1. PER CASCADE, THE DEVICE'S COUNTS ARE THE CPU REFERENCE: `attached` (every
//      predicate: the GI channel and rule 2's half-cell size floor), `items`
//      (attached AND at least one partition reaches the box), `voxelRecords`
//      (one per (instance, partition) whose world box reaches the cascade's box —
//      the PER-PARTITION cull, which a relief mesh of 13 partitions straddling
//      cascade 0's face makes partial), `voxelTriangles` and `voxelLevels` over
//      the attach set, and `voxelOverflow` 0.
//   2. RULE 2 IS APPLIED PER CASCADE: a 0.1 m cube is kept by the two cascades
//      whose cells it can fill half of and declined by the two it cannot; a
//      0.5 m cube by three and not the outermost.
//   3. A HIDE LEAVES THE NEXT GATHER BY ITSELF: nothing holds an item set any
//      more, so a hidden object is gone from the counts at the rebuild its
//      dirty box buys, with no set compare anywhere.
//   4. NO UPLOAD IN THE REBUILD: a from-scratch chain rebuild of a STILL scene
//      copies NOTHING into the GPU scene's tables (`GpuSceneStatus::copyRuns`
//      unchanged) — the rows, partitions and material words are already there,
//      and the feed is written on the device.
//
// Its own binary like every GI suite.
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
// The voxeliser's partition size (VctVoxelizer::kIndicesPerPartition) and rule 2's
// factor (OgreGi.cpp kCascadeSubVoxelFactor) — pinned here, like cascade_lod pins
// its fraction, so a change to either is a visible edit of this suite.
static const unsigned kPartitionIndices = 2001u;
static const float kSubVoxelFactor = 0.5f;

struct Box { float mn[3], mx[3]; };

/// One instance of the fixture as the CPU sees it: its world partitions (boxes and
/// triangle counts) and its whole world box.
struct Inst {
    std::string name;
    NodeId node = 0;
    std::vector<Box> parts;
    std::vector<unsigned> partTris;
    Box world;
    bool visible = true;
};

static Box boxOf(const MeshData &d, size_t firstIndex, size_t numIndices,
                 const Vec3 &pos, const Vec3 &scale)
{
    Box b;
    for (int a = 0; a < 3; ++a) { b.mn[a] = 1e30f; b.mx[a] = -1e30f; }
    for (size_t i = firstIndex; i < firstIndex + numIndices; ++i) {
        const unsigned v = d.indices[i];
        const float p[3] = { d.positions[v * 3 + 0] * scale.x + pos.x,
                             d.positions[v * 3 + 1] * scale.y + pos.y,
                             d.positions[v * 3 + 2] * scale.z + pos.z };
        for (int a = 0; a < 3; ++a) { b.mn[a] = std::min(b.mn[a], p[a]); b.mx[a] = std::max(b.mx[a], p[a]); }
    }
    return b;
}

static Inst describe(const std::string &name, NodeId node, const MeshData &d,
                     const Vec3 &pos, const Vec3 &scale)
{
    Inst in;
    in.name = name;
    in.node = node;
    const size_t n = d.indices.size();
    for (size_t first = 0; first < n; first += kPartitionIndices) {
        const size_t num = std::min(n - first, size_t(kPartitionIndices));
        in.parts.push_back(boxOf(d, first, num, pos, scale));
        in.partTris.push_back(unsigned(num / 3));
    }
    in.world = boxOf(d, 0, n, pos, scale);
    return in;
}

// The shader's own test: skip when any min is past the box's max or any max short
// of its min. A partition within `margin` of a face is AMBIGUOUS (the AABB job's
// float rounding may land it either side), and the reference counts it as a range.
enum Reach { Out, In, Edge };
static Reach reaches(const Box &b, const Box &cell, float margin)
{
    bool strictlyOut = false, surelyIn = true;
    for (int a = 0; a < 3; ++a) {
        if (b.mn[a] > cell.mx[a] + margin || b.mx[a] < cell.mn[a] - margin) strictlyOut = true;
        if (b.mn[a] > cell.mx[a] - margin || b.mx[a] < cell.mn[a] + margin) surelyIn = false;
    }
    if (strictlyOut) return Out;
    return surelyIn ? In : Edge;
}

struct Expect {
    int attached = 0, itemsLo = 0, itemsHi = 0;
    long long recordsLo = 0, recordsHi = 0, tris = 0, parts = 0;
};

static Expect reference(const std::vector<Inst> &scene, const GiStatus::CascadeStatus &c)
{
    Expect e;
    const Box cell = { { c.centre.x - c.halfSize, c.centre.y - c.halfSize, c.centre.z - c.halfSize },
                       { c.centre.x + c.halfSize, c.centre.y + c.halfSize, c.centre.z + c.halfSize } };
    const float minExtent = c.cell * kSubVoxelFactor;
    const float margin = 1e-3f;
    for (const Inst &in : scene) {
        if (!in.visible) continue;
        float ext = 0.0f;
        for (int a = 0; a < 3; ++a) ext = std::max(ext, in.world.mx[a] - in.world.mn[a]);
        if (ext < minExtent) continue;                          // rule 2
        ++e.attached;
        bool anyIn = false, anyEdge = false;
        for (size_t p = 0; p < in.parts.size(); ++p) {
            e.tris += in.partTris[p];
            ++e.parts;
            const Reach r = reaches(in.parts[p], cell, margin);
            if (r == In) { ++e.recordsLo; ++e.recordsHi; anyIn = true; }
            else if (r == Edge) { ++e.recordsHi; anyEdge = true; }
        }
        if (anyIn) { ++e.itemsLo; ++e.itemsHi; }
        else if (anyEdge) ++e.itemsHi;
    }
    return e;
}

// ---------------------------------------------------------------------------
// A RELIEF of 64 x 64 quads over 8 x 8 m: 8,192 triangles, 24,576 indices, so
// THIRTEEN partitions of 2,001 indices — strips of ~5 rows along z, which is what
// lets a cascade face cut through it.
static MeshData reliefMesh()
{
    const int N = 64;
    const float span = 8.0f;
    MeshData d;
    for (int z = 0; z <= N; ++z)
        for (int x = 0; x <= N; ++x) {
            const float fx = float(x) / float(N), fz = float(z) / float(N);
            d.positions.insert(d.positions.end(),
                               { (fx - 0.5f) * span,
                                 0.15f * std::sin(fx * 6.2831853f) * std::cos(fz * 6.2831853f),
                                 (fz - 0.5f) * span });
            d.normals.insert(d.normals.end(), { 0.0f, 1.0f, 0.0f });
        }
    for (int z = 0; z < N; ++z)
        for (int x = 0; x < N; ++x) {
            const unsigned a = unsigned(z * (N + 1) + x), b = a + 1u;
            const unsigned c = unsigned((z + 1) * (N + 1) + x + 1), e = c - 1u;
            d.indices.insert(d.indices.end(), { a, b, c, a, c, e });
        }
    return d;
}

static GiParams chainGi(GiQuality q)
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = q;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;
    gi.cascades = true;
    return gi;
}

static std::string hist(const std::vector<int> &h)
{
    std::string s = "{";
    for (size_t i = 0; i < h.size(); ++i) { if (i) s += ","; s += std::to_string(h[i]); }
    return s + "}";
}

static void judge(const char *tag, const GiStatus &st, const std::vector<Inst> &scene)
{
    for (size_t i = 0; i < st.cascades.size(); ++i) {
        const GiStatus::CascadeStatus &c = st.cascades[i];
        const Expect e = reference(scene, c);
        std::printf("   [%s] cascade %zu: cell %.3f half %.1f | device attached %d items %d records %lld "
                    "tris %lld levels %s overflow %lld | cpu attached %d items %d..%d records %lld..%lld "
                    "tris %lld parts %lld\n",
                    tag, i, c.cell, c.halfSize, c.attached, c.items, c.voxelRecords, c.voxelTriangles,
                    hist(c.voxelLevels).c_str(), c.voxelOverflow, e.attached, e.itemsLo, e.itemsHi,
                    e.recordsLo, e.recordsHi, e.tris, e.parts);
        const std::string at = std::string("[") + tag + "] cascade " + std::to_string(i) + ": ";
        CHECK(c.attached == e.attached, (at + "the attach set is the CPU's (" +
              std::to_string(c.attached) + " == " + std::to_string(e.attached) + ")").c_str());
        CHECK(c.items >= e.itemsLo && c.items <= e.itemsHi,
              (at + "the enclosed set is the CPU's (" + std::to_string(c.items) + ")").c_str());
        CHECK(c.voxelRecords >= e.recordsLo && c.voxelRecords <= e.recordsHi,
              (at + "ONE RECORD PER PARTITION THAT REACHES THE BOX (" +
               std::to_string(c.voxelRecords) + " in " + std::to_string(e.recordsLo) + ".." +
               std::to_string(e.recordsHi) + ")").c_str());
        CHECK(c.voxelTriangles == e.tris, (at + "the triangle reading is the attach set's (" +
              std::to_string(c.voxelTriangles) + " == " + std::to_string(e.tris) + ")").c_str());
        CHECK(c.voxelLevels.size() == 1u && c.voxelLevels[0] == int(e.parts),
              (at + "every attached partition at level 0 " + hist(c.voxelLevels)).c_str());
        CHECK(c.voxelOverflow == 0, (at + "and nothing was dropped for capacity").c_str());
    }
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-voxel-feed-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("vfeed", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("vfeed");
    view->setScene(scene);
    scene->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.3f, 0.3f, 0.3f));

    std::vector<Inst> fixture;
    const MeshData cube = enginetest::unitCubeMesh();
    const auto addCube = [&](const char *name, const Vec3 &pos, const Vec3 &scale, const Colour &col) {
        const NodeId n = enginetest::addTestCube(scene, col, 0.0f, 0.8f);
        enginetest::setNodePosition(scene, n, pos);
        enginetest::setNodeScale(scene, n, scale);
        fixture.push_back(describe(name, n, cube, pos, scale));
        return n;
    };
    addCube("ground", Vec3(0.0f, -0.55f, 0.0f), Vec3(200.0f, 0.1f, 200.0f), Colour(0.8f, 0.8f, 0.8f));
    // Small cubes near the camera (inside cascade 0) and far away (outside it).
    const NodeId nearSmall = addCube("small-near", Vec3(1.0f, 0.3f, 11.0f), Vec3(0.1f, 0.1f, 0.1f),
                                     Colour(0.9f, 0.2f, 0.2f));
    addCube("small-far", Vec3(-20.0f, 0.3f, -20.0f), Vec3(0.1f, 0.1f, 0.1f), Colour(0.2f, 0.9f, 0.2f));
    addCube("half-near", Vec3(-2.0f, 0.5f, 13.0f), Vec3(0.5f, 0.5f, 0.5f), Colour(0.2f, 0.2f, 0.9f));
    addCube("half-far", Vec3(40.0f, 0.5f, -30.0f), Vec3(0.5f, 0.5f, 0.5f), Colour(0.9f, 0.9f, 0.2f));
    addCube("metre-mid", Vec3(12.0f, 0.5f, 0.0f), Vec3(1.0f, 1.0f, 1.0f), Colour(0.9f, 0.5f, 0.2f));

    // THE RELIEF straddles cascade 0's faces in x and z, so its thirteen partitions
    // split between inside and outside.
    const MeshData relief = reliefMesh();
    const MeshId reliefMeshId = scene->createMesh(relief);
    PbrParams rp; rp.albedo = Colour(0.7f, 0.7f, 0.7f); rp.roughness = 0.8f;
    const MaterialId reliefMat = scene->createPbrMaterial(rp);
    const NodeId reliefNode = scene->createNode();
    CHECK(reliefMeshId && reliefMat && scene->attachMesh(reliefNode, reliefMeshId, reliefMat),
          "the relief (8,192 triangles, 13 partitions) is attached");
    const Vec3 reliefPos(4.0f, 0.5f, 9.0f);
    enginetest::setNodePosition(scene, reliefNode, reliefPos);
    fixture.push_back(describe("relief", reliefNode, relief, reliefPos, Vec3(1.0f, 1.0f, 1.0f)));
    CHECK(fixture.back().parts.size() == 13u, "the CPU splits it into 13 partitions too");

    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -1.0f, -0.35f), 3.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 4.0f, 12.0f), Vec3(0.0f, 0.5f, 0.0f));
    for (int i = 0; i < 2; ++i) e->renderOneFrame();

    // ---- 1 + 2. THE CHAIN, AND THE REFERENCE ------------------------------------
    CHECK(scene->setGlobalIllumination(chainGi(GiQuality::High)), "the High chain built");
    for (int i = 0; i < 8; ++i) e->renderOneFrame();
    GiStatus st = scene->giStatus();
    CHECK(st.cascades.size() == 4, "the chain has four cascades");
    if (st.cascades.size() != 4) { std::printf("FAIL: no chain\n"); return 1; }
    judge("high", st, fixture);
    // The relief really is cut by cascade 0: some of its partitions in, some out.
    {
        const Expect all = reference(fixture, st.cascades[0]);
        std::vector<Inst> reliefOnly(1, fixture.back());
        const Expect r = reference(reliefOnly, st.cascades[0]);
        CHECK(r.recordsHi > 0 && r.recordsLo < 13,
              ("the relief is PARTLY inside cascade 0 (" + std::to_string(r.recordsLo) + ".." +
               std::to_string(r.recordsHi) + " of 13 partitions) - the per-partition cull is exercised")
                  .c_str());
        (void)all;
    }
    // Rule 2 by name: the 0.1 m cube counts in cascades 0 and 1 only.
    {
        std::vector<Inst> one;
        for (const Inst &in : fixture) if (in.name == "small-near" || in.name == "small-far") one.push_back(in);
        bool ok = true;
        for (size_t i = 0; i < 4; ++i) {
            const Expect x = reference(one, st.cascades[i]);
            ok = ok && ((i < 2) ? x.attached == 2 : x.attached == 0);
        }
        CHECK(ok, "RULE 2: a 0.1 m cube is kept by cascades 0-1 and declined by 2-3 (in the reference "
                  "the device was held to)");
    }

    // ---- 3. A HIDE LEAVES THE NEXT GATHER BY ITSELF -----------------------------
    scene->setNodeVisible(nearSmall, false);
    for (Inst &in : fixture) if (in.node == nearSmall) in.visible = false;
    for (int i = 0; i < 8; ++i) e->renderOneFrame();
    GiStatus hidden = scene->giStatus();
    CHECK(hidden.cascades.size() == 4, "the chain survived the hide");
    if (hidden.cascades.size() == 4) {
        CHECK(hidden.cascades[0].rebuilds > st.cascades[0].rebuilds,
              "cascade 0 re-voxelised for the hide (its dirty box reached it)");
        judge("hidden", hidden, fixture);
    }
    scene->setNodeVisible(nearSmall, true);
    for (Inst &in : fixture) in.visible = true;
    for (int i = 0; i < 8; ++i) e->renderOneFrame();

    // ---- 4. NO UPLOAD IN THE REBUILD --------------------------------------------
    for (int i = 0; i < 4; ++i) e->renderOneFrame();        // settle: nothing owed
    const GpuSceneStatus before = scene->gpuSceneStatus();
    const unsigned long long rebuildsBefore = scene->giStatus().rebuilds;
    CHECK(before.live, "the GPU scene is live");
    CHECK(scene->setGlobalIllumination(chainGi(GiQuality::Medium)),
          "a from-scratch rebuild of the chain (the Medium tier) over the still scene");
    for (int i = 0; i < 8; ++i) e->renderOneFrame();
    const GpuSceneStatus after = scene->gpuSceneStatus();
    const GiStatus med = scene->giStatus();
    std::printf("   gpu scene copy runs %llu -> %llu, writes %llu -> %llu, rebuilds %llu -> %llu\n",
                before.copyRuns, after.copyRuns, before.writes, after.writes,
                rebuildsBefore, med.rebuilds);
    CHECK(med.rebuilds > rebuildsBefore, "the chain really was rebuilt from scratch");
    CHECK(after.copyRuns == before.copyRuns && after.writes == before.writes,
          "NO UPLOAD: the rebuild copied nothing into the GPU scene's tables");
    judge("medium", med, fixture);

    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
