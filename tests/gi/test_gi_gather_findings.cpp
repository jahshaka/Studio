// THE GATHER'S FOUR CORRECTNESS FINDINGS (PHOTON-GATHER-1b item 1;
// SPECS/photon/C2_GATHER_FILTERED_DEFAULT_ON_DESIGN.md section 1.1). One binary,
// four entries, the mode on the command line:
//
//   gi.gather_glass  GA-GLASS  A blended (Glass / Blend / Refractive) fragment
//                              reads NO probe irradiance: the gather's answer at
//                              a pixel is the OPAQUE surface the prepass drew
//                              there, not the glass in front of it. The glass
//                              keeps the diffuse path it had (the field, the
//                              cones, the environment), so its pixels are the
//                              gather-OFF picture's while the opaque surfaces
//                              around it move.
//   gi.gather_nest   GA-NEST   The registration is PASS-scoped: a colour render
//                              of the same SceneManager that is not the pass the
//                              gather was recorded for (a planar mirror, a probe
//                              capture) sees no `jah_probe_gather` and shades
//                              with its own diffuse path.
//   gi.gather_sky    GA-SKY    ONE-ENV's miss, asserted: a zenith sun with its
//                              disc on, and the open floor's probe irradiance is
//                              the disc-free sky's SH irradiance within 2 %.
//   gi.gather_plane_weight     THE GATHER'S PLANE WEIGHT at a crushed cage: a
//                              wall pixel beside the edge of a fence 2 cm in
//                              front of it — where every probe around it passes
//                              one plane test — reads the WALL's irradiance (the
//                              same wall with no fence), not the fence's; the
//                              share of the fence's excess that crosses the edge
//                              is printed.
//
// GA-CAGE AS THE DESIGN WROTE IT — the IRRADIANCE FIELD's crushed-cage guard
// (JahIfd_piece_ps.any's ifdFrontWeight / sumIfdWeight and the weight fade) —
// HAS NO ARM HERE, AND CANNOT WHILE THE GATHER RUNS (fix round, measured,
// spikes/photon-gather-1b/fieldguard-slot.log): the field answers only where the
// gather declines (w = 0), and the gather declines only where every bracketing
// probe fails the plane test — a surface more than 1 % of its view distance off
// every probe's plane. At a 2 cm fence the gather answers (w = 1 at every wall
// pixel, whatever the stride); behind a 20 cm fence with a 15 cm slot, stride 64,
// centred probes and no adaptive ones, the slot's wall pixels DO read w = 0 and
// the bound field answers — and neutralising the guard in the staged media moves
// no pixel there either: a cage is crushed only for a pixel no field probe can
// see, and a pixel the camera sees through a gap wider than the plane tolerance
// is one the probes in front of it see too. The guard is the no-gather tiers'
// (and VR's) protection; its fixture belongs with the field's suites.
//
// Every A/B here FREEZES the gather's frame index in BOTH arms (GAFAR-1's
// instrument rule): two live frames of a stochastic estimator differ on half
// their pixels and an A/B of them measures that noise.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
        char buf_[640];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static void render(Engine *e, int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

static const unsigned kSize = 256u;

/// THE CHAIN IS THE SAME SHAPE IN BOTH ARMS: the SSR row carries the prepass
/// whether or not the gather runs. Without it the gather's own row would ADD the
/// prepass (`ChainDesc::probeGather`) to one arm only, and an A/B would measure
/// the chain's shape — a Fade (Blend) slab renders 25/255 darker in a chain with
/// a prepass than in one without, gather or no gather (PHOTON-GATHER-1b's
/// finding, reported). With the prepass in both arms every A/B below is the
/// gather's row and nothing else (the SSR's own history is quiet here: the
/// glass A/B reads exactly 0/255 after the fix).
static void armChain(View *view, bool refractions = false)
{
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 1;
    fx.refractions = refractions;
    view->setPostFx(fx);
}

static GiParams chainGi(bool gather)
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;     // the cones answer where the gather does not
    gi.numBounces = 1;
    gi.cascades = true;
    gi.gather = gather ? GiToggle::On : GiToggle::Off;
    return gi;
}

/// The row by the tuning door (a gather toggle is not a GI rebuild), the frame
/// index FROZEN, the readback on when asked.
static void setGather(Scene *s, GiParams gi, bool on, bool readback = false)
{
    gi.gather = on ? GiToggle::On : GiToggle::Off;
    s->setGiTuning(gi);
    GatherTuning t;
    t.freezeFrameIndex = true;
    t.readback = readback;
    s->setGatherTuning(t);
}

static NodeId addBox(Scene *s, const PbrParams &p, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = s->createNode();
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    const MaterialId m = s->createPbrMaterial(p);
    if (!n || !mesh || !m || !s->attachMesh(n, mesh, m)) return 0;
    s->setNodeTransform(n, pos, Quat(), scale);
    return n;
}

static PbrParams matte(const Colour &albedo)
{
    PbrParams p;
    p.albedo = albedo;
    p.metalness = 0.0f;
    p.roughness = 1.0f;
    return p;
}

/// How two pictures differ INSIDE a rectangle (fractions of the image).
struct Delta { unsigned moved = 0u, total = 0u; double mean = 0.0; unsigned worst = 0u; };
static Delta deltaIn(const Image &a, const Image &b, float x0, float y0, float x1, float y1)
{
    Delta d;
    if (a.width != b.width || a.height != b.height) return d;
    double sum = 0.0;
    for (unsigned y = unsigned(y0 * a.height); y < unsigned(y1 * a.height); ++y)
        for (unsigned x = unsigned(x0 * a.width); x < unsigned(x1 * a.width); ++x) {
            const size_t i = (size_t(y) * a.width + x) * 4u;
            unsigned w = 0u;
            for (int c = 0; c < 3; ++c)
                w = std::max(w, unsigned(std::abs(int(a.rgba[i + c]) - int(b.rgba[i + c]))));
            ++d.total;
            sum += w;
            if (w) { ++d.moved; d.worst = std::max(d.worst, w); }
        }
    d.mean = d.total ? sum / d.total : 0.0;
    return d;
}

static void printDelta(const char *what, const Delta &d)
{
    std::printf("   %-44s %6u of %6u px moved, mean %.3f/255, worst %u\n", what, d.moved, d.total,
                d.mean, d.worst);
}

/// Mean linear-ish channel values of a rectangle (the 8-bit picture, 0..1).
static Colour meanIn(const Image &img, float x0, float y0, float x1, float y1)
{
    double r = 0, g = 0, b = 0; unsigned n = 0;
    for (unsigned y = unsigned(y0 * img.height); y < unsigned(y1 * img.height); ++y)
        for (unsigned x = unsigned(x0 * img.width); x < unsigned(x1 * img.width); ++x) {
            const Colour c = img.at(x, y); r += c.r; g += c.g; b += c.b; ++n;
        }
    return n ? Colour(float(r / n), float(g / n), float(b / n)) : Colour(0, 0, 0);
}

/// The readback's mean E/pi (rgb) and coverage over a rectangle.
struct IrrMean { double r = 0, g = 0, b = 0, w = 0; unsigned n = 0; };
static IrrMean irrIn(const GatherStatus &st, float x0, float y0, float x1, float y1,
                     bool coveredOnly = true)
{
    IrrMean m;
    if (st.irradiance.size() < size_t(st.irradianceW) * st.irradianceH * 4u) return m;
    for (unsigned y = unsigned(y0 * st.irradianceH); y < unsigned(y1 * st.irradianceH); ++y)
        for (unsigned x = unsigned(x0 * st.irradianceW); x < unsigned(x1 * st.irradianceW); ++x) {
            const float *p = &st.irradiance[(size_t(y) * st.irradianceW + x) * 4u];
            m.w += p[3];
            if (coveredOnly && p[3] < 0.5f) continue;
            m.r += p[0]; m.g += p[1]; m.b += p[2]; ++m.n;
        }
    const unsigned area = (unsigned(y1 * st.irradianceH) - unsigned(y0 * st.irradianceH)) *
                          (unsigned(x1 * st.irradianceW) - unsigned(x0 * st.irradianceW));
    if (m.n) { m.r /= m.n; m.g /= m.n; m.b /= m.n; }
    m.w = area ? m.w / area : 0.0;
    return m;
}

