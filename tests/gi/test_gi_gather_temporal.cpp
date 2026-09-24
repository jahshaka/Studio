// THE GATHER'S PIXEL HISTORY (PHOTON-GATHER-1c; SPECS/photon/C2_GATHER_FILTERED_
// DEFAULT_ON_DESIGN.md section 2) — the full-resolution history behind the
// integrate. One binary, the mode on the command line:
//
//   gi.gather_stable   A STILL VIEW IS STILL. A Showroom-2-shaped room (24 x 24 m,
//                      7.25 m to the ceiling, four columns, three point lamps,
//                      nothing lit but by the lamps and their bounce), the
//                      gather on, the frame index LIVE: after the warm-up the
//                      picture moves by less than 2/255 frame to frame at the
//                      room's indirect-lit pixels. The lever's pair runs in the
//                      same process: with `JAHSHAKA_GATHER_NO_TEMPORAL` set the
//                      same pixels carry each frame's estimate alone, and the
//                      suite asserts they DO move — the history is what stills
//                      them, and the lever is what says so.
//   gi.gather_motion   A MOVING VIEW IS THE VIEW IT SETTLES TO (gi.reflect_motion's
//                      method): a truck and a yaw of 24 frames, the last moving
//                      frame against the frame 45 frames later at the SAME pose,
//                      the mean absolute difference over the room in 8-bit codes,
//                      each against a bar derived from this suite's own still
//                      control and the lever's single-frame arm (the numbers and
//                      the derivation are at the bars).
//
// (The lane's importance-sampling measurement — the reference fixture's and this
// room's estimator variance with and without the reprojected PDF — was a third
// mode of this binary; the premise was refused and the mode went with the code.
// It lives in spikes/photon-gather-1c, with the numbers.)
//
// Frames, never time; every number here is a count of frames or a code.
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
        char buf_[768];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static void render(Engine *e, int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

static void setNoTemporal(bool on)
{
    if (on) ::setenv("JAHSHAKA_GATHER_NO_TEMPORAL", "1", 1);
    else    ::unsetenv("JAHSHAKA_GATHER_NO_TEMPORAL");
}

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

static NodeId addBox(Scene *s, MeshId mesh, const PbrParams &p, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = s->createNode();
    const MaterialId m = s->createPbrMaterial(p);
    if (!n || !m || !s->attachMesh(n, mesh, m)) return 0;
    s->setNodeTransform(n, pos, Quat(), scale);
    return n;
}

static NodeId gCentreLamp = 0;
static NodeId addPointLamp(Scene *s, const Vec3 &pos, float intensity, float range)
{
    const NodeId n = s->createNode();
    if (!n) return 0;
    s->setNodeTransform(n, pos, Quat(), Vec3(1, 1, 1));
    LightDesc l;
    l.type = LightType::Point;
    l.colour = Colour(1.0f, 0.97f, 0.92f);
    l.intensity = intensity;
    l.range = range;
    l.castShadows = true;
    return s->setLight(n, l) ? n : 0;
}

static GiParams gatherGi()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;    // the gather is the diffuse, alone
    gi.numBounces = 1;
    gi.cascades = true;
    gi.gather = GiToggle::On;
    return gi;
}

