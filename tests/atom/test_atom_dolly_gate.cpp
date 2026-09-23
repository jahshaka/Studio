// atom.dolly_gate — AT-A7r, the acceptance test of the whole LOD chain's
// VISIBILITY (SPECS/atom/A2_HONEST_GEOMETRY_AND_EVERY_ASSET_DESIGN.md §1.2,
// verbatim): a camera dollies onto a dense object and the picture must not POP.
//
// THE CLAIM, and it is a claim about the eye rather than about triangles: a
// level switch chosen by the world-error rule at a one-pixel budget is INVISIBLE
// — the frame it happens on differs from its predecessor no more than an
// ordinary frame of the same walk does. That is the only honest form of the
// statement, because a moving camera changes every pixel by itself: the frame
// before a switch and the frame after it differ because the camera moved 0.25 m
// AND because the mesh changed, and the second term is the one under test.
//
// THE CONTROL IS A PAIRED ARM IN THIS PROCESS, and it is a deviation from the
// brief's letter that the brief's own numbers force (ATOM-RESUMES-1 item 2,
// measured). AT-A7r asks for `bar = 3 x mean(|delta| over the NON-SWITCH steps)`
// — one number for the whole walk. That mean is not a control, because the
// ordinary step of THIS walk is not stationary: the same 0.25 m of travel moves
// the picture by 0.19 codes at 39.75 m and by 11.37 codes at 2.25 m (measured
// here, a 60x spread — a fixed fraction of the distance is a fixed fraction of
// the projected size, so the near half of any dolly moves the picture far more
// than the far half). Against a single mean of 2.00 codes, NINE ordinary steps
// inside 4.25 m failed the gate while the largest actual switch added only 1.5
// codes over its own neighbour: the instrument was measuring the dolly, not the
// chain.
//
// So the ordinary step is measured AT EVERY POSE, by walking the identical dolly
// twice in one process:
//
//   REF   `setLodBias(0)` pins every object at level 0 (Engine.h) — the same
//         poses with NO level change anywhere, so its per-step delta is the
//         camera's motion and nothing else. The arm asserts its own level is 0
//         at all 153 poses, which is what makes it a control.
//   CHAIN the shipped bias of 1: the chain switches where the rule says.
//
//     (1) delta_chain(step)                      <= 3 x max(that mean, REF at this pose)
//     (2) delta_chain(step) - delta_ref(step)    <= 3 x that mean
//
// and no step — switch or not — may exceed either. (2) IS AT-A7r's own bar,
// applied to the pop rather than to the picture: subtracting the control at the
// same pose leaves the part of the step the CHAIN is responsible for, and three
// times the walk's ordinary step is then a bound on the pop wherever the camera
// moves at all. (1) is the same statement made robustly where the control is
// sub-pixel (a 0.19-code step at 39.75 m, where a difference of two tiny numbers
// is noise); it is the weaker of the two near the camera, which is why both
// stand. Three, not one, because a pop that is not visible against three times
// the movement already on screen is not a pop worth the word.
//
// FRAMES, NEVER TIME (the engine has no wall clock): 30 warm-up frames at the
// start pose, then exactly ONE frame per 0.25 m step, 40 m down to 2 m.
//
// WHY THE HYSTERESIS BAND IS ON HERE (`setLodHysteresisOffscreen(true)`,
// Engine.h): the band is a PER-PASS property since ogre-patch 0075's amendment
// and it is ON for the view a user watches and OFF for every capture, so an
// offscreen view is the only view whose pixels `readPixels` can return and the
// only one that has no band by default. A gate on what the USER sees must
// therefore ask for it: with the band off this walk switches at the exact
// threshold, which is a different (and easier) picture than the shipped one —
// the band moves WHERE a switch happens and never how big it is, and it is what
// stops a camera parked on a threshold flipping every frame.
//
// THE INSTRUMENT SATURATES AT 1.0 (`OgreView::createRtt` is PFG_RGBA8_UNORM),
// so the fixture is lit to sit in the middle of the range: a mid-grey sphere, a
// single directional light at a moderate power, an ambient floor so the dark
// side is not clipped to black either. A saturated sphere would hide a pop by
// clamping it.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char _b[256];                                                           \
        std::snprintf(_b, sizeof(_b), fmt, __VA_ARGS__);                        \
        CHECK(cond, _b);                                                        \
    } while (0)

