// gi.cards_mover_shadow — A MOVER'S SHADOW ON CACHED SURFACES (PHOTON-CARDS-4).
//
// WHAT WAS WRONG (measured on the base, spikes/photon-cards-4/EVIDENCE-premise.txt):
// a card's sun visibility was the term its CAPTURE stored, and the capture's
// shadow node draws the still world only (a mover carries kMovableBit instead of
// kVisibleBit). So every cached read of a floor under a mover — a mirror's hit,
// the gather's hit, a card-lit cone — saw the floor LIT: raster shadow/lit 0.140,
// mirror 0.9997; a forced relight and a forced recapture both left the card at
// shadow 1.000. And a STILL caster that moved left its shadow behind in the
// receivers' cards (the old footprint 0.000 after 60 frames, the new one 1.000).
//
// THE RULE (OgreSurfaceCache.cpp, "The movers' shadow"): the still world's sun
// visibility stays the captured term; the MOVERS' occlusion is traced — a
// shadow-casting mover's transform write traces one sun ray per texel of the
// cards inside its sun-projected footprint (old and new) against the movers
// alone, in the frame of the write, and relights them; a still caster's
// transform write RECAPTURES the cards of its old and new footprints.
//
// THE ARMS (frames, never time), one process:
//   (a) the floor under a mover reads DARK in a perfect mirror where the raster
//       is dark: the umbra's shadow/lit ratio in the mirror within the radiance
//       store's quantum of the raster's (the SKIN-1 mirror method; GI off and no
//       ambient, so the ratio is the DIRECT term's — the trap "a mirror
//       fixture's raster is seen with the mirror hidden": no bounce off the
//       wall reaches the mirror shots only).
//   (b) the mover slides 2 m over 20 frames: on the frame after every write the
//       card texel the shadow just ENTERED reads dark and the one it just LEFT
//       reads lit (latency: 0 frames after the write's frame); the mirror after
//       the slide is printed.
//   (c) the mover stops: `relights`, `moverTraces` and `captures` stay constant
//       for 30 frames.
//   (d) a mover whose sun-projected footprint misses every card moves 20
//       frames: no trace, no relight.
//   (f) a STILL crate moved 3 m: the old footprint reads lit and the new one
//       dark within the capture budget's frames for the cards it queued.
//
// `--cost` (not a ctest row; run under scripts/gpu-exclusive.sh): paired arms
// of 0 / 1 / 30 moving movers on a Showroom-2-shaped floor at 1920x1080: the
// trace's and the relight's GPU milliseconds and the cards they touch a frame.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

static const unsigned kSize = 384;
static const float kMirrorZ = 4.85f;
/// THE STORE'S QUANTUM on a ratio to the lit value: R11G11B10F keeps 6 mantissa
/// bits on red and green (5 on blue), and the relight rounds to nearest — a
/// value is within half a step, 2^-7 of itself, and a ratio of two such values
/// within 2^-6. The bar is one step of the lit value: 2^-6.
static const float kStoreQuantum = 1.0f / 64.0f;

