// gi.contact_occlusion — AN OBJECT STANDING ON A FLOOR DARKENS THE FLOOR BESIDE IT
// BY THE AMOUNT THE PHYSICS SAYS (lane CONTACT-OCCLUSION-1).
//
// THE PHYSICS. A horizontal floor element at distance d from the foot of a long
// vertical wall of height h, under a sky of uniform radiance L (so an open floor
// receives E0 = pi * L), sees the sky everywhere above its horizon except where the
// wall stands. In the plane across the wall (the wall is long: a 2-D problem) the
// differential configuration factor from an element with normal n to a straight
// segment AB seen unoccluded is (Hottel; Howell's catalogue, the differential
// strip to a parallel infinite strip, C-11)
//
//     F = 1/2 * | sin(phi_B) - sin(phi_A) |,
//
// phi measured from the element's normal. For the floor element at (d, 0) with
// n = (0, 1) and the wall's face from (0, 0) to (0, h): phi_A = -90 degrees (the
// foot, along the floor), sin(phi_B) = -d / sqrt(d^2 + h^2) (the wall's top), so
//
//     F(d) = 1/2 * ( 1 - d / sqrt(d^2 + h^2) ),
//
// and a BLACK wall (albedo 0: it only blocks) leaves the floor E(d) = E0 (1 - F(d)):
// half the sky at the foot, 0.146 less than open at d = h, 0.026 less at d = 3h.
// The suite states that ratio against the SAME pixels with the wall hidden (the
// paired open arm, one process, one camera), so every factor the pixel carries
// besides the irradiance — the albedo, the diffuse lobe's energy factor, the
// exposure — divides out and the number IS E(d) / E0.
//
// A WHITE wall (albedo rho_w) also EMITS what it reflects: its own irradiance is
// the upper sky's half (a vertical element sees exactly half its hemisphere above
// the horizon, 1/2 E0) plus the floor's, which is itself darkened beside the wall
// and brightened by it. The suite solves that exactly on the same 2-D geometry — a
// radiosity over the wall's face and the floor, discretised finely, the same
// configuration factor between every pair, Jacobi to convergence — and gates
// B - A against it (the brief's "rho * F(d) * the wall's irradiance / E0", with
// the wall's irradiance and its variation with height solved rather than guessed).
//
// ARM C is the owner's case: the 2 m cube of the rig check (spikes/reflect-leak-2)
// instead of the wall. There is no 2-D closed form; the profile is printed, and
// the cube's own analytic sky visibility (the helper's quadrature) beside it.
//
// THE MEASUREMENT (the brief's section 4 item 1): JAH_CONTACT_MEASURE=1 prints the
// PER-TERM table — every shipped tier, and at the ray tiers the gather, the field
// and the cones one at a time — and gates nothing (JAH_CONTACT_VOXELS=1 adds the
// store's own readout at the wall and the floor; JAH_CONTACT_NOCARDS=1 takes the
// meshes' cards away; JAH_CONTACT_COST=1 is the sky pass's paired GPU cost).
//
// THE GATE (gi.contact_occlusion) and the TARGETS (gi.contact_occlusion_target,
// JAH_CONTACT_TARGET=1) are stated at the gating block in main(). Arm B's reference is
// solved at the TIER'S bounce count: the engine counts the sky as a light, so one
// bounce is the wall re-emitting what the sky gives it directly, and the floor-wall
// inter-reflection is the second and later bounces.
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
        char buf_[512];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

