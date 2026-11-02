// gi.feedback_closed_box.<part> and gi.feedback_facing_pair (BISTRO-REST-1, the owner's order 2026-11-02: "create a real
// test and test it with that first") - PHOTON'S FEEDBACK AGAINST CLOSED FORMS: THE FIXED POINT IS THE PHYSICS, A CHANGE
// NEVER RISES, AND A REST IS A REST. The voxel store read directly (Scene::giVoxelLight: the half store), at the RAY TIER
// (the voxel sides gathered by TLAS rays through the one hit read, jah_rq_hit.glsl), Epic. One part a row (the argument,
// <scene>[.<step>]: one step a process, the fixture's 10 s budget).
//
// THE BOX (parts box, scroll, lamp, room): a SEALED 8 x 4 x 8 m room (0.3 m slabs, every seam overlapped - gi.bounce_series'
// room) whose inner walls EMIT a uniform radiance E = 0.1 and reflect with albedo rho = 0.9 at roughness 1 (a dielectric,
// F0 0.04), no sky. THE CLOSED FORM (checked by hand): a wall's surface radiance is its emission plus its re-emission of
// what it gathers, L = E + a' <L_in> - <L_in> the cosine-weighted mean radiance its hemisphere sees, a' the material's WHOLE
// lobe integral: the diffuse hemispherical albedo rho x DA_hemi(1) plus the 4 % specular layer's F0 A-bar + B-bar
// (enginetest::disneyDiffuseAlbedoHemi / ggxSpecularAlbedoHemi, by direct integration; gi.bounce_series (2)'s a'). In a
// SEALED room of one E and one a' the uniform L* = E / (1 - a') satisfies it EXACTLY at every point (every direction of
// every hemisphere ends on a wall at L*, so <L_in> = L*), and it is the UNIQUE fixed point (a' < 1: a contraction).
// a' = 0.9 x 0.70542 + 0.01901 = 0.65390, L* = 0.1 / 0.34610 = 0.28893.
// THE PAIR (parts pair.up / pair.down, gi.feedback_facing_pair.*): TWO CONCENTRIC SPHERES - the inner (radius 1 m, its faces outward)
// emitting E1 = 0.1 with albedo 0.9 (a1 = 0.65390), the outer (radius 3 m, its faces inward) not emitting, albedo 0.98
// (a2 = 0.98 x 0.70542 + 0.01901 = 0.71032), nothing else. THE CLOSED FORM (checked by hand): by symmetry each sphere's
// radiance is uniform; the inner sphere's hemisphere sees only the outer (F12 = 1), the outer's sees the inner over the
// share F21 = (r1 / r2)^2 = 1/9 of its cosine-weighted hemisphere (the inner sphere's projected solid angle from any point of
// the outer, over pi: exactly the area ratio for concentric spheres) and itself over the rest, so
//   L1 = E1 + a1 L2,   L2 = a2 ( F21 L1 + (1 - F21) L2 )
//   =>  L2 = a2 F21 E1 / ( 1 - a2 (1 - F21) - a1 a2 F21 ) = 0.024898,  L1 = E1 + a1 L2 = 0.116281.
// The pair's Jacobi matrix [[0, a1], [a2 F21, a2 (1 - F21)]] has eigenvalues (a2 (1 - F21) +- sqrt( (a2 (1 - F21))^2 +
// 4 a1 a2 F21 )) / 2 = +0.705, -0.073: a sign-flipping mode (-s) - the two-surface exchange the iteration must not orbit in.
// THE PARTS: box.up / box.down (the closed form and the emission stepped up; the down row steps it up to its rest and
// back), pair.up / pair.down (the same, the spheres), scroll (gi.scroll_slab
// G's ingredients: a two-cascade 64-cell chain at High - 5 m / 20 m - the roof open to a sun of 5 and a sky of 0.05, an
// emissive panel in the slab a pinned cascade-0 scroll brings in), lamp.up / lamp.down (an interior lamp 8 -> 12 and,
// from its rest at 12, back to 8; the field on, a
// 1-m window in the +z wall to a sky of 0.5, the picture 64 x 64 - a guard: photon-7 bursts three times, this lane's tip
// none) and room (the lamp's up step with the cards on and the picture at gi.hold_after_stop's 960 x 540 - (f)'s ingredient:
// the gather's probe grid at the picture's real size; `nocards` as a third argument takes the cards off).
// THE LAWS, on the surface voxels (the box's: a cell inside a face, a metre from its edges, its room-facing half covered;
// the spheres': a cell within a cell of the sphere, its half facing along the radius covered; L = that half's light over
// its coverage):
//   (i)  THE FIXED POINT IS THE CLOSED FORM (box, pair): at the feedback's rest every voxel within ONE CODE plus ONE STORE
//        STEP of the closed form - the engine's own bounds: a brick stops once the tail of its series is under a code
//        (PhotonBrickFeedback_cs.glsl), and the half-float store rounds to nearest (a step 2^(floor(log2 L) - 10)).
//        Printed beside it, THE DEFICIT SPLIT: a 64-pass Jacobi of the same state (the door gi.bounceJacobi - no stop law,
//        but WITH the write hold: "feedback = Jacobi" means the feedback equals the HELD Jacobi) - the Jacobi against the
//        closed form is what the gather reads off it, the feedback against the Jacobi is where the iteration stops.
//        DECLARED RED to GATHER-TRUTH-1 (the lead, 2026-11-02): box -1.451 %, pair inner +0.785 %, outer +8.266 % on this
//        lane's tip (photon-7: -18.68 / +0.49 / +6.59 %); the closed form above is LAMBERTIAN while HlmsPbs's diffuse is
//        Disney and the hit read sends the bounce by DA(N.V) / DA_hemi - the expectation to be re-derived from the lobe
//        reference (GATHER-TRUTH-1 S0). Never loosened here.
//   (ii') MONOTONE TO THE NEW FIXED POINT (box, pair, lamp; up and down), per voxel: after a step UP of the sources the old
//        fixed point is a SUB-solution, x_old = A x_old + b_old <= A x_old + b_new, and for a positive A every update in
//        ANY order - Jacobi, Gauss-Seidel, the feedback's asynchronous slices - keeps x non-decreasing and <= x* (by
//        induction): no voxel moves down and none passes its new fixed point (the value it rests at). A step DOWN mirrors
//        it (the old fixed point a SUPER-solution). The one allowance is the store's rounding: ONE STORE STEP (the write
//        hold's quantum). A scheduler's pause cannot break it; an orbit, an overshoot or a read of a stale or mixed state
//        can. The box's and the pair's steps are MATERIAL SWAPS (setNodeMaterial: the emission doubled, then halved
//        back) - DECLARED RED to MATERIAL-SWAP-1 (photon-7's own: box up 17,608 moves against the step / 99,734 past,
//        this tip 1,338 / 14,695); the lamp's are relights.
//   (iii) A REST IS A REST (every part): from the first frame the chain and the feedback owe nothing (GiStatus::chainOwed /
//        feedbackOwed false), for 2 x lightWorkFrames + 2 frames nothing is owed, the half store does not change by a
//        bit (Scene::giHalfStoreWords: its words as stored, compared word for word) and no brick resumes or passes - the window is the floor share's bound on how late an owed brick is
//        passed (GiQualityFacts::lightWorkFrames: a cascade's floor alone pays its volume in 2 x lightWorkFrames frames;
//        a slab's rounding and the round's own frame the +2), so a write still owed when the rest was read lands in it.
//   THE PICTURE (lamp.up, room): its per-frame mean change after the lamp's step up goes to <work>/lamp_picture.txt for the
//        row's wrapper to judge by tests/support/rise_law.py (gi.hold_after_stop (f)'s law), with a PIC line a frame (the
//        field's dispatch, its owed restarts, the store's active bricks: what wrote that frame). The room's DECLARED RED
//        to PROBE-GATHER-REST-1: the screen-probe gather's picture bursts after a lamp step while the store is monotone
//        and resting (the field dispatching nothing, the store's active bricks falling) - photon-7's own (six rises
//        there, eight on this lane's tip, the same 4-frame cadence).
// A fixture: one offscreen view (the cards off but in the room part; the field off but in the lamp and room parts; the
// gather on at every ray chain; the rays on).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "../support/lawline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

