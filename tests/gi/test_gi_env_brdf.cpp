// gi.env_brdf — THE ENVIRONMENT'S SPECULAR TERM IS THE SPLIT SUM (PHOTON-3 stage 1).
//
// THE CLAIM. Under a UNIFORM environment of radiance L the split sum is not an
// approximation: the radiance a surface reflects toward a viewer at cos theta_v = v is
//
//     L * integral over l of f_s( l, v ) cos_l  =  L * kS * ( F0 A( v, r ) + B( v, r ) ),
//
// A and B the directional albedo of HlmsPbs's own specular lobe (BRDF_Default: GGX, the
// height-correlated Smith visibility, Schlick on V.H, alpha = r^2), linear in F0. This
// suite states that integral INDEPENDENTLY (enginetest::ggxSpecularAlbedo, its own
// sampler) and holds the rendered environment term to it, within 1 % + 1 LSB of the
// RGBA16F radiance read, for a dielectric (F0 0.04) and a metal (F0 0.9) at perceptual
// roughness 0.2, 0.5 and 1, at three view cosines.
//
// THE FIXTURE ISOLATES THE TERM. One box, its top face read at the picture's centre (the
// camera looks at that point, so v there is the camera's elevation seen from it), under a
// 1 x 1 equirect sky (every direction L, every prefiltered mip L) at Sky Light 1, GI off,
// no light: the pixel is Rd + Rs of BRDF_EnvMap. The diffuse is zero by the material - a
// BLACK dielectric (base 0, metalness 0: F0 0.04, albedo 0) and a full metal (metalness
// 1: albedo 0) - so the pixel is Rs alone, kS 1.
//
// WHAT IT GUARDS. The pin's own arm with no DFG table bound (no area light: every shipped
// scene) took envBRDF = ( 1, 0, 1 ), i.e. Rs = L kS fresnelS with
// fresnelS = F0 + ( 1 - v )^5 ( max( 1 - r, F0 ) - F0 ) - no albedo integral at all. At
// the base this suite is RED: the rough metal at normal view read 0.900 L against the true
// 0.276 L (3.26 x), the rough dielectric at v 0.2 r 0.5 0.191 L against 0.129 L (1.48 x).
// Every case prints the shipped formula's number beside the truth, and the suite checks
// that the bar would refuse it where it differs (so the fixture discriminates).
//
// Its own binary. Frames, never wall clock: one settle per material.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

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

static double srgbToLinear(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }

static const unsigned kSize = 128u;
static const unsigned char kSkyByte = 190;   // the sky's sRGB byte -> L

