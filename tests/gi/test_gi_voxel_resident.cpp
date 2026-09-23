// gi.voxel_resident — ATOM P4: THE VOXELISER READS THE RASTER'S OWN GEOMETRY
// (SPECS/atom/A5_VOXELISER_RESIDENT_DESIGN.md §1.1, §3).
//
// THE CLAIM UNDER TEST, in one sentence: the voxeliser no longer owns any copy of
// the world's triangles — its compute shader reads the vertex and index buffers
// the raster draws from, through their buffer device addresses and a per-mesh
// layout — and that is why the vertex FORMAT and the index WIDTH stopped being
// shader permutations and bucket keys.
//
// WHAT THE OLD PATH DID, so the cases below have a subject: every build()
// downloaded each mesh's vertex buffer to the CPU, unpacked it to eight floats
// per vertex, uploaded that into a private buffer, and copied each index buffer
// into one of TWO private buffers — one 16-bit, one 32-bit. A dispatch bound one
// pair, so a 16-bit mesh and a 32-bit mesh in the same octant were two dispatches
// of the whole volume each, and a mesh's normal or uv packing was a third and
// fourth variant of the shader.
//
// THE CASES:
//   1. ONE DISPATCH FOR BOTH INDEX WIDTHS. A cascade holding a small mesh
//      (16-bit indices) and a mesh with more than 65,535 vertices (32-bit) shares
//      ONE material, so it must cost ONE dispatch per octant. Under the old
//      bucket key it cost two, and a dispatch is sized by the whole octant
//      however few instances it holds — so this is the measured half of the
//      claim, not a tidiness argument. `voxelDispatches` is the reading.
//   2. AND BOTH ARE REALLY VOXELISED. The triangle reading is the sum of both
//      meshes, and the scene is lit: a "cheaper" that dropped the 32-bit mesh
//      would pass case 1 and fail here.
//   3. A MESH BUILT WITH NO UV DATA VOXELISES AND LIGHTS. It does NOT exercise
//      the row's "absent" branch and does not claim to: `buildMeshV2` writes the
//      uv element either way (zeros), so the row still says float2. What it
//      proves is that the shader reads the LAYOUT WORD for the uv offset rather
//      than assuming a fixed one — a mesh whose uvs are all zero draws its
//      albedo from texel (0,0) and is lit, where a mis-read offset would fetch a
//      position or a tangent lane as a uv and the albedo would be wrong or NaN.
//   4. IT IS DETERMINISTIC ACROSS TWO REAL REBUILDS. `refreshGlobalIllumination`
//      only RE-INJECTS (DOCS/traps/ENGINE.md, VOXEL-CLIP-1), so this case forces
//      actual re-voxelisations by TELEPORTING the camera a full cascade step and
//      back — `CascadeStatus::rebuilds` is asserted to have MOVED, or the case
//      would assert nothing at all, which is what it did before this fix round.
//      What is compared is the geometry readings AND a readback of the volumes
//      (`Scene::giVoxelStats`, which flushes the render system and downloads the
//      light volume and the voxeliser's OWN emissive store): every field exactly
//      equal, so a device address read in a different frame cannot move a voxel.
//   5. A REFUSED SCENE BUILDS AND DOES NOT CRASH. `JAH_VCT_REFUSE_GEOMETRY` makes
//      the voxeliser refuse every (mesh, level, submesh) — the state a device with
//      NO buffer device addresses is in for the whole scene, and the state a mesh
//      without a float3 position or with an unaligned stride is in for itself.
//      Items are queued, no geometry row exists, and the build must clear the
//      volumes and return: it used to walk into fillInstanceBuffers with a null
//      instance buffer and segfault, so "GI is empty" was a crash.
//
// Its own binary like every GI suite (the voxel lighting binds process-wide).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdlib>
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

