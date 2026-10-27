// shadow.second_directional (PHOTON-7 10-4, item R8 (3); L9: A LIGHT IS BLOCKED BY GEOMETRY ON EVERY PATH) — A SECOND
// DIRECTIONAL LIGHT CASTS ITS SHADOW, AND THE SUN KEEPS THE CASCADES. A post (0.5 x 2 x 0.5 m, albedo 0.5) on a ground
// slab (10 x 10 m, albedo 0.5) in a black world (no sky, no ambient, no GI, no rays: the direct light and the shadow
// maps alone), under two directionals 45 degrees up: the SUN A (the primary, travelling towards -x) and B (a secondary,
// travelling towards -z), B CREATED FIRST, so the light id - creation order - would hand B the PSSM slot.
//
// THE DEFECT IT GUARDS (red on the base): a secondary directional cast nothing - the engine cleared its flag
// (OgreScene::setLight's secondaryDirectional rule) because the shadow node gave directionals one slot - so B's light
// reached the ground behind the post through it.
//
// THE LAW, from the HDR readback (linear radiance, before any grade), over three ground squares - SA in A's shadow only,
// SB in B's shadow only, L in neither - each the brightest channel's mean over the square's pixels, three arms:
// "A alone" (B's intensity 0), "B alone" (A's intensity 0), "both":
//   (1) A LIGHT'S SHADOW IS DARK: under A alone SA reads 0, under B alone SB reads 0 (nothing else lights the world) -
//       within one code at the lit square's level (the display's sRGB step there: the picture's quantum);
//   (2) THE LIGHTS ADD (the renderer is linear in its lights): "both" = "A alone" + "B alone" on every square, within one
//       code at its level - so B adds nothing in its own shadow and A nothing in its;
//   (3) THE SUN HOLDS THE CASCADES: the shadow node's slot 0 (the PSSM set) holds A and a focused slot holds B
//       (PublishedEngineFrame::shadow.mapped).
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
// ONE CODE AT A LEVEL: the display's 8-bit sRGB step at linear radiance L - the width of the code L falls in (the
// encode's own quantum there: what a user can see change at that level, derived from the encoding, never picked)
double srgbEncode(double L) { return L <= 0.0031308 ? 12.92 * L : 1.055 * std::pow(L, 1.0 / 2.4) - 0.055; }
double srgbDecode(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }
double oneCodeAt(double L)
{
    const double code = std::min(std::floor(srgbEncode(std::max(L, 0.0)) * 255.0), 254.0);
    return srgbDecode((code + 1.0) / 255.0) - srgbDecode(code / 255.0);
}
// a directional light's node: its -Y along `dir` (the engine's light convention)
Quat aimDown(const Vec3 &dir)
{
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    const Vec3 d{ dir.x / len, dir.y / len, dir.z / len }, from{ 0.f, -1.f, 0.f };
    const Vec3 c{ from.y * d.z - from.z * d.y, from.z * d.x - from.x * d.z, from.x * d.y - from.y * d.x };
    const float w = 1.f + (from.x * d.x + from.y * d.y + from.z * d.z);
    const float n = std::sqrt(w * w + c.x * c.x + c.y * c.y + c.z * c.z);
    return Quat{ c.x / n, c.y / n, c.z / n, w / n };
}
}   // namespace

