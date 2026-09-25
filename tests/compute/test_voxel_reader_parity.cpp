// engine.voxel_reader_parity — THE ONE VOXEL READER, A FRAGMENT STAGE AND A
// COMPUTE STAGE, THE SAME ANSWER TO THE BIT (PHOTON-READER-1,
// SPECS/photon/B1_ONE_READER_DESIGN.md section 3).
//
// WHY. The cone march and the voxel read are written ONCE
// (irisgl/engine/src/rayquery/include/jah_voxel_{sample,march}.glsl) and four
// consumers run them: the pixel shader's cones (a fragment stage), the bounce
// job and the irradiance field's generation job (compute stages) and the ray
// hit. One source is what makes "the field and the cones read one radiance
// field the same way" possible; this suite is what makes it TRUE - the same
// cones through the same bound chain, marched by a fragment program and by an
// Hlms compute job both built from those files (Engine::voxelReaderParity), and
// every float compared bit for bit.
//
// WHAT IT COVERS. Two chains: the shipped anisotropic four-cascade chain (High)
// and the isotropic single volume (Low). Every walk variant the consumers use:
// the pixel's diffuse (the six-cone and the four-cone mip step), the specular
// walk, the field's free-space ray (the hop measured along the ray, no bias).
// Starts inside cascade 0 and outside it (the hop), directions down the axes,
// on the diagonals and in between, every cascade and four mips for the point
// reads; the specular empty-space skip on the single volume. And the RAY HIT's
// read of a point against ONE MARCH STEP onto it (the march at zero length).
//
// NOT VACUOUS: a harness returning zeros twice would pass a bit comparison. So
// the march must have met geometry (some opacity), carried radiance (some
// colour), crossed at least one hop, and the reads must be non-zero somewhere.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
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

static Vec3 unit(float x, float y, float z)
{
    const float l = std::sqrt(x * x + y * y + z * z);
    return Vec3(x / l, y / l, z / l);
}

/// 64 cones over every variant the consumers use.
static std::vector<VoxelReaderCone> makeCones(unsigned cascades, bool withSdf)
{
    const Vec3 dirs[8] = { unit(0, 1, 0),  unit(1, 0, 0),   unit(0, 0, -1),   unit(1, 1, 1),
                           unit(-1, 1, 0.3f), unit(0.2f, -1, 0.1f), unit(-0.6f, 0.3f, -0.74f),
                           unit(0.05f, 0.01f, 1) };
    const Vec3 starts[4] = { Vec3(0.5f, 0.52f, 0.5f), Vec3(0.31f, 0.47f, 0.66f),
                             Vec3(0.5f, 0.505f, 0.9f), Vec3(-0.08f, 0.5f, 0.5f) };
    // flags: pixel diffuse, pixel diffuse (four cones), specular, field ray
    const unsigned flags[4] = { 0u, 0u, 1u, 0u };
    const float tans[4] = { 0.577f, 0.98269f, 0.12f, 0.0437f };
    std::vector<VoxelReaderCone> cones;
    for (unsigned i = 0; i < 64u; ++i) {
        VoxelReaderCone k;
        k.posLS = starts[i % 4u];
        k.dirLS = dirs[(i / 4u) % 8u];
        const unsigned v = (i / 2u) % 4u;
        k.flags = flags[v];
        // The specular empty-space skip exists on a single volume only (as in the
        // pixel shader): there the specular variant takes it.
        if (withSdf && v == 2u) k.flags |= 2u;
        k.tanHalfAngle = tans[v];
        // The field's probe ray starts in free space: no origin surface (no normal).
        k.biasDirLS = (v == 3u) ? Vec3(0, 0, 0) : unit(0, 1, 0);
        k.cascade = i % (cascades ? cascades : 1u);
        k.lod = float((i / 3u) % 4u) * 0.75f;
        cones.push_back(k);
    }
    return cones;
}

