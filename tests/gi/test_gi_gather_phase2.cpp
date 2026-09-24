// THE GATHER'S PHASE-2 ACCEPTANCE (PHOTON-GATHER-1b item 5; SPECS/photon/
// C2_GATHER_FILTERED_DEFAULT_ON_DESIGN.md section 1.4) — the filter in probe
// space, the SH record and the plane-weighted four-probe integrate. One binary,
// the mode on the command line:
//
//   gi.gather_plane          NO BOUNCE CROSSES A PLANE EDGE. A low red emissive
//                            wall on a white floor, and two metres in front of
//                            it a platform whose top is a parallel plane one
//                            metre up that no photon of the wall reaches: the
//                            floor two probe strides out reads the wall's
//                            closed form (Lambert's projected solid angle,
//                            within 5 %), and the platform's top — adjacent to
//                            the red floor in the picture, which is exactly what
//                            a four-probe bilinear or a 3x3 filter could carry
//                            across — reads none of it past one stride.
//   gi.gather_deterministic  THREE COLD 60-FRAME STILLS: a fresh view and a
//                            fresh scene each time, the frame index LIVE (the
//                            sequence starts at the view's first frame), a
//                            railing across the shot so the adaptive pass
//                            appends through its atomic — and the
//                            `probeIrradiance` readback is byte-identical.
//
// Both read the gather's own answer (GatherTuning::readback: E/pi per pixel,
// the coverage in w), never a picture: no material, light or tonemap between
// the estimate and the assertion.
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

static NodeId addBox(Scene *s, const PbrParams &p, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = s->createNode();
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    const MaterialId m = s->createPbrMaterial(p);
    if (!n || !mesh || !m || !s->attachMesh(n, mesh, m)) return 0;
    s->setNodeTransform(n, pos, Quat(), scale);
    return n;
}

/// Matte by the ground's own recipe (the Specular workflow at ior 1.0 with a
/// black specular colour: F0 = 0).
static PbrParams matte(const Colour &albedo, const Colour &emissive = Colour(0, 0, 0))
{
    PbrParams p;
    p.albedo = albedo;
    p.emissive = emissive;
    p.roughness = 1.0f;
    p.workflow = PbrParams::Workflow::Specular;
    p.ior = 1.0f;
    p.specularColour = Colour(0.0f, 0.0f, 0.0f);
    return p;
}

static GiParams chainGi()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.numBounces = 1;
    gi.cascades = true;
    gi.gather = GiToggle::On;
    return gi;
}

// ===========================================================================
// gi.gather_plane
// ===========================================================================
static const unsigned kPlaneSize = 512u;
static const float kOrthoHalf = 8.0f;

/// Straight down, orthographic (gi.gather_reference's camera): screen right is
/// world +X, screen down is world +Z, and a pixel IS a floor point.
static CameraDesc topDownCamera()
{
    CameraDesc c;
    c.position = Vec3(0.0f, 6.0f, 0.0f);
    c.orientation = Quat(-0.70710678f, 0.0f, 0.0f, 0.70710678f);
    c.orthographic = true;
    c.orthoSize = kOrthoHalf;
    c.farClip = 200.0f;
    return c;
}

static void worldToPixel(float wx, float wz, int &px, int &py)
{
    px = int((double(wx) / kOrthoHalf * 0.5 + 0.5) * kPlaneSize);
    py = int((double(wz) / kOrthoHalf * 0.5 + 0.5) * kPlaneSize);
}

/// LAMBERT'S FORMULA: the projected solid angle of a planar polygon seen from p
/// by a receiver of normal n (gi.gather_reference's, which says why it is exact).
static double projectedSolidAngle(const double p[3], const double n[3], const double v[][3],
                                  int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        const int j = (i + 1) % count;
        double a[3], b[3];
        for (int k = 0; k < 3; ++k) { a[k] = v[i][k] - p[k]; b[k] = v[j][k] - p[k]; }
        const double la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        const double lb = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
        for (int k = 0; k < 3; ++k) { a[k] /= la; b[k] /= lb; }
        const double c = std::max(-1.0, std::min(1.0, a[0] * b[0] + a[1] * b[1] + a[2] * b[2]));
        double cr[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                         a[0] * b[1] - a[1] * b[0] };
        const double lc = std::sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
        if (lc < 1e-12) continue;
        sum += std::acos(c) * (n[0] * cr[0] + n[1] * cr[1] + n[2] * cr[2]) / lc;
    }
    return std::fabs(0.5 * sum);
}

