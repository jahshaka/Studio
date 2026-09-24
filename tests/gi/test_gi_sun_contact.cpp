// HARD SUN CONTACT SHADOWS (PHOTON P5, RY-R3; lane PHOTON-RAYS-1) —
// `gi.sun_contact` and `gi.sun_contact_norays`.
//
// WHAT IS WRONG WITHOUT IT: a shadow map is rendered with a depth BIAS (or every
// lit surface shadows itself), and the bias leaves a band of LIGHT where an
// object meets the ground — the crate that floats. The contact job traces one
// hardware ray per texel from the prepass' surface towards the sun, out to the
// contact range, and the PBS pass folds the answer in as min( map, ray ).
//
// THE FIXTURE: a 2 cm BOARD (1 x 1 m, standing, broadside to the sun) on a matte
// floor under a LOW sun (15 degrees, from -X), so its shadow runs 3.7 m along
// +X, and a camera on the +Z side looking across the whole shadow: the board's
// shaded face meets the floor at x = 0.5, the shadow ends at x = 4.23.
//
// WHY A BOARD AND NOT THE DESIGN'S SOLID CRATE, measured (spikes/photon-rays-1):
// under a 1 m crate the map does NOT leak at all — 0.0 mm at 5, 15, 30 and 60 m
// — because the occluder the map holds for a floor point beside the crate is the
// crate's SUNLIT face, a metre further up the light ray than the floor, far past
// any bias. The leak is the thin caster's: the stored occluder is the caster's
// own thickness away from the floor, and the bias (constant + normal offset,
// both in shadow texels) exceeds it — 44 mm at this 2 cm board from 5.4 m, 106 to
// 165 mm for 2-10 cm boards seen from 15-30 m (the PSSM split's texel grows with
// distance). Panels, planks, table tops, a crate's lid: the everyday contact.
//
// THE NUMBERS (every one in FRAMES and ground metres, never time):
//   1. THE GAP: the widest band of LIT floor next to the board's shaded face,
//      in metres and in texels of the ray job. With the row off it is the
//      atlas' leak — asserted to be at least two texels, or the fixture proves
//      nothing — and with the row on it must be at most ONE texel of the job's
//      resolution (full, then half).
//   2. THE FAR SHADOW: floor whose sun ray meets the board beyond the 2 m range
//      is the map's alone — it must read the same with the row on as with a
//      range too short to reach it (both arms carry the prepass, so the only
//      difference between them is the ray's answer).
//   3. NO ACNE: the sunlit floor reads the same with the ray as without it (a
//      self-intersecting ray would darken it).
//   4. THE ROW'S CONTRACT: the status says running, the divisor the resolution
//      asked for, the sun direction the light's.
//
// THE NO-RAYS ENTRY runs the same binary under JAHSHAKA_NO_RAY_QUERY: the row
// on must then render EXACTLY the picture of the row off, byte for byte, and
// say it is not on (a machine without the hardware renders the map alone).
//
// `--cost` (not a ctest row): the job's GPU milliseconds against the frame's,
// at 1920x1080, paired arms in ONE process (the lead's measurement law) — run
// it under scripts/gpu-exclusive.sh.
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
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

static const unsigned kSize = 640;          // square: groundPointForPixel's frame
static const int kSettleFrames = 12;        // no GI: the shadow map is the whole settle
static const float kPi = 3.14159265358979f;
static float kSunElevationDeg = 15.0f;
static float kSunPower = 8.0f;
static float kBoard = 0.02f;                // the caster's thickness along the sun
static float kFov = 45.0f;
static Vec3 kCamPos(2.2f, 3.0f, 4.2f);
static Vec3 kCamTarget(1.8f, 0.0f, 0.0f);
static const float kFaceX = 0.5f;           // the board's shaded face

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

static float luma(const Image &img, unsigned x, unsigned y)
{
    const size_t i = (size_t(y) * img.width + x) * 4u;
    return 0.2126f * img.rgba[i] + 0.7152f * img.rgba[i + 1] + 0.0722f * img.rgba[i + 2];
}

