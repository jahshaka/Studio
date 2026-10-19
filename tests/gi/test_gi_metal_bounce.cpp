// gi.metal_bounce — A METAL RE-EMITS BY ITS SPECULAR LOBE'S INTEGRAL (ENGINE-UPGRADE-U5 stage 5,
// METAL-BOUNCE-1; it replaces gi.voxel_metal_albedo, whose law - a metal re-emits 1 - metalness of the
// diffuser - was the DIFFUSE-ONLY store's: a white rough metal re-emitted 0).
//
// THE LAW. HlmsPbs splits a base colour b at metalness m into a diffuse lobe b (1 - m) and a specular
// lobe of F0 = lerp( 0.04, b, m ) (kS = 1 here) - EVERY surface's, a dielectric's 0.04 included
// (ENGINE-POLISH-PHOTON-1 stage 2) - and a surface lit from one direction at cosine mu reflects
//     b (1 - m) Dd( mu, r )  +  F0 A( mu, r ) + B( mu, r )
// of what a Lambertian of albedo 1 would - Dd the normalised Disney diffuse's directional albedo, A and B
// the GGX lobe's two Fresnel parts (enginetesthelpers.h, both by direct integration of the lobes). Under
// a UNIFORM incoming hemisphere the same with the hemispherical means Dd-bar, A-bar, B-bar. So against a
// diffuser of the same b and r (m = 0, whose own 0.04 lobe is in the reference):
//   THE DIRECT ARM (a sun straight down, mu = 1): the plate's stored radiance is
//       [ (1 - m) Dd( 1, r ) + F0 A( 1, r ) + B( 1, r ) ] / [ Dd( 1, r ) + 0.04 A( 1, r ) + B( 1, r ) ]
//     of the diffuser's (b = 1: a WHITE metal at m = 1), and a white panel facing the plate, lit by nothing
//     else, takes the same share of the diffuser's bounce (field off and on);
//   THE DIELECTRIC ARM (the same sun, r 9/31 and 23/31 - the table's own roughness rows): a BLACK dielectric's
//     store is its specular lobe alone, 0.04 A + B of the incident (0.040 / 0.026), measured against a white
//     metal's A + B; a b 0.6 dielectric's TOTAL holds the same lobe beside its diffuse one, against a b 1
//     dielectric's: ( 0.6 Dd + 0.04 A + B ) / ( Dd + 0.04 A + B ); bar 0.4 % (b 0.6, not the brief's 0.5:
//     153 / 255 is exact in the RGBA8 albedo volume, 0.5 is 128 / 255 = +0.39 %, the whole bar). The base's
//     diffuse-only dielectric read 0 and 0.6 (the gate on w = metalness x kS);
//   THE BOUNCE ARM (no lamp: an emitter of albedo 0 above the plate, the store's one bounce pass): the plate's stored
//     radiance is its re-emission of what it gathers,
//       [ (1 - m) Dd-bar( r ) + F0 A-bar( r ) + B-bar( r ) ] / [ Dd-bar( r ) + 0.04 A-bar( r ) + B-bar( r ) ]
//     of the diffuser's - the furnace's answer: a white rough metal re-emits ~ its lobe's reflectance.
// THE BARS: 1.5 % on every store ratio - the store's diffuse lobe is a fit (JahDiffuseAlbedo, worst
// 0.0096 against the integral this test takes), the lobe's table is bilinear (0.22 % worst at mu >= 0.2,
// PhotonSpecularAlbedo.h) and a half-float quantum of a lit voxel is 0.1 %; the panel's 3 % (gi.env_cone's).
// Roughness 0.3 (the metal re-emits MORE than the diffuser) and 0.9 (LESS): the two sides of the ratio.
// THE LIVE EDITS (the voxel inputs of a PBR edit, OgreMaterials.cpp): on the r 0.6 diffuser of the direct
// arm, a roughness-only edit (0.6 -> 0.3) and then a metalness edit (0 -> 0.5), each settled by the host's
// refresh, re-voxelise the cascade exactly once and the store follows them by the law.
// THE FIXTURE: a 2 x 0.1 x 2 m plate at the origin; one 12 m cascade of 64 cells, Medium, one bounce, no
// gather, no sky; the direct arm's panel standing beyond the plate's far edge, parallel to the sun.
// THE BASE (irisgl ee27dbf, the diffuse-only store), measured: the metals' ratios read 1 - m whatever the
// roughness - 0.500 / 0.000 direct, and the bounce arm's pure metal 0.000.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

namespace {

/// THE LOBES' RE-EMISSION of a white base (b = 1) at metalness m, kS = 1, in units of a Lambertian of albedo 1:
/// the diffuse lobe b (1 - m) Dd plus the specular lobe F0 A + B, F0 = lerp( 0.04, 1, m ) - at the sun's cosine 1...
double lobeDirect(double m, double r)
{
    double A = 0.0, B = 0.0;
    enginetest::ggxSpecularAlbedo(1.0, r, A, B, 512);
    const double dd = enginetest::disneyDiffuseAlbedo(1.0, r, 256);
    return (1.0 - m) * dd + (0.04 * (1.0 - m) + m) * A + B;
}
/// ...and of a uniform hemisphere.
double lobeHemi(double m, double r)
{
    double A = 0.0, B = 0.0;
    enginetest::ggxSpecularAlbedoHemi(r, A, B, 64, 192);
    const double dd = enginetest::disneyDiffuseAlbedoHemi(r, 64, 64);
    return (1.0 - m) * dd + (0.04 * (1.0 - m) + m) * A + B;
}
/// THE LAW'S RATIO, metalness m against the diffuser (m = 0), white base, kS = 1.
double directRatio(double m, double r) { return lobeDirect(m, r) / lobeDirect(0.0, r); }
double bounceRatio(double m, double r) { return lobeHemi(m, r) / lobeHemi(0.0, r); }

}  // namespace

