// gi.voxel_metal_albedo — A METAL RE-EMITS ITS DIFFUSE LOBE, base x (1 - metalness), INTO THE
// BOUNCE (PHOTON-MARCH-1 stage 2, VOXEL-METAL-ALBEDO-1; DEBTS 2026-10-13).
//
// THE LAW: a voxel stores the radiance its surface scatters DIFFUSELY - the light injection's
// direct term and the bounce's re-emission both multiply the voxel's albedo - and in the metallic
// workflow HlmsPbs's diffuse lobe is base x (1 - metalness) (the rest of the base colour is the
// specular F0: 800.PixelShader_piece_ps.any, diffuse -= diffuse x metalness). So a plate of base
// colour B and metalness m, under the same sun as a diffuser of the same B, stores (1 - m) of the
// diffuser's radiance, and a white receiver facing it, lit by nothing else, takes (1 - m) of the
// diffuser's bounce. BARS: the store's ratio within 1 % (one half-float quantum of a lit voxel is
// 0.1 %), the receiver's within 0.03 of the diffuser's bounce (gi.env_cone's 3 % bar; 0.005 for the
// pure metal, which has nothing left to re-emit), field off and on. The base stored the base colour
// whatever the metalness: every ratio read 1.
//
// THE FIXTURE: a sun straight down (no sky, no ambient: the receiver's face sees the plate's
// bounce and nothing else); a 2 x 0.1 x 2 m plate of base colour 0.8 at the origin, metalness 0 /
// 0.5 / 1 (three scenes); a white matte panel standing beyond the plate's far edge, its face
// looking back at the plate and parallel to the sun (no direct light on it); one 12 m cascade of
// 64 cells, Medium, one bounce, no gather; the camera facing the panel. The receiver's reading is
// the panel's GI-on value minus its GI-off value (the control: ~0).
// THE LIVE EDITS (the voxel inputs of a PBR edit, OgreMaterials.cpp): on the diffuser, a roughness-only
// edit (0.6 -> 0.3) and then a metalness edit (0 -> 0.5), each settled by the host's refresh, re-voxelise
// the cascade exactly once and the store follows them.
// THE BASE (irisgl d46046f), measured: the store and the bounce 1.000 of the diffuser's at metalness
// 0.5 and 1 (the base colour stood for the albedo), and either live edit re-voxelised nothing (0) and
// moved no voxel (spikes/photon-march-1/stage2-row-base.log). After: 0.500 / 0.000, one rebuild each.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

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

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-voxel-metal-albedo-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();

    View *rv = e->createOffscreenView("metalalbedo", 64, 64, Colour(0, 0, 0));
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
            enginetest::frames(e, 1);
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
    const float kMetal[3] = { 0.0f, 0.5f, 1.0f };
    double store[3][2] = {}, bounce[3][2] = {};
    const std::pair<GiToggle, const char *> arms[2] = { { GiToggle::Off, "field off" }, { GiToggle::On, "field on" } };
    for (int m = 0; m < 3; ++m) {
        Scene *s = e->createScene("metalalbedo");
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
        plate.albedo = Colour(0.8f, 0.8f, 0.8f);
        plate.roughness = 0.6f;
        plate.metalness = kMetal[m];
        const MaterialId plateMat = slab(Vec3(0.0f, 0.05f, 0.0f), Vec3(2.0f, 0.1f, 2.0f), plate);
        PbrParams panel; panel.albedo = Colour(1, 1, 1); panel.roughness = 1.0f;
        slab(Vec3(0.0f, 0.85f, 1.15f), Vec3(2.0f, 1.5f, 0.1f), panel);
        enginetest::testCameraLookAt(rv, Vec3(0.0f, 0.9f, -2.0f), Vec3(0.0f, 0.75f, 1.1f));
        GiParams off; off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        enginetest::frames(e, 10);
        const double dark = readHdr();
        for (int a = 0; a < 2; ++a) {
            GiParams gi;
            gi.mode = GiMode::Vct;
            gi.quality = GiQuality::Medium;
            gi.numBounces = 1;
            gi.ddgi = arms[a].first;
            gi.gather = GiToggle::Off;
            gi.cascadeCount = 1;
            gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 6.0f, 64, 0.0f };
            s->setGlobalIllumination(gi);
            enginetest::frames(e, 20);
            for (int f = 0; f < 4000 && !s->giStatus().giAtRest; ++f) enginetest::frames(e, 1);
            bounce[m][a] = readHdr() - dark;
            int n = 0;
            store[m][a] = plateRadiance(s, n);
            std::printf("metalness %.1f, %s: the plate's voxel radiance %.5f (%d voxels); the panel's bounce %.5f "
                        "(GI-off control %.5f)\n", kMetal[m], arms[a].second, store[m][a], n, bounce[m][a], dark);
        }
        if (m == 0) {
            // THE LIVE EDITS (the voxel inputs of a PBR edit, OgreMaterials.cpp): a ROUGHNESS-only edit
            // re-voxelises the cascade exactly once (its diffuse albedo is the voxel's); a METALNESS
            // edit too, and the store then holds (1 - m) of what it held.
            const auto rebuilds = [&]() {
                const GiStatus st = s->giStatus();
                return st.cascades.empty() ? 0ull : st.cascades[0].rebuilds;
            };
            const auto settle = [&]() {
                enginetest::frames(e, 20);
                for (int f = 0; f < 4000 && !s->giStatus().giAtRest; ++f) enginetest::frames(e, 1);
            };
            settle();
            const unsigned long long r0 = rebuilds();
            int n = 0;
            const double before = plateRadiance(s, n);
            PbrParams edit = plate;
            edit.roughness = 0.3f;
            CHECK(s->setPbrMaterial(plateMat, edit), "the roughness edit applies");
            s->refreshGlobalIllumination();   // the host's settle (the mirror's plan does it in the app)
            settle();
            const unsigned long long r1 = rebuilds();
            char msg[200];
            std::snprintf(msg, sizeof msg, "a roughness-only edit re-voxelises the cascade exactly once (%llu)", r1 - r0);
            CHECK(r1 - r0 == 1ull, msg);
            const double mid = plateRadiance(s, n);
            edit.metalness = 0.5f;
            CHECK(s->setPbrMaterial(plateMat, edit), "the metalness edit applies");
            s->refreshGlobalIllumination();
            settle();
            const unsigned long long r2 = rebuilds();
            std::snprintf(msg, sizeof msg, "a metalness edit re-voxelises the cascade exactly once (%llu)", r2 - r1);
            CHECK(r2 - r1 == 1ull, msg);
            const double after = plateRadiance(s, n);
            std::printf("live edits: rebuilds %llu -> %llu -> %llu; the plate's radiance %.5f -> %.5f (roughness 0.3) "
                        "-> %.5f (metalness 0.5)\n", r0, r1, r2, before, mid, after);
            std::snprintf(msg, sizeof msg, "...and the store then holds 1 - m of what it held (%.4f; want 0.50, bar 1 %%)",
                          after / mid);
            CHECK(std::fabs(after / mid - 0.5) <= 0.01, msg);
        }
        CHECK(std::fabs(dark) < 1e-3, "with no GI the panel's face is dark (no sun on it, no sky)");
        s->setGlobalIllumination(off);
        rv->setScene(nullptr);
        e->destroyScene(s);
    }
    e->destroyView(rv);
    for (int a = 0; a < 2; ++a) {
        CHECK(store[0][a] > 0.0 && bounce[0][a] > 1e-3, "the diffuser stores light and lights the panel (the reference)");
        for (int m = 1; m < 3; ++m) {
            const double want = 1.0 - kMetal[m];
            char msg[240];
            std::snprintf(msg, sizeof msg, "%s, metalness %.1f: the store holds 1 - m of the diffuser's radiance (%.4f; "
                          "want %.2f, bar 1 %%)", arms[a].second, kMetal[m], store[m][a] / store[0][a], want);
            CHECK(std::fabs(store[m][a] / store[0][a] - want) <= 0.01, msg);
            // the bar: 3 % (gi.env_cone's) of the diffuser's bounce, and for a PURE metal 0.5 % - nothing of
            // a metal's diffuse lobe is left to re-emit, so a leak of a few percent must not pass
            const double bar = kMetal[m] >= 1.0f ? 0.005 : 0.03;
            std::snprintf(msg, sizeof msg, "%s, metalness %.1f: the panel takes 1 - m of the diffuser's bounce (%.4f; "
                          "want %.2f, bar %.3f)", arms[a].second, kMetal[m], bounce[m][a] / bounce[0][a], want, bar);
            CHECK(std::fabs(bounce[m][a] / bounce[0][a] - want) <= bar, msg);
        }
    }
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
