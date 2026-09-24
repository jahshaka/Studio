// ssr.edge — THE RIM OF A SCREEN-SPACE REFLECTION (SSR-EDGE-1, PHOTON P3).
//
// THE DEFECT. Where the march's RANGE or its THICKNESS MARGIN runs out, a
// trusted hit becomes no hit across ONE STEP: the resolve lets a trusted hit
// win outright (SSR-2's mirror rule), so the edge of the accepted region is a
// pixel-sharp, speckled line where the screen's answer flips to the probe's
// (pictures-1's `refined` crop of the Showroom sphere, JUDGEMENT.md). The fix:
// a hit's weight FADES over the last steps of the range and across the
// thickness margin, so the envelope's edge hands over continuously.
//
// THE INSTRUMENT is the reflection's own WEIGHT — the alpha of the texture
// HlmsPbs composites (View::readReflectionHdr) — and the RIM METRIC is the
// count of pixels whose weight differs from a 4-neighbour's by more than 0.5:
// a continuous hand-over has none of them, a hard edge has one per boundary
// pixel, a speckled one more. It is read on two fixtures:
//
//   SPHERES   the pictures-1 subject rebuilt in the engine: two glossy
//             spheres a hand apart on a matte floor, the camera looking at one
//             of them so its rim reflects the other (the Showroom's SMOKE-41
//             pose in miniature; the Showroom itself is a Studio document).
//   FLOOR     tests/ssr's mirror floor under an emissive cube, where the
//             reflection's range end is the far edge of the reflected cube.
//
// RAYS OFF (JAHSHAKA_NO_RAY_QUERY in the row): the subject is the march and the
// resolve alone, and with rays off nothing in this picture depends on the frame
// index (the march's jitter is spatial), so the A/B's two arms are frozen by
// construction. `refined` march, full-resolution rays — the Showroom's row.
//
// JAH_SSR_EDGE_DUMP=1 writes each fixture's weight, picture and rim map as PPMs.
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
        std::printf((cond) ? "ok: " : "FAIL: ");                                \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!(cond)) ++failures;                                                \
    } while (0)

static bool envOn(const char *name)
{
    const char *v = std::getenv(name);
    return v && *v && *v != '0';
}

/// A UV sphere of radius 0.5, its seam WELDED by index reuse (the procedural
/// seam trap: a ring recomputed at 2 pi leaves a slit).
static MeshData sphereMesh(int rings = 48, int segments = 96)
{
    MeshData d;
    const float kPi = 3.14159265358979f;
    for (int r = 0; r <= rings; ++r) {
        const float v = float(r) / float(rings);
        const float th = v * kPi;
        for (int sgm = 0; sgm < segments; ++sgm) {
            const float ph = float(sgm) / float(segments) * 2.0f * kPi;
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = std::sin(th) * std::sin(ph);
            d.positions.insert(d.positions.end(), { 0.5f * x, 0.5f * y, 0.5f * z });
            d.normals.insert(d.normals.end(), { x, y, z });
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int sgm = 0; sgm < segments; ++sgm) {
            const unsigned a = unsigned(r * segments + sgm);
            const unsigned b = unsigned(r * segments + (sgm + 1) % segments);
            const unsigned c = a + unsigned(segments), e = b + unsigned(segments);
            d.indices.insert(d.indices.end(), { a, b, c, b, e, c });
        }
    return d;
}

static void writePpm(const ImageF &img, const std::string &path, bool alpha, float scale)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            const float v[3] = { alpha ? c.a : c.r, alpha ? c.a : c.g, alpha ? c.a : c.b };
            unsigned char rgb[3];
            for (int k = 0; k < 3; ++k) {
                const float s = std::min(1.0f, std::max(0.0f, v[k] * scale));
                rgb[k] = (unsigned char)(s * 255.0f + 0.5f);
            }
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
    std::printf("    wrote %s\n", path.c_str());
}

struct RimStats {
    unsigned receivers = 0;   ///< pixels of the reflecting surfaces, 2 px inside their silhouettes
    unsigned footprint = 0;   ///< ...with a weight above 0.02
    unsigned boundary = 0;    ///< ...on the envelope's edge (a receiver 4-neighbour differs by > 0.02)
    unsigned rim = 0;         ///< ...of which a receiver 4-neighbour differs by more than 0.5
};