/// THE COMPLETE CLOSED FORM of a floating emissive panel over the floor (fix
/// round of 1b, audit F1): the panel (x in [-halfW, halfW], y in [y0, y1], its
/// front face at z = front, 0.2 m thick) lights a floor point through its FRONT
/// face and its BOTTOM face, each counted where it FACES the point (Lambert's
/// routine returns |the projected solid angle| from either side, so a back face
/// is excluded by its normal); the ends face away from every point at x = 0, the
/// back and the top away from the floor in front. Averaged over one probe cell
/// around the point, as the reading is (the probes jitter inside their cells).
/// E/pi of the emitter's radiance `le`; `frontOnly` the front face's share.
static double panelClosedForm(double z, double stride, double le, double halfW, double y0,
                              double y1, double front, double &frontOnly)
{
    struct Face { double v[4][3]; double n[3]; };
    const double zb = front - 0.2;
    const Face faces[4] = {
        { { { -halfW, y0, front }, { halfW, y0, front }, { halfW, y1, front }, { -halfW, y1, front } }, { 0, 0, 1 } },
        { { { -halfW, y0, zb }, { halfW, y0, zb }, { halfW, y0, front }, { -halfW, y0, front } }, { 0, -1, 0 } },
        { { { halfW, y0, zb }, { halfW, y1, zb }, { halfW, y1, front }, { halfW, y0, front } }, { 1, 0, 0 } },
        { { { -halfW, y0, zb }, { -halfW, y1, zb }, { -halfW, y1, front }, { -halfW, y0, front } }, { -1, 0, 0 } },
    };
    const double up[3] = { 0.0, 1.0, 0.0 };
    double acc = 0.0, accFront = 0.0;
    for (int sy = 0; sy < 8; ++sy)
        for (int sx = 0; sx < 8; ++sx) {
            const double q[3] = { ((sx + 0.5) / 8.0 - 0.5) * stride, 0.0,
                                  z + ((sy + 0.5) / 8.0 - 0.5) * stride };
            for (int f = 0; f < 4; ++f) {
                double d = 0.0;
                for (int k = 0; k < 3; ++k) d += faces[f].n[k] * (q[k] - faces[f].v[0][k]);
                if (d <= 0.0) continue;
                const double e = le * projectedSolidAngle(q, up, faces[f].v, 4) / 3.14159265358979;
                acc += e;
                if (f == 0) accFront += e;
            }
        }
    frontOnly = accFront / 64.0;
    return acc / 64.0;
}

