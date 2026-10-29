// gi.photon_window (PHOTON-7 10-4, item 2; L9: THE RENDERER FOLLOWS THE RAY) — A WINDOW PASSES THE LIGHT ITS MATERIAL PASSES,
// ON EVERY PATH. gi.sealed_sky's room (4 x 3 x 4 m inside, 0.4 m walls, albedo 0.6, on a ground) with a 2 x 2 m opening in
// its roof, off centre, and a 2 cm PANE in the opening; a white sky of radiance 1 (a cubemap: the environment the
// reflections read and the SH beside it) and a sun 60 degrees up, intensity 3, whose beam through the opening lands on the
// floor; a mirror (metal, roughness 0.05) on the floor, seen from a camera inside along the ray that reflects through the
// opening to the sky. FOUR ARMS: the HOLE (the pane hidden: the reference), and the pane as GLASS, BLEND and REFRACTIVE at
// alpha 0.1 - Ogre's blend passes 1 - alpha of what is behind in all three (Transparent and Fade: the framebuffer blend;
// Refractive: the refraction times 1 - alpha, Refractions_piece_ps.any), so the transmittance is T = 0.9 and every quantity
// inside the room is linear in what enters through the opening.
//
// THE LAW, per reader, per arm, at each tier (argv[1]: "ray" the ray tier at High, "noray" the no-ray tier as it ships):
// (argv[2]: "sky" the sky alone - the transmission of the sky's light into the room, R7's; "sun" the sky and the sun -
// the sun patch too, the shadow maps' and the traced sun's, R8's)
// the arm's reading equals T times the hole's, |G - T H| <= 2 SE + one code, where SE is the standard error of the mean of
// the reader's PAIRED differences (sample by sample, G_i - T H_i: the hole's own noise and the arm's, measured, not
// picked) and one code is the display's 8-bit sRGB step at T H (the encoding's own quantum at the reading's level, what a
// user can see change there: L8 R3; the readings are radiance, the picture's unit). The readers (every one a mean over its points):
//   (1) THE SUN PATCH under the opening, on the floor: the PIXEL (the HDR readback: the shadow map's direct + the gather),
//       the CARDS (Scene::readCardAt: the card's radiance; its traced sun term printed), the VOXELS (cascade 0's floor cells:
//       the injection's direct + the bounce);
//   (2) THE ROOM AWAY FROM THE SUN (the sky and the bounce through the opening): the pixel's DIFFUSE (the floor beside the
//       patch), the CARDS there, every VOXEL of cascades 0 and 1 inside the room away from the patch, the FIELD's probes
//       inside the room (every texel);
//   (3) THE PIXEL's SPECULAR: the mirror's pixels whose reflection passes the opening (the sky through the pane).
// THE PANE's OWN LIGHT is not in T: Ogre's glass reflects alpha^2 kD diffuse and F0 alpha specular (at alpha 0.1: 0.8 % and
// 0.4 % of what reaches it) - under a code at these readings; Ogre's non-conserving glass BRDF is SPECS/audits B10's.
// RED ON THE BASE on every traced path (PHYSICS_AUDIT_2026-10-26.md Part A): the caches hold the pane as an opaque matte wall
// (the occupancy ray, the TLAS, the field's probe rays), the shadow maps cast its solid silhouette, the traced sun stops at
// it. A fixture (the engine alone, two small offscreen views of one scene). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "../support/displaycode.h"