namespace {

const double kPi = 3.14159265358979323846;

/// The picture: square, orthographic, straight down. 0.78 cm a pixel, so the
/// nearest point (5 cm) is six pixels off the wall's face.
const unsigned kSize = 1024u;
const float kOrthoHalf = 4.0f;
const float kCamY = 3.0f;          // above the wall's top, inside cascade 0
/// The wall: 2 m tall, 0.2 m thick, 40 m long (along z), its lit face at x = kFaceX.
const float kWallH = 2.0f;
const float kWallT = 0.2f;
const float kWallL = 40.0f;
const float kFaceX = -3.0f;
const float kWhite = 0.8f;
const float kFloorAlbedo = 0.5f;
const float kDs[7] = { 0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 4.0f };
const int kNd = 7;

void render(Engine *e, int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

CameraDesc topDownCamera()
{
    CameraDesc c;
    c.position = Vec3(0.0f, kCamY, 0.0f);
    c.orientation = Quat(-0.70710678f, 0.0f, 0.0f, 0.70710678f);   // -Z onto -Y
    c.orthographic = true;
    c.orthoSize = kOrthoHalf;
    c.nearClip = 0.05f;
    c.farClip = 200.0f;
    return c;
}

double worldToPixel(float w) { return (double(w) / kOrthoHalf * 0.5 + 0.5) * kSize; }

/// The mean red+green+blue of the pixel columns covering world x in [x0, x1] over
/// world z in [z0, z1] — the wall is long, so a tall strip is one floor distance
/// sampled hundreds of times (the gather's per-probe noise averages down).
double stripMean(const ImageF &img, float x0, float x1, float z0, float z1)
{
    const int px0 = int(std::floor(worldToPixel(x0))), px1 = int(std::ceil(worldToPixel(x1)));
    const int py0 = int(std::floor(worldToPixel(z0))), py1 = int(std::ceil(worldToPixel(z1)));
    double s = 0.0;
    int n = 0;
    for (int y = std::max(py0, 0); y < std::min(py1, int(img.height)); ++y)
        for (int x = std::max(px0, 0); x < std::min(px1, int(img.width)); ++x) {
            const Colour c = img.at(unsigned(x), unsigned(y));
            s += double(c.r) + double(c.g) + double(c.b);
            ++n;
        }
    return n ? s / n : 0.0;
}

// ---------------------------------------------------------------------------
// THE 2-D REFERENCE: the differential configuration factor, and the radiosity.

/// sin of the angle between the element's normal n and the direction to q.
double sinFromNormal(double px, double py, double nx, double ny, double qx, double qy)
{
    const double dx = qx - px, dy = qy - py;
    const double l = std::sqrt(dx * dx + dy * dy);
    return l > 0.0 ? (nx * dy - ny * dx) / l : 0.0;
}
/// F from the element at p (normal n) to the segment ab, seen unoccluded.
double segmentFactor(double px, double py, double nx, double ny, double ax, double ay, double bx,
                     double by)
{
    return 0.5 * std::fabs(sinFromNormal(px, py, nx, ny, bx, by) -
                           sinFromNormal(px, py, nx, ny, ax, ay));
}
/// The black wall's factor, closed form.
double wallFactor(double d) { return 0.5 * (1.0 - d / std::sqrt(d * d + double(kWallH) * kWallH)); }

/// The floor's irradiance / E0 beside a Lambertian wall of albedo rhoW, over a
/// floor of albedo rhoF: a radiosity solve on the 2-D section (wall face x = 0,
/// y in [0, h]; floor y = 0, x in [0, 400]), everything else the sky.
struct Radiosity {
    std::vector<double> floorX, floorE;   // floor element centres, their E / E0
    double wallMeanE = 0.0;               // the wall face's mean E / E0
    double at(double d) const {
        // Linear in the solved elements (fine near the wall).
        if (d <= floorX.front()) return floorE.front();
        for (size_t i = 1; i < floorX.size(); ++i)
            if (floorX[i] >= d) {
                const double t = (d - floorX[i - 1]) / (floorX[i] - floorX[i - 1]);
                return floorE[i - 1] + t * (floorE[i] - floorE[i - 1]);
            }
        return floorE.back();
    }
};

Radiosity solveRadiosity(double rhoW, double rhoF, int bounces = 0)
{
    // Floor elements: fine near the wall, geometric out to 400 m.
    std::vector<double> fe;   // edges
    fe.push_back(0.0);
    double w = 0.005;
    while (fe.back() < 400.0) { fe.push_back(fe.back() + w); w = std::min(w * 1.04, 2.0); }
    const int nf = int(fe.size()) - 1;
    const int nw = 200;
    std::vector<double> we(nw + 1);
    for (int j = 0; j <= nw; ++j) we[j] = double(kWallH) * j / nw;
    // factors
    std::vector<double> Ffw(size_t(nf) * nw), Fwf(size_t(nw) * nf);
    std::vector<double> floorSky(nf), wallSky(nw);
    for (int i = 0; i < nf; ++i) {
        const double x = 0.5 * (fe[i] + fe[i + 1]);
        double s = 0.0;
        for (int j = 0; j < nw; ++j) {
            const double f = segmentFactor(x, 0.0, 0.0, 1.0, 0.0, we[j], 0.0, we[j + 1]);
            Ffw[size_t(i) * nw + j] = f;
            s += f;
        }
        floorSky[i] = 1.0 - s;
    }
    for (int j = 0; j < nw; ++j) {
        const double y = 0.5 * (we[j] + we[j + 1]);
        double s = 0.0;
        for (int i = 0; i < nf; ++i) {
            const double f = segmentFactor(0.0, y, 1.0, 0.0, fe[i], 0.0, fe[i + 1], 0.0);
            Fwf[size_t(j) * nf + i] = f;
            s += f;
        }
        (void)s;
        // A vertical element sees exactly half its hemisphere above the horizon.
        wallSky[j] = 0.5;
    }
    // THE BOUNCE ORDER (the engine's `numBounces` counts the sky as a light, like a lamp:
    // one bounce = the light a surface re-emits of what the sky gives it directly).
    // Radiosities J = rho E, the floor's E read after `bounces` reflections: pass 0 is
    // the sky's direct term on both surfaces, each further pass adds one reflection
    // between them. bounces <= 0 = to convergence (the full inter-reflection).
    std::vector<double> Jw(nw), Jf(nf);
    for (int j = 0; j < nw; ++j) Jw[j] = rhoW * wallSky[j];
    for (int i = 0; i < nf; ++i) Jf[i] = rhoF * floorSky[i];
    const int passes = bounces > 0 ? bounces - 1 : 400;
    for (int it = 0; it < passes; ++it) {
        std::vector<double> nJw(nw), nJf(nf);
        for (int j = 0; j < nw; ++j) {
            double e = wallSky[j], seen = 0.0;
            for (int i = 0; i < nf; ++i) {
                e += Fwf[size_t(j) * nf + i] * Jf[i];
                seen += Fwf[size_t(j) * nf + i];
            }
            e += (0.5 - seen) * rhoF;   // the floor beyond the solved span: open
            nJw[j] = rhoW * e;
        }
        for (int i = 0; i < nf; ++i) {
            double e = floorSky[i];
            for (int j = 0; j < nw; ++j) e += Ffw[size_t(i) * nw + j] * Jw[j];
            nJf[i] = rhoF * e;
        }
        double delta = 0.0;
        for (int j = 0; j < nw; ++j) delta = std::max(delta, std::fabs(nJw[j] - Jw[j]));
        Jw.swap(nJw);
        Jf.swap(nJf);
        if (bounces <= 0 && delta < 1e-9) break;
    }
    std::vector<double> Ef(nf), Ew(nw);
    for (int i = 0; i < nf; ++i) {
        double e = floorSky[i];
        for (int j = 0; j < nw; ++j) e += Ffw[size_t(i) * nw + j] * Jw[j];
        Ef[i] = e;
    }
    for (int j = 0; j < nw; ++j) Ew[j] = rhoW > 0.0 ? Jw[j] / rhoW : 0.0;
    Radiosity r;
    for (int i = 0; i < nf; ++i) { r.floorX.push_back(0.5 * (fe[i] + fe[i + 1])); r.floorE.push_back(Ef[i]); }
    double m = 0.0;
    for (int j = 0; j < nw; ++j) m += Ew[j];
    r.wallMeanE = m / nw;
    return r;
}

// ---------------------------------------------------------------------------
// THE FIXTURE

struct Fixture {
    Engine *e = nullptr;
    View *view = nullptr;
    Scene *s = nullptr;
    MeshId cube = 0;
    NodeId wall = 0, box = 0;
    MaterialId black = 0, white = 0;
};

MaterialId matte(Scene *s, float albedo)
{
    PbrParams p;
    p.albedo = Colour(albedo, albedo, albedo);
    p.roughness = 1.0f;
    // MATTE by the default ground's recipe (GF1): Specular workflow, ior 1, black
    // specular = F0 0 — the pixel carries the diffuse and nothing else, and a
    // black wall reflects NOTHING (not even the 4 % a dielectric would).
    p.workflow = PbrParams::Workflow::Specular;
    p.ior = 1.0f;
    p.specularColour = Colour(0.0f, 0.0f, 0.0f);
    return s->createPbrMaterial(p);
}

bool build(Fixture &f)
{
    f.view = f.e->createOffscreenView("contact", kSize, kSize, Colour(0, 0, 0));
    if (!f.view) return false;
    f.view->setOffscreenContract(OffscreenContract::StillPicture);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;
    fx.ssao = false;
    fx.hdrReadback = true;
    f.view->setPostFx(fx);
    f.view->setShadows(true);
    f.s = f.e->createScene("contact");
    f.view->setScene(f.s);
    f.view->setCamera(topDownCamera());

    // THE SKY: a constant environment — a 1x1 equirect of one grey, the Sky Light
    // at unit gain. Uniform radiance in every direction; the floor sees its upper
    // half. No sun, no lamp: every photon on the floor came from the sky.
    const unsigned char px[4] = { 128, 128, 128, 255 };
    SkyDesc sky;
    sky.mode = SkyMode::Equirectangular;
    sky.equirect = f.s->createTexture(1, 1, px, true);
    if (!f.s->setSky(sky)) return false;
    f.s->setEnvironmentLight(Colour(1.0f, 1.0f, 1.0f));

    // THE MESH CARRIES ITS SIX BOX CARDS, as every baked mesh does (the bake's card
    // generator, enginetest::boxCards): a ray tier's hit reads the surface cache
    // first. JAH_CONTACT_NOCARDS=1 measures the voxel path alone.
    MeshData md = enginetest::unitCubeMesh();
    if (!std::getenv("JAH_CONTACT_NOCARDS")) md.cards = enginetest::boxCards(0.5f);
    f.cube = f.s->createMesh(md);
    const NodeId floor = f.s->createNode();
    if (!f.s->attachMesh(floor, f.cube, matte(f.s, kFloorAlbedo))) return false;
    f.s->setNodeTransform(floor, Vec3(0.0f, -0.25f, 0.0f), Quat(), Vec3(80.0f, 0.5f, 80.0f));

    f.black = matte(f.s, 0.0f);
    f.white = matte(f.s, kWhite);
    f.wall = f.s->createNode();
    if (!f.s->attachMesh(f.wall, f.cube, f.black)) return false;
    f.s->setNodeTransform(f.wall, Vec3(kFaceX - 0.5f * kWallT, 0.5f * kWallH, 0.0f), Quat(),
                          Vec3(kWallT, kWallH, kWallL));
    f.box = f.s->createNode();
    if (!f.s->attachMesh(f.box, f.cube, f.black)) return false;
    // The rig check's 2 m cube, its +x face where the wall's is.
    f.s->setNodeTransform(f.box, Vec3(kFaceX - 1.0f, 1.0f, 0.0f), Quat(), Vec3(2.0f, 2.0f, 2.0f));
    f.s->setNodeVisible(f.box, false);
    return true;
}

struct Tier { const char *name; GiMode mode; GiQuality q; int bounces; };
// kPhotonTable (src/services/worldmodes.cpp): technique, quality, bounces; the
// field and the gather AUTO (the tier's own resolution), the cards the default.
const Tier kTiers[4] = {
    { "Epic",   GiMode::VctPccHybrid, GiQuality::Epic,   3 },
    { "High",   GiMode::VctPccHybrid, GiQuality::High,   1 },
    { "Medium", GiMode::Vct,          GiQuality::Medium, 1 },
    { "Low",    GiMode::Vct,          GiQuality::Low,    1 },
};

enum class Arm { Open, Black, White, Cube };

/// Settles (giAtRest, frames-counted, then until the strip stops moving) and
/// returns the strip means at every kDs, averaged over 8 frames.
struct Profile { double v[kNd]; double far = 0.0; double top = 0.0; double across[17] = { 0 }; bool rest = false; int frames = 0; };

Profile measure(Fixture &f, Arm arm)
{
    f.s->setNodeVisible(f.wall, arm == Arm::Black || arm == Arm::White);
    f.s->setNodeVisible(f.box, arm == Arm::Cube);
    if (arm == Arm::Black || arm == Arm::White) {
        f.s->attachMesh(f.wall, f.cube, arm == Arm::White ? f.white : f.black);
    }
    f.s->refreshGlobalIllumination();
    Profile p;
    const bool cubeArm = arm == Arm::Cube;
    const float z0 = cubeArm ? -0.1f : -2.0f, z1 = cubeArm ? 0.1f : 2.0f;
    int frames = 0;
    render(f.e, 60);
    frames += 60;
    while (!f.s->giStatus().giAtRest && frames < 1200) { render(f.e, 10); frames += 10; }
    p.rest = f.s->giStatus().giAtRest;
    // ...and until the strip stops moving (trap 7: read until the value stops).
    double prev[kNd] = { 0 };
    for (int round = 0; round < 8; ++round) {
        render(f.e, 30);
        frames += 30;
        ImageF img;
        f.view->readPixelsHdr(img);
        double worst = 0.0;
        for (int i = 0; i < kNd; ++i) {
            const float x = kFaceX + kDs[i];
            const float hw = std::max(0.0078125f, std::min(0.25f * kDs[i], 0.05f));
            const double m = stripMean(img, x - hw, x + hw, z0, z1);
            if (round > 0 && prev[i] > 0.0) worst = std::max(worst, std::fabs(m / prev[i] - 1.0));
            prev[i] = m;
        }
        if (round > 0 && worst < 0.003) break;
    }
    // THE READING: the mean of 8 frames (the gather's per-frame estimate moves).
    for (int i = 0; i < kNd; ++i) p.v[i] = 0.0;
    for (int k = 0; k < 8; ++k) {
        f.e->renderOneFrame();
        ++frames;
        ImageF img;
        f.view->readPixelsHdr(img);
        for (int i = 0; i < kNd; ++i) {
            const float x = kFaceX + kDs[i];
            const float hw = std::max(0.0078125f, std::min(0.25f * kDs[i], 0.05f));
            p.v[i] += stripMean(img, x - hw, x + hw, z0, z1) / 8.0;
        }
        p.far += stripMean(img, 3.3f, 3.7f, z0, z1) / 8.0;
        p.top += stripMean(img, kFaceX - 0.15f, kFaceX - 0.05f, z0, z1) / 8.0;   // the wall's top
        // ACROSS THE VIEW (FIELD-EDGE-1): the floor from x = -3.9 to +3.9 m, the camera
        // (and cascade 0, and the field) centred on x = 0; read on the open arm.
        for (int q = 0; q < 17; ++q) {
            const float x = -3.9f + 0.4875f * float(q);
            p.across[q] += stripMean(img, x - 0.05f, x + 0.05f, 1.0f, 3.0f) / 8.0;
        }
    }
    p.frames = frames;
    return p;
}

void setTier(Fixture &f, const Tier &t, GiToggle gather, GiToggle ddgi)
{
    GiParams gi;
    gi.mode = t.mode;
    gi.quality = t.q;
    gi.numBounces = t.bounces;
    if (const char *b = std::getenv("JAH_CONTACT_BOUNCES")) gi.numBounces = std::atoi(b);   // measurement
    if (const char *c = std::getenv("JAH_CONTACT_CASCADES")) {   // measurement: "n,half,res"
        float half = 5.0f; int n = 1, res = 64;
        std::sscanf(c, "%d,%f,%d", &n, &half, &res);
        gi.cascadeCount = n;
        for (int i = 0; i < n; ++i) {
            gi.cascadeSet[i].halfSize = half * float(1 << i);
            gi.cascadeSet[i].resolution = res;
            gi.cascadeSet[i].stepCells = 4.0f;
        }
    }
    gi.gather = gather;
    gi.ddgi = ddgi;
    f.s->setGlobalIllumination(gi);
    render(f.e, 5);
}

void printRow(const char *label, const double *v)
{
    std::printf("   %-34s", label);
    for (int i = 0; i < kNd; ++i) std::printf(" %6.3f", v[i]);
    std::printf("\n");
}

}  // namespace

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-contact-occlusion-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Fixture f;
    f.e = engine.get();
    CHECK(build(f), "the fixture builds (a floor, a 2 x 0.2 x 40 m wall, a 2 m cube, a constant sky)");
    if (failures) return 1;
    const bool rays = f.e->rayQueryAvailable() && f.e->rayTracing();
    const bool measureMode = std::getenv("JAH_CONTACT_MEASURE") != nullptr;
    const bool targetMode = std::getenv("JAH_CONTACT_TARGET") != nullptr;

