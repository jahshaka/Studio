// gi.ddgi_edge — THE IRRADIANCE FIELD'S EDGE (ENGINE_CACHE_POLICY_SPEC P9).
//
// THE DEFECT this suite gates. The field's world->grid map has no clamp
// (OgreIrradianceField.cpp:794-812) and the cage loop's own comment promises a
// clamp it never performs (upstream IrradianceField_piece_ps.any:130-140, and
// our copy of that body in JahIfd_piece_ps.any). So a surface OUTSIDE the
// field's volume indexes past the probe grid, wraps into the interior rows, and
// is painted with a blurred picture of the room inside: the owner's report was
// "the Grand Showroom 2 roof shows texture maps", and the diagnosis
// (spikes/showroom2-roof/) measured a 48 m roof slab carrying a mean 21.0 with a
// spread of 20 levels where `ddgi:false` gives a flat 63.6 +- 1.
//
// THE SCENE. A SEALED BOX whose GI bounds top sits INSIDE its 0.5 m ceiling slab
// (bounds top 5.25, slab 5.0-5.5) — the worst case, and exactly the shape the
// automatic bounds produce today. Nothing outside the box is lit by anything but
// the ambient: the only light is a point light sealed inside. So every exterior
// reading below is a reading of what the irradiance field leaks.
//
// WHAT IT ASSERTS, and each is a measurement the base binary fails:
//   1. FLAT ROOF — the roof's outer face, sampled over a ring, varies by at most
//      2/255 with the field ON (the base binary: tens of levels).
//   2. THE FALLBACK — that roof's mean is the SAME as with the field off. A
//      surface outside the field must fall back to what it had before the field
//      existed, which here is VCT's ambient-escape term (the field's binding
//      sets VctDisableDiffuse and deletes it). Fading the field to zero without
//      restoring that term would leave the roof flat but BLACK, and this
//      assertion is what refuses that answer.
//   3. TWO GREY CUBES, one 2 m above the volume and one 18 m outside a wall:
//      neutral (no interior chroma) and equal to their field-off readings.
//   4. THE INTERIOR IS UNTOUCHED — the room's own bounce is still there, still
//      spatially varying, and still red where the red wall bounces onto the
//      floor. (The cross-binary proof that the interior did not change is the
//      rest of the gi.ddgi* suites plus this suite's printed interior numbers.)
//   5. THE ENCLOSING-BOUNDS VARIANT — bounds raised so the whole shell is INSIDE
//      the field (what the scene-side auto-bounds fix would produce). MEASURED,
//      NOT GATED, and that is the verification the lead asked for: the Chebyshev
//      term does NOT crush through-slab probes (the cage of a roof pixel whose
//      probes are embedded in the ceiling slab measures above 0.999 visible), so
//      the leak returns when the volume swallows the shell. Only what is
//      genuinely outside the enlarged volume is asserted there.
//
// Its OWN binary, like every other gi.ddgi* suite: the field binds PROCESS-WIDE
// to HlmsPbs (the setIrradianceField singleton hazard).
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

