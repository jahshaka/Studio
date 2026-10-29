// gi.sealed_sky (PHOTON-7 stage 10; L9: NO LIGHT OF ANY TYPE PASSES A WALL) — THE SKY CANNOT LIGHT A SEALED ROOM, on
// every store and reader that carries it. A closed box (4 x 3 x 4 m inside, 0.4 m walls, albedo 0.6) under a uniform
// white sky of radiance 1 (the SH's constant band, no cube) and nothing else; the rays on, a still picture, at High and
// at Epic.
//
// THE DEFECT IT GUARDS (10-0's P1, spikes/photon-7/stage10/README.txt): the caches' cones let the sky through the walls
// - the card relight's sixteen-cone sky share and four-cone escape (jahSkyShareFine, jahDiffuseCones) and the bounce's
// - so a sealed room read 16.3 % of an open roof at the gather's peak. Stage 10 gathers the caches by occupancy RAYS
// (jah_voxel_ray.glsl): the sky enters only at an earned miss, and a ray in a closed room never misses.
//
// THE LAWS, per tier, the bounce ON (the store's feedback converges the sky with every other light):
//   (1) THE CONTROL - the sky is on: the roof's top voxels (cascade 0's store, the side facing the sky) read over half
//       the albedo times the sky (the sky's irradiance times the albedo over pi, radiance: 0.6 for a white sky of 1) -
//       the reference R; and the card path carries the sky: the box's OUTER wall cards (read from the ground the box
//       stands on) read over a quarter of R;
//   (2) THE CARDS - the card the readers hit at every point of an 8 x 8 grid on each of the room's six faces (0.15 m in
//       from its edges: Scene::readCardAt, the hit's own pick) reads under one store step of R (q: the half float's step at R, R12);
//   (3) THE VOXELS - every occupied voxel of cascades 0 and 1 whose centre lies inside the room (its inner faces' cells:
//       the walls are two or more of those cells thick) reads under q. (The coarser cascades' voxels hold both
//       faces of a wall in one cell, their sides by orientation, not by inside and outside - the outermost's mix the
//       sky-lit roof with the ceiling: 10-1's evidence, spikes/photon-7/stage10/10-1.)
//   (4) THE FIELD - the field on (every shipped tier runs it): every probe whose lattice point lies inside the room
//       holds no texel of q (its irradiance over pi, the store's units). RED before 10-1's probe rays: the
//       aperture-0 march's escape let the sky through the walls (10-0's P3).
//   (6) THE e = 0 COUNTER (10-2a: at the ray tier the cards' indirect is gathered by TLAS rays, rq_card_gather.comp) -
//       the cards' TLAS gather ran (its dispatches and rays counted, read back: GiStatus::cards.gather) and the relight
//       read it; the card answering every point of (2)'s grid traced EXACTLY 0 rays to the sky at its last gather
//       (CardSample::gatherSky, the per-card counter: inside a watertight room no ray misses); the control: the outer
//       +x wall's cards (seen in (1)) send rays to the sky.
//   (5) THE NO-RAY ROUTE'S PICTURE (High, the rays off and the field off: the screen integrate answers every pixel) -
//       the middle of the picture inside the room, its brightest pixel under q. RED before the pixel's diffuse
//       became the caches' ray integral (the refresh's paths 4 and 7): the four cones' escape read 31.5 % of R. (The
//       specular cone's share - paths 6 and 9, 10-3's - is inside the number: 0.3 % here.)
//   (7) THE PIXEL, DIFFUSE AND SPECULAR (PHOTON-7 stage 10-3, item 1; at every tier the room is read at): the composed HDR
//       picture inside the room at rest - its DIFFUSE the matte middle (the two panels' pixels left out), its SPECULAR the
//       two METAL panels on the back wall (metalness 1: every pixel of theirs is the specular term), a smooth one
//       (roughness 0.05: the sky cube at a narrow lobe, REFLECT-1 / PCC-1) and a rough one (0.5: the rough lobe through a
//       shell) - each one's brightest pixel under q.
//   (9) THE CAPTURED CUBE (PHOTON-7 stage 10-3, the lead's condition: the sky cube at a narrow lobe measured, never
//       assumed): the same sky as a CUBEMAP of radiance 1 (the engine captures it: the environment cube the pixel's
//       specular reads - jahGlobalEnvVisibility gates it - and the SH beside it), the camera inside at rest: (7)'s three
//       reads again, each under q (R the roof's, the same sky's radiance). Last in each row (the ray tier at Epic;
//       the no-ray tier as it ships, rebuilt).
//   (8) THE TELEPORT INTO THE BOX (stage 10's (1), 2026-10-26: a ray no cascade holds reads OUTSIDE = the sky, so before a
//       cascade is built around a new pose a sealed room's nearby rays read the sky): the camera jumps from the settled
//       pose above the box to the one inside, and EVERY frame of the window until the room is at rest is read - the
//       picture's middle (the matte and the panels alike), its brightest pixel at most q in every frame.
// THE TIERS, ONE ROW EACH (argv[1]): gi.sealed_sky the ray tier at High and at Epic (laws 1-4, 6-8); gi.sealed_sky_noray
// the NO-RAY tier as it ships (rays off, the field on, no
// cards: laws 1's roof, 3, 4, 7, 8); and the no-ray route with the field off ((5) with 7, rebuilt inside: the screen
// integrate's cones answer every pixel).
// RED ON THE BASE (the caches' cones: the card relight's sixteen-cone sky share and four-cone escape, the bounce's -
// and the bounce's mean of a voxel's two sides, which let half of each sky-lit outer face light the inside).
// THE BARS ARE PHYSICS (PHOTON-7 10-3 R12): every law above reads against q, one half-float step at R (0.000244 at the
// measured R 0.4595; it was a picked 1 % of R). DECLARED RED, MEASURED 2026-10-27, one cause - THE COARSE TWO-SIDED
// CELLS (10-3's item 5: a cell holding both faces of a wall, the sky-lit outer one averaged into the inner; on the
// owner's review list, R13): the ray tier's (3) voxels c0 1.48 % of R and c1 0.60 %, (4) the field 0.081 % (its rays
// read those cells), (8) the teleport's first frame after the jump 0.46 % (the old pose's coarse cells); the no-ray
// tier's (3) c0 0.37 %, (7) / (9) the matte middle 0.10 % and (8) the same. Green against q: the cards (the brightest
// 0.0006 % - their TLAS rays never miss inside), the ray tier's pixel ((7) 0.0008 %, (9)), (5) and the field-off picture.
// A fixture (the engine alone, one small offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

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
/// THE BAR (PHOTON-7 10-3 R12, the rows re-pointed at physics): the exact answer inside a sealed room is 0, so the bar
/// is what the readback can resolve against the scene's own light - ONE STEP of the stores' half float at the roof's R
/// (the cards, the voxels, the field and the HDR picture are all RGBA16F): 2^(floor(log2 R) - 10). It replaced a picked
/// 1 % of R.
double quantumOf(double R) { return R > 0.0 ? std::ldexp(1.0, int(std::floor(std::log2(R))) - 10) : 0.0; }
void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    if (!ok) ++g_fail;
}
NodeId box(Scene *s, const Vec3 &at, const Vec3 &size, const PbrParams &p)
{
    const NodeId node = s->createNode();
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    const MeshId mesh = s->createMesh(d);
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    enginetest::setNodePosition(s, node, at);
    enginetest::setNodeScale(s, node, size);
    return node;
}
}   // namespace