namespace {
const float kE = 0.1f;
const float kRho = 0.9f, kRhoOuter = 0.98f;
const float kHalfX = 4.0f;
const float kHalfZ = 4.0f, kHeight = 4.0f, kThick = 0.3f;
const float kR1 = 1.0f, kR2 = 3.0f;
const Vec3 kSphereCentre(0.0f, 4.0f, 0.0f);

double storeStep(double L)
{
    const double a = std::fabs(L);
    return a < 6.1035156e-5 ? 5.9604645e-8 : std::ldexp(1.0, int(std::floor(std::log2(a))) - 10);
}
double lobe(double rho)
{
    double sA = 0.0, sB = 0.0;
    enginetest::ggxSpecularAlbedoHemi(1.0, sA, sB);
    return rho * enginetest::disneyDiffuseAlbedoHemi(1.0) + 0.04 * sA + sB;
}
MaterialId material(Scene *s, float rho, float emission)
{
    PbrParams p;
    p.albedo = Colour(rho, rho, rho);
    p.roughness = 1.0f;
    p.emissive = Colour(emission, emission, emission);
    return s->createPbrMaterial(p);
}
NodeId slab(Scene *s, const Vec3 &centre, const Vec3 &size, float emission)
{
    const NodeId n = s->createNode();
    s->attachMesh(n, s->createMesh(enginetest::unitCubeMesh()), material(s, kRho, emission));
    enginetest::setNodePosition(s, n, centre);
    enginetest::setNodeScale(s, n, size);
    return n;
}
/// a UV sphere of radius r, its faces outward (or inward: the winding and the normals flipped)
MeshData sphereMesh(float r, bool inward, int seg = 96, int rings = 48)
{
    MeshData d;
    for (int j = 0; j <= rings; ++j) {
        const float th = float(M_PI) * float(j) / float(rings);
        for (int i = 0; i <= seg; ++i) {
            const float ph = 2.0f * float(M_PI) * float(i) / float(seg);
            const float n[3] = { std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph) };
            d.positions.insert(d.positions.end(), { r * n[0], r * n[1], r * n[2] });
            const float sg = inward ? -1.0f : 1.0f;
            d.normals.insert(d.normals.end(), { sg * n[0], sg * n[1], sg * n[2] });
        }
    }
    for (int j = 0; j < rings; ++j)
        for (int i = 0; i < seg; ++i) {
            const unsigned a = unsigned(j * (seg + 1) + i), b = a + unsigned(seg + 1);
            // (counter-clockwise seen from the side the faces look to: the engine's front, unitCubeMesh's winding)
            if (inward) d.indices.insert(d.indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
            else d.indices.insert(d.indices.end(), { a, a + 1, b, a + 1, b + 1, b });
        }
    return d;
}

/// a half-float word's value (IEEE binary16)
double halfValue(std::uint16_t h)
{
    const int e = (h >> 10) & 0x1f, m = h & 0x3ff;
    const double v = e == 0 ? std::ldexp(double(m), -24) : e == 31 ? (m ? NAN : INFINITY) : std::ldexp(double(m | 0x400), e - 25);
    return (h & 0x8000) ? -v : v;
}
/// the surface voxels of cascade 0: per voxel the covered half that faces into the free space, its light over its
/// coverage, and which surface (0 the box / the inner sphere, 1 the outer sphere)
struct Reading {
    bool ok = false;
    std::vector<float> L;
    std::vector<int> surface;
    double mean[2] = { 0.0, 0.0 };
    size_t count[2] = { 0, 0 };
};
/// `strip`: the per-frame read (ii') takes - only one strip's cells downloaded and decoded (Scene::giVoxelLight's world
/// box: the box's floor, the spheres' equator), so a frame's read stays in the row's budget; else the whole cascade
Reading read(Scene *s, bool spheres, bool strip = false)
{
    Reading r;
    GiVoxelVolume v;
    if (strip) {
        const float lo[3] = { spheres ? kSphereCentre.x - kR2 - 0.5f : -kHalfX, spheres ? kSphereCentre.y - 0.1f : -0.3f,
                              spheres ? kSphereCentre.z - kR2 - 0.5f : -kHalfZ };
        const float hi[3] = { spheres ? kSphereCentre.x + kR2 + 0.5f : kHalfX, spheres ? kSphereCentre.y + 0.1f : 0.3f,
                              spheres ? kSphereCentre.z + kR2 + 0.5f : kHalfZ };
        if (!s->giVoxelLight(0, v, lo, hi) || !v.available || v.halfLight.empty()) return r;
    } else if (!s->giVoxelLight(0, v) || !v.available || v.halfLight.empty()) {
        return r;
    }
    const float cell = v.cell[0];
    for (int z = 0; z < v.depth; ++z)
        for (int y = 0; y < v.height; ++y)
            for (int x = 0; x < v.width; ++x) {
                const float p[3] = { v.origin[0] + cell * (x + 0.5f), v.origin[1] + cell * (y + 0.5f),
                                     v.origin[2] + cell * (z + 0.5f) };
                float in[3] = { 0.0f, 0.0f, 0.0f };   // the direction into the free space
                int surf = 0;
                if (spheres) {
                    const float q[3] = { p[0] - kSphereCentre.x, p[1] - kSphereCentre.y, p[2] - kSphereCentre.z };
                    const float rad = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
                    if (std::fabs(rad - kR1) < cell) surf = 0;
                    else if (std::fabs(rad - kR2) < cell) surf = 1;
                    else continue;
                    const float sg = surf == 0 ? 1.0f : -1.0f;
                    for (int k = 0; k < 3; ++k) in[k] = sg * q[k] / std::max(rad, 1e-6f);
                } else {
                    const bool inGrown = std::fabs(p[0]) < kHalfX + cell && std::fabs(p[2]) < kHalfZ + cell &&
                                         p[1] > -cell && p[1] < kHeight + cell;
                    const bool inShrunk = std::fabs(p[0]) < kHalfX - cell && std::fabs(p[2]) < kHalfZ - cell &&
                                          p[1] > cell && p[1] < kHeight - cell;
                    if (!inGrown || inShrunk) continue;
                    int near = 0;
                    near += std::fabs(p[0]) > kHalfX - 1.0f ? 1 : 0;
                    near += std::fabs(p[2]) > kHalfZ - 1.0f ? 1 : 0;
                    near += (p[1] < 1.0f || p[1] > kHeight - 1.0f) ? 1 : 0;
                    if (near > 1) continue;
                    const float dx = kHalfX - std::fabs(p[0]), dz = kHalfZ - std::fabs(p[2]), dy0 = p[1], dy1 = kHeight - p[1];
                    const float dmin = std::min(std::min(std::fabs(dx), std::fabs(dz)), std::min(std::fabs(dy0), std::fabs(dy1)));
                    if (dmin == std::fabs(dy0)) in[1] = 1.0f;
                    else if (dmin == std::fabs(dy1)) in[1] = -1.0f;
                    else if (dmin == std::fabs(dx)) in[0] = p[0] > 0.0f ? -1.0f : 1.0f;
                    else in[2] = p[2] > 0.0f ? -1.0f : 1.0f;
                }
                // the dominant axis of the free-space direction, its half (axis a's -a half at block 2a, +a at 2a + 1)
                const int a = std::fabs(in[0]) >= std::fabs(in[1]) && std::fabs(in[0]) >= std::fabs(in[2]) ? 0
                              : std::fabs(in[1]) >= std::fabs(in[2])                                       ? 1 : 2;
                const size_t b = size_t(2 * a + (in[a] > 0.0f ? 1 : 0));
                const float *side = &v.halfLight[(((size_t(z) * v.height + y) * 6u + b) * v.width + x) * 4u];
                if (!(side[3] > 0.5f)) continue;
                const float L = std::max(std::max(side[0], side[1]), side[2]) / side[3];
                r.L.push_back(L);
                r.surface.push_back(surf);
                r.mean[surf] += L;
                ++r.count[surf];
            }
    for (int k = 0; k < 2; ++k) if (r.count[k]) r.mean[k] /= double(r.count[k]);
    r.ok = !r.L.empty();
    return r;
}
}   // namespace

