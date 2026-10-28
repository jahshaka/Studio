// gi.lamps_outside (PHOTON-7 10-4, item R9; L9: A LAMP IS BLOCKED BY GEOMETRY ON EVERY PATH) — SEVENTEEN LAMPS OUTSIDE A
// SEALED BOX LIGHT NOTHING INSIDE IT. A closed box (4 x 3 x 4 m inside, 0.4 m walls, albedo 0.6) in a black world (no
// sky, no ambient, GI off: the pixel's direct light alone), seventeen point lamps on a ring 5 m out around it - sixteen
// casting and ONE with castShadows off (the lead's ruling: every Forward+ lamp is traced, the engine knows no "no
// shadow") - and the shadow-map budget at its floor (2): two lamps hold maps, FIFTEEN are Forward+ lamps.
//
// RED ON THE BASE (the lamp rays off, measured): the sealed box's inside peaked at 0.36, its mean 0.21.
// THE DEFECT IT GUARDS (R9): a Forward+ lamp - every lamp past the shadow-map budget - was shaded with no visibility at
// all (200.ForwardPlus_piece_ps.any's loops), so its light reached the inside of a sealed box through the walls. Now
// each such lamp's term is multiplied by ONE ray from the pixel to it (the fork's hook custom_ps_fwdplus_lamp,
// JahLampRay_piece_ps.any): the ray tier's TLAS, by its device address.
//
// THE MIRROR PILLAR (R9 (2)): a metal pillar inside, its reflection the inside walls - ray hits, decoded with the WORLD
// lamp list, each lamp traced from the hit (the hit's Forward+ cell deleted): the pillar's pixels are in the law below.
// THE NO-RAY ARM (argv[1] "noray", R9 at the no-ray tier): no TLAS, the voxel chain bound (the GI at High, its direct
// half alone) - each Forward+ lamp seen through the chain's own shadow march (jah_inject_visibility.glsl, the voxels' own
// lamp law), the same law (0) / (1) below.
// THE LAW (argv[1]: "ray" the ray tier at High), from the HDR readback (linear radiance), the inside view at rest:
//   (0) THE CONTROL - the box's +z wall hidden (an opening), the same lamps light the floor and the back wall inside:
//       the brightest channel's mean over the picture's middle half is R, over a code;
//   (1) THE SEALED BOX - every pixel of the picture's middle half reads 0 (nothing lights the inside: the exact answer)
//       within one code at R's level (the display's sRGB code width there: the picture's quantum).
// A fixture (the engine alone, one small offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using namespace jahshaka::engine;