// ===========================================================================
// GA-GLASS
// ===========================================================================
static int glassMain(Engine *e)
{
    View *view = e->createOffscreenView("glass", kSize, kSize, Colour(0, 0, 0));
    if (view) view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    if (!view) { std::printf("FAIL: view\n"); return 1; }
    view->setShadows(true);

    // Two chain shapes: the Glass mode renders INSIDE the opaque pass (no
    // refraction pass in the graph), the Refractive mode in the refraction pass
    // after it. Both must read no probe irradiance.
    struct Arm { const char *name; PbrAlphaMode mode; bool refractionPass; };
    const Arm arms[3] = {
        { "glass (in the opaque pass)", PbrAlphaMode::Glass, false },
        { "blend (in the opaque pass)", PbrAlphaMode::Blend, false },
        { "refractive (the refraction pass)", PbrAlphaMode::Refractive, true },
    };
    for (const Arm &arm : arms) {
        armChain(view, arm.refractionPass);
        Scene *s = e->createScene(std::string("glass-") + arm.name);
        view->setScene(s);
        if (!e->rayQueryAvailable() || !e->rayTracing()) {
            std::printf("ok: no ray queries on this machine — gi.gather_glass skips cleanly\n");
            return 0;
        }
        s->setAmbient(Colour(0.10f, 0.10f, 0.12f), Colour(0.03f, 0.03f, 0.03f));
        // A BLACK floor under the glass: whatever the glass transmits of it is
        // its specular alone, identical in both arms — so the glass region's
        // pixels are the GLASS's own shading and nothing the gather moves.
        addBox(s, matte(Colour(0.0f, 0.0f, 0.0f)), Vec3(0, -0.25f, 0), Vec3(24, 0.5f, 24));
        // A white floor patch to the side: an opaque surface the gather DOES
        // move, the proof that the two arms differ at all.
        addBox(s, matte(Colour(0.85f, 0.85f, 0.85f)), Vec3(-2.6f, 0.01f, 0.0f), Vec3(2.0f, 0.02f, 4.0f));
        // The bounce source both see: an emissive red panel behind.
        {
            PbrParams p = matte(Colour(0.05f, 0.05f, 0.05f));
            p.emissive = Colour(0.9f, 0.0f, 0.0f);
            addBox(s, p, Vec3(0.0f, 1.5f, -3.0f), Vec3(8.0f, 3.0f, 0.3f));
        }
        // THE GLASS: a white slab half a metre over the black floor.
        {
            PbrParams p = matte(Colour(0.9f, 0.9f, 0.9f));
            p.alphaMode = arm.mode;
            p.alpha = 0.6f;
            addBox(s, p, Vec3(1.4f, 0.5f, 0.0f), Vec3(2.0f, 0.1f, 2.4f));
        }
        enginetest::addDirectionalLight(s, Vec3(0.3f, -1.0f, -0.4f), 1.0f);
        enginetest::testCameraLookAt(view, Vec3(-0.6f, 6.5f, 7.5f), Vec3(-0.6f, 0.0f, -0.5f));

        const GiParams gi = chainGi(false);
        CHECK(s->setGlobalIllumination(gi), "the chain builds");
        setGather(s, gi, false);
        render(e, 48);
        Image off; view->readPixels(off);
        setGather(s, gi, true);
        render(e, 48);
        Image on; view->readPixels(on);
        const GatherStatus st = s->giStatus().gather;
        CHECK_MSG(st.running, "%s: the gather runs (%u probes)", arm.name, st.probes);
        if (std::getenv("JAH_GATHER_DUMP")) {
            const std::string dir = std::getenv("JAH_GATHER_DUMP");
            const auto dump = [&](const Image &img, const std::string &tag) {
                FILE *f = std::fopen((dir + "/glass-" + std::to_string(int(arm.mode)) + "-" + tag +
                                      ".ppm").c_str(), "wb");
                if (!f) return;
                std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
                for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) std::fwrite(&img.rgba[i], 1, 3, f);
                std::fclose(f);
            };
            dump(off, "off");
            dump(on, "on");
        }
        // Where the glass and the white patch are in the shot (measured on the
        // dumped pictures: the glass slab's top face, the patch's middle).
        const Delta glass = deltaIn(off, on, 0.66f, 0.43f, 0.86f, 0.57f);
        const Delta patch = deltaIn(off, on, 0.15f, 0.45f, 0.35f, 0.65f);
        std::printf(" %s\n", arm.name);
        printDelta("the glass, gather on vs off", glass);
        printDelta("the opaque white patch, gather on vs off", patch);
        CHECK_MSG(patch.mean > 1.0,
                  "%s: the gather MOVES the opaque patch beside the glass (mean %.3f/255) — the "
                  "two arms are different estimators", arm.name, patch.mean);
        if (arm.refractionPass) {
            // The refraction pass lies OUTSIDE the gather's bracket (the pass
            // property is registered for the PrePassUse pass alone), so the
            // refractive slab shades as it does with the gather off - but it
            // TRANSMITS the floor under it, which the opaque pass drew. Since
            // PHOTON-VOXEL-3/4 that floor's rough specular (roughness 1: a lobe
            // wider than a cone) takes its occluded share from the DIFFUSE
            // estimator's voxel light (Vct_piece_ps.any, applyVctRoughSpecular) -
            // the gather's where it runs - so the gather moves what the glass
            // transmits by the gather's own change of irradiance times the black
            // floor's specular albedo: the white patch's change over its albedo
            // 0.85, times the dielectric F0 0.04 at roughness 1 (the split-sum's
            // DFG there is below 1: an upper bound), plus the reader's quantum 0.25.
            const double glassBar = 0.25 + patch.mean / 0.85 * 0.04;
            CHECK_MSG(glass.mean <= glassBar && glass.worst <= 3u,
                      "GA-GLASS, %s: the glass reads no probe irradiance of its own — it moves only by "
                      "the transmitted floor's rough specular, which carries the gather's (mean "
                      "%.3f/255, worst %u; bar %.3f mean = 0.25 + the patch's %.3f / 0.85 x 0.04, 3 worst)",
                      arm.name, glass.mean, glass.worst, glassBar, patch.mean);
        } else {
            // IN THE GATHERING PASS a blended fragment's diffuse GI is THE
            // ENVIRONMENT TERM ALONE since PHOTON-GATHER-1d (the listener's
            // `jah_env_diffuse_only`): no probe, no cones, no field cage. So the
            // toggle MAY move the slab — it loses the cones' red bounce — but the
            // probe's answer must not reach it: that answer is the red panel's
            // bounce, and reading it made the slab REDDER (the 1b leak: +18.6/255
            // on the Blend slab). The slab's red may fall; it may not rise.
            const Colour gOff = meanIn(off, 0.66f, 0.43f, 0.86f, 0.57f);
            const Colour gOn = meanIn(on, 0.66f, 0.43f, 0.86f, 0.57f);
            const double redRise = 255.0 * (double(gOn.r) - double(gOff.r));
            const double greenMove = 255.0 * (double(gOn.g) - double(gOff.g));
            std::printf("   the slab's red %+.3f/255, green %+.3f/255 on the gather's toggle\n",
                        redRise, greenMove);
            CHECK_MSG(redRise <= 0.25,
                      "GA-GLASS, %s: the slab reads NO probe irradiance — its red does not rise with "
                      "the gather on (%+.3f/255; bar +0.25; the leak raised it 18.6/255)",
                      arm.name, redRise);
        }
        e->destroyScene(s);
    }

    // ---- F2: A BLENDED FRAGMENT UNDER THE PREPASS (PHOTON-GATHER-1d) ---------
    // `hlms_use_prepass` is a PASS property, and under it the PBS pixel shader
    // takes the fragment's normal, roughness and directional SHADOW from the
    // G-buffer at iFragCoord — the OPAQUE floor's under the slab, which lies in
    // the slab's OWN cast shadow: the slab rendered in its own shadow, measured
    // 25/255 dark (spikes/photon-gather-1b/AUDIT.md item 8). The listener now
    // withdraws the property per blended renderable. The same Fade slab over a
    // lit white floor, GI off (nothing but the direct term moves), with the
    // prepass (the SSR row) and without it (no chain at all): the slab reads the
    // same.
    {
        Scene *s = e->createScene("glass-f2");
        view->setScene(s);
        s->setAmbient(Colour(0.10f, 0.10f, 0.12f), Colour(0.03f, 0.03f, 0.03f));
        addBox(s, matte(Colour(0.85f, 0.85f, 0.85f)), Vec3(0, -0.25f, 0), Vec3(24, 0.5f, 24));
        {
            PbrParams p = matte(Colour(0.9f, 0.9f, 0.9f));
            p.alphaMode = PbrAlphaMode::Blend;
            p.alpha = 0.6f;
            addBox(s, p, Vec3(1.4f, 0.5f, 0.0f), Vec3(2.0f, 0.1f, 2.4f));
        }
        enginetest::addDirectionalLight(s, Vec3(0.3f, -1.0f, -0.4f), 1.0f);
        enginetest::testCameraLookAt(view, Vec3(-0.6f, 6.5f, 7.5f), Vec3(-0.6f, 0.0f, -0.5f));
        GiParams gi;
        gi.mode = GiMode::Off;
        CHECK(s->setGlobalIllumination(gi), "F2: GI off (only the direct term)");
        view->setPostFx(PostFxDesc());     // no chain: no prepass
        render(e, 12);
        Image plain; view->readPixels(plain);
        armChain(view);                      // the SSR row: the prepass
        render(e, 12);
        Image prepass; view->readPixels(prepass);
        if (std::getenv("JAH_GATHER_DUMP")) {
            const std::string dir = std::getenv("JAH_GATHER_DUMP");
            for (const auto &pr : { std::make_pair(&plain, "plain"), std::make_pair(&prepass, "prepass") }) {
                FILE *f = std::fopen((dir + "/f2-" + pr.second + ".ppm").c_str(), "wb");
                if (!f) continue;
                std::fprintf(f, "P6\n%u %u\n255\n", pr.first->width, pr.first->height);
                for (size_t i = 0; i + 3 < pr.first->rgba.size(); i += 4) std::fwrite(&pr.first->rgba[i], 1, 3, f);
                std::fclose(f);
            }
        }
        // THE CONTROL: the opaque floor beside the slab, where the prepass changes
        // nothing a blended fragment could be blamed for.
        const Delta floorCtl = deltaIn(plain, prepass, 0.15f, 0.45f, 0.35f, 0.65f);
        printDelta("F2: the opaque floor beside it (control)", floorCtl);
        const Colour a = meanIn(plain, 0.66f, 0.43f, 0.86f, 0.57f);
        const Colour b = meanIn(prepass, 0.66f, 0.43f, 0.86f, 0.57f);
        const double dark = 255.0 * ((double(a.r) + a.g + a.b) - (double(b.r) + b.g + b.b)) / 3.0;
        const Delta slab = deltaIn(plain, prepass, 0.66f, 0.43f, 0.86f, 0.57f);
        printDelta("F2: the Fade slab, prepass vs none", slab);
        std::printf("   F2: the slab is %.3f/255 darker under the prepass (1b measured 25)\n", dark);
        CHECK_MSG(std::fabs(dark) <= 1.0 && slab.mean <= 1.0,
                  "F2: A BLENDED FRAGMENT SHADES FROM ITSELF under the prepass — the Fade slab reads "
                  "%.3f/255 against the chain-less picture (mean |d| %.3f; bar 1; it was 25 dark, in its "
                  "own cast shadow)",
                  dark, slab.mean);
        e->destroyScene(s);
    }
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

