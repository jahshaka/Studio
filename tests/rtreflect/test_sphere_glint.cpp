// rtreflect.sphere_glint — A LAMP HAS A SIZE (PHOTON-7 stage 11, the lead's ruling: the sphere-light glint, the pixel and
// the ray hits through ONE JahBrdf function, jahSphereLightSpecDir - Karis 2013's representative point and the lobe
// widened by the sphere's angular size, renormalised).
//
// THE FIXTURE: a polished metal floor (perceptual roughness 0.05) under ONE point lamp at (0, 2, -2), no GI, no
// reflection pass, no sky in the floor: the floor's picture is the pixel's direct specular alone - the lamp's glint. The
// eye at (0, 1, 2) looks at the mirror point (0, 0, 2/3): the lamp's virtual image lies D = 5 m from the eye, so a lamp
// of source radius R shows, through the flat floor, a disc of angular radius asin( R / D ).
// THE LAWS (in HDR, 960 x 540, 45 degrees of vertical field):
//   (a) THE GLINT IS THE LAMP'S SIZE: at R = 0.1 m the glint's half-maximum area, as a disc, has an angular radius within
//       0.8 - 2.0 x asin( R / D ) (the lobe's own widening past the disc - alpha' = alpha + R / 2d - adds to it; a point
//       glint at alpha 0.0025 is a few pixels whatever R is: the RED BEFORE);
//   (b) IT SCALES WITH THE LAMP: R 0.1 against R 0.05, 1.5 - 2.5 x;
//   (c) THE NEGATIVE CONTROL: a lamp of R 0.001 m (a point) is under half R 0.05's;
//   and printed: each glint's energy (the summed radiance over its 4 x window) - a lamp's size moves its glint, not its
//   power (the renormalisation is Karis's approximation: the ratio is printed, not judged).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

static const unsigned kWidth = 960;
static const unsigned kHeight = 540;
static const float kFovDeg = 45.0f;
static const float kPathD = 5.0f;   // the eye to the lamp's virtual image (0, -2, -2)

struct Glint { float peak = 0.0f; double areaPx = 0.0; float radiusRad = 0.0f; double energy = 0.0; };

static Glint measure(const ImageF &img, float expectRad)
{
    Glint g;
    unsigned px = 0, py = 0;
    for (unsigned y = kHeight / 3u; y < kHeight; ++y)
        for (unsigned x = 0; x < kWidth; ++x) {
            const Colour c = img.at(x, y);
            const float L = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
            if (std::isfinite(L) && L > g.peak) { g.peak = L; px = x; py = y; }
        }
    const float pixRad = (kFovDeg * 3.14159265f / 180.0f) / float(kHeight);
    const int win = int(std::ceil(4.0f * expectRad / pixRad)) + 8;
    for (int y = int(py) - win; y <= int(py) + win; ++y)
        for (int x = int(px) - win; x <= int(px) + win; ++x) {
            if (x < 0 || y < 0 || x >= int(kWidth) || y >= int(kHeight)) continue;
            const Colour c = img.at(unsigned(x), unsigned(y));
            const float L = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
            if (!std::isfinite(L)) continue;
            g.energy += L;
            if (L >= 0.5f * g.peak) g.areaPx += 1.0;
        }
    g.radiusRad = std::sqrt(float(g.areaPx) / 3.14159265f) * pixRad;
    return g;
}

static NodeId addBox(Scene *s, const PbrParams &p, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = s->createNode();
    s->attachMesh(n, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(p));
    enginetest::setNodeScale(s, n, scale);
    enginetest::setNodePosition(s, n, pos);
    return n;
}

static Glint glintAt(Engine *e, View *view, float radius)
{
    Scene *s = e->createScene("glint");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    PbrParams floorP;
    floorP.albedo = Colour(0.9f, 0.9f, 0.9f);
    floorP.metalness = 1.0f;
    floorP.roughness = 0.05f;
    addBox(s, floorP, Vec3(0.0f, -0.1f, 0.0f), Vec3(8.0f, 0.2f, 8.0f));
    const NodeId lamp = s->createNode();
    s->setNodeTransform(lamp, Vec3(0.0f, 2.0f, -2.0f), Quat(), Vec3(1, 1, 1));
    LightDesc l;
    l.type = LightType::Point;
    l.colour = Colour(1, 1, 1);
    l.intensity = 2.0f;
    l.range = 20.0f;
    l.sourceRadius = radius;
    l.castShadows = false;
    s->setLight(lamp, l);
    GiParams gi;
    gi.mode = GiMode::Off;
    s->setGlobalIllumination(gi);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;
    fx.hdrReadback = true;
    view->setPostFx(fx);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.0f, 2.0f), Vec3(0.0f, 0.0f, 2.0f / 3.0f));
    enginetest::frames(e, 12);
    ImageF img;
    const bool ok = view->readPixelsHdr(img) && img.width == kWidth && img.height == kHeight;
    Glint g = ok ? measure(img, std::asin(std::max(radius, 1e-3f) / kPathD)) : Glint();
    std::printf("-- R %.3f m: the glint's peak %.1f, half-max area %.0f px -> angular radius %.5f rad (the lamp's "
                "asin(R/D) %.5f), its energy %.1f\n", radius, g.peak, g.areaPx, g.radiusRad, std::asin(radius / kPathD),
                g.energy);
    e->destroyScene(s);
    return g;
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-sphere-glint-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    View *view = e->createOffscreenView("glint", kWidth, kHeight, Colour(0, 0, 0),
                                        jahshaka::engine::OffscreenContract::Live);
    const Glint p = glintAt(e, view, 0.001f), h = glintAt(e, view, 0.05f), g = glintAt(e, view, 0.1f);
    const float want = std::asin(0.1f / kPathD);
    CHECK_MSG(g.radiusRad >= 0.8f * want && g.radiusRad <= 2.0f * want,
              "(a) THE GLINT IS THE LAMP'S SIZE: R 0.1 m shows %.5f rad, 0.8 - 2.0 x asin(R/D) = %.5f", g.radiusRad, want);
    const float ratio = h.radiusRad > 0.0f ? g.radiusRad / h.radiusRad : 0.0f;
    CHECK_MSG(ratio >= 1.5f && ratio <= 2.5f, "(b) IT SCALES WITH THE LAMP: R 0.1 / R 0.05 = %.2f (1.5 - 2.5)", ratio);
    CHECK_MSG(p.radiusRad < 0.5f * h.radiusRad, "(c) THE NEGATIVE CONTROL: a point (R 0.001) %.5f rad under half R 0.05's "
              "%.5f", p.radiusRad, h.radiusRad);
    std::printf("-- the glint's energy, R 0.05 / R 0.001 = %.3f, R 0.1 / R 0.001 = %.3f (printed, not judged)\n",
                p.energy > 0.0 ? h.energy / p.energy : 0.0, p.energy > 0.0 ? g.energy / p.energy : 0.0);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
