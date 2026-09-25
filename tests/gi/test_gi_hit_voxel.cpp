// gi.hit_voxel — A RAY'S HIT ON A STATIC SURFACE, SHADED FROM THE VOXELS, IS THAT SURFACE'S RADIANCE
// (PHOTON-VOXEL-5 item (iv); HIT-SHADE-1's finding F-A).
//
// THE FIXTURE is gi.hit_shade's arm (a) with the static control alone: a perfect mirror wall the
// camera faces, a matte crate (unit cube, no cards) BEHIND the camera where no screen-space march
// can see it, the sun from the mirror's side lighting the face the mirror shows. The reflection's
// hit on the crate is shaded by jahVoxelRadiance (jah_rq_hit.glsl): the voxels' light at the hit.
// The RASTER is the crate seen from the camera reflected in the mirror plane (flipped), with the
// mirror hidden: the same surface's radiance. HIT-SHADE-1 measured the reflection at 0.045 against
// the raster's 0.366 (12 %, GI on).
//
// THE BAR (derived): the voxel is the area mean of what it holds - a face voxel of a matte crate
// lit uniformly holds that face's radiance - so the reflection is the raster within the store's
// tolerance: the face's light over the voxel's c (the injection's premultiply undone), the
// half-float light store, the voxel's own sampled sun visibility vs the raster's shadow map on a
// fully lit face (both 1), the bounce (injected into both). 5 % of the radiance.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "../support/voxeldump.h"

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

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

static MaterialId matte(Scene *s, const Colour &albedo)
{
    PbrParams p;
    p.albedo = albedo;
    p.roughness = 1.0f;
    p.workflow = PbrParams::Workflow::Specular;
    p.ior = 1.0f;
    p.specularColour = Colour(0.0f, 0.0f, 0.0f);
    return s->createPbrMaterial(p);
}

static const unsigned kSize = 384;
static const float kMirrorZ = 4.85f;
static const Vec3 kCam(0.0f, 1.0f, -3.0f);
static const Vec3 kCamR(0.0f, 1.0f, 2.0f * kMirrorZ + 3.0f);

struct Mask { std::vector<unsigned char> m; unsigned n = 0; };

static Mask diffMask(const ImageF &a, const ImageF &b, bool flip, float thr)
{
    Mask r;
    r.m.assign(size_t(a.width) * a.height, 0u);
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour p = a.at(x, y), q = b.at(x, y);
            const float d = std::max(std::fabs(p.r - q.r), std::max(std::fabs(p.g - q.g), std::fabs(p.b - q.b)));
            if (d > thr) { r.m[size_t(y) * a.width + (flip ? a.width - 1u - x : x)] = 1u; ++r.n; }
        }
    return r;
}

static Mask erode(const Mask &a, unsigned w, unsigned h, int k)
{
    Mask r;
    r.m.assign(a.m.size(), 0u);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            bool all = true;
            for (int dy = -k; dy <= k && all; ++dy)
                for (int dx = -k; dx <= k && all; ++dx) {
                    const int xx = int(x) + dx, yy = int(y) + dy;
                    all = xx >= 0 && yy >= 0 && xx < int(w) && yy < int(h) && a.m[size_t(yy) * w + xx];
                }
            if (all) { r.m[size_t(y) * w + x] = 1u; ++r.n; }
        }
    return r;
}

