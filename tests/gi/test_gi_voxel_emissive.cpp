// gi.voxel_emissive — AN EMITTER AUTHORED AT RADIANCE L IS STORED AS L
// (PHOTON phase A, SPECS/photon/A2_VOXEL_CLIP_DESIGN.md section 2; lane
// VOXEL-CLIP-1, ogre-patch 0087).
//
// THE PHYSICS. Emissive radiance is not a reflectance. An albedo is a ratio and
// lives in [0, 1] by definition; the radiance a surface EMITS is a physical
// quantity in W/(m^2 sr) with no upper bound worth naming — a lamp, a screen, a
// sky panel are all authored above 1.0, and PHOTON's whole point is that the
// indirect light is computed from the same quantities the direct light is.
//
// THE DEFECT THIS SUITE FENCES. The voxeliser's MATERIAL store carries emissive
// as four honest floats (OgreVctMaterial.cpp: `shaderMaterial.emissive[i] =
// emissiveCol[i]`), and our order-independent merge accumulates it on a
// fixed-point grid whose per-contribution clamp is 16.0 (ogre-patch 0065) — but
// the FINAL STORE, the emissive voxel volume, was `PFG_RGBA8_UNORM`. So an
// emitter authored at 3.0 was written as exactly 1.0, and the light injection
// seeds the radiance volume from that texel (`blockColour = emissiveVal.xyz`):
// the clip entered the bounces, the irradiance field, the cones and the ray
// hits, at a third of the energy, with nothing in any log.
//
// WHY IT IS READ IN THE BYTES AND NOT IN A PICTURE. A clipped emitter draws a
// picture that is merely dimmer — indistinguishable from a dimmer lamp, from
// more occlusion, or from a shorter bounce series. PHOTON-M2 read a saturated
// gather as a contracting bounce series once already. So the question is asked
// where it lives: `giVoxelStats(cascade).peakEmissive` is the voxeliser's own
// emissive volume, downloaded, in SCENE RADIANCE.
//
// THE FIXTURE is the smallest thing that can answer it: ONE emissive cube in a
// black box — no sky, no lights, no ambient, albedo zero on the emitter and
// near-zero on the box, so the only radiance in the volume is the one this
// suite authored. (DOCS/traps/ENGINE.md: the sky's ambient is never injected
// into the voxels, so lighting a voxel fixture with a sky measures nothing;
// here the emitter IS the light.) `ddgi` is OFF — the irradiance field routes
// the diffuse at every shipped tier, and it is not this suite's subject.
//
// FOUR ARMS, on the two sides of the old ceiling: L = 0.5 and 1.0 were always
// stored correctly and are the controls (a patch that broke them would be
// changing the store's meaning rather than its range); L = 3.0 and 12.0 read
// 1.0 on the unpatched engine and are the acceptance.
//
// AND THE PICTURE HALF (HDR-READBACK-1): the same emitter seen head-on must
// draw ITS OWN radiance, L, read as radiance through the view's float readback.
// It is the one statement about the picture this fixture can make exactly — an
// emissive face with F0 = 0 and a black albedo shows its emission and nothing
// else — and at L = 3.0 and 12.0 it is a statement the 8-bit readback could
// never make (it reads 1.0 for both).
//
// EVERY CASCADE THAT HOLDS THE EMITTER IS CHECKED, because the store is
// per-cascade: a chain has one voxeliser per level and the clip was in all of
// them. A cascade coarse enough to decline a 2 m object holds nothing and is
// reported as such rather than asserted on.
//
// Its own binary like every GI suite.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                     \
    } while (0)
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!(cond)) ++failures;                                                \
    } while (0)

static const unsigned kSize = 128;

/// THE BAR: 2 % of the authored radiance. It is the store's own precision and
/// not a tolerance for a wrong answer — half-float carries 0.5, 1, 3 and 12
/// exactly, and the merge's fixed-point grid quantises a contribution to
/// 1/4096 (ogre-patch 0065), which is 0.008 % at L = 3.
static const double kBar = 0.02;

static void render(Engine *e, int frames = 1)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