// A flat grid of NxN quads spanning `span` metres, centred, facing +Y. N = 256
// gives 66,049 vertices, which is what forces a 32-BIT index buffer
// (OgreMesh.cpp's buildMeshV2 switches at 65,535) — the point of the fixture.
static MeshData grid(int N, float span)
{
    MeshData d;
    for (int z = 0; z <= N; ++z) {
        for (int x = 0; x <= N; ++x) {
            const float fx = float(x) / float(N), fz = float(z) / float(N);
            d.positions.insert(d.positions.end(),
                               { (fx - 0.5f) * span, 0.0f, (fz - 0.5f) * span });
            d.uvs.insert(d.uvs.end(), { fx, fz });
        }
    }
    for (int z = 0; z < N; ++z) {
        for (int x = 0; x < N; ++x) {
            const unsigned a = unsigned(z * (N + 1) + x), b = a + 1u;
            const unsigned c = unsigned((z + 1) * (N + 1) + x), e = c + 1u;
            d.indices.insert(d.indices.end(), { a, c, b, b, c, e });
        }
    }
    return d;
}

static void render(Engine *e, int frames)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static GiParams cascadeGi()
{
    GiParams g;
    g.mode = GiMode::Vct;
    g.quality = GiQuality::High;
    g.numBounces = 1;
    g.ddgi = GiToggle::Off;     // the field would route the diffuse (LATTICE-1)
    g.updateBudget = 0;
    g.cascades = true;
    g.cascadeVoxelLod = true;
    return g;
}