/// THE SHOWROOM-2-SHAPED ROOM (the sample's own layout: a closed 24 x 24 m room,
/// 7.25 m to the ceiling, four columns at (+-8, +-8), three point lamps — one
/// high in the middle, two in opposite corners — and a few objects on the
/// floor). Matte everywhere, one warm wall and one cool block so the bounce has
/// a colour; the ambient is black, so every indirect photon is the gather's.
/// The sample's own lamp intensities (0.65 in the middle, 0.5 in the corners).
static const float kLampScale = 1.0f;
static void buildShowroom(Scene *s)
{
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    addBox(s, cube, matte(Colour(0.62f, 0.60f, 0.58f)), Vec3(0.0f, -0.25f, 0.0f), Vec3(25.0f, 0.5f, 25.0f));
    addBox(s, cube, matte(Colour(0.80f, 0.80f, 0.80f)), Vec3(0.0f, 7.5f, 0.0f), Vec3(25.0f, 0.5f, 25.0f));
    addBox(s, cube, matte(Colour(0.75f, 0.74f, 0.72f)), Vec3(0.0f, 3.5f, 12.25f), Vec3(25.0f, 7.5f, 0.5f));
    addBox(s, cube, matte(Colour(0.78f, 0.45f, 0.30f)), Vec3(0.0f, 3.5f, -12.25f), Vec3(25.0f, 7.5f, 0.5f));
    addBox(s, cube, matte(Colour(0.75f, 0.74f, 0.72f)), Vec3(-12.25f, 3.5f, 0.0f), Vec3(0.5f, 7.5f, 25.0f));
    addBox(s, cube, matte(Colour(0.75f, 0.74f, 0.72f)), Vec3(12.25f, 3.5f, 0.0f), Vec3(0.5f, 7.5f, 25.0f));
    for (int i = 0; i < 4; ++i)
        addBox(s, cube, matte(Colour(0.70f, 0.70f, 0.70f)),
               Vec3((i & 1) ? 8.0f : -8.0f, 3.5f, (i & 2) ? 8.0f : -8.0f), Vec3(2.4f, 7.0f, 2.4f));
    addBox(s, cube, matte(Colour(0.25f, 0.45f, 0.80f)), Vec3(-4.0f, 1.0f, 0.0f), Vec3(2.0f, 2.0f, 2.0f));
    addBox(s, cube, matte(Colour(0.85f, 0.85f, 0.80f)), Vec3(3.5f, 0.75f, -2.0f), Vec3(1.5f, 1.5f, 3.0f));
    addBox(s, cube, matte(Colour(0.30f, 0.65f, 0.30f)), Vec3(1.0f, 0.5f, 7.0f), Vec3(1.0f, 1.0f, 1.0f));
    gCentreLamp = addPointLamp(s, Vec3(0.0f, 6.2f, 0.0f), kLampScale * 0.65f, 30.0f);
    addPointLamp(s, Vec3(-8.0f, 5.0f, -5.0f), kLampScale * 0.5f, 30.0f);
    addPointLamp(s, Vec3(8.0f, 5.0f, 5.0f), kLampScale * 0.5f, 30.0f);
}

static const unsigned kW = 768u, kH = 432u;

/// THE VIEW FIRST (Ogre's startup order: a render target exists before the
/// scene manager), then the scene, then the bind.
static View *makeView(Engine *e, Scene *&s, const char *name)
{
    View *view = e->createOffscreenView(name, kW, kH, Colour(0, 0, 0));
    if (!view) return nullptr;
    s = e->createScene(name);
    if (!s) return nullptr;
    view->setScene(s);
    view->setShadows(true);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    view->setPostFx(fx);
    return view;
}

/// Mean absolute difference in 8-bit codes over a rectangle, all three channels.
static float meanDiff(const Image &a, const Image &b, unsigned x0, unsigned x1, unsigned y0, unsigned y1)
{
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size()) return 1e9f;
    double sum = 0.0;
    size_t n = 0;
    for (unsigned y = y0; y < y1 && y < a.height; ++y)
        for (unsigned x = x0; x < x1 && x < a.width; ++x) {
            const size_t i = (size_t(y) * a.width + x) * 4u;
            for (int c = 0; c < 3; ++c) {
                sum += std::fabs(double(a.rgba[i + c]) - double(b.rgba[i + c]));
                ++n;
            }
        }
    return n ? float(sum / double(n)) : 0.0f;
}

// ===========================================================================
// gi.gather_stable
// ===========================================================================
/// The pixels the assertion is made at: a grid over the room's lower two thirds
/// (floor, walls, columns, blocks), every one of them lit by the lamps AND the
/// bounce. The worst of them decides.
struct StableReading {
    unsigned worstStep = 0u;      ///< the largest frame-to-frame step of any sampled pixel, codes
    double meanStep = 0.0;        ///< mean |step| over every pixel of the region, codes
    double overTwo = 0.0;         ///< fraction of region pixels whose worst step reached 2 codes
};

/// A binary PPM of an 8-bit picture (a debugging door: JAH_GATHER_DUMP=<dir>).
static void writePpm(const Image &img, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (size_t i = 0; i < size_t(img.width) * img.height; ++i) std::fwrite(&img.rgba[i * 4], 1, 3, f);
    std::fclose(f);
}

