// gi.card_lamp_shadow (PHOTON-7 stage 5, 5-3; SPECS/PHOTON_7_SPEC.md stage 5) - THE CARDS SEE LAMP SHADOWS, TRACED.
//
// THE FIXTURE: a carded 8 m floor and back wall, a lamp of 30 (range 6 m) 2.4 m over the floor's centre, and two 0.8 m
// blockers 0.8-1.6 m over the floor - a STILL one at x = -1.2 and a MOVER (the movable channel: never in the voxel
// store) at x = +1.2. The chain at High, the rays on, the gather on (its hits make the floor resident), the field off,
// no sun.
// THE LAWS (the floor's card read at world points through Scene::readCardAt; each point UMBRA when its segment to the
// lamp crosses a blocker shrunk by 0.1 m, LIT when it misses both grown by 0.2 m; the radiance normalised by the lamp's
// direct shape h / d^3 - the cosine over the square of the distance):
//   (1) BOTH SHADOWS ARE ON THE CARD: the umbra's normalised radiance under a quarter of the lit points' - the lamp's
//       direct is zero there and its bounce is what is left (unshadowed, the umbra reads like the lit: the ratio ~1.1);
//   (2) THE SHADOW FOLLOWS THE MOVED MOVER: the mover stepped 1.0 m along +z inside the lamp's range (its LampShadow
//       region stales the direct of the cards in the lamp's sphere; a mover changes no voxel, so no bounce change lists
//       the floor), at rest every sampled point within one code of a whole re-solve (the GI torn down and built again)
//       plus one step of the R11G11B10F store - RED where the LampShadow region does not reach the cards.
// (An EDIT inside a lamp's range stales the same sphere; a still caster's move also moves the store, whose change
// relights the floor through the GPU's accumulation, so no arm here can tell the two apart.)
//   (3) AN AREA LIGHT'S FORM FACTOR ON A CARD (a second fixture, below): the card's direct within the stored values'
//       half steps of D x a numeric form factor x the window at every floor point;
//   (4) PRINTED: the same light half-occluded - the card's one ray to the rectangle's centre against the visible form
//       factor across the penumbra (the worst error and the width over one code).
// A fixture (the engine alone, one offscreen view). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
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
NodeId cube(Scene *s, const Vec3 &at, const Vec3 &size, const Colour &albedo, bool movable) {
    const NodeId node = s->createNode();
    if (movable) s->setNodeMovable(node, true);   // before its geometry: no reclassification rebuild
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    PbrParams p;
    p.albedo = albedo;
    p.roughness = 0.9f;
    const MeshId mesh = s->createMesh(d);
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    enginetest::setNodePosition(s, node, at);
    enginetest::setNodeScale(s, node, size);
    return node;
}
/// Does the segment from a to b cross the box [lo, hi]? (slabs)
bool crosses(const Vec3 &a, const Vec3 &b, const Vec3 &lo, const Vec3 &hi) {
    double t0 = 0.0, t1 = 1.0;
    const double A[3] = { a.x, a.y, a.z }, B[3] = { b.x, b.y, b.z }, L[3] = { lo.x, lo.y, lo.z }, H[3] = { hi.x, hi.y, hi.z };
    for (int k = 0; k < 3; ++k) {
        const double d = B[k] - A[k];
        if (std::fabs(d) < 1e-12) {
            if (A[k] < L[k] || A[k] > H[k]) return false;
            continue;
        }
        double u = (L[k] - A[k]) / d, v = (H[k] - A[k]) / d;
        if (u > v) std::swap(u, v);
        t0 = std::max(t0, u);
        t1 = std::min(t1, v);
        if (t0 > t1) return false;
    }
    return true;
}
}   // namespace

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-card-lamp-shadow-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(true);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };
    View *view = e->createOffscreenView("card_lamp_shadow", 256, 256, Colour(0, 0, 0));
    Scene *scene = e->createScene("card_lamp_shadow");
    if (!view || !scene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(scene);
    {
        PostFxDesc fx;   // the chain, so the gather runs (test_gi_gather_findings.cpp's armChain)
        fx.allowOffscreen = true;
        fx.ssr = 1;
        view->setPostFx(fx);
    }
    view->setOffscreenContract(OffscreenContract::Live);
    cube(scene, Vec3(0.0f, -0.05f, 0.0f), Vec3(8.0f, 0.1f, 8.0f), Colour(0.6f, 0.6f, 0.6f), false);   // the floor
    cube(scene, Vec3(0.0f, 2.0f, -4.05f), Vec3(8.0f, 4.0f, 0.1f), Colour(0.5f, 0.5f, 0.6f), false);   // the back wall
    Vec3 blockerAt[2] = { Vec3(-1.2f, 1.2f, 0.0f), Vec3(1.2f, 1.2f, 0.0f) };
    cube(scene, blockerAt[0], Vec3(0.8f, 0.8f, 0.8f), Colour(0.5f, 0.4f, 0.3f), false);
    const NodeId mover = cube(scene, blockerAt[1], Vec3(0.8f, 0.8f, 0.8f), Colour(0.3f, 0.4f, 0.5f), true);
    const Vec3 lampPos(0.0f, 2.4f, 0.0f);
    {
        const NodeId lamp = scene->createNode();
        LightDesc l;
        l.type = LightType::Point;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = 30.0f;
        l.range = 6.0f;
        scene->setNodeTransform(lamp, lampPos, Quat(), Vec3(1, 1, 1));
        scene->setLight(lamp, l);
    }
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.5f, 5.0f), Vec3(0.0f, 0.5f, -1.0f));
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::On;
    gi.cards = true;
    gi.bounces = true;
    gi.updateBudget = 0;
    const auto settle = [&]() {
        unsigned long long relights = ~0ull, admitted = ~0ull;
        int quiet = 0, f = 0;
        for (; f < 1500 && quiet < 60; ++f) {
            run(1);
            const GiStatus g = scene->published().gi;
            const bool still = !g.chainOwed && !g.feedbackOwed && g.cards.built && g.cards.relights == relights &&
                               g.cards.admitted == admitted && g.cards.queueLength == 0u;
            relights = g.cards.relights;
            admitted = g.cards.admitted;
            quiet = still ? quiet + 1 : 0;
        }
        return quiet >= 60 ? f - quiet : -1;
    };
    // THE POINTS: the floor's top on a 0.15 m grid within 3 m of its centre
    std::vector<Vec3> pts;
    for (int i = -20; i <= 20; ++i)
        for (int k = -20; k <= 20; ++k) pts.push_back(Vec3(0.15f * float(i), 0.0f, 0.15f * float(k)));
    const auto classify = [&](const Vec3 &p, int &cls) {   // 1 umbra, 2 lit, 0 neither
        bool inside = false, near = false;
        for (const Vec3 &c : blockerAt) {
            inside = inside || crosses(p, lampPos, Vec3(c.x - 0.3f, c.y - 0.3f, c.z - 0.3f), Vec3(c.x + 0.3f, c.y + 0.3f, c.z + 0.3f));
            near = near || crosses(p, lampPos, Vec3(c.x - 0.6f, c.y - 0.6f, c.z - 0.6f), Vec3(c.x + 0.6f, c.y + 0.6f, c.z + 0.6f));
        }
        cls = inside ? 1 : (near ? 0 : 2);
    };
    // (each point's three channels; -1 where the card does not answer)
    const auto read = [&](std::vector<double> &out, size_t &okCount) {
        out.assign(pts.size() * 3u, -1.0);
        okCount = 0;
        for (size_t i = 0; i < pts.size(); ++i) {
            CardSample s;
            if (!scene->readCardAt(pts[i], Vec3(0, 1, 0), s) || !s.ok || !s.lit) continue;
            for (int c = 0; c < 3; ++c) out[i * 3u + c] = double(s.radiance[c]);
            ++okCount;
        }
    };
    const auto peak = [](const std::vector<double> &r, size_t i) {
        return std::max(std::max(r[i * 3u], r[i * 3u + 1u]), r[i * 3u + 2u]);
    };

    if (!scene->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
    e->waitForAsyncShaders();
    const int rest0 = settle();
    std::vector<double> r0;
    size_t ok0 = 0;
    read(r0, ok0);
    double umbraSum = 0.0, litSum = 0.0;
    size_t umbraN = 0, litN = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        if (r0[i * 3u] < 0.0) continue;
        int cls = 0;
        classify(pts[i], cls);
        if (!cls) continue;
        const double dx = lampPos.x - pts[i].x, dy = lampPos.y - pts[i].y, dz = lampPos.z - pts[i].z;
        const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double shape = double(lampPos.y) / (dist * dist * dist);
        if (cls == 1) { umbraSum += peak(r0, i) / shape; ++umbraN; }
        else { litSum += peak(r0, i) / shape; ++litN; }
    }
    const GiStatus g0 = scene->published().gi;
    const double ratio = umbraN && litN && litSum > 0.0 ? (umbraSum / double(umbraN)) / (litSum / double(litN)) : -1.0;
    std::printf("   at rest in %d frames (%u cards resident; the lamps traced on %llu relights): %zu of %zu points read "
                "(umbra %zu, lit %zu) | the umbra's normalised radiance over the lit points': %.3f\n", rest0,
                g0.cards.cardsResident, (unsigned long long)g0.cards.lampTraces, ok0, pts.size(), umbraN, litN, ratio);
    CHECK(rest0 >= 0 && umbraN > 40 && litN > 40, "the floor comes to rest carded and its points read");
    CHECK(ratio >= 0.0 && ratio < 0.25, "(1) BOTH SHADOWS ARE ON THE CARD: the umbra under a quarter of the lit points");

    // (2) THE MOVED MOVER, against a whole re-solve
    blockerAt[1] = Vec3(1.2f, 1.2f, 1.0f);
    enginetest::setNodePosition(scene, mover, blockerAt[1]);
    const int rest1 = settle();
    std::vector<double> r1, ro;
    size_t ok1 = 0, okO = 0;
    read(r1, ok1);
    GiParams off;
    off.mode = GiMode::Off;
    scene->setGlobalIllumination(off);
    run(2);
    scene->setGlobalIllumination(gi);
    e->waitForAsyncShaders();
    const int rest2 = settle();
    read(ro, okO);
    size_t compared = 0, over = 0;
    double worst = 0.0;
    for (size_t i = 0; i < pts.size(); ++i) {
        if (r1[i * 3u] < 0.0 || ro[i * 3u] < 0.0) continue;
        ++compared;
        double codes = 0.0;
        for (int c = 0; c < 3; ++c) {   // per channel: a code past one step of the store (6 / 6 / 5 mantissa bits)
            const double o = std::max(ro[i * 3u + c], 5.9604645e-6);
            const double step = std::ldexp(o, c == 2 ? -5 : -6);
            codes = std::max(codes, std::max(0.0, std::fabs(r1[i * 3u + c] - ro[i * 3u + c]) - step) / (0.01 * o));
        }
        worst = std::max(worst, codes);
        over += codes > 1.0 ? 1u : 0u;
    }
    std::printf("   the mover stepped: at rest in %d frames; the re-solve at rest in %d: %zu points compared, worst %.3f "
                "codes, %zu over a code\n", rest1, rest2, compared, worst, over);
    CHECK(rest1 >= 0 && rest2 >= 0 && compared > 100 && over == 0u,
          "(2) THE SHADOW FOLLOWS THE MOVED MOVER: every point within a code of a whole re-solve");

    scene->setGlobalIllumination(off);
    run(2);
    e->destroyView(view);
    e->destroyScene(scene);
    run(1);

    // ---- (3) AN AREA LIGHT ON A CARD, AGAINST A NUMERIC FORM FACTOR; (4) ITS PENUMBRA, PRINTED ----
    // A carded 2 m floor (one card, 128 texels: 1.5625 cm) and a carded back wall (the gather's hits make the floor
    // resident), an ACCURATE area light 1 x 0.5 m facing down 1.2 m over the floor's centre (intensity 3, range 20 m).
    // The card's direct (its radiance less its indirect and its flat term, over its kD) against D x F x the window:
    // D = intensity x pi (OgreScene's power scale), F = the rectangle's form factor at the texel, (1 / pi) x the
    // integral of cos x cos' / r^2 over the rectangle on a 200 x 100 midpoint grid, bounded over the texel the read
    // picked (its centre within half a texel of the point: F's least and largest on that square), the bar the three
    // stored values' half steps (R11G11B10F: 2^-7 of each on green).
    View *av = e->createOffscreenView("card_area", 256, 256, Colour(0, 0, 0));
    Scene *as = e->createScene("card_area");
    if (!av || !as) { std::printf("FAIL: the area view/scene\n"); return 1; }
    av->setScene(as);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        av->setPostFx(fx);
    }
    av->setOffscreenContract(OffscreenContract::Live);
    const float kAlbedo = 0.6f;
    cube(as, Vec3(0.0f, -0.05f, 0.0f), Vec3(2.0f, 0.1f, 2.0f), Colour(kAlbedo, kAlbedo, kAlbedo), false);
    cube(as, Vec3(0.0f, 1.0f, -1.05f), Vec3(2.0f, 2.0f, 0.1f), Colour(0.5f, 0.5f, 0.6f), false);
    const Vec3 areaAt(0.0f, 1.2f, 0.0f);
    const double aw = 1.0, ah = 0.5, kIntensity = 3.0, kRange = 20.0;
    {
        const NodeId lamp = as->createNode();
        LightDesc l;
        l.type = LightType::Area;
        l.accurate = true;
        l.rectWidth = float(aw);
        l.rectHeight = float(ah);
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = float(kIntensity);
        l.range = float(kRange);
        as->setNodeTransform(lamp, areaAt, Quat(), Vec3(1, 1, 1));
        as->setLight(lamp, l);
    }
    enginetest::testCameraLookAt(av, Vec3(0.0f, 2.2f, 2.6f), Vec3(0.0f, 0.3f, -0.5f));
    const auto settleOn = [&](Scene *sc) {
        unsigned long long relights = ~0ull, admitted = ~0ull;
        int quiet = 0, f = 0;
        for (; f < 1500 && quiet < 60; ++f) {
            run(1);
            const GiStatus g = sc->published().gi;
            const bool still = !g.chainOwed && !g.feedbackOwed && g.cards.built && g.cards.relights == relights &&
                               g.cards.admitted == admitted && g.cards.queueLength == 0u;
            relights = g.cards.relights;
            admitted = g.cards.admitted;
            quiet = still ? quiet + 1 : 0;
        }
        return quiet >= 60 ? f - quiet : -1;
    };
    // THE RECTANGLE (the light shines down its node's -Y: corners at y 1.2, x +-0.5, z +-0.25) and an occluder box's test
    std::vector<Vec3> plate;   // lo, hi when the plate is in
    const auto formFactor = [&](double px, double pz, bool visibleOnly) {
        const int nx = 200, nz = 100;
        const double dA = (aw / nx) * (ah / nz), y = areaAt.y;
        double sum = 0.0;
        for (int i = 0; i < nx; ++i)
            for (int k = 0; k < nz; ++k) {
                const double qx = -0.5 * aw + (i + 0.5) * aw / nx, qz = -0.5 * ah + (k + 0.5) * ah / nz;
                if (visibleOnly && !plate.empty() &&
                    crosses(Vec3(float(px), 0.0f, float(pz)), Vec3(float(qx), float(y), float(qz)), plate[0], plate[1]))
                    continue;
                const double dx = qx - px, dz = qz - pz, r2 = dx * dx + y * y + dz * dz;
                sum += y * y / (r2 * r2) * dA;   // cos (floor, +y) x cos (light, -y) / r^2
            }
        return sum / 3.14159265358979;
    };
    const auto windowAt = [&](double px, double pz) {
        const double d = std::sqrt(px * px + double(areaAt.y) * areaAt.y + pz * pz), x = d / kRange, x2 = x * x;
        const double w = std::max(0.0, std::min(1.0, 1.0 - x2 * x2));
        return w * w;
    };
    const double kHalfTexel = 1.0 / 128.0 + 1e-4;   // 2 m over 128 texels, halved (and a hair)
    if (!as->setGlobalIllumination(gi)) { std::printf("FAIL: the area GI builds\n"); return 1; }
    e->waitForAsyncShaders();
    const int restA = settleOn(as);
    size_t aRead = 0, aOver = 0;
    double aWorst = 0.0, aWorstRel = 0.0;
    for (int i = -9; i <= 9; ++i)
        for (int k = -9; k <= 9; ++k) {
            const double px = 0.1 * i, pz = 0.1 * k;
            CardSample cs;
            if (!as->readCardAt(Vec3(float(px), 0.0f, float(pz)), Vec3(0, 1, 0), cs) || !cs.ok || !cs.lit) continue;
            ++aRead;
            const double kD = cs.albedo[1];
            const double dk = double(cs.radiance[1]) - cs.indirect[1] - cs.flat[1];   // direct x kD, green
            double fLo = 1e30, fHi = -1e30;
            for (int u = -1; u <= 1; ++u)   // (the square's corners, edges' middles and centre: F is smooth on it)
                for (int v = -1; v <= 1; ++v) {
                    const double f = formFactor(px + kHalfTexel * u, pz + kHalfTexel * v, false);
                    fLo = std::min(fLo, f);
                    fHi = std::max(fHi, f);
                }
            const double scale = kD * kIntensity * 3.14159265358979 * windowAt(px, pz);
            const double tol = std::ldexp(double(cs.radiance[1]) + cs.indirect[1] + cs.flat[1], -7);
            const double past = std::max(0.0, std::max(scale * fLo - dk, dk - scale * fHi));
            const double rel = std::fabs(dk / (scale * 0.5 * (fLo + fHi)) - 1.0);
            aWorst = std::max(aWorst, past / std::max(tol, 1e-12));
            aWorstRel = std::max(aWorstRel, rel);
            aOver += past > tol ? 1u : 0u;
        }
    std::printf("   (3) the area light, unoccluded: at rest in %d frames; %zu of 361 floor points read; the card's direct "
                "against D x F (numeric) x the window: worst %.3f of the stored values' half steps past the texel's F "
                "range, %zu points past, the largest relative difference %.4f\n", restA, aRead, aWorst, aOver, aWorstRel);
    CHECK(restA >= 0 && aRead > 300 && aOver == 0u,
          "(3) THE AREA LIGHT'S FORM FACTOR ON THE CARD: every point within the stores' half steps of the numeric one");

    // (4) THE PENUMBRA, PRINTED: a still 0.05 m plate 0.6 m up over x < 0 (half the rectangle's view across a 1 m
    // band); the card's direct along z = 0, a texel apart, against D x the VISIBLE form factor (the same grid, each
    // sample's segment to the floor point tested against the plate), past the stored values' half steps.
    plate = { Vec3(-1.7f, 0.575f, -1.2f), Vec3(0.0f, 0.625f, 1.2f) };
    cube(as, Vec3(-0.85f, 0.6f, 0.0f), Vec3(1.7f, 0.05f, 2.4f), Colour(0.5f, 0.5f, 0.5f), false);
    const int restP = settleOn(as);
    size_t pRead = 0;
    int firstOver = 1 << 30, lastOver = -(1 << 30), overCount = 0;
    double pWorstFrac = 0.0, pWorstCodes = 0.0, pWorstAt = 0.0;
    for (int t = 0; t <= 102; ++t) {
        const double px = -0.8 + t / 64.0, pz = 0.0;
        CardSample cs;
        if (!as->readCardAt(Vec3(float(px), 0.0f, float(pz)), Vec3(0, 1, 0), cs) || !cs.ok || !cs.lit) continue;
        ++pRead;
        const double kD = cs.albedo[1];
        const double dk = double(cs.radiance[1]) - cs.indirect[1] - cs.flat[1];
        const double scale = kD * kIntensity * 3.14159265358979 * windowAt(px, pz);
        const double full = scale * formFactor(px, pz, false), truth = scale * formFactor(px, pz, true);
        // (past the three stored values' half steps, as (3)'s bar: the store's rounding is no penumbra error)
        const double tol = std::ldexp(double(cs.radiance[1]) + cs.indirect[1] + cs.flat[1], -7);
        const double err = std::max(0.0, std::fabs(dk - truth) - tol);
        const double codes = err / (0.01 * std::max(truth + cs.indirect[1] + cs.flat[1], 5.9604645e-6));
        if (err / std::max(full, 1e-12) > pWorstFrac) { pWorstFrac = err / std::max(full, 1e-12); pWorstAt = px; }
        pWorstCodes = std::max(pWorstCodes, codes);
        if (codes > 1.0) { firstOver = std::min(firstOver, t); lastOver = std::max(lastOver, t); ++overCount; }
    }
    std::printf("   (4) the area light's penumbra (printed): at rest in %d frames; %zu of 103 texels along z = 0 read; the "
                "card's centre-ray answer against the visible form factor: worst %.1f %% of the unoccluded direct (at x "
                "%.3f m), %.1f codes of the texel's radiance; over one code at %d texels, from x %.3f to %.3f m (%d texels "
                "wide; the true penumbra is 64)\n", restP, pRead, 100.0 * pWorstFrac, pWorstAt, pWorstCodes, overCount,
                overCount ? -0.8 + firstOver / 64.0 : 0.0, overCount ? -0.8 + lastOver / 64.0 : 0.0,
                overCount ? lastOver - firstOver + 1 : 0);
    as->setGlobalIllumination(off);
    run(2);
    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