int main(int argc, char **argv)
{
    // the part: <scene>[.<step>] - the scene box, pair, scroll, lamp or room; the step up or down (each its own row, the
    // fixture's 10 s budget: one step a process)
    const std::string part = argc > 1 ? argv[1] : "box.up";
    const std::string work = argc > 2 ? argv[2] : ".";
    const std::string scene = part.substr(0, part.find('.'));
    const std::string step = part.find('.') == std::string::npos ? std::string() : part.substr(part.find('.') + 1);
    const bool spheres = scene == "pair";
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-feedback-" + part + "-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int rendered = 0;   // (the row's FRAMES: every frame it renders, counted)
    const auto run = [&](int n) { enginetest::frames(e, n); rendered += n; };
    // (the room part: hold_after_stop's 960 x 540 picture - the gather's probes are the picture's, so its size is an
    // ingredient; the others a 64 x 64 view)
    const bool room = scene == "room";
    View *view = e->createOffscreenView("feedback", room ? 960 : 64, room ? 540 : 64, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *s = e->createScene("feedback");
    if (!view || !s) { std::printf("FAIL: the view and the scene\n"); return 1; }
    view->setScene(s);
    float sh[27] = {};
    s->setAmbientSh(sh);   // no sky
    std::vector<NodeId> emitters;
    double target[2] = { 0.0, 0.0 };
    const double a1 = lobe(kRho), a2 = lobe(kRhoOuter);
    if (spheres) {
        const NodeId in = s->createNode();
        s->attachMesh(in, s->createMesh(sphereMesh(kR1, false)), material(s, kRho, kE));
        enginetest::setNodePosition(s, in, kSphereCentre);
        emitters.push_back(in);
        const NodeId out = s->createNode();
        s->attachMesh(out, s->createMesh(sphereMesh(kR2, true)), material(s, kRhoOuter, 0.0f));
        enginetest::setNodePosition(s, out, kSphereCentre);
        const double F21 = (kR1 / kR2) * (kR1 / kR2);
        target[1] = a2 * F21 * kE / (1.0 - a2 * (1.0 - F21) - a1 * a2 * F21);
        target[0] = kE + a1 * target[1];
        const double tr = a2 * (1.0 - F21), disc = std::sqrt(tr * tr + 4.0 * a1 * a2 * F21);
        std::printf("   the pair: a1 %.5f, a2 %.5f, F21 %.5f; L1 = %.6f, L2 = %.6f; the Jacobi's eigenvalues %+.4f, %+.4f\n",
                    a1, a2, F21, target[0], target[1], 0.5 * (tr + disc), 0.5 * (tr - disc));
        enginetest::testCameraLookAt(view, Vec3(kSphereCentre.x, kSphereCentre.y, kSphereCentre.z + 2.0f), kSphereCentre);
    } else {
        const float w = 2.0f * kHalfX + 2.0f * kThick, d = 2.0f * kHalfZ + 2.0f * kThick;
        emitters.push_back(slab(s, Vec3(0.0f, -0.5f * kThick, 0.0f), Vec3(w, kThick, d), kE));
        if (scene != "scroll")   // (the scroll part: the roof OPEN to the sun and the sky - gi.scroll_slab's open scene)
            emitters.push_back(slab(s, Vec3(0.0f, kHeight + 0.5f * kThick, 0.0f), Vec3(w, kThick, d), kE));
        emitters.push_back(slab(s, Vec3(-kHalfX - 0.5f * kThick, 0.5f * kHeight, 0.0f), Vec3(kThick, kHeight + 2.0f * kThick, d), kE));
        emitters.push_back(slab(s, Vec3(kHalfX + 0.5f * kThick, 0.5f * kHeight, 0.0f), Vec3(kThick, kHeight + 2.0f * kThick, d), kE));
        emitters.push_back(slab(s, Vec3(0.0f, 0.5f * kHeight, -kHalfZ - 0.5f * kThick), Vec3(w, kHeight + 2.0f * kThick, kThick), kE));
        if (scene == "lamp" || room) {
            // THE LAMP PART's OPENING (photon-room's ingredient, gi.hold_after_stop (f)): a 1 x 1 m window in the +z wall,
            // its centre at 2 m, to a sky of radiance 0.5 (the four slabs around it)
            const float zc = kHalfZ + 0.5f * kThick, hw = 0.5f * w;
            emitters.push_back(slab(s, Vec3(-0.5f * (hw + 0.5f), 0.5f * kHeight, zc), Vec3(hw - 0.5f, kHeight + 2.0f * kThick, kThick), kE));
            emitters.push_back(slab(s, Vec3(0.5f * (hw + 0.5f), 0.5f * kHeight, zc), Vec3(hw - 0.5f, kHeight + 2.0f * kThick, kThick), kE));
            emitters.push_back(slab(s, Vec3(0.0f, 0.5f * (1.5f - kThick), zc), Vec3(1.0f, 1.5f + kThick, kThick), kE));
            emitters.push_back(slab(s, Vec3(0.0f, 0.5f * (2.5f + kHeight + kThick), zc), Vec3(1.0f, kHeight + kThick - 2.5f, kThick), kE));
            float sky[27] = {};
            sky[0] = sky[1] = sky[2] = 0.5f;
            s->setAmbientSh(sky);
        } else {
            emitters.push_back(slab(s, Vec3(0.0f, 0.5f * kHeight, kHalfZ + 0.5f * kThick), Vec3(w, kHeight + 2.0f * kThick, kThick), kE));
        }
        target[0] = kE / (1.0 - a1);
        if (scene == "scroll") {
            // THE SCROLL'S PANEL (gi.scroll_slab G's): an emissive panel in the slab cascade 0's scroll brings in
            const NodeId glow = s->createNode();
            PbrParams gp;
            gp.albedo = Colour(0.8f, 0.8f, 0.8f);
            gp.roughness = 0.35f;
            gp.emissive = Colour(2.5f, 1.2f, 0.4f);
            s->attachMesh(glow, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(gp));
            enginetest::setNodePosition(s, glow, Vec3(3.5f, 1.6f, -1.0f));
            enginetest::setNodeScale(s, glow, Vec3(0.1f, 1.2f, 1.6f));
            // ...THE SUN AND THE SKY through the open roof (gi.scroll_slab's: a sun of 5, an ambient of 0.05)
            enginetest::addDirectionalLight(s, Vec3(0.3f, -1.0f, -0.5f), 5.0f);
            s->setAmbient(Colour(0.05f, 0.05f, 0.05f), Colour(0.05f, 0.05f, 0.05f));
        }
        std::printf("   the box: a' %.5f (diffuse %.5f + specular %.5f, by integration); L* = E / (1 - a') = %.6f\n", a1,
                    kRho * enginetest::disneyDiffuseAlbedoHemi(1.0), a1 - kRho * enginetest::disneyDiffuseAlbedoHemi(1.0),
                    target[0]);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 0.0f), Vec3(0.0f, 2.0f, -3.0f));
    }
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Epic;
    if (scene == "scroll") {
        // THE SCROLL ACROSS A TWO-CASCADE SEAM (gi.scroll_slab G's chain): cascade 0 of 5 m and cascade 1 of 20 m, 64 cells
        // each, at High - the 8 m room is wider than cascade 0, so its emitting walls cross cascade 0's faces and a
        // scroll's slab lights them
        gi.quality = GiQuality::High;
        gi.cascadeCount = 2;
        gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 5.0f, 64, 0.0f };
        gi.cascadeSet[1] = GiParams::GiCascadeDesc{ 20.0f, 64, 0.0f };
    }
    gi.ddgi = (scene == "lamp" || room) ? GiToggle::On : GiToggle::Off;
    // (the room part: the cards on - photon-room's pipeline; the gather is on at every ray chain; a third argument
    // `nocards` takes them off - the ingredient's own arm)
    gi.cards = room && !(argc > 3 && std::string(argv[3]) == "nocards");
    gi.bounces = true;
    gi.updateBudget = 0;
    if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: the GI\n"); return 1; }
    e->waitForAsyncShaders();
    const unsigned lwf = giQualityFacts(gi.quality).lightWorkFrames;
    const int window = int(2u * lwf + 2u);
    const auto owes = [&]() { const GiStatus g = s->published().gi; return g.cascades.empty() || g.chainOwed || g.feedbackOwed; };
    const auto counters = [&](unsigned long long &resumes, unsigned long long &passes) {
        resumes = passes = 0;
        for (const auto &c : s->published().gi.cascades) { resumes += c.feedbackResumes; passes += c.feedbackPasses; }
    };
    // THE REST and (iii) after it: frames until nothing is owed, then the window - the half store compared WORD FOR WORD
    // (Scene::giHalfStoreWords: its RGBA16F words as stored, exact, no decode), the moved words' worst |delta| in store
    // steps printed beside the count
    int rests = 0;
    const auto restLaw = [&](const char *what, int cap) {
        ++rests;
        run(2);   // (a change reaches the published status a frame late: never read the predicate in the change's frame)
        int f = 2;
        for (; f < cap && owes(); ++f) run(1);
        char m[200];
        std::snprintf(m, sizeof m, "%s: the chain and the feedback come to rest (cap %d frames)", what, cap);
        CHECK(!owes(), m);
        std::vector<std::uint16_t> w0, w1;
        const bool read0 = s->giHalfStoreWords(0, w0);
        unsigned long long res0, pas0;
        counters(res0, pas0);
        int owedFrames = 0, firstOwed = -1;
        for (int k = 0; k < window; ++k) {
            run(1);
            if (owes()) { ++owedFrames; if (firstOwed < 0) firstOwed = k + 1; }
        }
        const bool read1 = s->giHalfStoreWords(0, w1);
        unsigned long long res1, pas1;
        counters(res1, pas1);
        std::snprintf(m, sizeof m, "%s: the half store read word for word, both ends of the window", what);
        CHECK(read0 && read1 && !w0.empty() && w0.size() == w1.size(), m);
        size_t moved = 0;
        double worst = 0.0;
        for (size_t i = 0; i < w0.size() && i < w1.size(); ++i)
            if (w0[i] != w1[i]) {
                ++moved;
                const double a = halfValue(w0[i]), b = halfValue(w1[i]);
                worst = std::max(worst, std::fabs(b - a) / storeStep(std::max(std::fabs(a), std::fabs(b))));
            }
        std::printf("   %s: rest after %d frames; (iii) over the %d-frame window: the half store moved in %zu of %zu words "
                    "(the worst %.2f steps), %llu resumes, %llu passes, owed on %d frames (the first %d)\n", what, f, window,
                    moved, w0.size(), worst, res1 - res0, pas1 - pas0, owedFrames, firstOwed);
        const std::string law = part + ".rest" + std::to_string(rests);
        enginetest::lawLine((law + "_moved_words").c_str(), double(moved), "words", "==", 0.0);
        enginetest::lawLine((law + "_resumes").c_str(), double(res1 - res0), "resumes", "==", 0.0);
        std::snprintf(m, sizeof m, "(iii) %s: A REST IS A REST - nothing written, resumed, passed or owed for the window", what);
        CHECK(moved == 0 && res1 == res0 && pas1 == pas0 && owedFrames == 0, m);
    };
    // (ii') frame by frame from a step to the next rest, PER VOXEL: after a step UP no voxel moves down and none passes
    // its new fixed point (the value it rests at); after a step DOWN the mirror - each within one store step (the write
    // hold's quantum). `dir` +1 up, -1 down, 0 no monotone law (the scroll: light enters and leaves). The lamp's up step
    // also writes the picture's per-frame change for the wrapper's rise law.
    const auto monotoneLaw = [&](const char *what, int dir, bool picture) {
        std::vector<std::vector<float>> hist;
        std::vector<double> pic;
        Image last;
        if (picture) enginetest::readLinear(view, last);
        hist.push_back(read(s, spheres, true).L);
        int quiet = 0, k = 0;
        bool rested = false;
        for (; k < 900; ++k) {
            run(1);
            const Reading cur = read(s, spheres, true);
            if (!cur.ok || cur.L.size() != hist.back().size()) { hist.clear(); hist.push_back(cur.L); continue; }
            const bool same = std::memcmp(cur.L.data(), hist.back().data(), cur.L.size() * sizeof(float)) == 0;
            hist.push_back(cur.L);
            if (picture) {
                Image img;
                if (enginetest::readLinear(view, img) && img.rgba.size() == last.rgba.size()) {
                    double m = 0.0;
                    for (size_t i = 0; i < img.rgba.size(); i += 4)
                        for (int c = 0; c < 3; ++c) m += std::abs(int(img.rgba[i + c]) - int(last.rgba[i + c]));
                    pic.push_back(m / double(img.rgba.size() / 4 * 3));
                    last = img;
                    // (what wrote that frame: the field's integration, its owed restarts, the store's active bricks)
                    const GiStatus g = s->published().gi;
                    unsigned active = 0;
                    for (const auto &c : g.cascades) active += c.feedbackActive;
                    std::printf("   PIC frame %d change %.4f field dispatched %u owed restarts %u working %d; store active bricks %u\n",
                                k + 1, pic.back(), g.ifdDispatchedLastFrame, g.ifdOwedRestarts, g.ifdWorking ? 1 : 0, active);
                }
            }
            quiet = owes() ? 0 : quiet + 1;
            if (quiet >= 2 && same) { rested = true; break; }
        }
        std::printf("   (ii') %s: %d frames to the next rest (%s); %zu voxels over %zu frames\n", what, k + 1,
                    rested ? "rested" : "NOT rested in 900", hist.back().size(), hist.size());
        char m[200];
        std::snprintf(m, sizeof m, "(ii') %s: the step comes to rest (cap 900 frames)", what);
        CHECK(rested, m);
        if (dir != 0 && hist.size() >= 2) {
            const std::vector<float> &fin = hist.back();
            size_t against = 0, past = 0;
            double worstAgainst = 0.0, worstPast = 0.0;
            for (size_t t = 1; t < hist.size(); ++t)
                for (size_t i = 0; i < fin.size(); ++i) {
                    const double now = hist[t][i], was = hist[t - 1][i];
                    const double st = storeStep(std::max(std::fabs(now), std::fabs(was)));
                    const double moved = dir * (now - was);   // > 0: with the step
                    if (moved < -st) { ++against; worstAgainst = std::max(worstAgainst, -moved / st); }
                    const double beyond = dir * (now - double(fin[i]));
                    const double sf = storeStep(std::fabs(double(fin[i])));
                    if (beyond > sf) { ++past; worstPast = std::max(worstPast, beyond / sf); }
                }
            std::printf("   (ii') %s: voxel moves AGAINST the step past a store step: %zu (the worst %.2f steps); values PAST the "
                        "new fixed point past a step: %zu (the worst %.2f steps)\n", what, against, worstAgainst, past, worstPast);
            enginetest::lawLine((part + "_against").c_str(), double(against), "moves", "==", 0.0);
            enginetest::lawLine((part + "_past").c_str(), double(past), "values", "==", 0.0);
            std::snprintf(m, sizeof m, "(ii') %s: MONOTONE TO THE NEW FIXED POINT - no voxel against the step, none past it", what);
            CHECK(against == 0 && past == 0, m);
        }
        if (picture) {
            const std::string path = work + "/lamp_picture.txt";
            if (FILE *fp = std::fopen(path.c_str(), "w")) {
                for (double v : pic) std::fprintf(fp, "%.6f\n", v);
                std::fclose(fp);
            }
            std::printf("   (ii) %s: the picture's per-frame mean change (%zu frames) -> %s\n", what, pic.size(), path.c_str());
        }
    };
    // (i): the fixed point against the closed form, and the deficit split by a 64-pass Jacobi
    const auto fixedPointLaw = [&](const Reading &fb, const double scale) {
        e->setArm("gi.bounceJacobi", 1.0);
        run(64);
        const Reading j64 = read(s, spheres);
        e->setArm("gi.bounceJacobi", 0.0);
        run(2);
        size_t over = 0;
        double worst = 0.0;
        double fbMean[2] = { 0, 0 }, jMean[2] = { 0, 0 };
        size_t n[2] = { 0, 0 };
        for (size_t i = 0; i < fb.L.size(); ++i) {
            const int k = fb.surface[i];
            const double T = scale * target[k];
            const double off = std::fabs(double(fb.L[i]) - T);
            if (off > 0.01 * T + storeStep(T)) ++over;
            worst = std::max(worst, off / (0.01 * T));
            fbMean[k] += fb.L[i];
            ++n[k];
            if (j64.ok && i < j64.L.size()) jMean[k] += j64.L[i];
        }
        for (int k = 0; k < (spheres ? 2 : 1); ++k) {
            if (!n[k]) continue;
            fbMean[k] /= double(n[k]);
            jMean[k] /= double(n[k]);
            const double T = scale * target[k];
            std::printf("   (i) %s: %zu voxels; the closed form %.6f; the feedback's mean %+.3f %%, the Jacobi-64's %+.3f %% "
                        "(the gather's own reading), the feedback against the Jacobi %+.3f %% (the iteration's stop)\n",
                        spheres ? (k ? "the outer sphere" : "the inner sphere") : "the box", n[k], T,
                        100.0 * (fbMean[k] / T - 1.0), 100.0 * (jMean[k] / T - 1.0), 100.0 * (fbMean[k] / jMean[k] - 1.0));
        }
        std::printf("   (i) the worst voxel %.2f codes off the closed form; %zu of %zu past a code and a step\n", worst, over, fb.L.size());
        enginetest::lawLine((part + ".fixed_point_worst").c_str(), worst, "codes");
        CHECK(over == 0, "(i) THE FIXED POINT IS THE CLOSED FORM: every surface voxel within a code and a step of it");
    };

    const bool stepped = scene == "box" || scene == "pair" || scene == "lamp";
    if ((stepped && step != "up" && step != "down") || (room && !step.empty() && step != "up") ||
        (scene == "scroll" && !step.empty()) || (!stepped && !room && scene != "scroll")) {
        std::printf("FAIL: the part %s (box.up|box.down|pair.up|pair.down|lamp.up|lamp.down|room|scroll)\n", part.c_str());
        return 1;
    }
    // (the lamp and room parts: an interior lamp of 8 at the room's centre from the start - the first rest is the lamp's)
    const bool lit = scene == "lamp" || room;
    NodeId lamp = 0;
    LightDesc ld;
    if (lit) {
        lamp = s->createNode();
        ld.type = LightType::Point;
        ld.colour = Colour(1.0f, 1.0f, 1.0f);
        ld.intensity = 8.0f;
        ld.range = 12.0f;
        s->setNodeTransform(lamp, Vec3(0.0f, 2.0f, 0.0f), Quat(), Vec3(1, 1, 1));
        s->setLight(lamp, ld);
    }
    restLaw(lit ? "the lamp at rest (the first rest)" : "the first rest", 900);
    if (scene == "box" || scene == "pair") {
        // the steps: the emission doubled (UP); for the DOWN row, doubled to its rest and then halved back
        if (step == "up") {
            const Reading r = read(s, spheres);
            if (!r.ok) { std::printf("FAIL: the surfaces read back\n"); return 1; }
            fixedPointLaw(r, 1.0);
        }
        for (NodeId n : emitters) s->setNodeMaterial(n, material(s, kRho, 2.0f * kE));
        if (step == "up") {
            monotoneLaw("the emission doubled (up)", +1, false);
            restLaw("after the step up", 900);
        } else {
            restLaw("the emission doubled, at its rest", 900);
            for (NodeId n : emitters) s->setNodeMaterial(n, material(s, kRho, kE));
            monotoneLaw("the emission halved back (down)", -1, false);
            restLaw("after the step down", 900);
        }
    } else if (scene == "scroll") {
        // A STEP THAT SCROLLS CASCADE 0 ALONE (gi.scroll_slab G's): a placement pin holds cascade 1 at its own camera and
        // places cascade 0 for a camera 1.05 of its steps along x - its commit over the bricks it changed
        const GiStatus pre = s->published().gi;
        const GiStatus::CascadeStatus &c0 = pre.cascades[0];
        const Vec3 target(c0.centre.x + 1.05f * c0.step, 2.0f, 0.0f);
        GiPlacementPin pin;
        pin.on = true;
        pin.cascadeBuiltCam.push_back(target);
        for (size_t k = 1; k < pre.cascades.size(); ++k) pin.cascadeBuiltCam.push_back(pre.cascades[k].builtCam);
        s->setGiPlacementPin(pin);
        const unsigned long long scrolls0 = c0.scrolls;
        enginetest::testCameraLookAt(view, target, Vec3(target.x, 2.0f, -3.0f));
        monotoneLaw("the scroll", 0, false);
        const unsigned long long scrolls1 = s->published().gi.cascades[0].scrolls;
        std::printf("   the scroll part: cascade 0 (%.2f m cells, step %.2f m) committed %llu scrolls\n", double(c0.cell),
                    double(c0.step), scrolls1 - scrolls0);
        CHECK(scrolls1 > scrolls0, "the camera's step scrolled the chain (the part's premise)");
        restLaw("after the scroll", 900);
    } else {
        // the lamp at rest (the first rest); then stepped 8 -> 12 (gi.hold_after_stop (f)'s step); for the DOWN row, to
        // its rest at 12 and back to 8
        ld.intensity = 12.0f;
        s->setLight(lamp, ld);
        if (step != "down") {
            monotoneLaw("the lamp stepped 8 -> 12 (up)", +1, true);
            restLaw("after the lamp step up", 900);
        } else {
            restLaw("the lamp at 12, at its rest", 900);
            ld.intensity = 8.0f;
            s->setLight(lamp, ld);
            monotoneLaw("the lamp stepped 12 -> 8 (down)", -1, false);
            restLaw("after the lamp step down", 900);
        }
    }
    std::printf("   the row rendered %d frames\n", rendered);
    std::printf("gi.feedback_%s: %s\n", part.c_str(), failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