static StableReading stableReading(Engine *e, View *view, int frames, const char *tag = nullptr)
{
    StableReading r;
    Image prev, cur;
    view->readPixels(prev);
    const unsigned y0 = kH / 3u, y1 = kH;
    std::vector<unsigned char> worst(size_t(kW) * kH, 0u);
    double sum = 0.0;
    size_t n = 0;
    for (int f = 0; f < frames; ++f) {
        e->renderOneFrame();
        view->readPixels(cur);
        for (unsigned y = y0; y < y1; ++y)
            for (unsigned x = 0; x < kW; ++x) {
                const size_t i = (size_t(y) * kW + x) * 4u;
                unsigned step = 0u;
                for (int c = 0; c < 3; ++c)
                    step = std::max(step, unsigned(std::abs(int(cur.rgba[i + c]) - int(prev.rgba[i + c]))));
                sum += step;
                ++n;
                unsigned char &w = worst[size_t(y) * kW + x];
                w = (unsigned char)std::max<unsigned>(w, step);
            }
        prev = cur;
    }
    if (const char *dir = std::getenv("JAH_GATHER_DUMP")) {
        if (tag) {
            Image map;
            map.width = kW;
            map.height = kH;
            map.rgba.assign(size_t(kW) * kH * 4u, 0u);
            for (size_t i = 0; i < size_t(kW) * kH; ++i)
                for (int c = 0; c < 3; ++c) map.rgba[i * 4 + c] = (unsigned char)std::min(255, worst[i] * 32);
            writePpm(map, std::string(dir) + "/stable-worst-" + tag + ".ppm");
            writePpm(cur, std::string(dir) + "/stable-frame-" + tag + ".ppm");
        }
    }
    // THE SAMPLED PIXELS: a 16 x 8 grid over the region.
    for (unsigned gy = 0; gy < 8u; ++gy)
        for (unsigned gx = 0; gx < 16u; ++gx) {
            const unsigned x = (gx * 2u + 1u) * kW / 32u;
            const unsigned y = y0 + (gy * 2u + 1u) * (y1 - y0) / 16u;
            r.worstStep = std::max<unsigned>(r.worstStep, worst[size_t(y) * kW + x]);
        }
    size_t over = 0;
    for (unsigned y = y0; y < y1; ++y)
        for (unsigned x = 0; x < kW; ++x)
            if (worst[size_t(y) * kW + x] >= 2u) ++over;
    r.meanStep = n ? sum / double(n) : 0.0;
    r.overTwo = double(over) / double(size_t(kW) * (y1 - y0));
    return r;
}

