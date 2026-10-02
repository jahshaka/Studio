// gi.field_edge — THE IRRADIANCE FIELD IS FLAT OVER AN OPEN FLOOR TO ITS STATED REACH
// (FIELD-EDGE-1, plan 9av).
//
// THE FIXTURE. An 80 m matte floor (albedo 0.5) under a constant sky (a 1x1 grey equirect at
// unit Sky Light gain), nothing else: every point of the floor sees the same sky and the same
// infinite floor, so its irradiance is ONE number everywhere. The Low tier (the field carries
// the diffuse; no gather), the camera straight down, orthographic, centred on the field.
//
// THE MEASUREMENT. The floor's radiance along the four half-axes and the four diagonals from
// the field's centre, out past the box the field states (GiStatus::ifdMin / ifdMax — cascade
// 0's box), each sample over a small square, divided by the centre's. A field that carries
// the same answer everywhere inside its reach reads 1 at every distance up to the box's face.
//
// THE BAR. Within 2 % of the centre at every sample inside the stated reach (the face). Past
// the face the field hands over to the cones (the edge fade, JahIfd_piece_ps.any) — printed.
// A photon-target row (gi.field_edge_target): it prints and does not gate.
//
// WHAT IT FOUND (FIELD-EDGE-1, d-build c5fbe870c; spikes/photon-ii-1/). THE PLAN ROW'S PREMISE
// IS FALSE: the 0.86-0.89 is not the field's edge. At camera height 3 m (the contact fixture's)
// the floor reads 1.000 out to 3.5 m, then a THIN DARK RING at r = 4.0 m in every direction
// (0.861 on the axes, 0.897 on the diagonals; a circle, not the box), then ~0.97 out to the
// face. At camera heights 1.5 / 2 / 2.5 / 3.5 / 4 / 5 m there is no ring. The cause, predicted
// for all seven heights from the arithmetic: the cage prologue's VIEW BIAS (0.2 spacings along
// normalize( -inPs.pos ), JahIfd_piece_ps.any) has a vertical share that shrinks with the
// distance from the eye, so the biased sample point CROSSES A PROBE LAYER where
// 0.05 + 0.2 x 1.422 x 3 / sqrt( r^2 + 9 ) = 0.222 (the floor at grid y 3.778, the layer at 4):
// r = 3.95 m. On the crossing the cage's front layer has trilinear weight ~0 and its back
// layer (under the floor) weight 0, so sumIfdWeight falls under the weight fade's knee
// (0.0005 .. 0.005; its comment still assumes upstream's deleted 0.2 floor) and the field fades
// to black for a few pixels: the ring. Outside it the cage holds the one layer 0.156 m above
// the floor (inside the floor's voxel row), which reads ~3 % low. What remains at the face at
// the other heights: 0.971 - 0.983 AT r = 5.0 m (the face sample), >= 0.989 at 4.75 m.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!(cond)) ++failures;                                                \
    } while (0)

namespace {

const unsigned kSize = 768u;
const float kOrthoHalf = 6.0f;
float kCamY = 3.0f;   // JAH_FIELD_EDGE_CAMY overrides (a measurement)

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

/// The mean r+g+b over the world square centred on (x, z), half side h.
double squareMean(const ImageF &img, float x, float z, float h)
{
    const int px0 = int(std::floor(worldToPixel(x - h))), px1 = int(std::ceil(worldToPixel(x + h)));
    const int py0 = int(std::floor(worldToPixel(z - h))), py1 = int(std::ceil(worldToPixel(z + h)));
    double s = 0.0;
    int n = 0;
    for (int y = std::max(py0, 0); y < std::min(py1, int(img.height)); ++y)
        for (int xx = std::max(px0, 0); xx < std::min(px1, int(img.width)); ++xx) {
            const Colour c = img.at(unsigned(xx), unsigned(y));
            s += double(c.r) + double(c.g) + double(c.b);
            ++n;
        }
    return n ? s / n : 0.0;
}

}   // namespace