static void measureChain(Engine *e, Scene *scene, const char *label, unsigned cascades,
                         bool aniso)
{
    const std::vector<VoxelReaderCone> cones = makeCones(cascades, !aniso);
    std::vector<VoxelReaderAnswer> frag, comp;
    const bool ok = e->voxelReaderParity(scene, cones, frag, comp);
    if (!ok) std::printf("   harness: %s\n", e->takeLastError().c_str());
    CHECK(ok && frag.size() == cones.size() && comp.size() == cones.size(),
          (std::string(label) + ": the harness ran both stages").c_str());
    if (!ok || frag.size() != cones.size() || comp.size() != cones.size()) return;

    unsigned differ = 0, hitDiffer = 0, hops = 0, opaque = 0, lit = 0, readsLit = 0;
    unsigned hitCompared = 0, hitNoStep = 0, hitFineIso = 0;
    for (size_t i = 0; i < cones.size(); ++i) {
        const VoxelReaderAnswer &f = frag[i];
        const VoxelReaderAnswer &c = comp[i];
        const bool same = std::memcmp(&f, &c, sizeof(VoxelReaderAnswer)) == 0;
        if (!same) {
            if (differ < 4u)
                std::printf("   cone %zu differs: frag march %.9g %.9g %.9g %.9g esc %.9g %.9g | "
                            "comp march %.9g %.9g %.9g %.9g esc %.9g %.9g\n",
                            i, f.march[0], f.march[1], f.march[2], f.march[3], f.escape[0],
                            f.escape[1], c.march[0], c.march[1], c.march[2], c.march[3],
                            c.escape[0], c.escape[1]);
            ++differ;
        }
        // THE RAY HIT'S READ AGAINST THE MARCH AT ZERO LENGTH. Three cases, told
        // apart by what the march did: no step at all (the point is past the box
        // or the mip past the cascade's hand-over: the march reads nothing there,
        // by its exit rule), the anisotropic chain's finest half-mip (the march's
        // first stretch reads the ISOTROPIC volume up to lod 0.5 by design, the hit
        // the directional one), and everything else, which must agree to the bit.
        const bool marchEmpty = c.marchRead[0] == 0.0f && c.marchRead[1] == 0.0f &&
                                c.marchRead[2] == 0.0f && c.marchRead[3] == 0.0f;
        const bool hitEmpty = c.hitRead[0] == 0.0f && c.hitRead[1] == 0.0f &&
                              c.hitRead[2] == 0.0f && c.hitRead[3] == 0.0f;
        if (aniso && cones[i].lod <= 0.5f) ++hitFineIso;
        else if (marchEmpty && !hitEmpty) ++hitNoStep;   // empty space agrees and is compared
        else {
            ++hitCompared;
            if (std::memcmp(c.hitRead, c.marchRead, sizeof(c.hitRead)) != 0 ||
                std::memcmp(f.hitRead, f.marchRead, sizeof(f.hitRead)) != 0)
                ++hitDiffer;
        }
        if (c.escape[2] >= 1.0f) ++hops;
        if (c.march[3] > 0.05f) ++opaque;
        if (c.march[0] + c.march[1] + c.march[2] > 0.0f) ++lit;
        if (c.hitRead[3] > 0.0f) ++readsLit;
    }
    std::printf("   %s: %zu cones - %u differ between the stages; %u crossed a hop, %u met "
                "geometry, %u carried radiance, %u point reads non-empty\n",
                label, cones.size(), differ, hops, opaque, lit, readsLit);
    CHECK(differ == 0u, (std::string(label) + ": THE FRAGMENT AND THE COMPUTE STAGE MARCH EVERY "
                         "CONE TO THE SAME BITS").c_str());
    std::printf("   %s: ray hit vs the march at zero length - %u compared, %u differ; %u where "
                "the march takes no step (past the box / the hand-over mip), %u at the "
                "anisotropic chain's finest half-mip (the march reads the isotropic volume "
                "there by design)\n", label, hitCompared, hitDiffer, hitNoStep, hitFineIso);
    CHECK(hitCompared > 0u && hitDiffer == 0u,
          (std::string(label) + ": THE RAY HIT'S READ OF A POINT IS WHAT ONE MARCH STEP "
                                "READS THERE, bit for bit").c_str());
    CHECK(opaque > 0u && lit > 0u && readsLit > 0u,
          (std::string(label) + ": not vacuous - the cones met lit geometry").c_str());
    if (cascades > 1u)
        CHECK(hops > 0u, (std::string(label) + ": not vacuous - the walk crossed a hop").c_str());
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-engine-voxel-reader-parity-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("parity", 128u, 128u, Colour(0, 0, 0));
    Scene *scene = e->createScene("parity");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.3f, 0.3f, 0.35f), Colour(0.1f, 0.1f, 0.1f));

    // A lit room corner beside the camera and a bouncer beyond cascade 0: the
    // cones meet geometry in the inner cascade and in the outer ones.
    const NodeId ground = enginetest::addTestCube(scene, Colour(0.8f, 0.8f, 0.8f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, ground, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, ground, Vec3(120.0f, 0.1f, 120.0f));
    const NodeId wall = enginetest::addTestCube(scene, Colour(0.9f, 0.2f, 0.15f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, wall, Vec3(-2.0f, 2.0f, 0.0f));
    enginetest::setNodeScale(scene, wall, Vec3(0.3f, 4.0f, 8.0f));
    const NodeId far = enginetest::addTestCube(scene, Colour(0.2f, 0.8f, 0.3f), 0.0f, 0.9f);
    enginetest::setNodePosition(scene, far, Vec3(0.0f, 4.0f, -18.0f));
    enginetest::setNodeScale(scene, far, Vec3(20.0f, 8.0f, 0.4f));
    enginetest::addDirectionalLight(scene, Vec3(0.3f, -1.0f, -0.4f), 4.0f);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 4.0f), Vec3(0.0f, 1.0f, -10.0f));

    // THE SHIPPED CHAIN: anisotropic, four cascades.
    {
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.numBounces = 1;
        gi.ddgi = GiToggle::Off;
        gi.cascades = true;
        CHECK(scene->setGlobalIllumination(gi), "the anisotropic chain builds");
        for (int i = 0; i < 10; ++i) e->renderOneFrame();
        const GiStatus st = scene->giStatus();
        CHECK(st.cascades.size() >= 2u, "...and it is a chain");
        measureChain(e, scene, "anisotropic chain", unsigned(st.cascades.size()), true);
    }
    // THE ISOTROPIC SINGLE VOLUME (Low): the reader's other half.
    {
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::Low;
        gi.numBounces = 1;
        gi.ddgi = GiToggle::Off;
        gi.cascades = false;
        gi.testBoundsMin = Vec3(-6.0f, -1.0f, -6.0f);
        gi.testBoundsMax = Vec3(6.0f, 8.0f, 6.0f);
        CHECK(scene->setGlobalIllumination(gi), "the isotropic single volume builds");
        for (int i = 0; i < 6; ++i) e->renderOneFrame();
        measureChain(e, scene, "isotropic volume", 1u, false);
    }

    GiParams off;
    off.mode = GiMode::Off;
    scene->setGlobalIllumination(off);
    e->renderOneFrame();
    view->setScene(nullptr);
    e->destroyScene(scene);
    e->destroyView(view);
    engine.reset();
    std::printf(failures ? "\n%d FAILURES\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