// ===========================================================================
// GA-NEST
// ===========================================================================
static int nestMain(Engine *e)
{
    View *view = e->createOffscreenView("nest", kSize, kSize, Colour(0, 0, 0));
    if (view) view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    if (!view) { std::printf("FAIL: view\n"); return 1; }
    view->setShadows(true);
    armChain(view);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.gather_nest skips cleanly\n");
        return 0;
    }

    // ---- 1. A PLANAR MIRROR reflecting a floor lit by a bounce ----------
    // The mirror is a vertical reflector the camera looks into; what it shows is
    // the white floor in front of it, lit by an emissive red panel's bounce —
    // a DIFFUSE-GI term the mirror's own render must compute (the cones here).
    // A mirror render that saw the gather's pass property would compile its
    // cone diffuse out (`vct_disable_diffuse`) and read no probe (it has no
    // screen position), i.e. LOSE the floor's indirect.
    {
        Scene *s = e->createScene("nest-planar");
        view->setScene(s);
        s->setAmbient(Colour(0.0f, 0.0f, 0.0f), Colour(0.0f, 0.0f, 0.0f));
        addBox(s, matte(Colour(0.85f, 0.85f, 0.85f)), Vec3(0, -0.25f, 0), Vec3(20, 0.5f, 20));
        {
            PbrParams p = matte(Colour(0.05f, 0.05f, 0.05f));
            // LOW and BRIGHT, behind the camera, facing a WHITE WALL a few
            // decimetres beyond it: the wall is lit by nothing but the
            // emitter's bounce, and it is what the mirror shows above the
            // emitter's own image.
            p.emissive = Colour(4.0f, 0.0f, 0.0f);
            addBox(s, p, Vec3(0.0f, 0.2f, 2.6f), Vec3(6.0f, 0.4f, 0.2f));
            addBox(s, matte(Colour(0.85f, 0.85f, 0.85f)), Vec3(0.0f, 1.5f, 3.1f), Vec3(8.0f, 3.0f, 0.2f));
        }
        PbrParams mp;
        mp.albedo = Colour(0.95f, 0.95f, 0.95f);
        mp.metalness = 1.0f;
        mp.roughness = 0.02f;
        const NodeId mirror = addBox(s, mp, Vec3(0.0f, 1.5f, -3.0f), Vec3(5.0f, 3.0f, 0.1f));
        // BOUNCE-ONLY (fix round, audit F5): no light and no ambient, so every
        // photon on the reflected floor is the emitter's diffuse bounce — the
        // term a leaked registration would remove (vct_disable_diffuse).
        enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 1.2f), Vec3(0.0f, 1.2f, -3.0f));
        PlanarReflectionParams pr;
        pr.budget = 1;
        pr.resolution = 512;
        pr.mipmaps = true;
        pr.accurateLighting = true;
        CHECK(s->setPlanarReflections(pr), "the planar budget applies");
        CHECK(s->setNodePlanarReflector(mirror, true), "the mirror is a reflector");

        const GiParams gi = chainGi(false);
        CHECK(s->setGlobalIllumination(gi), "the chain builds");
        setGather(s, gi, false);
        render(e, 48);
        Image off; view->readPixels(off);
        setGather(s, gi, true);
        render(e, 48);
        Image on; view->readPixels(on);
        if (const char *dump = std::getenv("JAH_GATHER_DUMP")) {
            FILE *f = std::fopen((std::string(dump) + "/nest-planar-on.ppm").c_str(), "wb");
            if (f) {
                std::fprintf(f, "P6\n%u %u\n255\n", on.width, on.height);
                for (size_t i = 0; i + 3 < on.rgba.size(); i += 4) std::fwrite(&on.rgba[i], 1, 3, f);
                std::fclose(f);
            }
        }
        CHECK(s->activePlanarReflectors() == 1, "the mirror renders");
        CHECK_MSG(s->giStatus().gather.running, "the gather runs (%u probes)",
                  s->giStatus().gather.probes);
        // ...AND A THIRD ARM WITH NO DIFFUSE GI AT ALL (GiMode::Off), so the
        // share of the mirror's picture that IS the bounce is measured rather
        // than assumed (audit F5: a 2 % bar discriminates a lost bounce only
        // where the bounce is well over 2 % of the reading).
        GiParams noGi = gi;
        noGi.mode = GiMode::Off;
        s->setGlobalIllumination(noGi);
        render(e, 48);
        Image none; view->readPixels(none);
        // The reflected WHITE WALL above the emitter's own image in the mirror.
        const float rx0 = 0.20f, ry0 = 0.40f, rx1 = 0.80f, ry1 = 0.49f;
        const Colour a = meanIn(off, rx0, ry0, rx1, ry1);
        const Colour b = meanIn(on, rx0, ry0, rx1, ry1);
        const Colour c = meanIn(none, rx0, ry0, rx1, ry1);
        const Delta d = deltaIn(off, on, rx0, ry0, rx1, ry1);
        printDelta("the mirror's reflected wall, gather on vs off", d);
        const double share = a.r > 1e-4f ? (double(a.r) - double(c.r)) / double(a.r) : 0.0;
        std::printf("   reflected wall red: gather off %.4f, on %.4f, NO diffuse GI %.4f -> the "
                    "bounce is %.1f %% of the reading\n", double(a.r), double(b.r), double(c.r),
                    100.0 * share);
        const double rel = a.r > 1e-4f ? std::fabs(double(b.r) - double(a.r)) / double(a.r) : 0.0;
        CHECK_MSG(share >= 0.5, "the reflected wall is lit BY THE BOUNCE (%.1f %% of it; bar 50 %%) "
                                "— a lost diffuse term would move it by that much", 100.0 * share);
        CHECK_MSG(rel <= 0.02,
                  "GA-NEST, planar: the mirror reflects the bounce-lit wall within 2 %% of the "
                  "gather-off picture (%.2f %%) — its render is not the pass the gather registered for",
                  100.0 * rel);
        e->destroyScene(s);
    }

    // ---- 2. A PROBE CAPTURE (the hybrid's reflection probes) -------------
    // Two fresh scenes, one with the gather armed from the first frame (so the
    // probes are PLACED — captured — while the registration is live), one
    // without; a mirror-metal box in the middle shows what the probes captured.
    {
        const auto shot = [&](bool gather) {
            Scene *s = e->createScene(gather ? "nest-pcc-on" : "nest-pcc-off");
            view->setScene(s);
            s->setAmbient(Colour(0.0f, 0.0f, 0.0f), Colour(0.0f, 0.0f, 0.0f));
            // A closed-ish room so the probes are kept (they photograph walls).
            const Colour white(0.85f, 0.85f, 0.85f);
            addBox(s, matte(white), Vec3(0, -0.25f, 0), Vec3(10, 0.5f, 10));
            addBox(s, matte(white), Vec3(0, 2.0f, -5.0f), Vec3(10, 4.0f, 0.3f));
            addBox(s, matte(white), Vec3(-5.0f, 2.0f, 0), Vec3(0.3f, 4.0f, 10));
            addBox(s, matte(white), Vec3(5.0f, 2.0f, 0), Vec3(0.3f, 4.0f, 10));
            {
                PbrParams p = matte(Colour(0.05f, 0.05f, 0.05f));
                // LOW, at the foot of a FRONT wall behind the camera: the wall
                // is lit by nothing but the emitter's bounce, and the metal
                // box's front face reflects it.
                p.emissive = Colour(4.0f, 0.0f, 0.0f);
                addBox(s, p, Vec3(0.0f, 0.2f, 4.5f), Vec3(8.0f, 0.4f, 0.2f));
                addBox(s, matte(white), Vec3(0, 2.0f, 5.0f), Vec3(10, 4.0f, 0.3f));
                addBox(s, matte(white), Vec3(0, 4.15f, 0), Vec3(10, 0.3f, 10));
            }
            PbrParams mp;
            mp.albedo = Colour(0.95f, 0.95f, 0.95f);
            mp.metalness = 1.0f;
            mp.roughness = 0.05f;
            addBox(s, mp, Vec3(0.0f, 0.8f, -1.0f), Vec3(1.6f, 1.6f, 1.6f));
            // BOUNCE-ONLY (fix round, audit F5): no light and no ambient, so every
        // photon on the reflected floor is the emitter's diffuse bounce — the
        // term a leaked registration would remove (vct_disable_diffuse).
            enginetest::testCameraLookAt(view, Vec3(0.0f, 1.6f, 3.5f), Vec3(0.0f, 0.8f, -1.0f));
            GiParams gi = chainGi(gather);
            gi.mode = GiMode::VctPccHybrid;
            // MEDIUM, NOT HIGH (PHOTON-F12-PCC): High with rays builds no probe
            // grid, so the probes this arm watches exist only below it; the
            // gather row is pinned, so it still runs.
            gi.quality = GiQuality::Medium;
            GatherTuning t;
            t.freezeFrameIndex = true;
            s->setGatherTuning(t);
            s->setGlobalIllumination(gi);
            render(e, 90);
            const GiStatus g = s->giStatus();
            Image img; view->readPixels(img);
            if (const char *dump = std::getenv("JAH_GATHER_DUMP")) {
                FILE *f = std::fopen((std::string(dump) + "/nest-pcc-" + (gather ? "on" : "off") + ".ppm").c_str(), "wb");
                if (f) {
                    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
                    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) std::fwrite(&img.rgba[i], 1, 3, f);
                    std::fclose(f);
                }
            }
            std::printf("   probe arm (gather %s): %d probes, gather running %d\n",
                        gather ? "on" : "off", int(g.probeCount), int(g.gather.running));
            e->destroyScene(s);
            return std::make_pair(img, int(g.probeCount));
        };
        const auto off = shot(false);
        const auto on = shot(true);
        CHECK_MSG(off.second > 0 && on.second > 0, "the hybrid places probes (%d / %d)", off.second,
                  on.second);
        // The metal box's front face: what the probes captured, reflected.
        const Colour a = meanIn(off.first, 0.40f, 0.42f, 0.60f, 0.62f);
        const Colour b = meanIn(on.first, 0.40f, 0.42f, 0.60f, 0.62f);
        const Delta d = deltaIn(off.first, on.first, 0.40f, 0.42f, 0.60f, 0.62f);
        printDelta("the metal box's probe reflection, gather on vs off", d);
        const double la = double(a.r + a.g + a.b), lb = double(b.r + b.g + b.b);
        const double rel = la > 1e-4 ? std::fabs(lb - la) / la : 0.0;
        std::printf("   probe reflection luminance-ish: off %.4f on %.4f\n", la, lb);
        CHECK_MSG(la > 0.03, "the probe reflection is lit (%.4f)", la);
        // WHAT THIS ARM CAN AND CANNOT SAY (fix round, audit F5, measured): the
        // box reflects the front wall ABOVE the emitter's image as BLACK in both
        // arms, where the world shows that wall bounce-lit — the capture does
        // not carry this bounce, so a registration leaked into a capture (whose
        // only effect would be `vct_disable_diffuse`) would have nothing to
        // remove there. The arm stays as
        // the capture's identity guard; the planar arm above is the decisive one.
        const Colour wallOff = meanIn(off.first, 0.30f, 0.32f, 0.70f, 0.44f);
        std::printf("   the captured wall above the emitter's image (bounce-lit in the world): %.4f "
                    "red — the capture does not carry the bounce\n", double(wallOff.r));
        CHECK_MSG(rel <= 0.02,
                  "GA-NEST, probe capture: what the probes captured is within 2 %% of the "
                  "gather-off capture (%.2f %%)", 100.0 * rel);
    }
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