/// The shipped PHOTON chain, cones only. `updateBudget` 0 keeps the reflection
/// probes out of the measurement; `ddgi` Off keeps the irradiance field out of
/// it (DOCS/traps/ENGINE.md, LATTICE-1).
static GiParams chainGi()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.updateBudget = 0;
    gi.cascades = true;
    return gi;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-voxel-emissive-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("voxemissive", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("voxemissive");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    {
        // A PASSTHROUGH view that keeps its scene radiance: no post chain, no
        // grade — the float scene target and the one composite, nothing else.
        PostFxDesc fx;
        fx.hdrReadback = true;
        view->setPostFx(fx);
    }
    // A BLACK BOX: no ambient at all, so nothing but the emitter puts radiance
    // in the volume.
    scene->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    // The box — matte and nearly black, so it receives but contributes nothing
    // the peak could be confused with.
    const NodeId floorN = enginetest::addTestCube(scene, Colour(0.02f, 0.02f, 0.02f), 0.0f, 1.0f);
    enginetest::setNodePosition(scene, floorN, Vec3(0.0f, -0.05f, 0.0f));
    enginetest::setNodeScale(scene, floorN, Vec3(20.0f, 0.1f, 20.0f));
    const NodeId wallN = enginetest::addTestCube(scene, Colour(0.02f, 0.02f, 0.02f), 0.0f, 1.0f);
    enginetest::setNodePosition(scene, wallN, Vec3(0.0f, 2.0f, -4.0f));
    enginetest::setNodeScale(scene, wallN, Vec3(10.0f, 4.0f, 0.2f));

    // THE EMITTER: a 2 m cube, black albedo (it must not bounce anything back
    // into itself), its emissive rewritten per arm. 2 m rather than 1 so more
    // than the innermost cascade is big enough to hold it — a cascade only
    // voxelises what fills half a voxel of its own grid.
    const NodeId lamp = scene->createNode();
    const MeshId cube = scene->createMesh(enginetest::unitCubeMesh());
    PbrParams lampP;
    lampP.albedo = Colour(0.0f, 0.0f, 0.0f);
    lampP.metalness = 0.0f;
    lampP.roughness = 1.0f;
    // ...AND IT REFLECTS NOTHING (Specular workflow, ior 1, black specular ->
    // F0 = 0), so its picture is its emission alone (the picture half).
    lampP.workflow = PbrParams::Workflow::Specular;
    lampP.ior = 1.0f;
    lampP.specularColour = Colour(0.0f, 0.0f, 0.0f);
    lampP.emissive = Colour(0.5f, 0.5f, 0.5f);
    const MaterialId lampMat = scene->createPbrMaterial(lampP);
    CHECK(lamp && cube && lampMat && scene->attachMesh(lamp, cube, lampMat),
          "the emitter exists");
    enginetest::setNodePosition(scene, lamp, Vec3(0.0f, 1.0f, 0.0f));
    enginetest::setNodeScale(scene, lamp, Vec3(2.0f, 2.0f, 2.0f));

    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.5f, 5.0f), Vec3(0.0f, 1.0f, 0.0f));

    CHECK(scene->setGlobalIllumination(chainGi()), "the cascade chain builds");
    render(e, 12);
    const GiStatus gst = scene->giStatus();
    CHECK(!gst.cascades.empty(), "the chain is up");
    if (gst.cascades.empty()) return 1;
    const int nCascades = int(gst.cascades.size());
    std::printf("   %d cascades; cascade 0 cell %.4f m, the outermost %.4f m\n",
                nCascades, double(gst.cascades.front().cell), double(gst.cascades.back().cell));

    const double kRadiance[4] = { 0.5, 1.0, 3.0, 12.0 };
    int cascadesMeasured = 0;
    for (int a = 0; a < 4; ++a) {
        const double L = kRadiance[a];
        std::printf("\n== L = %.2f %s ==\n", L,
                    L <= 1.0 ? "(the control: inside the old UNORM range)"
                             : "(THE ACCEPTANCE: above the old 1.0 ceiling)");
        lampP.emissive = Colour(float(L), float(L), float(L));
        CHECK(scene->setPbrMaterial(lampMat, lampP), "the emitter's radiance is authored");
        // A voxel-input change on a material a GI-visible item wears bumps the
        // material generation and owes a full re-solve; the refresh asks for it
        // and the scheduler spends one cascade per frame, so the chain needs at
        // least as many frames as it has cascades, plus the at-rest settle.
        scene->refreshGlobalIllumination();
        render(e, 48);

        int heldBy = 0;
        for (int c = 0; c < nCascades; ++c) {
            const GiVoxelStats vs = scene->giVoxelStats(c);
            if (!vs.available) {
                std::printf("   cascade %d: no readback\n", c);
                continue;
            }
            const double litRadiance =
                vs.multiplier > 0.0f ? double(vs.peak) / double(vs.multiplier) : 0.0;
            std::printf("   cascade %d: emissive store %s peak %.4f (at ceiling %lld, above 1.0 "
                        "%lld) | lit %s peak %.4f = %.4f radiance, %lld lit voxels\n",
                        c, vs.emissiveFormat.empty() ? "(none)" : vs.emissiveFormat.c_str(),
                        double(vs.peakEmissive), (long long)vs.emissiveAtMax,
                        (long long)vs.emissiveAboveOne, vs.format.c_str(), double(vs.peak),
                        litRadiance, (long long)vs.voxelsLit);
            if (vs.peakEmissive <= 0.0f) continue;   // this cascade declined the emitter
            ++heldBy;
            ++cascadesMeasured;
            const double rel = std::fabs(double(vs.peakEmissive) - L) / L;
            CHECK_MSG(rel <= kBar,
                      "cascade %d stores the emitter's authored radiance: %.4f against L = %.2f "
                      "(%.2f %% off, bar %.0f %%)",
                      c, double(vs.peakEmissive), L, 100.0 * rel, 100.0 * kBar);
        }
        CHECK_MSG(heldBy > 0, "at least one cascade holds the emitter at L = %.2f (%d do)", L,
                  heldBy);

        // THE PICTURE HALF: the emitter's front face fills the centre of the
        // frame (the camera looks at its centre from 4 m in front of it).
        ImageF img;
        if (!view->readPixelsHdr(img)) {
            CHECK_MSG(false, "the view reads its radiance back (%s)", e->lastError().c_str());
        } else {
            double sum = 0.0;
            int n = 0;
            for (unsigned y = kSize / 2u - 4u; y <= kSize / 2u + 4u; ++y)
                for (unsigned x = kSize / 2u - 4u; x <= kSize / 2u + 4u; ++x) {
                    sum += img.at(x, y).g;
                    ++n;
                }
            const double pix = sum / n;
            const double rel = std::fabs(pix - L) / L;
            CHECK_MSG(rel <= 0.01,
                      "THE PICTURE: the emitter's face draws %.4f against its authored L = %.2f "
                      "(%.3f %% off, bar 1 %%) — read as radiance, above 1.0 included", pix, L,
                      100.0 * rel);
        }
    }

    CHECK_MSG(cascadesMeasured >= 4,
              "the claim was measured on %d cascade-arms, not on one", cascadesMeasured);

    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
