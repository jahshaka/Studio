// gi.cone_emitter — AN EMITTER IN A SPECULAR LOBE READS ITS RADIANCE (PHOTON-PHYSICS-1, item 1,
// CONE-EMITTER-1; D3's finding: gi.hit_planar's screen cone read an emitter at ~0.39 of the true
// picture, the traced mirror at ~0.49, uniformly over the three channels).
//
// THE PHYSICS. A glossy surface reflects L_r = integral of L(w) f_r(v, w) cos(w) dw over the
// hemisphere; the split sum reads it as (the lobe's mean radiance) x (the specular albedo DFG).
// An emitter of radiance L that covers the whole lobe therefore reflects L x DFG, one that covers
// a share k of the lobe's weight L x k x DFG — k is a property of the GEOMETRY and the BRDF alone:
//     k = integral over the quad of f_r cos dw / integral over the hemisphere of f_r cos dw
// with f_r the engine's GGX (alpha = roughness^2, the height-correlated Smith visibility, F0 = 1),
// integrated here numerically (a 1024 x 512 grid over the hemisphere, ray against the quad).
//
// THE MEASUREMENT: scene radiance (the float readback) of the plate's pixel where it reflects the
// emitter, divided by L x DFG — and DFG is not computed, it is MEASURED: the same plate, the same
// camera, under a UNIFORM SKY of radiance S outside any volume reads S x DFG (the environment
// lobe of a uniform sky is S at every roughness). So the ratio
//     read = pixel / (L x reference / S)
// is the lobe's coverage the cone read — 1.0 for a covered lobe.
//
// THE FIXTURE: a white metal plate (the reflector, roughness 0.05 / 0.2 / 0.5) under an emissive
// 1 m x 1 m panel (radiance L = 5, black albedo, F0 = 0) standing 1 m away across the plate's
// reflection of the eye: the eye at (0, 1, 1) looks at the plate's point P = (0, 0, 0), the
// mirror direction leaves P at 45 degrees and meets the panel's centre (0, 1, -1), sqrt 2 m away.
// No sky, no environment light, no ambient, no lamp: the panel is the only light. The Medium
// chain, cones only (the field off — LATTICE-1; the probes' update budget 0; no rays, no SSR).
// TWO CHAIN POSITIONS: the GI's own driver view (the scene's authoritative view, created first)
// at the eye (the fixture inside cascade 0) and 7.5 m away (the fixture in cascade 1) — the cone
// reads the panel in whichever cascade holds it, and both must read the same radiance.
//
// (c) THE STORE: the light volume at the panel's voxels, decoded (giVoxelVolume: premultiplied by
// the voxel's opacity c, times the store's multiplier) — the panel's voxels must hold L x c.
//
// WHAT IT FOUND (PHOTON-PHYSICS-1). The store was right (c: 5.0000 of 5 in every cascade). The
// loss was the walk: (1) every cascade past the first came back at HALF its radiance - the
// specular hop took upstream's brightness ramp mix( 0.5, w, lod / mips^3 ) instead of the unit
// conversion w (roughness 0.05: 1.000 L in cascade 0, 0.450 L in cascade 1; D3's 0.39 / 0.49);
// (2) the specular cone started one cell along the NORMAL and walked a line parallel to the
// reflection, sliding the mirror image one cell (0.74 L where the reflection meets the panel
// 0.25 m below its edge; 0.96 after) - it starts on its own axis now, aged by what it skipped.
// THE RESIDUAL (the lobe target row): a partly covered wide lobe over-reads - roughness 0.5
// reads 1.48x (cascade 0) / 1.58x (cascade 1) the GGX coverage 0.396. Two model terms of the
// one reader, neither a loss: the aperture (upstream's tan( r pi / 2 ), 44.5 degrees at 0.5,
// wider than the GGX lobe) and the footprint kernel (a bilinear read of a texel K weighs a
// small central object as A / K^2, a uniform disc of radius K as A / (pi K^2)). A covered lobe
// is exact either way; the partial one is the reader's own lane.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

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
static double kL = 5.0;                          // the panel's radiance
static Vec3 kEye(0.0f, 1.0f, 1.0f);
static const Vec3 kP(0.0f, 0.0f, 0.0f);
// the panel: x -0.5..0.5, y 0.5..1.5, its lit face at z = -1 (0.02 m thick, behind it)
static const float kPanelZ = -1.0f, kPanelT = 0.02f;