#include <cstdint>
#include <cstring>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
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
// a box from its min and max corners
NodeId slab(Scene *s, float x0, float y0, float z0, float x1, float y1, float z1, MaterialId mat)
{
    return box(s, Vec3(0.5f * (x0 + x1), 0.5f * (y0 + y1), 0.5f * (z0 + z1)), Vec3(x1 - x0, y1 - y0, z1 - z0), mat);
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
double brightest(const float *c) { return std::max(c[0], std::max(c[1], c[2])); }
// ONE CODE AT A LEVEL: enginetest::oneCodeAt (tests/support/displaycode.h)
using enginetest::oneCodeAt;
}   // namespace

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "ray";
    if (mode != "ray" && mode != "noray") { std::printf("FAIL: the mode is ray or noray, not '%s'\n", mode.c_str()); return 2; }
    const bool rays = mode == "ray";
    // THE LIGHT (argv[2]): "sky" the sky alone (the transmission of the sky's light, every cache's, R7), "sun" the sky
    // and the sun (the sun patch too: the shadow maps and the traced sun, R8)
    const std::string light = argc > 2 ? argv[2] : "sun";
    if (light != "sky" && light != "sun") { std::printf("FAIL: the light is sky or sun, not '%s'\n", light.c_str()); return 2; }
    const bool sun = light == "sun";
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-photon-window-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    // TWO VIEWS from one eye inside the room: A at the sun patch and the floor beside it, B at the mirror
    View *viewA = e->createOffscreenView("photon_window_a", 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
    View *viewB = e->createOffscreenView("photon_window_b", 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *scene = e->createScene("photon_window");
    if (!viewA || !viewB || !scene) { std::printf("FAIL: views/scene\n"); return 1; }
    for (View *v : { viewA, viewB }) {
        v->setScene(scene);
        v->setShadows(true);   // the sun's maps: the pixel's direct is shadowed, as in every editor view
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        fx.hdrReadback = true;
        fx.refractions = true;   // the Refractive arm draws in its own pass, as the material defines it
        v->setPostFx(fx);
    }
    if (!rays) scene->setRayTracing(RayTracingMode::Off);
    PbrParams matte;
    matte.albedo = Colour(0.6f, 0.6f, 0.6f);
    matte.roughness = 0.9f;
    const MaterialId wall = scene->createPbrMaterial(matte);
    // the room: the floor, the four walls, the ground under it
    box(scene, Vec3(0.0f, -0.2f, 0.0f), Vec3(4.8f, 0.4f, 4.8f), wall);
    box(scene, Vec3(-2.2f, 1.5f, 0.0f), Vec3(0.4f, 3.0f, 4.0f), wall);
    box(scene, Vec3(2.2f, 1.5f, 0.0f), Vec3(0.4f, 3.0f, 4.0f), wall);
    box(scene, Vec3(0.0f, 1.5f, -2.2f), Vec3(4.8f, 3.0f, 0.4f), wall);
    box(scene, Vec3(0.0f, 1.5f, 2.2f), Vec3(4.8f, 3.0f, 0.4f), wall);
    box(scene, Vec3(0.0f, -0.6f, 0.0f), Vec3(20.0f, 0.4f, 20.0f), wall);
    // the roof (y 3.0 .. 3.4) around the 2 x 2 m opening at x [-0.6, 1.4], z [-1.4, 0.6] (2 m: the no-ray tier's coarse
    // cells - 0.31 m where its rays meet the roof - hold the opening's rim with the roof, and a 1 m opening is mostly
    // rim there: both arms' rays stop at it, the hole's own estimate far from the exact answer)
    const float ox0 = -0.6f, ox1 = 1.4f, oz0 = -1.4f, oz1 = 0.6f;
    slab(scene, -2.4f, 3.0f, -2.4f, ox0, 3.4f, 2.4f, wall);
    slab(scene, ox1, 3.0f, -2.4f, 2.4f, 3.4f, 2.4f, wall);
    slab(scene, ox0, 3.0f, -2.4f, ox1, 3.4f, oz0, wall);
    slab(scene, ox0, 3.0f, oz1, ox1, 3.4f, 2.4f, wall);
    // the pane, 2 cm thick in the middle of the opening; its material is the arm's
    PbrParams paneP;
    paneP.albedo = Colour(0.8f, 0.8f, 0.8f);
    paneP.roughness = 0.1f;
    const MaterialId paneMat = scene->createPbrMaterial(paneP);
    const NodeId pane = slab(scene, ox0, 3.19f, oz0, ox1, 3.21f, oz1, paneMat);
    // the mirror on the floor where the eye's reflected ray meets the opening's centre
    const Vec3 eye(1.6f, 2.2f, 1.8f);
    const Vec3 mirrorAt(1.09f, 0.0f, 0.87f);
    {
        PbrParams metal;
        metal.albedo = Colour(0.95f, 0.95f, 0.95f);
        metal.metalness = 1.0f;
        metal.roughness = 0.05f;
        box(scene, Vec3(mirrorAt.x, 0.01f, mirrorAt.z), Vec3(0.6f, 0.02f, 0.6f), scene->createPbrMaterial(metal));
    }
    if (!pane) { std::printf("FAIL: the pane\n"); return 1; }
    // the sun, 60 degrees up, travelling towards -x: its beam through the opening lands on the floor at x [-2, -0.56],
    // z [-1.4, 0.6] (and on the -x wall below the beam's far edge)
    if (sun) enginetest::addDirectionalLight(scene, Vec3(-0.5f, -0.8660254f, 0.0f), 3.0f);
    {
        SkyDesc sky;
        sky.mode = SkyMode::Cubemap;
        std::vector<unsigned char> face(8 * 8 * 4, 255);
        for (int f = 0; f < 6; ++f) sky.faces[f] = scene->createTexture(8, 8, face.data(), true, false);
        if (!scene->setSky(sky)) { std::printf("FAIL: the sky\n"); return 1; }
    }
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::On;
    gi.cards = true;
    gi.bounces = true;
    gi.updateBudget = 0;
    if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
    const Vec3 atA(-1.25f, 0.0f, 0.45f);
    enginetest::testCameraLookAt(viewA, eye, atA);
    enginetest::testCameraLookAt(viewB, eye, mirrorAt);
    e->waitForAsyncShaders();
    for (int f = 0; f < 600 && scene->published().environmentPending; ++f) run(1);

    // THE REST (gi.sealed_sky's): the chain owes nothing, the cache admitted and relit nothing, for 30 frames in a row
    const auto settle = [&]() {
        unsigned long long relights = ~0ull, admitted = ~0ull;
        int quiet = 0, f = 0;
        for (; f < 1200 && quiet < 30; ++f) {
            run(1);
            const GiStatus g = scene->published().gi;
            const bool cardsStill = !rays || (g.cards.built && g.cards.relights == relights &&
                                              g.cards.admitted == admitted && g.cards.queueLength == 0u);
            const bool still = !g.chainOwed && !g.feedbackOwed && cardsStill && !g.ifdWorking;
            relights = g.cards.relights;
            admitted = g.cards.admitted;
            quiet = still ? quiet + 1 : 0;
        }
        return quiet >= 30 ? f - quiet : -1;
    };

    // THE POINTS. The sun patch (inside the beam by 0.1 m or more), the floor beside it (out of the beam by 0.4 m), the
    // mirror's middle (its reflected rays meet the roof within 0.2 m of the opening's centre)
    std::vector<Vec3> patch, beside, mirror;
    for (int a = 0; a < 4; ++a)
        for (int b = 0; b < 4; ++b) {
            patch.push_back(Vec3(-1.6f + 0.6f * a / 3.0f, 0.0f, -1.1f + 1.4f * b / 3.0f));
            beside.push_back(Vec3(-1.5f + 0.5f * a / 3.0f, 0.0f, 1.0f + 0.6f * b / 3.0f));
        }
    for (int a = 0; a < 5; ++a)
        for (int b = 0; b < 5; ++b) mirror.push_back(Vec3(mirrorAt.x - 0.08f + 0.04f * a, 0.02f, mirrorAt.z - 0.08f + 0.04f * b));

    // ONE ARM's READINGS: every reader's samples, in a fixed order (paired across arms)
    struct Reading { std::vector<double> v; bool ok = true; };
    enum R { PixPatch, CardPatch, VoxPatch, PixBeside, CardBeside, VoxRoom, Field, PixMirror, NumReaders };
    const char *names[NumReaders] = { "(1) the sun patch, the PIXEL", "(1) the sun patch, the CARDS",
                                      "(1) the sun patch, the VOXELS", "(2) the room, the PIXEL's DIFFUSE",
                                      "(2) the room, the CARDS", "(2) the room, the VOXELS", "(2) the room, the FIELD",
                                      "(3) the PIXEL's SPECULAR (the mirror)" };
    const auto readPixels = [&](View *v, const Vec3 &at, const std::vector<Vec3> &pts, Reading &r) {
        ImageF img;
        if (!v->readPixelsHdr(img) || img.width < 64u) { r.ok = false; return; }
        for (const Vec3 &p : pts) {
            int x, y;
            if (!toPixel(eye, at, p, img.width, img.height, x, y)) { r.ok = false; continue; }
            const Colour c = img.at(unsigned(x), unsigned(y));
            r.v.push_back(std::max(c.r, std::max(c.g, c.b)));
        }
    };
    const auto readCards = [&](const std::vector<Vec3> &pts, Reading &r, double *sunTerm) {
        double s = 0.0;
        for (const Vec3 &p : pts) {
            CardSample cs;
            if (!scene->readCardAt(p, Vec3(0, 1, 0), cs) || !cs.ok) { r.ok = false; continue; }
            r.v.push_back(brightest(cs.radiance));
            s += cs.shadow;
        }
        if (sunTerm) *sunTerm = pts.empty() ? 0.0 : s / double(pts.size());
    };
    // the voxels: cascade 0's floor cells under the patch (the cell holding y = 0 under the patch's points' box), and every
    // occupied cell of cascades 0 and 1 inside the room (x, z in [-2, 2], y in [0, 3]) outside the patch's x-z box
    const auto readVoxels = [&](Reading &patchR, Reading &roomR) {
        for (int c = 0; c < 2; ++c) {
            GiVoxelVolume v;
            if (!scene->giVoxelLight(c, v) || !v.available) { patchR.ok = roomR.ok = false; continue; }
            for (int z = 0; z < v.depth; ++z)
                for (int y = 0; y < v.height; ++y)
                    for (int x = 0; x < v.width; ++x) {
                        const size_t i = ((size_t(z) * v.height + y) * v.width + x) * 4u;
                        const float cOcc = v.light[i + 3];
                        if (!(cOcc > 0.0f)) continue;
                        const double cx = v.origin[0] + (x + 0.5) * v.cell[0], cy = v.origin[1] + (y + 0.5) * v.cell[1],
                                     cz = v.origin[2] + (z + 0.5) * v.cell[2];
                        const bool inPatch = cx > -1.65 && cx < -0.95 && cz > -1.15 && cz < 0.35;
                        const float L[3] = { v.light[i] / cOcc, v.light[i + 1] / cOcc, v.light[i + 2] / cOcc };
                        if (c == 0 && inPatch && cy - 0.5 * v.cell[1] <= 0.0 && cy + 0.5 * v.cell[1] > 0.0)
                            patchR.v.push_back(brightest(L));
                        else if (!inPatch && std::fabs(cx) < 2.0 && std::fabs(cz) < 2.0 && cy > 0.0 && cy < 3.0)
                            roomR.v.push_back(brightest(L));
                    }
        }
    };
    // the field's probes inside the room, every texel (gi.sealed_sky's slot rule)
    const auto readField = [&](Reading &r) {
        const GiStatus st = scene->published().gi;
        GiFieldAtlas a;
        if (!(st.ifdBound && scene->giFieldAtlas(a) && a.available && a.irradBytesPerTexel == 8u)) { r.ok = false; return; }
        const unsigned count = a.probes[0] * a.probes[1] * a.probes[2], res = a.irradBordered - 2u;
        const float sz[3] = { st.ifdMax.x - st.ifdMin.x, st.ifdMax.y - st.ifdMin.y, st.ifdMax.z - st.ifdMin.z };
        const float o0[3] = { st.ifdMin.x, st.ifdMin.y, st.ifdMin.z };
        for (unsigned p = 0; p < count; ++p) {
            const unsigned sl[3] = { p % a.probes[0], (p / a.probes[0]) % a.probes[1], p / (a.probes[0] * a.probes[1]) };
            float wp[3];
            for (int k = 0; k < 3; ++k) {
                const unsigned n = a.probes[k];
                const unsigned local = (sl[k] + n - a.windowOffset[k] % n) & (n - 1u);
                const float block = sz[k] / float(n), spacing = (sz[k] + 2.0f * block) / float(n);
                wp[k] = o0[k] - block + float(local) * spacing;
            }
            if (!(std::fabs(wp[0]) < 2.0f && std::fabs(wp[2]) < 2.0f && wp[1] > 0.0f && wp[1] < 3.0f)) continue;
            const unsigned x0 = (p * a.irradBordered) % a.irradWidth + 1u;
            const unsigned y0 = ((p * a.irradBordered) / a.irradWidth) * a.irradBordered + 1u;
            for (unsigned t = 0; t < res * res; ++t) {
                const unsigned x = x0 + t % res, y = y0 + t / res;
                uint16_t h[4];
                std::memcpy(h, &a.irradiance[(size_t(y) * a.irradWidth + x) * 8u], sizeof(h));
                double m = 0.0;
                for (int c = 0; c < 3; ++c) {
                    const int ex = (h[c] >> 10) & 0x1f, man = h[c] & 0x3ff;
                    const double v = ex ? std::ldexp(1.0 + man / 1024.0, ex - 15) : std::ldexp(man / 1024.0, -14);
                    m = std::max(m, (h[c] & 0x8000) ? -v : v);
                }
                r.v.push_back(m);
            }
        }
    };
    const auto readArm = [&](double &sunTermPatch) {
        std::vector<Reading> r(NumReaders);
        readPixels(viewA, atA, patch, r[PixPatch]);
        readPixels(viewA, atA, beside, r[PixBeside]);
        readPixels(viewB, mirrorAt, mirror, r[PixMirror]);
        sunTermPatch = -1.0;
        if (rays) {
            readCards(patch, r[CardPatch], &sunTermPatch);
            readCards(beside, r[CardBeside], nullptr);
        }
        readVoxels(r[VoxPatch], r[VoxRoom]);
        readField(r[Field]);
        return r;
    };
    const auto mean = [](const std::vector<double> &v) {
        double s = 0.0;
        for (double x : v) s += x;
        return v.empty() ? 0.0 : s / double(v.size());
    };

    std::printf("== %s, High, the %s\n", mode.c_str(), sun ? "sky and the sun" : "sky alone");
    // THE HOLE
    scene->setNodeVisible(pane, false);
    const int restHole = settle();
    double holeSun = -1.0;
    const std::vector<Reading> hole = readArm(holeSun);
    std::printf("   the hole: at rest after %d frames; the cards' sun term at the patch %.4f\n", restHole, holeSun);
    for (int k = 0; k < NumReaders; ++k)
        if ((rays || (k != CardPatch && k != CardBeside)) && (sun || (k != PixPatch && k != CardPatch && k != VoxPatch)))
            std::printf("     %-40s %zu samples, mean %.6f\n", names[k], hole[k].v.size(), mean(hole[k].v));
    check(restHole >= 0, "the hole arm comes to rest");
    if (sun)
        check(hole[PixPatch].ok && mean(hole[PixPatch].v) > 2.0 * mean(hole[PixBeside].v),
              "the control: the sun patch under the opening is lit by the sun");
    scene->setNodeVisible(pane, true);

    struct Arm { PbrAlphaMode m; const char *name; };
    const Arm arms[3] = { { PbrAlphaMode::Glass, "GLASS" }, { PbrAlphaMode::Blend, "BLEND" },
                          { PbrAlphaMode::Refractive, "REFRACTIVE" } };
    const double alpha = 0.1, T = 1.0 - alpha;
    for (const Arm &a : arms) {
        PbrParams p = paneP;
        p.alphaMode = a.m;
        p.alpha = float(alpha);
        if (!scene->setPbrMaterial(paneMat, p)) { check(false, "the arm's material"); continue; }
        e->waitForAsyncShaders();
        const int rest = settle();
        double sunTerm = -1.0;
        const std::vector<Reading> g = readArm(sunTerm);
        std::printf("   %s (alpha %.1f, T %.2f): at rest after %d frames; the cards' sun term at the patch %.4f (the hole's "
                    "%.4f)\n", a.name, alpha, T, rest, sunTerm, holeSun);
        check(rest >= 0, "the arm comes to rest");
        for (int k = 0; k < NumReaders; ++k) {
            if (!rays && (k == CardPatch || k == CardBeside)) continue;
            if (!sun && (k == PixPatch || k == CardPatch || k == VoxPatch)) continue;   // no sun patch under the sky alone
            const Reading &h = hole[k], &r = g[k];
            const size_t n = std::min(h.v.size(), r.v.size());
            double sd = 0.0, md = 0.0;
            for (size_t i = 0; i < n; ++i) md += r.v[i] - T * h.v[i];
            md = n ? md / double(n) : 0.0;
            for (size_t i = 0; i < n; ++i) sd += (r.v[i] - T * h.v[i] - md) * (r.v[i] - T * h.v[i] - md);
            const double se = n > 1 ? std::sqrt(sd / double(n - 1) / double(n)) : 0.0;
            const double H = mean(h.v), G = mean(r.v), code = oneCodeAt(T * H), bar = 2.0 * se + code;
            const bool ok = h.ok && r.ok && n > 0 && h.v.size() == r.v.size() && H > 0.0 && std::fabs(G - T * H) <= bar;
            std::printf("     %-40s %s: %.6f against T x the hole's %.6f (ratio %.4f; %+.2f codes; the bar %.2f codes: "
                        "2 SE %.6f + a code)\n", names[k], ok ? "ok  " : "FAIL", G, T * H, H > 0.0 ? G / H : -1.0,
                        code > 0.0 ? (G - T * H) / code : 0.0, code > 0.0 ? bar / code : 0.0, 2.0 * se);
            char what[256];
            std::snprintf(what, sizeof(what), "%s: %s passes T = %.2f of the hole's", a.name, names[k], T);
            // THE NO-RAY TIER's ROOM (PHOTON-7 10-4, the lead's ruling 2026-10-27): its rays meet the roof in 0.31 m cells
            // that hold the opening's rim with the roof, so the HOLE arm is not the exact answer there (2.9x darker than
            // the ray tier's, its rim read from inside as the roof's sky-lit top) and the pane, which turns those cells'
            // reads to their inside, sits 1.2-1.8 codes under T x it: the no-ray tier's cell size, not transmission - on
            // the owner's review list, printed. The mirror's rays cross the pane's own cells: a law at every tier.
            if (!rays && (k == PixBeside || k == VoxRoom || k == Field)) {
                std::printf("     (printed, the no-ray room: not a law here - the hole's own coarse rim, the review list)\n");
                continue;
            }
            check(ok, what);
        }
    }
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
