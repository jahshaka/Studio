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
// and the cones one at a time — and gates nothing.
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

Radiosity solveRadiosity(double rhoW, double rhoF)
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
    std::vector<double> Ef(nf, 1.0), Ew(nw, 0.5);
    for (int it = 0; it < 200; ++it) {
        std::vector<double> nEf(nf), nEw(nw);
        for (int i = 0; i < nf; ++i) {
            double e = floorSky[i];
            for (int j = 0; j < nw; ++j) e += Ffw[size_t(i) * nw + j] * rhoW * Ew[j];
            nEf[i] = e;
        }
        for (int j = 0; j < nw; ++j) {
            double e = wallSky[j];
            double seen = 0.0;
            for (int i = 0; i < nf; ++i) {
                e += Fwf[size_t(j) * nf + i] * rhoF * Ef[i];
                seen += Fwf[size_t(j) * nf + i];
            }
            e += (0.5 - seen) * rhoF * 1.0;   // the floor beyond the solved span: open
            nEw[j] = e;
        }
        double delta = 0.0;
        for (int i = 0; i < nf; ++i) delta = std::max(delta, std::fabs(nEf[i] - Ef[i]));
        Ef.swap(nEf);
        Ew.swap(nEw);
        if (delta < 1e-9) break;
    }
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

    f.cube = f.s->createMesh(enginetest::unitCubeMesh());
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
struct Profile { double v[kNd]; double far = 0.0; double top = 0.0; bool rest = false; int frames = 0; };

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

    // ---- THE REFERENCE ----------------------------------------------------
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
            const Profile C = measure(f, Arm::Cube);
            std::printf("\n== %s — %s (gather %s, field %s; frames open %d A %d B %d C %d; rest %d%d%d%d)\n",
                        t.name, term.name, st.gather.running ? "RUNNING" : "off",
                        st.ifdTargetSamples ? "on" : "off", open.frames, A.frames, B.frames,
                        C.frames, open.rest, A.rest, B.rest, C.rest);
            double rA[kNd], rB[kNd], rC[kNd], dBA[kNd], refBA[kNd];
            for (int i = 0; i < kNd; ++i) {
                rA[i] = A.v[i] / open.v[i];
                rB[i] = B.v[i] / open.v[i];
                rC[i] = C.v[i] / open.v[i];
                dBA[i] = rB[i] - rA[i];
                refBA[i] = refBv[i] - refA[i];
            }
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

            if (measureMode) continue;
            const bool fine = t.q == GiQuality::Epic || t.q == GiQuality::High;
            const double tol = fine ? 0.04 : 0.08;
            double worstA = 0.0, worstB = 0.0;
            for (int i = 1; i < kNd; ++i) {   // d >= 0.1 m
                worstA = std::max(worstA, std::fabs(rA[i] - refA[i]));
                worstB = std::max(worstB, std::fabs(dBA[i] - refBA[i]));
            }
            CHECK_MSG(worstA <= tol,
                      "%s, arm A: the floor beside a black wall is the sky's 1 - F(d) (worst |off| %.3f, "
                      "bar %.2f, d >= 0.1 m)", t.name, worstA, tol);
            CHECK_MSG(worstB <= tol,
                      "%s, arm B: a white wall adds its bounce (B - A against the radiosity; worst "
                      "|off| %.3f, bar %.2f)", t.name, worstB, tol);
        }
    }

    f.view->setScene(nullptr);
    f.e->destroyScene(f.s);
    f.e->destroyView(f.view);
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
