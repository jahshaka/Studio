// gi.fog_shaft / gi.fog_sealed (PHOTON-7 10-4, item R11; L9: A MEDIUM IS LIT BY WHAT REACHES IT) — THE FOG'S IN-SCATTER
// TAKES THE SUN'S SHADOW ALONG THE EYE RAY AND THE MEASURED SKY VISIBILITY. A room (4 x 3 x 4 m inside, 0.4 m walls)
// whose every surface is BLACK, so the picture inside is the World fog's in-scatter alone (white, density 0.25 exp2 a
// metre), a 2 x 2 m opening in its roof, the sun 60 degrees up travelling towards -x, GI off. The eye inside looks across
// the room below the roof, through the beam.
//
// RED ON THE BASE (the fog's in-scatter unshadowed, the horizon's colour everywhere): a sealed room's fog glowed its full
// colour. THE DEFECT IT GUARDS (R11): jah_fog_along.glsl scattered every fog colour with no visibility, and a bright
// surface punched through by its own luminance. Now every in-scatter colour is scaled by the share of the medium's light
// that reaches it - s W_sun + (1 - s) V_sky (JahFog_piece_vs_piece_ps.any's jahFogLitAt).
//
// THE LAW (argv[1]), from the HDR readback (linear radiance), the picture's middle half:
//   "shaft" - the sun alone (no Sky Light: s = 1):
//     (0) THE OPENING: the beam lights the fog - the mean is over a code;
//     (1) THE ROOF CLOSED (a black tile over the opening): every pixel reads 0 within one code at the opening's mean
//         (the sun reaches no point of any eye ray);
//     (2) THE PANE in the opening (Glass, alpha 0.5: Ogre's blend passes 1 - alpha; the shadow node's transmittance
//         layer carries it, R8): the picture is (1 - alpha) times the opening's - its mean within one code at that level.
//   "sealed" - the sun and a Sky Light (s < 1), the roof closed: every pixel reads 0 within one code at the fog's own
//     open level (no light of either reaches the medium inside). RED until the measured sky visibility lands (jahSkyVis,
//     R11's second half: V_sky stands at 1 until then).
// A fixture (the engine alone, one small offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
using namespace jahshaka::engine;