    // ---- THE REFERENCE ----------------------------------------------------
    // THE SKY PASS'S COST (JAH_CONTACT_COST=1; run under scripts/gpu-exclusive.sh with
    // locked clocks): a lamp orbits so the light tick re-injects every few frames, and
    // the two arms — the sky's light on (the sky pass runs) and at zero gain (it is
    // skipped: VctLighting::hasEnvironmentLight) — alternate in blocks in ONE process.
    if (std::getenv("JAH_CONTACT_COST")) {
        const char *tn = std::getenv("JAH_CONTACT_TIER");
        const Tier *t = &kTiers[1];
        for (const Tier &x : kTiers) if (tn && !std::strcmp(tn, x.name)) t = &x;
        setTier(f, *t, GiToggle::Auto, GiToggle::Auto);
        f.s->setNodeVisible(f.wall, true);
        f.s->attachMesh(f.wall, f.cube, f.white);
        const NodeId lamp = f.s->createNode();
        LightDesc l; l.type = LightType::Point; l.intensity = 2.0f; l.range = 12.0f; l.castShadows = false;
        f.s->setLight(lamp, l);
        render(f.e, 300);
        f.e->setFrameMonitor(MonitorLevel::Review);
        render(f.e, 10);
        std::vector<FrameRecord> sink; f.e->takeFrameRecords(sink);
        double sum[2] = { 0, 0 }; int n[2] = { 0, 0 };
        double relight[2] = { 0, 0 }; int nr[2] = { 0, 0 };
        int frame = 0;
        for (int block = 0; block < 12; ++block) {
            const int arm = block & 1;
            f.s->setEnvironmentLight(arm ? Colour(0, 0, 0) : Colour(1, 1, 1));
            std::vector<FrameRecord> recs;
            for (int k = 0; k < 90; ++k, ++frame) {
                const float a = 0.05f * float(frame);
                f.s->setNodeTransform(lamp, Vec3(-1.5f + std::cos(a), 1.2f, std::sin(a)), Quat(), Vec3(1, 1, 1));
                f.e->renderOneFrame();
                if (k < 30) { std::vector<FrameRecord> d; f.e->takeFrameRecords(d); continue; }
                f.e->takeFrameRecords(recs);
                const float rg = f.s->giStatus().cards.relightGpuMs;
                if (rg > 0.0f) { relight[arm] += rg; ++nr[arm]; }
            }
            for (size_t r = 0; r < recs.size(); ++r)
                for (const CacheWork &w : recs[r].cacheWork) {
                    if (w.detail.rfind("vct.light", 0) == 0 && w.gpuMs > 0.0f) { sum[arm] += w.gpuMs; ++n[arm]; }
                }
        }
        std::printf("\n== COST at %s: the light tick's GPU ms per injection round: sky pass ON %.3f (n %d), "
                    "OFF %.3f (n %d); the card relight's GPU ms: ON %.3f (n %d), OFF %.3f (n %d)\n",
                    t->name, n[0] ? sum[0] / n[0] : -1.0, n[0], n[1] ? sum[1] / n[1] : -1.0, n[1],
                    nr[0] ? relight[0] / nr[0] : -1.0, nr[0], nr[1] ? relight[1] / nr[1] : -1.0, nr[1]);
        return 0;
    }