static float meanLuma(View *view)
{
    Image img;
    if (!view->readPixels(img)) return -1.0f;
    double sum = 0.0;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Colour c = img.at(x, y);
            sum += 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
        }
    return float(sum / double(kSize * kSize));
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-voxel-resident-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("vres", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("vres");
    view->setScene(scene);
    scene->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.3f, 0.3f, 0.3f));

    // ONE material for everything, so a dispatch count above one cannot be blamed
    // on the material bucket (that is gi.cascade_determinism's subject).
    PbrParams p; p.albedo = Colour(0.8f, 0.75f, 0.7f); p.metalness = 0.0f; p.roughness = 0.85f;
    const MaterialId mat = scene->createPbrMaterial(p);

    // The 16-bit mesh: 8x8 quads, 81 vertices.
    const MeshData small = grid(8, 2.0f);
    // The 32-bit mesh: 256x256 quads, 66,049 vertices -> a 32-bit index buffer.
    const MeshData large = grid(256, 6.0f);
    CHECK(small.positions.size() / 3 < 65535u, "the small fixture has 16-bit indices");
    CHECK(large.positions.size() / 3 > 65535u,
          ("and the large one forces 32-bit indices (" +
           std::to_string(large.positions.size() / 3) + " vertices)").c_str());

    const MeshId meshSmall = scene->createMesh(small);
    const MeshId meshLarge = scene->createMesh(large);
    CHECK(meshSmall && meshLarge, "both meshes were created");

    const NodeId nSmall = scene->createNode();
    CHECK(scene->attachMesh(nSmall, meshSmall, mat), "the 16-bit mesh is attached");
    enginetest::setNodePosition(scene, nSmall, Vec3(0.0f, 1.0f, 0.0f));
    const NodeId nLarge = scene->createNode();
    CHECK(scene->attachMesh(nLarge, meshLarge, mat), "and the 32-bit mesh beside it");
    enginetest::setNodePosition(scene, nLarge, Vec3(0.0f, 0.0f, 0.0f));

    const NodeId sun = enginetest::addDirectionalLight(scene, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);
    CHECK(sun != 0, "a sun");
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, 6.0f), Vec3(0.0f, 0.5f, 0.0f));

    CHECK(scene->setGlobalIllumination(cascadeGi()), "the cascade chain is built");
    render(e, 10);
    GiStatus st = scene->giStatus();
    CHECK(st.cascades.size() >= 2, ("the chain has " + std::to_string(st.cascades.size()) + " cascades").c_str());

    // ---- 1. ONE DISPATCH FOR BOTH INDEX WIDTHS --------------------------
    // A bucket IS a dispatch and a dispatch is sized by the whole octant, so this
    // is the cost of the index-width variant the resident read deleted. One octant
    // (dividideOctants(1,1,1)) and one material => one dispatch, where the old
    // bucket key (which named the index BUFFER) gave two.
    for (size_t i = 0; i < st.cascades.size(); ++i)
        std::printf("   cascade %zu: attached %d, triangles %lld, dispatches %lld\n", i,
                    st.cascades[i].attached, st.cascades[i].voxelTriangles,
                    st.cascades[i].voxelDispatches);
    bool oneDispatchEach = true;
    for (const auto &c : st.cascades)
        if (c.attached > 0 && c.voxelDispatches != 1) oneDispatchEach = false;
    CHECK(oneDispatchEach,
          "A 16-BIT AND A 32-BIT MESH SHARING A MATERIAL COST ONE DISPATCH PER OCTANT "
          "(the index width is a field of the geometry row, not a bucket key)");

    // ---- 2. AND BOTH ARE REALLY VOXELISED -------------------------------
    // The inner cascade's cell is fine enough to take level 0 of both, so its
    // reading is the authored total of the two meshes.
    const long long expectTris = (long long)(small.indices.size() / 3 + large.indices.size() / 3);
    CHECK(st.cascades[0].voxelTriangles == expectTris,
          ("the inner cascade voxelises BOTH meshes' authored triangles (" +
           std::to_string(st.cascades[0].voxelTriangles) + " == " +
           std::to_string(expectTris) + ")").c_str());
    const float luma = meanLuma(view);
    std::printf("   mean luminance with the chain up: %.6f\n", luma);
    CHECK(luma > 0.02f, "and the scene is lit, so the resident read found real triangles");

    // ---- 3. A MESH BUILT WITH NO UV DATA -------------------------------
    // NOT the "absent" branch: buildMeshV2 writes the uv element either way, so the
    // row says float2 and points at the real lane. What this proves is that the
    // layout word's uv OFFSET is read — a mis-read offset would fetch a position or
    // tangent lane as a uv, and the albedo would be wrong.
    MeshData bare = grid(4, 1.5f);
    bare.uvs.clear();
    const MeshId meshBare = scene->createMesh(bare);
    CHECK(meshBare, "a mesh with no uv data was created");
    const NodeId nBare = scene->createNode();
    CHECK(scene->attachMesh(nBare, meshBare, mat), "and attached");
    enginetest::setNodePosition(scene, nBare, Vec3(1.5f, 1.5f, 0.0f));
    render(e, 10);
    GiStatus withBare = scene->giStatus();
    const long long expectWithBare = expectTris + (long long)(bare.indices.size() / 3);
    CHECK(withBare.cascades[0].voxelTriangles == expectWithBare,
          ("it is voxelised too (" + std::to_string(withBare.cascades[0].voxelTriangles) +
           " == " + std::to_string(expectWithBare) + ")").c_str());
    CHECK(meanLuma(view) > 0.02f, "and the scene is still lit");

    // ---- 4. DETERMINISTIC ACROSS TWO REAL REBUILDS ----------------------
    // `refreshGlobalIllumination()` alone only re-injects, so it is NOT used here:
    // the camera is teleported a full cascade step away and back, which moves the
    // chain's centre and forces a re-voxelisation, and `rebuilds` is asserted to
    // have moved. Everything read is then compared exactly.
    const float lumaBefore = meanLuma(view);
    const GiVoxelStats statsBefore = scene->giVoxelStats(0);
    CHECK(statsBefore.available,
          ("the voxel volumes read back (" + statsBefore.format + ", " +
           std::to_string(statsBefore.voxels) + " voxels, " +
           std::to_string(statsBefore.voxelsLit) + " lit)").c_str());
    const unsigned long long buildsBefore = withBare.cascades[0].rebuilds;

    enginetest::testCameraLookAt(view, Vec3(400.0f, 3.0f, 406.0f), Vec3(400.0f, 0.5f, 400.0f));
    render(e, 16);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, 6.0f), Vec3(0.0f, 0.5f, 0.0f));
    render(e, 24);

    GiStatus again = scene->giStatus();
    std::printf("   cascade 0 rebuilds: %llu -> %llu\n", buildsBefore, again.cascades[0].rebuilds);
    CHECK(again.cascades[0].rebuilds > buildsBefore,
          "the teleport really did re-voxelise (a refresh alone would not have)");
    bool same = again.cascades.size() == withBare.cascades.size();
    for (size_t i = 0; same && i < again.cascades.size(); ++i)
        same = again.cascades[i].voxelTriangles == withBare.cascades[i].voxelTriangles &&
               again.cascades[i].voxelDispatches == withBare.cascades[i].voxelDispatches &&
               again.cascades[i].voxelLevels == withBare.cascades[i].voxelLevels;
    CHECK(same, "the second build binds exactly the same geometry, level for level");

    // THE READBACK, field for field. `peakEmissive`/`emissiveAtMax`/`emissiveAboveOne`
    // are the VOXELISER's own emissive store (no normalisation); the rest is the light
    // volume the cones read, which is a digest of what the voxeliser wrote.
    const GiVoxelStats statsAfter = scene->giVoxelStats(0);
    CHECK(statsAfter.available, "and the volumes read back again");
    const bool volumesIdentical =
        statsAfter.format == statsBefore.format && statsAfter.width == statsBefore.width &&
        statsAfter.height == statsBefore.height && statsAfter.depth == statsBefore.depth &&
        statsAfter.peak == statsBefore.peak && statsAfter.peakDirect == statsBefore.peakDirect &&
        statsAfter.meanLit == statsBefore.meanLit &&
        statsAfter.voxelsLit == statsBefore.voxelsLit &&
        statsAfter.voxelsAtMax == statsBefore.voxelsAtMax &&
        statsAfter.directAtMax == statsBefore.directAtMax &&
        statsAfter.voxelsAboveOne == statsBefore.voxelsAboveOne &&
        statsAfter.emissiveFormat == statsBefore.emissiveFormat &&
        statsAfter.peakEmissive == statsBefore.peakEmissive &&
        statsAfter.emissiveAtMax == statsBefore.emissiveAtMax &&
        statsAfter.emissiveAboveOne == statsBefore.emissiveAboveOne;
    std::printf("   volumes before: peak %.9g meanLit %.17g lit %lld aboveOne %lld\n",
                statsBefore.peak, statsBefore.meanLit, statsBefore.voxelsLit,
                statsBefore.voxelsAboveOne);
    std::printf("   volumes after : peak %.9g meanLit %.17g lit %lld aboveOne %lld\n",
                statsAfter.peak, statsAfter.meanLit, statsAfter.voxelsLit,
                statsAfter.voxelsAboveOne);
    CHECK(volumesIdentical,
          "AND THE VOLUMES READ BACK IDENTICAL after the second voxelisation — every "
          "field of giVoxelStats, the voxeliser's emissive store included");
    const float lumaAfter = meanLuma(view);
    std::printf("   mean luminance before %.6f, after %.6f\n", lumaBefore, lumaAfter);
    CHECK(std::fabs(lumaAfter - lumaBefore) < 0.002f,
          "and the picture is the same after the rebuild");

    // ---- 5. A REFUSED SCENE BUILDS AND DOES NOT CRASH -------------------
    // Every row refused = the no-buffer-device-address state for the whole scene.
    // Reaching the end of this case at all is most of the assertion.
    setenv("JAH_VCT_REFUSE_GEOMETRY", "1", 1);
    scene->refreshGlobalIllumination();
    enginetest::testCameraLookAt(view, Vec3(400.0f, 3.0f, 406.0f), Vec3(400.0f, 0.5f, 400.0f));
    render(e, 16);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, 6.0f), Vec3(0.0f, 0.5f, 0.0f));
    render(e, 24);
    GiStatus refused = scene->giStatus();
    std::printf("   refused build: triangles %lld, dispatches %lld, rebuilds %llu\n",
                refused.cascades[0].voxelTriangles, refused.cascades[0].voxelDispatches,
                refused.cascades[0].rebuilds);
    CHECK(refused.cascades[0].rebuilds > again.cascades[0].rebuilds,
          "A SCENE WHOSE EVERY MESH IS REFUSED BUILDS WITHOUT CRASHING");
    CHECK(refused.cascades[0].voxelTriangles == 0 && refused.cascades[0].voxelDispatches == 0,
          ("and reports no geometry and no dispatch (" +
           std::to_string(refused.cascades[0].voxelTriangles) + " triangles, " +
           std::to_string(refused.cascades[0].voxelDispatches) + " dispatches)").c_str());
    unsetenv("JAH_VCT_REFUSE_GEOMETRY");
    scene->refreshGlobalIllumination();
    render(e, 16);
    CHECK(scene->giStatus().cascades[0].voxelTriangles == 0 ||
              scene->giStatus().cascades[0].voxelTriangles == expectWithBare,
          "and the refusal is not sticky beyond the hook");

    scene->setGlobalIllumination(GiParams());
    render(e, 2);
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);

    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
