// shadow.pane_lamp (PHOTON-7 10-4, item R8 (2); L9: A TRANSPARENT SURFACE TRANSMITS A LIGHT'S RAY, ATTENUATED BY ITS
// MATERIAL) — A LAMP'S SHADOW THROUGH A PANE IS THE PANE'S TRANSMITTANCE. A floor slab (6 x 6 m, albedo 0.5) in a black
// world (no sky, no ambient, no GI, no rays: the direct light and the shadow maps alone), a lamp 2.5 m above it and a
// horizontal pane (1.2 x 1.2 m, 2 cm, albedo 0.3) 1 m above the floor between them; the eye low at the side, under the
// pane's height, so it sees the floor beneath the pane without looking through it.
//
// THE PATHS IT GUARDS (R8 (2), the shadow transmittance layer, OgreShadow.cpp): a POINT lamp - the cube faces' pane
// passes and their copy into the atlas layer; a SPOT - the focused map's pane pass. (The sun's PSSM pane passes are
// gi.photon_window's sun patch.) Before R8 (2) a pane cast the shadow of an opaque wall (every caster pass drew it
// solid), so the floor under it read 0 - deduced from that code, not run on the base.
//
// THE LAW, per lamp, from the HDR readback (linear radiance, before any grade), over a square of the floor under the
// pane's middle (its shadow's inner half), the brightest channel's mean: (0) the hole (the pane hidden) H; then with the
// pane GLASS at alpha 0.5 the floor reads T x H, T = 1 - alpha = 0.5 (Ogre's blend: what the pixel of the pane passes),
// within one code at T x H (the display's sRGB code width there: the picture's quantum) plus T's own quantum in the
// layer (8 bits: 1/510 of H); and with the pane OPAQUE it reads 0 within the same code (the layer never lights a
// solid caster's shadow).
// THE FIX ROUND's ARMS (the one read's F2 and F8): Blend, Glass and Refractive at alpha 1 on the STOCK caster's path (the
// pane culled at the front) read 0 like the opaque pane (red before F2: the hole, 0.107738 - the copy wrote no depth);
// ADDITIVE at alpha 0.5 reads the hole within a code (red before F8: 0.000001 - an additive surface absorbs nothing).
// THE ALPHA DRAG (F12): Glass through 0.3, 0.4, 0.6 and 0.7 builds no shader (the process's compiled and cached
// counters stand still: ONE pane caster for every alpha) and each alpha reads (1 - alpha) x the hole within a code (red
// before F12: every new alpha level was a caster permutation of its own, built while its shadow was missing).
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
    const MeshId mesh = s->createMesh(d);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    enginetest::setNodePosition(s, node, at);
    enginetest::setNodeScale(s, node, size);
    return node;
}
// where a world point lands in a picture from `eye` looking at `at` (testCameraDescLookAt's basis: 45 degrees vertical,
// the view's square aspect; top-left origin)
bool toPixel(const Vec3 &eye, const Vec3 &at, const Vec3 &w, unsigned W, unsigned H, int &px, int &py)
{
    double f[3] = { at.x - eye.x, at.y - eye.y, at.z - eye.z };
    const double fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (double &c : f) c /= fl;
    double r[3] = { -f[2], 0.0, f[0] };   // f x (0, 1, 0)
    const double rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
    r[0] /= rl; r[2] /= rl;
    const double u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
    const double d[3] = { w.x - eye.x, w.y - eye.y, w.z - eye.z };
    const double xc = d[0] * r[0] + d[1] * r[1] + d[2] * r[2], yc = d[0] * u[0] + d[1] * u[1] + d[2] * u[2],
                 zc = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
    if (!(zc > 0.0)) return false;
    const double t = std::tan(0.5 * 45.0 * 3.14159265358979 / 180.0), aspect = double(W) / double(H);
    px = int(std::floor((0.5 + 0.5 * xc / (zc * t * aspect)) * W));
    py = int(std::floor((0.5 - 0.5 * yc / (zc * t)) * H));
    return px >= 0 && py >= 0 && px < int(W) && py < int(H);
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

int main()
{
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-shadow-pane-lamp-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("pane_lamp", 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *scene = e->createScene("pane_lamp");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    view->setShadows(true);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.hdrReadback = true;
        view->setPostFx(fx);
    }
    scene->setRayTracing(RayTracingMode::Off);
    PbrParams matte;
    matte.albedo = Colour(0.5f, 0.5f, 0.5f);
    matte.roughness = 1.0f;
    const MaterialId floorMat = scene->createPbrMaterial(matte);
    PbrParams paneP;
    paneP.albedo = Colour(0.3f, 0.3f, 0.3f);
    paneP.roughness = 0.5f;
    const MaterialId paneMat = scene->createPbrMaterial(paneP);
    const NodeId pane = box(scene, Vec3(0.0f, 1.0f, 0.0f), Vec3(1.2f, 0.02f, 1.2f), paneMat);
    if (!box(scene, Vec3(0.0f, -0.1f, 0.0f), Vec3(6.0f, 0.2f, 6.0f), floorMat) || !pane) {
        std::printf("FAIL: the floor and the pane\n");
        return 1;
    }
    // THE NEAREST PANE's ARM (the one read's F1, below): a second pane above the first (hidden until then)
    const NodeId upper = box(scene, Vec3(0.0f, 1.4f, 0.0f), Vec3(1.2f, 0.02f, 1.2f), paneMat);
    if (!upper) { std::printf("FAIL: the upper pane\n"); return 1; }
    scene->setNodeVisible(upper, false);
    float sh[27] = { 0 };   // no sky, no ambient
    scene->setAmbientSh(sh);
    const NodeId lamp = scene->createNode();
    scene->setNodeTransform(lamp, Vec3(0.0f, 2.5f, 0.0f), Quat(), Vec3(1, 1, 1));   // the node's -Y: straight down
    const Vec3 eye(3.2f, 0.5f, 3.2f), at(0.0f, 0.0f, 0.0f);
    enginetest::testCameraLookAt(view, eye, at);

    // the pane's shadow on the floor is its square magnified 2.5 / 1.5 (half-size 1.0 m); the square read is its inner
    // half less a margin (the maps' filter)
    const auto read = [&](double &mean) {
        ImageF img;
        if (!view->readPixelsHdr(img) || img.width < 64u || img.height < 64u) return false;
        double sum = 0.0;
        int n = 0;
        for (float x = -0.4f; x <= 0.4f + 1e-4f; x += 0.05f)
            for (float z = -0.4f; z <= 0.4f + 1e-4f; z += 0.05f) {
                int px, py;
                if (!toPixel(eye, at, Vec3(x, 0.0f, z), img.width, img.height, px, py)) return false;
                const Colour c = img.at(unsigned(px), unsigned(py));
                sum += std::max(c.r, std::max(c.g, c.b));
                ++n;
            }
        mean = n ? sum / n : 0.0;
        return n > 0;
    };
    const auto settle = [&]() { e->waitForAsyncShaders(); run(4); };

    struct Lamp { LightType type; const char *name; };
    for (const Lamp &l : { Lamp{ LightType::Point, "POINT" }, Lamp{ LightType::Spot, "SPOT" } }) {
        LightDesc ld;
        ld.type = l.type;
        ld.colour = Colour(1, 1, 1);
        ld.intensity = 1.5f;
        ld.range = 12.0f;
        ld.spotAngleDegrees = 120.0f;
        ld.spotSoftness = 0.1f;
        ld.castShadows = true;
        if (!scene->setLight(lamp, ld)) { check(false, "the lamp"); continue; }
        double hole = 0.0, glass = 0.0, opaque = 0.0;
        scene->setNodeVisible(pane, false);
        settle();
        bool ok = read(hole);
        scene->setNodeVisible(pane, true);
        PbrParams p = paneP;
        p.alphaMode = PbrAlphaMode::Glass;
        p.alpha = 0.5f;
        ok = scene->setPbrMaterial(paneMat, p) && (settle(), read(glass)) && ok;
        // THE ALPHA DRAG (the one read's F12): ONE pane caster shader for every alpha - its transmittance read from its
        // material, never baked into its permutation - so a drag through four alphas builds and loads no shader (the
        // process's compiled and cached counters stand still) and each alpha's shadow is its own (1 - alpha)
        const float dragAlphas[4] = { 0.3f, 0.4f, 0.6f, 0.7f };
        double drag[4] = { 0.0, 0.0, 0.0, 0.0 };
        unsigned compiled0 = 0, cached0 = 0, expected0 = 0, compiled1 = 0, cached1 = 0, expected1 = 0;
        e->shaderBuildProgress(compiled0, cached0, expected0);
        for (int k = 0; k < 4; ++k) {
            p.alpha = dragAlphas[k];
            ok = scene->setPbrMaterial(paneMat, p) && (settle(), read(drag[k])) && ok;
        }
        e->shaderBuildProgress(compiled1, cached1, expected1);
        p.alpha = 0.5f;
        // THE NEAREST PANE (the one read's F1): the shadow node's transmittance layer carries the nearest pane's depth and a
        // receiver applies its T only past it. A receiver the opaque atlas does not hold - a PANE ITSELF - is the case:
        // the pane's own lit face reads its light whole (its pixel with the shadows on equals its pixel with the view's
        // shadows off - the engine's test door); under a second pane above it, the upper pane's T alone; and the floor
        // under both, both. The floor black for the pane's own reads (its pixel is then its own lit face alone), read from a
        // second eye under the upper pane's height. RED BEFORE F1: the pane's face read T x its light (every receiver at a
        // texel took the layer's whole product), under the upper pane T^2.
        double faceOff = 0.0, faceOn = 0.0, faceUnder = 0.0, floorTwo = 0.0;
        {
            const Vec3 eyeB(1.5f, 1.3f, 1.5f), atB(0.0f, 1.01f, 0.0f);
            const auto readFace = [&](double &mean) {
                ImageF img;
                if (!view->readPixelsHdr(img) || img.width < 64u || img.height < 64u) return false;
                double sum = 0.0;
                int n = 0;
                for (float x = -0.2f; x <= 0.2f + 1e-4f; x += 0.05f)
                    for (float z = -0.2f; z <= 0.2f + 1e-4f; z += 0.05f) {
                        int px, py;
                        if (!toPixel(eyeB, atB, Vec3(x, 1.01f, z), img.width, img.height, px, py)) return false;
                        const Colour c = img.at(unsigned(px), unsigned(py));
                        sum += std::max(c.r, std::max(c.g, c.b));
                        ++n;
                    }
                mean = n ? sum / n : 0.0;
                return n > 0;
            };
            PbrParams black = matte;
            black.albedo = Colour(0.0f, 0.0f, 0.0f);
            ok = scene->setPbrMaterial(paneMat, p) && scene->setPbrMaterial(floorMat, black) && ok;
            enginetest::testCameraLookAt(view, eyeB, atB);
            view->setShadows(false);
            ok = (settle(), readFace(faceOff)) && ok;
            view->setShadows(true);
            ok = (settle(), readFace(faceOn)) && ok;
            scene->setNodeVisible(upper, true);
            ok = (settle(), readFace(faceUnder)) && ok;
            ok = scene->setPbrMaterial(floorMat, matte) && ok;
            enginetest::testCameraLookAt(view, eye, at);
            ok = (settle(), read(floorTwo)) && ok;
            scene->setNodeVisible(upper, false);
        }
        // THE BLENDED MODES AT ALPHA 1 (the one read's F2): a surface that passes nothing of what lies behind it in the
        // pixel casts the shadow of an opaque one - on the STOCK caster's path too (the pane's faces culled at the front:
        // a caster the Atom cut cannot draw, OgreScene::atomCasterThroughCut; a skinned item takes the same path); and
        // ADDITIVE (F8) absorbs nothing at any alpha: it casts no shadow
        double atOne[3] = { 0.0, 0.0, 0.0 }, additive = 0.0;
        const PbrAlphaMode oneModes[3] = { PbrAlphaMode::Blend, PbrAlphaMode::Glass, PbrAlphaMode::Refractive };
        scene->setNodeFaceCull(pane, FaceCull::Front);
        for (int k = 0; k < 3; ++k) {
            p.alphaMode = oneModes[k];
            p.alpha = 1.0f;
            ok = scene->setPbrMaterial(paneMat, p) && (settle(), read(atOne[k])) && ok;
        }
        scene->setNodeFaceCull(pane, FaceCull::Material);
        p.alphaMode = PbrAlphaMode::Additive;
        p.alpha = 0.5f;
        ok = scene->setPbrMaterial(paneMat, p) && (settle(), read(additive)) && ok;
        p.alphaMode = PbrAlphaMode::Opaque;
        p.alpha = 1.0f;
        ok = scene->setPbrMaterial(paneMat, p) && (settle(), read(opaque)) && ok;
        const double T = 0.5, bar = oneCodeAt(T * hole) + hole / 510.0;
        std::printf("== %s: the hole %.6f; GLASS 0.5 %.6f against T x H %.6f (%+.6f; the bar %.6f); OPAQUE %.6f (the bar "
                    "%.6f)\n", l.name, hole, glass, T * hole, glass - T * hole, bar, opaque, oneCodeAt(T * hole));
        char what[256];
        std::snprintf(what, sizeof(what), "%s: the floor under the pane is lit without it (the hole %.6f)", l.name, hole);
        check(ok && hole > 0.02, what);
        std::snprintf(what, sizeof(what), "%s: through a glass pane at alpha 0.5 the floor reads (1 - alpha) x the hole "
                      "within a code", l.name);
        check(ok && std::fabs(glass - T * hole) <= bar, what);
        std::snprintf(what, sizeof(what), "%s: under an opaque pane the floor reads 0 within a code", l.name);
        check(ok && opaque <= oneCodeAt(T * hole), what);
        const unsigned built = (compiled1 - compiled0) + (cached1 - cached0);
        bool dragged = true;
        for (int k = 0; k < 4; ++k) {
            const double want = (1.0 - double(dragAlphas[k])) * hole, b = oneCodeAt(want) + hole / 510.0;
            std::printf("   %s: THE DRAG, GLASS %.1f %.6f against (1 - alpha) x H %.6f (%+.6f; the bar %.6f)\n", l.name,
                        double(dragAlphas[k]), drag[k], want, drag[k] - want, b);
            dragged = dragged && std::fabs(drag[k] - want) <= b;
        }
        std::printf("   %s: THE DRAG built %u shaders (compiled %u, from the cache %u)\n", l.name, built,
                    compiled1 - compiled0, cached1 - cached0);
        std::snprintf(what, sizeof(what), "%s: an alpha drag through four alphas builds no shader and each alpha casts its "
                      "own (1 - alpha) within a code (F12)", l.name);
        check(ok && dragged && built == 0u, what);
        {
            const double Tw = 1024.0 / 2047.0;   // (the pane word's 0.5, which the caster's 1 - alpha equals at 8 bits)
            const double bOn = oneCodeAt(faceOff) + faceOff / 510.0, bUnder = oneCodeAt(Tw * faceOff) + faceOff / 510.0;
            const double bTwo = oneCodeAt(0.25 * hole) + hole / 510.0;
            std::printf("   %s: THE NEAREST PANE - the pane's own lit face %.6f against its light whole %.6f (bar %.6f); under "
                        "a second pane %.6f against T x %.6f (bar %.6f); the floor under both %.6f against T^2 x H %.6f "
                        "(bar %.6f)\n", l.name, faceOn, faceOff, bOn, faceUnder, Tw * faceOff, bUnder, floorTwo, 0.25 * hole,
                        bTwo);
            // (a point light's panes are drawn into a cube and copied as colour: its rectangle's depth is NEAR, every
            // receiver past it - its T applies to all, a named limit: printed, not a law)
            if (l.type == LightType::Spot) {
                std::snprintf(what, sizeof(what), "%s: a pane's own lit face reads its light whole, under a second pane that "
                              "pane's T alone, the floor under both both (F1)", l.name);
                check(ok && faceOff > 0.002 && std::fabs(faceOn - faceOff) <= bOn &&
                          std::fabs(faceUnder - Tw * faceOff) <= bUnder && std::fabs(floorTwo - 0.25 * hole) <= bTwo,
                      what);
            }
        }
        const char *oneNames[3] = { "BLEND", "GLASS", "REFRACTIVE" };
        for (int k = 0; k < 3; ++k) {
            std::printf("   %s: %s at alpha 1, the stock caster %.6f (an opaque caster's 0, the bar %.6f)\n", l.name, oneNames[k], atOne[k],
                        oneCodeAt(T * hole));
            std::snprintf(what, sizeof(what), "%s: a %s surface at alpha 1 casts an opaque shadow (0 within a code)", l.name,
                          oneNames[k]);
            check(ok && atOne[k] <= oneCodeAt(T * hole), what);
        }
        std::printf("   %s: ADDITIVE at alpha 0.5 %.6f against the hole %.6f (%+.6f)\n", l.name, additive, hole,
                    additive - hole);
        std::snprintf(what, sizeof(what), "%s: an ADDITIVE surface absorbs nothing: the floor under it reads the hole "
                      "within a code", l.name);
        check(ok && std::fabs(additive - hole) <= oneCodeAt(hole) + hole / 510.0, what);
        paneP.alphaMode = PbrAlphaMode::Opaque;
    }
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