// The view is square and big enough that the sphere is hundreds of pixels
// across at the near end and tens at the far end: a pop has to be measurable at
// both ends of the walk.
static const unsigned kSize = 1024;

// THE FIXTURE: a UV sphere of 20,000 triangles (100 x 100) with FIVE coarser
// index lists over THE SAME VERTICES — 2, 4, 5, 10 and 20 rows/columns per
// quad. Decimating a lat-long grid by a divisor of its own resolution is the
// one decimation that needs no new vertices, which is what the bake produces and
// what makes a level change free of a draw call.
static const int kSeg = 100, kRing = 100;
static const float kRadius = 0.5f;
static const int kStrides[5] = { 2, 4, 5, 10, 20 };

static void spherePoint(int seg, int ring, float out[3])
{
    const float phi = float(seg) / float(kSeg) * 6.28318530718f;      // longitude
    const float theta = float(ring) / float(kRing) * 3.14159265359f;  // latitude, 0..pi
    out[0] = kRadius * std::sin(theta) * std::cos(phi);
    out[1] = kRadius * std::cos(theta);
    out[2] = kRadius * std::sin(theta) * std::sin(phi);
}

/// THE BOUND OF A LEVEL, MEASURED AND NOT ASSERTED. For a triangle whose three
/// vertices lie on the sphere, the largest distance between the sphere's surface
/// and that triangle's plane is `radius - (the plane's distance from the
/// centre)`: the sagitta of the cap the triangle cuts off. The bake measures a
/// two-sided distance; on a convex surface this IS that distance, and it is in
/// MESH units, which is what `MeshData::lodBounds` must be (the strategy divides
/// the world error by the instance's scale to reach these units).
static float measuredBound(const std::vector<float> &pos, const std::vector<unsigned> &idx)
{
    float worst = 0.0f;
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        const float *a = &pos[idx[t] * 3], *b = &pos[idx[t + 1] * 3], *c = &pos[idx[t + 2] * 3];
        const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        const float e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
        float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                       e1[0] * e2[1] - e1[1] * e2[0] };
        const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (!(nl > 1.0e-12f)) continue;                 // a degenerate pole triangle
        n[0] /= nl; n[1] /= nl; n[2] /= nl;
        const float planeDist = std::fabs(n[0] * a[0] + n[1] * a[1] + n[2] * a[2]);
        worst = std::max(worst, kRadius - planeDist);
    }
    return worst;
}

static MeshData sphereWithChain(std::vector<float> &boundsOut)
{
    MeshData d;
    for (int ring = 0; ring <= kRing; ++ring)
        for (int seg = 0; seg <= kSeg; ++seg) {
            float p[3];
            spherePoint(seg, ring, p);
            d.positions.insert(d.positions.end(), { p[0], p[1], p[2] });
            const float l = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            d.normals.insert(d.normals.end(), { p[0] / l, p[1] / l, p[2] / l });
        }
    auto build = [&](int step) {
        std::vector<unsigned> idx;
        for (int ring = 0; ring < kRing; ring += step)
            for (int seg = 0; seg < kSeg; seg += step) {
                const unsigned a = unsigned(ring * (kSeg + 1) + seg);
                const unsigned b = unsigned(ring * (kSeg + 1) + seg + step);
                const unsigned c = unsigned((ring + step) * (kSeg + 1) + seg + step);
                const unsigned e = unsigned((ring + step) * (kSeg + 1) + seg);
                idx.insert(idx.end(), { a, b, c, a, c, e });
            }
        return idx;
    };
    d.indices = build(1);
    boundsOut.clear();
    for (int s : kStrides) {
        d.lodIndices.push_back(build(s));
        boundsOut.push_back(measuredBound(d.positions, d.lodIndices.back()));
    }
    d.lodBounds = boundsOut;
    d.lodErrors = boundsOut;
    return d;
}