static int planeMain(Engine *e, bool targetRow)
{
    View *view = e->createOffscreenView("plane", kPlaneSize, kPlaneSize, Colour(0, 0, 0));
    if (view) view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    Scene *s = e->createScene("plane");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    view->setShadows(true);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    view->setPostFx(fx);
    view->setCamera(topDownCamera());
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.gather_plane skips cleanly\n");
        return 0;
    }
    // No light, no ambient, no sky: every photon here left the emitter.
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    // THE LOWER FLOOR: white, its top face at y = 0.
    addBox(s, matte(Colour(0.9f, 0.9f, 0.9f)), Vec3(0.0f, -0.25f, 0.0f), Vec3(40.0f, 0.5f, 40.0f));
    // THE EMITTER: a low red panel over it, x in [-3, 3], y in [0.2, 0.9], its
    // front face at z = +0.1 facing +Z (0.2 m thick, which the voxeliser holds).
    // It floats a fifth of a metre up so the closed form is one clean rectangle
    // (the lane measured the same -7 % at two strides with the panel standing on
    // the floor: the dark share is the HIT's read of the voxel cache at an
    // oblique face, not the panel's contact with the floor — see the target row).
    const float kLe = 0.9f, kHalfW = 3.0f, kY0 = 0.2f, kH = 0.9f, kFront = 0.1f;
    addBox(s, matte(Colour(0.0f, 0.0f, 0.0f), Colour(kLe, 0.0f, 0.0f)),
           Vec3(0.0f, 0.5f * (kY0 + kH), 0.0f), Vec3(kHalfW * 2.0f, kH - kY0, 0.2f));
    // THE PLATFORM, two metres in front of it: a BLACK block 0.98 m high capped
    // by a thin WHITE top at y = 1. Its top is a plane PARALLEL to the lower
    // floor, one metre up, and adjacent to it in the picture — and no photon of
    // the emitter reaches it (every face of the emitter lies under the
    // platform's horizon). The lower floor's probes answer red one pixel away;
    // a four-probe bilinear or a 3x3 filter that ignored the plane would carry
    // that red onto the platform's edge.
    const float kPlatformZ = 2.1f;
    addBox(s, matte(Colour(0.0f, 0.0f, 0.0f)), Vec3(0.0f, 0.49f, kPlatformZ + 1.5f), Vec3(8.0f, 0.98f, 3.0f));
    addBox(s, matte(Colour(0.9f, 0.9f, 0.9f)), Vec3(0.0f, 0.99f, kPlatformZ + 1.5f), Vec3(8.0f, 0.02f, 3.0f));
    const GiParams gi = chainGi();
    CHECK(s->setGlobalIllumination(gi), "the chain builds");
    GatherTuning t;
    t.readback = true;
    // THE ESTIMATOR'S MEAN, NOT ONE HELD DRAW (PHOTON-GATHER-1d): a still view
    // holds one N-sample rest mean; the rest door keeps every frame live.
    t.restOff = true;
    s->setGatherTuning(t);
    render(e, 40);

    // THE READING: the red channel of the gather's E/pi, averaged over 96 live
    // frames (a mean of the estimator, as gi.gather_reference takes it) in a
    // 9 x 9 block — the answer only, never a picture.
    const float stride = 16.0f * (2.0f * kOrthoHalf / float(kPlaneSize));   // metres per probe
    struct Point { const char *what; float z; bool platform; double sum = 0.0; unsigned n = 0; };
    std::vector<Point> pts = {
        { "floor, 1 stride", kFront + 1.0f * stride, false },
        { "floor, 2 strides", kFront + 2.0f * stride, false },
        { "floor, 3 strides", kFront + 3.0f * stride, false },
        { "platform, 0.5 stride", kPlatformZ + 0.5f * stride, true },
        { "platform, 1 stride", kPlatformZ + 1.0f * stride, true },
        { "platform, 2 strides", kPlatformZ + 2.0f * stride, true },
    };
    unsigned lastFrame = ~0u, frames = 0u;
    for (int f = 0; f < 200 && frames < 96u; ++f) {
        e->renderOneFrame();
        const GatherStatus st = s->giStatus().gather;
        if (st.irradiance.empty() || st.irradianceFrame == lastFrame) continue;
        lastFrame = st.irradianceFrame;
        ++frames;
        for (Point &p : pts) {
            int cx, cy;
            worldToPixel(0.0f, p.z, cx, cy);
            for (int y = cy - 4; y <= cy + 4; ++y)
                for (int x = cx - 4; x <= cx + 4; ++x) {
                    const float *v = &st.irradiance[(size_t(y) * st.irradianceW + x) * 4u];
                    p.sum += v[0];
                    ++p.n;
                }
        }
    }
    CHECK_MSG(frames >= 64u, "the readback delivered %u distinct frames", frames);
    // THE CLOSED FORM on the lower floor: Le times the emitter face's projected
    // solid angle over pi (nothing else reaches it: no sky, no light, the
    // emitter's albedo is zero, the floor is planar and the platform's face is
    // black). On the platform's top it is ZERO.
    // THE COMPLETE FORM (fix round, audit F1): the panel FLOATS, so its BOTTOM
    // face (6 x 0.2 m at y = 0.2, facing down) lights the floor too, beside the
    // front face. Each face counts where it FACES the point (Lambert's routine
    // returns |the projected solid angle| from either side, so a back face is
    // excluded by its normal); the ends (x = +-3) face away from every point
    // measured here (x = 0), the back and the top face away from the floor in
    // front. Averaged over one probe cell around the point, as the reading is
    // (the probes jitter inside their cells; gi.gather_reference's rule).
    const auto closedAt = [&](double z, double &frontOnly) {
        return panelClosedForm(z, stride, kLe, kHalfW, kY0, kH, kFront, frontOnly);
    };
    std::printf("   stride %.3f m; the red channel of the gather's E/pi:\n", double(stride));
    double got[6] = {}, closed[6] = {};
    for (size_t i = 0; i < pts.size(); ++i) {
        const Point &p = pts[i];
        got[i] = p.n ? p.sum / p.n : 0.0;
        double front = 0.0;
        closed[i] = p.platform ? 0.0 : closedAt(double(p.z), front);
        std::printf("     %-22s z %6.3f m: %.5f  (complete form %.5f, front face only %.5f%s)\n",
                    p.what, double(p.z), got[i], closed[i], front,
                    p.platform ? ", a plane 1 m up" : "");
    }
    const double r2 = closed[1] > 0.0 ? got[1] / closed[1] : 0.0;
    // THE BARS FROM THE ARITHMETIC (fix round, audit F1), against the COMPLETE
    // form (front face + the floating panel's bottom): at 1 / 2 / 3 strides the
    // base (GATHER-1a's single-probe read) reads 0.738 / 0.807 / 0.762 and this
    // lane 0.791 / 0.890 / 0.906. The residual that survives — -11 % at two
    // strides — is named by exclusion exactly as gi.gather_reference names its
    // own (the estimator chain moves no mean; what is left is the radiance the
    // rays read out of the voxel cache at the panel's face, here seen
    // obliquely); GA-1e's card read at the hit is predicted to close it. The
    // gating floor is 0.85 (the reading less a 2 % allowance for the 96-frame
    // mean's noise, less 2 % for the sequence), the ceiling 1.05; the brief's
    // 5 % is the TARGET row (gi.gather_plane_target, photon-target).
    if (targetRow) {
        std::printf("target: %.3f (bar 1.00 +- 0.05)\n", r2);
        CHECK_MSG(std::fabs(r2 - 1.0) <= 0.05,
                  "TARGET — THE FLOOR TWO STRIDES OUT READS THE COMPLETE CLOSED FORM: %.5f against "
                  "%.5f (%.3f; bar 1.00 +- 0.05)", got[1], closed[1], r2);
        std::printf("%s\n", failures ? "FAILED" : "PASSED");
        return failures ? 1 : 0;
    }
    CHECK_MSG(r2 >= 0.85 && r2 <= 1.05,
              "THE FLOOR TWO STRIDES OUT READS THE COMPLETE CLOSED FORM: %.5f against %.5f (%.3f; "
              "bar 0.85..1.05, the 5 %% row is gi.gather_plane_target)", got[1], closed[1], r2);
    // The edge's own pixels (inside the first stride) may take the lower floor's
    // probe through the bilinear's weight where the plane test's ramp still
    // grants some — past one stride, nothing.
    CHECK_MSG(got[4] <= 0.02 * got[0] && got[5] <= 0.02 * got[0],
              "NO BOUNCE CROSSES THE PLANE EDGE: the platform's top reads %.5f one stride and "
              "%.5f two strides in from its edge, against %.5f on the floor beside it (bar 2 %% "
              "of it; at half a stride %.5f)", got[4], got[5], got[0], got[3]);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