int main(int argc, char **argv)
{
    // `--route`: THE TINTED METAL AND THE MOVER (gi.metal_bounce_route); the rest is gi.metal_bounce
    const bool route = argc > 1 && std::string(argv[1]) == "--route";
    // `--gate`: THE MOVERS' GATE ON A METAL'S CARD (gi.metal_bounce_gate, ENGINE-POLISH-PHOTON-1's fix round)
    const bool gate = argc > 1 && std::string(argv[1]) == "--gate";
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-metal-bounce-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    // THE FRAMES THE ROW RENDERS (JAHSHAKA_TEST_FRAMES states them): every step goes through here
    unsigned long long rendered = 0;
    const auto run = [&](int n) { enginetest::frames(e, n); rendered += unsigned(n); };

    View *rv = e->createOffscreenView("metalbounce", 64, 64, Colour(0, 0, 0));
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;
    fx.hdr = false;
    fx.hdrReadback = true;
    rv->setPostFx(fx);
    // The panel's face, the view's centre (the plate lies below it in the picture).
    const auto readHdr = [&]() {
        double acc = 0.0;
        for (int f = 0; f < 4; ++f) {
            run(1);
            ImageF img;
            if (!rv->readPixelsHdr(img)) return -1.0;
            double s2 = 0.0; int n = 0;
            for (unsigned y = 20; y < 32; ++y)
                for (unsigned x = 24; x < 40; ++x) {
                    const Colour c = img.at(x, y);
                    s2 += 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; ++n;
                }
            acc += s2 / n;
        }
        return acc / 4.0;
    };
    // THE STORE: the plate's voxels under its centre, radiance = light / its opacity (luminance)
    const auto plateRadiance = [&](Scene *s, int &n) {
        GiVoxelVolume v;
        double rad = 0.0;
        n = 0;
        if (s->giVoxelVolume(0, v) && v.available) {
            for (int dz = -2; dz <= 2; ++dz)
                for (int dx = -2; dx <= 2; ++dx) {
                    const int cx = int((0.0f - v.origin[0]) / v.cell[0]) + dx;
                    const int cy = int((0.05f - v.origin[1]) / v.cell[1]);
                    const int cz = int((0.0f - v.origin[2]) / v.cell[2]) + dz;
                    const size_t i = 4 * (size_t(cx) + size_t(v.width) * (size_t(cy) + size_t(v.height) * size_t(cz)));
                    if (v.light[i + 3] > 0.0f) {
                        rad += (0.2126 * v.light[i] + 0.7152 * v.light[i + 1] + 0.0722 * v.light[i + 2]) / v.light[i + 3];
                        ++n;
                    }
                }
        }
        return n ? rad / n : -1.0;
    };
    const auto settle = [&](Scene *s) {
        run(20);
        for (int f = 0; f < 4000 && !s->published().gi.giAtRest; ++f) run(1);
    };
    const auto giOn = [&](GiToggle field, int bounces) {
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::Medium;
        gi.numBounces = bounces;
        gi.ddgi = field;
        gi.gather = GiToggle::Off;
        gi.cascadeCount = 1;
        gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 6.0f, 64, 0.0f };
        return gi;
    };
    struct Case { float rough, metal; };
    const std::pair<GiToggle, const char *> arms[2] = { { GiToggle::Off, "field off" }, { GiToggle::On, "field on" } };

    if (!route && !gate) {
    // ---- THE DIRECT ARM -------------------------------------------------------------------------
    const Case direct[5] = { { 0.3f, 0.0f }, { 0.3f, 0.5f }, { 0.3f, 1.0f }, { 0.9f, 0.0f }, { 0.9f, 1.0f } };
    double store[5][2] = {}, bounce[5][2] = {};
    for (int c = 0; c < 5; ++c) {
        Scene *s = e->createScene("metalbounce");
        CHECK(rv->setScene(s), "the view shows the scene");
        float sh[27] = { 0 };
        s->setAmbientSh(sh);
        s->setEnvironmentLight(Colour(0, 0, 0, 1));
        CHECK(enginetest::addDirectionalLight(s, Vec3(0.0f, -1.0f, 0.0f), 3.0f) != 0, "the sun straight down");
        const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
        const auto slab = [&](const Vec3 &pos, const Vec3 &scale, const PbrParams &p) {
            const NodeId nd = s->createNode();
            const MaterialId mat = s->createPbrMaterial(p);
            s->attachMesh(nd, cube, mat);
            s->setNodeTransform(nd, pos, Quat(), scale);
            return mat;
        };
        PbrParams plate;
        plate.albedo = Colour(1.0f, 1.0f, 1.0f);
        plate.roughness = direct[c].rough;
        plate.metalness = direct[c].metal;
        slab(Vec3(0.0f, 0.05f, 0.0f), Vec3(2.0f, 0.1f, 2.0f), plate);
        PbrParams panel; panel.albedo = Colour(1, 1, 1); panel.roughness = 1.0f;
        slab(Vec3(0.0f, 0.85f, 1.15f), Vec3(2.0f, 1.5f, 0.1f), panel);
        enginetest::testCameraLookAt(rv, Vec3(0.0f, 0.9f, -2.0f), Vec3(0.0f, 0.75f, 1.1f));
        GiParams off; off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        run(10);
        const double dark = readHdr();
        for (int a = 0; a < 2; ++a) {
            s->setGlobalIllumination(giOn(arms[a].first, 1));
            settle(s);
            bounce[c][a] = readHdr() - dark;
            int n = 0;
            store[c][a] = plateRadiance(s, n);
            std::printf("direct: roughness %.1f metalness %.1f, %s: the plate's voxel radiance %.5f (%d voxels); the "
                        "panel's bounce %.5f (GI-off control %.5f)\n", direct[c].rough, direct[c].metal, arms[a].second,
                        store[c][a], n, bounce[c][a], dark);
        }
        CHECK(std::fabs(dark) < 1e-3, "with no GI the panel's face is dark (no sun on it, no sky)");
        s->setGlobalIllumination(off);
        rv->setScene(nullptr);
        e->destroyScene(s);
    }
    for (int a = 0; a < 2; ++a) {
        for (int c = 0; c < 5; ++c) {
            const int ref = direct[c].rough < 0.5f ? 0 : 3;
            if (c == ref) {
                CHECK(store[c][a] > 0.0 && bounce[c][a] > 1e-3, "the diffuser stores light and lights the panel (the reference)");
                continue;
            }
            const double want = directRatio(direct[c].metal, direct[c].rough);
            char msg[300];
            std::snprintf(msg, sizeof msg, "direct, %s, roughness %.1f metalness %.1f: the store holds the lobes' ratio "
                          "of the diffuser's radiance (%.4f; want %.4f, bar 1.5 %%)", arms[a].second, direct[c].rough,
                          direct[c].metal, store[c][a] / store[ref][a], want);
            CHECK(std::fabs(store[c][a] / store[ref][a] / want - 1.0) <= 0.015, msg);
            std::snprintf(msg, sizeof msg, "direct, %s, roughness %.1f metalness %.1f: the panel takes the same share of "
                          "the diffuser's bounce (%.4f; want %.4f, bar 3 %%)", arms[a].second, direct[c].rough,
                          direct[c].metal, bounce[c][a] / bounce[ref][a], want);
            CHECK(std::fabs(bounce[c][a] / bounce[ref][a] / want - 1.0) <= 0.03, msg);
        }
    }

    // ---- THE DIELECTRIC ARM (ENGINE-POLISH-PHOTON-1 stage 2) --------------------------------------------
    // The plate alone under the sun (no panel: its bounce would be the plate's second term), the store's direct
    // term only (one bounce = the pixel's gather, none in the store): a white METAL (F0 = 1: A + B), a BLACK
    // dielectric (its lobe alone: 0.04 A + B), a b 0.6 and a b 1 dielectric (their ratio holds the lobe beside the
    // diffuse one) - at the table's rows 9/31 and 23/31.
    {
        const float rough[2] = { 9.0f / 31.0f, 23.0f / 31.0f };
        struct Plate { float base, metal; };
        const Plate plates[4] = { { 1.0f, 1.0f }, { 0.0f, 0.0f }, { 0.6f, 0.0f }, { 1.0f, 0.0f } };
        for (int k = 0; k < 2; ++k) {
            double rad[4] = {};
            for (int c = 0; c < 4; ++c) {
                Scene *s = e->createScene("dielectric");
                CHECK(rv->setScene(s), "the view shows the dielectric scene");
                float sh[27] = { 0 };
                s->setAmbientSh(sh);
                s->setEnvironmentLight(Colour(0, 0, 0, 1));
                CHECK(enginetest::addDirectionalLight(s, Vec3(0.0f, -1.0f, 0.0f), 3.0f) != 0, "the sun straight down");
                const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
                PbrParams plate;
                plate.albedo = Colour(plates[c].base, plates[c].base, plates[c].base);
                plate.roughness = rough[k];
                plate.metalness = plates[c].metal;
                const NodeId nd = s->createNode();
                s->attachMesh(nd, cube, s->createPbrMaterial(plate));
                s->setNodeTransform(nd, Vec3(0.0f, 0.05f, 0.0f), Quat(), Vec3(2.0f, 0.1f, 2.0f));
                enginetest::testCameraLookAt(rv, Vec3(0.0f, 0.9f, -2.0f), Vec3(0.0f, 0.1f, 0.0f));
                s->setGlobalIllumination(giOn(GiToggle::Off, 1));
                settle(s);
                int n = 0;
                rad[c] = plateRadiance(s, n);
                std::printf("dielectric arm: roughness %.4f, base %.1f metalness %.1f: the plate's voxel radiance %.6f "
                            "(%d voxels)\n", rough[k], plates[c].base, plates[c].metal, rad[c], n);
                GiParams off; off.mode = GiMode::Off;
                s->setGlobalIllumination(off);
                rv->setScene(nullptr);
                e->destroyScene(s);
            }
            double A = 0.0, B = 0.0;
            enginetest::ggxSpecularAlbedo(1.0, rough[k], A, B, 512);
            // the incident, in the store's units: the white metal re-emits A + B of it
            const double incident = rad[0] / (A + B);
            const double want = 0.04 * A + B;
            const double black = rad[1] / incident;
            const double dd = enginetest::disneyDiffuseAlbedo(1.0, rough[k], 256);
            const double wantTotal = (0.6 * dd + want) / (dd + want), total = rad[2] / rad[3];
            char msg[320];
            std::snprintf(msg, sizeof msg, "dielectric, roughness %.4f: a BLACK dielectric re-emits its lobe, 0.04 A + B "
                          "of the incident (%.5f; want %.5f, bar 0.4 %%)", rough[k], black, want);
            CHECK(rad[0] > 0.0 && std::fabs(black / want - 1.0) <= 0.004, msg);
            std::snprintf(msg, sizeof msg, "dielectric, roughness %.4f: a b 0.6 dielectric's total holds the lobe beside "
                          "the diffuse one, against b 1 (%.5f; want %.5f, the diffuse lobe alone 0.6; bar 0.4 %%)",
                          rough[k], total, wantTotal);
            CHECK(rad[3] > 0.0 && std::fabs(total / wantTotal - 1.0) <= 0.004, msg);
        }
    }

    }
    // ---- THE TINTED METAL AND THE MOVER (decision 22.1) ----------------------------------------------
    if (route)
    // A TINTED metal (base 1 / 0.6 / 0.3, metalness 1, r 0.3): the lobe F0 A + w B per CHANNEL, F0 = b, w = 1 - so a
    // store or a route that swapped the two (A + b B) reads a different colour. The store against the white diffuser,
    // channel by channel, bar 1.5 %. THEN THE ROUTE: with the screen-probe gather on and the SURFACE CACHE ON
    // (ENGINE-POLISH-PHOTON-1 stage 3: the cards-off arm is deleted), a still metal's hits read its CARD first and a
    // MOVER's go to the decode (jahHitAlwaysDecodes). The card re-emits the lobe the store does (the capture's
    // Specular layer, the relight's flat term from the same table), and the decode the same lobe (800.Atom_piece_ps.any,
    // atomGatherMetalLobe): (i) the card's stored flat term is the closed form E ( F0 A + w B ) per channel within
    // 0.4 % plus the radiance format's half step (R11G11B10F: 6 / 6 / 5 mantissa bits - the card's own quantum);
    // (ii) the panel's gathered E/pi from the still plate is the mover's times the card's stored / exact ratio, per
    // channel within 0.4 % - the card answers what it stores. THE CARD LEG SITS UP TO 0.9 % (blue) FROM THE DECODE
    // because of the Radiance / Flat layers' format (R11G11B10F: the stored values are the nearest representable to
    // the closed form - the format's half step is the format's, not slack; wider formats are a later decision, not
    // ENGINE-POLISH-PHOTON-1's). And THE STORE'S LEG: the same plate still on the uncarded cube, its hits reading
    // the store's material and direct term, against the mover within 0.4 %. The gather's sample sequence is held
    // (freezeFrameIndex): the two arms trace the same rays, and their difference is the route's alone.
    // THE DIELECTRIC'S ROUTE (ENGINE-POLISH-PHOTON-1 stage 2): the same plate, a BLACK dielectric first (its
    // re-emission is its 0.04 lobe alone; the material edited in place, the gather's view kept), then the tinted
    // metal: the panel's gathered E/pi from the black plate is (0.04 A + B) / (A + B) of the tinted metal's RED
    // channel (base 1 there: F0 = 1), the panel's view factor cancelling (every plate point sees the sun at cosine
    // 1) - as a MOVER (the decode, atomGatherMetalLobe) within 0.4 %, and still (its card) by the card's stored values
    // the same way as the metal's. The base re-emitted nothing from a dielectric on either route.
    // (The dielectric FIRST: the gather's held history keeps ~0.1 % of the previous plate after 48 frames, which is
    // 2.5 % of the dielectric's small answer after the metal and nothing of the metal's after the dielectric.) THE
    // METAL EDIT IS MADE WHILE THE PLATE IS A MOVER (BUGS-1 item 3): a mover is in no voxel, so the edit re-voxelises
    // nothing, but the store's row of that material must be the edited one when the plate turns still and is
    // voxelised again - ENGINE-POLISH-PHOTON-1's finding (the still arm read the old row: .198 for .008).
    {
        const float tint[3] = { 1.0f, 0.6f, 0.3f };
        double diffuserRgb[3] = {}, metalRgb[3] = {};
        double gathered[2][3] = {}, dielectric[2][3] = {};
        // the card's stored flat term at the plate's top, black dielectric / tinted metal, and whether it answered
        double cardFlat[2][3] = {};
        double storeLeg[3] = {};   // the tinted metal still on the uncarded cube: the store's route
        bool cardLit[2] = { false, false };
        bool gatherRan = false;
        for (int pass = 0; pass < 2; ++pass) {
            Scene *s = e->createScene("metaltint");
            CHECK(rv->setScene(s), "the view shows the tinted scene");
            float sh[27] = { 0 };
            s->setAmbientSh(sh);
            s->setEnvironmentLight(Colour(0, 0, 0, 1));
            CHECK(enginetest::addDirectionalLight(s, Vec3(0.0f, -1.0f, 0.0f), 3.0f) != 0, "the sun straight down");
            const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
            // the plate's mesh CARRIES CARDS (its six faces; the surface cache builds them where rays run)
            MeshData cardedCube = enginetest::unitCubeMesh();
            cardedCube.cards = enginetest::boxCards(0.5f);
            const MeshId cardedMesh = s->createMesh(cardedCube);
            PbrParams plate;
            plate.albedo = pass ? Colour(tint[0], tint[1], tint[2]) : Colour(1.0f, 1.0f, 1.0f);
            plate.roughness = 0.3f;
            plate.metalness = pass ? 1.0f : 0.0f;
            const NodeId plateNode = s->createNode();
            const MaterialId plateMat = s->createPbrMaterial(plate);
            s->attachMesh(plateNode, cardedMesh, plateMat);
            s->setNodeTransform(plateNode, Vec3(0.0f, 0.05f, 0.0f), Quat(), Vec3(2.0f, 0.1f, 2.0f));
            PbrParams panel; panel.albedo = Colour(1, 1, 1); panel.roughness = 1.0f;
            const NodeId panelNode = s->createNode();
            s->attachMesh(panelNode, cube, s->createPbrMaterial(panel));
            s->setNodeTransform(panelNode, Vec3(0.0f, 0.85f, 1.15f), Quat(), Vec3(2.0f, 1.5f, 0.1f));
            enginetest::testCameraLookAt(rv, Vec3(0.0f, 0.9f, -2.0f), Vec3(0.0f, 0.75f, 1.1f));
            s->setGlobalIllumination(giOn(GiToggle::Off, 1));
            settle(s);
            GiVoxelVolume v;
            double rgb[3] = {};
            int n = 0;
            if (s->giVoxelVolume(0, v) && v.available)
                for (int dz = -2; dz <= 2; ++dz)
                    for (int dx = -2; dx <= 2; ++dx) {
                        const int cx = int((0.0f - v.origin[0]) / v.cell[0]) + dx;
                        const int cy = int((0.05f - v.origin[1]) / v.cell[1]);
                        const int cz = int((0.0f - v.origin[2]) / v.cell[2]) + dz;
                        const size_t i = 4 * (size_t(cx) + size_t(v.width) * (size_t(cy) + size_t(v.height) * size_t(cz)));
                        if (v.light[i + 3] > 0.0f) {
                            for (int c = 0; c < 3; ++c) rgb[c] += v.light[i + c] / v.light[i + 3];
                            ++n;
                        }
                    }
            for (int c = 0; c < 3; ++c) (pass ? metalRgb : diffuserRgb)[c] = n ? rgb[c] / n : -1.0;
            if (pass == 1 && e->rayQueryAvailable() && e->rayTracing()) {
                // the gather's own view (the screen-probe gather runs at High, on a picture its stride resolves)
                View *gv = e->createOffscreenView("metalroute", 256, 256, Colour(0, 0, 0));
                PostFxDesc gfx;
                gfx.allowOffscreen = true;
                gfx.ssr = 0;
                gv->setPostFx(gfx);
                gv->setShadows(true);
                gv->setOffscreenContract(OffscreenContract::StillPicture);
                rv->setScene(nullptr);
                CHECK(gv->setScene(s), "the gather's view shows the tinted scene");
                enginetest::testCameraLookAt(gv, Vec3(0.0f, 0.9f, -2.0f), Vec3(0.0f, 0.75f, 1.1f));
                GiParams g = giOn(GiToggle::Off, 1);
                g.quality = GiQuality::High;
                g.gather = GiToggle::On;
                s->setGlobalIllumination(g);
                GatherTuning t;
                t.readback = true;
                t.restOff = true;
                t.freezeFrameIndex = true;
                s->setGatherTuning(t);
                // arms 0 / 1 the black dielectric still / a mover; 2 / 3 the tinted metal still / a mover; 4 THE STORE'S
                // LEG: the tinted metal still on the UNCARDED cube - its hits read the store's material and direct
                // term (jahHitDirect) on purpose, the route the cards answer ahead of
                PbrParams black = plate;
                black.albedo = Colour(0.0f, 0.0f, 0.0f);
                black.metalness = 0.0f;
                CHECK(s->setPbrMaterial(plateMat, black), "the plate turns a black dielectric");
                s->refreshGlobalIllumination();
                for (int arm = 0; arm < 5; ++arm) {
                    if (arm == 1 || arm == 3) s->setNodeMovable(plateNode, true);
                    if (arm == 2) {
                        CHECK(s->setPbrMaterial(plateMat, plate), "the plate turns the tinted metal WHILE A MOVER");
                        s->setNodeMovable(plateNode, false);
                        s->refreshGlobalIllumination();
                    }
                    if (arm == 4) {
                        s->setNodeMovable(plateNode, false);
                        CHECK(s->attachMesh(plateNode, cube, plateMat), "the plate on the uncarded cube (the store's leg)");
                        s->refreshGlobalIllumination();
                    }
                    settle(s);
                    run(48);   // the held sequence's history converged on the arm's route
                    const GatherStatus st = s->published().gi.gather;
                    double sum[3] = {};
                    unsigned px = 0;
                    if (!st.irradiance.empty())
                        for (unsigned y = 80; y < 128; ++y)
                            for (unsigned x = 96; x < 160; ++x) {
                                const float *q = &st.irradiance[(size_t(y) * st.irradianceW + x) * 4u];
                                if (q[3] <= 0.0f) continue;
                                for (int c = 0; c < 3; ++c) sum[c] += q[c];
                                ++px;
                            }
                    double *got = arm == 4 ? storeLeg : arm >= 2 ? gathered[arm - 2] : dielectric[arm];
                    for (int c = 0; c < 3; ++c) got[c] = px ? sum[c] / px : -1.0;
                    if (arm == 0 || arm == 2) {   // a still arm: what the plate's card holds at its top
                        CardSample cs;
                        const int k = arm ? 1 : 0;
                        cardLit[k] = s->readCardAt(Vec3(0.0f, 0.1f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), cs, plateNode) && cs.lit;
                        for (int c = 0; c < 3; ++c) cardFlat[k][c] = cs.flat[c];
                        std::printf("the route: the %s plate's card at its top: lit %d, flat %.6f %.6f %.6f, radiance %.6f "
                                    "%.6f %.6f, F0 kS %.4f %.4f %.4f, kS %.4f\n", k ? "tinted metal" : "black dielectric",
                                    int(cardLit[k]), cs.flat[0], cs.flat[1], cs.flat[2], cs.radiance[0], cs.radiance[1],
                                    cs.radiance[2], cs.specularF0[0], cs.specularF0[1], cs.specularF0[2], cs.specularWeight);
                    }
                    std::printf("the route: the %s plate %s, the panel's gathered E/pi %.6f %.6f %.6f (%u px; readback "
                                "%ux%u, %zu floats, gather running %d)\n", arm < 2 ? "black dielectric" : "tinted metal",
                                arm == 1 || arm == 3 ? "a MOVER (the decode)" : arm == 4 ? "still, NO CARDS (the store)"
                                                                                        : "still (its card)",
                                got[0], got[1], got[2], px,
                                st.irradianceW, st.irradianceH, st.irradiance.size(), int(st.running));
                }
                gatherRan = true;
                gv->setScene(nullptr);
                e->destroyView(gv);
            }
            GiParams off; off.mode = GiMode::Off;
            s->setGlobalIllumination(off);
            rv->setScene(nullptr);
            e->destroyScene(s);
        }
        double A = 0.0, B = 0.0;
        enginetest::ggxSpecularAlbedo(1.0, 0.3, A, B, 512);
        const double dd = enginetest::disneyDiffuseAlbedo(1.0, 0.3, 256);
        // (the white diffuser's own 0.04 lobe in the reference: ENGINE-POLISH-PHOTON-1 stage 2)
        const double ref = dd + 0.04 * A + B;
        for (int c = 0; c < 3; ++c) {
            const double want = (tint[c] * A + B) / ref, swapped = (A + tint[c] * B) / ref;
            char msg[300];
            std::snprintf(msg, sizeof msg, "tinted metal, channel %d: the store holds F0 A + w B of the diffuser's "
                          "(%.4f; want %.4f, the two swapped %.4f; bar 1.5 %%)", c, metalRgb[c] / diffuserRgb[c], want,
                          swapped);
            CHECK(diffuserRgb[c] > 0.0 && std::fabs(metalRgb[c] / diffuserRgb[c] / want - 1.0) <= 0.015, msg);
        }
        if (gatherRan) {
            // THE SUN'S IRRADIANCE / pi ON THE PLATE (addDirectionalLight's power 3 is pi x the intensity: a facing
            // Lambertian of albedo a sends a x 3 / pi; IMAGE-1's units) and the lobes' closed forms
            const double sun = 3.0 / 3.14159265358979323846;
            const double exact[2][3] = { { sun * (0.04 * A + B), sun * (0.04 * A + B), sun * (0.04 * A + B) },
                                         { sun * (tint[0] * A + B), sun * (tint[1] * A + B), sun * (tint[2] * A + B) } };
            // the radiance layer's half step at a value (R11G11B10F: 6 mantissa bits on red and green, 5 on blue)
            const auto halfStep = [](double v, int c) {
                return v > 0.0 ? 0.5 * std::ldexp(1.0, std::ilogb(v) - (c == 2 ? 5 : 6)) : 0.0;
            };
            for (int k = 0; k < 2; ++k)
                for (int c = 0; c < 3; ++c) {
                    const double bar = 0.004 * exact[k][c] + halfStep(exact[k][c], c);
                    char msg[320];
                    std::snprintf(msg, sizeof msg, "THE %s'S CARD HOLDS ITS LOBE, channel %d: the card's flat term %.6f, "
                                  "the closed form E ( F0 A + w B ) %.6f (bar 0.4 %% + the format's half step: %.6f)",
                                  k ? "TINTED METAL" : "BLACK DIELECTRIC", c, cardFlat[k][c], exact[k][c], bar);
                    CHECK(cardLit[k] && std::fabs(cardFlat[k][c] - exact[k][c]) <= bar, msg);
                }
            for (int c = 0; c < 3; ++c) {
                const double want = cardLit[1] ? cardFlat[1][c] / exact[1][c] : 0.0;
                char msg[320];
                std::snprintf(msg, sizeof msg, "THE ROUTE DOES NOT MOVE THE BOUNCE, channel %d: the panel's gathered E/pi "
                              "from the tinted metal by its card %.5f, as a mover %.5f (%.4f; the card's stored / exact "
                              "%.4f; bar 0.4 %%)", c, gathered[0][c], gathered[1][c], gathered[0][c] / gathered[1][c], want);
                CHECK(gathered[1][c] > 0.0 && want > 0.0 &&
                      std::fabs(gathered[0][c] / gathered[1][c] / want - 1.0) <= 0.004, msg);
            }
            for (int c = 0; c < 3; ++c) {
                char msg[320];
                std::snprintf(msg, sizeof msg, "THE STORE'S LEG, channel %d: the panel's gathered E/pi from the tinted metal "
                              "still on the uncarded cube (the store's route) %.5f, as a mover %.5f (%.4f; bar 0.4 %%)", c,
                              storeLeg[c], gathered[1][c], gathered[1][c] > 0.0 ? storeLeg[c] / gathered[1][c] : 0.0);
                CHECK(gathered[1][c] > 0.0 && std::fabs(storeLeg[c] / gathered[1][c] - 1.0) <= 0.004, msg);
            }
            const double wantD = (0.04 * A + B) / (A + B);
            for (int c = 0; c < 3; ++c) {
                const double ratio = gathered[1][0] > 0.0 ? dielectric[1][c] / gathered[1][0] : 0.0;
                char msg[320];
                std::snprintf(msg, sizeof msg, "THE DIELECTRIC'S LOBE BY THE DECODE (a mover), channel %d: the panel's "
                              "gathered E/pi from the black dielectric is (0.04 A + B) / (A + B) of the white-red metal's "
                              "(%.5f; want %.5f, bar 0.4 %%)", c, ratio, wantD);
                CHECK(dielectric[1][c] > 0.0 && std::fabs(ratio / wantD - 1.0) <= 0.004, msg);
            }
            for (int c = 0; c < 3; ++c) {
                const double want = cardLit[0] && dielectric[1][c] > 0.0 ? cardFlat[0][c] / exact[0][c] : 0.0;
                char msg[320];
                std::snprintf(msg, sizeof msg, "THE DIELECTRIC'S LOBE BY ITS CARD (still), channel %d: the panel's "
                              "gathered E/pi %.6f, as a mover %.6f (%.4f; the card's stored / exact %.4f; bar 0.4 %%)", c,
                              dielectric[0][c], dielectric[1][c],
                              dielectric[1][c] > 0.0 ? dielectric[0][c] / dielectric[1][c] : 0.0, want);
                CHECK(want > 0.0 && std::fabs(dielectric[0][c] / dielectric[1][c] / want - 1.0) <= 0.004, msg);
            }
        }
        else std::printf("ok: no ray queries on this machine - the route arm skips\n");
    }

    if (!route && !gate) {
    // ---- THE BOUNCE ARM (the furnace's answer) --------------------------------------------------
    const Case furnace[4] = { { 0.3f, 0.0f }, { 0.3f, 1.0f }, { 0.9f, 0.0f }, { 0.9f, 1.0f } };
    double reemit[4] = {};
    for (int c = 0; c < 4; ++c) {
        Scene *s = e->createScene("metalfurnace");
        CHECK(rv->setScene(s), "the view shows the furnace");
        float sh[27] = { 0 };
        s->setAmbientSh(sh);
        s->setEnvironmentLight(Colour(0, 0, 0, 1));
        const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
        const auto slab = [&](const Vec3 &pos, const Vec3 &scale, const PbrParams &p) {
            const NodeId nd = s->createNode();
            s->attachMesh(nd, cube, s->createPbrMaterial(p));
            s->setNodeTransform(nd, pos, Quat(), scale);
        };
        PbrParams plate;
        plate.albedo = Colour(1.0f, 1.0f, 1.0f);
        plate.roughness = furnace[c].rough;
        plate.metalness = furnace[c].metal;
        slab(Vec3(0.0f, 0.05f, 0.0f), Vec3(2.0f, 0.1f, 2.0f), plate);
        // the emitter above it: radiance 1, albedo 0 (it reflects nothing back - one bounce is the answer)
        PbrParams lamp;
        lamp.albedo = Colour(0, 0, 0);
        lamp.roughness = 1.0f;
        lamp.emissive = Colour(1.0f, 1.0f, 1.0f);
        slab(Vec3(0.0f, 1.55f, 0.0f), Vec3(4.0f, 0.1f, 4.0f), lamp);
        enginetest::testCameraLookAt(rv, Vec3(0.0f, 0.8f, -3.0f), Vec3(0.0f, 0.1f, 0.0f));
        // TWO bounces: the store's own bounce pass (a document's first bounce is the pixel's gather and runs
        // none in the store - PhotonVoxelLighting::update)
        s->setGlobalIllumination(giOn(GiToggle::Off, 2));
        settle(s);
        int n = 0;
        reemit[c] = plateRadiance(s, n);
        std::printf("bounce: roughness %.1f metalness %.1f: the plate's voxel radiance %.5f (%d voxels)\n",
                    furnace[c].rough, furnace[c].metal, reemit[c], n);
        GiParams off; off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        rv->setScene(nullptr);
        e->destroyScene(s);
    }
    for (int c = 1; c < 4; c += 2) {
        CHECK(reemit[c - 1] > 0.0, "the diffuser re-emits what it gathers (the reference)");
        const double want = bounceRatio(furnace[c].metal, furnace[c].rough);
        char msg[300];
        std::snprintf(msg, sizeof msg, "bounce, roughness %.1f: the white metal re-emits its lobe's hemispherical "
                      "albedo against the diffuser's (%.4f; want %.4f, bar 1.5 %%)", furnace[c].rough,
                      reemit[c] / reemit[c - 1], want);
        CHECK(std::fabs(reemit[c] / reemit[c - 1] / want - 1.0) <= 0.015, msg);
    }

    // ---- THE LIVE EDITS -------------------------------------------------------------------------
    {
        Scene *s = e->createScene("metaledits");
        CHECK(rv->setScene(s), "the view shows the edit scene");
        float sh[27] = { 0 };
        s->setAmbientSh(sh);
        s->setEnvironmentLight(Colour(0, 0, 0, 1));
        CHECK(enginetest::addDirectionalLight(s, Vec3(0.0f, -1.0f, 0.0f), 3.0f) != 0, "the sun straight down");
        const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
        const NodeId nd = s->createNode();
        PbrParams plate;
        plate.albedo = Colour(1.0f, 1.0f, 1.0f);
        plate.roughness = 0.6f;
        plate.metalness = 0.0f;
        const MaterialId plateMat = s->createPbrMaterial(plate);
        s->attachMesh(nd, cube, plateMat);
        s->setNodeTransform(nd, Vec3(0.0f, 0.05f, 0.0f), Quat(), Vec3(2.0f, 0.1f, 2.0f));
        enginetest::testCameraLookAt(rv, Vec3(0.0f, 0.9f, -2.0f), Vec3(0.0f, 0.1f, 0.0f));
        s->setGlobalIllumination(giOn(GiToggle::Off, 1));
        settle(s);
        const auto rebuilds = [&]() {
            const GiStatus st = s->published().gi;
            return st.cascades.empty() ? 0ull : st.cascades[0].rebuilds;
        };
        const unsigned long long r0 = rebuilds();
        int n = 0;
        const double before = plateRadiance(s, n);
        PbrParams edit = plate;
        edit.roughness = 0.3f;
        CHECK(s->setPbrMaterial(plateMat, edit), "the roughness edit applies");
        s->refreshGlobalIllumination();   // the host's settle (the mirror's plan does it in the app)
        settle(s);
        const unsigned long long r1 = rebuilds();
        char msg[240];
        std::snprintf(msg, sizeof msg, "a roughness-only edit re-voxelises the cascade exactly once (%llu)", r1 - r0);
        CHECK(r1 - r0 == 1ull, msg);
        const double mid = plateRadiance(s, n);
        {   // THE ROUGHNESS IS IN THE STORE (MAIN-REDS-1's fix round, carried from gi.voxel_metal_albedo): the voxel holds
            // the light its surface scatters through HlmsPbs's normalised Disney diffuse lobe - the injection's direct
            // term times the lobe's directional albedo at the sun's cosine (1 here: the sun straight down on the plate's
            // top), the bounce's re-emission times its hemispherical mean. Both rise from r 0.6 to 0.3 (the lobe's energy
            // factor, lerp(1, 1/1.51, r), grows as r falls), so the stored radiance rises, by a ratio between the two
            // factors' - computed from the lobes (lobeDirect / lobeHemi), within the 1 % the store's ratios
            // carry (the renderer's jahDiffuseAlbedo is a fit to the DFG table, the quantum 0.1 %).
            // (ENGINE-POLISH-PHOTON-1 stage 2: the diffuser's own 0.04 specular lobe is in both factors.)
            const double direct = lobeDirect(0.0, 0.3) / lobeDirect(0.0, 0.6);
            const double hemi = lobeHemi(0.0, 0.3) / lobeHemi(0.0, 0.6), lo = std::min(direct, hemi),
                         hi = std::max(direct, hemi);
            const double got = before > 0.0 ? mid / before : 0.0;
            char rmsg[260];
            std::snprintf(rmsg, sizeof rmsg, "...and the STORE follows the roughness: the plate's radiance x%.4f, "
                          "between the lobe's direct x%.4f and hemispherical x%.4f factors (bar 1 %%)", got, direct, hemi);
            CHECK(got > 1.0 && got >= lo - 0.01 && got <= hi + 0.01, rmsg);
        }
        edit.metalness = 0.5f;
        CHECK(s->setPbrMaterial(plateMat, edit), "the metalness edit applies");
        s->refreshGlobalIllumination();
        settle(s);
        const unsigned long long r2 = rebuilds();
        std::snprintf(msg, sizeof msg, "a metalness edit re-voxelises the cascade exactly once (%llu)", r2 - r1);
        CHECK(r2 - r1 == 1ull, msg);
        const double after = plateRadiance(s, n);
        std::printf("live edits: rebuilds %llu -> %llu -> %llu; the plate's radiance %.5f -> %.5f (roughness 0.3) "
                    "-> %.5f (metalness 0.5)\n", r0, r1, r2, before, mid, after);
        const double want = directRatio(0.5, 0.3);
        std::snprintf(msg, sizeof msg, "...and the store then holds the lobes' ratio of what it held (%.4f; want %.4f, "
                      "bar 1.5 %%)", after / mid, want);
        CHECK(std::fabs(after / mid / want - 1.0) <= 0.015, msg);
        GiParams off; off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        rv->setScene(nullptr);
        e->destroyScene(s);
    }
    }
    // ---- THE MOVERS' GATE ON A METAL'S CARD (ENGINE-POLISH-PHOTON-1's fix round) ----------------------------
    // A ray's hit lowers its INDIRECT share by the movers above it (MOVER-OCCLUSION-1: jahHitIndirectGate, four short
    // rays against the movers): the store gates its whole bounce (jah_rq_hit_radiance.glsl), and the card read must cut
    // the same share - for a metal that share is the specular LOBE's (the Flat term), not the diffuse half's. THE
    // FIXTURE: the tinted metal plate (r 0.3) on a carded cube under a uniform sky, NO sun (its indirect is the whole
    // answer), the white panel, the screen-probe gather with its sequence held; a black non-reflecting MOVER slab
    // roofing the plate 1.8 m up (above the panel's top) or parked 40 m away. THE LAW: the gate is a factor on the
    // indirect whichever route answers - under the roof every gate ray is blocked, so the plate's whole sky lobe goes on
    // BOTH routes. The panel's gathered E/pi is the sky it sees PLUS the plate's share, so the plate's share is
    // isolated by the same geometry with a BLACK plate (no lobe, no diffuse: what the panel gathers then is the sky
    // and the roof alone, route-free): per channel ( route under - black under ) / ( route away - black away ) is the
    // same on the card and the store, within 0.004 (0.4 % of the share away). The base's card read cut the diffuse
    // half only: a metal's card kept its whole lobe under the roof (its ratio 1).
    if (gate && e->rayQueryAvailable() && e->rayTracing()) {
        Scene *s = e->createScene("metalgate");
        const unsigned char kSky = 160;
        const unsigned char skyPx[4] = { kSky, kSky, kSky, 255 };
        SkyDesc sky;
        sky.mode = SkyMode::Equirectangular;
        sky.equirect = s->createTexture(1, 1, skyPx, true);
        CHECK(sky.equirect && s->setSky(sky), "the uniform sky binds");
        float sh[27] = { 0 };
        run(1);
        for (int f = 0; f < 20 && !s->published().copySkyAmbientSh(sh); ++f) run(1);
        s->setAmbientSh(sh);
        s->setEnvironmentLight(Colour(1, 1, 1, 1));
        const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
        MeshData cardedCube = enginetest::unitCubeMesh();
        cardedCube.cards = enginetest::boxCards(0.5f);
        const MeshId cardedMesh = s->createMesh(cardedCube);
        PbrParams plate;
        plate.albedo = Colour(1.0f, 0.6f, 0.3f);
        plate.roughness = 0.3f;
        plate.metalness = 1.0f;
        const MaterialId plateMat = s->createPbrMaterial(plate);
        const NodeId plateNode = s->createNode();
        s->attachMesh(plateNode, cardedMesh, plateMat);
        s->setNodeTransform(plateNode, Vec3(0.0f, 0.05f, 0.0f), Quat(), Vec3(2.0f, 0.1f, 2.0f));
        PbrParams panel; panel.albedo = Colour(1, 1, 1); panel.roughness = 1.0f;
        const NodeId panelNode = s->createNode();
        s->attachMesh(panelNode, cube, s->createPbrMaterial(panel));
        s->setNodeTransform(panelNode, Vec3(0.0f, 0.85f, 1.15f), Quat(), Vec3(2.0f, 1.5f, 0.1f));
        // the mover: black, no specular (it sends the panel nothing of its own), a slab over the plate's middle
        PbrParams black;
        black.albedo = Colour(0, 0, 0);
        black.roughness = 1.0f;
        black.workflow = PbrParams::Workflow::Specular;
        black.ior = 1.0f;
        black.specularColour = Colour(0, 0, 0);
        const NodeId moverNode = s->createNode();
        s->attachMesh(moverNode, cube, s->createPbrMaterial(black));
        s->setNodeMovable(moverNode, true);
        // a 20 m roof over the plate ABOVE the panel's top (1.6 m): every one of the gate's four rays from every plate
        // point meets it (they reach 4 m; 60 degrees off the normal is 3.1 m out at its height), none of the plate is
        // hidden from the panel. A PARTIAL roof (3 x 2.4 m) reads card 0.664 / store 0.657 - the two routes' sky lobes
        // differ in level and in spread across the plate (~15 % here), so a partial gate weights them differently.
        const Vec3 moverAway(40.0f, 1.9f, 0.0f), moverOver(0.0f, 1.9f, -0.3f);
        const Vec3 moverScale(20.0f, 0.05f, 20.0f);
        s->setNodeTransform(moverNode, moverAway, Quat(), moverScale);
        View *gv = e->createOffscreenView("metalgate", 256, 256, Colour(0, 0, 0));
        PostFxDesc gfx;
        gfx.allowOffscreen = true;
        gfx.ssr = 0;
        gv->setPostFx(gfx);
        gv->setShadows(true);
        gv->setOffscreenContract(OffscreenContract::StillPicture);
        CHECK(gv->setScene(s), "the gather's view shows the gate's scene");
        enginetest::testCameraLookAt(gv, Vec3(0.0f, 0.9f, -2.0f), Vec3(0.0f, 0.75f, 1.1f));
        GiParams g = giOn(GiToggle::Off, 1);
        g.quality = GiQuality::High;
        g.gather = GiToggle::On;
        s->setGlobalIllumination(g);
        GatherTuning t;
        t.readback = true;
        t.restOff = true;
        t.freezeFrameIndex = true;
        s->setGatherTuning(t);
        // arms: 0 the card, the mover away; 1 the card, the mover over; 2 the store (uncarded), over; 3 the store,
        // away; 4 the BLACK plate, away; 5 the black plate, over
        double got[6][3] = {};
        bool cardLit = false;
        const MaterialId blackMat = s->createPbrMaterial(black);
        for (int arm = 0; arm < 6; ++arm) {
            if (arm == 2) {
                CHECK(s->attachMesh(plateNode, cube, plateMat), "the plate on the uncarded cube (the store's route)");
                s->refreshGlobalIllumination();
            }
            if (arm == 4) {
                CHECK(s->attachMesh(plateNode, cube, blackMat), "the plate turns black (the sky's share alone)");
                s->refreshGlobalIllumination();
            }
            s->setNodeTransform(moverNode, arm == 1 || arm == 2 || arm == 5 ? moverOver : moverAway, Quat(), moverScale);
            settle(s);
            run(72);   // the held history's carry-over of the previous arm (0.1 % after 48 frames) under 1e-4
            const GatherStatus st = s->published().gi.gather;
            double sum[3] = {};
            unsigned px = 0;
            if (!st.irradiance.empty())
                for (unsigned y = 80; y < 128; ++y)
                    for (unsigned x = 96; x < 160; ++x) {
                        const float *q = &st.irradiance[(size_t(y) * st.irradianceW + x) * 4u];
                        if (q[3] <= 0.0f) continue;
                        for (int c = 0; c < 3; ++c) sum[c] += q[c];
                        ++px;
                    }
            for (int c = 0; c < 3; ++c) got[arm][c] = px ? sum[c] / px : -1.0;
            if (arm == 0) {
                CardSample cs;
                cardLit = s->readCardAt(Vec3(0.0f, 0.1f, 0.6f), Vec3(0.0f, 1.0f, 0.0f), cs, plateNode) && cs.lit;
                std::printf("the gate: the plate's card lit %d, flat %.6f %.6f %.6f, stored indirect %.6f %.6f %.6f\n",
                            int(cardLit), cs.flat[0], cs.flat[1], cs.flat[2], cs.indirectStored[0], cs.indirectStored[1],
                            cs.indirectStored[2]);
            }
            static const char *what[6] = { "its card, the mover away", "its card, the mover over it",
                                           "the store (no cards), the mover over it", "the store, the mover away",
                                           "nothing (a black plate), the mover away",
                                           "nothing (a black plate), the mover over it" };
            std::printf("the gate: the plate by %s, the panel's gathered E/pi %.6f %.6f %.6f (%u px)\n", what[arm],
                        got[arm][0], got[arm][1], got[arm][2], px);
        }
        CHECK(cardLit, "the plate's card answers (lit)");
        for (int c = 0; c < 3; ++c) {
            const double cardAway = got[0][c] - got[4][c], storeAway = got[3][c] - got[4][c];
            const double card = cardAway > 0.0 ? (got[1][c] - got[5][c]) / cardAway : 0.0;
            const double store = storeAway > 0.0 ? (got[2][c] - got[5][c]) / storeAway : 0.0;
            char msg[320];
            std::snprintf(msg, sizeof msg, "THE MOVER GATES THE METAL'S LOBE ON EVERY ROUTE, channel %d: the plate's share "
                          "under the mover / away, by its card %.4f, by the store %.4f (the shares away %.5f / %.5f; "
                          "bar 0.004)", c, card, store, cardAway, storeAway);
            CHECK(cardAway > 0.0 && storeAway > 0.0 && store < 0.5 && std::fabs(card - store) <= 0.004, msg);
        }
        GiParams off; off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        gv->setScene(nullptr);
        e->destroyView(gv);
        e->destroyScene(s);
    }
    else if (gate) std::printf("ok: no ray queries on this machine - the gate arm skips\n");
    e->destroyView(rv);
    std::printf("frames rendered: %llu\n", rendered);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