static int stableMain(Engine *e)
{
    Scene *s = nullptr;
    View *view = makeView(e, s, "stable");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.gather_stable skips cleanly\n");
        return 0;
    }
    buildShowroom(s);
    enginetest::testCameraLookAt(view, Vec3(2.0f, 1.8f, 10.5f), Vec3(-1.0f, 1.2f, -6.0f));
    CHECK(s->setGlobalIllumination(gatherGi()), "the chain builds over the room");
    setNoTemporal(false);
    render(e, 120);   // THE WARM-UP: the chain settled, the history past its floor
    const GatherStatus st = s->giStatus().gather;
    CHECK_MSG(st.running && st.temporal && st.historyAge > 60u,
              "the gather runs with its pixel history (running %d, temporal %d, age %u)",
              int(st.running), int(st.temporal), st.historyAge);
    const StableReading with = stableReading(e, view, 60, "history");

    // THE LEVER'S PAIR: the same view, each frame's estimate alone.
    setNoTemporal(true);
    render(e, 8);
    const GatherStatus st2 = s->giStatus().gather;
    CHECK_MSG(!st2.temporal, "JAHSHAKA_GATHER_NO_TEMPORAL turns the pixel history off (temporal %d)",
              int(st2.temporal));
    const StableReading without = stableReading(e, view, 60, "alone");
    setNoTemporal(false);

    std::printf("   the room's lower two thirds, 60 live frames after the warm-up (codes, 8-bit):\n"
                "     %-26s worst step at the 128 sampled pixels %u, mean |step| %.3f, pixels "
                "whose worst step reached 2: %.2f %%\n"
                "     %-26s worst step at the 128 sampled pixels %u, mean |step| %.3f, pixels "
                "whose worst step reached 2: %.2f %%\n",
                "WITH the pixel history", with.worstStep, with.meanStep, 100.0 * with.overTwo,
                "each frame alone (lever)", without.worstStep, without.meanStep,
                100.0 * without.overTwo);
    CHECK_MSG(with.worstStep < 2u,
              "A STILL VIEW IS STILL: no sampled pixel of the room moves by 2/255 or more frame to "
              "frame after the warm-up (worst %u codes, bar < 2)", with.worstStep);
    CHECK_MSG(without.worstStep >= 2u && without.meanStep > 2.0 * with.meanStep,
              "...AND THE HISTORY IS WHAT STILLS IT: each frame's estimate alone moves the same "
              "pixels by up to %u codes (mean %.3f against %.3f with the history)",
              without.worstStep, without.meanStep, with.meanStep);

    // THE PRICE OF STILLNESS: A LIGHTING CHANGE ARRIVES LATE. The middle lamp is
    // switched off and the picture read every frame until it is within one code
    // (mean over the region) of where it settles 120 frames later — once with
    // the history, once with the lever (each frame its own estimate: the GI
    // chain's own settle, which the history cannot beat). The difference is the
    // history's lag, and it is bounded by its floor: a 1/10 blend leaves 0.9^k
    // of a step after k frames.
    const auto lagOf = [&](bool history) {
        setNoTemporal(!history);
        LightDesc on;
        on.type = LightType::Point;
        on.colour = Colour(1.0f, 0.97f, 0.92f);
        on.intensity = kLampScale * 0.65f;
        on.range = 30.0f;
        s->setLight(gCentreLamp, on);
        s->refreshGlobalIllumination();   // the mirror's job in the app: a light edit re-injects
        render(e, 120);
        Image before;
        view->readPixels(before);
        LightDesc off = on;
        off.intensity = 0.0f;
        s->setLight(gCentreLamp, off);
        s->refreshGlobalIllumination();
        std::vector<Image> frames(121);
        for (int f = 0; f <= 120; ++f) {
            e->renderOneFrame();
            view->readPixels(frames[size_t(f)]);
        }
        int arrived = -1;
        for (int f = 0; f <= 120; ++f)
            if (meanDiff(frames[size_t(f)], frames[120], 0u, kW, kH / 3u, kH) < 1.0f) { arrived = f; break; }
        const float step = meanDiff(frames[0], frames[120], 0u, kW, kH / 3u, kH);
        const float whole = meanDiff(before, frames[120], 0u, kW, kH / 3u, kH);
        s->setLight(gCentreLamp, on);
        s->refreshGlobalIllumination();
        setNoTemporal(false);
        std::printf("     the middle lamp OFF, %s: the first frame within 1 code (mean) of the settled "
                    "picture is frame %d (the first frame after the switch is %.2f codes from it; the "
                    "whole switch %.2f codes)\n",
                    history ? "WITH the history" : "each frame alone ", arrived, double(step),
                    double(whole));
        return arrived;
    };
    const int lagHistory = lagOf(true);
    const int lagAlone = lagOf(false);
    CHECK_MSG(lagHistory >= 0 && lagAlone >= 0 && lagHistory <= lagAlone + 30,
              "THE HISTORY FOLLOWS A LIGHTING CHANGE: within 1 code %d frames after the switch, "
              "against %d for each frame alone (the chain's own settle) — the history's lag at most "
              "30 frames (its 1/10 floor leaves 0.9^30 = 4 %% of a step)", lagHistory, lagAlone);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}

// ===========================================================================
// gi.gather_motion
// ===========================================================================
static const int kWarmFrames = 90;
static const int kMoveFrames = 24;
static const int kSettleFrames = 45;

struct Pose { Vec3 pos; Vec3 target; };

/// A steady turn about the camera, 1.5 degrees a frame (gi.reflect_motion's).
static Pose yawPose(int frame)
{
    const float a = float(frame) * 1.5f * 3.14159265f / 180.0f;
    const Vec3 c(2.0f, 1.8f, 10.0f);
    return { c, Vec3(c.x - 16.0f * std::sin(a), 1.2f, c.z - 16.0f * std::cos(a)) };
}

/// A steady slide to the left, 0.1 m a frame — a TRANSLATION, which is what
/// tests the depth half of a reprojection and what reveals the floor from
/// behind the blocks (the room is 24 m across, so 2.4 m of slide stays inside).
static Pose truckPose(int frame)
{
    const float x = 2.0f - 0.1f * float(frame);
    return { Vec3(x, 1.8f, 10.0f), Vec3(x - 1.0f, 1.2f, -6.0f) };
}

static float runMove(Engine *e, View *view, Pose (*poseAt)(int), const char *what)
{
    const Pose rest = poseAt(0);
    enginetest::testCameraLookAt(view, rest.pos, rest.target);
    render(e, kWarmFrames);
    for (int i = 1; i <= kMoveFrames; ++i) {
        const Pose p = poseAt(i);
        enginetest::testCameraLookAt(view, p.pos, p.target);
        render(e, 1);
    }
    Image moving, settled;
    view->readPixels(moving);
    render(e, kSettleFrames);
    view->readPixels(settled);
    const float err = meanDiff(moving, settled, 0u, kW, kH / 3u, kH);
    std::printf("     %-40s %.3f codes\n", what, err);
    return err;
}