// ===========================================================================
// GA-SKY
// ===========================================================================
/// The SH irradiance / pi of `skyAmbientSh` at a world normal (Engine.h's basis:
/// 1, y, z, x, xy, yz, 3z^2 - 1, zx, x^2 - y^2 — already the cosine-convolved
/// mean incident radiance).
static void evalAmbientSh(const float sh[27], const Vec3 &n, double out[3])
{
    const double b[9] = { 1.0, n.y, n.z, n.x, n.x * n.y, n.y * n.z, 3.0 * n.z * n.z - 1.0,
                          n.z * n.x, n.x * n.x - n.y * n.y };
    for (int c = 0; c < 3; ++c) {
        out[c] = 0.0;
        for (int i = 0; i < 9; ++i) out[c] += double(sh[i * 3 + c]) * b[i];
    }
}

static int skyMain(Engine *e)
{
    View *view = e->createOffscreenView("sky", kSize, kSize, Colour(0, 0, 0));
    if (view) view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    Scene *s = e->createScene("sky");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    view->setShadows(true);
    armChain(view);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.gather_sky skips cleanly\n");
        return 0;
    }
    // AN OPEN FLOOR under the analytic sky with the sun at the ZENITH and its
    // disc ON: the direct sun arrives through the light, once; the sky the
    // gather's escaping rays read is the disc-free capture (ONE-ENV).
    addBox(s, matte(Colour(0.8f, 0.8f, 0.8f)), Vec3(0, -0.25f, 0), Vec3(60, 0.5f, 60));
    SkyDesc sky;
    sky.mode = SkyMode::Atmosphere;
    sky.sun.enabled = true;
    sky.sun.angularDiameterDeg = 2.0f;
    sky.sun.colour = Colour(200.0f, 200.0f, 180.0f, 1.0f);
    // Both directions point FROM the scene TOWARDS the sun: the zenith.
    sky.sun.dir[0] = 0.0f; sky.sun.dir[1] = 1.0f; sky.sun.dir[2] = 0.0f;
    sky.atmosphere.hasSun = true;
    sky.atmosphere.sunDir[0] = 0.0f; sky.atmosphere.sunDir[1] = 1.0f; sky.atmosphere.sunDir[2] = 0.0f;
    CHECK(s->setSky(sky), "the analytic sky with a zenith sun disc binds");
    enginetest::addDirectionalLight(s, Vec3(0.0f, -1.0f, 0.0001f), 3.0f);
    float sh[27] = {};
    bool shReady = false;
    for (int f = 0; f < 30 && !shReady; ++f) { render(e, 1); shReady = s->skyAmbientSh(sh); }
    CHECK(shReady, "the sky's SH is integrated");
    s->setAmbientSh(sh);
    s->setEnvironmentLight(Colour(1.0f, 1.0f, 1.0f));
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, 6.0f), Vec3(0.0f, 0.0f, 0.0f));
    GiParams gi = chainGi(true);
    CHECK(s->setGlobalIllumination(gi), "the chain builds");
    setGather(s, gi, true, true);
    // TWO DISCS, ONE PROCESS (fix round, audit F6): the sun's disc at 200 and at
    // 2,000 (x10). A miss that read the disc would carry ten times its share in
    // the second arm — at 200 the disc is 24 / 14 / 8 % of the sky's E/pi per
    // channel (the audit's 2.4 / 1.4 / 0.8 % at 20, the first cut's disc) —
    // so the two arms' ratios must agree; the 2 % bar alone was the size of the
    // disc's share and could not decide.
    double ratios[2][3] = {};
    IrrMean m;
    for (int arm = 0; arm < 2; ++arm) {
        sky.sun.colour = arm == 0 ? Colour(200.0f, 200.0f, 180.0f, 1.0f)
                                  : Colour(2000.0f, 2000.0f, 1800.0f, 1.0f);
        CHECK(s->setSky(sky), "the disc's radiance is set");
        render(e, 60);
        const GatherStatus st = s->giStatus().gather;
        CHECK_MSG(st.running && !st.irradiance.empty(), "the gather runs and reads back (%ux%u)",
                  st.irradianceW, st.irradianceH);
        // The floor's lower half of the shot: every probe there sees nothing but sky.
        m = irrIn(st, 0.2f, 0.6f, 0.8f, 0.95f);
        double ref[3];
        evalAmbientSh(sh, Vec3(0.0f, 1.0f, 0.0f), ref);
        std::printf("   disc x%d: the open floor's probe E/pi %.4f %.4f %.4f (coverage %.3f); the "
                    "disc-free sky's SH E/pi at +Y %.4f %.4f %.4f\n", arm ? 10 : 1, m.r, m.g, m.b,
                    m.w, ref[0], ref[1], ref[2]);
        const double got[3] = { m.r, m.g, m.b };
        for (int c = 0; c < 3; ++c) ratios[arm][c] = ref[c] > 1e-6 ? got[c] / ref[c] : 0.0;
    }
    for (int c = 0; c < 3; ++c) {
        CHECK_MSG(std::fabs(ratios[1][c] - ratios[0][c]) <= 0.002,
                  "GA-SKY: THE MISS DOES NOT READ THE DISC — channel %d reads %.4f with the disc "
                  "and %.4f with it ten times brighter (bar 0.002; a disc read would move it by ~%.0f %%)",
                  c, ratios[0][c], ratios[1][c], 9.0 * (c == 0 ? 24.0 : (c == 1 ? 14.0 : 8.0)));
        // THE RESIDUAL (1.016 / 1.007 / 1.004) is therefore the REFERENCE's: the
        // SH9 the sky integrates to is a band-limited clamped cosine, which reads
        // a sky with a bright zenith glow (the sun's aureole, whiter than the
        // blue sky, so largest in red) a little LOW at the zenith normal; the
        // gather's quadrature integrates the cube itself.
        CHECK_MSG(std::fabs(ratios[1][c] - 1.0) <= 0.02,
                  "GA-SKY: channel %d of the open floor's probe irradiance is the disc-free sky's "
                  "SH irradiance within 2 %% (ratio %.4f)", c, ratios[1][c]);
    }
    CHECK_MSG(m.w > 0.99, "the open floor is covered by probes (%.3f)", m.w);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