static void render(Engine *e, int frames = 5)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static float lum(const Colour &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

static void show(const char *what, const Colour &c)
{
    std::printf("   %-36s r=%.4f g=%.4f b=%.4f  (lum %.4f)\n", what, c.r, c.g, c.b, lum(c));
}

static NodeId addSlab(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = enginetest::addTestCube(s, albedo, 0.0f, 0.9f);
    enginetest::setNodePosition(s, n, pos);
    enginetest::setNodeScale(s, n, scale);
    return n;
}

// ---------------------------------------------------------------------------
// Region statistics, in 8-bit steps, which is the unit every threshold here is
// stated in (the diagnosis' numbers are 8-bit too).
struct Stats {
    Colour mean{0, 0, 0};
    float  spread = 0.0f;      ///< max - min over the region, worst channel, in 1/255
    float  chroma = 0.0f;      ///< worst |channel - channel| within one pixel, in 1/255
    unsigned n = 0;
};

/// Mean/spread/chroma over the pixels of `img` for which `keep(x, y)` is true.
template <typename Pred>
static Stats regionStats(const Image &img, Pred keep)
{
    Stats st;
    float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
    double sum[3] = { 0, 0, 0 };
    for (unsigned y = 0; y < img.height; ++y) {
        for (unsigned x = 0; x < img.width; ++x) {
            if (!keep(x, y)) continue;
            const Colour c = img.at(x, y);
            const float ch[3] = { c.r, c.g, c.b };
            for (int i = 0; i < 3; ++i) {
                sum[i] += ch[i];
                lo[i] = std::min(lo[i], ch[i]);
                hi[i] = std::max(hi[i], ch[i]);
            }
            const float cm = std::max(std::max(std::fabs(c.r - c.g), std::fabs(c.g - c.b)),
                                      std::fabs(c.r - c.b));
            st.chroma = std::max(st.chroma, cm * 255.0f);
            ++st.n;
        }
    }
    if (!st.n) return st;
    st.mean = Colour(float(sum[0] / st.n), float(sum[1] / st.n), float(sum[2] / st.n));
    for (int i = 0; i < 3; ++i) st.spread = std::max(st.spread, (hi[i] - lo[i]) * 255.0f);
    return st;
}

static void showStats(const char *what, const Stats &s)
{
    std::printf("   %-30s n=%4u mean r=%.4f g=%.4f b=%.4f  spread %.2f/255  chroma %.2f/255\n",
                what, s.n, s.mean.r, s.mean.g, s.mean.b, s.spread, s.chroma);
}

static float meanDelta255(const Stats &a, const Stats &b)
{
    return std::max(std::max(std::fabs(a.mean.r - b.mean.r), std::fabs(a.mean.g - b.mean.g)),
                    std::fabs(a.mean.b - b.mean.b)) * 255.0f;
}

// ---------------------------------------------------------------------------
// THE SCENE. Interior x,z in [-4,4], y in [0,5]; a 0.5 m shell; a point light
// sealed inside and OFF-CENTRE, so the interior irradiance varies strongly in x
// and z — which is what makes a leak show up as a PATTERN on the roof rather
// than as a constant. One wall is red, so the leak is chromatic too.
static const unsigned kRes = 128;

struct Box {
    View  *view = nullptr;          ///< the exterior camera (orthographic, re-aimed per shot)
    View  *inside = nullptr;        ///< the interior camera
    Scene *scene = nullptr;
};

static Box buildBox(Engine *e)
{
    Box b;
    b.view = e->createOffscreenView("ifdedge", kRes, kRes, Colour(0, 0, 0));
    b.inside = e->createOffscreenView("ifdedge_in", kRes, kRes, Colour(0, 0, 0));
    b.scene = e->createScene("ifdedge");
    b.view->setScene(b.scene);
    b.inside->setScene(b.scene);
    // A genuine hemisphere pair: the exterior's ONLY light, and the term the
    // field's binding deletes (VctDisableDiffuse) and the fallback restores.
    b.scene->setAmbient(Colour(0.30f, 0.30f, 0.30f), Colour(0.14f, 0.14f, 0.14f));

    const Colour grey(0.45f, 0.45f, 0.45f);
    const Colour red(0.55f, 0.02f, 0.02f);
    addSlab(b.scene, grey, Vec3(0.0f, -0.25f, 0.0f), Vec3(9.0f, 0.5f, 9.0f));    // floor
    addSlab(b.scene, grey, Vec3(0.0f,  5.25f, 0.0f), Vec3(9.0f, 0.5f, 9.0f));    // ceiling slab
    addSlab(b.scene, grey, Vec3(-4.25f, 2.5f, 0.0f), Vec3(0.5f, 5.0f, 9.0f));    // -X wall
    addSlab(b.scene, grey, Vec3( 4.25f, 2.5f, 0.0f), Vec3(0.5f, 5.0f, 9.0f));    // +X wall
    addSlab(b.scene, grey, Vec3(0.0f, 2.5f, -4.25f), Vec3(9.0f, 5.0f, 0.5f));    // -Z wall
    addSlab(b.scene, red,  Vec3(0.0f, 2.5f,  4.25f), Vec3(9.0f, 5.0f, 0.5f));    // +Z wall (red)

    // OUTSIDE, and lit by nothing: a cube 2.25 m above the field's top and a
    // cube 18 m beyond the +X wall.
    addSlab(b.scene, grey, Vec3(0.0f, 7.5f, 0.0f), Vec3(1.5f, 1.5f, 1.5f));      // cube A
    addSlab(b.scene, grey, Vec3(23.0f, 2.5f, 0.0f), Vec3(2.0f, 2.0f, 2.0f));     // cube B

    // The light: INSIDE (a directional light injects nothing into a sealed
    // volume), off-centre, so the interior bounce is strongly spatial.
    const NodeId light = b.scene->createNode();
    b.scene->setNodeTransform(light, Vec3(2.2f, 1.2f, 2.2f), Quat(), Vec3(1, 1, 1));
    LightDesc l;
    l.type = LightType::Point;
    l.colour = Colour(1, 1, 1);
    l.intensity = 1.2f;
    l.range = 20.0f;
    l.castShadows = false;
    b.scene->setLight(light, l);

    enginetest::testCameraLookAt(b.inside, Vec3(0.0f, 2.0f, -3.4f), Vec3(0.0f, 0.4f, 1.0f));
    return b;
}

/// Straight down on `centre`, orthographic: the projection is then exact and
/// every pixel window below is a fixed piece of world. (A hair of Z tilt keeps
/// the look-at basis out of its degenerate case.)
static void aimDown(View *v, float cx, float cz, float halfExtent)
{
    CameraDesc c = enginetest::testCameraDescLookAt(Vec3(cx, 30.0f, cz + 0.02f),
                                                    Vec3(cx, 0.0f, cz));
    c.orthographic = true;
    c.orthoSize = halfExtent;                  // HALF the vertical extent
    c.nearClip = 0.1f;
    c.farClip = 200.0f;
    v->setCamera(c);
}

// The roof shot: half-extent 5.2 m over a 9 m roof, so the roof covers +-55 px
// of the 128 and cube A (1.5 m, centred) covers +-9. The ring between 20 and 48
// px from the centre is roof, all of it, in every frame of this suite.
static bool roofRing(unsigned x, unsigned y)
{
    const int dx = int(x) - int(kRes / 2), dy = int(y) - int(kRes / 2);
    const int d = std::max(std::abs(dx), std::abs(dy));
    return d >= 20 && d <= 48;
}
static bool cubeTop(unsigned x, unsigned y)      // cube A's top face, centre of the same shot
{
    const int dx = int(x) - int(kRes / 2), dy = int(y) - int(kRes / 2);
    return std::max(std::abs(dx), std::abs(dy)) <= 4;
}
// Cube B's shot: half-extent 3 m over a 2 m cube -> +-21 px.
static bool cubeBTop(unsigned x, unsigned y)
{
    const int dx = int(x) - int(kRes / 2), dy = int(y) - int(kRes / 2);
    return std::max(std::abs(dx), std::abs(dy)) <= 10;
}

/// One full set of readings at the current GI settings.
struct Reading {
    Stats roof, cubeA, cubeB;
    Colour floorNear{0, 0, 0}, floorFar{0, 0, 0};
    Image  interior;
};

static Reading measure(Engine *e, Box &b)
{
    Reading r;
    Image img;
    aimDown(b.view, 0.0f, 0.0f, 5.2f);
    render(e);
    b.view->readPixels(img);
    r.roof  = regionStats(img, roofRing);
    r.cubeA = regionStats(img, cubeTop);

    aimDown(b.view, 23.0f, 0.0f, 3.0f);
    render(e);
    b.view->readPixels(img);
    r.cubeB = regionStats(img, cubeBTop);

    render(e);
    b.inside->readPixels(r.interior);
    // The floor patch nearest the red wall (the camera looks from -Z toward +Z,
    // so the far half of the floor is the red wall's side) and one off to the
    // side, which the bounce reaches far less.
    r.floorNear = r.interior.at(64, 78);
    r.floorFar  = r.interior.at(10, 120);
    return r;
}

static GiParams vctBase()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;
    gi.numBounces = 2;
    gi.boundsMin = Vec3(-5.0f, -1.0f, -5.0f);
    // THE WORST CASE, and today's automatic answer: the top of the volume sits
    // INSIDE the ceiling slab (5.0 - 5.5).
    gi.boundsMax = Vec3(5.0f, 5.25f, 5.0f);
    return gi;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-ddgi-edge-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    Box b = buildBox(e);

    // =====================================================================
    // CASE 1 — the reference: VCT, no field. This is "whatever it had before
    // the field existed", and every exterior assertion below is stated against
    // it.
    // =====================================================================
    std::printf("\n== case 1: the field-off reference ==\n");
    GiParams off = vctBase();
    CHECK(b.scene->setGlobalIllumination(off), "VCT without DDGI arms");
    const Reading refOff = measure(e, b);
    showStats("roof, field off", refOff.roof);
    showStats("cube 2 m above, field off", refOff.cubeA);
    showStats("cube 18 m out, field off", refOff.cubeB);
    show("interior floor near the red wall", refOff.floorNear);
    show("interior floor, far corner", refOff.floorFar);
    CHECK(refOff.roof.n > 1000 && refOff.cubeA.n > 50 && refOff.cubeB.n > 200,
          "every region has pixels (the windows are aimed at what they claim)");
    CHECK(lum(refOff.roof.mean) > 0.05f && lum(refOff.roof.mean) < 0.90f,
          "the roof is mid-tone with the field off (the flatness assertions are not vacuous)");
    CHECK(refOff.roof.spread <= 2.0f,
          "REFERENCE: the roof is already flat with the field off (nothing else varies out there)");

    // =====================================================================
    // CASE 2 — the field ON over the worst-case bounds. THE regression.
    // =====================================================================
    std::printf("\n== case 2: the field ON, bounds top inside the ceiling slab ==\n");
    GiParams on = vctBase();
    on.ddgi = GiToggle::On;
    CHECK(b.scene->setGlobalIllumination(on), "DDGI arms");
    const GiStatus st = b.scene->giStatus();
    CHECK(st.ifdBound, "the field is bound (the leak has a source)");
    std::printf("   field: %d probes, converged %d\n", st.ifdProbes, int(st.ifdConverged));
    const Reading ddgiOn = measure(e, b);
    showStats("roof, field ON", ddgiOn.roof);
    showStats("cube 2 m above, field ON", ddgiOn.cubeA);
    showStats("cube 18 m out, field ON", ddgiOn.cubeB);
    show("interior floor near the red wall", ddgiOn.floorNear);
    show("interior floor, far corner", ddgiOn.floorFar);

    std::printf("   roof spread %.2f/255, mean delta vs field off %.2f/255\n",
                ddgiOn.roof.spread, meanDelta255(ddgiOn.roof, refOff.roof));
    CHECK(ddgiOn.roof.spread <= 2.0f,
          "FLAT ROOF: outside the field the roof varies by at most 2/255 with DDGI on");
    CHECK(ddgiOn.roof.chroma <= 2.0f,
          "NO INTERIOR CHROMA on the roof (the red wall does not reach the outside)");
    CHECK(meanDelta255(ddgiOn.roof, refOff.roof) <= 2.0f,
          "THE FALLBACK: the roof reads what it read before the field existed");

    std::printf("   cube 2 m above: spread %.2f chroma %.2f delta %.2f/255\n",
                ddgiOn.cubeA.spread, ddgiOn.cubeA.chroma, meanDelta255(ddgiOn.cubeA, refOff.cubeA));
    CHECK(ddgiOn.cubeA.chroma <= 2.0f, "the cube 2 m above the volume is neutral");
    CHECK(meanDelta255(ddgiOn.cubeA, refOff.cubeA) <= 3.0f,
          "the cube 2 m above the volume reads its field-off value");

    std::printf("   cube 18 m out: spread %.2f chroma %.2f delta %.2f/255\n",
                ddgiOn.cubeB.spread, ddgiOn.cubeB.chroma, meanDelta255(ddgiOn.cubeB, refOff.cubeB));
    CHECK(ddgiOn.cubeB.chroma <= 2.0f, "the cube 18 m outside the wall is neutral");
    CHECK(meanDelta255(ddgiOn.cubeB, refOff.cubeB) <= 3.0f,
          "the cube 18 m outside the wall reads its field-off value");

    // THE INTERIOR still has its bounce, and it is still spatial and still red.
    const float nearRedBias = ddgiOn.floorNear.r - ddgiOn.floorNear.g;
    std::printf("   interior: near-wall red bias %.4f, near lum %.4f, far lum %.4f\n",
                nearRedBias, lum(ddgiOn.floorNear), lum(ddgiOn.floorFar));
    CHECK(nearRedBias > 0.01f,
          "THE INTERIOR IS UNTOUCHED: the red wall still bounces red onto the floor");
    CHECK(lum(ddgiOn.floorNear) > lum(ddgiOn.floorFar) * 1.05f,
          "the interior bounce is still SPATIAL (near the source brighter than the far corner)");
    {
        unsigned wx = 0, wy = 0;
        float worst = 0.0f;
        for (unsigned y = 0; y < kRes; ++y)
            for (unsigned x = 0; x < kRes; ++x) {
                const Colour ca = refOff.interior.at(x, y), cb = ddgiOn.interior.at(x, y);
                const float d = std::max(std::max(std::fabs(ca.r - cb.r), std::fabs(ca.g - cb.g)),
                                         std::fabs(ca.b - cb.b));
                if (d > worst) { worst = d; wx = x; wy = y; }
            }
        std::printf("   interior worst pixel moves %.2f/255 between field off and on at (%u,%u)\n",
                    worst * 255.0f, wx, wy);
        CHECK(worst * 255.0f > 2.0f,
              "the field still DOES something inside (the flatness outside is not the field "
              "being switched off)");
    }

    // =====================================================================
    // CASE 3 — THE ENCLOSING-BOUNDS VARIANT. The scene-side fix for the
    // automatic bounds would enclose the shell's OUTER faces, which puts the
    // roof INSIDE the field volume: the cage is then a legal cage and only its
    // own visibility test can stop the interior probes painting the roof.
    // =====================================================================
    std::printf("\n== case 3: bounds enclosing the shell (the roof is inside the volume) ==\n");
    GiParams enc = vctBase();
    enc.boundsMax = Vec3(5.5f, 6.0f, 5.5f);          // above the roof's outer face (5.5)
    CHECK(b.scene->setGlobalIllumination(enc), "VCT without DDGI, enclosing bounds");
    const Reading encOff = measure(e, b);
    showStats("roof, enclosing bounds, field off", encOff.roof);
    enc.ddgi = GiToggle::On;
    CHECK(b.scene->setGlobalIllumination(enc), "DDGI arms over the enclosing bounds");
    const Reading encOn = measure(e, b);
    showStats("roof, enclosing bounds, field ON", encOn.roof);
    std::printf("   roof spread %.2f/255, mean delta vs field off %.2f/255\n",
                encOn.roof.spread, meanDelta255(encOn.roof, encOff.roof));
    showStats("cube 2 m above, enclosing bounds", encOn.cubeA);
    // NO ASSERTION ON THE ROOF HERE, AND THE REASON IS THE FINDING. With the
    // shell enclosed, the roof is INSIDE the field's authored volume, so no
    // bounds-relative fade can see it — and the probes that answer it are the
    // ones EMBEDDED IN THE CEILING SLAB, which the depth atlas reports as
    // unoccluded (measured: the trilinear-weighted Chebyshev visibility of such
    // a pixel is above 0.999, so no threshold separates it from an interior
    // floor). Recognising probes inside geometry is probe classification,
    // explicitly out of scope for this lane. The numbers above are therefore
    // recorded, not gated: they say what enclosing the shell would cost, and
    // they are the reason the scene-side auto-bounds change must not ship
    // without a probe-classification pass. What IS gated here is that
    // everything genuinely outside the enlarged volume still reads clean.
    CHECK(meanDelta255(encOn.cubeA, encOff.cubeA) <= 3.0f,
          "the cube above the ENLARGED volume still reads its field-off value");
    CHECK(encOn.cubeA.chroma <= 2.0f, "and is still neutral");

    // =====================================================================
    // CASE 4 — the intensity dial still governs the interior, which is the
    // cheapest proof that the fade did not simply turn the field off.
    // =====================================================================
    std::printf("\n== case 4: intensity 0 removes the interior bounce ==\n");
    GiParams zero = vctBase();
    zero.ddgi = GiToggle::On;
    zero.ddgiIntensity = 0.0f;
    CHECK(b.scene->setGlobalIllumination(zero), "DDGI at intensity 0 arms");
    const Reading zeroOn = measure(e, b);
    show("interior floor near the red wall, intensity 0", zeroOn.floorNear);
    CHECK(lum(zeroOn.floorNear) < lum(ddgiOn.floorNear),
          "intensity 0 is darker inside than intensity 1 (the field was doing the work)");
    showStats("roof, intensity 0", zeroOn.roof);
    CHECK(zeroOn.roof.spread <= 2.0f, "and the roof is flat at intensity 0 too");

    b.view->setScene(nullptr);
    b.inside->setScene(nullptr);
    e->destroyScene(b.scene);
    e->destroyView(b.view);
    e->destroyView(b.inside);
    engine.reset();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