/// THE OBJECT'S PIXELS: everything either frame drew over the clear colour.
/// A union, because the silhouette MOVES between two steps and the pixels the
/// object just left are as much a part of the comparison as the ones it just
/// took. The background contributes nothing to a difference (it is the same
/// black in both frames) and is excluded anyway so the mean is the object's.
static double maskedMeanAbsDiff(const Image &a, const Image &b, unsigned &maskPixels)
{
    maskPixels = 0;
    if (a.width != b.width || a.height != b.height) return 1.0e9;
    double sum = 0.0;
    const float lit = 3.0f / 255.0f;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour ca = a.at(x, y), cb = b.at(x, y);
            const float la = std::max(ca.r, std::max(ca.g, ca.b));
            const float lb = std::max(cb.r, std::max(cb.g, cb.b));
            if (la <= lit && lb <= lit) continue;
            ++maskPixels;
            sum += std::fabs(double(ca.r) - double(cb.r)) + std::fabs(double(ca.g) - double(cb.g)) +
                   std::fabs(double(ca.b) - double(cb.b));
        }
    return maskPixels ? sum / (3.0 * double(maskPixels)) : 0.0;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-atom-dolly-gate-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("dolly", kSize, kSize, Colour(0, 0, 0));
    Scene *scene = e->createScene("dolly");
    if (!view || !scene) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(scene);
    // THE WATCHED VIEW'S BAND (see the header): set once, at creation, because
    // it is a graph-shape property.
    view->setLodHysteresisOffscreen(true);
    CHECK(view->lodHysteresisOffscreen(), "the offscreen view carries the watched view's LOD band");

    std::vector<float> bounds;
    const MeshData sphere = sphereWithChain(bounds);
    const unsigned long long triangles = sphere.indices.size() / 3u;
    CHECK_MSG(triangles >= 19000ull && triangles <= 21000ull,
              "the fixture is a dense sphere: %llu triangles at level 0", triangles);
    CHECK_MSG(bounds.size() >= 5u, "the chain has %zu coarser levels over the same vertices",
              bounds.size());
    bool ascending = bounds[0] > 0.0f;
    for (size_t i = 1; i < bounds.size(); ++i) ascending = ascending && bounds[i] > bounds[i - 1];
    CHECK(ascending, "the measured bounds are positive and strictly increasing");
    std::printf("   measured bounds (mesh units):");
    for (size_t i = 0; i < bounds.size(); ++i)
        std::printf(" L%zu %.5f", i + 1, bounds[i]);
    std::printf("\n");

    const MeshId mesh = scene->createMesh(sphere);
    PbrParams p;
    p.albedo = Colour(0.55f, 0.55f, 0.55f);   // mid-range under this light: no clipping
    p.roughness = 0.55f;
    p.metalness = 0.0f;
    const MaterialId mat = scene->createPbrMaterial(p);
    const NodeId node = scene->createNode();
    if (!mesh || !mat || !node || !scene->attachMesh(node, mesh, mat)) {
        std::printf("FAIL: fixture: %s\n", e->lastError().c_str());
        return 1;
    }
    enginetest::setNodePosition(scene, node, Vec3(0.0f, 0.0f, 0.0f));
    // A light OFF the dolly axis, so the shading has a gradient across the
    // silhouette and a level change shows up as a shading change and not only as
    // a silhouette one.
    enginetest::addDirectionalLight(scene, Vec3(-0.5f, -0.6f, -0.62f), 2.2f);
    scene->setAmbient(Colour(0.10f, 0.10f, 0.12f), Colour(0.06f, 0.06f, 0.08f));

    const float kFar = 40.0f, kNear = 2.0f, kStep = 0.25f;
    auto pose = [&](float dist) {
        enginetest::testCameraLookAt(view, Vec3(0.0f, 0.0f, dist), Vec3(0.0f, 0.0f, 0.0f));
    };

    // WHERE THE SWITCHES SHOULD BE, from the rule and this view's own
    // projection — printed, never asserted here (engine.lod_rule_parity is what
    // holds the rule to its copies). It is what makes the table readable, and
    // the observed switch sits a band past the predicted one by design (the
    // hysteresis holds the level across ~10 % of the threshold).
    pose(kFar);
    for (int i = 0; i < 5; ++i) e->renderOneFrame();
    GpuCullRequest vr;
    if (e->fillCullView(view, vr)) {
        std::printf("   this view: %.0f px tall, proj11 %.4f, budget %.1f px -> switches at",
                    vr.viewportHeight, vr.projScaleY, kLodBudgetPixels);
        for (float b : bounds) {
            // allowed(d) = tol * 2 * (d - radius) / (proj11 * H) = b
            const float d = b * vr.projScaleY * vr.viewportHeight / (2.0f * kLodBudgetPixels) +
                            kRadius;
            std::printf(" %.2f m", d);
        }
        std::printf("\n");
    }

    struct Step { float dist; unsigned level; double delta; unsigned mask; bool switched; };

    // ---- ONE WALK, IN FRAMES: 30 warm-up frames at the start pose, then
    // exactly one frame per 0.25 m step, 40 m -> 2 m ---------------------------
    unsigned levels = 0;
    bool readAll = true;
    auto walk = [&](const char *what, float mul = 1.0f) {
        std::vector<Step> out;
        pose(kFar * mul);
        for (int i = 0; i < 30; ++i) e->renderOneFrame();     // warm up, in FRAMES
        Image previous;
        unsigned previousLevel = 0xFFFFFFFFu;
        for (float dist = kFar; dist >= kNear - 1.0e-4f; dist -= kStep) {
            pose(dist * mul);
            e->renderOneFrame();                             // ONE frame per step, by design
            std::vector<ObjectLodDesc> drawn;
            scene->objectLods(drawn);
            unsigned level = 0xFFFFFFFFu;
            for (const ObjectLodDesc &d : drawn)
                if (d.node == node) { level = d.level; levels = d.levels; }
            if (level == 0xFFFFFFFFu) {
                std::printf("FAIL: %s: no LOD reading at %.2f m\n", what, dist);
                ++failures;
                break;
            }
            Image img;
            if (!view->readPixels(img)) {
                std::printf("FAIL: %s: readPixels at %.2f m\n", what, dist);
                readAll = false;
                break;
            }
            Step s{ dist, level, 0.0, 0u, false };
            if (previous.width) {
                s.delta = maskedMeanAbsDiff(previous, img, s.mask);
                s.switched = (previousLevel != 0xFFFFFFFFu && level != previousLevel);
                out.push_back(s);
            }
            previous = img;
            previousLevel = level;
        }
        return out;
    };

    // THE CONTROL FIRST: the identical dolly with the chain pinned at level 0.
    scene->setLodBias(0.0f);
    const std::vector<Step> ref = walk("the pinned control");
    unsigned refNotZero = 0;
    for (const Step &s : ref) if (s.level != 0u) ++refNotZero;
    CHECK_MSG(refNotZero == 0u,
              "the control really is a control: level 0 at every one of its %zu poses (%u not)",
              ref.size(), refNotZero);

    // ...THEN THE SHIPPED CHAIN over the same poses, in the same process.
    scene->setLodBias(1.0f);
    const std::vector<Step> steps = walk("the chain");

    CHECK(readAll, "every step's picture read back");
    CHECK_MSG(steps.size() >= 150u, "%zu compared pairs over the 40 m -> 2 m dolly", steps.size());
    CHECK_MSG(steps.size() == ref.size(), "the two arms walked the same %zu poses", steps.size());
    CHECK_MSG(levels >= 6u, "the drawn mesh carries %u levels (level 0 plus the chain)", levels);

    // ---- THE BAR, computed from this run's own steps -------------------------
    double nonSwitchSum = 0.0;
    unsigned nonSwitchCount = 0, switchCount = 0;
    bool monotone = true;
    for (size_t i = 0; i < steps.size(); ++i) {
        if (steps[i].switched) ++switchCount;
        else { nonSwitchSum += steps[i].delta; ++nonSwitchCount; }
        if (i && steps[i].level > steps[i - 1].level) monotone = false;
    }
    const double ordinary = nonSwitchCount ? nonSwitchSum / double(nonSwitchCount) : 0.0;
    auto barAt = [&](size_t i) {
        const double local = i < ref.size() ? ref[i].delta : 0.0;
        return 3.0 * std::max(ordinary, local);
    };

    // THE EXCESS is the column the gate below is really about: how much of this
    // step's movement the CHAIN added over the control's movement at the same
    // pose. It is the pop, in codes, with the camera's own motion subtracted.
    auto excessAt = [&](size_t i) {
        return steps[i].delta - (i < ref.size() ? ref[i].delta : 0.0);
    };

    std::printf("\n== the dolly (one frame per step; delta = mean |RGB| over the object's pixels) ==\n");
    std::printf("   %-8s %-6s %-11s %-9s %-11s %-7s %-10s %-9s %s\n", "dist m", "level", "delta",
                "codes/255", "ref(level 0)", "ratio", "excess", "mask px", "");
    for (size_t i = 0; i < steps.size(); ++i) {
        const Step &s = steps[i];
        const double r = i < ref.size() && ref[i].delta > 0.0 ? s.delta / ref[i].delta : 0.0;
        std::printf("   %-8.2f %-6u %-11.6f %-9.2f %-11.6f %-7.2f %-10.2f %-9u%s\n", s.dist,
                    s.level, s.delta, s.delta * 255.0, i < ref.size() ? ref[i].delta : 0.0, r,
                    excessAt(i) * 255.0, s.mask, s.switched ? "  <- SWITCH" : "");
    }

    std::printf("\n   AT-A7r's own number: the mean of the %u non-switch steps is %.6f (%.2f "
                "codes); 3x it = %.6f (%.2f codes) is the bar's FLOOR\n",
                nonSwitchCount, ordinary, ordinary * 255.0, 3.0 * ordinary, 3.0 * ordinary * 255.0);
    std::printf("   the ordinary step is NOT stationary: the control moves %.2f codes at %.2f m "
                "and %.2f codes at %.2f m\n",
                ref.empty() ? 0.0 : ref.front().delta * 255.0, ref.empty() ? 0.0f : ref.front().dist,
                ref.empty() ? 0.0 : ref.back().delta * 255.0, ref.empty() ? 0.0f : ref.back().dist);
    std::printf("   %u switch steps\n", switchCount);

    double worstSwitchRatio = 0.0, worstAnyRatio = 0.0, worstSwitch = 0.0;
    float worstSwitchDist = 0.0f, worstRatioDist = 0.0f;
    for (size_t i = 0; i < steps.size(); ++i) {
        const double bar = barAt(i);
        const double ratio = bar > 0.0 ? steps[i].delta / bar : 0.0;
        if (ratio > worstAnyRatio) { worstAnyRatio = ratio; worstRatioDist = steps[i].dist; }
        if (steps[i].switched) {
            if (steps[i].delta > worstSwitch) {
                worstSwitch = steps[i].delta;
                worstSwitchDist = steps[i].dist;
            }
            worstSwitchRatio = std::max(worstSwitchRatio, ratio);
        }
    }
    std::printf("   worst SWITCH step %.6f (%.2f codes) at %.2f m; worst fraction of its own bar: "
                "%.2f at a switch, %.2f over all steps (at %.2f m)\n",
                worstSwitch, worstSwitch * 255.0, worstSwitchDist, worstSwitchRatio, worstAnyRatio,
                worstRatioDist);

    // THE CHAIN MUST ACTUALLY BE WALKED, or this is a gate over one level.
    CHECK_MSG(switchCount >= 3u, "the walk crosses %u natural switches", switchCount);
    CHECK(monotone, "THE LEVEL IS MONOTONE NON-INCREASING: dollying in never coarsens");
    CHECK_MSG(ordinary > 0.0, "the walk moves the picture at all (ordinary step %.6f)", ordinary);
    // THE GATE ITSELF: every step, switch or not, under its own bar.
    unsigned over = 0;
    for (size_t i = 0; i < steps.size(); ++i)
        if (steps[i].delta > barAt(i)) {
            ++over;
            std::printf("   OVER THE BAR: %.2f m, level %u, delta %.6f (%.2f codes) vs bar %.6f%s\n",
                        steps[i].dist, steps[i].level, steps[i].delta, steps[i].delta * 255.0,
                        barAt(i), steps[i].switched ? " [switch]" : "");
        }
    CHECK_MSG(over == 0u,
              "no step of the dolly exceeds 3x the ordinary movement at its own pose (%u over)",
              over);

    // ---- AND THE BRIEF'S OWN BAR, ON THE EXCESS -----------------------------
    //
    // The assertion above bounds the step's WHOLE delta, which at the near end
    // is dominated by the camera: at 2.00 m the control alone moves 12.81 codes,
    // so 3x it is 38 codes of room and a pop adding 25 codes on top of the motion
    // would pass there (deep-auditor, SHOULD-FIX 3d). The pop itself is the
    // EXCESS over the control at the same pose — the camera's contribution
    // subtracted, which is exactly the quantity AT-A7r's one-number bar was
    // reaching for — and THAT is what 3x the walk's ordinary step bounds
    // honestly, at every distance:
    //
    //     delta_chain(i) - delta_ref(i) <= 3 x mean(non-switch delta)
    //
    // Both assertions stand: this one is the strong statement wherever the
    // control moves at all, and the one above is what keeps the sub-pixel far
    // end (a 0.19-code step at 39.75 m) from being gated on the difference of
    // two tiny numbers. MEASURED: the four switches' excesses are 1.33 / 0.19 /
    // 0.25 / 0.05 codes against a bar of 5.99 — 4.5x of room at the worst.
    const double excessBar = 3.0 * ordinary;
    unsigned overExcess = 0;
    double worstExcess = 0.0;
    float worstExcessDist = 0.0f;
    bool worstExcessSwitch = false;
    for (size_t i = 0; i < steps.size(); ++i) {
        const double x = excessAt(i);
        if (x > worstExcess) {
            worstExcess = x;
            worstExcessDist = steps[i].dist;
            worstExcessSwitch = steps[i].switched;
        }
        if (x > excessBar) {
            ++overExcess;
            std::printf("   OVER THE EXCESS BAR: %.2f m, level %u, excess %.2f codes vs %.2f%s\n",
                        steps[i].dist, steps[i].level, x * 255.0, excessBar * 255.0,
                        steps[i].switched ? " [switch]" : "");
        }
    }
    std::printf("   worst EXCESS over the control %.2f codes at %.2f m%s, against a bar of %.2f "
                "codes (3x the ordinary step)\n",
                worstExcess * 255.0, worstExcessDist, worstExcessSwitch ? " [switch]" : "",
                excessBar * 255.0);
    CHECK_MSG(overExcess == 0u,
              "THE POP ITSELF: no step adds more than 3x the ordinary step OVER the control at its "
              "own pose (%u over; worst %.2f codes of %.2f)",
              overExcess, worstExcess * 255.0, excessBar * 255.0);

    // ---- THE SCALED ARM (ATOM-RESUMES-1 item 1's acceptance) ----------------
    //
    // THE RULE IS HOMOGENEOUS IN (SCALE, DISTANCE): an instance ten times as
    // large, seen from ten times as far, subtends the same angle and shows the
    // same deviation in pixels, so it must take THE SAME LEVEL at every step of
    // the same walk. A strategy that compares a world-space error against
    // thresholds measured in MESH units fails this at every scale but 1 — which
    // is exactly what it did until the divisor landed. MEASURED on this fixture
    // with the divisor removed: all 152 poses draw a different level from their
    // 1x twin (the walk starts one level coarser and ends up finishing the chain
    // early) and the walk crosses TWO switches instead of four.
    //
    // It is the same 152 poses at 10x, so the two level sequences are compared
    // step for step and not merely at their ends.
    {
        enginetest::setNodeScale(scene, node, Vec3(10.0f, 10.0f, 10.0f));
        const std::vector<Step> scaled = walk("the 10x arm", 10.0f);
        CHECK_MSG(scaled.size() == steps.size(), "the 10x arm walked the same %zu poses",
                  scaled.size());
        unsigned differing = 0, scaledSwitches = 0;
        double deltaRatioSum = 0.0;
        unsigned deltaRatioCount = 0;
        for (size_t i = 0; i < scaled.size() && i < steps.size(); ++i) {
            if (scaled[i].level != steps[i].level) {
                ++differing;
                if (differing <= 4u)
                    std::printf("   THE SCALE TERM IS WRONG: pose %.2f m x10 draws level %u, "
                                "%.2f m x1 draws level %u\n",
                                steps[i].dist * 10.0f, scaled[i].level, steps[i].dist,
                                steps[i].level);
            }
            if (scaled[i].switched) ++scaledSwitches;
            if (steps[i].delta > 0.0) {
                deltaRatioSum += scaled[i].delta / steps[i].delta;
                ++deltaRatioCount;
            }
        }
        std::printf("\n== the 10x arm: 400 m -> 20 m in 2.5 m steps, one frame each ==\n");
        std::printf("   %u switches (the 1x walk had %u); mean per-step delta ratio 10x / 1x = "
                    "%.3f (the walks are geometrically similar, so 1.0 is the physics)\n",
                    scaledSwitches, switchCount,
                    deltaRatioCount ? deltaRatioSum / double(deltaRatioCount) : 0.0);
        CHECK_MSG(differing == 0u,
                  "SCALE 10 AT 10x THE DISTANCE PICKS THE SAME LEVEL AS SCALE 1 AT 1x, at every "
                  "one of the %zu poses (%u differ)", scaled.size(), differing);
        CHECK_MSG(scaledSwitches == switchCount,
                  "...and crosses the same %u switches (%u)", switchCount, scaledSwitches);
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
