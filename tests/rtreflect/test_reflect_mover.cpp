// A MOVING OBJECT'S REFLECTIONS CONVERGE AS A STILL ONE'S DO (REFLECT-MOVERS-1) —
// `gi.reflect_mover`.
//
// THE OWNER'S FINDING: "heavy dithering in reflections, especially when moving
// objects — if an animated sphere moving around has its reflection dither as it
// moves, that's a problem." The ray tier's reflection is one GGX ray a pixel a
// frame behind a TEMPORAL MEAN, and the mean's history is found by reprojecting
// the pixel's surface point through the previous CAMERA. A point on a MOVER was
// somewhere else last frame, so the camera-only reprojection lands on the wrong
// texel, the 5 % distance test rejects it, and the mean restarts from this
// frame's single sample — every frame, for as long as the object moves: the raw
// per-frame estimate IS the dither.
//
// THE FIXTURE: a floor and a sphere (a MOVER: Scene::setNodeMovable) that runs a
// circle over it, 0.04 m a frame, two still emissive columns behind. Six arms on
// one scene, the materials and the march swapped between them: a glossy floor and
// a mirror floor under a matte sphere (the REFLECTED footprint: the sphere's
// mirror image through the floor plane, minus the sphere), and a glossy sphere
// (its own disk) — with the screen-space march on (the desktop default) and with
// the rays alone (a headset's chain never marches).
//
// THE NUMBERS, frames-counted: at four checkpoints of a 240-frame run the MOVING
// frame is read, the sphere is held at that pose for kSettleFrames and the
// SETTLED frame is read; the error is the mean absolute difference inside the
// region, in 8-bit codes, and its HIGH-FREQUENCY part (the difference minus its
// own 3x3 mean — the grain; a trail or a lag is smooth). The STILL case is the
// sphere parked at the same pose from a fresh start for the same kRunIn frames,
// against the same settled frame. A motion-compensated frame-to-frame delta
// (FLICKER) is printed beside them.
//
// MEASURED (RTX 4080 SUPER, spikes/reflect-movers-1), base -> this lane:
//   glossy sphere, march      settled 5.57 -> 2.12 codes, grain 2.23x -> 1.48x still
//   glossy sphere, rays       settled 5.57 -> 2.03,       grain 2.10x -> 1.31x
//   glossy floor, rays        settled 22.7 -> 3.78 (the trail gone; grain 0.54 -> 1.39)
//   mirror floor, rays        1.01x -> 1.01x (a mirror keeps no history)
//   glossy/mirror floor, march  5.58 / 4.38 -> 4.42 / 4.38: THE MARCH'S one-frame
//     object lag (JahSsrResolve_ps.glsl), printed as `target:`, not gated.
// The four selftest hashes are unchanged (no still pixel moves).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

static const unsigned kWidth = 640;
static const unsigned kHeight = 360;
static const float kFovDeg = 45.0f;
static const int kWarmFrames = 90;
static const int kRunIn = 60;           // moving frames before each checkpoint
static const int kCheckpoints = 4;      // 4 x 60 = 240 moving frames
static const int kSettleFrames = 90;
static const int kFlickerFrames = 30;   // the last frames before a checkpoint, frame to frame
static const float kStep = 0.04f;       // metres a frame along the circle
static const float kPathR = 1.5f;
static const float kSphereR = 0.6f;
static const float kSphereY = 0.9f;
static const Vec3 kCamPos(0.0f, 2.2f, 5.5f);
static const Vec3 kCamTarget(0.0f, 0.4f, -0.5f);

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

