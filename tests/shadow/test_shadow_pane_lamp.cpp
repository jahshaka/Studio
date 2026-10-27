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
        paneP.alphaMode = PbrAlphaMode::Opaque;
    }
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
