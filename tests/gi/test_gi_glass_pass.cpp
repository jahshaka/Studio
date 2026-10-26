// gi.glass_pass (PHOTON-7 10-4, item R6; L9: THE RENDERER FOLLOWS THE RAY) — A PANE IS SHADED BY THE LIGHT THAT REACHES
// IT, NEVER BY THE SURFACE BEHIND IT, AND IT OCCLUDES NOTHING. A niche (3 x 3 m, 3 m deep, open towards +z, albedo 0.6) lit
// by a lamp inside it, in a black world (no sky, no light outside); a 1 x 1 m pane (a 2 cm slab, albedo 0.3) stands 2.5 m in
// front of its mouth, facing the camera outside. The pane's FRONT sees only the dark outside and the lamp is behind it, so
// the light the pane itself sends to the camera is zero in every blending mode, and the picture through it is the hole's
// picture passed by the mode's blend (Ogre's: Glass and Blend pass 1 - alpha, Additive passes 1 and adds its own light,
// Modulate multiplies by its own lit colour - zero here).
//
// THE TWO DEFECTS IT GUARDS (PHOTON-7 10-4):
//   (a) R6 (SPECS/audits/PHYSICS_AUDIT_2026-10-26.md A0): Glass, Additive and Modulate were filed in the OPAQUE queue
//       (OgreMaterials.cpp renderQueueFor), so on a screen-GI chain they drew in the opaque pass, whose pass properties hand
//       every fragment the screen products at its pixel (JahProbeGather_piece_ps.any: the gathered diffuse and the rough
//       specular) - the products of the OPAQUE surface behind the pane. Measured on the base: Additive +86.6 codes (ray) /
//       +106.3 (no rays) over the hole's pass, Modulate +210.7 / +266.4.
//   (b) Blend and Glass WROTE DEPTH (Ogre's Fade/Transparent keep the macroblock's depth write), and the Atom stock depth
//       pass (a depth-only pass, OgreChain.cpp) put the pane's depth in the scene's: every screen product behind the pane
//       took it for the surface, and the wall seen through it lost its whole indirect light - measured on the base, the
//       wall through an alpha-0 pane read 0.758 against the hole's 1.237: its direct light alone, on both tiers.
//
// THE LAW, per tier (argv[1]: "ray" the ray tier at High, "noray" the no-ray tier as it ships), from the HDR readback (the
// linear scene radiance, before any grade), over the pane's pixels (its projected rectangle, 3 px in): (0) the hole (the pane
// hidden) is the reference H; then for each mode the pane's MEAN equals the mode's pass of the hole's mean - Glass and Blend
// (alpha 0.5) 0.5 mean(H), Additive (alpha 0.5) mean(H), Modulate 0 - within ONE CODE (1/255 of mean(H), the readback's
// quantum at the picture's level; the hole's mean has no noise of its own at rest: its two reads one frame apart are
// printed). Each pixel's excess over its bound is printed beside it: what is left there is the caches' pane (R7: the
// caches still hold a pane as an opaque matte wall, so the niche's escaping light comes back from it - held under the bar
// by the pane's distance and albedo; measured at 2.5 m / 0.3: under a code in the mean, a few codes at a pixel of the no-ray
// tier's per-pixel rays).
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
}   // namespace

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "ray";
    if (mode != "ray" && mode != "noray") { std::printf("FAIL: the mode is ray or noray, not '%s'\n", mode.c_str()); return 2; }
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-glass-pass-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("glass_pass", 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *scene = e->createScene("glass_pass");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        fx.hdrReadback = true;
        view->setPostFx(fx);
    }
    if (mode == "noray") scene->setRayTracing(RayTracingMode::Off);
    PbrParams matte;
    matte.albedo = Colour(0.6f, 0.6f, 0.6f);
    matte.roughness = 0.9f;
    const MaterialId wallMat = scene->createPbrMaterial(matte);
    // the niche: open towards +z, its mouth at z = 0
    box(scene, Vec3(0.0f, -0.2f, -1.5f), Vec3(3.8f, 0.4f, 3.0f), wallMat);    // floor
    box(scene, Vec3(0.0f, 3.2f, -1.5f), Vec3(3.8f, 0.4f, 3.0f), wallMat);     // ceiling
    box(scene, Vec3(-1.7f, 1.5f, -1.5f), Vec3(0.4f, 3.0f, 3.0f), wallMat);    // -x wall
    box(scene, Vec3(1.7f, 1.5f, -1.5f), Vec3(0.4f, 3.0f, 3.0f), wallMat);     // +x wall
    box(scene, Vec3(0.0f, 1.5f, -3.2f), Vec3(3.8f, 3.8f, 0.4f), wallMat);     // the back wall
    {
        const NodeId lamp = scene->createNode();
        LightDesc ld;
        ld.type = LightType::Point;
        ld.colour = Colour(1, 1, 1);
        ld.intensity = 8.0f;
        ld.range = 12.0f;
        ld.castShadows = true;
        scene->setNodeTransform(lamp, Vec3(0.0f, 2.2f, -1.2f), Quat(), Vec3(1, 1, 1));
        scene->setLight(lamp, ld);
    }
    // the pane, 0.6 m in front of the mouth; its material is the arm's
    PbrParams paneP;
    paneP.albedo = Colour(0.3f, 0.3f, 0.3f);
    paneP.roughness = 0.5f;
    const MaterialId paneMat = scene->createPbrMaterial(paneP);
    const float paneZ = 2.5f, paneH = 0.5f;
    const NodeId pane = box(scene, Vec3(0.0f, 1.5f, paneZ), Vec3(2.0f * paneH, 2.0f * paneH, 0.02f), paneMat);
    if (!pane) { std::printf("FAIL: the pane\n"); return 1; }
    float sh[27] = { 0 };   // no sky
    scene->setAmbientSh(sh);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::On;
    gi.cards = true;
    gi.bounces = true;
    gi.updateBudget = 0;
    if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
    const Vec3 eye(0.0f, 1.5f, 7.5f), at(0.0f, 1.5f, 0.0f);
    enginetest::testCameraLookAt(view, eye, at);
    e->waitForAsyncShaders();

    // THE REST (gi.sealed_sky's): the chain owes nothing, the cache relit nothing, for 30 frames in a row
    const bool cardsOn = mode == "ray";
    const auto settle = [&]() {
        unsigned long long relights = ~0ull, admitted = ~0ull;
        int quiet = 0, f = 0;
        for (; f < 900 && quiet < 30; ++f) {
            run(1);
            const GiStatus g = scene->published().gi;
            const bool cardsStill = !cardsOn || (g.cards.built && g.cards.relights == relights &&
                                                 g.cards.admitted == admitted && g.cards.queueLength == 0u);
            const bool still = !g.chainOwed && !g.feedbackOwed && cardsStill && !g.ifdWorking;
            relights = g.cards.relights;
            admitted = g.cards.admitted;
            quiet = still ? quiet + 1 : 0;
        }
        return quiet >= 30 ? f - quiet : -1;
    };
    // the pane's rectangle in the picture (testCameraDescLookAt's basis: 45 degrees vertical, square aspect; the camera on
    // the pane's axis), 3 px in
    const auto paneRect = [&](unsigned W, unsigned H, int &x0, int &x1, int &y0, int &y1) {
        const double t = std::tan(0.5 * 45.0 * 3.14159265358979 / 180.0), d = eye.z - (paneZ + 0.01);
        const double half = 0.5 * paneH / (d * t);   // the half-size as a fraction of the picture
        x0 = int(std::ceil((0.5 - half) * W)) + 3;
        x1 = int(std::floor((0.5 + half) * W)) - 3;
        y0 = int(std::ceil((0.5 - half) * H)) + 3;
        y1 = int(std::floor((0.5 + half) * H)) - 3;
    };
    struct Pic { bool ok = false; unsigned w = 0, h = 0; std::vector<double> v; double mean = 0.0; };
    const auto read = [&]() {
        Pic p;
        ImageF img;
        if (!view->readPixelsHdr(img) || img.width < 64u || img.height < 64u) return p;
        int x0, x1, y0, y1;
        paneRect(img.width, img.height, x0, x1, y0, y1);
        double sum = 0.0;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const Colour c = img.at(unsigned(x), unsigned(y));
                p.v.push_back(std::max(c.r, std::max(c.g, c.b)));
                sum += p.v.back();
            }
        p.ok = !p.v.empty();
        p.mean = p.ok ? sum / double(p.v.size()) : 0.0;
        return p;
    };

    // (0) THE HOLE
    scene->setNodeVisible(pane, false);
    const int restHole = settle();
    const Pic hole = read();
    // the hole's own noise: two reads one frame apart at rest (the gather's rest mean is fixed: they agree to the bit
    // where nothing moves) - and its pixel spread, printed
    run(1);
    const Pic hole2 = read();
    double noise = 0.0, lo = 1e30, hi = 0.0;
    for (size_t i = 0; hole.ok && hole2.ok && i < hole.v.size() && i < hole2.v.size(); ++i) {
        noise = std::max(noise, std::fabs(hole.v[i] - hole2.v[i]));
        lo = std::min(lo, hole.v[i]);
        hi = std::max(hi, hole.v[i]);
    }
    const double code = hole.mean / 255.0;
    std::printf("== %s\n   (0) the hole: at rest after %d frames, %zu pixels, mean %.6f (min %.6f, max %.6f); one frame later "
                "the mean moved %.6f, a pixel at most %.6f; one code %.6f\n", mode.c_str(), restHole, hole.v.size(), hole.mean,
                lo, hi, std::fabs(hole2.mean - hole.mean), noise, code);
    check(restHole >= 0 && hole.ok && hole.mean > 0.02, "(0) the hole: the niche's back wall is lit through the mouth");
    scene->setNodeVisible(pane, true);

    struct Arm { PbrAlphaMode m; float alpha; const char *name; };
    const Arm arms[4] = { { PbrAlphaMode::Blend, 0.5f, "BLEND (the control)" },
                          { PbrAlphaMode::Glass, 0.5f, "GLASS" },
                          { PbrAlphaMode::Additive, 0.5f, "ADDITIVE" },
                          { PbrAlphaMode::Modulate, 1.0f, "MODULATE" } };
    for (const Arm &a : arms) {
        PbrParams p = paneP;
        p.alphaMode = a.m;
        p.alpha = a.alpha;
        if (!scene->setPbrMaterial(paneMat, p)) { check(false, "the arm's material"); continue; }
        e->waitForAsyncShaders();
        const int rest = settle();
        const Pic pic = read();
        // the bound per pixel: the blend's pass of the hole (Glass/Blend 1 - alpha, Additive 1, Modulate 0)
        const double pass = a.m == PbrAlphaMode::Additive ? 1.0 : a.m == PbrAlphaMode::Modulate ? 0.0 : 1.0 - a.alpha;
        double worst = -1e30;
        size_t over = 0u;
        for (size_t i = 0; pic.ok && i < pic.v.size() && i < hole.v.size(); ++i) {
            const double excess = pic.v[i] - pass * hole.v[i];
            worst = std::max(worst, std::fabs(excess));
            if (std::fabs(excess) > code) ++over;
        }
        const double meanExcess = pic.mean - pass * hole.mean;
        std::printf("   %s: at rest after %d frames; the pane's mean %.6f against the hole's pass %.6f (%+.6f = %+.2f "
                    "codes); per pixel the largest |excess| %.6f (%.2f codes), %zu of %zu pixels over a code\n", a.name,
                    rest, pic.mean, pass * hole.mean, meanExcess, code > 0.0 ? meanExcess / code : -1.0, worst,
                    code > 0.0 ? worst / code : -1.0, over, pic.v.size());
        char what[256];
        std::snprintf(what, sizeof(what), "%s: the pane passes the hole's picture by its blend and sends no light of its "
                      "own (its mean within one code of the hole's pass)", a.name);
        check(rest >= 0 && pic.ok && pic.v.size() == hole.v.size() && std::fabs(meanExcess) <= code, what);
    }
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