/// One unit in the last place of a half float at `x` (10 mantissa bits).
static double halfLsb(double x) { return x > 0.0 ? std::ldexp(1.0, int(std::floor(std::log2(x))) - 10) : 0.0; }

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-env-brdf-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();

    View *view = e->createOffscreenView("envbrdf", kSize, kSize, Colour(0, 0, 0));
    if (!view) { std::printf("FAIL: the offscreen view\n"); return 1; }
    view->setOffscreenContract(OffscreenContract::StillPicture);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 0;
        fx.hdrReadback = true;   // every number below is RADIANCE (HDR-READBACK-1)
        view->setPostFx(fx);
    }
    Scene *s = e->createScene("envbrdf");
    view->setScene(s);

    // ---- THE FIXTURE: one box --------------------------------------------------
    const NodeId n = s->createNode();
    const MeshId m = s->createMesh(enginetest::unitCubeMesh());
    PbrParams p;
    p.albedo = Colour(0.0f, 0.0f, 0.0f);
    p.metalness = 0.0f;
    p.roughness = 1.0f;
    const MaterialId mat = s->createPbrMaterial(p);
    CHECK(n && m && mat && s->attachMesh(n, m, mat), "the box exists");

    // ---- THE UNIFORM SKY, Sky Light 1, GI off -----------------------------------
    const double L = srgbToLinear(kSkyByte / 255.0);
    const unsigned char skyPx[4] = { kSkyByte, kSkyByte, kSkyByte, 255 };
    SkyDesc sky;
    sky.mode = SkyMode::Equirectangular;
    sky.equirect = s->createTexture(1, 1, skyPx, true);
    CHECK(sky.equirect && s->setSky(sky), "the uniform sky binds");
    float skySh[27] = { 0.0f };
    bool shReady = false;
    for (int f = 0; f < 20 && !shReady; ++f) {
        enginetest::frames(e, 1);
        shReady = s->published().copySkyAmbientSh(skySh);
    }
    CHECK(shReady, "the sky's SH is integrated");
    s->setAmbientSh(skySh);
    s->setEnvironmentLight(Colour(1.0f, 1.0f, 1.0f, 1.0f));
    GiParams off;
    off.mode = GiMode::Off;
    CHECK(s->setGlobalIllumination(off), "GI off");

    const Vec3 top(0.0f, 0.5f, 0.0f);
    // THE CURRENCY: the corner pixel is the sky, radiance L (HDR-READBACK-1).
    enginetest::testCameraLookAt(view, Vec3(0.3f, 3.0f, 1.2f), top);
    enginetest::frames(e, 6);
    {
        ImageF img;
        CHECK(view->readPixelsHdr(img), "the view reads its radiance back");
        const double c = img.at(2, 2).g;
        CHECK_MSG(std::fabs(c - L) < 0.005 * L,
                  "the sky draws its own radiance (%.5f against L = %.5f, bar 0.5 %%)", c, L);
    }

    // ---- THE CASES ---------------------------------------------------------------
    // Three poses: the camera on a 2.4 m sphere around the face's centre at elevation
    // asin( v ) (a small azimuth, so no pose is degenerate for the look-at's +Y up).
    const double kViews[3] = { 0.95, 0.5, 0.2 };
    const double kRough[3] = { 0.2, 0.5, 1.0 };
    struct Mat { const char *name; double f0; Colour albedo; float metal; };
    const Mat kMats[2] = { { "dielectric", 0.04, Colour(0.0f, 0.0f, 0.0f), 0.0f },
                           { "metal",      0.90, Colour(0.9f, 0.9f, 0.9f), 1.0f } };
    int discriminated = 0, differing = 0;
    std::printf("\n   %-10s %4s %4s  %9s %9s %7s  %9s %6s\n", "material", "r", "v", "read/L", "truth/L",
                "ratio", "shipped/L", "x");
    for (const double v : kViews) {
        const double el = std::asin(v), d = 2.4;
        const Vec3 cam(float(top.x + d * std::cos(el) * 0.2), float(top.y + d * v),
                       float(top.z + d * std::cos(el) * 0.98));
        enginetest::testCameraLookAt(view, cam, top);
        // THE VIEW COSINE AT THE READ PIXEL'S OWN POINT: the centre pixel's ray is half a pixel off the look-at
        // axis (0.18 degree at 45 degrees over 128 px), which at grazing moves v by 0.003 - 1.4 % of a polished
        // dielectric's B at v 0.2. The point is where that pixel's centre ray meets the top face (y 0.5).
        const Vec3 at = enginetest::groundPointForPixel(cam, top, kSize / 2u, kSize / 2u, kSize, top.y, 45.0f);
        const double dx = cam.x - at.x, dy = cam.y - at.y, dz = cam.z - at.z;
        const double vAt = dy / std::sqrt(dx * dx + dy * dy + dz * dz);
        for (const Mat &mt : kMats) {
            for (const double r : kRough) {
                PbrParams q = p;
                q.albedo = mt.albedo;
                q.metalness = mt.metal;
                q.roughness = float(r);
                CHECK(s->setPbrMaterial(mat, q), "the material is set");
                enginetest::frames(e, 3);
                ImageF img;
                view->readPixelsHdr(img);
                const double read = img.at(kSize / 2u, kSize / 2u).g;
                double A = 0.0, B = 0.0;
                enginetest::ggxSpecularAlbedo(vAt, r, A, B, 256);
                const double truth = L * (mt.f0 * A + B);
                const double shipped =
                    L * (mt.f0 + std::pow(1.0 - vAt, 5.0) * (std::max(1.0 - r, mt.f0) - mt.f0));
                const double bar = 0.01 * truth + halfLsb(truth);
                std::printf("   %-10s %4.2f %4.2f  %9.5f %9.5f %7.4f  %9.5f %6.3f\n", mt.name, r, vAt,
                            read / L, truth / L, read / truth, shipped / L, shipped / truth);
                CHECK_MSG(std::fabs(read - truth) <= bar,
                          "%s r %.1f v %.2f: the environment's specular is L kS ( F0 A + B ) within 1 %% + "
                          "1 LSB (%.5f against %.5f, bar %.5f)", mt.name, r, vAt, read, truth, bar);
                if (std::fabs(shipped - truth) > 4.0 * bar) {
                    ++differing;
                    if (std::fabs(shipped - read) > bar) ++discriminated;
                }
            }
        }
    }
    // THE FIXTURE DISCRIMINATES: wherever the pin's (1, 0, 1) arm differs from the truth by
    // more than four bars, the read is not the shipped number.
    CHECK_MSG(differing >= 10 && discriminated == differing,
              "the shipped ( 1, 0, 1 ) arm would be RED here: %d of the 18 cases differ from the truth by "
              "over four bars, and the read refuses the shipped number in %d of them", differing, discriminated);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
