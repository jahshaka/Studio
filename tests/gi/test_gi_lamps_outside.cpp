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
    if (mode != "ray") { std::printf("FAIL: the mode is ray, not '%s'\n", mode.c_str()); return 2; }
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
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.hdrReadback = true;
        view->setPostFx(fx);
    }
    PbrParams matte;
    matte.albedo = Colour(0.6f, 0.6f, 0.6f);
    matte.roughness = 0.9f;
    const MaterialId wall = scene->createPbrMaterial(matte);
    // the box: inside x, z in [-2, 2], y in [0, 3]; 0.4 m walls
    box(scene, Vec3(0.0f, -0.2f, 0.0f), Vec3(4.8f, 0.4f, 4.8f), wall);                  // floor
    box(scene, Vec3(0.0f, 3.2f, 0.0f), Vec3(4.8f, 0.4f, 4.8f), wall);                   // roof
    box(scene, Vec3(-2.2f, 1.5f, 0.0f), Vec3(0.4f, 3.0f, 4.0f), wall);                  // -x
    box(scene, Vec3(2.2f, 1.5f, 0.0f), Vec3(0.4f, 3.0f, 4.0f), wall);                   // +x
    box(scene, Vec3(0.0f, 1.5f, -2.2f), Vec3(4.8f, 3.0f, 0.4f), wall);                  // -z
    const NodeId front = box(scene, Vec3(0.0f, 1.5f, 2.2f), Vec3(4.8f, 3.0f, 0.4f), wall);   // +z (the control's opening)
    if (!front) { std::printf("FAIL: the box\n"); return 1; }
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
        if (!scene->setLight(lamp, ld)) { std::printf("FAIL: lamp %d\n", k); return 1; }
    }
    float sh[27] = { 0 };   // no sky, no ambient
    scene->setAmbientSh(sh);
    // the eye inside, near the +z wall, looking at the floor and the back wall
    const Vec3 eye(0.0f, 1.8f, 1.6f), at(0.0f, 0.6f, -1.5f);
    enginetest::testCameraLookAt(view, eye, at);
    e->waitForAsyncShaders();

    struct Pic { bool ok = false; double mean = 0.0, peak = 0.0; size_t n = 0; };
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
                p.peak = std::max(p.peak, v);
                ++p.n;
            }
        p.ok = p.n > 0;
        p.mean = p.ok ? sum / double(p.n) : 0.0;
        return p;
    };
    const auto settle = [&]() { e->waitForAsyncShaders(); run(8); };

    // (0) THE CONTROL: the +z wall hidden
    scene->setNodeVisible(front, false);
    settle();
    const Pic open = read();
    scene->setNodeVisible(front, true);
    settle();
    const Pic sealed = read();
    const double code = oneCodeAt(open.mean);
    const ShadowStatus st = e->published().shadow;
    std::printf("== %s: %zu focused maps; the opening: mean %.6f (peak %.6f); sealed: mean %.6f, peak %.6f; one code at R "
                "%.6f\n", mode.c_str(), st.mapped.size() ? st.mapped.size() - 1u : 0u, open.mean, open.peak, sealed.mean,
                sealed.peak, code);
    check(open.ok && open.mean > 0.01, "(0) the control: through the opening the lamps light the inside");
    char what[256];
    std::snprintf(what, sizeof(what), "(1) the sealed box: every pixel inside reads 0 (peak %.6f) within one code at R (%.6f)",
                  sealed.peak, code);
    check(sealed.ok && sealed.peak <= code, what);
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