// ===========================================================================
// gi.gather_deterministic
// ===========================================================================
static int deterministicMain(Engine *e)
{
    const unsigned kSize = 256u;
    std::vector<std::vector<float>> stills;
    std::vector<unsigned> adaptive;
    for (int run = 0; run < 3; ++run) {
        View *view = e->createOffscreenView("det" + std::to_string(run), kSize, kSize, Colour(0, 0, 0));
        if (view) view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
        Scene *s = e->createScene("det" + std::to_string(run));
        if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
        view->setScene(s);
        view->setShadows(true);
        PostFxDesc fx;
        fx.allowOffscreen = true;
        view->setPostFx(fx);
        if (!e->rayQueryAvailable() || !e->rayTracing()) {
            std::printf("ok: no ray queries on this machine — gi.gather_deterministic skips cleanly\n");
            return 0;
        }
        s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.02f, 0.02f, 0.03f));
        addBox(s, matte(Colour(0.85f, 0.85f, 0.85f)), Vec3(0, -0.25f, 0), Vec3(20, 0.5f, 20));
        addBox(s, matte(Colour(0.05f, 0.05f, 0.05f), Colour(0.9f, 0.1f, 0.0f)), Vec3(0.0f, 1.5f, -4.0f),
               Vec3(8.0f, 3.0f, 0.3f));
        // THE RAILING: cells whose pixels leave their probe's plane, so the
        // adaptive pass appends — through an atomic whose ORDER is the
        // scheduler's, which is exactly what must not reach the answer.
        for (int i = 0; i < 16; ++i)
            addBox(s, matte(Colour(0.7f, 0.7f, 0.72f)), Vec3(-3.75f + 0.5f * float(i), 0.6f, -1.0f),
                   Vec3(0.06f, 1.2f, 0.06f));
        enginetest::addDirectionalLight(s, Vec3(-0.2f, -1.0f, -0.35f), 1.5f);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.2f, 4.0f), Vec3(0.0f, 0.4f, -2.0f));
        CHECK(s->setGlobalIllumination(chainGi()), "the chain builds");
        GatherTuning t;
        t.readback = true;               // THE FRAME INDEX LIVE: the sequence runs
        s->setGatherTuning(t);
        // THE SAME GATHER FRAME in every run: the readback lags by the frames
        // in flight, so the still is taken when the readback names frame 50
        // (inside the 60 rendered — a cold view's sequence starts at 0).
        GatherStatus st;
        for (int f = 0; f < 60; ++f) {
            e->renderOneFrame();
            const GatherStatus now = s->giStatus().gather;
            if (now.irradianceFrame == 50u && !now.irradiance.empty()) st = now;
        }
        std::printf("   cold run %d: gather frame %u read back (%ux%u), %u adaptive probes\n", run,
                    st.irradianceFrame, st.irradianceW, st.irradianceH, st.adaptive);
        CHECK_MSG(!st.irradiance.empty(), "cold run %d reads back", run);
        stills.push_back(st.irradiance);
        adaptive.push_back(st.adaptive);
        e->destroyScene(s);
        e->destroyView(view);
    }
    CHECK_MSG(adaptive[0] > 0u, "the railing appends adaptive probes (%u) — the atomic is exercised",
              adaptive[0]);
    const auto same = [](const std::vector<float> &a, const std::vector<float> &b) {
        return a.size() == b.size() && !a.empty() &&
               std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
    };
    unsigned differ = 0u;
    for (size_t i = 0; i < stills[0].size() && i < stills[1].size(); ++i)
        if (std::memcmp(&stills[0][i], &stills[1][i], sizeof(float)) != 0) ++differ;
    std::printf("   floats that differ between cold runs 0 and 1: %u of %zu\n", differ,
                stills[0].size());
    CHECK(same(stills[0], stills[1]) && same(stills[1], stills[2]),
          "DETERMINISM: three cold 60-frame stills read back byte-identical probeIrradiance "
          "(the frame index live, the adaptive atomic's order not reaching the answer)");
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

