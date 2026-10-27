// gi.bounce_series (PHOTON-7 stage 4; SPECS/PHOTON_7_SPEC.md stage 4) — THE BOUNCES CONVERGE BY FEEDBACK TO THE SERIES'
// LIMIT, read in the voxel STORE (Scene::giVoxelLight, the total alone - no gather, no field, no picture).
//
// A SEALED 8 x 4 x 8 m room (0.3 m slabs, every seam overlapped) whose inner walls EMIT a uniform radiance E and
// reflect with albedo rho = 0.5 at roughness 1 under a uniform sky of radiance S (setAmbientSh's constant band - what
// an escaping cone sees; no lamp): a wall voxel's surface radiance is
//     L = E + a' ( (1 - e) L + e S )
// (a' the surface's re-emission of what it gathers - rho x DA_hemi(1) = 0.351 for the diffuse lobe - and e the share of
// the gather's cones that ESCAPE the sealed room: the cones' own leak), so  L = ( E + a' e S ) / ( 1 - a' (1 - e) ).
// THREE ARMS a tier, each a fresh room run to the feedback's rest:
//   MAIN (E 1, S 0) - the series' limit E / (1 - a'(1 - e));
//   THE SKY-LEAK ARM (E 0, S 1) - the escape share it MEASURES: L_sky = a' e S / (1 - a'(1 - e)), so
//       e = L_sky (1 - a') / (a' (S - L_sky)), per cascade (printed);
//   THE OPERATOR'S OWN ARM (E 1, S = S* = E / (1 - a')) - the sky held at the room's answer, so an escaping cone sees what
//       the walls would: the store converges to S* whatever e is.
// THE LAWS, per tier (Low, Medium, High, Epic), each on cascade 0's inner-wall voxels (covered, a cell inside the
// room's faces, a metre from its edges), surface radiance = the store's total over its coverage:
//   (1) FEEDBACK = A 64-PASS JACOBI WITHIN ONE CODE (main arm): the feedback run to its rest (no brick active) against 64
//       Jacobi passes of cascade 0 from its direct term (the measuring door gi.bounceJacobi set to 1, 64 frames: the outer
//       cascades hold their rest, so both answers are fixed points of one operator), every voxel within 1 % of the
//       Jacobi's radiance - the stop law's own bound (a brick stops once the tail of its series is under a code); the
//       door is seen to run (every brick of cascade 0 a frame, the outer cascades held);
//   (2) THE SERIES' LIMIT WITH NO ESCAPE (PHOTON-7 10-3 R12: physics, not a picked 3 % around a measured e): a sealed
//       room escapes nothing, so the reference is E / (1 - a'), a' the material's WHOLE lobe by direct integration (the
//       diffuse hemispherical albedo x rho + the dielectric's 4 % specular layer, F0 A-bar + B-bar - the store re-emits
//       both) - the Jacobi within ONE STORE STEP of it (2^(floor(log2 L) - 10)), the feedback within that and its stop
//       law's code. The voxels are read on their ROOM-FACING side (the preflight's W3: a two-sided voxel's light is its
//       two sides' mean, which averaged the void-lit outer face in - the old "escape": now c0 e = 0.000 exactly).
//       DECLARED RED (2026-10-27): the store holds -0.60 % of the limit at High (a' 0.36781 against 0.37161) - the
//       diffuse fit's own -0.5 % at r = 1 (jahDiffuseAlbedoHemi 0.7018 against the integral 0.7052) and a specular
//       share of 0.0169 against the integral's 0.0190 (cause unmeasured: the lead's ruling owed);
//   (2b) THE OPERATOR'S ARM AT THE SAME LIMIT (E 1, S = S*): with no escape the sky is never seen, so its store holds
//       (2)'s limit whatever S is, within a step and the stop law's code;
//   (2c) THE INJECTION ALONE (R12): the bounces off, the inner walls hold E within a store step (measured 1.000000);
//   (3) THE FEEDBACK STOPS: the chain owes nothing at its rest (its brick-passes printed; the old "fewer than the
//       Jacobi's 64 a brick" could not fail - 1.2-2.1 a brick - and is DELETED: the stop and resume rules are (1)'s and
//       (4)'s).
//   (4) A STOPPED BRICK BESIDE A MOVING ONE RESUMES (stage 4's read R3; the main arm's room after its Jacobi and the
//       feedback's rest; High and Epic - see the arm): a lamp
//       of 200 switched on 0.5 m over the floor's centre at the room's rest (range 3 m: only its sphere's bricks are
//       activated, every other brick of the room stopped) moves the light of the walls outside its range by more than a
//       code (counted: the arm fails when nothing outside moves), which can only reach the feedback through the stopped
//       bricks' resume (what their gather sees move, accumulated); at the feedback's rest every inner-wall voxel within
//       one code of a 64-pass Jacobi of the same state - the stop law's bound with the resume's own error in it. RED on
//       the resume's old weight (the centres' distance, one face: 316 voxels over a code, the worst 2.36, no brick of
//       cascade 0 resumed - High). (The coupling is the store's: at 50 the walls outside moved 0.72 codes at most.)
//   (6) A SUSTAINED CHANGE NEVER STARVES CASCADE 0 (PHOTON-7 stage 5's read A2: the outermost-first spend of the frame's
//       relight count gave the outer cascades all of it while every frame's change restarted the rounds): through (5)'s
//       fade - the lamp changing every frame, its sphere meeting every cascade - cascade 0's relight goes round its owed
//       box at least every 2 x lightWorkFrames + 2 frames (its FLOOR share alone pays its volume in 2 x lightWorkFrames;
//       a slab's rounding and the round's own frame the +2);
//   (5) THE SLOW FADE ENDS WHERE THE STEP ENDS (PHOTON-7 stage 4's fix, the lead's bar; High and Epic, after the resume
//       arm): the lamp from 400 to 200 in one frame, and - from 400 again - FADED to 200 linearly over kFadeFrames, slow
//       enough that every inner-wall voxel, inside the range and beyond it, moves under ONE STEP of the half-float store
//       a frame (2^-10 of its value; printed: the end states' difference over the frames); at each arm's rest every
//       inner-wall voxel beyond the lamp's range box within one code of a 64-pass Jacobi of the end state. RED where a
//       slow change is dropped (a move under a step recorded as none never resumes the bricks beyond the range, which
//       stay at the old light).
//   (7) A CARRIED BRICK'S INJECTION REACHES ITS STOPPED NEIGHBOURS (PHOTON-7 stage 5's read A2 (i); High and Epic, after
//       (5)): the lamp FADED from 200 to 400 over kCarryFadeFrames - a sustained change, the floor on - with the test door
//       gi.feedbackCarriedRoute set: a brick its injection moved in a frame is CARRIED that frame (never passed), and
//       the stopped bricks accumulate the selection's carried section alone, so a carried brick's delta is the ONLY
//       route to the stopped bricks beyond the range (a later pass's move is measured from the total the delta
//       already wrote: it never sees the delta). At the rest, every inner-wall voxel beyond the range box within one
//       code of a 64-pass Jacobi of the end state; the arm can fail (the voxels beyond move over a code). RED with the
//       carried section unread (High: 296 voxels over a code, the worst 2.25; the door's other half alone,
//       the passed bricks' moves read, is green either way: under a 120-frame fade the bounces' moves alone resume them).
//   (8) AT THE RAY TIER (PHOTON-7 stage 10, 10-2b; the part `rays`, High and Epic): the voxel sides are gathered by TLAS
//       rays (rq_voxel_sides.comp) - the sky-leak arm with the rays on: the sides' gather ran (its dispatches and sides
//       read back, GiStatus::voxelSides), and the inner-wall voxels of cascades 0 and 1 read under 1 % of the sky (L9's
//       bar; e printed per cascade - no TLAS ray inside the watertight room escapes, and the residue is the hit read's
//       FOOTPRINT: a long ray reads a coarse level, whose texel holds a wall's two sides as one; the coarser cascades'
//       inner-wall cells hold both faces of the 0.3 m walls themselves and are printed only); and the main arm's
//       feedback against its 64-pass Jacobi within a code (law (1) at the ray tier: the TLAS gather converges).
// (THE LEAK ITSELF is printed, not barred: the cones at coarse levels read a texel the wall only partly covers - a
// march finding, VOXEL-LEAK-1. A pass-1 reading is never the operator: the outer cascades run first in a frame, so an
// inner cascade's first pass already reads an outer one's bounce.)
// ONE TIER A ROW (gi.bounce_series.<tier>, the argument; none = every tier). A fixture (the engine alone, one small
// offscreen view; rays, the gather, the field and the cards off). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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
const float kE = 1.0f;       // the walls' emission (radiance), the main and the operator's arms
const float kRho = 0.5f;     // their albedo
const float kHalfX = 4.0f, kHalfZ = 4.0f, kHeight = 4.0f, kThick = 0.3f;