int main()
{
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-shadow-second-directional-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("second_directional", 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *scene = e->createScene("second_directional");
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
    const MaterialId mat = scene->createPbrMaterial(matte);
    if (!box(scene, Vec3(0.0f, -0.1f, 0.0f), Vec3(10.0f, 0.2f, 10.0f), mat) ||
        !box(scene, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.5f, 2.0f, 0.5f), mat)) {
        std::printf("FAIL: the ground and the post\n");
        return 1;
    }
    float sh[27] = { 0 };   // no sky, no ambient
    scene->setAmbientSh(sh);
    // B FIRST (the lower id), then the sun A
    const Vec3 dirB(0.0f, -1.0f, -1.0f), dirA(-1.0f, -1.0f, 0.0f);
    const NodeId nodeB = scene->createNode(), nodeA = scene->createNode();
    scene->setNodeTransform(nodeB, Vec3(0, 0, 0), aimDown(dirB), Vec3(1, 1, 1));
    scene->setNodeTransform(nodeA, Vec3(0, 0, 0), aimDown(dirA), Vec3(1, 1, 1));
    const auto light = [&](NodeId n, float intensity, bool primary) {
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1.f, 1.f, 1.f);
        l.intensity = intensity;
        l.castShadows = true;
        l.primaryDirectional = primary;
        return scene->setLight(n, l);
    };
    const float powerA = 2.0f / 3.14159265f, powerB = 1.0f / 3.14159265f;
    if (!light(nodeB, powerB, false) || !light(nodeA, powerA, true)) { std::printf("FAIL: the lights\n"); return 1; }
    // the eye on the shadows' side (-x, -z), so the post hides none of the squares
    const Vec3 eye(-4.0f, 6.0f, -4.0f), at(-0.5f, 0.0f, -0.5f);
    enginetest::testCameraLookAt(view, eye, at);
    e->waitForAsyncShaders();

    // the squares on the ground (y = 0): SA in A's shadow (the post's shadow runs 2 m towards -x), SB in B's (towards
    // -z), L in neither; each a grid of points 0.1 m apart, its pixels
    struct Square { const char *name; float x0, x1, z0, z1; };
    const Square squares[3] = { { "SA (A's shadow)", -1.8f, -0.7f, -0.12f, 0.12f },
                                { "SB (B's shadow)", -0.12f, 0.12f, -1.8f, -0.7f },
                                { "L (lit by both)", 0.8f, 1.9f, -1.9f, -0.8f } };
    const auto read = [&](double out[3]) {
        ImageF img;
        if (!view->readPixelsHdr(img) || img.width < 64u || img.height < 64u) return false;
        for (int q = 0; q < 3; ++q) {
            const Square &s = squares[q];
            double sum = 0.0;
            int n = 0;
            for (float x = s.x0; x <= s.x1 + 1e-4f; x += 0.1f)
                for (float z = s.z0; z <= s.z1 + 1e-4f; z += 0.04f) {
                    int px, py;
                    if (!toPixel(eye, at, Vec3(x, 0.0f, z), img.width, img.height, px, py)) return false;
                    const Colour c = img.at(unsigned(px), unsigned(py));
                    sum += std::max(c.r, std::max(c.g, c.b));
                    ++n;
                }
            out[q] = n ? sum / n : 0.0;
        }
        return true;
    };
    const auto settle = [&]() { e->waitForAsyncShaders(); run(4); };

    double both[3], aAlone[3], bAlone[3];
    settle();
    bool ok = read(both);
    // (3) the slots, read in the "both" arm
    const ShadowStatus st = e->published().shadow;
    int slotA = -1, slotB = -1;
    for (const ShadowMapInfo &m : st.mapped) {
        if (m.node == nodeA) slotA = int(m.slot);
        if (m.node == nodeB) slotB = int(m.slot);
    }
    ok = light(nodeB, 0.0f, false) && (settle(), read(aAlone)) && ok;
    ok = light(nodeB, powerB, false) && light(nodeA, 0.0f, true) && (settle(), read(bAlone)) && ok;
    if (!ok) { std::printf("FAIL: the readbacks\n"); return 1; }
    std::printf("== the squares' means (A alone / B alone / both):\n");
    for (int q = 0; q < 3; ++q)
        std::printf("   %-16s %.6f / %.6f / %.6f (the sum %.6f; one code there %.6f)\n", squares[q].name, aAlone[q],
                    bAlone[q], both[q], aAlone[q] + bAlone[q], oneCodeAt(aAlone[q] + bAlone[q]));
    std::printf("   the slots: A %d, B %d (%zu slots, %u focused maps)\n", slotA, slotB, st.mapped.size(), st.focusedMaps);

    check(aAlone[2] > 0.05 && bAlone[2] > 0.05, "the lit square is lit by each light");
    char what[256];
    std::snprintf(what, sizeof(what), "(1) A's shadow under A alone reads 0 (%.6f) within one code at the lit level (%.6f)",
                  aAlone[0], oneCodeAt(aAlone[2]));
    check(aAlone[0] <= oneCodeAt(aAlone[2]), what);
    std::snprintf(what, sizeof(what), "(1) B's shadow under B alone reads 0 (%.6f) within one code at the lit level (%.6f) "
                  "- the second directional casts", bAlone[1], oneCodeAt(bAlone[2]));
    check(bAlone[1] <= oneCodeAt(bAlone[2]), what);
    for (int q = 0; q < 3; ++q) {
        const double sum = aAlone[q] + bAlone[q];
        std::snprintf(what, sizeof(what), "(2) %s: both (%.6f) = A alone + B alone (%.6f) within one code (%.6f)",
                      squares[q].name, both[q], sum, oneCodeAt(sum));
        check(std::fabs(both[q] - sum) <= oneCodeAt(sum), what);
    }
    check(slotA == 0, "(3) the sun A holds slot 0, the PSSM set, though B was created first");
    check(slotB > 0, "(3) the second directional B holds a focused slot");
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