static MeshData sphereMesh(int rings = 32, int segments = 64)
{
    MeshData d;
    const float kPi = 3.14159265358979f;
    for (int r = 0; r <= rings; ++r) {
        const float th = float(r) / float(rings) * kPi;
        for (int sg = 0; sg < segments; ++sg) {
            const float ph = float(sg) / float(segments) * 2.0f * kPi;
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = std::sin(th) * std::sin(ph);
            d.positions.insert(d.positions.end(), { 0.5f * x, 0.5f * y, 0.5f * z });
            d.normals.insert(d.normals.end(), { x, y, z });
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int sg = 0; sg < segments; ++sg) {
            const unsigned a = unsigned(r * segments + sg);
            const unsigned b = unsigned(r * segments + (sg + 1) % segments);
            const unsigned c = a + unsigned(segments), e = b + unsigned(segments);
            d.indices.insert(d.indices.end(), { a, b, c, b, e, c });
        }
    return d;
}

/// The sphere's centre at frame `f` of the run: a circle about the origin.
static Vec3 pathAt(int f)
{
    const float a = float(f) * kStep / kPathR;
    return Vec3(kPathR * std::sin(a), kSphereY, kPathR * std::cos(a) - 0.5f);
}

/// A world point's pixel (and its forward distance) through the fixture camera
/// (testCameraDescLookAt's basis, the engine's default vertical fov).
static bool project(const Vec3 &p, float &px, float &py, float &z)
{
    Vec3 f(kCamTarget.x - kCamPos.x, kCamTarget.y - kCamPos.y, kCamTarget.z - kCamPos.z);
    float l = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    f = Vec3(f.x / l, f.y / l, f.z / l);
    Vec3 r(-f.z, 0.0f, f.x);
    l = std::sqrt(r.x * r.x + r.z * r.z);
    r = Vec3(r.x / l, 0.0f, r.z / l);
    const Vec3 u(r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x);
    const Vec3 d(p.x - kCamPos.x, p.y - kCamPos.y, p.z - kCamPos.z);
    z = d.x * f.x + d.y * f.y + d.z * f.z;
    if (z <= 0.0f) return false;
    const float t = std::tan(kFovDeg * 0.5f * 3.14159265f / 180.0f);
    const float aspect = float(kWidth) / float(kHeight);
    const float nx = (d.x * r.x + d.y * r.y + d.z * r.z) / (z * t * aspect);
    const float ny = (d.x * u.x + d.y * u.y + d.z * u.z) / (z * t);
    px = (nx + 1.0f) * 0.5f * float(kWidth);
    py = (1.0f - ny) * 0.5f * float(kHeight);
    return true;
}

/// The screen disk of a sphere of radius `rad` at `c` (centre pixel, radius in pixels).
static void diskOf(const Vec3 &c, float rad, float &cx, float &cy, float &pr)
{
    float z = 1.0f;
    project(c, cx, cy, z);
    const float t = std::tan(kFovDeg * 0.5f * 3.14159265f / 180.0f);
    pr = rad / (z * t) * 0.5f * float(kHeight);
}

enum class Region { Reflected, Sphere };

/// The pixels an arm is measured over: the sphere's MIRROR IMAGE through the floor
/// plane (y = 0) with the sphere's own disk (dilated) cut out, or the sphere's own
/// disk (eroded off its silhouette).
static std::vector<unsigned> regionAt(const Vec3 &c, Region what, float erode = 0.0f)
{
    float sx, sy, sr, vx, vy, vr;
    diskOf(c, kSphereR, sx, sy, sr);
    diskOf(Vec3(c.x, -c.y, c.z), kSphereR, vx, vy, vr);
    std::vector<unsigned> px;
    for (unsigned y = 0; y < kHeight; ++y)
        for (unsigned x = 0; x < kWidth; ++x) {
            const float fx = float(x) + 0.5f, fy = float(y) + 0.5f;
            const float ds = std::hypot(fx - sx, fy - sy);
            const float dv = std::hypot(fx - vx, fy - vy);
            const bool in = what == Region::Sphere ? ds < sr - 3.0f - erode
                                                   : (dv < vr - 2.0f - erode && ds > sr + 3.0f + erode);
            if (in) px.push_back(y * kWidth + x);
        }
    return px;
}

static float meanDiff(const Image &a, const Image &b, const std::vector<unsigned> &px)
{
    if (a.width != b.width || a.height != b.height || px.empty()) return 1e9f;
    double s = 0.0;
    for (unsigned i : px)
        for (int k = 0; k < 3; ++k)
            s += std::fabs(double(a.rgba[size_t(i) * 4u + k]) - double(b.rgba[size_t(i) * 4u + k]));
    return float(s / double(px.size() * 3u));
}

/// THE DITHER: the HIGH-FREQUENCY part of the error — the difference image minus
/// its own 3x3 box mean, averaged over the region. A trail or a lag is smooth and
/// cancels here; a single-sample grain does not.
static float hfDiff(const Image &a, const Image &b, const std::vector<unsigned> &px)
{
    if (a.width != b.width || a.height != b.height || px.empty()) return 1e9f;
    const int w = int(a.width), h = int(a.height);
    auto d = [&](int x, int y, int k) {
        x = std::min(std::max(x, 0), w - 1); y = std::min(std::max(y, 0), h - 1);
        const size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4u + size_t(k);
        return double(a.rgba[i]) - double(b.rgba[i]);
    };
    double s = 0.0;
    for (unsigned i : px) {
        const int x = int(i % a.width), y = int(i / a.width);
        for (int k = 0; k < 3; ++k) {
            double m = 0.0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) m += d(x + dx, y + dy, k);
            s += std::fabs(d(x, y, k) - m / 9.0);
        }
    }
    return float(s / double(px.size() * 3u));
}