int main()
{
    if (const char *y = std::getenv("JAH_FIELD_EDGE_CAMY")) kCamY = float(std::atof(y));
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-field-edge-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setFixedFrameDelta(1.0f / 60.0f);
    View *view = e->createOffscreenView("field-edge", kSize, kSize, Colour(0, 0, 0));
    if (!view) { std::printf("FAIL: view\n"); return 1; }
    view->setOffscreenContract(OffscreenContract::StillPicture);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;
    fx.ssao = false;
    fx.hdrReadback = true;
    view->setPostFx(fx);
    view->setShadows(true);
    Scene *s = e->createScene("field-edge");
    view->setScene(s);
    view->setCamera(topDownCamera());

    // JAH_FIELD_EDGE_GRADSKY=1 (a measurement): a 1x2 equirect, the upper hemisphere bright and
    // the lower dark, in place of the constant grey.
    const bool gradSky = std::getenv("JAH_FIELD_EDGE_GRADSKY") != nullptr;
    const unsigned char px[8] = { 128, 128, 128, 255, 128, 128, 128, 255 };
    const unsigned char pxGrad[8] = { 230, 230, 240, 255, 40, 40, 40, 255 };
    SkyDesc sky;
    sky.mode = SkyMode::Equirectangular;
    sky.equirect = gradSky ? s->createTexture(1, 2, pxGrad, true) : s->createTexture(1, 1, px, true);
    CHECK_MSG(s->setSky(sky), "%s", "the constant sky binds");
    s->setEnvironmentLight(Colour(1.0f, 1.0f, 1.0f));

    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    PbrParams mp;
    mp.albedo = Colour(0.5f, 0.5f, 0.5f);
    mp.roughness = 1.0f;
    mp.workflow = PbrParams::Workflow::Specular;   // matte: F0 0, the diffuse alone
    mp.ior = 1.0f;
    mp.specularColour = Colour(0.0f, 0.0f, 0.0f);
    const NodeId floorN = s->createNode();
    s->attachMesh(floorN, cube, s->createPbrMaterial(mp));
    s->setNodeTransform(floorN, Vec3(0.0f, -0.25f, 0.0f), Quat(), Vec3(80.0f, 0.5f, 80.0f));

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Low;
    gi.numBounces = 1;
    gi.gather = GiToggle::Off;
    gi.ddgi = std::getenv("JAH_FIELD_EDGE_NOFIELD") ? GiToggle::Off : GiToggle::On;   // measurement
    if (std::getenv("JAH_FIELD_EDGE_GIOFF")) gi.mode = GiMode::Off;                     // measurement
    if (const char *c = std::getenv("JAH_FIELD_EDGE_CASCADE")) {   // measurement: "half,res"
        float half = 5.0f; int res = 64;
        std::sscanf(c, "%f,%d", &half, &res);
        gi.cascadeCount = 2;
        gi.cascadeSet[0] = GiParams::GiCascadeDesc{ half, res, 0.0f };
        gi.cascadeSet[1] = GiParams::GiCascadeDesc{ 4.0f * half, res, 0.0f };
    }
    CHECK_MSG(s->setGlobalIllumination(gi), "%s", "the Low tier builds");
    int frames = 0;
    render(e, 60);
    frames += 60;
    while (!s->giStatus().giAtRest && frames < 2400) { render(e, 10); frames += 10; }
    const GiStatus st = s->giStatus();
    CHECK_MSG(st.giAtRest && (st.ifdBound || gi.ddgi == GiToggle::Off || gi.mode == GiMode::Off),
              "the field is bound and at rest (%d frames)", frames);
    std::printf("   the field's stated box: (%.3f %.3f %.3f) .. (%.3f %.3f %.3f), %d probes\n",
                double(st.ifdMin.x), double(st.ifdMin.y), double(st.ifdMin.z), double(st.ifdMax.x),
                double(st.ifdMax.y), double(st.ifdMax.z), st.ifdProbes);

    // THE READING: the mean of 8 frames (trap 7: after rest, nothing should move).
    const int kDirs = 8;
    const float dirs[kDirs][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
                                   { 0.70710678f, 0.70710678f }, { -0.70710678f, 0.70710678f },
                                   { 0.70710678f, -0.70710678f }, { -0.70710678f, -0.70710678f } };
    const int kSteps = 23;   // 0 .. 5.5 m in 0.25 m steps
    static double v[kDirs][kSteps];
    double centre = 0.0;
    for (int d = 0; d < kDirs; ++d) for (int i = 0; i < kSteps; ++i) v[d][i] = 0.0;
    for (int k = 0; k < 8; ++k) {
        render(e, 1);
        ImageF img;
        view->readPixelsHdr(img);
        centre += squareMean(img, 0.0f, 0.0f, 0.1f) / 8.0;
        if (const char *dump = std::getenv("JAH_FIELD_EDGE_DUMP")) {   // measurement: a PGM of the
            if (k == 0) {                                               // floor / the centre, x200 + 0.5 grey
                const double c0 = squareMean(img, 0.0f, 0.0f, 0.1f);
                if (FILE *f = std::fopen(dump, "wb")) {
                    std::fprintf(f, "P5\n%u %u\n255\n", img.width, img.height);
                    for (unsigned yy = 0; yy < img.height; ++yy)
                        for (unsigned xx = 0; xx < img.width; ++xx) {
                            const Colour c = img.at(xx, yy);
                            const double r = (double(c.r) + c.g + c.b) / c0;
                            const int q = int(std::lround(std::max(0.0, std::min(255.0, 128.0 + (r - 1.0) * 1000.0))));
                            std::fputc(q, f);
                        }
                    std::fclose(f);
                }
            }
        }
        for (int d = 0; d < kDirs; ++d)
            for (int i = 0; i < kSteps; ++i) {
                const float r = 0.25f * float(i);
                v[d][i] += squareMean(img, dirs[d][0] * r, dirs[d][1] * r, 0.06f) / 8.0;
            }
    }
    const float cx = 0.5f * (st.ifdMin.x + st.ifdMax.x), cz = 0.5f * (st.ifdMin.z + st.ifdMax.z);
    const float hx = 0.5f * (st.ifdMax.x - st.ifdMin.x), hz = 0.5f * (st.ifdMax.z - st.ifdMin.z);
    std::printf("   centre %.5f; the box's centre (%.3f, %.3f), half extents %.3f x %.3f m\n", centre,
                double(cx), double(cz), double(hx), double(hz));
    double worst = 0.0, worstR = 0.0;
    int worstDir = -1;
    for (int d = 0; d < kDirs; ++d) {
        std::printf("   dir (%+.2f %+.2f):", double(dirs[d][0]), double(dirs[d][1]));
        for (int i = 0; i < kSteps; ++i) {
            const double ratio = v[d][i] / centre;
            std::printf(" %.3f", ratio);
            const float x = dirs[d][0] * 0.25f * float(i), z = dirs[d][1] * 0.25f * float(i);
            const bool inside = std::fabs(x - cx) <= hx && std::fabs(z - cz) <= hz;
            if (inside && std::fabs(ratio - 1.0) > worst) {
                worst = std::fabs(ratio - 1.0);
                worstR = 0.25 * i;
                worstDir = d;
            }
        }
        std::printf("\n");
    }
    std::printf("   (columns: r = 0, 0.25, ... 5.5 m from the camera's point)\n");
    CHECK_MSG(centre > 0.0 && worst <= 0.02,
              "THE FIELD IS FLAT OVER AN OPEN FLOOR TO ITS STATED REACH: worst %.2f %% at %.2f m along dir %d "
              "(bar 2 %%)", 100.0 * worst, worstR, worstDir);

    GiParams off; off.mode = GiMode::Off;
    s->setGlobalIllumination(off);
    view->setScene(nullptr);
    e->destroyScene(s);
    engine.reset();
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