static float lum(const ImageF &img, unsigned x, unsigned y) {
    const Colour c = img.at(x, y);
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

static MaterialId matte(Scene *s, float albedo) {
    PbrParams p;
    p.albedo = Colour(albedo, albedo, albedo);
    p.roughness = 0.9f;
    return s->createPbrMaterial(p);
}

/// The floor texel's lit radiance (green channel) at a world point on its top face.
static float cardRadiance(Scene *s, float x, float z, bool *ok = nullptr) {
    CardSample cs;
    const bool got = s->readCardAt(Vec3(x, 0.0f, z), Vec3(0, 1, 0), cs) && cs.ok;
    if (ok) *ok = got;
    return got ? cs.radiance[1] : -1.0f;
}
static float cardShadow(Scene *s, float x, float z) {
    CardSample cs;
    return (s->readCardAt(Vec3(x, 0.0f, z), Vec3(0, 1, 0), cs) && cs.ok) ? cs.shadow : -1.0f;
}

static int costMain(Engine *e);

int main(int argc, char **argv)
{
    const bool wantCost = argc > 1 && !std::strcmp(argv[1], "--cost");
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = wantCost ? "test-gi-cards-mover-cost-ogre.log" : "test-gi-cards-mover-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    if (wantCost) return costMain(e);

    View *view = e->createOffscreenView("cardmover", kSize, kSize, Colour(0, 0, 0));
    Scene *s = e->createScene("cardmover");
    view->setScene(s);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries — the movers' term needs the ray tier; the arm skips\n");
        return 0;
    }
    // NO AMBIENT, NO GI: every pixel of the floor is the sun's direct term, so
    // the umbra is black in the raster and in any honest cached read of it.
    s->setAmbient(Colour(0.0f, 0.0f, 0.0f), Colour(0.0f, 0.0f, 0.0f));

    const NodeId wall = s->createNode();
    {
        PbrParams wp;
        wp.albedo = Colour(1, 1, 1);
        wp.metalness = 1.0f;
        wp.roughness = 0.0f;
        s->attachMesh(wall, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(wp));
        enginetest::setNodeScale(s, wall, Vec3(14.0f, 9.0f, 0.3f));
        enginetest::setNodePosition(s, wall, Vec3(0.0f, 3.0f, kMirrorZ + 0.15f));
    }
    // THE CARDED FLOOR: 12 m x 12 m, its top at y = 0, x in [-6, 6], z in [-7.3, 4.7].
    const NodeId floorNode = s->createNode();
    {
        MeshData md = enginetest::unitCubeMesh();
        md.cards = enginetest::boxCards(0.5f);
        s->attachMesh(floorNode, s->createMesh(md), matte(s, 0.5f));
        enginetest::setNodeScale(s, floorNode, Vec3(12.0f, 0.2f, 12.0f));
        enginetest::setNodePosition(s, floorNode, Vec3(0.0f, -0.1f, -1.3f));
    }
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    // THE MOVER: a 1.2 m crate, a red emissive marker so its pixels are never floor.
    PbrParams cp;
    cp.albedo = Colour(0.05f, 0.05f, 0.05f);
    cp.emissive = Colour(1.0f, 0.0f, 0.0f);
    cp.roughness = 0.6f;
    const MaterialId crateMat = s->createPbrMaterial(cp);
    const NodeId crate = s->createNode();
    s->setNodeMovable(crate, true);
    s->attachMesh(crate, cube, crateMat);
    enginetest::setNodeScale(s, crate, Vec3(1.2f, 1.2f, 1.2f));
    float crateZ = -1.0f;
    enginetest::setNodePosition(s, crate, Vec3(-0.6f, 0.6f, crateZ));
    // THE STILL CRATE for (f): its shadow is the captured term's.
    const NodeId still = s->createNode();
    s->attachMesh(still, cube, crateMat);
    enginetest::setNodeScale(s, still, Vec3(1.2f, 1.2f, 1.2f));
    enginetest::setNodePosition(s, still, Vec3(-0.6f, 0.6f, -5.0f));
    // THE SUN from -X at 45 degrees: a crate's shadow runs +X from x = 0 to 1.2
    // across the crate's own z extent.
    enginetest::addDirectionalLight(s, Vec3(1.0f, -1.0f, 0.0f), 3.0f);
    view->setShadows(true);

    GiParams gi;
    gi.mode = GiMode::Off;
    gi.quality = GiQuality::High;
    gi.cards = GiToggle::On;
    gi.cardResidencyRadius = 40.0f;
    s->setGlobalIllumination(gi);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    fx.ssrScreenMarch = false;       // rays only: the floor under the crate is behind the camera
    fx.hdr = false;
    fx.hdrReadback = true;
    view->setPostFx(fx);

    const Vec3 cam(0.0f, 1.5f, 2.0f);
    const Vec3 camR(0.0f, 1.5f, 2.0f * kMirrorZ - cam.z);   // reflected in the mirror plane
    const Vec3 look(0.0f, 0.7f, kMirrorZ);                   // on the plane: its own reflection
    const auto aim = [&](bool mirror) {
        s->setNodeVisible(wall, mirror);
        enginetest::testCameraLookAt(view, mirror ? cam : camR, look);
    };
    const auto grab = [&]() {
        ImageF img;
        view->readPixelsHdr(img);
        return img;
    };
    const auto shot = [&](bool mirror, bool showCrate, int frames) {
        aim(mirror);
        s->setNodeVisible(crate, showCrate);
        render(e, frames);
        return grab();
    };
    render(e, 90);
    {
        const GiStatus st = s->giStatus();
        CHECK_MSG(st.cards.built && st.cards.cardsResident > 0u, "the floor's cards are resident (%u)",
                  st.cards.cardsResident);
    }

    // THE UMBRA MASK, from the RASTER through the reflected camera (x flipped into
    // the mirror's image): a floor pixel (not the red crate) at under half its
    // crate-less value, whose 5x5 neighbourhood is all such — the umbra's
    // interior, so the PSSM filter's penumbra and the trace's hard edge are
    // both outside it.
    // A READ SPECK: an umbra pixel whose MIRROR reads at least half its lit value
    // — the card read picking a lit texel for that pixel's hit. It is the read's
    // own rate (a STILL crate's umbra shows it too, arm (a)'s control), counted
    // apart and held to the control's rate; the ratio is over the rest.
    struct Ratio { unsigned px = 0, specks = 0; double raster = -1.0, mirror = -1.0, rasterLit = 0.0, mirrorLit = 0.0; };
    const auto measure = [&](const ImageF &rOn, const ImageF &rOff, const ImageF &mOn, const ImageF &mOff) {
        std::vector<unsigned char> dark(size_t(kSize) * kSize, 0u);
        for (unsigned y = 0; y < kSize; ++y)
            for (unsigned x = 0; x < kSize; ++x) {
                const float la = lum(rOn, x, y), lb = lum(rOff, x, y);
                if (!(lb > 1e-4f) || la > 0.5f * lb || rOn.at(x, y).r > 0.5f) continue;
                dark[size_t(y) * kSize + x] = 1u;
            }
        Ratio r;
        double rs = 0, ro = 0, ms = 0, mo = 0;
        for (unsigned y = 2; y + 2 < kSize; ++y)
            for (unsigned x = 2; x + 2 < kSize; ++x) {
                bool inner = true;
                for (int dy = -2; dy <= 2 && inner; ++dy)
                    for (int dx = -2; dx <= 2 && inner; ++dx)
                        inner = dark[size_t(int(y) + dy) * kSize + unsigned(int(x) + dx)] != 0u;
                if (!inner) continue;
                const unsigned mx = kSize - 1u - x;
                if (lum(mOn, mx, y) >= 0.5f * lum(mOff, mx, y)) { ++r.specks; continue; }
                rs += lum(rOn, x, y); ro += lum(rOff, x, y);
                ms += lum(mOn, mx, y); mo += lum(mOff, mx, y);
                ++r.px;
            }
        if (r.px && ro > 0.0 && mo > 0.0) {
            r.raster = rs / ro;
            r.mirror = ms / mo;
            r.rasterLit = ro / r.px;
            r.mirrorLit = mo / r.px;
        }
        return r;
    };

    // ---- (a) THE UMBRA IN THE MIRROR -----------------------------------------
    std::printf("\n== (a) the floor under a mover, in a perfect mirror and in the raster\n");
    Ratio a, c;
    {
        // The mover's umbra (the still crate hidden), the still crate's umbra —
        // the control, a shadow the CAPTURE holds (the mover hidden) — and the
        // floor with neither. Each 40 frames: a hidden still caster recaptures
        // its footprint (the finding-3 rule), within 4 frames at this budget.
        s->setNodeVisible(still, false);
        const ImageF mOn = shot(true, true, 40);
        const ImageF rOn = shot(false, true, 40);
        // THE CONTROL stands where the mover stood (its umbra covers the same
        // pixels): a still crate moved in, recaptured into the floor's cards.
        s->setNodeVisible(still, true);
        enginetest::setNodePosition(s, still, Vec3(-0.6f, 0.6f, crateZ));
        const ImageF mCtl = shot(true, false, 40);
        const ImageF rCtl = shot(false, false, 40);
        s->setNodeVisible(still, false);
        const ImageF mOff = shot(true, false, 40), rOff = shot(false, false, 40);
        s->setNodeVisible(still, true);
        enginetest::setNodePosition(s, still, Vec3(-0.6f, 0.6f, -5.0f));
        a = measure(rOn, rOff, mOn, mOff);
        c = measure(rCtl, rOff, mCtl, mOff);
        std::printf("    MOVER umbra %u px (+%u read specks): raster shadow/lit %.4f, mirror %.4f "
                    "(lit: raster %.4f, mirror %.4f)\n", a.px, a.specks, a.raster, a.mirror, a.rasterLit,
                    a.mirrorLit);
        std::printf("    STILL umbra %u px (+%u read specks): raster shadow/lit %.4f, mirror %.4f (control)\n",
                    c.px, c.specks, c.raster, c.mirror);
        CHECK_MSG(a.px > 200u, "the raster shows the mover's umbra on the floor (%u px)", a.px);
        CHECK_MSG(a.px && std::fabs(a.mirror - a.raster) <= kStoreQuantum,
                  "(a) the mirror's umbra is the raster's: shadow/lit %.4f vs %.4f (bar: the store's quantum %.4f)",
                  a.mirror, a.raster, kStoreQuantum);
        const double rateA = double(a.specks) / double(std::max(1u, a.px + a.specks));
        const double rateC = double(c.specks) / double(std::max(1u, c.px + c.specks));
        CHECK_MSG(c.px > 200u && rateA <= rateC + 0.02,
                  "(a) the mover's umbra shows the card read's specks at the still control's rate "
                  "(%.1f %% vs %.1f %%, bar +2 points)", 100.0 * rateA, 100.0 * rateC);
    }
    const GiStatus afterA = s->giStatus();
    std::printf("    movers %u, traces %llu, retired %llu, relights %llu\n", afterA.cards.moverCasters,
                (unsigned long long)afterA.cards.moverTraces, (unsigned long long)afterA.cards.moverRetired,
                (unsigned long long)afterA.cards.relights);

    // ---- (b) THE SLIDE ------------------------------------------------------
    // 2 m along +Z over 20 frames, 0.1 m a write. The crate spans z +- 0.6: after
    // the write to zc, the band (zc + 0.5, zc + 0.6] was just ENTERED and the band
    // (zc - 0.7, zc - 0.6] just LEFT — probed at their centres, x = 0.6 (the
    // shadow's middle across).
    std::printf("\n== (b) the mover slides 2 m over 20 frames\n");
    aim(true);
    s->setNodeVisible(crate, true);
    render(e, 40);
    const float litRef = cardRadiance(s, 3.0f, crateZ);
    std::printf("    lit reference texel radiance %.4f\n", litRef);
    unsigned darkLate = 0, litLate = 0;
    float worstEnter = 0.0f, worstLeft = 1e9f;
    for (int f = 0; f < 20; ++f) {
        crateZ += 0.1f;
        enginetest::setNodePosition(s, crate, Vec3(-0.6f, 0.6f, crateZ));
        render(e, 1);   // THE FRAME AFTER THE WRITE
        const float entered = cardRadiance(s, 0.6f, crateZ + 0.55f);
        const float left = cardRadiance(s, 0.6f, crateZ - 0.65f);
        worstEnter = std::max(worstEnter, entered / litRef);
        worstLeft = std::min(worstLeft, left / litRef);
        if (entered > kStoreQuantum * litRef) ++darkLate;
        if (std::fabs(left - litRef) > kStoreQuantum * litRef) ++litLate;
    }
    CHECK_MSG(litRef > 0.05f, "the card lights the floor (%.4f)", litRef);
    CHECK_MSG(darkLate == 0u,
              "(b) the texel the shadow ENTERED reads dark on the frame after every write (worst %.4f of lit; "
              "%u of 20 late)", worstEnter, darkLate);
    CHECK_MSG(litLate == 0u,
              "(b) the texel the shadow LEFT reads lit on the frame after every write (worst %.4f of lit; "
              "%u of 20 late)", worstLeft, litLate);
    {
        // THE MIRROR on the frame after the last write, against the settled raster
        // at the final position (the reflection's own temporal filter included).
        const ImageF mNow = grab();
        const ImageF mOff = shot(true, false, 40);
        const ImageF rOn = shot(false, true, 40), rOff = shot(false, false, 40);
        const Ratio b = measure(rOn, rOff, mNow, mOff);
        std::printf("    the mirror on the frame after the last write: umbra %u px, shadow/lit %.4f "
                    "(raster %.4f)\n", b.px, b.mirror, b.raster);
        CHECK_MSG(b.px > 200u && std::fabs(b.mirror - b.raster) <= kStoreQuantum,
                  "(b) the mirror's shadow followed: %.4f vs the raster's %.4f one frame after the last write",
                  b.mirror, b.raster);
        aim(true);
        s->setNodeVisible(crate, true);
        render(e, 20);
    }

    // ---- (c) THE MOVER STOPS -------------------------------------------------
    std::printf("\n== (c) the mover at rest\n");
    {
        const GiStatus s0 = s->giStatus();
        render(e, 30);
        const GiStatus s1 = s->giStatus();
        std::printf("    relights %llu -> %llu, traces %llu -> %llu, captures %llu -> %llu\n",
                    (unsigned long long)s0.cards.relights, (unsigned long long)s1.cards.relights,
                    (unsigned long long)s0.cards.moverTraces, (unsigned long long)s1.cards.moverTraces,
                    (unsigned long long)s0.cards.captures, (unsigned long long)s1.cards.captures);
        CHECK_MSG(s1.cards.relights == s0.cards.relights && s1.cards.moverTraces == s0.cards.moverTraces &&
                      s1.cards.captures == s0.cards.captures,
                  "(c) a mover at rest costs nothing for 30 frames (relights, traces, captures constant)");
    }

    // ---- (d) A FOOTPRINT THAT MISSES EVERY CARD ------------------------------
    // Off the floor's +X edge (x = 6) with the sun throwing its shadow further
    // +X: nothing the cache holds lies in it.
    std::printf("\n== (d) a mover whose footprint misses every card\n");
    {
        const NodeId far = s->createNode();
        s->setNodeMovable(far, true);
        s->attachMesh(far, cube, crateMat);
        enginetest::setNodeScale(s, far, Vec3(1.2f, 1.2f, 1.2f));
        enginetest::setNodePosition(s, far, Vec3(9.0f, 0.6f, -2.0f));
        render(e, 30);
        const GiStatus s0 = s->giStatus();
        unsigned traced = 0;
        for (int f = 0; f < 20; ++f) {
            enginetest::setNodePosition(s, far, Vec3(9.0f, 0.6f, -2.0f + 0.1f * float(f + 1)));
            render(e, 1);
            traced += s->giStatus().cards.moverTracedLastFrame;
        }
        const GiStatus s1 = s->giStatus();
        std::printf("    movers %u; traces %llu -> %llu, relights %llu -> %llu\n", s1.cards.moverCasters,
                    (unsigned long long)s0.cards.moverTraces, (unsigned long long)s1.cards.moverTraces,
                    (unsigned long long)s0.cards.relights, (unsigned long long)s1.cards.relights);
        CHECK_MSG(s1.cards.moverCasters == 2u, "both movers are traced casters (%u)", s1.cards.moverCasters);
        CHECK_MSG(traced == 0u && s1.cards.moverTraces == s0.cards.moverTraces &&
                      s1.cards.relights == s0.cards.relights,
                  "(d) 20 writes of a mover whose footprint misses every card relight nothing (%u traced)",
                  traced);
    }

    // ---- (f) A STILL CRATE MOVED 3 m ------------------------------------------
    std::printf("\n== (f) a still crate moved 3 m\n");
    {
        const float oldZ = -5.0f, newZ = -2.0f;
        const float before = cardShadow(s, 0.6f, oldZ);
        CHECK_MSG(before >= 0.0f && before < 0.1f, "the still crate's shadow is captured (%.3f)", before);
        const GiStatus s0 = s->giStatus();
        enginetest::setNodePosition(s, still, Vec3(-0.6f, 0.6f, newZ));
        int frames = -1;
        for (int f = 1; f <= 60; ++f) {
            render(e, 1);
            if (cardShadow(s, 0.6f, oldZ) > 0.9f && cardShadow(s, 0.6f, newZ) >= 0.0f &&
                cardShadow(s, 0.6f, newZ) < 0.1f) { frames = f; break; }
        }
        const GiStatus s1 = s->giStatus();
        const unsigned long long queued = s1.cards.casterRecaptures - s0.cards.casterRecaptures;
        const unsigned perFrame = std::max(1u, s1.cards.budgetTexels / (128u * 128u));
        const int bar = int((queued + perFrame - 1u) / perFrame) + 2;
        std::printf("    %llu cards queued for recapture (%u a frame at this budget): old lit / new dark "
                    "after %d frames (bar %d)\n", queued, perFrame, frames, bar);
        CHECK_MSG(queued > 0u, "the move queued its footprints' cards (%llu)", queued);
        CHECK_MSG(frames > 0 && frames <= bar,
                  "(f) the old footprint reads lit and the new one dark within the capture budget's "
                  "frames (%d, bar %d)", frames, bar);
    }

    std::printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// --cost: 0 / 1 / 30 moving movers on a Showroom-2-shaped floor (sc1b_measure's
// shape: a 24 m carded floor, four walls, forty still props, the sun), paired
// arms in one process, 1920x1080, High. Every mover in the moving arm slides
// every frame; the arms alternate in rounds so drift hits all three alike.
static int costMain(Engine *e)
{
    View *view = e->createOffscreenView("cardmovercost", 1920u, 1080u, Colour(0, 0, 0));
    Scene *s = e->createScene("cardmovercost");
    view->setScene(s);
    if (!e->rayQueryAvailable() || !e->rayTracing()) { std::printf("cost: no ray queries\n"); return 0; }
    view->setShadows(true);
    s->setAmbient(Colour(0.12f, 0.12f, 0.14f), Colour(0.08f, 0.08f, 0.10f));
    MeshData md = enginetest::unitCubeMesh();
    md.cards = enginetest::boxCards(0.5f);
    const MeshId carded = s->createMesh(md);
    const MeshId plain = s->createMesh(enginetest::unitCubeMesh());
    {
        const NodeId n = s->createNode();
        s->attachMesh(n, carded, matte(s, 0.7f));
        enginetest::setNodeScale(s, n, Vec3(24.0f, 0.2f, 24.0f));
        enginetest::setNodePosition(s, n, Vec3(0, -0.1f, 0));
    }
    const MaterialId wallMat = matte(s, 0.6f);
    for (int w = 0; w < 4; ++w) {
        const NodeId n = s->createNode();
        s->attachMesh(n, carded, wallMat);
        const bool alongX = (w & 1) == 0;
        enginetest::setNodeScale(s, n, alongX ? Vec3(24.0f, 5.0f, 0.3f) : Vec3(0.3f, 5.0f, 24.0f));
        enginetest::setNodePosition(s, n, alongX ? Vec3(0.0f, 2.5f, (w == 0 ? 12.0f : -12.0f))
                                                 : Vec3((w == 1 ? 12.0f : -12.0f), 2.5f, 0.0f));
    }
    const MaterialId propMat = matte(s, 0.5f);
    for (int i = 0; i < 40; ++i) {
        const NodeId n = s->createNode();
        s->attachMesh(n, carded, propMat);
        enginetest::setNodeScale(s, n, Vec3(1.6f, 1.6f, 1.6f));
        enginetest::setNodePosition(s, n, Vec3(float(i % 8) * 2.8f - 9.8f, 0.8f, float(i / 8) * 2.8f - 7.0f));
    }
    {
        const NodeId sun = s->createNode();
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = 2.0f / 3.14159265358979323846f;
        l.castShadows = true;
        s->setNodeTransform(sun, Vec3(0, 0, 0), Quat(0.2f, 0.0f, 0.0f, 0.98f), Vec3(1, 1, 1));
        s->setLight(sun, l);
    }
    // THIRTY MOVERS: 0.8 m crates in the aisles between the props.
    const MaterialId moverMat = matte(s, 0.4f);
    std::vector<NodeId> movers;
    std::vector<Vec3> home;
    for (int i = 0; i < 30; ++i) {
        const NodeId n = s->createNode();
        s->setNodeMovable(n, true);
        s->attachMesh(n, plain, moverMat);
        enginetest::setNodeScale(s, n, Vec3(0.8f, 0.8f, 0.8f));
        const Vec3 p(float(i % 6) * 2.8f - 8.4f, 0.4f, float(i / 6) * 2.8f - 5.6f);
        enginetest::setNodePosition(s, n, p);
        movers.push_back(n);
        home.push_back(p);
    }
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.cards = GiToggle::On;
    gi.testBoundsMin = Vec3(-26.0f, -2.0f, -26.0f);
    gi.testBoundsMax = Vec3(26.0f, 14.0f, 26.0f);
    s->setGlobalIllumination(gi);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    view->setPostFx(fx);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 6.0f, -18.0f), Vec3(0.0f, 1.0f, 0.0f));
    render(e, 240);   // the compile storm, the captures, the settle

    const int arms[3] = { 0, 1, 30 };
    double traceMs[3] = {}, relightMs[3] = {}, traced[3] = {}, relit[3] = {}, texels[3] = {};
    int traceN[3] = {}, relightN[3] = {}, frames[3] = {};
    int tick = 0;
    for (int round = 0; round < 4; ++round)
        for (int a = 0; a < 3; ++a) {
            for (int f = 0; f < 24; ++f) {
                ++tick;
                for (int m = 0; m < arms[a]; ++m) {
                    const float dz = 0.4f * std::sin(0.15f * float(tick) + float(m));
                    enginetest::setNodePosition(s, movers[size_t(m)],
                                                Vec3(home[size_t(m)].x, home[size_t(m)].y, home[size_t(m)].z + dz));
                }
                e->renderOneFrame();
                if (f < 6) continue;   // the arm's first frames carry the last arm's timestamps
                const CardCacheStatus c = s->giStatus().cards;
                traced[a] += c.moverTracedLastFrame;
                texels[a] += c.moverTexelsLastFrame;
                relit[a] += c.relitLastFrame;
                ++frames[a];
                if (arms[a] && c.moverGpuMs >= 0.0f) { traceMs[a] += c.moverGpuMs; ++traceN[a]; }
                if (c.relightGpuMs >= 0.0f && c.relitLastFrame) { relightMs[a] += c.relightGpuMs; ++relightN[a]; }
            }
        }
    std::printf("cost: 1920x1080, High, Showroom-2-shaped floor; %u cards resident\n",
                s->giStatus().cards.cardsResident);
    std::printf("cost: arm        traced cards/f  traced texels/f  trace GPU ms  relit cards/f  relight GPU ms\n");
    for (int a = 0; a < 3; ++a)
        std::printf("cost: %2d movers  %14.2f  %15.0f  %12.4f  %13.2f  %14.4f\n", arms[a],
                    frames[a] ? traced[a] / frames[a] : 0.0, frames[a] ? texels[a] / frames[a] : 0.0,
                    traceN[a] ? traceMs[a] / traceN[a] : 0.0, frames[a] ? relit[a] / frames[a] : 0.0,
                    relightN[a] ? relightMs[a] / relightN[a] : 0.0);
    std::printf("cost: pending past the budget at the end: %u\n", s->giStatus().cards.moverPending);
    return 0;
}