/// THE FRAME-TO-FRAME NOISE, MOTION-COMPENSATED: this frame against the last one
/// sampled where the region's content WAS (bilinear, `shift` pixels back), mean
/// absolute difference in codes. A still region compares in place. What is left
/// is what changed at the object's own points between two frames — the flicker —
/// not the motion.
static float flicker(const Image &prevImg, const Image &cur, const std::vector<unsigned> &px, float sx,
                     float sy)
{
    if (px.empty()) return 0.0f;
    double s = 0.0;
    const int w = int(cur.width), h = int(cur.height);
    for (unsigned i : px) {
        const float x = float(i % cur.width) - sx, y = float(i / cur.width) - sy;
        const int x0 = std::min(std::max(int(std::floor(x)), 0), w - 2);
        const int y0 = std::min(std::max(int(std::floor(y)), 0), h - 2);
        const float fx = std::min(std::max(x - float(x0), 0.0f), 1.0f);
        const float fy = std::min(std::max(y - float(y0), 0.0f), 1.0f);
        for (int k = 0; k < 3; ++k) {
            auto at = [&](int xx, int yy) {
                return float(prevImg.rgba[(size_t(yy) * size_t(w) + size_t(xx)) * 4u + size_t(k)]);
            };
            const float v = (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) +
                            (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
            s += std::fabs(double(cur.rgba[size_t(i) * 4u + size_t(k)]) - double(v));
        }
    }
    return float(s / double(px.size() * 3u));
}

static void writePpm(const Image &img, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (size_t i = 0; i < size_t(img.width) * img.height; ++i)
        std::fwrite(&img.rgba[i * 4u], 1, 3, f);
    std::fclose(f);
}

struct Arm {
    const char *name;
    float floorMetal, floorRough;
    float sphereMetal, sphereRough;
    Region region;
    bool march;          // the screen-space march on (the desktop default) or rays alone (VR's shape)
    /// THE BARS (0 = printed, not gated): the moving-vs-settled error in codes, and
    /// the high-frequency part of it against the still case's.
    float settledBar, hfRatioBar;
};

/// THE COST (--cost): the per-pixel motion read at 1080p, PAIRED in one process —
/// the reflection pass' GPU ms with the id image bound against the same frames with
/// it withheld (JAH_R5_NO_MOTION, read per frame), alternating blocks over a moving
/// glossy sphere on a glossy floor. Run it under scripts/gpu-exclusive.sh with the
/// clocks locked; it prints, it does not gate (a millisecond bar is a target).
static int costMain(Engine *e)
{
    View *view = e->createOffscreenView("reflectmover-cost", 1920, 1080, Colour(0.45f, 0.55f, 0.70f));
    Scene *s = e->createScene("reflectmover-cost");
    if (!view || !s) { std::printf("FAIL: cost view/scene\n"); return 1; }
    view->setScene(s);
    s->setAmbient(Colour(0.45f, 0.55f, 0.70f), Colour(0.30f, 0.30f, 0.32f));
    const NodeId floor = s->createNode();
    PbrParams fp; fp.albedo = Colour(0.85f, 0.85f, 0.85f); fp.metalness = 1.0f; fp.roughness = 0.2f;
    s->attachMesh(floor, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(fp));
    enginetest::setNodeScale(s, floor, Vec3(60.0f, 0.2f, 60.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    const NodeId sphere = s->createNode();
    s->setNodeMovable(sphere, true);
    PbrParams sp; sp.albedo = Colour(0.95f, 0.8f, 0.6f); sp.metalness = 1.0f; sp.roughness = 0.25f;
    s->attachMesh(sphere, s->createMesh(sphereMesh()), s->createPbrMaterial(sp));
    enginetest::setNodeScale(s, sphere, Vec3(2.0f * kSphereR, 2.0f * kSphereR, 2.0f * kSphereR));
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.4f), 2.0f);
    GiParams gi; gi.mode = GiMode::Vct; gi.quality = GiQuality::High; gi.numBounces = 1;
    gi.testBoundsMin = Vec3(-20.0f, -2.0f, -20.0f); gi.testBoundsMax = Vec3(20.0f, 8.0f, 20.0f);
    s->setGlobalIllumination(gi);
    PostFxDesc fx; fx.allowOffscreen = true; fx.ssr = 2;
    view->setPostFx(fx);
    enginetest::testCameraLookAt(view, kCamPos, kCamTarget);
    int frame = 0;
    const auto step = [&](int n, double *sum, int *count) {
        for (int i = 0; i < n; ++i) {
            enginetest::setNodePosition(s, sphere, pathAt(++frame));
            e->renderOneFrame();
            const float ms = s->rayQueryStatus().reflectMs;
            if (sum && i >= 10 && ms > 0.0f) { *sum += ms; ++*count; }   // the timestamps lag a few frames
        }
    };
    step(120, nullptr, nullptr);
    double on = 0.0, off = 0.0;
    int nOn = 0, nOff = 0;
    for (int round = 0; round < 24; ++round) {
        unsetenv("JAH_R5_NO_MOTION");
        step(30, &on, &nOn);
        setenv("JAH_R5_NO_MOTION", "1", 1);
        step(30, &off, &nOff);
    }
    unsetenv("JAH_R5_NO_MOTION");
    const double a = nOn ? on / nOn : -1.0, b = nOff ? off / nOff : -1.0;
    std::printf("target: the reflection pass at 1920x1080, a moving glossy sphere: motion read %.4f ms, "
                "withheld %.4f ms, cost %+.4f ms (bar 0.1) [%d / %d frames]\n", a, b, a - b, nOn, nOff);
    return 0;
}

int main(int argc, char **argv)
{
    const char *dumpDir = getenv("JAH_REFLECT_MOVER_DUMP");
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-reflect-mover-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    View *view = e->createOffscreenView("reflectmover", kWidth, kHeight, Colour(0.45f, 0.55f, 0.70f));
    Scene *s = e->createScene("reflectmover");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    const bool raysWanted = !getenv("JAHSHAKA_NO_RAY_QUERY");
    if (raysWanted && !(e->rayQueryAvailable() && e->rayTracing())) {
        std::printf("ok: no ray queries on this machine — gi.reflect_mover is about the tier; skipping\n");
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--cost") return costMain(e);
    s->setAmbient(Colour(0.45f, 0.55f, 0.70f), Colour(0.30f, 0.30f, 0.32f));

    const NodeId floor = s->createNode();
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    PbrParams fp; fp.albedo = Colour(0.85f, 0.85f, 0.85f);
    const MaterialId floorMat0 = s->createPbrMaterial(fp);
    if (!(floor && cube && floorMat0 && s->attachMesh(floor, cube, floorMat0))) {
        std::printf("FAIL: the floor\n"); return 1;
    }
    enginetest::setNodeScale(s, floor, Vec3(60.0f, 0.2f, 60.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));

    // THE MOVER: told BEFORE its geometry (Scene::setNodeMovable's rule).
    const NodeId sphere = s->createNode();
    s->setNodeMovable(sphere, true);
    const MeshId ball = s->createMesh(sphereMesh());
    PbrParams sp; sp.albedo = Colour(1.0f, 0.45f, 0.1f); sp.roughness = 0.8f;
    const MaterialId sphereMat0 = s->createPbrMaterial(sp);
    if (!(sphere && ball && sphereMat0 && s->attachMesh(sphere, ball, sphereMat0))) {
        std::printf("FAIL: the sphere\n"); return 1;
    }
    enginetest::setNodeScale(s, sphere, Vec3(2.0f * kSphereR, 2.0f * kSphereR, 2.0f * kSphereR));
    enginetest::setNodePosition(s, sphere, pathAt(0));
    // Two still, bright columns behind, so the glossy sphere has something to reflect.
    for (int i = 0; i < 2; ++i) {
        const NodeId n = s->createNode();
        PbrParams p; p.albedo = Colour(0.05f, 0.05f, 0.05f); p.roughness = 0.9f;
        p.emissive = i ? Colour(0.1f, 0.9f, 0.2f) : Colour(0.2f, 0.3f, 1.0f);
        const MaterialId m = s->createPbrMaterial(p);
        if (n && m) s->attachMesh(n, cube, m);
        enginetest::setNodeScale(s, n, Vec3(1.0f, 3.0f, 1.0f));
        enginetest::setNodePosition(s, n, Vec3(i ? 3.5f : -3.5f, 1.5f, -4.0f));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.4f), 2.0f);

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.testBoundsMin = Vec3(-20.0f, -2.0f, -20.0f);
    gi.testBoundsMax = Vec3(20.0f, 8.0f, 20.0f);
    if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: gi\n"); return 1; }
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    view->setPostFx(fx);
    enginetest::testCameraLookAt(view, kCamPos, kCamTarget);
    render(e, kWarmFrames);
    {
        const AtomDrawStatus as = s->atomDrawStatus();
        const RayQueryStatus rq = s->rayQueryStatus();
        std::printf("    atom: on=%d atomItems=%u pbsItems=%u   rayQuery reflect=%d\n", int(as.on),
                    as.atomItems, as.pbsItems, int(rq.reflect));
    }

    const Arm arms[] = {
        // THE MARCH OWNS A FLOOR'S REFLECTION OF AN ON-SCREEN OBJECT (desktop): its
        // colour is the PREVIOUS frame's, reprojected through the camera only
        // (JahSsrResolve_ps.glsl: "what still lags by a frame is a moving OBJECT's
        // own motion") — not this lane's text, so printed as a target.
        { "march: glossy floor 0.2, the sphere's reflection", 1.0f, 0.2f, 0.0f, 0.8f, Region::Reflected,
          true, 0.0f, 0.0f },
        { "march: mirror floor 0.05, the sphere's reflection", 1.0f, 0.05f, 0.0f, 0.8f, Region::Reflected,
          true, 0.0f, 0.0f },
        // THE RAY TIER'S OWN: a glossy mover's reflection of the world (the owner's
        // dither: 5.94 codes and 2.2x the still grain before), and a floor's
        // reflection of a mover where only the rays answer (a headset; 22.8 codes of
        // trail before) — and a mirror, which has no history to get wrong.
        { "glossy sphere 0.25, its own reflection", 0.0f, 0.9f, 1.0f, 0.25f, Region::Sphere, true, 3.0f,
          1.6f },
        { "rays: glossy floor 0.2, the sphere's reflection", 1.0f, 0.2f, 0.0f, 0.8f, Region::Reflected,
          false, 8.0f, 0.0f },
        { "rays: mirror floor 0.05, the sphere's reflection", 1.0f, 0.05f, 0.0f, 0.8f, Region::Reflected,
          false, 0.0f, 1.5f },
        { "rays: glossy sphere 0.25, its own reflection", 0.0f, 0.9f, 1.0f, 0.25f, Region::Sphere, false,
          3.0f, 1.6f },
    };
    int armIdx = 0;
    for (const Arm &arm : arms) {
        PbrParams f2 = fp; f2.metalness = arm.floorMetal; f2.roughness = arm.floorRough;
        PbrParams s2 = sp; s2.metalness = arm.sphereMetal; s2.roughness = arm.sphereRough;
        if (arm.sphereMetal > 0.0f) s2.albedo = Colour(0.95f, 0.8f, 0.6f);
        s->setNodeMaterial(floor, s->createPbrMaterial(f2));
        s->setNodeMaterial(sphere, s->createPbrMaterial(s2));
        {
            PostFxDesc afx = fx;
            if (!arm.march) afx.ssrScreenMarch = false;
            view->setPostFx(afx);
        }
        enginetest::setNodePosition(s, sphere, pathAt(0));
        render(e, kWarmFrames);

        double sumMoving = 0.0, sumStill = 0.0, sumHfMoving = 0.0, sumHfStill = 0.0;
        double sumFlickerMoving = 0.0, sumFlickerStill = 0.0;
        int nFlickerMoving = 0, nFlickerStill = 0;
        int frame = 0;
        for (int c = 1; c <= kCheckpoints; ++c) {
            Image last;
            for (int i = 0; i < kRunIn; ++i) {
                ++frame;
                enginetest::setNodePosition(s, sphere, pathAt(frame));
                render(e, 1);
                if (i >= kRunIn - kFlickerFrames - 1) {
                    Image img;
                    view->readPixels(img);
                    if (!last.rgba.empty()) {
                        // the region's content moved by its centre's screen step
                        const Vec3 c1 = pathAt(frame), c0 = pathAt(frame - 1);
                        const Vec3 m1 = arm.region == Region::Sphere ? c1 : Vec3(c1.x, -c1.y, c1.z);
                        const Vec3 m0 = arm.region == Region::Sphere ? c0 : Vec3(c0.x, -c0.y, c0.z);
                        float x1, y1, x0, y0, z;
                        project(m1, x1, y1, z);
                        project(m0, x0, y0, z);
                        sumFlickerMoving += flicker(last, img, regionAt(c1, arm.region, 2.0f), x1 - x0, y1 - y0);
                        ++nFlickerMoving;
                    }
                    last = img;
                }
            }
            Image moving, settled, still;
            view->readPixels(moving);
            render(e, kSettleFrames);                // held at the checkpoint's pose
            view->readPixels(settled);
            // THE STILL CASE at the same pose: the sphere parked there from a fresh
            // start (away and back, so its history restarts), kRunIn frames.
            enginetest::setNodePosition(s, sphere, pathAt(frame + 400));
            render(e, kSettleFrames);
            enginetest::setNodePosition(s, sphere, pathAt(frame));
            render(e, kRunIn - kFlickerFrames - 1);
            {
                Image a, b;
                view->readPixels(a);
                for (int i = 0; i < kFlickerFrames; ++i) {
                    render(e, 1);
                    view->readPixels(b);
                    sumFlickerStill += flicker(a, b, regionAt(pathAt(frame), arm.region, 2.0f), 0.0f, 0.0f);
                    ++nFlickerStill;
                    a = b;
                }
                still = a;
            }
            const std::vector<unsigned> px = regionAt(pathAt(frame), arm.region);
            const float em = meanDiff(moving, settled, px);
            const float es = meanDiff(still, settled, px);
            const float hm = hfDiff(moving, settled, px), hs = hfDiff(still, settled, px);
            std::printf("    [%s] frame %3d  region %5zu px   moving %.3f (hf %.3f)   still %.3f (hf %.3f) codes\n",
                        arm.name, frame, px.size(), em, hm, es, hs);
            if (arm.region == Region::Reflected) {
                // THE SPHERE ITSELF too (its diffuse is the gather's — the brief's §3.5)
                const std::vector<unsigned> sp = regionAt(pathAt(frame), Region::Sphere);
                std::printf("      sphere disk: moving %.3f (hf %.3f)   still %.3f (hf %.3f)\n",
                            meanDiff(moving, settled, sp), hfDiff(moving, settled, sp),
                            meanDiff(still, settled, sp), hfDiff(still, settled, sp));
            }
            sumMoving += em;
            sumStill += es;
            sumHfMoving += hm;
            sumHfStill += hs;
            if (dumpDir && c == kCheckpoints) {
                const std::string base = std::string(dumpDir) + "/arm" + std::to_string(armIdx);
                writePpm(moving, base + "-moving.ppm");
                writePpm(settled, base + "-settled.ppm");
                writePpm(still, base + "-still.ppm");
            }
        }
        const float m = float(sumMoving / kCheckpoints), st = float(sumStill / kCheckpoints);
        const float hm = float(sumHfMoving / kCheckpoints), hs = float(sumHfStill / kCheckpoints);
        const float fm = float(sumFlickerMoving / std::max(nFlickerMoving, 1)),
                    fs = float(sumFlickerStill / std::max(nFlickerStill, 1));
        std::printf("RESULT [%s] vs-settled: moving %.3f still %.3f (x%.2f)  hf: moving %.3f still %.3f (x%.2f)  "
                    "FLICKER: moving %.3f still %.3f (x%.2f)\n",
                    arm.name, m, st, m / std::max(st, 1e-3f), hm, hs, hm / std::max(hs, 1e-3f), fm, fs,
                    fm / std::max(fs, 1e-3f));
        if (arm.settledBar > 0.0f)
            CHECK_MSG(m < arm.settledBar, "[%s] the moving reflection is within %.1f codes of the settled one (%.3f)",
                      arm.name, arm.settledBar, m);
        if (arm.hfRatioBar > 0.0f)
            CHECK_MSG(hm < arm.hfRatioBar * std::max(hs, 0.05f),
                      "[%s] its grain is within %.2fx the still case's (%.3f against %.3f)", arm.name,
                      arm.hfRatioBar, hm, hs);
        if (arm.settledBar <= 0.0f && arm.hfRatioBar <= 0.0f)
            std::printf("target: [%s] moving %.3f codes against the settled frame (still %.3f)\n", arm.name, m, st);
        ++armIdx;
    }
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