static void render(Engine *e, int frames = 1)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static bool settle(Engine *e, Scene *s)
{
    for (int f = 0; f < 4000; ++f) {
        if (s->giStatus().giAtRest) return true;
        e->renderOneFrame();
    }
    return s->giStatus().giAtRest;
}

/// The centre 4 x 4 pixels' mean luminance, scene radiance.
static double centreLum(View *v)
{
    ImageF img;
    if (!v->readPixelsHdr(img)) return -1.0;
    double s = 0.0;
    int n = 0;
    for (unsigned y = kSize / 2 - 2; y < kSize / 2 + 2; ++y)
        for (unsigned x = kSize / 2 - 2; x < kSize / 2 + 2; ++x) {
            const Colour c = img.at(x, y);
            s += 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
            ++n;
        }
    return s / n;
}

/// THE LOBE'S COVERAGE BY THE PANEL (the header's k): the engine's GGX at P for the eye's view,
/// F0 = 1, over the hemisphere; the share of its weight whose direction meets the panel's face.
static double ggxCoverage(double perceptualRoughness)
{
    const double pi = 3.14159265358979;
    const double a = std::max(perceptualRoughness * perceptualRoughness, 1e-4);
    const double a2 = a * a;
    // V: from P to the eye; N = +Y.
    double vx = kEye.x - kP.x, vy = kEye.y - kP.y, vz = kEye.z - kP.z;
    const double vl = std::sqrt(vx * vx + vy * vy + vz * vz);
    vx /= vl; vy /= vl; vz /= vl;
    const double NdotV = vy;
    const int nT = 2048, nP = 2048;
    double all = 0.0, onPanel = 0.0;
    for (int i = 0; i < nT; ++i) {
        const double ct = (i + 0.5) / nT;               // cos theta, uniform in cos: dw = dcos dphi
        const double st = std::sqrt(1.0 - ct * ct);
        for (int j = 0; j < nP; ++j) {
            const double ph = 2.0 * pi * (j + 0.5) / nP;
            const double lx = st * std::cos(ph), ly = ct, lz = st * std::sin(ph);
            double hx = lx + vx, hy = ly + vy, hz = lz + vz;
            const double hl = std::sqrt(hx * hx + hy * hy + hz * hz);
            hx /= hl; hy /= hl; hz /= hl;
            const double NdotH = hy, NdotL = ly;
            const double d = NdotH * NdotH * (a2 - 1.0) + 1.0;
            const double D = a2 / (pi * d * d);
            const double lambdaV = NdotL * std::sqrt(NdotV * NdotV * (1.0 - a2) + a2);
            const double lambdaL = NdotV * std::sqrt(NdotL * NdotL * (1.0 - a2) + a2);
            const double Vis = 0.5 / (lambdaV + lambdaL);
            const double w = D * Vis * NdotL;          // f_r cos, F = 1 (dw uniform)
            all += w;
            // the ray P + t l meets the panel's face plane z = kPanelZ
            if (lz < -1e-9) {
                const double t = (kPanelZ - kP.z) / lz;
                const double x = kP.x + t * lx, y = kP.y + t * ly;
                if (x >= -0.5 && x <= 0.5 && y >= 0.5 && y <= 1.5) onPanel += w;
            }
        }
    }
    return all > 0.0 ? onPanel / all : 0.0;
}