/// THE RIM METRIC, on the reflecting surfaces only. The receiver's own
/// silhouette is an object edge (the weight is 1 on a mirror and 0 on the
/// matte floor beside it) and not the envelope's, so each reflector's mask is
/// eroded by two pixels — the resolve's 3x3 taps reach one ray texel across it
/// — and a pair counts only when BOTH pixels are inside the same reflector.
static RimStats rimStats(const ImageF &w, const std::vector<unsigned char> &receiver,
                         std::vector<unsigned char> *rimMask = nullptr)
{
    RimStats st;
    const unsigned W = w.width, H = w.height;
    std::vector<unsigned char> inner(receiver.size(), 0);
    for (unsigned y = 2; y + 2 < H; ++y)
        for (unsigned x = 2; x + 2 < W; ++x) {
            const unsigned char id = receiver[size_t(y) * W + x];
            bool all = id != 0;
            for (int dy = -2; dy <= 2 && all; ++dy)
                for (int dx = -2; dx <= 2 && all; ++dx)
                    all = receiver[size_t(y + dy) * W + (x + dx)] == id;
            inner[size_t(y) * W + x] = all ? 1 : 0;
        }
    const auto at = [&](unsigned x, unsigned y) { return w.at(x, y).a; };
    if (rimMask) rimMask->assign(receiver.size(), 0);
    for (unsigned y = 1; y + 1 < H; ++y)
        for (unsigned x = 1; x + 1 < W; ++x) {
            if (!inner[size_t(y) * W + x]) continue;
            ++st.receivers;
            const float c = at(x, y);
            if (c > 0.02f) ++st.footprint;
            float worst = 0.0f;
            const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
            for (int k = 0; k < 4; ++k) {
                const unsigned nx = unsigned(int(x) + dx[k]), ny = unsigned(int(y) + dy[k]);
                if (!inner[size_t(ny) * W + nx]) continue;
                worst = std::max(worst, std::fabs(c - at(nx, ny)));
            }
            if (worst > 0.02f) ++st.boundary;
            if (worst > 0.5f) {
                ++st.rim;
                if (rimMask) (*rimMask)[size_t(y) * W + x] = 1;
            }
        }
    return st;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-ssr-edge-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    const bool dump = envOn("JAH_SSR_EDGE_DUMP");

    const unsigned kW = 512u, kH = 288u;   // 16:9, as the Showroom's shot
    View *view = e->createOffscreenView("ssredge", kW, kH, Colour(0, 0, 0));
    if (!view) { std::printf("FAIL: view: %s\n", e->lastError().c_str()); return 1; }

    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;                // full-resolution rays: the Showroom's row
    fx.ssrMarchPhase = 1;      // `refined`: the document default since PICTURES-1
    fx.hdrReadback = true;

    RimStats results[2];
    const char *names[2] = { "spheres", "floor" };
    for (int fixture = 0; fixture < 2; ++fixture) {
        Scene *s = e->createScene(names[fixture]);
        view->setScene(s);
        view->setPostFx(fx);
        // A SKY, so a glossy surface the march does not answer shows the
        // environment (the probe/sky answer the rim hands over to) instead of
        // black: a two-texel gradient, bright blue above, warm grey below.
        {
            const unsigned char px[8] = { 150, 185, 235, 255, 95, 85, 75, 255 };
            SkyDesc sky;
            sky.mode = SkyMode::Equirectangular;
            sky.equirect = s->createTexture(1, 2, px, true);
            s->setSky(sky);
            float sh[27] = { 0.0f };
            bool ready = false;
            for (int f = 0; f < 20 && !ready; ++f) { e->renderOneFrame(); ready = s->skyAmbientSh(sh); }
            s->setAmbientSh(sh);
            s->setEnvironmentLight(Colour(1.0f, 1.0f, 1.0f, 1.0f));
        }
        const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
        // Every material, and whether it is a REFLECTOR (the mask pass paints
        // the reflectors white and everything else black).
        // Every material, and which REFLECTOR it is (0 = none): the mask pass
        // paints reflector k at 10 k and everything else black, so the metric
        // can tell one reflector from the next (an edge between two of them is
        // a silhouette too).
        struct Mat { MaterialId id; int receiver; };
        std::vector<Mat> mats;
        const auto mat = [&](Colour albedo, float metal, float rough, Colour emissive, int receiver) {
            PbrParams p;
            p.albedo = albedo;
            p.metalness = metal;
            p.roughness = rough;
            p.emissive = emissive;
            const MaterialId id = s->createPbrMaterial(p);
            mats.push_back({ id, receiver });
            return id;
        };
        if (fixture == 0) {
            // THE SPHERES. A matte grey floor, a warm back wall, two glossy
            // metal spheres of radius 1 (roughness 0.08), a sun. The camera is
            // in front of the near sphere and a little to its right: the floor
            // in front of both spheres fills their lower hemispheres through
            // the march, and those rays head TOWARD the camera, where one step
            // crosses much of the screen — the Showroom's own condition.
            const MeshId sphere = s->createMesh(sphereMesh());
            const NodeId floorN = s->createNode();
            s->attachMesh(floorN, cube, mat(Colour(0.45f, 0.45f, 0.45f), 0.0f, 0.9f, Colour(0, 0, 0), 0));
            s->setNodeTransform(floorN, Vec3(0.0f, -0.1f, 0.0f), Quat(), Vec3(30.0f, 0.2f, 30.0f));
            const NodeId wallN = s->createNode();
            s->attachMesh(wallN, cube, mat(Colour(0.7f, 0.35f, 0.2f), 0.0f, 0.8f, Colour(0, 0, 0), 0));
            s->setNodeTransform(wallN, Vec3(0.0f, 2.5f, -6.0f), Quat(), Vec3(30.0f, 5.0f, 0.4f));
            const Colour sphereCols[2] = { Colour(0.95f, 0.93f, 0.88f), Colour(0.2f, 0.45f, 0.9f) };
            const Vec3 at[2] = { Vec3(1.2f, 1.0f, 0.5f), Vec3(-1.4f, 1.0f, -2.0f) };
            for (int i = 0; i < 2; ++i) {
                const NodeId n = s->createNode();
                s->attachMesh(n, sphere, mat(sphereCols[i], 1.0f, 0.08f, Colour(0, 0, 0), i + 1));
                s->setNodeTransform(n, at[i], Quat(), Vec3(2.0f, 2.0f, 2.0f));
            }
            enginetest::addDirectionalLight(s, Vec3(-0.4f, -1.0f, -0.5f), 3.0f);
            enginetest::testCameraLookAt(view, Vec3(1.6f, 1.2f, 4.2f), Vec3(1.0f, 1.0f, 0.0f));
        } else {
            // THE FLOOR: tests/ssr's mirror floor under an emissive cube.
            const NodeId floorN = s->createNode();
            s->attachMesh(floorN, cube, mat(Colour(1.0f, 1.0f, 1.0f), 1.0f, 0.0f, Colour(0, 0, 0), 1));
            s->setNodeTransform(floorN, Vec3(0.0f, -0.1f, 0.0f), Quat(), Vec3(12.0f, 0.2f, 12.0f));
            const NodeId cubeN = s->createNode();
            s->attachMesh(cubeN, cube, mat(Colour(0.05f, 0.05f, 0.05f), 0.0f, 0.5f,
                                           Colour(3.0f, 0.0f, 0.0f), 0));
            s->setNodeTransform(cubeN, Vec3(0.0f, 2.2f, 0.0f), Quat(), Vec3(1.5f, 1.5f, 1.5f));
            enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);
            enginetest::testCameraLookAt(view, Vec3(0.0f, 1.4f, 7.0f), Vec3(0.0f, 0.6f, 0.0f));
        }
        for (int i = 0; i < 12; ++i) e->renderOneFrame();   // the history is a frame late
        ImageF refl, pic;
        CHECK_MSG(view->readReflectionHdr(refl) && refl.width == kW,
                  "%s: the reflection's weight reads back", names[fixture]);
        view->readPixelsHdr(pic);

        // THE RECEIVER MASK: the same frame with reflector k emitting 10 k and
        // everything else black, no SSR.
        std::vector<unsigned char> receiver(size_t(kW) * kH, 0);
        {
            PostFxDesc plain = fx;
            plain.ssr = 0;
            view->setPostFx(plain);
            for (const Mat &m : mats) {
                PbrParams p;
                p.albedo = Colour(0, 0, 0);
                p.workflow = PbrParams::Workflow::Specular;
                p.ior = 1.0f;
                p.specularColour = Colour(0, 0, 0);
                const float k = 10.0f * float(m.receiver);
                p.emissive = Colour(k, k, k);
                s->setPbrMaterial(m.id, p);
            }
            for (int i = 0; i < 4; ++i) e->renderOneFrame();
            ImageF maskImg;
            view->readPixelsHdr(maskImg);
            for (unsigned y = 0; y < kH; ++y)
                for (unsigned x = 0; x < kW; ++x)
                    receiver[size_t(y) * kW + x] =
                        (unsigned char)std::lround(std::max(0.0f, maskImg.at(x, y).g) / 10.0f);
            view->setPostFx(fx);
        }

        std::vector<unsigned char> rimMask;
        const RimStats st = rimStats(refl, receiver, &rimMask);
        results[fixture] = st;
        std::printf("   %-8s receivers %6u px | footprint %6u px | envelope boundary %5u px | RIM "
                    "(|dw| > 0.5) %5u px (%.1f %% of the boundary)\n",
                    names[fixture], st.receivers, st.footprint, st.boundary, st.rim,
                    st.boundary ? 100.0 * double(st.rim) / double(st.boundary) : 0.0);
        if (dump) {
            writePpm(refl, std::string("ssr-edge-") + names[fixture] + "-weight.ppm", true, 1.0f);
            writePpm(pic, std::string("ssr-edge-") + names[fixture] + "-picture.ppm", false, 1.0f);
            // The rim pixels in red over the weight (grey) and the receivers (dim blue).
            FILE *f = std::fopen((std::string("ssr-edge-") + names[fixture] + "-rim.ppm").c_str(), "wb");
            if (f) {
                std::fprintf(f, "P6\n%u %u\n255\n", kW, kH);
                for (unsigned y = 0; y < kH; ++y)
                    for (unsigned x = 0; x < kW; ++x) {
                        const size_t i = size_t(y) * kW + x;
                        const unsigned char g = (unsigned char)(std::min(1.0f, std::max(0.0f, refl.at(x, y).a)) * 200.0f);
                        unsigned char rgb[3] = { g, g, (unsigned char)std::min(255, g + (receiver[i] ? 40 : 0)) };
                        if (rimMask[i]) { rgb[0] = 255; rgb[1] = 0; rgb[2] = 0; }
                        std::fwrite(rgb, 1, 3, f);
                    }
                std::fclose(f);
            }
        }
        CHECK_MSG(st.footprint > 500u, "%s: the fixture HAS a reflection to measure (%u px)",
                  names[fixture], st.footprint);
        view->setScene(nullptr);
        e->destroyScene(s);
    }

    // THE BARS, from the A/B on this rig (RTX 4080 SUPER, 2026-09-24; the base
    // media against SSR-EDGE-1's, same binary, same frames, rays off):
    //
    //                 rim before -> after     of the envelope boundary
    //   spheres       143 -> 127 px (-11 %)   4.0 % -> 3.7 %
    //   floor         308 -> 136 px (-56 %)   49.3 % -> 14.8 %
    //
    // The FLOOR's reflected cube lost its hard top edge (the thickness margin,
    // a fade now) and keeps its two vertical sides: rays passing beside the cube
    // continue into the sky, which has no depth — a miss beside a hit whose gap
    // is zero, the reflected OBJECT's silhouette, not the envelope's end. The
    // SPHERES lost the toothed end of the floor's reflection (the range end: the
    // teeth were the checkerboard's two phases either side of the screen exit)
    // and keep two things no fade of the range or the thickness can see: the
    // near sphere OCCLUDING the floor the far one reflects (the gap there is the
    // occluder's, rejected outright — an occlusion edge in screen space), and
    // the limbs, where neighbouring pixels' rays diverge by more than the fade
    // is wide. Each bar is the measured value plus 10 %, and each refuses the
    // base media outright (143 > 140, 308 > 150).
    CHECK_MSG(results[0].rim <= 140u,
              "spheres: THE RANGE END HANDS OVER — %u rim pixels (bar 140; the base media's 143)",
              results[0].rim);
    CHECK_MSG(results[1].rim <= 150u,
              "floor: THE THICKNESS MARGIN HANDS OVER — %u rim pixels (bar 150; the base media's 308)",
              results[1].rim);

    e->destroyView(view);
    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