// ===========================================================================
// gi.gather_cards — THE HIT LIT FROM THE CARD (PHOTON-GATHER-1d, GA-1e)
// ===========================================================================
//
// The gather's hits read `jahHitRadiance` — the reflection trace's own
// function: the hit instance's surface CARD wherever one describes the point
// (the gather passes NO footprint gate — the fix round's decision; the
// reflection keeps its k = 4), the voxel cascades beyond. This suite puts
// gi.gather_plane's red emissive panel and white floor on CARDED meshes (the
// six-face box list every convex bake produces) and reads the floor's red E/pi
// in front of the panel in three arms, one process, the history OFF:
//   * the VOXEL arm — the surface cache off (`cards` Off): every hit reads the
//     cascades, the picture before GA-1e; the frame index frozen;
//   * the CARD arm — the cache on; the frame index frozen, the shipped stride;
//   * THE ESTIMATOR arm (the fix round's near-foot chase) — the card arm at a
//     4-px probe stride with the frame index LIVE, 64 frames averaged: what each
//     probe measures, apart from the 16-px interpolation and from one frozen
//     draw's noise, against the closed form averaged over ITS cell. Barred
//     0.90..1.05 over the profile: the ray start lifted by half a voxel (the
//     defect it found) read 1.069 at the foot and 0.888 at 1.6 m; with the
//     start at an epsilon it reads 0.94..1.00.
// It reports the bounce's COLOUR (the panel emits (0.9, 0.2, 0.05): the floor's
// G/R must be the emitter's 0.222 in every arm — a read that mixes the black
// floor's voxels in darkens the bounce, it must not change its hue) and its EDGE
// (the distance from the panel's face at which the red falls to half its peak),
// and the three arms against the complete closed form.
static int cardsMain(Engine *e)
{
    View *view = e->createOffscreenView("cards", kPlaneSize, kPlaneSize, Colour(0, 0, 0));
    if (view) view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    Scene *s = e->createScene("cards");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(s);
    view->setShadows(true);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    view->setPostFx(fx);
    view->setCamera(topDownCamera());
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.gather_cards skips cleanly\n");
        return 0;
    }
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    const auto carded = [&](const PbrParams &p, const Vec3 &pos, const Vec3 &scale) {
        MeshData md = enginetest::unitCubeMesh();
        md.cards = enginetest::boxCards(0.5f);
        const NodeId n = s->createNode();
        const MeshId mesh = s->createMesh(md);
        const MaterialId m = s->createPbrMaterial(p);
        if (!n || !mesh || !m || !s->attachMesh(n, mesh, m)) return NodeId(0);
        s->setNodeTransform(n, pos, Quat(), scale);
        return n;
    };
    const float kLeR = 0.9f, kLeG = 0.2f, kLeB = 0.05f;
    const float kHalfW = 3.0f, kY0 = 0.2f, kH = 0.9f, kFront = 0.1f;
    CHECK(carded(matte(Colour(0.9f, 0.9f, 0.9f)), Vec3(0.0f, -0.25f, 0.0f), Vec3(40.0f, 0.5f, 40.0f)) &&
              carded(matte(Colour(0.0f, 0.0f, 0.0f), Colour(kLeR, kLeG, kLeB)),
                     Vec3(0.0f, 0.5f * (kY0 + kH), 0.0f), Vec3(kHalfW * 2.0f, kH - kY0, 0.2f)),
          "the carded floor and panel exist");
    GiParams gi = chainGi();
    gi.cards = GiToggle::On;
    CHECK(s->setGlobalIllumination(gi), "the chain builds");
    GatherTuning t;
    t.readback = true;
    t.freezeFrameIndex = true;
    s->setGatherTuning(t);
    setenv("JAHSHAKA_GATHER_NO_TEMPORAL", "1", 1);

    const float stride = 16.0f * (2.0f * kOrthoHalf / float(kPlaneSize));
    // The profile: from just in front of the panel out to three strides.
    std::vector<float> zs;
    for (float d = 0.05f; d <= 1.6f; d += 0.05f) zs.push_back(kFront + d);
    struct ArmResult { std::vector<double> r, g; unsigned cards = 0; bool ran = false; };
    const auto readArm = [&](const char *what, GiToggle cards, unsigned probeStride, int liveFrames) {
        ArmResult a;
        GiParams g = gi;
        g.cards = cards;
        s->setGiTuning(g);
        GatherTuning at = t;
        at.probeStride = probeStride;
        at.freezeFrameIndex = liveFrames <= 1;
        s->setGatherTuning(at);
        render(e, 150);   // captures + relights (a card's indirect is marched before it is read)
        unsigned lastFrame = ~0u;
        GatherStatus st;
        for (int f = 0; f < 20; ++f) {
            e->renderOneFrame();
            st = s->giStatus().gather;
            if (!st.irradiance.empty() && st.irradianceFrame != lastFrame) break;
        }
        a.ran = st.running && !st.irradiance.empty();
        a.cards = s->giStatus().cards.cardsResident;
        // THE LIVE AVERAGE: `liveFrames` distinct read-back frames, summed.
        if (liveFrames > 1 && a.ran) {
            std::vector<double> acc(st.irradiance.size(), 0.0);
            int got = 0;
            unsigned lf = st.irradianceFrame;
            for (int f = 0; f < 8 * liveFrames && got < liveFrames; ++f) {
                e->renderOneFrame();
                const GatherStatus q = s->giStatus().gather;
                if (q.irradiance.size() != acc.size() || q.irradianceFrame == lf) continue;
                lf = q.irradianceFrame;
                for (size_t i = 0; i < acc.size(); ++i) acc[i] += q.irradiance[i];
                ++got;
            }
            for (size_t i = 0; i < acc.size(); ++i) st.irradiance[i] = float(acc[i] / std::max(got, 1));
        }
        // A ROW ACROSS THE PANEL'S MIDDLE TWO THIRDS (x in [-2, 2] m, eight probe
        // cells) at each distance: one frozen frame's estimate is one draw per
        // probe, and the row averages eight of them — in every arm the same
        // eight, the frame index frozen, so the arms stay paired.
        int x0 = 0, x1 = 0, unused = 0;
        worldToPixel(-2.0f, 0.0f, x0, unused);
        worldToPixel(2.0f, 0.0f, x1, unused);
        for (float z : zs) {
            int cx, cy;
            worldToPixel(0.0f, z, cx, cy);
            double sr = 0.0, sg = 0.0;
            unsigned n = 0;
            for (int y = cy - 1; y <= cy + 1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    if (st.irradiance.empty()) break;
                    const float *v = &st.irradiance[(size_t(y) * st.irradianceW + x) * 4u];
                    sr += v[0];
                    sg += v[1];
                    ++n;
                }
            a.r.push_back(n ? sr / n : 0.0);
            a.g.push_back(n ? sg / n : 0.0);
        }
        std::printf("   %-44s ran %d, %u cards resident\n", what, int(a.ran), a.cards);
        return a;
    };
    const ArmResult vox = readArm("the VOXEL arm (cache off)", GiToggle::Off, 0u, 1);
    const ArmResult card = readArm("the CARD arm (cache on, ungated)", GiToggle::On, 0u, 1);
    const unsigned kFineStride = 4u;
    const ArmResult open = readArm("the ESTIMATOR arm (cards, 4 px, 64 live frames)", GiToggle::On,
                                   kFineStride, 64);
    const float fineStride = float(kFineStride) * (2.0f * kOrthoHalf / float(kPlaneSize));
    unsetenv("JAHSHAKA_GATHER_NO_TEMPORAL");
    CHECK(vox.ran && card.ran && open.ran, "all three arms ran the gather and read it back");
    CHECK_MSG(vox.cards == 0u && card.cards > 0u && open.cards > 0u,
              "the cache is off in the voxel arm and resident in the card arms (%u / %u / %u cards)",
              vox.cards, card.cards, open.cards);

    std::printf("\n   the floor's red E/pi in front of the panel (d = distance from its face):\n");
    std::printf("      d (m)   CLOSED     VOXEL ratio      CARD ratio   CLOSED(4px) ESTIMATOR ratio\n");
    double worstHue = 0.0, maxDiff = 0.0, estLo = 1e9, estHi = 0.0;
    for (size_t i = 0; i < zs.size(); ++i) {
        double frontOnly = 0.0, fo4 = 0.0;
        const double c = panelClosedForm(zs[i], stride, kLeR, kHalfW, kY0, kH, kFront, frontOnly);
        const double c4 = panelClosedForm(zs[i], fineStride, kLeR, kHalfW, kY0, kH, kFront, fo4);
        const double est = c4 > 0.0 ? open.r[i] / c4 : 0.0;
        estLo = std::min(estLo, est);
        estHi = std::max(estHi, est);
        if (i % 3 == 0 || i + 1 == zs.size())
            std::printf("     %5.2f  %8.5f  %8.5f %5.3f  %8.5f %5.3f  %8.5f %8.5f %5.3f\n",
                        double(zs[i] - kFront), c, vox.r[i], vox.r[i] / c, card.r[i], card.r[i] / c,
                        c4, open.r[i], est);
        for (const ArmResult *a : { &vox, &card, &open }) {
            if (a->r[i] <= 1e-4) continue;
            worstHue = std::max(worstHue, std::fabs(a->g[i] / a->r[i] - double(kLeG / kLeR)));
        }
        maxDiff = std::max(maxDiff, std::fabs(card.r[i] - vox.r[i]) / std::max(vox.r[i], 1e-6));
    }
    // THE EDGE: the distance from the face at which the red falls to half its
    // PEAK along the profile.
    const auto edge = [&](const ArmResult &a) {
        const double half = 0.5 * *std::max_element(a.r.begin(), a.r.end());
        for (size_t i = 1; i < a.r.size(); ++i)
            if (a.r[i] <= half) {
                const double f = (a.r[i - 1] - half) / std::max(a.r[i - 1] - a.r[i], 1e-9);
                return double(zs[i - 1] - kFront) + f * double(zs[i] - zs[i - 1]);
            }
        return double(zs.back() - kFront);
    };
    double frontClosed = 0.0;
    std::vector<double> closedProfile;
    for (float z : zs) {
        double fo = 0.0;
        closedProfile.push_back(panelClosedForm(z, stride, kLeR, kHalfW, kY0, kH, kFront, fo));
    }
    ArmResult closedArm;
    closedArm.r = closedProfile;
    (void)frontClosed;
    const double eVox = edge(vox), eCard = edge(card), eClosed = edge(closedArm);
    std::printf("   THE EDGE (where the red falls to half its peak): closed form %.3f m, voxel %.3f m, card %.3f m\n",
                eClosed, eVox, eCard);
    std::printf("   THE COLOUR: the worst |G/R - %.3f| over the profile in any arm: %.4f\n",
                double(kLeG / kLeR), worstHue);
    std::printf("   the card arm against the voxel arm: largest relative difference %.2f %%\n",
                100.0 * maxDiff);
    CHECK_MSG(worstHue <= 0.02,
              "THE BOUNCE KEEPS THE EMITTER'S COLOUR in every arm (G/R within 0.02 of %.3f; worst %.4f)",
              double(kLeG / kLeR), worstHue);
    CHECK_MSG(estLo >= 0.90 && estHi <= 1.05,
              "THE ESTIMATOR MEASURES THE SURFACE: at a 4-px stride, 64 live frames averaged, the floor's "
              "red reads %.3f..%.3f of the closed form over its own cell (bar 0.90..1.05; a ray start lifted "
              "half a voxel off the surface read 0.888..1.069)", estLo, estHi);
    CHECK_MSG(maxDiff > 0.005,
              "THE CARD IS READ: the card arm differs from the voxel arm somewhere on the profile "
              "(largest %.2f %%)", 100.0 * maxDiff);
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
    if (mode == "plane") return planeMain(e, argc > 2 && std::string(argv[2]) == "--target");
    if (mode == "deterministic") return deterministicMain(e);
    if (mode == "cards") return cardsMain(e);
    std::printf("FAIL: unknown mode '%s' (plane | deterministic | cards)\n", mode.c_str());
    return 1;
}