const double kADiffuse = kRho * 0.701832;   // a' = rho x DA_hemi(1) (JahDiffuseAlbedo_piece_all.any)
const int kCarryFadeFrames = 120;   // (7): a sustained change, the injected bricks carried in their frames
const int kFadeFrames = 1200;   // (5): every inner-wall voxel under a step of the store a frame (printed)

NodeId slab(Scene *s, const Vec3 &centre, const Vec3 &size, float emission)
{
    PbrParams p;
    p.albedo = Colour(kRho, kRho, kRho);
    p.roughness = 1.0f;
    p.emissive = Colour(emission, emission, emission);
    const NodeId n = s->createNode();
    s->attachMesh(n, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(p));
    enginetest::setNodePosition(s, n, centre);
    enginetest::setNodeScale(s, n, size);
    return n;
}

struct Reading {
    bool ok = false;
    size_t voxels = 0;
    double mean = 0.0;
    std::vector<float> L;   // the selected voxels' surface radiance (the brightest channel), in selection order
};

/// A cascade's inner-wall voxels: covered (c > 0.5), centred within one of its cells inside a face of the room, a metre
/// from its edges; L = the store's total / its coverage (the total is premultiplied by c).
Reading read(Scene *s, int cascade = 0)
{
    Reading r;
    GiVoxelVolume v;
    if (!s->giVoxelLight(cascade, v) || !v.available || v.light.empty()) return r;
    const float cell = v.cell[0];
    for (int z = 0; z < v.depth; ++z)
        for (int y = 0; y < v.height; ++y)
            for (int x = 0; x < v.width; ++x) {
                const float p[3] = { v.origin[0] + cell * (x + 0.5f), v.origin[1] + cell * (y + 0.5f),
                                     v.origin[2] + cell * (z + 0.5f) };
                // inside the room's inner box grown by a cell, and not inside it shrunk by a cell: the inner skin
                const bool inGrown = std::fabs(p[0]) < kHalfX + cell && std::fabs(p[2]) < kHalfZ + cell &&
                                     p[1] > -cell && p[1] < kHeight + cell;
                const bool inShrunk = std::fabs(p[0]) < kHalfX - cell && std::fabs(p[2]) < kHalfZ - cell &&
                                      p[1] > cell && p[1] < kHeight - cell;
                if (!inGrown || inShrunk) continue;
                // a metre from every edge of the face it is on (the corners' gather sees another mix)
                int near = 0;
                near += std::fabs(p[0]) > kHalfX - 1.0f ? 1 : 0;
                near += std::fabs(p[2]) > kHalfZ - 1.0f ? 1 : 0;
                near += (p[1] < 1.0f || p[1] > kHeight - 1.0f) ? 1 : 0;
                if (near > 1) continue;
                const size_t i = ((size_t(z) * v.height + y) * v.width + x) * 4u;
                const float c = v.light[i + 3];
                if (!(c > 0.5f)) continue;
                // THE ROOM-FACING SIDE (PHOTON-7 10-3 R12, the preflight's W3): a two-sided voxel's `light` is the MEAN
                // of its two sides, so a wall cell read whole averaged the outer face (lit by the void) into the inner
                // one - the "escape" the old law measured. The side whose face looks INTO the room is read: the store's
                // front where the voxeliser's canonical normal points inward, its back where it points out.
                float in[3] = { 0.0f, 0.0f, 0.0f };   // the inward normal of the face the cell lies on
                const float dx = kHalfX - std::fabs(p[0]), dz = kHalfZ - std::fabs(p[2]), dy0 = p[1],
                            dy1 = kHeight - p[1];
                const float dmin = std::min(std::min(std::fabs(dx), std::fabs(dz)), std::min(std::fabs(dy0), std::fabs(dy1)));
                if (dmin == std::fabs(dy0)) in[1] = 1.0f;
                else if (dmin == std::fabs(dy1)) in[1] = -1.0f;
                else if (dmin == std::fabs(dx)) in[0] = p[0] > 0.0f ? -1.0f : 1.0f;
                else in[2] = p[2] > 0.0f ? -1.0f : 1.0f;
                const float *side = v.light.data() + i;
                if (v.normal.size() == v.light.size() && v.lightFront.size() == v.light.size() &&
                    v.lightBack.size() == v.light.size() && v.normal[i + 3] > 0.5f) {
                    const float n[3] = { 2.0f * v.normal[i] - 1.0f, 2.0f * v.normal[i + 1] - 1.0f,
                                         2.0f * v.normal[i + 2] - 1.0f };
                    const bool frontIn = n[0] * in[0] + n[1] * in[1] + n[2] * in[2] >= 0.0f;
                    side = (frontIn ? v.lightFront.data() : v.lightBack.data()) + i;
                }
                const float L = std::max(std::max(side[0], side[1]), side[2]) / c;
                r.L.push_back(L);
                r.mean += L;
            }
    r.voxels = r.L.size();
    r.ok = r.voxels > 0;
    if (r.ok) r.mean /= double(r.voxels);
    return r;
}
}   // namespace