// ===========================================================================
// THE GATHER'S PLANE WEIGHT AT A CRUSHED CAGE (was GA-CAGE)
// ===========================================================================
static int planeWeightMain(Engine *e)
{
    View *view = e->createOffscreenView("cage", kSize, kSize, Colour(0, 0, 0));
    if (view) view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    if (!view) { std::printf("FAIL: view\n"); return 1; }
    view->setShadows(true);
    armChain(view);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.gather_plane_weight skips cleanly\n");
        return 0;
    }
    // A GREY WALL and, 2 cm in front of its face, a thin FENCE PANEL over its
    // left half (its edge at x = 0), lit by an emissive red panel standing in
    // front of the fence. THE PHYSICS: the wall just right of the fence's edge
    // sees the same half-space the fence's face sees, less a sliver 2 cm deep —
    // so its irradiance is the irradiance the SAME wall has with no fence at all,
    // to within that sliver. What a cage crushed onto one plane does wrong is
    // hand the wall the FENCE's probes (they pass a plane test 2 cm off at every
    // view distance past 2 m); what this suite measures is how much of the
    // fence's excess over the bare wall crosses the edge: THE GUARD'S WEIGHT,
    // printed. Read on the gather's own answer (the readback, E/pi): the two
    // arms are two scenes at one pose in one process, both frozen.
    const auto shot = [&](bool fence, GatherStatus &st, Image &img) {
        Scene *s = e->createScene(std::string("cage-") + (fence ? "fence" : "bare"));
        view->setScene(s);
        s->setAmbient(Colour(0.05f, 0.05f, 0.05f), Colour(0.02f, 0.02f, 0.02f));
        addBox(s, matte(Colour(0.7f, 0.7f, 0.7f)), Vec3(0, -0.25f, 0), Vec3(20, 0.5f, 20));
        addBox(s, matte(Colour(0.7f, 0.7f, 0.7f)), Vec3(0, 2.0f, -3.0f), Vec3(10, 4.0f, 0.2f));
        if (fence)
            addBox(s, matte(Colour(0.7f, 0.7f, 0.7f)), Vec3(-2.5f, 1.5f, -2.88f),
                   Vec3(5.0f, 3.0f, 0.02f));
        {
            PbrParams p = matte(Colour(0.05f, 0.05f, 0.05f));
            p.emissive = Colour(0.9f, 0.0f, 0.0f);
            addBox(s, p, Vec3(-2.5f, 1.5f, -2.3f), Vec3(3.0f, 2.0f, 0.1f));
        }
        enginetest::addDirectionalLight(s, Vec3(0.0f, -1.0f, -0.2f), 0.6f);
        enginetest::testCameraLookAt(view, Vec3(1.0f, 1.6f, 1.5f), Vec3(0.0f, 1.5f, -3.0f));
        const GiParams gi = chainGi(true);
        s->setGlobalIllumination(gi);
        setGather(s, gi, true, true);
        render(e, 150);
        st = s->giStatus().gather;
        view->readPixels(img);
        e->destroyScene(s);
    };
    // Screen columns (the fence's edge projects at x = 0.49 of the shot): the
    // wall within one probe stride right of the edge, and the fence's face left
    // of it (right of the emissive panel's own silhouette).
    const float edgeX0 = 0.50f, edgeX1 = 0.53f, faceX0 = 0.40f, faceX1 = 0.47f;
    const float y0 = 0.30f, y1 = 0.50f;
    GatherStatus fenceSt, bareSt;
    Image fenceImg, bareImg;
    shot(true, fenceSt, fenceImg);
    shot(false, bareSt, bareImg);
    CHECK_MSG(!fenceSt.irradiance.empty() && !bareSt.irradiance.empty(),
              "both arms read back (%ux%u)", fenceSt.irradianceW, fenceSt.irradianceH);
    const IrrMean edge = irrIn(fenceSt, edgeX0, y0, edgeX1, y1);
    const IrrMean bare = irrIn(bareSt, edgeX0, y0, edgeX1, y1);
    const IrrMean face = irrIn(fenceSt, faceX0, y0, faceX1, y1);
    std::printf("   E/pi red: the fence's face %.4f, the wall at the edge %.4f, the SAME wall with no "
                "fence %.4f (coverage at the edge %.3f)\n", face.r, edge.r, bare.r, edge.w);
    const double excess = face.r - bare.r;
    const double weight = std::fabs(excess) > 1e-5 ? (edge.r - bare.r) / excess : 0.0;
    std::printf("   PLANE WEIGHT: %.1f %% of the fence's excess over the bare wall "
                "crosses the edge onto the wall\n", 100.0 * weight);
    CHECK_MSG(face.n > 0u && edge.n > 0u && bare.n > 0u, "the three regions are covered (%u %u %u)",
              face.n, edge.n, bare.n);
    // TWO-SIDED, AGAINST THE FENCE'S PREDICTED OCCLUSION (the lead's ruling on
    // the audit round). The fence stands 1-3 cm proud of the wall and the red
    // emitter lies 61-81 degrees off the wall's normal, so the fence HIDES part
    // of the emitter from the wall beside its edge: all of it within 7 cm, 26 %
    // at 10 cm, 4 % at 20 cm (fence_occlusion_mc.py beside this file, the same
    // Monte Carlo; the two-sided bar this suite had before held only while every
    // probe's rays started half a voxel — 4 cm — off its surface, IN FRONT of the
    // fence). The PREDICTION for the measured pixels: the bare wall's reading
    // less the emitter light the fence takes away, 0.9 (the emitter's red
    // radiance) x the drop in the fraction of cosine-weighted directions that
    // reach the emitter's wall-facing face — averaged over the region's wall
    // points, each spread over its probe cell (a pixel reads the probes of the
    // cells around it). The other light (the sun's bounce, the ambient) is the
    // same in both arms to first order and cancels in the bare reading.
    // THE TOLERANCE, derived: one probe's 64 rays read the emitter's fraction f
    // as a binomial, sigma_probe = 0.9 sqrt(f (1 - f) / 64); the region's pixels
    // average the n probes of the cells that feed them, so the reading's sigma
    // is sigma_probe / sqrt(n); the bar is 3 of those plus the store's quantum
    // (half float: the reading x 2^-10).
    double predicted = 0.0, tolerance = 0.0, fBare = 0.0, fFence = 0.0;
    {
        const Vec3 camPos(1.0f, 1.6f, 1.5f), camTarget(0.0f, 1.5f, -3.0f);
        const auto wallPoint = [&](float px, float py, Vec3 &out) {
            Vec3 f(camTarget.x - camPos.x, camTarget.y - camPos.y, camTarget.z - camPos.z);
            float l = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
            f = Vec3(f.x / l, f.y / l, f.z / l);
            Vec3 r(-f.z, 0.0f, f.x);
            l = std::sqrt(r.x * r.x + r.z * r.z);
            r = Vec3(r.x / l, 0.0f, r.z / l);
            const Vec3 u(r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x);
            const float t = std::tan(45.0f * 0.5f * 3.14159265f / 180.0f);
            const float nx = 2.0f * px / float(kSize) - 1.0f, ny = 1.0f - 2.0f * py / float(kSize);
            const Vec3 d(f.x + (r.x * nx + u.x * ny) * t, f.y + (r.y * nx + u.y * ny) * t,
                         f.z + (r.z * nx + u.z * ny) * t);
            const float sz = (-2.9f - camPos.z) / d.z;
            out = Vec3(camPos.x + d.x * sz, camPos.y + d.y * sz, -2.9f);
            return out.x > 0.0f;                    // x <= 0 is the fence's face, not the wall's
        };
        uint32_t rng = 12345u;
        const auto uni = [&rng]() { rng = rng * 1664525u + 1013904223u; return double(rng >> 8) / 16777216.0; };
        const auto fraction = [&](const Vec3 &p, bool fence) {
            const int kDirs = 4000;
            int hit = 0;
            for (int k = 0; k < kDirs; ++k) {
                const double u = uni(), v = uni(), rr = std::sqrt(u), th = 6.283185307 * v;
                const double dx = rr * std::cos(th), dy = rr * std::sin(th), dz = std::sqrt(1.0 - u);
                const double t = (-2.35 - p.z) / dz;
                const double x = p.x + t * dx, y = p.y + t * dy;
                if (x < -4.0 || x > -1.0 || y < 0.5 || y > 2.5) continue;
                if (fence) {
                    const double t1 = (-2.89 - p.z) / dz, t2 = (-2.87 - p.z) / dz;
                    const double x1 = p.x + t1 * dx, x2 = p.x + t2 * dx;
                    const double y1 = p.y + t1 * dy;
                    if (std::min(x1, x2) <= 0.0 && y1 >= 0.0 && y1 <= 3.0) continue;
                }
                ++hit;
            }
            return double(hit) / kDirs;
        };
        const float rx0 = edgeX0 * kSize, rx1 = edgeX1 * kSize, ry0 = y0 * kSize, ry1 = y1 * kSize;
        const float kStride = 16.0f;
        int n = 0;
        for (int k = 0; k < 96; ++k) {
            const float px = float(rx0 + (rx1 - rx0) * uni()) + kStride * float(uni() - 0.5);
            const float py = float(ry0 + (ry1 - ry0) * uni()) + kStride * float(uni() - 0.5);
            Vec3 w;
            if (!wallPoint(px, py, w)) continue;
            fBare += fraction(w, false);
            fFence += fraction(w, true);
            ++n;
        }
        if (n) { fBare /= n; fFence /= n; }
        predicted = bare.r - 0.9 * (fBare - fFence);
        const double sigmaProbe = 0.9 * std::sqrt(std::max(fBare * (1.0 - fBare), 0.0) / 64.0);
        const double cells = (std::ceil((rx1 - rx0) / kStride) + 1.0) * (std::ceil((ry1 - ry0) / kStride) + 1.0);
        tolerance = 3.0 * sigmaProbe / std::sqrt(cells) + bare.r / 1024.0;
        std::printf("   THE FENCE'S OCCLUSION, predicted: the emitter's cosine-weighted fraction over the "
                    "region %.4f bare, %.4f fenced (%.0f %% hidden); the wall at the edge predicted %.4f, "
                    "measured %.4f (bare %.4f); tolerance %.4f = 3 x %.4f / sqrt(%.0f probes) + quantum\n",
                    fBare, fFence, fBare > 0 ? 100.0 * (1.0 - fFence / fBare) : 0.0, predicted, edge.r,
                    bare.r, tolerance, sigmaProbe, cells);
    }
    CHECK_MSG(std::fabs(edge.r - predicted) <= tolerance,
              "THE PLANE WEIGHT AND THE FENCE'S SHADOW: the wall beside the fence's edge reads %.4f "
              "against the %.4f its geometry predicts (the bare wall %.4f less the emitter light the "
              "fence hides; bar +- %.4f)", edge.r, predicted, bare.r, tolerance);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