namespace {
int g_fail = 0;
void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    if (!ok) ++g_fail;
}
NodeId box(Scene *s, const Vec3 &at, const Vec3 &size, MaterialId mat)
{
    const NodeId node = s->createNode();
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    const MeshId mesh = s->createMesh(d);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    enginetest::setNodePosition(s, node, at);
    enginetest::setNodeScale(s, node, size);
    return node;
}
// ONE CODE AT A LEVEL: the width of the display's 8-bit sRGB code linear radiance L falls in
double srgbEncode(double L) { return L <= 0.0031308 ? 12.92 * L : 1.055 * std::pow(L, 1.0 / 2.4) - 0.055; }
double srgbDecode(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }
double oneCodeAt(double L)
{
    const double code = std::min(std::floor(srgbEncode(std::max(L, 0.0)) * 255.0), 254.0);
    return srgbDecode((code + 1.0) / 255.0) - srgbDecode(code / 255.0);
}
}   // namespace

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "ray";
    if (mode != "ray" && mode != "cards" && mode != "noray") {
        std::printf("FAIL: the mode is ray, cards or noray, not '%s'\n", mode.c_str());
        return 2;
    }
    const bool cardsMode = mode == "cards";
    const bool noRay = mode == "noray";
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-lamps-outside-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    e->setShadowMapBudget(2u);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("lamps_outside", 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *scene = e->createScene("lamps_outside");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    view->setShadows(true);
    if (noRay) scene->setRayTracing(RayTracingMode::Off);   // the no-ray tier as it ships
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.hdrReadback = true;
        fx.ssr = 1;   // the reflections: the mirror pillar's hits are decoded (their lamps traced from the hit)
        view->setPostFx(fx);
    }
    PbrParams matte;
    matte.albedo = Colour(0.6f, 0.6f, 0.6f);
    matte.roughness = 0.9f;
    const MaterialId floorMat = scene->createPbrMaterial(matte);
    // THE NO-RAY ARM'S SHELL REFLECTS NOTHING OUTSIDE: the no-ray GI's own diffuse and specular let the shell's lit OUTER
    // faces through into the sealed box (measured with the lamps' voxel term forced to 0, so none of it is theirs: mean
    // 0.00083 / peak 0.0037 with the walls grey, mean 0.00038 / peak 0.0066 on the mirror pillar with them black and the
    // floor's edges outside - a GI leak, on the review list, not this law's subject). So in this arm the walls and the
    // roof are black and the grey floor lies wholly inside them (the walls down to its underside): nothing outside
    // reflects, and the inside reads the lamps' own visibility - the floor what a leaking lamp would light.
    PbrParams black = matte;
    black.albedo = Colour(0.0f, 0.0f, 0.0f);
    const MaterialId wall = noRay ? scene->createPbrMaterial(black) : floorMat;
    const float floorW = noRay ? 4.0f : 4.8f, wallH = noRay ? 3.8f : 3.0f, wallY = noRay ? 1.3f : 1.5f;
    // the box: inside x, z in [-2, 2], y in [0, 3]; 0.4 m walls
    box(scene, Vec3(0.0f, -0.2f, 0.0f), Vec3(floorW, 0.4f, floorW), floorMat);           // floor
    box(scene, Vec3(0.0f, 3.2f, 0.0f), Vec3(4.8f, 0.4f, 4.8f), wall);                   // roof
    box(scene, Vec3(-2.2f, wallY, 0.0f), Vec3(0.4f, wallH, 4.0f), wall);                // -x
    box(scene, Vec3(2.2f, wallY, 0.0f), Vec3(0.4f, wallH, 4.0f), wall);                 // +x
    box(scene, Vec3(0.0f, wallY, -2.2f), Vec3(4.8f, wallH, 0.4f), wall);                // -z
    const NodeId front = box(scene, Vec3(0.0f, wallY, 2.2f), Vec3(4.8f, wallH, 0.4f), wall);   // +z (the control's opening)
    if (!front) { std::printf("FAIL: the box\n"); return 1; }
    // THE MIRROR PILLAR inside, in the picture's middle (R9 (2)): its reflection is the inside walls - ray HITS, whose
    // lamps the hit decode shades from the world list, each traced from the hit
    {
        PbrParams mirror;
        mirror.albedo = Colour(0.95f, 0.95f, 0.95f);
        mirror.metalness = 1.0f;
        mirror.roughness = 0.05f;
        // (the no-ray arm has no hits to decode, and its mirror's reflection is the no-ray specular's: with the lamps'
        // voxel term forced to 0 it read a constant 0.0066 there inside the black shell - not this law's subject, on the
        // review list beside the GI's leak above - so its pillar is matte, a surface a leaking lamp would light)
        if (noRay) {
            mirror = matte;
        }
        if (!box(scene, Vec3(0.0f, 1.0f, -0.6f), Vec3(0.5f, 2.0f, 0.5f), scene->createPbrMaterial(mirror))) {
            std::printf("FAIL: the pillar\n");
            return 1;
        }
    }
    // SEVENTEEN LAMPS on a ring 5 m out, at the room's mid height (lamp 0 with castShadows off)
    for (int k = 0; k < 17; ++k) {
        const float a = 6.2831853f * float(k) / 17.0f;
        const NodeId lamp = scene->createNode();
        scene->setNodeTransform(lamp, Vec3(5.0f * std::sin(a), 1.5f, 5.0f * std::cos(a)), Quat(), Vec3(1, 1, 1));
        LightDesc ld;
        ld.type = LightType::Point;
        ld.colour = Colour(1, 1, 1);
        ld.intensity = 3.0f;
        ld.range = 30.0f;
        ld.castShadows = k != 0;   // lamp 0 casts no shadow map: a Forward+ lamp, traced all the same
        if (cardsMode) ld.colour = Colour(1, 0, 0);   // (the cards arm: the outside lamps RED)
        if (!scene->setLight(lamp, ld)) { std::printf("FAIL: lamp %d\n", k); return 1; }
    }
    // THE CARDS ARM (R9 (3)): twelve dim GREEN lamps INSIDE besides - twenty-nine reach the floor's cards, past the old
    // 16-lamp table, whose overflow (the smallest shares: the dim green lamps and a red one) shared ONE visibility, the
    // green's - so the red one leaked. Every lamp keeps its own traced bit now (the 48-lamp table): no red may reach the
    // floor's card texels.
    if (cardsMode)
        for (int k = 0; k < 12; ++k) {
            const float a = 6.2831853f * float(k) / 12.0f;
            const NodeId lamp = scene->createNode();
            scene->setNodeTransform(lamp, Vec3(1.2f * std::sin(a), 2.4f, 1.2f * std::cos(a)), Quat(), Vec3(1, 1, 1));
            LightDesc ld;
            ld.type = LightType::Point;
            ld.colour = Colour(0, 1, 0);
            ld.intensity = 0.25f;
            ld.range = 30.0f;
            ld.castShadows = true;
            if (!scene->setLight(lamp, ld)) { std::printf("FAIL: inside lamp %d\n", k); return 1; }
        }
    float sh[27] = { 0 };   // no sky, no ambient
    scene->setAmbientSh(sh);
    // the eye inside, near the +z wall, looking at the floor and the back wall
    const Vec3 eye(0.0f, 1.8f, 1.6f), at(0.0f, 0.6f, -1.5f);
    enginetest::testCameraLookAt(view, eye, at);
    e->waitForAsyncShaders();

    struct Pic { bool ok = false; double mean = 0.0, peak = 0.0; size_t n = 0; unsigned px = 0, py = 0; };
    const auto read = [&]() {
        Pic p;
        ImageF img;
        if (!view->readPixelsHdr(img) || img.width < 64u || img.height < 64u) return p;
        double sum = 0.0;
        for (unsigned y = img.height / 4u; y < 3u * img.height / 4u; ++y)
            for (unsigned x = img.width / 4u; x < 3u * img.width / 4u; ++x) {
                const Colour c = img.at(x, y);
                const double v = std::max(c.r, std::max(c.g, c.b));
                sum += v;
                if (v > p.peak) { p.peak = v; p.px = x; p.py = y; }
                ++p.n;
            }
        p.ok = p.n > 0;
        p.mean = p.ok ? sum / double(p.n) : 0.0;
        return p;
    };
    const auto settle = [&]() { e->waitForAsyncShaders(); run(8); };
    // THE NO-RAY ARM: the voxel chain bound (the GI on, its direct half alone: no bounce, no field, no cards), no TLAS -
    // each Forward+ lamp seen through the chain's own shadow march (PhotonVct_piece_ps.any's jahLampVoxelView); the
    // chain at rest (voxelised, its light injected) before every reading
    const auto chainAtRest = [&]() {
        int quiet = 0, f = 0;
        for (; f < 600 && quiet < 30; ++f) {
            run(1);
            const GiStatus g = scene->published().gi;
            quiet = g.chainOwed || g.feedbackOwed ? 0 : quiet + 1;
        }
        return quiet >= 30;
    };
    if (noRay) {
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.ddgi = GiToggle::Off;
        gi.cards = false;
        gi.bounces = false;
        if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
    }

    if (cardsMode) {
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.ddgi = GiToggle::Off;
        gi.cards = true;
        gi.bounces = false;   // the direct half alone: what the lamp table's bits gate
        gi.updateBudget = 0;
        if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
        unsigned long long relights = ~0ull, admitted = ~0ull;
        int quiet = 0, f = 0;
        for (; f < 900 && quiet < 30; ++f) {
            run(1);
            const GiStatus g = scene->published().gi;
            const bool still = !g.chainOwed && g.cards.built && g.cards.relights == relights &&
                               g.cards.admitted == admitted && g.cards.queueLength == 0u;
            relights = g.cards.relights;
            admitted = g.cards.admitted;
            quiet = still ? quiet + 1 : 0;
        }
        double red = 0.0, green = 0.0;
        size_t n = 0, tabled = 0, maxCount = 0;
        for (int i = -3; i <= 3; ++i)
            for (int j = -3; j <= 3; ++j) {
                CardSample c;
                if (!scene->readCardAt(Vec3(0.45f * float(i), 0.0f, 0.45f * float(j)), Vec3(0, 1, 0), c) || !c.ok || !c.lit)
                    continue;
                red += c.radiance[0] - c.indirect[0] - c.flat[0];
                green += c.radiance[1] - c.indirect[1] - c.flat[1];
                tabled += c.lampCount;
                maxCount = std::max<size_t>(maxCount, c.lampCount);
                ++n;
            }
        const double mr = n ? red / double(n) : -1.0, mg = n ? green / double(n) : 0.0;
        std::printf("== cards: at rest after %d frames; %zu floor texels, %zu lamps a table at most; the direct half: red %.6f, "
                    "green %.6f (one code at the green %.6f)\n", quiet >= 30 ? f - quiet : -1, n, maxCount, mr, mg,
                    oneCodeAt(mg));
        check(n > 20 && mg > 0.01, "the cards arm: the inside lamps light the floor's cards");
        check(maxCount > 16, "the cards arm: more than sixteen lamps reach a card (past the old table)");
        char w[256];
        std::snprintf(w, sizeof(w), "(2) THE CARDS: no red outside lamp reaches the floor's card texels (red direct %.6f "
                      "within one code at the green's level %.6f)", mr, oneCodeAt(mg));
        check(n > 0 && std::fabs(mr) <= oneCodeAt(mg), w);
        std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
        return g_fail ? 1 : 0;
    }

    // (0) THE CONTROL: the +z wall hidden
    scene->setNodeVisible(front, false);
    settle();
    const bool restOpen = !noRay || chainAtRest();
    const Pic open = read();
    scene->setNodeVisible(front, true);
    settle();
    const bool restSealed = !noRay || chainAtRest();
    const Pic sealed = read();
    check(restOpen && restSealed, "the voxel chain at rest before each reading");
    const double code = oneCodeAt(open.mean);
    const ShadowStatus st = e->published().shadow;
    std::printf("== %s: %zu focused maps; the opening: mean %.6f (peak %.6f); sealed: mean %.6f, peak %.6f at pixel (%u, "
                "%u); one code at R %.6f\n", mode.c_str(), st.mapped.size() ? st.mapped.size() - 1u : 0u, open.mean,
                open.peak, sealed.mean, sealed.peak, sealed.px, sealed.py, code);
    check(open.ok && open.mean > 0.01, "(0) the control: through the opening the lamps light the inside");
    char what[256];
    std::snprintf(what, sizeof(what), "(1) the sealed box: every pixel inside reads 0 (peak %.6f) within one code at R (%.6f)",
                  sealed.peak, code);
    check(sealed.ok && sealed.peak <= code, what);
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
