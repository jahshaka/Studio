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
//       plus one step of the radiance store (RGBA16F since 10-2d) - RED where the LampShadow region does not reach the cards.
// (An EDIT inside a lamp's range stales the same sphere; a still caster's move also moves the store, whose change
// relights the floor through the GPU's accumulation, so no arm here can tell the two apart.)
//   (3) AN AREA LIGHT'S FORM FACTOR ON A CARD (a second fixture, below): the card's direct within the stored values'
//       half steps of D x a numeric form factor x the window at every floor point;
//   (4) ITS PENUMBRA (the 5-3 fix: an area light's visibility fractional, n x n ratio rays a texel): the same light
//       half-occluded - every texel against the visible form factor within GiQualityFacts::cardAreaStrata's law, the
//       band's signed mean under one code; the trace's rays and GPU ms printed;
//   (5) PRINTED: the bounce on the back wall with the voxels' area visibility one march to the centre (shipped)
//       against 16 x 16 ratio marches (the measuring arm gi.voxelAreaStrata).
// THE PIXEL'S ARMS (PHOTON-7 stage 7, 7-A: the pixel takes the cards' law - the ray tier's jahAreaVis, the ratio over
// the same strata, the fork's hook multiplying the LTC terms): the same half-occluded light, the camera moved UNDER the
// plate (it hides the floor's umbra from above), the visibility read back (the test door area.visReadback):
//   (6) THE PIXEL BEHIND THE PLATE READS NO DIRECT: the floor's full-umbra pixels (x in [-0.9, -0.6]) against the lit
//       ones at the mirrored distance (x in [0.6, 0.9]) in the picture's radiance - under a quarter (the direct is zero
//       there and the bounce is what is left). RED before 7-A: the LTC light reached the umbra whole (ratio ~1).
//   (7) THE PIXEL'S PENUMBRA: along the floor's z = 0 line, the read-back visibility of light 0 against the truth - the
//       numeric visible form factor over the full one - within the strata law's bound (4)'s, plus the RGBA8 store's
//       half step, plus the truth's own range over the resolve's footprint (+-1.5 trace texels); over the band the
//       SIGNED mean under one code of the unoccluded direct (the estimator unbiased).
//   (8) THE GLOSSY LOBE TAKES ITS OWN RATIO (the spec's rule: the diffuse-weighted visible fraction stood for the
//       LTC specular's by up to 4.6 codes of a 0.3-rough metal's unoccluded specular - over a code - so the pixel
//       carries a second, specular-weighted set): the floor turned to a glossy metal (perceptual roughness 0.3), the
//       read-back specular visibility along z = 0 against the numeric specular-weighted visible fraction (GGX
//       D G2 / ( 4 N.V ) x cos' / r^2 over the rectangle, the pixel's own view) - over the band the SIGNED mean under
//       one code of the unoccluded specular; the per-point error, and the diffuse ratio's error on the same points,
//       printed.
// THE NO-RAY ROUTE'S ARMS (PHOTON-7 stage 7, 7-6 item 2; the owner's law: every light is blocked by a wall on every
// path): rays off, the same half-occluded light, the matte floor again - the visibility the screen job marches through
// the voxel chain (Jahshaka/ScreenAreaVis, the ray tier's law in one text), read back:
//   (9) THE NO-RAY PIXEL BEHIND THE PLATE READS NO DIRECT: (6)'s bar. RED before 7-6 item 2: a no-ray chain bound no
//       visibility - the umbra read 0.9974 of the lit floor's radiance;
//   (10) PRINTED: its penumbra against (7)'s numeric truth - its error the voxel cell (the march's start a cell off the
//       surface) and VOXEL-LEAK-1's thin wall, named; and both producers' GPU time at 1080p (the ray tier's GPU rows, the
//       no-ray route's passes' own timestamp pairs - which enclose a barrier's wait on the screen integrate in front).
// A fixture (the engine alone, one offscreen view). Counted in frames. TWO ROWS (PHOTON-7 stage 7: whole, past a
// fixture's 10 s budget), each its own fixture: gi.card_lamp_shadow (`lamp`: (1), (2)), gi.card_lamp_shadow_area
// (`area`: (3)-(10)); every bar as written above.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <set>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