// ===========================================================================
// gi.gather_letterbox — THE GATHER'S EYE BASIS UNDER A CONSTRAINED ASPECT
// (PHOTON-GATHER-1d; 1c's hand-off item 5). Under a letterboxing camera the
// picture is the target's INNER rectangle while the gather addresses the whole
// target, so its eye basis is EXPANDED to the target — the ray tier's shared
// helper (RayQueryTier::expandEyeToTarget), the reflection's and the sun
// contact's own. Without it every probe reconstructed its surface through the
// shot's basis at the TARGET's uv: a wrong world point off the floor, which the
// plane test then declines. THE FIXTURE: an open floor under the sky, shot
// square and then letterboxed to 2:1 (bars top and bottom): the inner
// rectangle's floor reads the square shot's E/pi, every band of it answered,
// and the bars hold no probe.
static int letterboxMain(Engine *e)
{
    View *view = e->createOffscreenView("letterbox", kSize, kSize, Colour(0, 0, 0));
    if (view) view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    Scene *s = e->createScene("letterbox");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    view->setShadows(true);
    armChain(view);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.gather_letterbox skips cleanly\n");
        return 0;
    }
    // THE OPEN FLOOR UNDER THE SKY (gi.gather_sky's fixture): every floor point
    // sees the same sky, so the floor's E/pi is ONE number wherever it is sampled
    // and a probe answers every floor pixel — WHEN it sits on the surface its
    // pixel shows. A probe reconstructed through the wrong basis sits off it,
    // and the plane test then declines the pixels whose surface it is not on.
    addBox(s, matte(Colour(0.8f, 0.8f, 0.8f)), Vec3(0, -0.25f, 0), Vec3(200, 0.5f, 200));
    SkyDesc sky;
    sky.mode = SkyMode::Atmosphere;
    sky.sun.enabled = true;
    sky.sun.angularDiameterDeg = 2.0f;
    sky.sun.colour = Colour(200.0f, 200.0f, 180.0f, 1.0f);
    sky.sun.dir[0] = 0.0f; sky.sun.dir[1] = 1.0f; sky.sun.dir[2] = 0.0f;
    sky.atmosphere.hasSun = true;
    sky.atmosphere.sunDir[0] = 0.0f; sky.atmosphere.sunDir[1] = 1.0f; sky.atmosphere.sunDir[2] = 0.0f;
    CHECK(s->setSky(sky), "the analytic sky binds");
    enginetest::addDirectionalLight(s, Vec3(0.0f, -1.0f, 0.0001f), 3.0f);
    float sh[27] = {};
    bool shReady = false;
    for (int f = 0; f < 30 && !shReady; ++f) { render(e, 1); shReady = s->skyAmbientSh(sh); }
    s->setAmbientSh(sh);
    s->setEnvironmentLight(Colour(1.0f, 1.0f, 1.0f));
    // A STEEP view: the floor fills the frame to its top, so the inner
    // rectangle's rows far from the centre — where a basis that ignores the bars
    // is most wrong — are floor.
    CameraDesc cam = enginetest::testCameraDescLookAt(Vec3(0.0f, 4.0f, 3.0f), Vec3(0.0f, 0.0f, -1.0f));
    view->setCamera(cam);
    GiParams gi = chainGi(true);
    CHECK(s->setGlobalIllumination(gi), "the chain builds");
    setGather(s, gi, true, true);
    render(e, 60);
    GatherStatus st = s->giStatus().gather;
    CHECK_MSG(st.running && !st.irradiance.empty(), "the gather runs and reads back (%ux%u)",
              st.irradianceW, st.irradianceH);
    const IrrMean square = irrIn(st, 0.05f, 0.05f, 0.95f, 0.95f);
    cam.constrainAspect = true;
    cam.aspect = 2.0f;            // a 2:1 shot in a square target: bars of a quarter each
    view->setCamera(cam);
    render(e, 60);
    st = s->giStatus().gather;
    const IrrMean inner = irrIn(st, 0.05f, 0.26f, 0.95f, 0.74f);
    const IrrMean edges[2] = { irrIn(st, 0.05f, 0.26f, 0.95f, 0.32f), irrIn(st, 0.05f, 0.68f, 0.95f, 0.74f) };
    const IrrMean bar = irrIn(st, 0.05f, 0.80f, 0.95f, 0.98f, false);
    std::printf("   the floor's E/pi: square shot %.4f %.4f %.4f (coverage %.4f); the 2:1 shot's inner "
                "rectangle %.4f %.4f %.4f (coverage %.4f; its top and bottom bands %.4f / %.4f); the "
                "bar's coverage %.4f\n", square.r, square.g, square.b, square.w, inner.r, inner.g,
                inner.b, inner.w, edges[0].w, edges[1].w, bar.w);
    const double worst = std::max(std::fabs(inner.r / std::max(square.r, 1e-6) - 1.0),
                                  std::max(std::fabs(inner.g / std::max(square.g, 1e-6) - 1.0),
                                           std::fabs(inner.b / std::max(square.b, 1e-6) - 1.0)));
    CHECK_MSG(worst <= 0.02, "the letterboxed floor reads the square shot's E/pi within %.2f %% (bar 2 %%)",
              100.0 * worst);
    CHECK_MSG(std::min(edges[0].w, edges[1].w) >= 0.99,
              "EVERY PROBE OF THE LETTERBOXED SHOT SITS ON ITS SURFACE: the inner rectangle's top and "
              "bottom bands are answered %.4f / %.4f (bar 0.99)", edges[0].w, edges[1].w);
    CHECK_MSG(bar.w <= 0.01, "the bars hold no probe (coverage %.3f)", bar.w);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