/// One pixel's floor point (every pixel is asked; the caller filters by region).
struct Sample { unsigned x, y; Vec3 ground; };
static std::vector<Sample> gFloor;

static void buildFloorSamples()
{
    gFloor.clear();
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Vec3 g = enginetest::groundPointForPixel(kCamPos, kCamTarget, x, y, kSize, 0.0f, kFov);
            gFloor.push_back({ x, y, g });
        }
}

/// The mean luma over the floor points inside [x0,x1] x [z0,z1].
static float regionMean(const Image &img, float x0, float x1, float z0, float z1, unsigned *n = nullptr)
{
    double sum = 0.0;
    unsigned count = 0;
    for (const Sample &s : gFloor)
        if (s.ground.x >= x0 && s.ground.x <= x1 && s.ground.z >= z0 && s.ground.z <= z1) {
            sum += luma(img, s.x, s.y);
            ++count;
        }
    if (n) *n = count;
    return count ? float(sum / count) : -1.0f;
}

/// The mean absolute difference over the same region (codes, 8-bit luma).
static float regionDiff(const Image &a, const Image &b, float x0, float x1, float z0, float z1)
{
    double sum = 0.0;
    unsigned count = 0;
    for (const Sample &s : gFloor)
        if (s.ground.x >= x0 && s.ground.x <= x1 && s.ground.z >= z0 && s.ground.z <= z1) {
            sum += std::fabs(luma(a, s.x, s.y) - luma(b, s.x, s.y));
            ++count;
        }
    return count ? float(sum / count) : 1e9f;
}

/// THE GAP: the farthest LIT floor point from the board's shaded face, within
/// the first metre of the shadow and away from its penumbral sides (|z| < 0.3).
/// "Lit" is above the midpoint between the sunlit floor and the deep shadow.
static float contactGap(const Image &img, float threshold)
{
    float gap = 0.0f;
    for (const Sample &s : gFloor)
        if (s.ground.x >= kFaceX && s.ground.x <= kFaceX + 1.0f && std::fabs(s.ground.z) < 0.3f &&
            luma(img, s.x, s.y) > threshold)
            gap = std::max(gap, s.ground.x - kFaceX);
    return gap;
}

/// The world size of one pixel at the contact (the camera's own footprint at
/// the face's foot): the unit a gap is stated in.
static float pixelFootprintAtContact()
{
    const float dx = kFaceX - kCamPos.x, dy = -kCamPos.y, dz = -kCamPos.z;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    return dist * 2.0f * std::tan(0.5f * kFov * kPi / 180.0f) / float(kSize);
}

static bool savePpm(const Image &img, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (size_t i = 0; i < size_t(img.width) * img.height; ++i)
        std::fwrite(&img.rgba[i * 4u], 1, 3, f);
    std::fclose(f);
    return true;
}