namespace {
int g_fail = 0;
void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    if (!ok) ++g_fail;
}
NodeId slab(Scene *s, float x0, float y0, float z0, float x1, float y1, float z1, MaterialId mat)
{
    const NodeId node = s->createNode();
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    enginetest::setNodePosition(s, node, Vec3(0.5f * (x0 + x1), 0.5f * (y0 + y1), 0.5f * (z0 + z1)));
    enginetest::setNodeScale(s, node, Vec3(x1 - x0, y1 - y0, z1 - z0));
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
    const std::string mode = argc > 1 ? argv[1] : "shaft";
    if (mode != "shaft" && mode != "sealed") { std::printf("FAIL: the mode is shaft or sealed, not '%s'\n", mode.c_str()); return 2; }
    const bool sealedMode = mode == "sealed";
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-fog-inscatter-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("fog_inscatter", 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *scene = e->createScene("fog_inscatter");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    view->setShadows(true);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.hdrReadback = true;
        view->setPostFx(fx);
    }
    PbrParams black;
    black.albedo = Colour(0.0f, 0.0f, 0.0f);
    black.roughness = 1.0f;
    const MaterialId wall = scene->createPbrMaterial(black);
    // the room: inside x, z in [-2, 2], y in [0, 3]; the roof (y 3.0 .. 3.4) around the opening x [-0.6, 1.4],
    // z [-1.4, 0.6]
    slab(scene, -2.4f, -0.4f, -2.4f, 2.4f, 0.0f, 2.4f, wall);
    slab(scene, -2.4f, 0.0f, -2.0f, -2.0f, 3.0f, 2.0f, wall);
    slab(scene, 2.0f, 0.0f, -2.0f, 2.4f, 3.0f, 2.0f, wall);
    slab(scene, -2.4f, 0.0f, -2.4f, 2.4f, 3.0f, -2.0f, wall);
    slab(scene, -2.4f, 0.0f, 2.0f, 2.4f, 3.0f, 2.4f, wall);
    const float ox0 = -0.6f, ox1 = 1.4f, oz0 = -1.4f, oz1 = 0.6f;
    slab(scene, -2.4f, 3.0f, -2.4f, ox0, 3.4f, 2.4f, wall);
    slab(scene, ox1, 3.0f, -2.4f, 2.4f, 3.4f, 2.4f, wall);
    slab(scene, ox0, 3.0f, -2.4f, ox1, 3.4f, oz0, wall);
    slab(scene, ox0, 3.0f, oz1, ox1, 3.4f, 2.4f, wall);
    // the tile that closes the opening, and the pane in it (2 cm, the arm's)
    const NodeId tile = slab(scene, ox0, 3.0f, oz0, ox1, 3.4f, oz1, wall);
    PbrParams paneP;
    paneP.albedo = Colour(0.8f, 0.8f, 0.8f);
    paneP.roughness = 0.1f;
    paneP.alphaMode = PbrAlphaMode::Glass;
    paneP.alpha = 0.5f;
    const NodeId pane = slab(scene, ox0, 3.19f, oz0, ox1, 3.21f, oz1, scene->createPbrMaterial(paneP));
    if (!tile || !pane) { std::printf("FAIL: the room\n"); return 1; }
    // the sun, 60 degrees up, travelling towards -x
    enginetest::addDirectionalLight(scene, Vec3(-0.5f, -0.8660254f, 0.0f), 3.0f);
    float sh[27] = { 0 };   // no Sky Light in the shaft arms (s = 1); the sealed arm's sky below
    if (sealedMode) sh[0] = sh[1] = sh[2] = 0.3f;
    scene->setAmbientSh(sh);
    {
        SkyDesc sky;
        sky.mode = SkyMode::NoSky;
        scene->setSky(sky);
    }
    FogDesc fog;
    fog.enabled = true;
    fog.colour = Colour(1.0f, 1.0f, 1.0f);
    fog.density = 0.25f;   // exp2 a metre: half the light over 4 m
    scene->setFog(fog);
    // the eye inside, below the roof, looking across the room through the beam
    enginetest::testCameraLookAt(view, Vec3(1.6f, 1.2f, 1.8f), Vec3(-1.5f, 1.0f, -0.6f));
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

    if (sealedMode) {
        scene->setNodeVisible(pane, false);
        settle();
        const Pic sealed = read();
        // the fog's own open level: its colour (1) times what the middle half's rays scatter in over ~4 m, fully lit
        const double openLevel = 1.0 - std::exp2(-0.25 * 4.0);
        std::printf("== sealed: the sun and a Sky Light, the roof closed: mean %.6f, peak %.6f; one code at the open "
                    "level %.3f is %.6f\n", sealed.mean, sealed.peak, openLevel, oneCodeAt(openLevel));
        char w[256];
        std::snprintf(w, sizeof(w), "the sealed room's fog is dark: every pixel 0 (peak %.6f) within one code at %.3f (%.6f)",
                      sealed.peak, openLevel, oneCodeAt(openLevel));
        check(sealed.ok && sealed.peak <= oneCodeAt(openLevel), w);
        std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
        return g_fail ? 1 : 0;
    }

    // (0) THE OPENING
    scene->setNodeVisible(tile, false);
    scene->setNodeVisible(pane, false);
    settle();
    const Pic open = read();
    // (1) THE ROOF CLOSED
    scene->setNodeVisible(tile, true);
    settle();
    const Pic closed = read();
    // (2) THE PANE
    scene->setNodeVisible(tile, false);
    scene->setNodeVisible(pane, true);
    settle();
    const Pic paned = read();
    const double code = oneCodeAt(open.mean);
    const double want = 0.5 * open.mean;
    std::printf("== shaft: the opening mean %.6f (peak %.6f); closed mean %.6f, peak %.6f; the pane mean %.6f against "
                "(1 - alpha) x the opening's %.6f, one code there %.6f\n", open.mean, open.peak, closed.mean, closed.peak,
                paned.mean, want, oneCodeAt(want));
    check(open.ok && open.mean > code, "(0) the opening: the beam lights the fog");
    char w[256];
    std::snprintf(w, sizeof(w), "(1) the roof closed: every pixel 0 (peak %.6f) within one code at the opening's mean (%.6f)",
                  closed.peak, code);
    check(closed.ok && closed.peak <= code, w);
    std::snprintf(w, sizeof(w), "(2) the pane: the in-scatter is (1 - alpha) x the opening's (%.6f against %.6f, one code %.6f)",
                  paned.mean, want, oneCodeAt(want));
    check(paned.ok && std::fabs(paned.mean - want) <= oneCodeAt(want), w);
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