static Colour maskMean(const ImageF &img, const Mask &m, bool flip)
{
    double s[3] = { 0, 0, 0 };
    unsigned n = 0;
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            if (!m.m[size_t(y) * img.width + x]) continue;
            const Colour c = img.at(flip ? img.width - 1u - x : x, y);
            s[0] += c.r; s[1] += c.g; s[2] += c.b; ++n;
        }
    return n ? Colour(float(s[0] / n), float(s[1] / n), float(s[2] / n)) : Colour(0, 0, 0);
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-hit-voxel-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("hitvoxel", kSize, kSize, Colour(0, 0, 0));
    Scene *s = e->createScene("hitvoxel");
    view->setScene(s);
    if (!e->rayQueryAvailable()) {
        std::printf("ok: no ray queries on this machine - gi.hit_voxel skips cleanly\n");
        return 0;
    }
    view->setOffscreenContract(OffscreenContract::StillPicture);
    view->setShadows(true);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.04f, 0.04f, 0.04f));
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    const NodeId wall = s->createNode();
    {
        PbrParams wp;
        wp.albedo = Colour(1, 1, 1);
        wp.metalness = 1.0f;
        wp.roughness = 0.0f;
        s->attachMesh(wall, cube, s->createPbrMaterial(wp));
    }
    enginetest::setNodeScale(s, wall, Vec3(14.0f, 9.0f, 0.3f));
    enginetest::setNodePosition(s, wall, Vec3(0.0f, 1.0f, kMirrorZ + 0.15f));
    const NodeId floorN = s->createNode();
    s->attachMesh(floorN, cube, matte(s, Colour(0.3f, 0.3f, 0.3f)));
    s->setNodeTransform(floorN, Vec3(0.0f, -0.55f, -4.0f), Quat(), Vec3(30.0f, 0.1f, 30.0f));
    enginetest::addDirectionalLight(s, Vec3(-0.25f, -1.0f, -1.05f), 3.0f);
    const NodeId crate = s->createNode();
    s->attachMesh(crate, cube, matte(s, Colour(0.7f, 0.45f, 0.2f)));
    enginetest::setNodePosition(s, crate, Vec3(0.3f, 0.0f, -8.0f));

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.gather = GiToggle::Off;
    gi.testBoundsMin = Vec3(-8.0f, -2.0f, -12.0f);
    gi.testBoundsMax = Vec3(8.0f, 8.0f, 6.0f);
    s->setGlobalIllumination(gi);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    fx.hdrReadback = true;
    view->setPostFx(fx);

    const auto shot = [&](bool mirrorArm) {
        s->setNodeVisible(wall, mirrorArm);
        if (mirrorArm) enginetest::testCameraLookAt(view, kCam, Vec3(0.0f, 1.0f, kMirrorZ));
        else enginetest::testCameraLookAt(view, kCamR, Vec3(0.0f, 1.0f, -20.0f));
        render(e, 40);
        ImageF img;
        view->readPixelsHdr(img);
        return img;
    };
    std::printf("\n== a STATIC crate's reflection, shaded from the voxels at the hit, against its raster ==\n");
    s->setNodeVisible(floorN, false);
    s->setNodeVisible(crate, false);
    const ImageF mNone = shot(true), rNone = shot(false);
    s->setNodeVisible(crate, true);
    const ImageF mOn = shot(true), rOn = shot(false);
    enginetest::dumpVoxelStore(s, "hitvoxel");
    if (const char *dir = std::getenv("JAH_HIT_DUMP")) {   // evidence: the two pictures, x4 radiance
        const ImageF *pics[2] = { &mOn, &rOn };
        const char *names[2] = { "/hit-voxel-mirror.ppm", "/hit-voxel-raster.ppm" };
        for (int k = 0; k < 2; ++k) {
            FILE *f = std::fopen((std::string(dir) + names[k]).c_str(), "wb");
            if (!f) continue;
            std::fprintf(f, "P6\n%u %u\n255\n", kSize, kSize);
            for (unsigned y = 0; y < kSize; ++y)
                for (unsigned x = 0; x < kSize; ++x) {
                    const Colour c = pics[k]->at(x, y);
                    const unsigned char px[3] = { (unsigned char)std::min(255.0f, c.r * 4.0f * 255.0f),
                                                  (unsigned char)std::min(255.0f, c.g * 4.0f * 255.0f),
                                                  (unsigned char)std::min(255.0f, c.b * 4.0f * 255.0f) };
                    std::fwrite(px, 1, 3, f);
                }
            std::fclose(f);
        }
    }
    const Mask refl = diffMask(mOn, mNone, false, 0.01f);
    const Mask rast = diffMask(rOn, rNone, true, 0.01f);
    Mask in;
    in.m.assign(refl.m.size(), 0u);
    for (size_t i = 0; i < refl.m.size(); ++i)
        if (refl.m[i] && rast.m[i]) { in.m[i] = 1u; ++in.n; }
    const Mask core = erode(in, kSize, kSize, 2);
    const Colour cr = maskMean(mOn, core, false), ca = maskMean(rOn, core, true);
    const float lr = 0.2126f * cr.r + 0.7152f * cr.g + 0.0722f * cr.b;
    const float la = 0.2126f * ca.r + 0.7152f * ca.g + 0.0722f * ca.b;
    std::printf("   reflection %u px, raster %u px, interior both %u px: reflection (%.4f %.4f %.4f) lum %.4f, raster "
                "(%.4f %.4f %.4f) lum %.4f -> %.3fx\n", refl.n, rast.n, core.n, cr.r, cr.g, cr.b, lr, ca.r, ca.g, ca.b,
                la, la > 0 ? lr / la : 0.0f);
    CHECK_MSG(core.n > 200, "the crate is seen in both pictures (%u px)", core.n);
    CHECK_MSG(la > 0.01f && std::fabs(lr / la - 1.0f) <= 0.05f,
              "THE STATIC CRATE'S REFLECTION, from the voxels at the hit, is its raster's radiance (%.4f / %.4f = "
              "%.3f; bar 5 %%)", lr, la, la > 0 ? lr / la : 0.0f);
    view->setScene(nullptr);
    e->destroyScene(s);
    e->destroyView(view);
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