static void buildFixture(Scene *s, int crates)
{
    s->setAmbient(Colour(0.30f, 0.34f, 0.40f), Colour(0.20f, 0.20f, 0.20f));
    const NodeId floor = enginetest::addTestCube(s, Colour(0.7f, 0.7f, 0.7f), 0.0f, 1.0f);
    enginetest::setNodeScale(s, floor, Vec3(60.0f, 0.2f, 60.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    // THE CRATE at the origin (and, for --cost, a field of them).
    for (int i = 0; i < crates; ++i) {
        const NodeId c = enginetest::addTestCube(s, Colour(0.55f, 0.45f, 0.35f), 0.0f, 1.0f);
        const int gx = i % 10, gz = i / 10;
        enginetest::setNodePosition(s, c, i == 0 ? Vec3(0.0f, 0.5f, 0.0f)
                                                 : Vec3(-12.0f + 2.6f * float(gx), 0.5f,
                                                        -14.0f + 2.6f * float(gz)));
        // THE BOARD: its shaded face at x = kFaceX, standing on the floor. A
        // thickness of 1 is the solid crate (the no-leak control of the sweep).
        if (i == 0) {
            enginetest::setNodeScale(s, c, Vec3(kBoard, 1.0f, 1.0f));
            enginetest::setNodePosition(s, c, Vec3(kFaceX - 0.5f * kBoard, 0.5f, 0.0f));
        }
    }
    const float e = kSunElevationDeg * kPi / 180.0f;
    enginetest::addDirectionalLight(s, Vec3(std::cos(e), -std::sin(e), 0.0f), kSunPower);
}

static int costMain(Engine *e)
{
    // THE COST AT 1080p: a field of 100 crates, a camera over it. Arms paired in
    // ONE process, alternated, the frame index irrelevant (the job has none).
    View *view = e->createOffscreenView("suncontactcost", 1920, 1080, Colour(0.45f, 0.55f, 0.70f));
    Scene *s = e->createScene("suncontactcost");
    if (!view || !s || !view->setScene(s)) { std::printf("FAIL: view/scene\n"); return 1; }
    buildFixture(s, 100);
    view->setShadows(true);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 9.0f, 16.0f), Vec3(0.0f, 0.0f, -4.0f));
    e->setFrameMonitor(MonitorLevel::Review);
    struct Arm { const char *name; bool contact; int ssr; SunContactResolution res; };
    const Arm arms[] = {
        { "off (no prepass)", false, 0, SunContactResolution::Full },
        { "on, full", true, 0, SunContactResolution::Full },
        { "on, half", true, 0, SunContactResolution::Half },
        { "ssr prepass, off", false, 1, SunContactResolution::Full },
        { "ssr prepass, on full", true, 1, SunContactResolution::Full },
    };
    const int kArms = int(sizeof(arms) / sizeof(arms[0]));
    std::vector<double> frameSum(kArms, 0.0), jobSum(kArms, 0.0);
    std::vector<int> frameN(kArms, 0), jobN(kArms, 0);
    for (int round = 0; round < 4; ++round) {
        for (int a = 0; a < kArms; ++a) {
            PostFxDesc fx;
            fx.allowOffscreen = true;
            fx.ssr = arms[a].ssr;
            view->setPostFx(fx);
            SunContactDesc sc;
            sc.enabled = arms[a].contact;
            sc.resolution = arms[a].res;
            s->setSunContact(sc);
            render(e, 30);                           // warm: the shape rebuilt, the queries back
            std::vector<FrameRecord> drop;
            e->takeFrameRecords(drop);
            render(e, 60);
            std::vector<FrameRecord> recs;
            for (int k = 0; k < 8; ++k) { render(e, 1); e->takeFrameRecords(recs); }
            for (const FrameRecord &r : recs) {
                if (r.gpuMs > 0.0f) { frameSum[a] += r.gpuMs; ++frameN[a]; }
                for (const CacheWork &w : r.cacheWork)
                    if (w.detail == "sun.contact" && w.gpuMs >= 0.0f) { jobSum[a] += w.gpuMs; ++jobN[a]; }
            }
            const SunContactStatus st = s->sunContactStatus();
            if (arms[a].contact && st.gpuMs >= 0.0f && jobN[a] == 0) { jobSum[a] += st.gpuMs; ++jobN[a]; }
        }
    }
    e->setFrameMonitor(MonitorLevel::Off);
    std::printf("    arm                        passes GPU ms   job GPU ms   (frames)\n");
    for (int a = 0; a < kArms; ++a)
        std::printf("    %-26s %10.4f   %10.4f   (%d / %d)\n", arms[a].name,
                    frameN[a] ? frameSum[a] / frameN[a] : -1.0, jobN[a] ? jobSum[a] / jobN[a] : -1.0,
                    frameN[a], jobN[a]);
    const auto mean = [&](int a) { return frameN[a] ? frameSum[a] / frameN[a] : -1.0; };
    const auto job = [&](int a) { return jobN[a] ? jobSum[a] / jobN[a] : -1.0; };
    if (mean(0) > 0.0 && mean(3) > 0.0) {
        std::printf("    RATIOS (paired arms, one process):\n");
        std::printf("      job full / passes (on, full)          %.4f\n", job(1) / mean(1));
        std::printf("      job half / passes (on, half)          %.4f\n", job(2) / mean(2));
        std::printf("      job full / job half                   %.4f\n", job(1) / job(2));
        std::printf("      (passes + job) on full / off          %.4f  (the prepass included)\n",
                    (mean(1) + job(1)) / mean(0));
        std::printf("      (passes + job) on / off under SSR     %.4f  (the prepass already paid)\n",
                    (mean(4) + job(4)) / mean(3));
    }
    return 0;
}