int main(int argc, char **argv)
{
    // ONE ROW A TIER (PHOTON-7 stage 10-3, the lead's ruling: each within the fixture's 10 s with margin): "ray" the ray
    // tier at High and Epic (gi.sealed_sky), "noray" the no-ray tier as it ships and with the field off
    // (gi.sealed_sky_noray)
    const std::string mode = argc > 1 ? argv[1] : "ray";
    if (mode != "ray" && mode != "noray") { std::printf("FAIL: the mode is ray or noray, not '%s'\n", mode.c_str()); return 2; }
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-gi-sealed-sky-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("sealed_sky", 256, 256, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *scene = e->createScene("sealed_sky");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        fx.hdrReadback = true;
        view->setPostFx(fx);
    }
    // a still picture gathers
    const float kAlbedo = 0.6f;
    PbrParams matte;
    matte.albedo = Colour(kAlbedo, kAlbedo, kAlbedo);
    matte.roughness = 0.9f;
    // the room
    box(scene, Vec3(0.0f, -0.2f, 0.0f), Vec3(4.8f, 0.4f, 4.8f), matte);
    box(scene, Vec3(0.0f, 3.2f, 0.0f), Vec3(4.8f, 0.4f, 4.8f), matte);
    box(scene, Vec3(-2.2f, 1.5f, 0.0f), Vec3(0.4f, 3.0f, 4.0f), matte);
    const NodeId wxp = box(scene, Vec3(2.2f, 1.5f, 0.0f), Vec3(0.4f, 3.0f, 4.0f), matte);
    box(scene, Vec3(0.0f, 1.5f, -2.2f), Vec3(4.8f, 3.0f, 0.4f), matte);
    const NodeId wzp = box(scene, Vec3(0.0f, 1.5f, 2.2f), Vec3(4.8f, 3.0f, 0.4f), matte);
    box(scene, Vec3(0.0f, -0.6f, 0.0f), Vec3(20.0f, 0.4f, 20.0f), matte);   // the ground the box stands on
    // (7)'s SPECULAR: two metal panels on the back wall's inside, 4 cm thick, their front face 5 cm off the wall, filling
    // the inside camera's middle - the smooth one on the left (x < 0), the rough one on the right
    struct Panel { float x0, x1, y0, y1, z; const char *name; };
    const Panel panels[2] = { { -1.05f, -0.05f, 0.6f, 1.8f, -1.95f, "the smooth panel (roughness 0.05)" },
                              { 0.05f, 1.05f, 0.6f, 1.8f, -1.95f, "the rough panel (roughness 0.5)" } };
    for (int k = 0; k < 2; ++k) {
        PbrParams metal;
        metal.albedo = Colour(0.9f, 0.9f, 0.9f);
        metal.metalness = 1.0f;
        metal.roughness = k == 0 ? 0.05f : 0.5f;
        const Panel &q = panels[k];
        box(scene, Vec3(0.5f * (q.x0 + q.x1), 0.5f * (q.y0 + q.y1), q.z - 0.02f),
            Vec3(q.x1 - q.x0, q.y1 - q.y0, 0.04f), metal);
    }
    float sh[27] = { 0 };
    sh[0] = sh[1] = sh[2] = 1.0f;   // a uniform white sky of radiance 1
    scene->setAmbientSh(sh);

    // THE REST (gi.card_oracle's, its window cut to the law's): the chain owes nothing, and the cache admitted, captured
    // and relit nothing, for 30 frames in a row - past the readers' window (S = 10) and the demand read-back's worst
    // landing (period + lag + 1 = 9: tests/support/owed_rows.py), so the resident set the reads ask for has arrived
    bool cardsOn = true;   // the rays off (5): no cards, the chain and the field alone
    const auto settle = [&](const std::function<void(int)> &each = {}) {
        unsigned long long relights = ~0ull, admitted = ~0ull;
        int quiet = 0, f = 0;
        for (; f < 1200 && quiet < 30; ++f) {
            run(1);
            if (each) each(f);
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
    const auto middle = [&](double &mean) {
        ImageF pic;
        mean = -1.0;
        if (!view->readPixelsHdr(pic) || pic.width < 64u || pic.height < 64u) return false;
        double sum = 0.0;
        size_t n = 0;
        for (unsigned y = pic.height / 4u; y < pic.height * 3u / 4u; ++y)
            for (unsigned x = pic.width / 4u; x < pic.width * 3u / 4u; ++x) { sum += pic.at(x, y).g; ++n; }
        mean = n ? sum / double(n) : -1.0;
        return n > 0;
    };
    // a face's resident cards: their texels' brightest channel's peak and mean (texels holding nothing skipped)
    struct Read { unsigned cards = 0; size_t texels = 0; double peak = 0.0, mean = 0.0; };
    const auto readFace = [&](NodeId node, unsigned bucket) {
        Read r;
        std::vector<CardRadiance> all;
        unsigned long long cacheFrame = 0ull;
        if (!scene->readCardRadiance(all, cacheFrame)) return r;
        double sum = 0.0;
        for (const CardRadiance &c : all) {
            if (c.node != node || c.bucket != bucket || c.lastRelit == 0ull) continue;
            ++r.cards;
            for (size_t i = 0; i + 2 < c.rgb.size(); i += 3) {
                const double v = std::max(c.rgb[i], std::max(c.rgb[i + 1], c.rgb[i + 2]));
                if (!(v > 0.0)) continue;
                r.peak = std::max(r.peak, v);
                sum += v;
                ++r.texels;
            }
        }
        r.mean = r.texels ? sum / double(r.texels) : 0.0;
        return r;
    };

    // THE ROOM's voxels of one cascade: the occupied cells whose centre lies inside the room (x, z in [-2, 2], y in
    // [0, 3]); the brightest channel of each one's light (un-premultiplied); and the roof's top cells (the reference)
    struct VoxRead { size_t cells = 0; double peak = 0.0; size_t roofCells = 0; double roofMean = 0.0; bool ok = false; };
    const auto readVoxels = [&](int c) {
        VoxRead r;
        GiVoxelVolume v;
        if (!scene->giVoxelLight(c, v) || !v.available) return r;
        r.ok = true;
        double roofSum = 0.0;
        for (int z = 0; z < v.depth; ++z)
            for (int y = 0; y < v.height; ++y)
                for (int x = 0; x < v.width; ++x) {
                    const size_t i = ((size_t(z) * v.height + y) * v.width + x) * 4u;
                    const float cOcc = v.light[i + 3];
                    if (!(cOcc > 0.0f)) continue;
                    const double cx = v.origin[0] + (x + 0.5) * v.cell[0], cy = v.origin[1] + (y + 0.5) * v.cell[1],
                                 cz = v.origin[2] + (z + 0.5) * v.cell[2];
                    const double L = std::max(v.light[i], std::max(v.light[i + 1], v.light[i + 2])) / cOcc;
                    if (std::fabs(cx) < 2.0 && std::fabs(cz) < 2.0 && cy > 0.0 && cy < 3.0) {
                        ++r.cells;
                        r.peak = std::max(r.peak, L);
                    }
                    // the roof's top: the cell holding y = 3.4 over the roof's middle
                    if (std::fabs(cx) < 1.5 && std::fabs(cz) < 1.5 && cy - 0.5 * v.cell[1] <= 3.4 &&
                        cy + 0.5 * v.cell[1] > 3.4) {
                        ++r.roofCells;
                        roofSum += std::max(v.lightFront[i], std::max(v.lightFront[i + 1], v.lightFront[i + 2])) / cOcc;
                    }
                }
        r.roofMean = r.roofCells ? roofSum / double(r.roofCells) : 0.0;
        return r;
    };

    // (3) THE VOXELS of cascades 0 and 1 inside the room, against R (every tier the room is read at)
    const auto voxelLaw = [&](double R) {
        double vWorst = 0.0;
        bool vOk = true;
        for (int c = 0; c < 2; ++c) {
            const VoxRead vr = readVoxels(c);
            std::printf("   (3) cascade %d: %zu voxels inside the room, the brightest %.6f (%.4f %% of R)\n", c, vr.cells,
                        vr.peak, R > 0.0 ? 100.0 * vr.peak / R : -1.0);
            vOk = vOk && vr.ok && vr.cells > 0;
            vWorst = std::max(vWorst, vr.peak);
        }
        {
            // (the BURIED origins the caches skipped - jahRayOriginBuried: a lift crossing a front face, the origin inside
            // a solid - the voxel sides' and the cards' gather's, over the scene's life; PHOTON-7 stage 10-3)
            const GiStatus g = scene->published().gi;
            std::printf("   (3) the buried origins skipped: %llu voxel sides, %llu card texels\n", g.voxelSides.buried,
                        g.cards.gather.buried);
        }
        check(vOk && R > 0.0 && vWorst < quantumOf(R),
              "(3) THE VOXELS: no voxel of cascades 0 and 1 inside the sealed room reads one store step of the roof's");
    };
    // (4) THE FIELD's probes inside the room (gi.field_oracle's slot rule: the window's slot to its lattice point)
    const auto fieldLaw = [&](double R) {
        const GiStatus st = scene->published().gi;
        GiFieldAtlas a;
        unsigned inside = 0u, above = 0u;
        double fWorst = 0.0, fAbove = 0.0;
        const bool fok = st.ifdBound && scene->giFieldAtlas(a) && a.available && a.irradBytesPerTexel == 8u;
        if (fok) {
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
                const bool in = std::fabs(wp[0]) < 2.0f && std::fabs(wp[2]) < 2.0f && wp[1] > 0.0f && wp[1] < 3.0f;
                const bool up = wp[1] > 4.5f;   // the control: open sky above the box
                if (!in && !up) continue;
                ++(in ? inside : above);
                double &worstHere = in ? fWorst : fAbove;
                const unsigned x0 = (p * a.irradBordered) % a.irradWidth + 1u;
                const unsigned y0 = ((p * a.irradBordered) / a.irradWidth) * a.irradBordered + 1u;
                for (unsigned t = 0; t < res * res; ++t) {
                    const unsigned x = x0 + t % res, y = y0 + t / res;
                    uint16_t h[4];
                    std::memcpy(h, &a.irradiance[(size_t(y) * a.irradWidth + x) * 8u], sizeof(h));
                    for (int c = 0; c < 3; ++c) {
                        // a half's bits to its value (normal, subnormal; the field holds no Inf or NaN here)
                        const int ex = (h[c] >> 10) & 0x1f, man = h[c] & 0x3ff;
                        const double v = ex ? std::ldexp(1.0 + man / 1024.0, ex - 15) : std::ldexp(man / 1024.0, -14);
                        worstHere = std::max(worstHere, (h[c] & 0x8000) ? -v : v);
                    }
                }
            }
        }
        std::printf("   (4) the field: bound %d, %u probes inside the room, the brightest texel %.6f (%.4f %% of R); "
                    "the control, %u probes in the open above, the brightest %.4f\n",
                    int(st.ifdBound), inside, fWorst, R > 0.0 ? 100.0 * fWorst / R : -1.0, above, fAbove);
        check(fok && above > 0u && fAbove > 0.5, "(4) the control: the field's probes in the open above the box hold the sky");
        check(fok && inside > 0u && R > 0.0 && fWorst < quantumOf(R),
              "(4) THE FIELD: no probe inside the sealed room holds one store step of the roof's");
    };

    // THE INSIDE CAMERA and where a world point lands in its picture (testCameraDescLookAt's basis, 45 degrees vertical,
    // the view's square aspect; top-left origin)
    const Vec3 inPos(0.0f, 1.5f, 1.4f), inAt(0.0f, 1.0f, -2.0f);
    const auto toPixel = [&](const Vec3 &w, unsigned W, unsigned H, double &px, double &py) {
        double f[3] = { inAt.x - inPos.x, inAt.y - inPos.y, inAt.z - inPos.z };
        const double fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        for (double &c : f) c /= fl;
        double r[3] = { -f[2], 0.0, f[0] };   // f x (0, 1, 0)
        const double rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
        r[0] /= rl; r[2] /= rl;
        const double u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
        const double d[3] = { w.x - inPos.x, w.y - inPos.y, w.z - inPos.z };
        const double xc = d[0] * r[0] + d[1] * r[1] + d[2] * r[2], yc = d[0] * u[0] + d[1] * u[1] + d[2] * u[2],
                     zc = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
        const double t = std::tan(0.5 * 45.0 * 3.14159265358979 / 180.0), aspect = double(W) / double(H);
        px = (0.5 + 0.5 * xc / (zc * t * aspect)) * W;
        py = (0.5 - 0.5 * yc / (zc * t)) * H;
    };
    // a panel's pixels: the rectangle inside its four projected corners, 3 px in (`outer`: the corners' bounds, 2 px out)
    struct Rect { int x0 = 0, y0 = 0, x1 = -1, y1 = -1; bool has(int x, int y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; } };
    const auto panelRect = [&](const Panel &q, unsigned W, unsigned H, bool outer) {
        double ax[4], ay[4];
        const Vec3 c[4] = { Vec3(q.x0, q.y0, q.z), Vec3(q.x1, q.y0, q.z), Vec3(q.x0, q.y1, q.z), Vec3(q.x1, q.y1, q.z) };
        for (int k = 0; k < 4; ++k) toPixel(c[k], W, H, ax[k], ay[k]);
        Rect r;
        if (outer) {
            r.x0 = int(std::floor(std::min(std::min(ax[0], ax[1]), std::min(ax[2], ax[3])))) - 2;
            r.x1 = int(std::ceil(std::max(std::max(ax[0], ax[1]), std::max(ax[2], ax[3])))) + 2;
            r.y0 = int(std::floor(std::min(std::min(ay[0], ay[1]), std::min(ay[2], ay[3])))) - 2;
            r.y1 = int(std::ceil(std::max(std::max(ay[0], ay[1]), std::max(ay[2], ay[3])))) + 2;
        } else {
            r.x0 = int(std::ceil(std::max(ax[0], ax[2]))) + 3;   // the left corners' innermost
            r.x1 = int(std::floor(std::min(ax[1], ax[3]))) - 3;
            r.y0 = int(std::ceil(std::max(ay[2], ay[3]))) + 3;   // the top corners (y1) lie higher: smaller rows
            r.y1 = int(std::floor(std::min(ay[0], ay[1]))) - 3;
        }
        return r;
    };
    // (7)'s read of one picture: the matte middle's brightest pixel (the panels left out) and each panel's
    struct PixRead {
        bool ok = false;
        double diffuse = 0.0, spec[2] = { 0.0, 0.0 }, border = 0.0, middle = 0.0;
        unsigned specPx[2] = { 0u, 0u };
        int borderAt[2] = { -1, -1 };
    };
    const auto readPicture = [&]() {
        PixRead r;
        ImageF pic;
        if (!view->readPixelsHdr(pic) || pic.width < 64u || pic.height < 64u) return r;
        r.ok = true;
        const Rect in[2] = { panelRect(panels[0], pic.width, pic.height, false), panelRect(panels[1], pic.width, pic.height, false) };
        const Rect out[2] = { panelRect(panels[0], pic.width, pic.height, true), panelRect(panels[1], pic.width, pic.height, true) };
        for (unsigned y = 0; y < pic.height; ++y)
            for (unsigned x = 0; x < pic.width; ++x) {
                const Colour c = pic.at(x, y);
                const double v = std::max(c.r, std::max(c.g, c.b));
                const bool mid = x >= pic.width / 4u && x < pic.width * 3u / 4u && y >= pic.height / 4u && y < pic.height * 3u / 4u;
                if (mid) r.middle = std::max(r.middle, v);
                bool inner = false, outerHit = false;
                for (int k = 0; k < 2; ++k) {
                    if (in[k].has(int(x), int(y))) { r.spec[k] = std::max(r.spec[k], v); ++r.specPx[k]; inner = true; }
                    outerHit = outerHit || out[k].has(int(x), int(y));
                }
                // THE PANELS' BORDERS: their edges and side faces (metal, seen at a grazing angle) and the wall beside them
                if (outerHit && !inner && v > r.border) { r.border = v; r.borderAt[0] = int(x); r.borderAt[1] = int(y); }
                if (mid && !outerHit) r.diffuse = std::max(r.diffuse, v);
            }
        return r;
    };
    // (7) THE PIXEL at rest inside, diffuse and specular, against R
    const auto pixelLaw = [&](double R, const char *tier) {
        const PixRead p = readPicture();
        std::printf("   (7) %s, the picture inside at rest: the matte middle's brightest %.6f (%.4f %% of R); %s %.6f "
                    "(%.4f %%, %u px), %s %.6f (%.4f %%, %u px); the panels' borders %.6f (%.4f %%) at (%d, %d)\n", tier,
                    p.diffuse, R > 0.0 ? 100.0 * p.diffuse / R : -1.0, panels[0].name, p.spec[0],
                    R > 0.0 ? 100.0 * p.spec[0] / R : -1.0, p.specPx[0], panels[1].name, p.spec[1],
                    R > 0.0 ? 100.0 * p.spec[1] / R : -1.0, p.specPx[1], p.border, R > 0.0 ? 100.0 * p.border / R : -1.0,
                    p.borderAt[0], p.borderAt[1]);
        check(p.ok && R > 0.0 && p.diffuse < quantumOf(R),
              "(7) THE PIXEL'S DIFFUSE: no matte pixel inside the sealed room reads one store step of the roof's");
        check(p.ok && R > 0.0 && p.specPx[0] > 100u && p.specPx[1] > 100u && p.spec[0] < quantumOf(R) && p.spec[1] < quantumOf(R),
              "(7) THE PIXEL'S SPECULAR: no pixel of either metal panel inside the sealed room reads one store step of the roof's");
        check(p.ok && R > 0.0 && p.border < quantumOf(R),
              "(7) THE PANELS' BORDERS (their grazing edges and the wall beside them) read under one store step of the roof's");
    };
    // (8) THE TELEPORT WINDOW: the camera jumps inside, and every frame until the room is at rest is read
    struct Window { int frames = 0, worstFrame = -1, laterFrame = -1; double worst = 0.0, first = -1.0, later = 0.0, last = 0.0; bool ok = true; };
    unsigned long long skyAt = 0;   // MEASURING ARM (reverted): the cards' gather's C2 sky records at the jump
    const auto teleportIn = [&](Window &w) {
        static int jumps = 0;
        e->setArm("rayquery.skyRecReset", double(++jumps));   // (the records emptied at the jump: latched next frame)
        skyAt = 0;
        enginetest::testCameraLookAt(view, inPos, inAt);
        return settle([&](int f) {
            const PixRead p = readPicture();
            w.ok = w.ok && p.ok;
            ++w.frames;
            if (p.middle > w.worst) { w.worst = p.middle; w.worstFrame = f; }
            if (f == 0) w.first = p.middle;
            else if (p.middle > w.later) { w.later = p.middle; w.laterFrame = f; }
            w.last = p.middle;
        });
    };
    const auto teleportLaw = [&](const Window &w, double R, const char *tier) {
        const double k = R > 0.0 ? 100.0 / R : -1.0;
        {   // MEASURING ARM (reverted): the window's C2 sky answers in the cards' gather, by the secondary's start: INNER
            // (inside the room's interior box, |x|, |z| <= 2.01, -0.01 <= y <= 3.01, its normal into the room) or not
            const RayQueryStatus &rq = scene->published().rayQuery;
            const size_t n = rq.skyRec.size() / 9u;
            size_t inner = 0, shown = 0, from = 0;
            for (size_t i = from; i < n; ++i) {
                const float *r = &rq.skyRec[i * 9u];
                const bool in = std::fabs(r[0]) <= 2.01f && std::fabs(r[2]) <= 2.01f && r[1] >= -0.01f && r[1] <= 3.01f;
                if (!in) continue;
                ++inner;
                if (shown++ < 6)
                    std::printf("   SKYREC %s inner: P (%.4f %.4f %.4f) N (%.3f %.3f %.3f) L (%.3f %.3f %.3f)\n", tier, r[0], r[1],
                                r[2], r[3], r[4], r[5], r[6], r[7], r[8]);
            }
            std::printf("   SKYREC %s, the teleport window: the cards' gather's C2 sky answers %llu (recorded from %zu to %zu), of "
                        "them starting INSIDE the room %zu\n", tier, rq.skyRecords - skyAt, from, n, inner);
        }
        std::printf("   (8) %s, the teleport into the box: %d frames read to rest, the picture's middle at its brightest %.6f "
                    "(%.4f %% of R) at frame %d after the jump; the first frame %.4f %%, the brightest later %.4f %% (frame "
                    "%d), the last %.4f %%\n", tier, w.frames, w.worst, k * w.worst, w.worstFrame, k * w.first,
                    k * w.later, w.laterFrame, k * w.last);
        check(w.ok && w.frames > 0 && R > 0.0 && w.worst <= quantumOf(R),
              "(8) THE TELEPORT INTO THE BOX: no frame of the window until rest reads one store step of the roof's");
    };

    // (9) THE CAPTURED CUBE: a uniform white cubemap sky (radiance 1, the SH's sky as a cube), the camera where it stands
    const auto cubeLaw = [&](double R, const char *tier) {
        SkyDesc sky;
        sky.mode = SkyMode::Cubemap;
        std::vector<unsigned char> face(8 * 8 * 4, 255);
        for (int f = 0; f < 6; ++f) sky.faces[f] = scene->createTexture(8, 8, face.data(), true, false);
        const bool set = scene->setSky(sky);
        int f = 0;
        for (; f < 600 && scene->published().environmentPending; ++f) run(1);
        const int rest = settle();
        const PixRead p = readPicture();
        std::printf("   (9) %s, the captured cube (landed in %d frames, at rest after %d): the matte middle's brightest %.6f "
                    "(%.4f %% of R); %s %.6f (%.4f %%), %s %.6f (%.4f %%); the panels' borders %.6f (%.4f %%)\n", tier, f, rest,
                    p.diffuse, R > 0.0 ? 100.0 * p.diffuse / R : -1.0, panels[0].name, p.spec[0],
                    R > 0.0 ? 100.0 * p.spec[0] / R : -1.0, panels[1].name, p.spec[1], R > 0.0 ? 100.0 * p.spec[1] / R : -1.0,
                    p.border, R > 0.0 ? 100.0 * p.border / R : -1.0);
        check(set && rest >= 0 && p.ok && R > 0.0 && p.diffuse < quantumOf(R),
              "(9) THE CAPTURED CUBE, THE DIFFUSE: no matte pixel inside the sealed room reads one store step of the roof's");
        check(set && rest >= 0 && p.ok && R > 0.0 && p.spec[0] < quantumOf(R) && p.spec[1] < quantumOf(R),
              "(9) THE CAPTURED CUBE, THE SPECULAR: no pixel of either metal panel inside the sealed room reads one store step of the roof's");
        check(set && rest >= 0 && p.ok && R > 0.0 && p.border < quantumOf(R),
              "(9) THE CAPTURED CUBE, THE PANELS' BORDERS read under one store step of the roof's");
    };

    const GiQuality tiers[2] = { GiQuality::High, GiQuality::Epic };
    const char *tierName[2] = { "High", "Epic" };
    for (int t = 0; mode == "ray" && t < 2; ++t) {
        std::printf("== %s\n", tierName[t]);
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = tiers[t];
        gi.ddgi = GiToggle::On;
        gi.cards = true;
        gi.bounces = true;
        gi.updateBudget = 0;
        if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
        e->waitForAsyncShaders();
        // (1) the control: above the box, the ground's rays read its outer walls
        enginetest::testCameraLookAt(view, Vec3(6.0f, 7.0f, 6.0f), Vec3(0.0f, 1.0f, 0.0f));
        const int restOut = settle();
        const VoxRead out0 = readVoxels(0);
        const double R = out0.roofMean;
        const Read outer = readFace(wxp, 0u);
        const Read outerZ = readFace(wzp, 4u);
        std::printf("   (1) above: at rest after %d frames; the roof's top voxels (%zu) read %.4f - the sky's irradiance "
                    "times the albedo over pi is %.2f; the outer +x wall's cards %u (%zu texels) mean %.4f, the +z's %u "
                    "mean %.4f\n", restOut, out0.roofCells, R, double(kAlbedo), outer.cards, outer.texels, outer.mean,
                    outerZ.cards, outerZ.mean);
        const double outerMean = std::max(outer.mean, outerZ.mean);
        // (6)'s control: the outer +x wall's cards' sky rays, an 8 x 8 grid on its outer face (x = 2.4)
        long long outerE = 0;
        unsigned outerGathered = 0u;
        for (int a = 0; a < 8; ++a)
            for (int b = 0; b < 8; ++b) {
                const Vec3 at(2.4f, 0.15f + 2.7f * (a + 0.5f) / 8.0f, -1.85f + 3.7f * (b + 0.5f) / 8.0f);
                CardSample cs;
                if (!scene->readCardAt(at, Vec3(1, 0, 0), cs) || !cs.ok) continue;
                if (cs.gatherSky > 0) ++outerGathered;
                outerE = std::max(outerE, cs.gatherSky);
            }
        check(restOut >= 0 && out0.ok && R > 0.5 * kAlbedo && outerMean > 0.25 * R,
              "(1) THE CONTROL - THE SKY IS ON: the roof's voxels read over half the albedo times the sky, the outer "
              "walls' cards over a quarter of that");
        // inside: its gather reads (and so keeps resident) the room's faces - the jump there is (8)'s teleport
        Window win;
        const int restIn = teleportIn(win);
        // THE ROOM'S VISIBLE FACES, sampled where a reader can hit them: an 8 x 8 grid on each inner face, 0.15 m in from
        // its edges (a card also captures the parts of a face a neighbouring box covers - the floor under the walls -
        // whose texels gather from inside a solid and no ray can read: they are not the room's)
        struct Inner { Vec3 o, u, v, n; const char *name; };
        const Inner faces[6] = {
            { Vec3(-1.85f, 0.0f, -1.85f), Vec3(3.7f, 0, 0), Vec3(0, 0, 3.7f), Vec3(0, 1, 0), "the floor" },
            { Vec3(-1.85f, 3.0f, -1.85f), Vec3(3.7f, 0, 0), Vec3(0, 0, 3.7f), Vec3(0, -1, 0), "the ceiling" },
            { Vec3(-2.0f, 0.15f, -1.85f), Vec3(0, 2.7f, 0), Vec3(0, 0, 3.7f), Vec3(1, 0, 0), "the -x wall" },
            { Vec3(2.0f, 0.15f, -1.85f), Vec3(0, 2.7f, 0), Vec3(0, 0, 3.7f), Vec3(-1, 0, 0), "the +x wall" },
            { Vec3(-1.85f, 0.15f, -2.0f), Vec3(3.7f, 0, 0), Vec3(0, 2.7f, 0), Vec3(0, 0, 1), "the -z wall" },
            { Vec3(-1.85f, 0.15f, 2.0f), Vec3(3.7f, 0, 0), Vec3(0, 2.7f, 0), Vec3(0, 0, -1), "the +z wall" } };
        double worst = 0.0;
        long long innerE = 0;
        unsigned facesRead = 0u, innerEscaped = 0u, innerPoints = 0u, innerCounted = 0u;
        for (const Inner &f : faces) {
            unsigned read = 0u;
            double peak = 0.0, sum = 0.0;
            for (int a = 0; a < 8; ++a)
                for (int b = 0; b < 8; ++b) {
                    const float ua = (a + 0.5f) / 8.0f, vb = (b + 0.5f) / 8.0f;
                    const Vec3 at(f.o.x + f.u.x * ua + f.v.x * vb, f.o.y + f.u.y * ua + f.v.y * vb,
                                  f.o.z + f.u.z * ua + f.v.z * vb);
                    CardSample cs;
                    if (!scene->readCardAt(at, f.n, cs) || !cs.ok) continue;
                    ++read;
                    ++innerPoints;
                    if (cs.gatherSky >= 0) ++innerCounted;
                    if (cs.gatherSky != 0) ++innerEscaped;
                    innerE = std::max(innerE, cs.gatherSky);
                    const double v = std::max(cs.radiance[0], std::max(cs.radiance[1], cs.radiance[2]));
                    peak = std::max(peak, v);
                    sum += v;
                }
            std::printf("   (2) %-11s %u of 64 points carded: peak %.6f (%.4f %% of R), mean %.6f\n", f.name, read, peak,
                        R > 0.0 ? 100.0 * peak / R : -1.0, read ? sum / read : 0.0);
            if (read >= 48u) ++facesRead;
            worst = std::max(worst, peak);
        }
        std::printf("   (2) inside: at rest after %d frames, %u of 6 faces carded; the brightest point %.4f %% of R\n",
                    restIn, facesRead, R > 0.0 ? 100.0 * worst / R : -1.0);
        check(restIn >= 0 && facesRead == 6u && R > 0.0 && worst < quantumOf(R),
              "(2) THE CARDS: no card point of the sealed room's faces reads one store step of the roof's");
        {
            const GiStatus st = scene->published().gi;
            const CardCacheStatus::GatherCounts &gc = st.cards.gather;
            std::printf("   (6) the cards' TLAS gather: %llu dispatches, %llu rays - %llu cards, %llu cascades, %llu back "
                        "faces, %llu no sample, %llu sky; %llu texels (%llu with no sample); by rays %d, %llu waits\n",
                        gc.dispatches, gc.rays, gc.card, gc.voxel, gc.backFaces, gc.noSample, gc.sky, gc.texels,
                        gc.emptyTexels, int(st.cards.gatherByRays), st.cards.gatherWaits);
            std::printf("   (6) inside: %u carded points (%u of their cards counted), %u whose card sent a ray to the sky, "
                        "the most %lld; the control: the outer +x wall's cards at %u of its points escape, the most %lld "
                        "rays\n", innerPoints, innerCounted, innerEscaped, innerE, outerGathered, outerE);
            check(gc.dispatches > 0ull && gc.rays > 0ull && gc.sky > 0ull && st.cards.gatherByRays,
                  "(6) the cards' TLAS gather ran and the relight read it (its counts read back)");
            check(outerGathered > 0u && outerE > 0, "(6) the control: the outer wall's rays escape to the sky");
            check(innerPoints > 0u && innerCounted == innerPoints && innerEscaped == 0u,
                  "(6) THE e = 0 COUNTER: no ray of a carded point inside the sealed room escapes");
        }
        voxelLaw(R);
        fieldLaw(R);
        pixelLaw(R, tierName[t]);
        teleportLaw(win, R, tierName[t]);
        if (t == 1) cubeLaw(R, "Epic");
    }
    // THE NO-RAY TIER AS IT SHIPS (rays off, the field on, no cards): the roof's control, then inside - the voxels, the
    // field, the pixel and the teleport window
    double Rnoray = 0.0;
    if (mode == "noray") {
        std::printf("== High, no rays (the field on)\n");
        scene->setRayTracing(RayTracingMode::Off);
        cardsOn = false;
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.ddgi = GiToggle::On;
        gi.cards = true;
        gi.bounces = true;
        gi.updateBudget = 0;
        check(scene->setGlobalIllumination(gi), "the no-ray tier's GI builds");
        e->waitForAsyncShaders();
        enginetest::testCameraLookAt(view, Vec3(6.0f, 7.0f, 6.0f), Vec3(0.0f, 1.0f, 0.0f));
        const int restOut = settle();
        const VoxRead out0 = readVoxels(0);
        const double R = out0.roofMean;
        Rnoray = R;
        std::printf("   (1) above: at rest after %d frames; the roof's top voxels (%zu) read %.4f\n", restOut, out0.roofCells, R);
        check(restOut >= 0 && out0.ok && R > 0.5 * kAlbedo,
              "(1) THE CONTROL at the no-ray tier: the roof's voxels read over half the albedo times the sky");
        Window win;
        const int restIn = teleportIn(win);
        std::printf("   inside: at rest after %d frames\n", restIn);
        check(restIn >= 0, "the no-ray tier's room comes to rest");
        voxelLaw(R);
        fieldLaw(R);
        pixelLaw(R, "no rays");
        teleportLaw(win, R, "no rays");
    }
    // (5) printed: the no-ray route (rays off, the field off), High, the picture's middle inside against R - rebuilt
    // where the camera stands, inside (the teleport is the shipped no-ray tier's, above; R is that tier's roof)
    if (mode == "noray") {
        std::printf("== High, no rays, the field off\n");
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.ddgi = GiToggle::Off;
        gi.cards = true;
        gi.bounces = true;
        gi.updateBudget = 0;
        check(scene->setGlobalIllumination(gi), "(5) the no-ray route's GI builds");
        {
            e->waitForAsyncShaders();
            const double R = Rnoray;
            const int rest = settle();
            double mean = -1.0;
            middle(mean);
            ImageF pic;
            double peak = 0.0;
            if (view->readPixelsHdr(pic))
                for (unsigned y = pic.height / 4u; y < pic.height * 3u / 4u; ++y)
                    for (unsigned x = pic.width / 4u; x < pic.width * 3u / 4u; ++x) peak = std::max(peak, double(pic.at(x, y).g));
            std::printf("   (5) the no-ray route inside (rays off, the field off, High): at rest after %d frames, the "
                        "picture's middle mean %.6f, peak %.6f (%.4f %% / %.4f %% of R %.4f)\n", rest, mean, peak,
                        R > 0.0 ? 100.0 * mean / R : -1.0, R > 0.0 ? 100.0 * peak / R : -1.0, R);
            check(rest >= 0 && mean >= 0.0 && R > 0.5 * kAlbedo && peak < quantumOf(R),
                  "(5) THE NO-RAY ROUTE: no pixel of the picture inside the sealed room reads one store step of the roof's");
            pixelLaw(R, "no rays, the field off");
        }
    }
    if (mode == "noray") {
        // (9) at the no-ray tier as it ships: the field back on, then the captured cube
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.ddgi = GiToggle::On;
        gi.cards = true;
        gi.bounces = true;
        gi.updateBudget = 0;
        check(scene->setGlobalIllumination(gi), "(9) the no-ray tier's GI rebuilds");
        e->waitForAsyncShaders();
        settle();
        {
            cubeLaw(Rnoray, "no rays");
        }
    }
    std::printf("\n%s (%d failures, %d frames)\n", g_fail ? "FAIL" : "PASS", g_fail, frames);
    return g_fail ? 1 : 0;
}