namespace {
NodeId cube(Scene *s, const Vec3 &at, const Vec3 &size, const Colour &albedo, bool movable, MaterialId *matOut = nullptr) {
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
    if (matOut) *matOut = mat;
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

int main(int argc, char **argv)
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
    // ONE PART A ROW (PHOTON-7 stage 7: whole, past the fixture's 10 s budget), named by the argument: `lamp` - the
    // lamp's two shadows on the card, (1) and (2); `area` - the area light's fixture, (3)-(10). None = both.
    const std::string part = argc > 1 ? argv[1] : "";
    const bool doLamp = part.empty() || part == "lamp", doArea = part.empty() || part == "area";
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.cards = true;
    gi.bounces = true;
    gi.updateBudget = 0;
    GiParams off;
    off.mode = GiMode::Off;
    if (doLamp) {
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
        // A STILL PICTURE GATHERS (View::setOffscreenContract; PHOTON-7 7-5: the gather is recorded only for a view whose
        // chain gathers, and a Live view takes the field's answer)
        view->setOffscreenContract(OffscreenContract::StillPicture);
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
        // (each point's three channels; -1 where the card does not answer) - and its INDIRECT half beside (the worst
        // point's breakdown: which half of the card's value disagrees)
        std::vector<double> lastIndirect;
        const auto read = [&](std::vector<double> &out, size_t &okCount) {
            out.assign(pts.size() * 3u, -1.0);
            lastIndirect.assign(pts.size() * 3u, -1.0);
            okCount = 0;
            for (size_t i = 0; i < pts.size(); ++i) {
                CardSample s;
                if (!scene->readCardAt(pts[i], Vec3(0, 1, 0), s) || !s.ok || !s.lit) continue;
                for (int c = 0; c < 3; ++c) out[i * 3u + c] = double(s.radiance[c]);
                for (int c = 0; c < 3; ++c) lastIndirect[i * 3u + c] = double(s.indirect[c]);
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
        std::vector<double> r1, ro, n1, no;
        size_t ok1 = 0, okO = 0;
        read(r1, ok1);
        n1 = lastIndirect;
        // (the resident set of each state: a source card resident in one and voxel-read in the other is not an
        // unreported drift - the breakdown's question)
        std::vector<CardRadiance> set1, setO;
        unsigned long long setFrame = 0ull;
        scene->readCardRadiance(set1, setFrame);
        scene->setGlobalIllumination(off);
        run(2);
        scene->setGlobalIllumination(gi);
        e->waitForAsyncShaders();
        const int rest2 = settle();
        read(ro, okO);
        no = lastIndirect;
        scene->readCardRadiance(setO, setFrame);
        size_t compared = 0, over = 0;
        double worst = 0.0;
        size_t worstAt = 0;
        int worstCh = 0;
        for (size_t i = 0; i < pts.size(); ++i) {
            if (r1[i * 3u] < 0.0 || ro[i * 3u] < 0.0) continue;
            ++compared;
            double codes = 0.0;
            for (int c = 0; c < 3; ++c) {   // per channel: a code past one step of the radiance store (RGBA16F, 10-2d)
                const double o = std::max(ro[i * 3u + c], 5.9604645e-6);
                const double step = std::ldexp(o, -10);
                const double cc = std::max(0.0, std::fabs(r1[i * 3u + c] - ro[i * 3u + c]) - step) / (0.01 * o);
                if (cc > codes) { codes = cc; if (codes > worst) worstCh = c; }
            }
            if (codes > worst) worstAt = i;
            worst = std::max(worst, codes);
            over += codes > 1.0 ? 1u : 0u;
        }
        std::printf("   the mover stepped: at rest in %d frames; the re-solve at rest in %d: %zu points compared, worst %.3f "
                    "codes, %zu over a code\n", rest1, rest2, compared, worst, over);
        {
            // THE WORST POINT, BROKEN DOWN (the card's value = direct + indirect + flat; the moved state against the re-solve)
            const size_t i = worstAt;
            int cls = 0;
            classify(pts[i], cls);
            std::printf("   the worst point (%.2f, %.2f) %s, channel %d: radiance moved %.6g %.6g %.6g / re-solve %.6g %.6g %.6g; "
                        "indirect moved %.6g %.6g %.6g / re-solve %.6g %.6g %.6g\n", pts[i].x, pts[i].z,
                        cls == 1 ? "UMBRA" : cls == 2 ? "LIT" : "penumbra", worstCh, r1[i * 3u], r1[i * 3u + 1], r1[i * 3u + 2],
                        ro[i * 3u], ro[i * 3u + 1], ro[i * 3u + 2], n1[i * 3u], n1[i * 3u + 1], n1[i * 3u + 2], no[i * 3u],
                        no[i * 3u + 1], no[i * 3u + 2]);
            // (the over-a-code points by class, and the radiance's and the indirect's own worst codes over them)
            size_t byCls[3] = { 0, 0, 0 };
            double worstInd = 0.0;
            for (size_t k = 0; k < pts.size(); ++k) {
                if (r1[k * 3u] < 0.0 || ro[k * 3u] < 0.0) continue;
                double cd = 0.0, ci = 0.0;
                for (int c = 0; c < 3; ++c) {
                    const double o = std::max(ro[k * 3u + c], 5.9604645e-6);
                    cd = std::max(cd, std::max(0.0, std::fabs(r1[k * 3u + c] - ro[k * 3u + c]) - std::ldexp(o, -10)) / (0.01 * o));
                    ci = std::max(ci, std::fabs(n1[k * 3u + c] - no[k * 3u + c]) / (0.01 * o));
                }
                if (cd > 1.0) { int kc = 0; classify(pts[k], kc); ++byCls[kc]; worstInd = std::max(worstInd, ci); }
            }
            std::printf("   over a code: %zu umbra, %zu lit, %zu penumbra; over them the INDIRECT half's change reaches %.3f codes "
                        "of the radiance\n", byCls[1], byCls[2], byCls[0], worstInd);
            std::set<std::pair<NodeId, unsigned>> a, b;
            for (const CardRadiance &c : set1) a.insert({ c.node, c.card });
            for (const CardRadiance &c : setO) b.insert({ c.node, c.card });
            size_t onlyMoved = 0, onlyResolve = 0;
            for (const auto &k : a) onlyMoved += b.count(k) ? 0u : 1u;
            for (const auto &k : b) onlyResolve += a.count(k) ? 0u : 1u;
            std::printf("   resident: the moved state %zu cards, the re-solve %zu - %zu only in the moved, %zu only in the re-solve\n",
                        a.size(), b.size(), onlyMoved, onlyResolve);
            for (const CardRadiance &c : set1)
                if (!b.count({ c.node, c.card }))
                    std::printf("     only in the moved: node %llu card %u face %u size %u, last read %llu\n",
                                (unsigned long long)c.node, c.card, c.bucket, c.size, (unsigned long long)c.lastRead);
            for (const CardRadiance &c : setO)
                if (!a.count({ c.node, c.card }))
                    std::printf("     only in the re-solve: node %llu card %u face %u size %u, last read %llu\n",
                                (unsigned long long)c.node, c.card, c.bucket, c.size, (unsigned long long)c.lastRead);
        }
        CHECK(rest1 >= 0 && rest2 >= 0 && compared > 100 && over == 0u,
              "(2) THE SHADOW FOLLOWS THE MOVED MOVER: every point within a code of a whole re-solve");

        scene->setGlobalIllumination(off);
        run(2);
        e->destroyView(view);
        e->destroyScene(scene);
        run(1);
    }

    if (doArea) {
        // ---- (3) AN AREA LIGHT ON A CARD, AGAINST A NUMERIC FORM FACTOR; (4) ITS PENUMBRA, PRINTED ----
        // A carded 2 m floor (one card, 128 texels: 1.5625 cm) and a carded back wall (the gather's hits make the floor
        // resident), an ACCURATE area light 1 x 0.5 m facing down 1.2 m over the floor's centre (intensity 3, range 20 m).
        // The card's direct (its radiance less its indirect and its flat term, over its kD) against D x F x the window:
        // D = intensity x pi (OgreScene's power scale), F = the rectangle's form factor at the texel, (1 / pi) x the
        // integral of cos x cos' / r^2 over the rectangle on a 200 x 100 midpoint grid, bounded over the texel the read
        // picked (its centre within half a texel of the point: F's least and largest on that square), the bar the three
        // stored values' half steps (the radiance RGBA16F's, the indirect and flat terms' R11G11B10F 2^-7 on green).
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
        // A STILL PICTURE GATHERS (View::setOffscreenContract; PHOTON-7 7-5: the gather is recorded only for a view whose
        // chain gathers, and a Live view takes the field's answer)
        av->setOffscreenContract(OffscreenContract::StillPicture);
        const float kAlbedo = 0.6f;
        MaterialId floorMat = 0;
        cube(as, Vec3(0.0f, -0.05f, 0.0f), Vec3(2.0f, 0.1f, 2.0f), Colour(kAlbedo, kAlbedo, kAlbedo), false, &floorMat);
        cube(as, Vec3(0.0f, 1.0f, -1.05f), Vec3(2.0f, 2.0f, 0.1f), Colour(0.5f, 0.5f, 0.6f), false);
        const Vec3 areaAt(0.0f, 1.2f, 0.0f);
        const double aw = 1.0, ah = 0.5, kIntensity = 3.0, kRange = 20.0;
        {
            const NodeId lamp = as->createNode();
            LightDesc l;
            l.type = LightType::Area;
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
                // (the stored values' half steps: the radiance RGBA16F's 2^-11 since 10-2d, the indirect and flat terms'
                // R11G11B10F 2^-7 on green)
                const double tol = std::ldexp(double(cs.radiance[1]), -11) + std::ldexp(double(cs.indirect[1]) + cs.flat[1], -7);
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

        // (4) THE PENUMBRA: a still 0.05 m plate 0.6 m up over x < 0 (half the rectangle's view across a 1 m band); the
        // card's direct along z = 0, a texel apart, against D x the VISIBLE form factor (the same grid, each sample's
        // segment to the floor point tested against the plate). The rounding excluded (the three stored values' half
        // steps and the fractional byte's, 1 / 510 of the area light's unoccluded direct). THE BARS (GiQualityFacts::
        // cardAreaStrata's law): at every texel the error within the weight of the one column of strata the plate's edge
        // cuts (the edge is parallel to the rectangle's side: from each point it projects to a line x = const on the
        // rectangle) - each stratum's form-factor weight bounded over its corners and centre - and over the band (the
        // texels the plate shades in part) the SIGNED mean under one code of the texel's radiance (the jitter unbiased).
        plate = { Vec3(-1.7f, 0.575f, -1.2f), Vec3(0.0f, 0.625f, 1.2f) };
        e->setFrameMonitor(MonitorLevel::Review);
        cube(as, Vec3(-0.85f, 0.6f, 0.0f), Vec3(1.7f, 0.05f, 2.4f), Colour(0.5f, 0.5f, 0.5f), false);
        const int restP = settleOn(as);
        run(6);   // the GPU samples come back a few frames late
        e->setFrameMonitor(MonitorLevel::Off);
        double lampMsMax = -1.0;
        unsigned lampFrames = 0;
        {
            std::vector<MonitorRecord> records;
            while (e->popMonitorRecords(records) > 0) {}
            for (const MonitorRecord &r : records) {
                double ms = -1.0;
                for (const CacheWork &w : r.cacheWork)
                    if (w.detail == "cards.lamps" && w.gpuMs >= 0.0f) ms = std::max(ms, 0.0) + double(w.gpuMs);
                if (ms >= 0.0) { ++lampFrames; lampMsMax = std::max(lampMsMax, ms); }
            }
        }
        const GiStatus gp = as->published().gi;
        const unsigned n = std::max(gp.cards.areaStrata, 1u);
        // the law's bound at a floor point: the largest column's share of the strata's form-factor weights
        const auto lawBound = [&](double px, double pz) {
            const auto f = [&](double qx, double qz) {
                const double dx = qx - px, dz = qz - pz, y = areaAt.y, r2 = dx * dx + y * y + dz * dz;
                return y * y / (r2 * r2);
            };
            double minSum = 0.0, worstCol = 0.0;
            for (unsigned ii = 0; ii < n; ++ii) {
                double colMax = 0.0;
                for (unsigned jj = 0; jj < n; ++jj) {
                    const double x0 = -0.5 * aw + aw * ii / n, x1 = -0.5 * aw + aw * (ii + 1) / n;
                    const double z0 = -0.5 * ah + ah * jj / n, z1 = -0.5 * ah + ah * (jj + 1) / n;
                    const double v[5] = { f(x0, z0), f(x1, z0), f(x0, z1), f(x1, z1), f(0.5 * (x0 + x1), 0.5 * (z0 + z1)) };
                    colMax += *std::max_element(v, v + 5);
                    minSum += *std::min_element(v, v + 5);
                }
                worstCol = std::max(worstCol, colMax);
            }
            return minSum > 0.0 ? worstCol / minSum : 1.0;
        };
        size_t pRead = 0, pBand = 0, pPast = 0;
        double pWorstFrac = 0.0, pWorstAt = 0.0, pWorstOfBound = 0.0, bandSigned = 0.0, bandAbs = 0.0, boundMin = 1.0;
        double bandErr = 0.0, bandRad = 0.0;
        for (int t = 0; t <= 102; ++t) {
            // AT THE TEXELS' CENTRES (the floor's one card: 128 texels over its 2 m, centres at -1 + ( i + 0.5 ) / 64): the
            // truth where the card's answer was traced (a point half a texel off moves the truth by the penumbra's slope).
            const double px = -1.0 + (13.5 + t) / 64.0, pz = 0.5 / 64.0;
            CardSample cs;
            if (!as->readCardAt(Vec3(float(px), 0.0f, float(pz)), Vec3(0, 1, 0), cs) || !cs.ok || !cs.lit) continue;
            ++pRead;
            const double kD = cs.albedo[1];
            const double dk = double(cs.radiance[1]) - cs.indirect[1] - cs.flat[1];
            const double scale = kD * kIntensity * 3.14159265358979 * windowAt(px, pz);
            const double fFull = formFactor(px, pz, false), fVis = formFactor(px, pz, true);
            const double full = scale * fFull, truth = scale * fVis;
            const double tol = std::ldexp(double(cs.radiance[1]) + cs.indirect[1] + cs.flat[1], -7) + full / 510.0;
            const double err = std::max(0.0, std::fabs(dk - truth) - tol);
            const double bound = lawBound(px, pz);
            boundMin = std::min(boundMin, bound);
            if (err > bound * full) ++pPast;
            pWorstOfBound = std::max(pWorstOfBound, err / std::max(bound * full, 1e-12));
            if (err / std::max(full, 1e-12) > pWorstFrac) { pWorstFrac = err / std::max(full, 1e-12); pWorstAt = px; }
            if (fVis > 1e-6 * fFull && fVis < (1.0 - 1e-6) * fFull) {   // the band: shaded in part
                const double code = 0.01 * std::max(truth + cs.indirect[1] + cs.flat[1], 5.9604645e-6);
                bandSigned += (dk - truth) / code;
                bandAbs += std::fabs(dk - truth) / code;
                bandErr += dk - truth;
                bandRad += truth + cs.indirect[1] + cs.flat[1];
                ++pBand;
            }
        }
        bandSigned /= double(std::max<size_t>(pBand, 1u));
        bandAbs /= double(std::max<size_t>(pBand, 1u));
        // THE BAND'S MEAN ERROR in codes of its mean radiance (the energy a reader averaging the band sees moved: the
        // estimator's bias) - beside the per-texel codes' mean, which the umbra's end weighs by 1 / its small radiance
        const double bandCodes = pBand && bandRad > 0.0 ? bandErr / (0.01 * bandRad) : 0.0;
        std::printf("   (4) the area light's penumbra: at rest in %d frames; %zu of 103 texels along z = 0 read, %zu in the band; "
                    "n = %u strata a side: the worst error %.2f %% of the unoccluded direct (at x %.3f m), %.3f of the law's "
                    "bound there (the bound %.1f %% at its least), %zu past it; the band's mean error %+.3f codes of its mean "
                    "radiance (per texel: %+.3f codes signed, %.3f unsigned) | one relight's trace: at most %llu rays, GPU %.3f ms the frame's largest (%u frames traced)\n",
                    restP, pRead, pBand, n, 100.0 * pWorstFrac, pWorstAt, pWorstOfBound, 100.0 * boundMin, pPast, bandCodes,
                    bandSigned, bandAbs, (unsigned long long)gp.cards.lampRaysLastTrace, lampMsMax, lampFrames);
        CHECK(restP >= 0 && pRead > 90 && pBand > 40 && pPast == 0u && std::fabs(bandCodes) < 1.0,
              "(4) THE AREA LIGHT'S PENUMBRA: every texel within the strata law's bound, the band's mean under one code");

        // ---- (6) (7) (8) THE PIXEL'S ARMS (PHOTON-7 stage 7, 7-A) ----
        e->setArm("area.visReadback", 1.0);
        const Vec3 eyeP(0.0f, 0.3f, 2.6f), atP(0.0f, 0.0f, -0.5f);
        enginetest::testCameraLookAt(av, eyeP, atP);
        {
            PostFxDesc fx;
            fx.allowOffscreen = true;
            fx.ssr = 1;
            fx.hdrReadback = true;
            av->setPostFx(fx);
        }
        // the trace's and the resolve's GPU time while the view moves in (the frame monitor's rows, a few frames late)
        e->setFrameMonitor(MonitorLevel::Review);
        run(12);
        e->setFrameMonitor(MonitorLevel::Off);
        double areaTraceMs = -1.0, areaResolveMs = -1.0;
        {
            std::vector<MonitorRecord> records;
            while (e->popMonitorRecords(records) > 0) {}
            for (const MonitorRecord &r : records)
                for (const CacheWork &w : r.cacheWork) {
                    if (w.detail == "area.vis" && w.gpuMs >= 0.0f) areaTraceMs = std::max(areaTraceMs, double(w.gpuMs));
                    if (w.detail == "area.resolve" && w.gpuMs >= 0.0f) areaResolveMs = std::max(areaResolveMs, double(w.gpuMs));
                }
        }
        const int restX = settleOn(as);
        const unsigned nX = std::max(as->published().areaVis.strata, 1u);
        run(int(nX * nX) + 16);   // the visibility's rest: every stratum once, then the hold
        const AreaVisStatus avs = as->published().areaVis;
        ImageF pic;
        const bool picOk = av->readPixelsHdr(pic);
        // the camera's projection (testCameraDescLookAt's basis, the default 45-degree vertical field, a square target)
        const auto projectTo = [&](double wx, double wy, double wz, int &ix, int &iy) {
            const double fx0 = atP.x - eyeP.x, fy0 = atP.y - eyeP.y, fz0 = atP.z - eyeP.z;
            const double fl = std::sqrt(fx0 * fx0 + fy0 * fy0 + fz0 * fz0);
            const double f[3] = { fx0 / fl, fy0 / fl, fz0 / fl };
            double r[3] = { f[1] * 0.0 - f[2] * 1.0, f[2] * 0.0 - f[0] * 0.0, f[0] * 1.0 - f[1] * 0.0 };
            const double rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
            for (double &c : r) c /= rl;
            const double u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
            const double d[3] = { wx - eyeP.x, wy - eyeP.y, wz - eyeP.z };
            const double zf = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
            const double tanY = std::tan(22.5 * 3.14159265358979 / 180.0), tanX = tanY * double(pic.width) / double(pic.height);
            const double xn = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / (zf * tanX);
            const double yn = (d[0] * u[0] + d[1] * u[1] + d[2] * u[2]) / (zf * tanY);
            ix = int(std::floor((xn + 1.0) * 0.5 * double(pic.width)));
            iy = int(std::floor((1.0 - yn) * 0.5 * double(pic.height)));
            return zf > 0.0 && ix >= 0 && iy >= 0 && ix < int(pic.width) && iy < int(pic.height);
        };
        // (6) and (7) on the ray tier - and (9) and (10) on the no-ray route (PHOTON-7 stage 7, 7-6): the umbra and the
        // penumbra of the picture `pic` and the read-back visibility `avs`
        const auto pixelArms = [&](const AreaVisStatus &avs, int restX, double areaTraceMs, double areaResolveMs,
                                   bool noRay) {
            // (6) the umbra against the lit, in the picture's radiance (green)
            double pxUmbraSum = 0.0, pxLitSum = 0.0;
            size_t pxUmbraN = 0, pxLitN = 0;
            for (int i = 0; i <= 6; ++i)
                for (int k = -3; k <= 3; ++k) {
                    const double ax = 0.6 + 0.05 * i, az = 0.1 * k;
                    int ux, uy, lx, ly;
                    if (!picOk || !projectTo(-ax, 0.0, az, ux, uy) || !projectTo(ax, 0.0, az, lx, ly)) continue;
                    pxUmbraSum += pic.at(unsigned(ux), unsigned(uy)).g;
                    pxLitSum += pic.at(unsigned(lx), unsigned(ly)).g;
                    ++pxUmbraN;
                    ++pxLitN;
                }
            const double pxUmbraRatio = pxLitSum > 0.0 ? pxUmbraSum / pxLitSum : 1.0;
            std::printf("   %s: the visibility %s (%u lights, n = %u, the trace %ux%u, held %d, %.1f MB; moving: the "
                        "trace %.3f ms, the resolve %.3f ms the frame's largest); %zu umbra / %zu lit floor pixels: the umbra's "
                        "radiance %.4f of the lit's\n",
                        noRay ? "(9) the NO-RAY pixel's umbra" : "(6) the pixel's umbra",
                        avs.running ? "running" : avs.reason.c_str(), avs.lights, avs.strata, avs.traceW, avs.traceH,
                        avs.held ? 1 : 0, double(avs.vramBytes) / 1e6, areaTraceMs, areaResolveMs, pxUmbraN, pxLitN, pxUmbraRatio);
            if (noRay)
                CHECK(restX >= 0 && avs.running && pxUmbraN > 20u && pxUmbraRatio < 0.25,
                      "(9) THE NO-RAY PIXEL BEHIND THE PLATE READS NO DIRECT: the umbra's radiance under a quarter of the lit's");
            else
                CHECK(restX >= 0 && avs.running && pxUmbraN > 20u && pxUmbraRatio < 0.25,
                      "(6) THE PIXEL BEHIND THE PLATE READS NO DIRECT: the umbra's radiance under a quarter of the lit's");
            // (7) the penumbra's visibility against the numeric truth
            const bool visOk = avs.vis.size() == size_t(avs.visW) * avs.visH * 8u && avs.visW == pic.width;
            size_t xRead = 0, xBand = 0, xPast = 0;
            double xWorst = 0.0, xWorstAt = 0.0, xBandSigned = 0.0, xWorstOfBound = 0.0;
            for (int t = -60; t <= 60; ++t) {
                const double px = 0.01 * t, pz = 0.0;
                int ix, iy;
                if (!visOk || !projectTo(px, 0.0, pz, ix, iy)) continue;
                const double vis = avs.vis[(size_t(iy) * avs.visW + size_t(ix)) * 8u] / 255.0;   // byte 0: light 0, diffuse
                const double fFull = formFactor(px, pz, false);
                if (!(fFull > 0.0)) continue;
                // the truth's range over the resolve's footprint: +-1.5 trace texels = +-3 pixels, in world units along x
                const double dx = px - eyeP.x, dy = 0.0 - eyeP.y, dz = pz - eyeP.z;
                const double distW = std::sqrt(dx * dx + dy * dy + dz * dz);
                const double pixW = distW * 2.0 * std::tan(22.5 * 3.14159265358979 / 180.0) / double(pic.height);
                double tLo = 1.0, tHi = 0.0;
                for (int sx = -3; sx <= 3; ++sx) {
                    const double qx = px + pixW * sx;
                    const double t0 = formFactor(qx, pz, true) / std::max(formFactor(qx, pz, false), 1e-12);
                    tLo = std::min(tLo, t0);
                    tHi = std::max(tHi, t0);
                }
                const double truth = formFactor(px, pz, true) / fFull;
                ++xRead;
                const double past = std::max(0.0, std::max(tLo - vis, vis - tHi));
                const double bound = lawBound(px, pz) + 0.5 / 255.0;
                if (past > bound) ++xPast;
                xWorstOfBound = std::max(xWorstOfBound, past / bound);
                if (std::fabs(vis - truth) > xWorst) { xWorst = std::fabs(vis - truth); xWorstAt = px; }
                if (truth > 1e-6 && truth < 1.0 - 1e-6) {
                    ++xBand;
                    xBandSigned += (vis - truth) * 100.0;   // codes of the unoccluded direct (1 % a code)
                }
            }
            xBandSigned /= double(std::max<size_t>(xBand, 1u));
            std::printf("   %s: %zu points along z = 0 read, %zu in the band: the worst |vis - truth| %.3f (at x "
                        "%.2f m), %.3f of the bound past the footprint's range, %zu past; the band's signed mean %+.3f codes of the "
                        "unoccluded direct\n", noRay ? "(10) the NO-RAY pixel's penumbra (printed: its error the voxel cell - the "
                                                 "march's start a cell off the surface - and VOXEL-LEAK-1's thin wall)"
                                               : "(7) the pixel's penumbra", xRead, xBand, xWorst, xWorstAt, xWorstOfBound, xPast, xBandSigned);
            if (!noRay)
                CHECK(visOk && xRead > 80u && xBand > 30u && xPast == 0u && std::fabs(xBandSigned) < 1.0,
                      "(7) THE PIXEL'S PENUMBRA: every point within the strata law's bound, the band's signed mean under one code");
        };
        pixelArms(avs, restX, areaTraceMs, areaResolveMs, false);
        // THE VISIBILITY'S GPU TIME AT 1080p (printed, both producers - one law, so the ray tier's is the no-ray route's
        // reference): a 1920 x 1080 view of the same scene and camera, the monitor's rows "area.vis" and "area.resolve"
        // over its first frames (each frame's, a few frames late)
        const auto time1080 = [&](const char *label) {
            View *hd = e->createOffscreenView("card_area_1080", 1920, 1080, Colour(0, 0, 0));
            double noRayTraceMs = -1.0, noRayResolveMs = -1.0;
            std::vector<double> traceAll, resolveAll, passTrace, passResolve, passIntegrate;
            if (hd) {
                hd->setScene(as);
                PostFxDesc fx;
                fx.allowOffscreen = true;
                fx.ssr = 1;
                hd->setPostFx(fx);
                hd->setOffscreenContract(OffscreenContract::StillPicture);
                enginetest::testCameraLookAt(hd, eyeP, atP);
                e->setFrameMonitor(MonitorLevel::Review);
                run(12);
                e->setFrameMonitor(MonitorLevel::Off);
                std::vector<MonitorRecord> records;
                while (e->popMonitorRecords(records) > 0) {}
                for (const MonitorRecord &r : records) {
                    // (THE NO-RAY ROUTE'S GPU TIME is its two compute passes' own timestamp pairs - the frame's largest of
                    // each: the 1080p view's, beside the small view's held ones)
                    double ft = -1.0, fr = -1.0, fi = -1.0;
                    for (const FramePass &fp : r.passes) {
                        if (fp.pass == "Jahshaka screen integrate" && fp.gpuMs >= 0.0f) fi = std::max(fi, double(fp.gpuMs));
                        if (fp.pass == "Jahshaka screen area visibility" && fp.gpuMs >= 0.0f) ft = std::max(ft, double(fp.gpuMs));
                        if (fp.pass == "Jahshaka screen area visibility resolve" && fp.gpuMs >= 0.0f)
                            fr = std::max(fr, double(fp.gpuMs));
                    }
                    if (ft >= 0.0) passTrace.push_back(ft);
                    if (fr >= 0.0) passResolve.push_back(fr);
                    if (fi >= 0.0) passIntegrate.push_back(fi);
                    for (const CacheWork &w : r.cacheWork) {
                        if (w.detail == "area.vis" && w.gpuMs >= 0.0f) {
                            noRayTraceMs = std::max(noRayTraceMs, double(w.gpuMs));
                            traceAll.push_back(double(w.gpuMs));
                        }
                        if (w.detail == "area.resolve" && w.gpuMs >= 0.0f) {
                            noRayResolveMs = std::max(noRayResolveMs, double(w.gpuMs));
                            resolveAll.push_back(double(w.gpuMs));
                        }
                    }
                }
                e->destroyView(hd);
                run(2);
            }
            const auto median = [](std::vector<double> v) {
                if (v.empty()) return -1.0;
                std::sort(v.begin(), v.end());
                return v[v.size() / 2u];
            };
            const bool passes = !passResolve.empty();   // the no-ray route's passes; the ray tier's rows
            const auto most = [](const std::vector<double> &v) {
                return v.empty() ? -1.0 : *std::max_element(v.begin(), v.end());
            };
            std::printf("   %s at 1080p (one LTC light, n = %u, the trace 960 x 540, %zu frames timed, %s): the trace %.3f ms "
                        "median / %.3f largest, the resolve %.3f median / %.3f largest\n", label, nX,
                        passes ? passResolve.size() : resolveAll.size(),
                        passes ? "its passes' own timestamps" : "its GPU rows area.vis / area.resolve",
                        passes ? median(passTrace) : median(traceAll), passes ? most(passTrace) : noRayTraceMs,
                        passes ? median(passResolve) : median(resolveAll), passes ? most(passResolve) : noRayResolveMs);
            if (passes)
                std::printf("      (beside it, the screen integrate's pass %.3f ms median: a pass's timestamp pair encloses its "
                            "barriers' wait on the compute work in front)\n", median(passIntegrate));
        };
        time1080("(6) the ray tier's visibility");
        // (8) THE GLOSSY LOBE'S OWN RATIO: the floor a glossy metal, the specular visibility read back against the numeric
        PbrParams glossy;
        glossy.albedo = Colour(kAlbedo, kAlbedo, kAlbedo);
        glossy.metalness = 1.0f;
        glossy.roughness = 0.3f;
        as->setPbrMaterial(floorMat, glossy);
        const int restG = settleOn(as);
        run(int(nX * nX) + 16);
        const AreaVisStatus avg = as->published().areaVis;
        const bool visG = avg.vis.size() == size_t(avg.visW) * avg.visH * 8u && avg.visW == pic.width;
        {
            const double rough = 0.3, a = rough * rough, a2 = a * a;
            size_t m = 0, band = 0;
            double worstS = 0.0, worstD = 0.0, bandSigned = 0.0, bandDiffAbs = 0.0;
            for (int t = -50; t <= 50; t += 2) {
                const double px = 0.01 * t, pz = 0.0;
                int ix, iy;
                if (!visG || !projectTo(px, 0.0, pz, ix, iy)) continue;
                const double vx = eyeP.x - px, vy = eyeP.y - 0.0, vz = eyeP.z - pz, vl = std::sqrt(vx * vx + vy * vy + vz * vz);
                const double V[3] = { vx / vl, vy / vl, vz / vl };
                const double NoV = std::max(V[1], 1e-4);
                const int nx = 100, nz = 50;
                const double dA = (aw / nx) * (ah / nz), y = areaAt.y;
                double sAll = 0.0, sVis = 0.0, dAll = 0.0, dVis = 0.0;
                for (int i = 0; i < nx; ++i)
                    for (int k = 0; k < nz; ++k) {
                        const double qx = -0.5 * aw + (i + 0.5) * aw / nx, qz = -0.5 * ah + (k + 0.5) * ah / nz;
                        const double lx = qx - px, ly = y, lz = qz - pz, r2 = lx * lx + ly * ly + lz * lz, rl = std::sqrt(r2);
                        const double L[3] = { lx / rl, ly / rl, lz / rl };
                        const double NoL = L[1], cosL = L[1];   // the floor's +y, the light's -y
                        const double H[3] = { V[0] + L[0], V[1] + L[1], V[2] + L[2] };
                        const double hl = std::sqrt(H[0] * H[0] + H[1] * H[1] + H[2] * H[2]);
                        const double NoH = H[1] / hl;
                        const double dd = NoH * NoH * (a2 - 1.0) + 1.0, D = a2 / (3.14159265358979 * dd * dd);
                        const double G2 = 2.0 * NoL * NoV / (NoV * std::sqrt(a2 + (1.0 - a2) * NoL * NoL) +
                                                             NoL * std::sqrt(a2 + (1.0 - a2) * NoV * NoV));
                        const double wSpec = D * G2 / (4.0 * NoV) * cosL / r2 * dA;
                        const double wDiff = NoL * cosL / r2 * dA;
                        const bool blocked = !plate.empty() &&
                                             crosses(Vec3(float(px), 0.0f, float(pz)), Vec3(float(qx), float(y), float(qz)),
                                                     plate[0], plate[1]);
                        sAll += wSpec;
                        dAll += wDiff;
                        if (!blocked) { sVis += wSpec; dVis += wDiff; }
                    }
                if (!(sAll > 0.0) || !(dAll > 0.0)) continue;
                const double truthS = sVis / sAll, truthD = dVis / dAll;
                const size_t at = (size_t(iy) * avg.visW + size_t(ix)) * 8u;
                const double visS = avg.vis[at + 4u] / 255.0;   // byte 4: light 0, specular
                ++m;
                worstS = std::max(worstS, std::fabs(visS - truthS) * 100.0);
                worstD = std::max(worstD, std::fabs(truthD - truthS) * 100.0);
                if (truthS > 1e-6 && truthS < 1.0 - 1e-6) {
                    ++band;
                    bandSigned += (visS - truthS) * 100.0;
                    bandDiffAbs += std::fabs(truthD - truthS) * 100.0;
                }
            }
            bandSigned /= double(std::max<size_t>(band, 1u));
            bandDiffAbs /= double(std::max<size_t>(band, 1u));
            std::printf("   (8) the glossy lobe (perceptual 0.3 metal; at rest in %d frames): %zu points, %zu in the band: the "
                        "specular visibility's worst error %.2f codes of the unoccluded specular, the band's signed mean %+.3f; "
                        "the diffuse ratio on the same points: worst %.2f, the band's mean %.2f codes\n",
                        restG, m, band, worstS, bandSigned, worstD, bandDiffAbs);
            CHECK(restG >= 0 && visG && m > 40u && band > 20u && std::fabs(bandSigned) < 1.0,
                  "(8) THE GLOSSY LOBE TAKES ITS OWN RATIO: the specular visibility's band signed mean under one code");
        }
        e->setArm("area.visReadback", 0.0);

        // (5) PRINTED: THE BOUNCE THE VOXELS' CENTRE MARCH LEAVES (the lead's ruling: measured before the voxels change).
        // The back wall's card indirect - the bounce it gathers from the store, the floor's penumbra in its view - with
        // the voxels' area light visibility one march to the rectangle's centre (shipped) against the same store with
        // 16 x 16 ratio marches a half-face (the measuring arm gi.voxelAreaStrata), the GI rebuilt whole between: the
        // difference in codes of the wall texel's radiance.
        const auto readWall = [&](std::vector<double> &ind, std::vector<double> &rad) {
            ind.clear();
            rad.clear();
            for (int i = -8; i <= 8; ++i)
                for (int k = 1; k <= 15; ++k) {
                    CardSample cs;
                    const bool ok = as->readCardAt(Vec3(0.1f * float(i), 0.1f * float(k), -1.0f), Vec3(0, 0, 1), cs) && cs.ok &&
                                    cs.lit && cs.indirect[1] > 0.0f;
                    ind.push_back(ok ? double(cs.indirect[1]) : -1.0);
                    rad.push_back(ok ? double(cs.radiance[1]) : -1.0);
                }
        };
        std::vector<double> indA, radA, indB, radB;
        readWall(indA, radA);
        e->setArm("gi.voxelAreaStrata", 16.0);
        as->setGlobalIllumination(off);
        run(2);
        as->setGlobalIllumination(gi);
        e->waitForAsyncShaders();
        const int restV = settleOn(as);
        readWall(indB, radB);
        e->setArm("gi.voxelAreaStrata", 0.0);
        size_t vN = 0, vOver = 0;
        double vWorst = 0.0, vMean = 0.0, vSigned = 0.0;
        for (size_t k = 0; k < indA.size(); ++k) {
            if (indA[k] < 0.0 || indB[k] < 0.0) continue;
            const double codes = (indA[k] - indB[k]) / (0.01 * std::max(radB[k], 5.9604645e-6));
            ++vN;
            vWorst = std::max(vWorst, std::fabs(codes));
            vMean += std::fabs(codes);
            vSigned += codes;
            vOver += std::fabs(codes) > 1.0 ? 1u : 0u;
        }
        vMean /= double(std::max<size_t>(vN, 1u));
        vSigned /= double(std::max<size_t>(vN, 1u));
        std::printf("   (5) the voxels' centre march against 16 x 16 ratio marches (printed): the many-ray store at rest in %d "
                    "frames; the back wall's bounce at %zu of %zu points: worst %.3f codes of its radiance, mean %.3f "
                    "(signed %+.3f), %zu over one code\n", restV, vN, indA.size(), vWorst, vMean, vSigned, vOver);

        // ---- (9) (10) THE NO-RAY ROUTE'S PIXEL (PHOTON-7 stage 7, 7-6): rays off, the same half-occluded light, the
        // matte floor of (6) and (7) again - the visibility the screen job marches through the voxel chain, read back.
        // RED before 7-6 item 2: a no-ray chain bound no visibility, and its umbra read the lit floor's direct.
        e->setRayTracing(false);
        {
            PbrParams matte;
            matte.albedo = Colour(kAlbedo, kAlbedo, kAlbedo);
            matte.roughness = 0.9f;
            as->setPbrMaterial(floorMat, matte);
        }
        e->setArm("area.visReadback", 1.0);
        as->setGlobalIllumination(off);
        run(2);
        as->setGlobalIllumination(gi);
        e->waitForAsyncShaders();
        // (no cards without rays: the chain's rest alone, 60 quiet frames - then the visibility's own rest and hold)
        int restN = -1;
        for (int f = 0, quiet = 0; f < 1500; ++f) {
            run(1);
            const GiStatus g = as->published().gi;
            quiet = !g.chainOwed && !g.feedbackOwed && !g.cascades.empty() ? quiet + 1 : 0;
            if (quiet >= 60) { restN = f - quiet; break; }
        }
        run(int(nX * nX) + 16);
        const AreaVisStatus avn = as->published().areaVis;
        const bool picN = av->readPixelsHdr(pic);
        pixelArms(avn, picN ? restN : -1, -1.0, -1.0, true);
        time1080("(9) the no-ray visibility");
        e->setArm("area.visReadback", 0.0);
        e->setRayTracing(true);
        as->setGlobalIllumination(off);
        run(2);
    }
    std::printf("\nframes %d\n%s (%d failure%s)\n", frames, failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