int main(int argc, char **argv)
{
    const bool cost = argc > 1 && std::strcmp(argv[1], "--cost") == 0;
    const char *dumpDir = std::getenv("JAH_SUN_CONTACT_DUMP");   // evidence pictures, a tool
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    const bool raysWanted = !getenv("JAHSHAKA_NO_RAY_QUERY");
    cfg.logFile = raysWanted ? "test-sun-contact-ogre.log" : "test-sun-contact-norays-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    // THE SWEEP'S KNOBS (measurement switches, not modes — the table in
    // spikes/photon-rays-1 was taken with them): the camera's distance from its
    // target along the fixture's own direction, its vertical angle, the
    // caster's thickness (1 = the solid crate), the sun's elevation and power.
    if (const char *v = getenv("JAH_SC_WALL")) kBoard = float(atof(v));
    if (const char *v = getenv("JAH_SC_ELEV")) kSunElevationDeg = float(atof(v));
    if (const char *v = getenv("JAH_SC_POWER")) kSunPower = float(atof(v));
    if (const char *v = getenv("JAH_SC_FOV")) kFov = float(atof(v));
    if (const char *v = getenv("JAH_SC_DIST")) {
        const float d = float(atof(v));
        Vec3 dir(kCamPos.x - kCamTarget.x, kCamPos.y - kCamTarget.y, kCamPos.z - kCamTarget.z);
        const float l = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        kCamPos = Vec3(kCamTarget.x + dir.x / l * d, kCamTarget.y + dir.y / l * d, kCamTarget.z + dir.z / l * d);
    }
    if (cost) return costMain(e);

    View *view = e->createOffscreenView("suncontact", kSize, kSize, Colour(0.45f, 0.55f, 0.70f));
    Scene *s = e->createScene("suncontact");
    if (!view || !s || !view->setScene(s)) {
        std::printf("FAIL: view/scene: %s\n", e->lastError().c_str());
        return 1;
    }
    // Asked AFTER the first view exists: the device is made with it.
    const bool haveRays = e->rayQueryAvailable() && e->rayTracing();
    if (raysWanted && !haveRays) {
        std::printf("ok: this build/machine has no ray queries (available=%d wanted=%d) — "
                    "gi.sun_contact is about the ray job and skips cleanly; "
                    "gi.sun_contact_norays covers the fallback picture\n",
                    int(e->rayQueryAvailable()), int(e->rayTracing()));
        return 0;
    }
    buildFixture(s, 1);
    view->setShadows(true);
    {
        CameraDesc c = enginetest::testCameraDescLookAt(kCamPos, kCamTarget);
        c.fovDegrees = kFov;
        view->setCamera(c);
    }
    PostFxDesc fx;
    fx.allowOffscreen = true;
    view->setPostFx(fx);
    buildFloorSamples();

    const auto shot = [&](bool on, SunContactResolution res, float range, Image &out) {
        SunContactDesc sc;
        sc.enabled = on;
        sc.resolution = res;
        sc.range = range;
        s->setSunContact(sc);
        render(e, kSettleFrames);
        return view->readPixels(out);
    };

    // ---- THE DEFAULT IS OFF ------------------------------------------------
    CHECK(!s->sunContact().enabled, "the row is off by default");
    CHECK(!s->sunContactStatus().on, "...and so is the job");

    Image off, onFull, onHalf, onShort, offAgain;
    CHECK(shot(false, SunContactResolution::Full, kSunContactDefaultRange, off), "read back: row off");
    CHECK(shot(true, SunContactResolution::Full, kSunContactDefaultRange, onFull), "read back: row on, full");
    const SunContactStatus stFull = s->sunContactStatus();
    CHECK(shot(true, SunContactResolution::Half, kSunContactDefaultRange, onHalf), "read back: row on, half");
    const SunContactStatus stHalf = s->sunContactStatus();
    CHECK(shot(true, SunContactResolution::Full, kSunContactMinRange, onShort), "read back: row on, shortest range");
    CHECK(shot(false, SunContactResolution::Full, kSunContactDefaultRange, offAgain), "read back: row off again");

    if (dumpDir) {
        const std::string d(dumpDir);
        savePpm(off, d + "/sun_contact_off.ppm");
        savePpm(onFull, d + "/sun_contact_on_full.ppm");
        savePpm(onHalf, d + "/sun_contact_on_half.ppm");
        savePpm(onShort, d + "/sun_contact_on_short.ppm");
    }

    // ---- THE ROW OFF AGAIN IS THE ROW OFF (the job gives everything back) ---
    CHECK_MSG(off.rgba == offAgain.rgba, "turning the row off restores the picture byte for byte");

    if (!raysWanted) {
        // ---- THE NO-RAYS PICTURE ---------------------------------------------
        CHECK(!stFull.on, "without ray queries the row does not resolve on");
        CHECK_MSG(off.rgba == onFull.rgba && off.rgba == onHalf.rgba,
                  "without ray queries the row on renders EXACTLY the row-off picture");
        std::printf("%s\n", failures ? "gi.sun_contact_norays: FAILED" : "gi.sun_contact_norays: all ok");
        return failures ? 1 : 0;
    }

    // ---- THE ROW'S CONTRACT ------------------------------------------------
    std::printf("    status (full): on=%d running=%d %ux%u divisor %u rays %llu range %.2f "
                "toSun (%.3f, %.3f, %.3f) gpu %.4f ms cpu %.4f ms reason '%s'\n",
                int(stFull.on), int(stFull.running), stFull.width, stFull.height, stFull.divisor,
                stFull.rays, double(stFull.range), double(stFull.toSun[0]), double(stFull.toSun[1]),
                double(stFull.toSun[2]), double(stFull.gpuMs), double(stFull.cpuMs),
                stFull.reason.c_str());
    CHECK(stFull.on && stFull.running, "the job runs for the view with the row on");
    CHECK_MSG(stFull.divisor == 1u && stFull.width == kSize && stFull.height == kSize,
              "full resolution: one ray per pixel (%ux%u, divisor %u)", stFull.width,
              stFull.height, stFull.divisor);
    CHECK_MSG(stHalf.divisor == 2u && stHalf.width == kSize / 2u && stHalf.height == kSize / 2u,
              "half resolution: one ray per 2x2 block (%ux%u, divisor %u)", stHalf.width,
              stHalf.height, stHalf.divisor);
    {
        const float e = kSunElevationDeg * kPi / 180.0f;
        const float want[3] = { -std::cos(e), std::sin(e), 0.0f };   // towards the sun
        float dev = 0.0f;
        for (int i = 0; i < 3; ++i) dev = std::max(dev, std::fabs(stFull.toSun[i] - want[i]));
        CHECK_MSG(dev < 1e-3f, "the rays are cast towards the sun (worst axis off by %.5f)", double(dev));
    }

    // ---- THE REFERENCES ------------------------------------------------------
    unsigned nLit = 0, nShadow = 0;
    const float lit = regionMean(off, 1.0f, 2.5f, 0.8f, 1.5f, &nLit);       // sunlit floor
    const float shadow = regionMean(off, 1.5f, 2.0f, -0.3f, 0.3f, &nShadow);  // deep shadow
    const float threshold = 0.5f * (lit + shadow);
    std::printf("    sunlit floor %.1f (%u px), deep shadow %.1f (%u px), threshold %.1f\n",
                double(lit), nLit, double(shadow), nShadow, double(threshold));
    CHECK_MSG(nLit > 200 && nShadow > 200 && lit - shadow > 40.0f,
              "the fixture draws a sunlit floor and a shadow on it (%.1f vs %.1f codes)",
              double(lit), double(shadow));

    // ---- 1. THE GAP ----------------------------------------------------------
    const float px = pixelFootprintAtContact();
    const float gapOff = contactGap(off, threshold);
    const float gapFull = contactGap(onFull, threshold);
    const float gapHalf = contactGap(onHalf, threshold);
    std::printf("    THE CONTACT GAP (lit floor beside the shadowed face; one pixel = %.2f mm):\n"
                "      the shadow map alone   %.1f mm  (%.2f px)\n"
                "      + rays, full           %.1f mm  (%.2f px, bar 1 texel = %.2f mm)\n"
                "      + rays, half           %.1f mm  (%.2f px, bar 1 texel = %.2f mm)\n",
                double(px * 1000.0f), double(gapOff * 1000.0f), double(gapOff / px),
                double(gapFull * 1000.0f), double(gapFull / px), double(px * 1000.0f),
                double(gapHalf * 1000.0f), double(gapHalf / px), double(2.0f * px * 1000.0f));
    CHECK_MSG(gapOff >= 2.0f * px,
              "THE LEAK EXISTS: the shadow map alone leaves %.1f mm of light at the contact (>= 2 px)",
              double(gapOff * 1000.0f));
    CHECK_MSG(gapFull <= 1.0f * px + 1e-4f,
              "THE GAP CLOSES (full): %.2f mm <= one texel (%.2f mm)", double(gapFull * 1000.0f),
              double(px * 1000.0f));
    CHECK_MSG(gapHalf <= 2.0f * px + 1e-4f,
              "THE GAP CLOSES (half): %.2f mm <= one texel (%.2f mm)", double(gapHalf * 1000.0f),
              double(2.0f * px * 1000.0f));

    // ---- 2. THE FAR SHADOW IS THE MAP'S --------------------------------------
    // Floor whose ray meets the face beyond the 2 m range: (x - 0.5) / cos(15)
    // > 2 for x > 2.44. Both arms carry the prepass, and the shortest range
    // answers nothing there either — so any difference is the ray's.
    const float farDiff = regionDiff(onFull, onShort, 2.6f, 3.8f, -0.3f, 0.3f);
    const float farOffDiff = regionDiff(onFull, off, 2.6f, 3.8f, -0.3f, 0.3f);
    std::printf("    far shadow (x 2.6..3.8): on vs shortest range %.4f codes, on vs row off %.4f codes\n",
                double(farDiff), double(farOffDiff));
    CHECK_MSG(farDiff == 0.0f,
              "BEYOND THE RANGE THE MAP IS ALONE: the far shadow is unchanged (%.4f codes)", double(farDiff));
    CHECK_MSG(regionMean(onFull, 2.6f, 3.8f, -0.3f, 0.3f) < threshold,
              "...and it is still a shadow (the map's)");

    // ---- 3. NO ACNE ------------------------------------------------------------
    const float litDiff = regionDiff(onFull, onShort, 1.0f, 2.5f, 0.8f, 1.5f);
    const float litHalfDiff = regionDiff(onHalf, onShort, 1.0f, 2.5f, 0.8f, 1.5f);
    std::printf("    sunlit floor: full vs shortest %.4f codes, half vs shortest %.4f codes\n",
                double(litDiff), double(litHalfDiff));
    CHECK_MSG(litDiff == 0.0f && litHalfDiff == 0.0f,
              "NO ACNE: the sunlit floor is untouched by the rays (%.4f / %.4f codes)",
              double(litDiff), double(litHalfDiff));
    // FOR THE RECORD, not a bar: what the PREPASS alone moves (the row on gives a
    // view the prepass, whose G-buffer shadow term is quantised) — the same
    // difference any view pays when its SSR row turns on.
    std::printf("    the prepass alone (row on vs row off, sunlit floor): %.4f codes\n",
                double(regionDiff(onShort, off, 1.0f, 2.5f, 0.8f, 1.5f)));

    std::printf("%s\n", failures ? "gi.sun_contact: FAILED" : "gi.sun_contact: all ok");
    return failures ? 1 : 0;
}