static int motionMain(Engine *e)
{
    Scene *s = nullptr;
    View *view = makeView(e, s, "motion");
    if (!view || !s) { std::printf("FAIL: view/scene\n"); return 1; }
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray queries on this machine — gi.gather_motion skips cleanly\n");
        return 0;
    }
    buildShowroom(s);
    CHECK(s->setGlobalIllumination(gatherGi()), "the chain builds over the room");

    // THE GATHER IS IN THE PICTURE AT ALL: the settled room with the gather
    // differs from the same room with the gather off (two frames with no
    // indirect light in them would agree perfectly and pass every bar below).
    {
        const Pose rest = truckPose(0);
        enginetest::testCameraLookAt(view, rest.pos, rest.target);
        render(e, kWarmFrames);
        Image with, without;
        view->readPixels(with);
        GiParams off = gatherGi();
        off.gather = GiToggle::Off;
        off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        render(e, 30);
        view->readPixels(without);
        s->setGlobalIllumination(gatherGi());
        render(e, 30);
        const float present = meanDiff(with, without, 0u, kW, kH / 3u, kH);
        CHECK_MSG(present > 3.0f,
                  "THE GATHER IS IN THE PICTURE: the room with and without its indirect light differs by "
                  "%.3f codes (> 3)", present);
    }

    std::printf("   moving frame against the frame it settles to, the room's lower two thirds:\n");
    // THE CONTROL: a still camera, the same two read-backs 45 frames apart —
    // the history's own noise floor (a live frame index and a blend floor keep
    // it moving by a fraction of a code).
    float still = 0.0f;
    {
        const Pose rest = truckPose(0);
        enginetest::testCameraLookAt(view, rest.pos, rest.target);
        render(e, kWarmFrames + kMoveFrames);
        Image a, b;
        view->readPixels(a);
        render(e, kSettleFrames);
        view->readPixels(b);
        still = meanDiff(a, b, 0u, kW, kH / 3u, kH);
        std::printf("     %-40s %.3f codes\n", "still camera (the control)", still);
    }
    const float yaw = runMove(e, view, yawPose, "yaw 1.5 deg/frame");
    const float truck = runMove(e, view, truckPose, "truck 0.1 m/frame");
    // THE LEVER'S ARM: every frame its own estimate — what a history that
    // rejected everything under motion would show.
    setNoTemporal(true);
    const float yawAlone = runMove(e, view, yawPose, "yaw, each frame alone (lever)");
    const float truckAlone = runMove(e, view, truckPose, "truck, each frame alone (lever)");
    setNoTemporal(false);

    // THE BARS, from this suite's own numbers (RTX 4080 SUPER; the run is
    // deterministic — the frame index counts from the view's birth):
    //
    //                                   control   yaw     truck
    //     the history, reprojected       0.125    0.174   0.174
    //     each frame alone (the lever)            0.532   0.506
    //     NOT reprojected (the previous
    //     camera forced to this one)              0.745   0.354
    //
    // Each motion bar sits at the MIDPOINT between the measured 0.174 and the
    // nearest broken arm, the un-reprojected slide's 0.354: half of a missing
    // reprojection reds it, and so does a history that rejects everything under
    // motion (0.51-0.53, twice the bar). The control's bar is the same 0.26 less
    // the motion's own excess over it (0.05): 0.21.
    const float kStillBar = 0.21f;
    const float kYawBar = 0.26f;
    const float kTruckBar = 0.26f;
    CHECK_MSG(still < kStillBar, "THE CONTROL: a still camera's room is settled (%.3f codes < %.2f)",
              still, kStillBar);
    CHECK_MSG(yaw < kYawBar,
              "A TURNING CAMERA: the moving room is within %.2f codes of the settled one (%.3f; each "
              "frame alone %.3f)", kYawBar, yaw, yawAlone);
    CHECK_MSG(truck < kTruckBar,
              "A SLIDING CAMERA: the moving room is within %.2f codes of the settled one (%.3f; each "
              "frame alone %.3f)", kTruckBar, truck, truckAlone);
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
    cfg.logFile = "test-gi-gather-temporal-" + mode + "-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    if (mode == "stable") return stableMain(e);
    if (mode == "motion") return motionMain(e);
    std::printf("FAIL: unknown mode '%s' (stable | motion)\n", mode.c_str());
    return 1;
}