int main(int argc, char **argv)
{
    // --lobe-target: gi.cone_emitter_lobe_target (a photon-target row: the 0.5 lobe against the
    // GGX coverage). --sweep <eye y> [<L>]: not a row - the same arms from another eye height (the
    // reflection meets the panel elsewhere) and radiance, printed, nothing asserted.
    const bool lobeTarget = argc > 1 && std::strcmp(argv[1], "--lobe-target") == 0;
    const bool sweep = argc > 1 && std::strcmp(argv[1], "--sweep") == 0;
    if (sweep && argc > 2) kEye.y = float(std::atof(argv[2]));
    if (sweep && argc > 3) kL = std::atof(argv[3]);
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-cone-emitter-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;
    fx.hdr = false;
    fx.hdrReadback = true;

    const double roughs[3] = { 0.05, 0.2, 0.5 };

    // ---- THE REFERENCE: the plate under a uniform sky of radiance S, no volume ----------------
    double reference[3] = { -1.0, -1.0, -1.0 };
    const unsigned char kSkyByte = 128;
    const double S = std::pow((kSkyByte / 255.0 + 0.055) / 1.055, 2.4);
    {
        View *rv = e->createOffscreenView("ce-ref", kSize, kSize, Colour(0, 0, 0));
        rv->setPostFx(fx);
        Scene *open = e->createScene("ce-uniform");
        CHECK(rv->setScene(open), "the reference view shows the uniform-sky scene");
        const unsigned char skyPx[4] = { kSkyByte, kSkyByte, kSkyByte, 255 };
        SkyDesc sky;
        sky.mode = SkyMode::Equirectangular;
        sky.equirect = open->createTexture(1, 1, skyPx, true);
        CHECK(sky.equirect && open->setSky(sky), "the uniform sky binds");
        float sh[27] = { 0 };
        for (int f = 0; f < 20 && !open->skyAmbientSh(sh); ++f) e->renderOneFrame();
        open->setAmbientSh(sh);
        open->setEnvironmentLight(Colour(1, 1, 1, 1));
        PbrParams metal;
        metal.albedo = Colour(1, 1, 1);
        metal.metalness = 1.0f;
        const NodeId nd = open->createNode();
        const MaterialId mat = open->createPbrMaterial(metal);
        open->attachMesh(nd, open->createMesh(enginetest::unitCubeMesh()), mat);
        open->setNodeTransform(nd, Vec3(0.0f, -0.05f, 0.0f), Quat(), Vec3(6.0f, 0.1f, 6.0f));
        enginetest::testCameraLookAt(rv, kEye, kP);
        for (int r = 0; r < 3; ++r) {
            metal.roughness = float(roughs[r]);
            open->setPbrMaterial(mat, metal);
            render(e, 10);
            reference[r] = centreLum(rv);
        }
        rv->setScene(nullptr);
        e->destroyScene(open);
        e->destroyView(rv);
    }

    // ---- THE EMITTER SCENE ---------------------------------------------------------------------
    View *driver = e->createOffscreenView("ce-gi", 16, 16, Colour(0, 0, 0));
    View *view = e->createOffscreenView("ce", kSize, kSize, Colour(0, 0, 0));
    view->setOffscreenContract(OffscreenContract::StillPicture);
    view->setPostFx(fx);
    Scene *s = e->createScene("ce");
    driver->setScene(s);
    view->setScene(s);
    {
        SkyDesc none;
        none.mode = SkyMode::NoSky;
        s->setSky(none);
        s->setEnvironmentLight(Colour(0, 0, 0, 1));
    }
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    PbrParams plateP;
    plateP.albedo = Colour(1, 1, 1);
    plateP.metalness = 1.0f;
    plateP.roughness = 0.05f;
    const MaterialId plateM = s->createPbrMaterial(plateP);
    const NodeId plate = s->createNode();
    s->attachMesh(plate, cube, plateM);
    s->setNodeTransform(plate, Vec3(0.0f, -0.05f, 0.0f), Quat(), Vec3(6.0f, 0.1f, 6.0f));
    PbrParams panelP;
    panelP.albedo = Colour(0, 0, 0);
    panelP.metalness = 0.0f;
    panelP.roughness = 1.0f;
    panelP.workflow = PbrParams::Workflow::Specular;
    panelP.ior = 1.0f;
    panelP.specularColour = Colour(0, 0, 0);
    panelP.emissive = Colour(float(kL), float(kL), float(kL));
    const NodeId panel = s->createNode();
    s->attachMesh(panel, cube, s->createPbrMaterial(panelP));
    s->setNodeTransform(panel, Vec3(0.0f, 1.0f, kPanelZ - 0.5f * kPanelT), Quat(), Vec3(1.0f, 1.0f, kPanelT));
    enginetest::testCameraLookAt(view, kEye, kP);

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 1;
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::Off;
    gi.updateBudget = 0;
    CHECK(s->setGlobalIllumination(gi), "the Medium chain builds (cones only)");

    struct ChainPos { const char *name; Vec3 eye; Vec3 at; };
    const ChainPos chains[2] = {
        { "the fixture in cascade 0 (the chain on the eye)", kEye, kP },
        { "the fixture in cascade 1 (the chain 7.5 m away)", Vec3(0.0f, 1.0f, 7.5f), Vec3(0.0f, 1.0f, 12.0f) },
    };
    double reads[2][3] = {};
    const double cover[3] = { ggxCoverage(roughs[0]), ggxCoverage(roughs[1]), ggxCoverage(roughs[2]) };
    for (int c = 0; c < 2; ++c) {
        enginetest::testCameraLookAt(driver, chains[c].eye, chains[c].at);
        std::printf("\n== %s ==\n", chains[c].name);
        for (int r = 0; r < 3; ++r) {
            plateP.roughness = float(roughs[r]);
            s->setPbrMaterial(plateM, plateP);
            render(e, 4);
            settle(e, s);
            render(e, 8);
            const double pix = centreLum(view);
            reads[c][r] = reference[r] > 0.0 ? pix / (kL * reference[r] / S) : -1.0;
            std::printf("   roughness %.2f: pixel %.4f, reference (S x DFG) %.4f of S %.4f -> the lobe read %.4f of "
                        "L; the GGX lobe's coverage by the panel %.4f (read / coverage %.3f)\n",
                        roughs[r], pix, reference[r], S, reads[c][r], cover[r],
                        cover[r] > 0 ? reads[c][r] / cover[r] : 0.0);
        }
        // (c) THE STORE at the panel: every cascade that holds it.
        const GiStatus st = s->giStatus();
        for (int k = 0; k < int(st.cascades.size()) && k < 3; ++k) {
            GiVoxelVolume vol;
            if (!s->giVoxelVolume(k, vol) || !vol.available || vol.multiplier <= 0.0f) continue;
            double sumL = 0.0, sumC = 0.0, peak = 0.0;
            int n = 0;
            for (int z = 0; z < vol.depth; ++z)
                for (int y = 0; y < vol.height; ++y)
                    for (int x = 0; x < vol.width; ++x) {
                        const double wx = vol.origin[0] + (x + 0.5) * vol.cell[0];
                        const double wy = vol.origin[1] + (y + 0.5) * vol.cell[1];
                        const double wz = vol.origin[2] + (z + 0.5) * vol.cell[2];
                        if (std::fabs(wx) > 0.5 - vol.cell[0] || wy < 0.5 + vol.cell[1] ||
                            wy > 1.5 - vol.cell[1] || std::fabs(wz - (kPanelZ - 0.5 * kPanelT)) > 0.5 * vol.cell[2])
                            continue;
                        const size_t i = (size_t(z) * vol.height + y) * vol.width + x;
                        const double cov = vol.albedo[i * 4 + 3];
                        const double lg = vol.light[i * 4 + 1] / vol.multiplier;
                        sumL += lg;
                        sumC += cov;
                        if (cov > 0.05) peak = std::max(peak, lg / cov);
                        ++n;
                    }
            const double held = sumC > 0.0 ? sumL / sumC : 0.0;
            std::printf("   (c) cascade %d (cell %.3f m): %d panel voxels, opacity mean %.3f, radiance held / opacity "
                        "%.4f (peak %.4f) of L %.2f\n", k, vol.cell[0], n, n ? sumC / n : 0.0, held, peak, kL);
            if (n > 0 && !sweep && !lobeTarget)
                CHECK_MSG(std::fabs(held / kL - 1.0) <= 0.03,
                          "(c) cascade %d's panel voxels hold L x their opacity (%.4f / %.2f, bar 3 %%)", k, held, kL);
        }
    }

    // THE BARS. A covered lobe reads the emitter's radiance (>= 0.9 L, the brief's bar) wherever
    // the chain holds it. The roughness-0.5 lobe against the GGX coverage of the panel is the
    // photon-target row (the header's residual): the cone reads 1.48x / 1.58x of it.
    for (int c = 0; c < 2 && !sweep && !lobeTarget; ++c)
        CHECK_MSG(reads[c][0] >= 0.9, "%s: roughness 0.05, a covered lobe, reads %.3f of L (bar >= 0.9)",
                  chains[c].name, reads[c][0]);
    for (int c = 0; c < 2 && lobeTarget; ++c) {
        std::printf("target: %s: the 0.5 lobe reads %.3f against the GGX coverage %.3f (%.2fx; bar 10 %%)\n",
                    chains[c].name, reads[c][2], cover[2], reads[c][2] / cover[2]);
        CHECK_MSG(std::fabs(reads[c][2] / cover[2] - 1.0) <= 0.10,
                  "%s: roughness 0.5 reads %.3f against the GGX lobe's coverage %.3f (bar 10 %%)",
                  chains[c].name, reads[c][2], cover[2]);
    }

    view->setScene(nullptr);
    driver->setScene(nullptr);
    e->destroyScene(s);
    e->destroyView(view);
    e->destroyView(driver);
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