    // THE MOVER AND THE STORE (JAH_CONTACT_MOVER=1; the reflect_mover diagnosis): a
    // movable sphere circles over the floor under a sun at the High tier; per frame the
    // cascade-0 light volume's digest says whether the store changed, and the voxel at a
    // fixed floor point its radiance.
    if (std::getenv("JAH_CONTACT_MOVER")) {
        GiParams gi;
        gi.mode = GiMode::Vct; gi.quality = GiQuality::High; gi.numBounces = 1;
        gi.cascadeCount = 1;
        gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 20.0f, 128, 0.0f };
        f.s->setGlobalIllumination(gi);
        f.s->setNodeVisible(f.wall, false);
        f.s->setNodeVisible(f.box, false);
        if (!std::getenv("JAH_CONTACT_MOVER_NOSUN"))
            enginetest::addDirectionalLight(f.s, Vec3(-0.3f, -1.0f, 0.4f), 2.0f);
        if (std::getenv("JAH_CONTACT_MOVER_AMBIENT")) {   // reflect_mover's own sky
            f.s->setSky(SkyDesc());
            f.s->setAmbient(Colour(0.45f, 0.55f, 0.70f), Colour(0.30f, 0.30f, 0.32f));
        }
        const NodeId ball = f.s->createNode();
        f.s->setNodeMovable(ball, !std::getenv("JAH_CONTACT_MOVER_STILL"));
        PbrParams bp; bp.albedo = Colour(1.0f, 0.45f, 0.1f); bp.roughness = 0.8f;
        f.s->attachMesh(ball, f.cube, f.s->createPbrMaterial(bp));
        const auto at = [](int k) { const float a = 0.02f * float(k); return Vec3(1.5f * std::cos(a), 0.6f, 1.5f * std::sin(a)); };
        f.s->setNodeTransform(ball, at(0), Quat(), Vec3(1.2f, 1.2f, 1.2f));
        render(f.e, 240);
        std::string prev;
        int changed = 0;
        const int N = 60;
        for (int k = 1; k <= N; ++k) {
            f.s->setNodeTransform(ball, at(k), Quat(), Vec3(1.2f, 1.2f, 1.2f));
            f.e->renderOneFrame();
            const GiVoxelStats vs = f.s->giVoxelStats(0);
            if (!prev.empty() && vs.lightDigest != prev) ++changed;
            if (k <= 6 || k == N) std::printf("   mover frame %d: digest %s peak %.4f meanLit %.5f lit %lld\n", k,
                                               vs.lightDigest.c_str(), double(vs.peak), vs.meanLit, (long long)vs.voxelsLit);
            prev = vs.lightDigest;
        }
        std::printf("\n== MOVER: the store changed on %d of %d moving frames\n", changed, N - 1);
        render(f.e, 120);
        {
            // THE THREE READINGS OF ONE FLOOR POINT (1, 0, 1): the pixel (top-down, V = N),
            // the floor's card, the cascade-0 voxel — all in radiance.
            ImageF img; f.view->readPixelsHdr(img);
            const double px = worldToPixel(-2.0f), py = worldToPixel(-2.0f);
            const Colour c = img.at(unsigned(px), unsigned(py));
            CardSample cs;
            const bool cok = f.s->readCardAt(Vec3(-2.0f, 0.0f, -2.0f), Vec3(0, 1, 0), cs) && cs.ok;
            GiVoxelVolume vv;
            double vr = -1.0;
            if (f.s->giVoxelVolume(0, vv) && vv.available) {
                const int ix = int((-2.0f - vv.origin[0]) / vv.cell[0]);
                const int iy = int((0.01f - vv.origin[1]) / vv.cell[1]);
                const int iz = int((-2.0f - vv.origin[2]) / vv.cell[2]);
                for (int yy = iy - 1; yy <= iy; ++yy) {
                    const size_t i = (size_t(iz) * vv.height + yy) * vv.width + ix;
                    const double cc = vv.albedo[i * 4 + 3];
                    if (cc > 0.5) vr = vv.light[i * 4 + 1] / (vv.multiplier * cc);
                }
            }
            std::printf("   FLOOR (-2,0,-2) green: pixel %.4f  card %s %.4f (indirect %.4f)  voxel %.4f\n", c.g,
                        cok ? "ok" : "none", cok ? cs.radiance[1] : -1.0f, cok ? cs.indirect[1] : -1.0f, vr);
        }
        const std::string settled = f.s->giVoxelStats(0).lightDigest;
        std::printf("   settled digest %s (the last moving frame's %s)\n", settled.c_str(), prev.c_str());
        return 0;
    }

    const Radiosity refB = solveRadiosity(kWhite, kFloorAlbedo);
    double refA[kNd], refBv[kNd];
    std::printf("\n   d (m)                             ");
    for (int i = 0; i < kNd; ++i) std::printf(" %6.2f", kDs[i]);
    std::printf("\n");
    for (int i = 0; i < kNd; ++i) { refA[i] = 1.0 - wallFactor(kDs[i]); refBv[i] = refB.at(kDs[i]); }
    printRow("REF black wall 1 - F(d)", refA);
    printRow("REF white wall (radiosity)", refBv);
    std::printf("   (the white wall's mean irradiance %.3f E0; the floor albedo %.2f)\n",
                refB.wallMeanE, double(kFloorAlbedo));
    // Self-check of the radiosity against the closed form at rho_w = 0.
    {
        const Radiosity r0 = solveRadiosity(0.0, kFloorAlbedo);
        double worst = 0.0;
        for (int i = 0; i < kNd; ++i) worst = std::max(worst, std::fabs(r0.at(kDs[i]) - refA[i]));
        CHECK_MSG(worst < 2e-3, "the radiosity solve at rho_w = 0 IS the closed form (worst %.5f)", worst);
    }

    struct Term { const char *name; GiToggle gather, ddgi; bool raysOnly; };
    const Term kTerms[] = {
        { "tier (gather+field auto)", GiToggle::Auto, GiToggle::Auto, false },
        { "gather off (the field)",   GiToggle::Off,  GiToggle::Auto, true },
        { "gather on, field off",     GiToggle::On,   GiToggle::Off,  true },
        { "cones only",               GiToggle::Off,  GiToggle::Off,  false },
    };
    const int nTerms = measureMode ? 4 : 1;

    const char *only = std::getenv("JAH_CONTACT_TIER");     // measurement: one tier
    const bool whiteFirst = std::getenv("JAH_CONTACT_WHITE_FIRST") != nullptr;
    for (const Tier &t : kTiers) {
        if (only && std::strcmp(only, t.name) != 0) continue;
        for (int ti = 0; ti < nTerms; ++ti) {
            const Term &term = kTerms[ti];
            const bool rayTier = t.mode == GiMode::VctPccHybrid;
            if (term.raysOnly && (!rayTier || !rays)) continue;
            setTier(f, t, term.gather, term.ddgi);
            const Profile open = measure(f, Arm::Open);
            const GiStatus st = f.s->giStatus();
            Profile A, B;
            if (whiteFirst) { B = measure(f, Arm::White); A = measure(f, Arm::Black); }
            else { A = measure(f, Arm::Black); B = measure(f, Arm::White); }
            if (measureMode && std::getenv("JAH_CONTACT_VOXELS")) {
                // THE STORE, read where the white wall and the open floor are: radiance
                // = light / (k c), against the sky radiance L (the 1x1 sky's linear grey).
                f.s->setNodeVisible(f.wall, true);
                f.s->attachMesh(f.wall, f.cube, f.white);
                f.s->refreshGlobalIllumination();
                render(f.e, 240);
                GiVoxelVolume vv;
                if (f.s->giVoxelVolume(0, vv) && vv.available) {
                    const double L = std::pow((128.0 / 255.0 + 0.055) / 1.055, 2.4);
                    const auto at = [&](float wx, float wy, float wz, const char *what) {
                        const int ix = int((wx - vv.origin[0]) / vv.cell[0]);
                        const int iy = int((wy - vv.origin[1]) / vv.cell[1]);
                        const int iz = int((wz - vv.origin[2]) / vv.cell[2]);
                        if (ix < 0 || iy < 0 || iz < 0 || ix >= vv.width || iy >= vv.height || iz >= vv.depth) {
                            std::printf("   voxel %s: outside\n", what); return; }
                        const size_t i = (size_t(iz) * vv.height + iy) * vv.width + ix;
                        const double c = vv.albedo[i * 4 + 3];
                        const double lr = vv.light[i * 4 + 0];
                        const double back = vv.lightBack.empty() ? -1.0 : vv.lightBack[i * 4 + 0];
                        std::printf("   voxel %-22s (%d,%d,%d) albedo %.3f c %.3f  L/(k c)/L_sky: mean %.4f back %.4f "
                                    "front %.4f\n", what, ix, iy, iz, vv.albedo[i * 4], c,
                                    c > 0 ? lr / (vv.multiplier * c) / L : 0.0,
                                    c > 0 && back >= 0 ? back / (vv.multiplier * c) / L : -1.0,
                                    c > 0 && back >= 0 ? (2 * lr - back) / (vv.multiplier * c) / L : -1.0);
                    };
                    std::printf("   THE STORE (cascade 0, cell %.3f m, k %.4f): a Lambertian lit by the sky "
                                "alone re-emits rho x 0.702 (the lobe) x its sky share\n", vv.cell[0],
                                vv.multiplier);
                    for (float y : { 0.3f, 1.0f, 1.7f })
                        for (float x : { kFaceX - 0.02f, kFaceX - 0.1f, kFaceX - 0.18f }) {
                            char nm[64]; std::snprintf(nm, sizeof nm, "wall x%.2f y%.1f", x, y);
                            at(x, y, 0.0f, nm);
                        }
                    for (float x : { kFaceX + 0.3f, 1.0f, 3.0f }) {
                        char nm[64]; std::snprintf(nm, sizeof nm, "floor x%.1f", x);
                        at(x, -0.06f, 0.0f, nm);
                        at(x, 0.01f, 0.0f, nm);
                    }
                }
            }
            const Profile C = measure(f, Arm::Cube);
            std::printf("\n== %s — %s (gather %s, field %s; frames open %d A %d B %d C %d; rest %d%d%d%d)\n",
                        t.name, term.name, st.gather.running ? "RUNNING" : "off",
                        st.ifdTargetSamples ? "on" : "off", open.frames, A.frames, B.frames,
                        C.frames, open.rest, A.rest, B.rest, C.rest);
            std::printf("   cards resident %u (instances %u)\n", st.cards.cardsResident,
                        st.cards.instancesResident);
            double rA[kNd], rB[kNd], rC[kNd], dBA[kNd], refBA[kNd];
            for (int i = 0; i < kNd; ++i) {
                rA[i] = A.v[i] / open.v[i];
                rB[i] = B.v[i] / open.v[i];
                rC[i] = C.v[i] / open.v[i];
                dBA[i] = rB[i] - rA[i];
                refBA[i] = refBv[i] - refA[i];
            }
            std::printf("   open floor ACROSS the view (x -3.9..3.9 m, / its value at x = 0):");
            for (int q = 0; q < 17; ++q) std::printf(" %.3f", open.across[q] / open.across[8]);
            std::printf("\n");
            std::printf("   open floor: %.4f at d=0.05 .. %.4f at d=4 (flat = 1: %.4f); far strip %.4f\n",
                        open.v[0], open.v[kNd - 1], open.v[0] / open.v[kNd - 1], open.far);
            std::printf("   the wall's top face (a material check): open %.4f A %.4f B %.4f\n", open.top,
                        A.top, B.top);
            printRow("A black wall  E/E_open", rA);
            printRow("  REF", refA);
            printRow("B white wall  E/E_open", rB);
            printRow("  REF", refBv);
            printRow("B - A", dBA);
            printRow("  REF", refBA);
            printRow("C 2 m cube    E/E_open", rC);

            // ARM B'S REFERENCE AT THE TIER'S OWN BOUNCE COUNT (the sky counted as a light).
            const Radiosity refN = solveRadiosity(kWhite, kFloorAlbedo, t.bounces);
            double refBAn[kNd];
            for (int i = 0; i < kNd; ++i) refBAn[i] = refN.at(kDs[i]) - refA[i];
            printRow("  REF at the tier's bounces", refBAn);
            if (measureMode) continue;

            // THE GATE, and the TARGETS (JAH_CONTACT_TARGET=1, the photon-target twin):
            //   arm A where the gather runs (Epic, High, Medium on a ray machine): 0.04;
            //   arm B at the tier's bounce count where ONE bounce is asked (High, Medium):
            //     0.03 — the sky's direct term re-emitted by the wall, the lane's fix;
            //   TARGETS: arm A on the field alone (Low; 0.08). Its miss (0.10-0.11 at
            //     d = 0.5-1 m) is the probe SPACING, not a term: the grid is 32x16x16 over
            //     cascade 0's 10 m box (0.31 m across the wall, 0.625 m in height), the
            //     layer under the floor is crushed by its own visibility, so the floor
            //     reads the cage's upper layer 0.47 m above it — a point that high sees
            //     less of a 2 m wall (1 - F from 0.47 m: 0.77 at d = 1, the field 0.84).
            //     Arm B on the field (0.06),
            //     and Epic's three bounces (0.03 against the three-bounce solve: the voxel
            //     and card stores' floor-to-wall share is the four-cone set's, a quarter
            //     of the hemisphere where the floor fills half of it).
            const bool gathers = rays && (t.mode == GiMode::VctPccHybrid || t.q == GiQuality::Medium);
            double worstA = 0.0, worstB = 0.0;
            for (int i = 1; i < kNd; ++i) {   // d >= 0.1 m
                worstA = std::max(worstA, std::fabs(rA[i] - refA[i]));
                worstB = std::max(worstB, std::fabs(dBA[i] - refBAn[i]));
            }
            const bool oneBounce = t.bounces == 1;
            const bool gateA = gathers, gateB = gathers && oneBounce;
            if (gateA != targetMode) {
                const double tolA = gateA ? 0.04 : 0.08;
                CHECK_MSG(worstA <= tolA,
                          "%s, arm A: the floor beside a black wall is the sky's 1 - F(d) (worst "
                          "|off| %.3f, bar %.2f, d >= 0.1 m)", t.name, worstA, tolA);
            }
            if (gateB != targetMode) {
                const double tolB = gathers ? 0.03 : 0.06;
                CHECK_MSG(worstB <= tolB,
                          "%s, arm B: a white wall under the sky bounces albedo x its sky "
                          "irradiance onto the floor (B - A against the radiosity at %d bounce(s); "
                          "worst |off| %.3f, bar %.2f)", t.name, t.bounces, worstB, tolB);
            }
            if (!targetMode) {
                CHECK_MSG(B.top > 1.4 * open.top,
                          "%s: the white wall's top face is lit (%.4f against the open floor's "
                          "%.4f)", t.name, B.top, open.top);
            }
        }
    }

    // ---- CASE D: THE FLOOR UNDER A HOVERING MOVER (MOVER-OCCLUSION-1) ----------------
    // A BLACK 2 m cube, a MOVER, its bottom face 1 m above the floor, at Epic, under the
    // same constant sky. The point under its centre sees the bottom face as a parallel
    // square: the differential element to a parallel rectangle a x b at distance c, the
    // element under a corner, F = 1/(2 pi) [X/sqrt(1+X^2) atan(Y/sqrt(1+X^2)) + Y/sqrt(1+Y^2)
    // atan(X/sqrt(1+Y^2))], X = a/c, Y = b/c (Howell's catalogue B-3); centred = four
    // quadrants of a/2 x a/2: F = 4 F_corner(1, 1) = 0.554, so E/E_open = 0.446. (The
    // sides cannot be seen from under the bottom face.) Read two ways, each against the
    // same pixels with the cube hidden: DIRECTLY (a low camera beside it: the floor pixel,
    // the gather's rays meeting the mover) and IN A MIRROR (a vertical mirror 3 m away:
    // the reflection ray's HIT on the floor under the mover, whose store radiance the
    // mover must gate — jahMoverSkyVisibility). Bars: the direct reading within 0.05 of
    // the closed form, the mirror's within 0.05 of the direct one.
    // THE MIRROR HALF GATES (gi.contact_occlusion); THE DIRECT HALF is a target
    // (gi.contact_occlusion_target): the floor pixel reads 0.380 against 0.446 — the
    // gather's own answer, too dark by 0.066 (HOVER-GATHER-1).
    if (rays && !measureMode) {
        const Tier &epic = kTiers[0];
        setTier(f, epic, GiToggle::Auto, GiToggle::Auto);
        {   // THE RAY TIER'S REFLECTION ROW (Epic's trace): the mirror answers with rays,
            // not the voxel cone alone (a mover is not in the store the cone reads).
            PostFxDesc fx;
            fx.allowOffscreen = true;
            fx.ssr = 2;
            fx.ssao = false;
            fx.hdrReadback = true;
            f.view->setPostFx(fx);
        }
        f.s->setNodeVisible(f.wall, false);
        f.s->setNodeVisible(f.box, false);
        const float x0 = 0.5f;
        const NodeId hover = f.s->createNode();
        f.s->setNodeMovable(hover, true);
        f.s->attachMesh(hover, f.cube, f.black);
        f.s->setNodeTransform(hover, Vec3(x0, 2.0f, 0.0f), Quat(), Vec3(2.0f, 2.0f, 2.0f));
        const NodeId mirror = f.s->createNode();
        PbrParams mp; mp.albedo = Colour(1.0f, 1.0f, 1.0f); mp.metalness = 1.0f; mp.roughness = 0.0f;
        f.s->attachMesh(mirror, f.cube, f.s->createPbrMaterial(mp));
        f.s->setNodeTransform(mirror, Vec3(x0 - 3.1f, 1.5f, 0.0f), Quat(), Vec3(0.2f, 3.0f, 8.0f));   // face x0 - 3
        const double cornerF = [] {
            const double X = 1.0, Y = 1.0;
            return 1.0 / (2.0 * kPi) * (X / std::sqrt(1 + X * X) * std::atan(Y / std::sqrt(1 + X * X)) +
                                        Y / std::sqrt(1 + Y * Y) * std::atan(X / std::sqrt(1 + Y * Y)));
        }();
        const double closed = 1.0 - 4.0 * cornerF;
        const Vec3 eye(x0 + 3.5f, 0.35f, 0.0f);
        // THE MIRROR'S CAMERA: to the side (z = 3), looking at the point's image behind
        // the mirror's front face (x0 - 3): its ray meets the mirror at (x0 - 3, 0.48,
        // 1.8), and the reflected ray reaches the point under the cube's centre passing
        // 0.16 m high under its edge — the mirror shows the point, the camera's line of
        // sight never crosses the cube.
        const Vec3 mirrorEye(x0 - 1.0f, 0.8f, 3.0f);
        int settleFrames = 0;
        const auto readCentre = [&](const Vec3 &target, bool cube) {
            f.s->setNodeVisible(hover, cube);
            const bool viaMirror = target.x < x0 - 3.0f;
            f.view->setCamera(enginetest::testCameraDescLookAt(viaMirror ? mirrorEye : eye, target));
            f.s->refreshGlobalIllumination();
            int frames = 120;
            render(f.e, 120);
            while (!f.s->giStatus().giAtRest && frames < 1020) { render(f.e, 10); frames += 10; }
            settleFrames = std::max(settleFrames, frames + 8);
            double acc = 0.0;
            for (int k = 0; k < 8; ++k) {
                f.e->renderOneFrame();
                ImageF img; f.view->readPixelsHdr(img);
                double sum = 0.0; int n = 0;
                for (int y = int(kSize / 2) - 2; y <= int(kSize / 2) + 2; ++y)
                    for (int x = int(kSize / 2) - 6; x <= int(kSize / 2) + 6; ++x) {
                        const Colour c = img.at(unsigned(x), unsigned(y));
                        sum += double(c.r) + c.g + c.b; ++n;
                    }
                acc += sum / n / 8.0;
            }
            return acc;
        };
        const Vec3 under(x0, 0.0f, 0.0f), inMirror(x0 - 6.0f, 0.0f, 0.0f);   // the point's image
        const double dOpen = readCentre(under, false), dCube = readCentre(under, true);
        const double mOpen = readCentre(inMirror, false), mCube = readCentre(inMirror, true);
        const double direct = dCube / dOpen, mirrored = mCube / mOpen;
        std::printf("\n== CASE D (Epic): the floor under a hovering 2 m mover, bottom 1 m up: closed form %.3f; "
                    "direct %.3f (%.4f / %.4f); in the mirror %.3f (%.4f / %.4f)\n", closed, direct, dCube,
                    dOpen, mirrored, mCube, mOpen);
        std::printf("   (each reading settled: 120 frames, then until giAtRest, then 8 averaged — at most %d "
                    "frames)\n", settleFrames);
        if (targetMode) {
            // THE TARGET: the floor pixel itself (the gather's own answer) — 0.380 at
            // delivery, too dark by 0.066 (HOVER-GATHER-1).
            CHECK_MSG(std::fabs(direct - closed) <= 0.05,
                      "the floor under a hovering mover is darkened by its sky occlusion: %.3f against the "
                      "closed form %.3f (bar 0.05)", direct, closed);
        } else {
            // THE MOVER GATE'S BAR: the mirror's reflection ray HITS the floor under the
            // cube and reads the store, which holds no mover — only the hit's mover gate
            // (jahMoverSkyVisibility) can darken it. 0.501 at delivery.
            CHECK_MSG(std::fabs(mirrored - closed) <= 0.06,
                      "a mirror's reflection of the floor under a hovering mover carries the mover's sky "
                      "occlusion: %.3f against the closed form %.3f (bar 0.06)", mirrored, closed);
        }
    }

    f.view->setScene(nullptr);
    f.e->destroyScene(f.s);
    f.e->destroyView(f.view);
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