int main(int argc, char **argv)
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-bounce-series-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(false);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    const GiQuality tiers[4] = { GiQuality::Low, GiQuality::Medium, GiQuality::High, GiQuality::Epic };
    const char *names[4] = { "low", "medium", "high", "epic" };
    // ONE ROOM, ONE ARM: built, its chain run to the feedback's rest (the chain owes nothing for 8 frames in a row, cap
    // 900), its cascades read; the room stays up for the caller and is torn down by `drop`.
    struct Arm {
        View *view = nullptr;
        Scene *scene = nullptr;
        int restFrames = -1;
        GiStatus status;
        std::vector<Reading> cascades;
    };
    // (the store's door downloads every volume of a cascade: an arm reads only the cascades its law needs)
    const auto arm = [&](GiQuality tier, float E, float S, bool everyCascade, bool bounces = true) {
        Arm a;
        a.view = e->createOffscreenView("bounce_series", 64, 64, Colour(0, 0, 0), jahshaka::engine::OffscreenContract::Live);
        a.scene = e->createScene("bounce_series");
        if (!a.view || !a.scene) return a;
        Scene *s = a.scene;
        a.view->setScene(s);
        float sh[27] = {};
        sh[0] = sh[1] = sh[2] = S;   // a uniform environment of radiance S (Engine.h, setAmbientSh)
        s->setAmbientSh(sh);
        const float w = 2.0f * kHalfX + 2.0f * kThick, d = 2.0f * kHalfZ + 2.0f * kThick;
        slab(s, Vec3(0.0f, -0.5f * kThick, 0.0f), Vec3(w, kThick, d), E);                      // the floor
        slab(s, Vec3(0.0f, kHeight + 0.5f * kThick, 0.0f), Vec3(w, kThick, d), E);             // the ceiling
        slab(s, Vec3(-kHalfX - 0.5f * kThick, 0.5f * kHeight, 0.0f), Vec3(kThick, kHeight + 2.0f * kThick, d), E);
        slab(s, Vec3(kHalfX + 0.5f * kThick, 0.5f * kHeight, 0.0f), Vec3(kThick, kHeight + 2.0f * kThick, d), E);
        slab(s, Vec3(0.0f, 0.5f * kHeight, -kHalfZ - 0.5f * kThick), Vec3(w, kHeight + 2.0f * kThick, kThick), E);
        slab(s, Vec3(0.0f, 0.5f * kHeight, kHalfZ + 0.5f * kThick), Vec3(w, kHeight + 2.0f * kThick, kThick), E);
        enginetest::testCameraLookAt(a.view, Vec3(0.0f, 2.0f, 0.0f), Vec3(0.0f, 2.0f, -3.0f));
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = tier;
        gi.ddgi = GiToggle::Off;
        gi.cards = false;
        gi.bounces = bounces;
        gi.updateBudget = 0;
        if (!s->setGlobalIllumination(gi)) return a;
        e->waitForAsyncShaders();
        int f = 0, quiet = 0;
        for (; f < 900 && quiet < 8; ++f) {
            run(1);
            const GiStatus g = s->published().gi;
            quiet = (!g.cascades.empty() && !g.chainOwed && !g.feedbackOwed) ? quiet + 1 : 0;
        }
        a.restFrames = quiet >= 8 ? f - quiet : -1;
        a.status = s->published().gi;
        for (size_t c = 0; c < (everyCascade ? a.status.cascades.size() : size_t(1)); ++c) a.cascades.push_back(read(s, int(c)));
        return a;
    };
    const auto drop = [&](Arm &a) {
        if (a.scene) {
            GiParams off;
            off.mode = GiMode::Off;
            a.scene->setGlobalIllumination(off);
            run(2);
            e->destroyScene(a.scene);
        }
        if (a.view) e->destroyView(a.view);
        run(2);
        a = Arm();
    };
    // e FROM THE SKY-LEAK ARM: L_sky = a' e S / (1 - a'(1 - e))  =>  e = L_sky (1 - a') / (a' (S - L_sky)).
    const auto escapeOf = [](double lSky, double S) { return lSky * (1.0 - kADiffuse) / (kADiffuse * (S - lSky)); };
    const double sStar = kE / (1.0 - kADiffuse);
    // ONE TIER A ROW (gi.bounce_series.<tier>: a fixture's 10 s budget, source.row_budget), named by the argument;
    // none = every tier. AND AT HIGH AND EPIC ONE PART A ROW (the second argument; PHOTON-7 stage 7: past the 10 s budget
    // whole): `series` - the sky-leak, main and operator arms and the resume arm, laws (1)-(4); `fade` - the main room to
    // its rest and the resume arm's state rebuilt (unjudged: the lamp arms start from it), then (5)'s FADE arm and (6),
    // which rides on the fade; `lamp` - the same state, then (5)'s STEP arm and (7), the carried route from the step's end
    // (the lamp at 200 at rest - the world the fade also ends in, within (5)'s code). None = every part.
    const std::string part = argc > 2 ? argv[2] : "";
    const bool doSeries = part.empty() || part == "series";
    const bool doFade = part.empty() || part == "fade";
    const bool doStep = part.empty() || part == "lamp";
    const bool doLamp = doFade || doStep;
    const bool doRays = part == "rays";
    for (int t = 0; t < 4; ++t) {
        if (argc > 1 && std::string(argv[1]) != names[t]) continue;
        std::printf("\n== %s ==\n", names[t]);
        const auto tierT0 = std::chrono::steady_clock::now();
        const int tierF0 = frames;
        // (8) THE RAY TIER (the part `rays`): the sky-leak arm and the main arm's Jacobi with the rays on
        if (doRays) {
            e->setRayTracing(true);
            Arm sky = arm(tiers[t], 0.0f, 1.0f, true);
            const GiStatus::VoxelSideCounts vs = sky.status.voxelSides;
            size_t inner = 0, lit = 0;
            double peak = 0.0, peak01 = 0.0;
            std::printf("   (8) the ray tier's sky-leak arm (E 0, S 1) at rest in %d frames:", sky.restFrames);
            for (size_t c = 0; c < sky.cascades.size(); ++c) {
                const Reading &r = sky.cascades[c];
                double cp = 0.0;
                for (float L : r.L) cp = std::max(cp, double(L));
                if (c < 2u) {
                    inner += r.voxels;
                    for (float L : r.L) lit += L != 0.0f ? 1u : 0u;
                    peak01 = std::max(peak01, cp);
                }
                peak = std::max(peak, cp);
                std::printf("  c%zu %zu voxels, e %.5f, the brightest %.6g", c, r.voxels, r.ok ? escapeOf(r.mean, 1.0) : -1.0,
                            cp);
            }
            std::printf("\n   (8) the sides' TLAS gather: %llu dispatches, %llu rays - %llu cards, %llu cascades, %llu back "
                        "faces, %llu no sample, %llu sky; %llu sides (%llu with no sample), %llu waits\n",
                        vs.dispatches, vs.rays, vs.card, vs.voxel, vs.backFaces, vs.noSample, vs.sky, vs.sides,
                        vs.emptySides, vs.waits);
            CHECK(sky.restFrames >= 0 && vs.dispatches > 0ull && vs.sides > 0ull && vs.waits == 0ull,
                  "(8) the sides' TLAS gather ran at the ray tier (its counts read back, no pass waited)");
            std::printf("   (8) cascades 0 and 1: %zu inner-wall voxels, %zu lit at all, the brightest %.6g (%.4f %% of the sky)\n",
                        inner, lit, peak01, 100.0 * peak01);
            CHECK(inner > 0u && peak01 < 0.01,
                  "(8) AT THE RAY TIER no inner-wall voxel of cascades 0 and 1 reads 1 % of the sky (the voxel rays' e: "
                  "0.205 at High)");
            drop(sky);
            Arm m = arm(tiers[t], kE, 0.0f, false);
            if (!m.scene || m.cascades.empty() || !m.cascades[0].ok) {
                CHECK(false, "(8) the ray tier's main arm's room and store");
                drop(m);
                e->setRayTracing(false);
                continue;
            }
            const Reading fb = m.cascades[0];
            e->setArm("gi.bounceJacobi", 1.0);
            run(64);
            const Reading j64 = read(m.scene);
            e->setArm("gi.bounceJacobi", 0.0);
            run(2);
            double worst = 0.0;
            size_t over = 0;
            const bool same = fb.ok && j64.ok && fb.voxels == j64.voxels;
            for (size_t i = 0; same && i < fb.L.size(); ++i) {
                const double rel = std::fabs(double(fb.L[i]) - double(j64.L[i])) / std::max(double(j64.L[i]), 1e-6);
                worst = std::max(worst, rel);
                over += rel > 0.01 ? 1u : 0u;
            }
            const double limit = kE / (1.0 - kADiffuse);
            std::printf("   (8) the ray tier's main arm at rest in %d frames: feedback %.5f, Jacobi-64 %.5f (the limit "
                        "E / (1 - a') %.5f: %+.2f %%); feedback vs Jacobi-64 worst %.3f codes, %zu over one\n",
                        m.restFrames, fb.mean, j64.mean, limit, 100.0 * (j64.mean / limit - 1.0), 100.0 * worst, over);
            CHECK(m.restFrames >= 0 && same && over == 0u,
                  "(8) THE TLAS GATHER CONVERGES: the ray tier's feedback within a code of its 64-pass Jacobi");
            drop(m);
            e->setRayTracing(false);
            continue;
        }
        // THE SKY-LEAK ARM: the escape share, per cascade (the series half)
        std::vector<double> esc, skyL;
        if (doSeries) {
        Arm sky = arm(tiers[t], 0.0f, 1.0f, true);
        if (!sky.scene || sky.cascades.empty() || !sky.cascades[0].ok) {
            CHECK(false, "the sky-leak arm's room and store");
            drop(sky);
            continue;
        }
        CHECK(sky.restFrames >= 0, "the sky-leak arm comes to rest");
        std::printf("   the sky-leak arm (E 0, S 1) at rest in %d frames: the ESCAPE e per cascade (a' %.4f):", sky.restFrames,
                    kADiffuse);
        for (size_t c = 0; c < sky.cascades.size(); ++c) {
            esc.push_back(sky.cascades[c].ok ? escapeOf(sky.cascades[c].mean, 1.0) : -1.0);
            skyL.push_back(sky.cascades[c].ok ? sky.cascades[c].mean : -1.0);
            std::printf("  c%zu %.3f (L %.4f, cell %.3f m)", c, esc.back(), sky.cascades[c].mean,
                        double(sky.status.cascades[c].cell));
        }
        std::printf("\n");
        drop(sky);
        }

        // THE MAIN ARM: the feedback to its rest, then the 64-pass Jacobi
        Arm m = arm(tiers[t], kE, 0.0f, false);
        if (!m.scene || m.cascades.empty() || !m.cascades[0].ok) {
            CHECK(false, "the main arm's room and store");
            drop(m);
            continue;
        }
        const GiStatus atRest = m.status;   // (a copy: the arm is dropped below)
        const unsigned long long passes = atRest.cascades[0].feedbackPasses;
        const unsigned long long bricksRan = atRest.cascades[0].feedbackBricksRan;
        const unsigned bricks0 = atRest.cascades[0].bricks;
        std::printf("   the feedback came to rest in %d frames: cascade 0 ran %llu passes over %llu bricks (%u bricks; %.1f "
                    "brick-passes a brick), %llu stops, %llu resumes, %llu saturated\n", m.restFrames, passes, bricksRan,
                    bricks0, bricks0 ? double(bricksRan) / bricks0 : 0.0, atRest.cascades[0].feedbackStops,
                    atRest.cascades[0].feedbackResumes, atRest.cascades[0].feedbackSaturated);
        if (doSeries) CHECK(m.restFrames >= 0, "THE FEEDBACK STOPS: the chain owes nothing at its rest");
        const Reading fb = m.cascades[0];
        // THE JACOBI OVER CASCADE 0 (the door set to 1): restarted from its direct term, every brick a pass a frame, the
        // outer cascades holding their state - the feedback's rest, so cascade 0's two answers are fixed points of one
        // operator.
        e->setArm("gi.bounceJacobi", 1.0);
        run(64);   // the arm latches at the frame's top: its first frame restarts from the direct term, then a pass a frame
        const Reading j64 = read(m.scene);
        e->setArm("gi.bounceJacobi", 0.0);
        run(2);
        // THE DOOR RAN (the arm can fail): cascade 0 ran EVERY brick a frame through the window (its books land a few
        // frames late: 56 of the 64 at least), the cascades outside it held.
        const GiStatus afterJ = m.scene->published().gi;
        const unsigned long long jRan = afterJ.cascades[0].feedbackBricksRan - bricksRan;
        const unsigned long long outerRan =
            afterJ.cascades.size() > 1 ? afterJ.cascades[1].feedbackBricksRan - atRest.cascades[1].feedbackBricksRan : 0ull;
        std::printf("   the Jacobi's window: cascade 0 ran %llu brick-passes (%.1f a brick), cascade 1 %llu\n", jRan,
                    bricks0 ? double(jRan) / bricks0 : 0.0, outerRan);
        if (doSeries)
            CHECK(jRan >= 56ull * bricks0 && outerRan == 0ull,
                  "the Jacobi door ran every brick of cascade 0 a frame, the outer cascades held");
        // (4) THE RESUME ARM: a lamp over the floor's centre, the room at its rest (the Jacobi's tail stopped first, so
        // every brick the lamp's sphere does not activate is STOPPED) - at the ray tiers' chain only (High, Epic: 128^3, a
        // 0.625 m brick); at Low and Medium (64^3, 1.25 m bricks) the walls a brick outside the range sit at the room's own
        // walls and the lamp moves none of them by a code (Medium 0.07 at a lamp of 500, range 2): the arm could not fail.
        size_t lampOver = 0, lampMovedOutside = 0, lampMovedAll = 0;
        double lampMeanBefore = 0.0, lampMeanAfter = 0.0, lampOutsideMax = 0.0;
        double lampWorst = 0.0;
        int lampRest = -1;
        bool lampOk = false;
        const bool lampArm = tiers[t] == GiQuality::High || tiers[t] == GiQuality::Epic;
        NodeId lampNode = 0;
        std::vector<char> outside;
        Reading lj;   // the resume arm's Jacobi (the lamp at 200): the fade's start
        if (lampArm) {
            for (int f = 0, quiet = 0; f < 900 && quiet < 8; ++f) {   // the Jacobi's tail to its rest
                run(1);
                const GiStatus g = m.scene->published().gi;
                quiet = (!g.chainOwed && !g.feedbackOwed) ? quiet + 1 : 0;
            }
            const float lampY = 0.5f, lampRange = 3.0f;
            lampNode = m.scene->createNode();
            const NodeId lamp = lampNode;
            LightDesc ld;
            ld.type = LightType::Point;
            ld.colour = Colour(1.0f, 1.0f, 1.0f);
            ld.intensity = 200.0f;
            ld.range = lampRange;
            m.scene->setNodeTransform(lamp, Vec3(0.0f, lampY, 0.0f), Quat(), Vec3(1, 1, 1));
            m.scene->setLight(lamp, ld);
            const GiStatus before = m.scene->published().gi;
            int f = 0, quiet = 0;
            for (; f < 900 && quiet < 8; ++f) {
                run(1);
                const GiStatus g = m.scene->published().gi;
                quiet = (!g.chainOwed && !g.feedbackOwed) ? quiet + 1 : 0;
            }
            lampRest = quiet >= 8 ? f - quiet : -1;
            const GiStatus after = m.scene->published().gi;
            for (size_t c = 0; c < after.cascades.size() && c < before.cascades.size(); ++c)
                std::printf("   the resume arm's cascade %zu: %llu passes, %llu bricks run, %llu stops, %llu resumes, %llu carried, "
                            "the relight carried %llu frames\n", c,
                            after.cascades[c].feedbackPasses - before.cascades[c].feedbackPasses,
                            after.cascades[c].feedbackBricksRan - before.cascades[c].feedbackBricksRan,
                            after.cascades[c].feedbackStops - before.cascades[c].feedbackStops,
                            after.cascades[c].feedbackResumes - before.cascades[c].feedbackResumes,
                            after.cascades[c].feedbackCarried - before.cascades[c].feedbackCarried,
                            after.cascades[c].relightCarried - before.cascades[c].relightCarried);
            const Reading lf = read(m.scene);
            e->setArm("gi.bounceJacobi", 1.0);
            run(64);
            lj = read(m.scene);
            e->setArm("gi.bounceJacobi", 0.0);
            run(2);
            lampOk = lf.ok && lj.ok && lf.voxels == lj.voxels && lj.voxels == j64.voxels;
            if (lampOk) {
                // the voxels' positions in the selection's order, re-derived to tell the lamp's box from the rest
                GiVoxelVolume v;
                if (m.scene->giVoxelLight(0, v) && v.available) {
                    const float cell = v.cell[0];
                    for (int z = 0; z < v.depth; ++z)
                        for (int y = 0; y < v.height; ++y)
                            for (int x = 0; x < v.width; ++x) {
                                const float p[3] = { v.origin[0] + cell * (x + 0.5f), v.origin[1] + cell * (y + 0.5f),
                                                     v.origin[2] + cell * (z + 0.5f) };
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
                                const size_t i = ((size_t(z) * v.height + y) * v.width + x) * 4u;
                                if (!(v.light[i + 3] > 0.5f)) continue;
                                // outside the lamp's range box, by a brick (the activation's own boxes are whole bricks)
                                const float pad = lampRange + 8.0f * cell;
                                outside.push_back(std::fabs(p[0]) > pad || std::fabs(p[2]) > pad ||
                                                  std::fabs(p[1] - lampY) > pad ? 1 : 0);
                            }
                }
                for (size_t i = 0; i < lf.voxels; ++i) {
                    const double rel = std::fabs(double(lf.L[i]) - double(lj.L[i])) / std::max(double(lj.L[i]), 1e-6);
                    lampWorst = std::max(lampWorst, rel);
                    lampOver += rel > 0.01 ? 1u : 0u;
                    const double moved = std::fabs(double(lj.L[i]) - double(j64.L[i])) / std::max(double(j64.L[i]), 1e-6);
                    if (i < outside.size() && outside[i] && moved > 0.01) ++lampMovedOutside;
                    if (moved > 0.01) ++lampMovedAll;
                    if (i < outside.size() && outside[i]) lampOutsideMax = std::max(lampOutsideMax, moved);
                }
                lampMeanBefore = j64.mean;
                lampMeanAfter = lj.mean;
            }
            std::printf("   the resume arm (a lamp of 200, range %.1f m, %.1f m over the floor): at rest in %d frames - cascade 0 "
                        "resumed %llu bricks, stopped %llu; %zu inner-wall voxels outside its range moved over a code; "
                        "feedback vs Jacobi-64: worst voxel %.3f codes, %zu over one code (moved anywhere %zu; the inner "
                        "walls' mean %.5f -> %.5f; outside, the most moved %.3f codes of %zu); stopped on the operator's "
                        "bound (r never measured): %u before the lamp, %u at its rest\n", lampRange, lampY, lampRest,
                        after.cascades[0].feedbackResumes - before.cascades[0].feedbackResumes,
                        after.cascades[0].feedbackStops - before.cascades[0].feedbackStops, lampMovedOutside,
                        100.0 * lampWorst, lampOver, lampMovedAll, lampMeanBefore, lampMeanAfter, 100.0 * lampOutsideMax,
                        size_t(std::count(outside.begin(), outside.end(), char(1))), before.cascades[0].feedbackOnPrior,
                        after.cascades[0].feedbackOnPrior);
        }
        // (5) THE SLOW FADE: from the lamp at 400 at rest, to 200 in one frame, and - from 400 again - FADED to 200 over
        // kFadeFrames; each arm's rest against a 64-pass Jacobi of the end state (the resume arm's, the lamp at 200) at every
        // inner-wall voxel beyond the lamp's range box (the resume arm's set). The fade is slow enough that every voxel
        // of the room - the bricks inside the range (the sources the bricks beyond resume from) and beyond it - moves
        // under one step of the store a frame (the end states' difference over the frames, printed).
        size_t stepOver = 0, fadeOver = 0, beyond = 0;
        size_t carryOver = 0, carryMoved = 0;
        double carryWorst = 0.0;
        int carryRest = -1;
        unsigned long long carryResumes = 0, carryCarried = 0;
        bool carryOk = false;
        int fadeRoundGap = -1, fadeRounds = 0, fadeRoundBound = 0;
        double stepWorst = 0.0, fadeWorst = 0.0, travelBeyond = 0.0, travelInside = 0.0;
        int stepRest = -1, fadeRest = -1;
        bool fadeOk = false;
        if (lampArm && lampOk && doLamp) {
            const auto toRest = [&](int cap) {
                int f = 0, quiet = 0;
                for (; f < cap && quiet < 8; ++f) {
                    run(1);
                    const GiStatus g = m.scene->published().gi;
                    quiet = (!g.chainOwed && !g.feedbackOwed) ? quiet + 1 : 0;
                }
                return quiet >= 8 ? f - quiet : -1;
            };
            const auto setLamp = [&](float intensity) {
                LightDesc ld;
                ld.type = LightType::Point;
                ld.colour = Colour(1.0f, 1.0f, 1.0f);
                ld.intensity = intensity;
                ld.range = 3.0f;
                m.scene->setLight(lampNode, ld);
            };
            toRest(900);   // the resume arm's Jacobi tail (the lamp at 200)
            // THE START: the lamp at 400, at rest, and (the fade's travel) its Jacobi
            setLamp(400.0f);
            toRest(900);
            Reading o400, sEnd, fEnd;
            if (doFade) {
                e->setArm("gi.bounceJacobi", 1.0);
                run(64);
                o400 = read(m.scene);
                e->setArm("gi.bounceJacobi", 0.0);
                run(2);
                toRest(900);
            }
            // THE STEP ARM: 400 -> 200 in one frame
            if (doStep) {
                setLamp(200.0f);
                stepRest = toRest(900);
                sEnd = read(m.scene);
                if (doFade) {
                    // back to 400, to the rest
                    setLamp(400.0f);
                    toRest(900);
                }
            }
            if (doFade) {
            // THE FADE ARM: 400 -> 200 linearly over kFadeFrames - and (6) cascade 0's relight rounds through it
            unsigned long long rounds0 = m.scene->published().gi.cascades.empty()
                                             ? 0ull : m.scene->published().gi.cascades[0].relightRounds;
            int lastRound = 0, roundGap = 0, roundsDone = 0;
            for (int f = 1; f <= kFadeFrames; ++f) {
                setLamp(400.0f - 200.0f * float(f) / float(kFadeFrames));
                run(1);
                const GiStatus g = m.scene->published().gi;
                const unsigned long long r0 = g.cascades.empty() ? 0ull : g.cascades[0].relightRounds;
                if (r0 != rounds0) {
                    roundGap = std::max(roundGap, f - lastRound);
                    lastRound = f;
                    rounds0 = r0;
                    ++roundsDone;
                }
            }
            roundGap = std::max(roundGap, kFadeFrames + 1 - lastRound);   // (the tail: no round since the last)
            const unsigned lwf = giQualityFacts(tiers[t]).lightWorkFrames;
            fadeRoundGap = roundGap;
            fadeRounds = roundsDone;
            fadeRoundBound = int(2u * lwf + 2u);
            fadeRest = toRest(900);
            fEnd = read(m.scene);
            }
            // each arm against the end state's Jacobi (the resume arm's, the lamp at 200), beyond the range
            fadeOk = lj.voxels == outside.size() && (!doFade || (o400.ok && fEnd.ok && o400.voxels == lj.voxels &&
                                                                 fEnd.voxels == lj.voxels)) &&
                     (!doStep || (sEnd.ok && sEnd.voxels == lj.voxels));
            if (fadeOk)
                for (size_t i = 0; i < lj.voxels; ++i) {
                    const double o = std::max(double(lj.L[i]), 1e-6);
                    const double travel = doFade ? std::fabs(double(o400.L[i]) - double(lj.L[i])) / o : 0.0;
                    if (!outside[i]) { travelInside = std::max(travelInside, travel); continue; }
                    ++beyond;
                    travelBeyond = std::max(travelBeyond, travel);
                    if (doStep) {
                        const double rs = std::fabs(double(sEnd.L[i]) - double(lj.L[i])) / o;
                        stepWorst = std::max(stepWorst, rs);
                        stepOver += rs > 0.01 ? 1u : 0u;
                    }
                    if (doFade) {
                        const double rf = std::fabs(double(fEnd.L[i]) - double(lj.L[i])) / o;
                        fadeWorst = std::max(fadeWorst, rf);
                        fadeOver += rf > 0.01 ? 1u : 0u;
                    }
                }
            std::printf("   the slow fade (the lamp 400 -> 200 over %d frames): the inner-wall voxels move %.2f codes at most "
                        "inside the range (%.3f steps of the store a frame) and %.3f beyond it (%.3f steps a frame; %zu "
                        "voxels) | the STEP arm at rest in %d frames: beyond, worst %.3f codes, %zu over one | the FADE arm at "
                        "rest %d frames after: beyond, worst %.3f codes, %zu over one\n", kFadeFrames, 100.0 * travelInside,
                        travelInside * 1024.0 / kFadeFrames, 100.0 * travelBeyond, travelBeyond * 1024.0 / kFadeFrames,
                        beyond, stepRest, 100.0 * stepWorst, stepOver, fadeRest, 100.0 * fadeWorst, fadeOver);
            // (7) THE CARRIED ROUTE: from the step's end (the lamp at 200, at rest - the world the fade also ends in),
            // FADED to 400 with the carry door set
            if (doStep) {
            const GiStatus c0 = m.scene->published().gi;
            e->setArm("gi.feedbackCarriedRoute", 1.0);
            for (int f = 1; f <= kCarryFadeFrames; ++f) {
                setLamp(200.0f + 200.0f * float(f) / float(kCarryFadeFrames));
                run(1);
            }
            carryRest = toRest(900);
            e->setArm("gi.feedbackCarriedRoute", 0.0);
            const GiStatus c1 = m.scene->published().gi;
            if (!c0.cascades.empty() && !c1.cascades.empty()) {
                carryResumes = c1.cascades[0].feedbackResumes - c0.cascades[0].feedbackResumes;
                carryCarried = c1.cascades[0].feedbackCarried - c0.cascades[0].feedbackCarried;
            }
            const Reading cEnd = read(m.scene);
            e->setArm("gi.bounceJacobi", 1.0);
            run(64);
            const Reading cJ = read(m.scene);
            e->setArm("gi.bounceJacobi", 0.0);
            run(2);
            carryOk = cEnd.ok && cJ.ok && cEnd.voxels == lj.voxels && cJ.voxels == lj.voxels && lj.voxels == outside.size();
            if (carryOk)
                for (size_t i = 0; i < lj.voxels; ++i) {
                    if (!outside[i]) continue;
                    const double o = std::max(double(cJ.L[i]), 1e-6);
                    const double rel = std::fabs(double(cEnd.L[i]) - double(cJ.L[i])) / o;
                    carryWorst = std::max(carryWorst, rel);
                    carryOver += rel > 0.01 ? 1u : 0u;
                    carryMoved += std::fabs(double(cJ.L[i]) - double(lj.L[i])) / o > 0.01 ? 1u : 0u;
                }
            std::printf("   (7) the carried route (the lamp 200 -> 400 over %d frames, every injected brick carried in its "
                        "frame, the carried section the only route): at rest in %d frames - cascade 0 resumed %llu bricks, carried %llu; beyond the range %zu "
                        "voxels moved over a code; vs Jacobi-64 worst %.3f codes, %zu over one\n", kCarryFadeFrames,
                        carryRest, carryResumes, carryCarried, carryMoved, 100.0 * carryWorst, carryOver);
            }
        }
        std::vector<double> mainL;
        for (const Reading &r : m.cascades) mainL.push_back(r.ok ? r.mean : -1.0);
        drop(m);

        // THE OPERATOR'S ARM: the sky held at the room's answer (the series half)
        bool opOk = !doSeries;
        double lOp = 0.0;
        if (doSeries) {
            Arm op = arm(tiers[t], kE, float(sStar), false);
            opOk = op.scene && !op.cascades.empty() && op.cascades[0].ok;
            lOp = opOk ? op.cascades[0].mean : 0.0;
            drop(op);
        }

        if (!fb.ok || !j64.ok || fb.voxels != j64.voxels || !opOk) {
            CHECK(false, "the store reads back the same inner-wall voxels in every arm");
            continue;
        }
        if (doSeries) {
        const double ec = esc[0];
        const double limit = kE / (1.0 - kADiffuse * (1.0 - ec));
        double worst = 0.0;
        size_t over = 0;
        for (size_t i = 0; i < fb.voxels; ++i) {
            const double rel = std::fabs(double(fb.L[i]) - double(j64.L[i])) / std::max(double(j64.L[i]), 1e-6);
            worst = std::max(worst, rel);
            over += rel > 0.01 ? 1u : 0u;
        }
        // (the two arms' own fit, printed - the mean field's: a' e = L_sky / L_main, a'(1 - e) = 1 - E / L_main)
        if (mainL[0] > 0.0 && skyL[0] >= 0.0) {
            const double ae = skyL[0] / mainL[0], a1e = 1.0 - kE / mainL[0];
            std::printf("   the two arms' fit on cascade 0: a' %.4f, e %.3f\n", ae + a1e, ae + a1e > 0.0 ? ae / (ae + a1e) : 0.0);
        }
        std::printf("   %zu inner-wall voxels (cell %.4f m), e %.4f: the limit E / (1 - a'(1 - e)) = %.5f; feedback %.5f (%+.2f %%), "
                    "Jacobi-64 %.5f (%+.2f %%); feedback vs Jacobi-64: worst voxel %.3f codes, %zu voxels over one code\n",
                    fb.voxels, double(atRest.cascades[0].cell), ec, limit, fb.mean, 100.0 * (fb.mean / limit - 1.0),
                    j64.mean, 100.0 * (j64.mean / limit - 1.0), 100.0 * worst, over);
        std::printf("   the operator's arm (E 1, S* = E / (1 - a') = %.5f): the store %.5f (%+.2f %% of S*)\n", sStar, lOp,
                    100.0 * (lOp / sStar - 1.0));
        CHECK(over == 0u, "(1) FEEDBACK = A 64-PASS JACOBI WITHIN ONE CODE at every inner-wall voxel");
        // THE PHYSICS' OWN LIMIT (R12): a sealed room escapes NOTHING, so the reference is e = 0 - L = E / (1 - a') - and
        // the bar the store's QUANTUM at it (one half-float step: 2^(floor(log2 L) - 10)) beside the Jacobi's 64-pass
        // truncation (a'^64 L, nothing) - never a picked 3 % around a measured escape. The feedback adds its stop law's
        // own bound, (1)'s one code.
        // a' IS THE MATERIAL's WHOLE LOBE, by direct integration (the store re-emits both, the bounce's own rule): the
        // diffuse hemispherical albedo x rho, plus the dielectric's 4 % specular layer, F0 A-bar + B-bar (kS 1). The
        // diffuse-only a' (the fit's 0.3509) left the specular share out - the 2.7 % the store read "above" it.
        double sA = 0.0, sB = 0.0;
        enginetest::ggxSpecularAlbedoHemi(1.0, sA, sB);
        const double aTrue = kRho * enginetest::disneyDiffuseAlbedoHemi(1.0) + 0.04 * sA + sB;
        const double limit0 = kE / (1.0 - aTrue);
        const double q0 = std::ldexp(1.0, int(std::floor(std::log2(limit0))) - 10);
        std::printf("   THE e = 0 LIMIT E / (1 - a'), a' %.5f (diffuse %.5f + specular %.5f, by integration) = %.5f (q %.6f): the "
                    "Jacobi-64 %+.6f (%+.3f %%), the feedback %+.6f, the operator's arm %.5f; the store's own a' %.5f\n", aTrue,
                    kRho * enginetest::disneyDiffuseAlbedoHemi(1.0), 0.04 * sA + sB, limit0, q0, j64.mean - limit0,
                    100.0 * (j64.mean / limit0 - 1.0), fb.mean - limit0, lOp, j64.mean > 0.0 ? 1.0 - kE / j64.mean : -1.0);
        CHECK(std::fabs(j64.mean - limit0) <= q0 && std::fabs(fb.mean - limit0) <= q0 + 0.01 * limit0,
              "(2) THE SERIES' LIMIT WITH NO ESCAPE: the Jacobi within one store step of E / (1 - a'), the feedback within "
              "that and its stop law's code");
        CHECK(std::fabs(lOp - limit0) <= q0 + 0.01 * limit0,
              "(2b) THE OPERATOR'S ARM AT THE SAME LIMIT, within a store step and the stop law's code (a sealed room's sky "
              "is never seen: whatever S is)");
        {   // (2c) THE INJECTION ALONE (R12): the bounces off - the direct store holds the emission E, exactly
            Arm d = arm(tiers[t], kE, 0.0f, false, false);
            const double lD = d.scene && !d.cascades.empty() && d.cascades[0].ok ? d.cascades[0].mean : -1.0;
            drop(d);
            const double qE = std::ldexp(1.0, int(std::floor(std::log2(double(kE)))) - 10);
            std::printf("   the injection alone (the bounces off): the store %.6f against E %.1f (%+.3f %%)\n", lD,
                        double(kE), 100.0 * (lD / kE - 1.0));
            CHECK(lD >= 0.0 && std::fabs(lD - kE) <= qE,
                  "(2c) THE INJECTION: with the bounces off the inner walls hold E within a store step");
        }
        if (lampArm) {
            CHECK(lampOk && lampRest >= 0 && lampMovedOutside > 0u,
                  "(4) the resume arm: the lamp's light moves inner-wall voxels outside its range, and the feedback comes to rest");
            CHECK(lampOk && lampOver == 0u,
                  "(4) A STOPPED BRICK BESIDE A MOVING ONE RESTS WITHIN ONE CODE OF A 64-PASS JACOBI at every inner-wall voxel");
        }
        }
        if (lampArm && doFade) {
            std::printf("   (6) the fade is a SUSTAINED change: cascade 0's relight went round its owed box %d times in %d frames, the "
                        "longest wait %d frames (the floor's bound: 2 x lightWorkFrames + 2 = %d)\n", fadeRounds, kFadeFrames,
                        fadeRoundGap, fadeRoundBound);
            CHECK(fadeRoundGap >= 0 && fadeRoundGap <= fadeRoundBound,
                  "(6) A SUSTAINED CHANGE NEVER STARVES CASCADE 0: its relight rounds within the floor's bound");
            CHECK(fadeOk && travelInside * 1024.0 / kFadeFrames < 1.0 && travelBeyond * 1024.0 / kFadeFrames < 1.0 &&
                      travelBeyond > 0.01 && fadeRest >= 0,
                  "(5) the slow fade moves the voxels beyond the range over a code in all, every voxel under a step a frame");
            CHECK(fadeOk && fadeOver == 0u,
                  "(5) THE SLOW FADE ENDS WHERE THE STEP ENDS: beyond the range, the FADE arm within one code of a 64-pass Jacobi");
        }
        if (lampArm && doStep) {
            CHECK(fadeOk && stepRest >= 0 && stepOver == 0u,
                  "(5) THE SLOW FADE ENDS WHERE THE STEP ENDS: beyond the range, the STEP arm within one code of a 64-pass Jacobi");
            CHECK(carryOk && carryRest >= 0 && carryMoved > 0u,
                  "(7) the carried route: the fade moves voxels beyond the range over a code, and the feedback comes to rest");
            CHECK(carryOk && carryOver == 0u,
                  "(7) A CARRIED BRICK'S INJECTION REACHES ITS STOPPED NEIGHBOURS: beyond the range, within one code of a 64-pass Jacobi");
        }
        std::printf("   (the tier's three arms: %d frames, %.1f s)\n", frames - tierF0,
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - tierT0).count());
    }
    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