// ===========================================================================
// gi.gather_edge — GA-EDGE (PHOTON-GATHER-1d fix round)
// ===========================================================================
// A pixel on a SILHOUETTE belongs to the surface it is on. The selftest's B1
// showed 1-px bright lines along the blocks' top silhouettes: the integrate's
// four grid probes all sat on OTHER surfaces there (the sky has none, the
// neighbour cells' probes are on the floor or the wall), every plane test
// failed, the pixel's coverage fell to 0 and the field's cage — a different,
// brighter estimate — owned it. THE FIXTURE: a grey block on a sky-lit floor,
// seen from below its top so its top edge stands against the sky and its two
// sides against the far floor and sky: the silhouette row (and column, both
// sides) must read within the store's quantum of the row (column) inside it,
// and the gather must answer it.
static int edgeMain(Engine *e)
{
    View *view = e->createOffscreenView("edge", kSize, kSize, Colour(0, 0, 0));
    if (view) view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    Scene *s = e->createScene("edge");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    view->setShadows(true);
    armChain(view);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.gather_edge skips cleanly\n");
        return 0;
    }
    addBox(s, matte(Colour(0.8f, 0.8f, 0.8f)), Vec3(0, -0.25f, 0), Vec3(200, 0.5f, 200));
    addBox(s, matte(Colour(0.5f, 0.5f, 0.5f)), Vec3(0, 1.0f, 0), Vec3(2.0f, 2.0f, 2.0f));
    SkyDesc sky;
    sky.mode = SkyMode::Atmosphere;
    sky.sun.enabled = true;
    sky.sun.angularDiameterDeg = 2.0f;
    sky.sun.colour = Colour(200.0f, 200.0f, 180.0f, 1.0f);
    sky.sun.dir[0] = 0.3f; sky.sun.dir[1] = 0.8f; sky.sun.dir[2] = 0.5f;
    sky.atmosphere.hasSun = true;
    sky.atmosphere.sunDir[0] = 0.3f; sky.atmosphere.sunDir[1] = 0.8f; sky.atmosphere.sunDir[2] = 0.5f;
    CHECK(s->setSky(sky), "the analytic sky binds");
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.8f, -0.5f), 3.0f);
    float sh[27] = {};
    bool shReady = false;
    for (int f = 0; f < 30 && !shReady; ++f) { render(e, 1); shReady = s->skyAmbientSh(sh); }
    s->setAmbientSh(sh);
    s->setEnvironmentLight(Colour(1.0f, 1.0f, 1.0f));
    // Below the block's top (1.6 m < 2 m): the front face's top edge is the
    // silhouette against the sky.
    view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.3f, 1.6f, 5.0f), Vec3(0.0f, 1.2f, 0.0f)));
    GiParams gi = chainGi(true);
    gi.ddgi = GiToggle::On;              // the field's cage is what a declined pixel falls to
    CHECK(s->setGlobalIllumination(gi), "the chain builds (with the field)");
    setGather(s, gi, true, true);
    render(e, 90);
    GatherStatus st = s->giStatus().gather;
    CHECK_MSG(st.running && !st.irradiance.empty(), "the gather runs and reads back (%ux%u)",
              st.irradianceW, st.irradianceH);
    Image img;
    CHECK(view->readPixels(img), "the picture reads back");
    // THE BLOCK'S PIXELS: the gather answers them and the sky has no surface, so
    // the readback's DEPTH-VALID mask is the block + the floor; the block is the
    // part above the floor's horizon row at the centre column.
    const auto lum = [&](unsigned x, unsigned y) {
        const unsigned char *p = &img.rgba[(size_t(y) * img.width + x) * 4u];
        return double(p[0]) + double(p[1]) + double(p[2]);
    };
    const auto irr = [&](unsigned x, unsigned y) { return &st.irradiance[(size_t(y) * st.irradianceW + x) * 4u]; };
    const auto isSurface = [&](unsigned x, unsigned y) {
        const float *v = irr(x, y);
        return v[0] + v[1] + v[2] > 0.0f || v[3] > 0.0f;   // the sky writes (0,0,0,0)
    };
    // The top silhouette: the first surface row from the top in the middle columns.
    unsigned yTop = 0;
    const unsigned cx = kSize / 2u;
    for (unsigned y = 0; y < kSize; ++y) if (isSurface(cx, y)) { yTop = y; break; }
    // The side silhouettes at the block's mid height.
    const unsigned yMid = yTop + 12u;
    unsigned xL = 0, xR = kSize - 1u;
    for (unsigned x = 0; x < kSize; ++x) if (isSurface(x, yMid)) { xL = x; break; }
    for (unsigned x = kSize; x-- > 0;) if (isSurface(x, yMid)) { xR = x; break; }
    // The row / column means over the middle of the edge, and the gather's
    // coverage there.
    double top = 0, below = 0, covTop = 0; unsigned nTop = 0;
    for (unsigned x = xL + 4u; x + 4u < xR; ++x) {
        if (!isSurface(x, yTop) || !isSurface(x, yTop + 1u)) continue;
        top += lum(x, yTop); below += lum(x, yTop + 1u); covTop += irr(x, yTop)[3]; ++nTop;
    }
    double left = 0, leftIn = 0, right = 0, rightIn = 0, covL = 0, covR = 0; unsigned nSide = 0;
    for (unsigned y = yTop + 4u; y < yTop + 24u && y < kSize; ++y) {
        left += lum(xL, y); leftIn += lum(xL + 1u, y); covL += irr(xL, y)[3];
        right += lum(xR, y); rightIn += lum(xR - 1u, y); covR += irr(xR, y)[3];
        ++nSide;
    }
    if (nTop) { top /= nTop; below /= nTop; covTop /= nTop; }
    if (nSide) { left /= nSide; leftIn /= nSide; right /= nSide; rightIn /= nSide; covL /= nSide; covR /= nSide; }
    // THE STORE'S QUANTUM: the picture is 8-bit, three channels summed — one
    // code a channel is 3 in these sums.
    const double kQuantum = 3.0;
    std::printf("   the silhouette (row %u, columns %u..%u): top %.2f vs the row below %.2f (coverage %.3f); "
                "left %.2f vs inside %.2f (coverage %.3f); right %.2f vs inside %.2f (coverage %.3f)\n",
                yTop, xL, xR, top, below, covTop, left, leftIn, covL, right, rightIn, covR);
    CHECK_MSG(nTop > 8u && nSide > 8u, "the block's silhouettes were found (%u / %u samples)", nTop, nSide);
    CHECK_MSG(std::fabs(top - below) <= kQuantum,
              "THE TOP SILHOUETTE BELONGS TO ITS SURFACE: its row reads %.2f against %.2f inside "
              "(bar: the store's quantum, %.0f summed codes)", top, below, kQuantum);
    CHECK_MSG(std::fabs(left - leftIn) <= kQuantum && std::fabs(right - rightIn) <= kQuantum,
              "...AND BOTH SIDES: left %.2f / %.2f, right %.2f / %.2f", left, leftIn, right, rightIn);
    CHECK_MSG(covTop >= 0.99 && covL >= 0.99 && covR >= 0.99,
              "THE GATHER ANSWERS THE SILHOUETTE: coverage top %.3f, left %.3f, right %.3f (bar 0.99)",
              covTop, covL, covR);
    // THE EDGE-ON TOP: the camera just above the block's top, which is then a
    // strip three pixels tall that few probes land on. Its pixels must stay on
    // the gather (the silhouette rule's two steps): measured 0.32 of them
    // answered before the rule, 0.80 with it — the rest are the strip's far
    // edge, two metres behind every probe in reach, which only a probe ON the
    // top can answer (placement's quarter samples miss a 3-px strip; recorded).
    {
        view->setCamera(enginetest::testCameraDescLookAt(Vec3(0.3f, 2.12f, 5.0f), Vec3(0.0f, 1.2f, 0.0f)));
        render(e, 90);
        st = s->giStatus().gather;
        unsigned yt = 0;
        for (unsigned y = 0; y < kSize; ++y) if (isSurface(cx, y)) { yt = y; break; }
        double cov = 0.0;
        unsigned n = 0;
        for (unsigned y = yt; y < yt + 3u; ++y)
            for (unsigned x = 80; x < 180; ++x) { cov += irr(x, y)[3]; ++n; }
        cov = n ? cov / n : 0.0;
        std::printf("   the edge-on top (rows %u..%u): gather coverage %.3f\n", yt, yt + 2u, cov);
        CHECK_MSG(cov >= 0.75, "AN EDGE-ON TOP STAYS ON THE GATHER: %.3f of its strip answered (bar 0.75; "
                  "0.32 before the silhouette rule)", cov);
    }
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-gather-" + mode + "-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    if (mode == "glass") return glassMain(e);
    if (mode == "nest") return nestMain(e);
    if (mode == "sky") return skyMain(e);
    if (mode == "plane_weight") return planeWeightMain(e);
    if (mode == "letterbox") return letterboxMain(e);
    if (mode == "edge") return edgeMain(e);
    std::printf("FAIL: unknown mode '%s' (glass | nest | sky | plane_weight)\n", mode.c_str());
    return 1;
}
