// THE DISTORTION PASS, pixel-asserted (SPECS/POST_LOOKS_SPEC.md §5).
//
// An object with a Distortion material draws NOTHING of itself: it writes a
// screen-space displacement field into its own target through its own render
// queue, and a compose quad then warps the scene image behind it. Heat haze, a
// blast wave, a shock ring.
//
// THE FIXTURE. A high-contrast checkerboard wall of emissive cubes fills the
// frame; a single flat quad with a Distortion material stands in front of it,
// covering the middle. Emissive so no light, shadow or ambient term can move a
// pixel; a checkerboard because a WARP is only visible where there is something
// to displace — over a flat colour a perfect warp is invisible, which is a
// property of the effect and not a defect in it.
//
// WHAT EACH SECTION PROVES
//
//   1. STRENGTH 0 IS BYTE-IDENTICAL. The emitter is in the scene, the pass is
//      in the graph, and the frame is bit for bit the frame with distortion
//      off — because the field decodes to a zero offset and the compose quad's
//      fetch lands on the source texel.
//   2. IT WARPS THE BACKGROUND. At full strength the checkerboard behind the
//      emitter MOVES: many pixels change, and they change WHERE THE EMITTER IS
//      and nowhere else. The second half is what separates a warp from a global
//      filter.
//   3. THE EMITTER DRAWS NO COLOUR OF ITS OWN. Its displacement map is pale
//      blue; if it were ever drawn as colour the frame would be a smear of it.
//      Asserted as: the warped frame's colours are drawn FROM the background's
//      palette (its mean is close to the undistorted mean), which a pale-blue
//      overlay would break immediately.
//   4. IT IS OCCLUDED BY OPAQUE GEOMETRY. Put a wall between the camera and the
//      emitter and the frame is byte-identical to the wall alone: the
//      distortion pass borrows the scene's depth buffer, so haze behind a wall
//      does not warp the wall.
//   5. IT NEVER DRAWS IN THE PASSTHROUGH SHAPE. With the whole post chain off
//      — which is EVERY offscreen view in this application: thumbnails,
//      previews, material previews, every other pixel suite — a scene
//      containing a distortion emitter renders byte-identically to the same
//      scene without one. That is the negative the visibility-bit design
//      exists to make true, and it is the assertion that protects the rest of
//      the test tree from this feature.
//   6. THE SHAPE/UNIFORM SPLIT. Turning distortion on or off rebuilds the
//      workspace; changing the strength does not.
//
// JAH_DISTORTION_DUMP=1 writes a .ppm per measured frame beside the binary.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
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
        std::printf(cond ? "ok: " : "FAIL: ");                                  \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!(cond)) ++failures;                                                \
    } while (0)
#define REQUIRE(cond)                                                           \
    do { if (!(cond)) { std::printf("FAIL: precondition " #cond "\n"); return 1; } } while (0)

static bool envOn(const char *name)
{
    const char *v = std::getenv(name);
    return v && *v && *v != '0';
}

static void writePpm(const Image &img, const char *path)
{
    if (!envOn("JAH_DISTORTION_DUMP")) return;
    FILE *f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            const unsigned char rgb[3] = {
                (unsigned char)(c.r < 0 ? 0 : (c.r > 1 ? 255 : c.r * 255.0f + 0.5f)),
                (unsigned char)(c.g < 0 ? 0 : (c.g > 1 ? 255 : c.g * 255.0f + 0.5f)),
                (unsigned char)(c.b < 0 ? 0 : (c.b > 1 ? 255 : c.b * 255.0f + 0.5f)) };
            std::fwrite(rgb, 1, 3, f);
        }
    std::fclose(f);
    std::printf("    wrote %s\n", path);
}

static void render(Engine *e, int frames = 3)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static unsigned pixelDiff(const Image &a, const Image &b)
{
    if (a.width != b.width || a.height != b.height) return 0xFFFFFFFFu;
    unsigned diff = 0;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour p = a.at(x, y), q = b.at(x, y);
            if (p.r != q.r || p.g != q.g || p.b != q.b) ++diff;
        }
    return diff;
}

/// How many pixels differ inside a rectangle, and how many outside it.
static void diffInsideOutside(const Image &a, const Image &b,
                              unsigned x0, unsigned y0, unsigned x1, unsigned y1,
                              unsigned &inside, unsigned &outside)
{
    inside = outside = 0;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour p = a.at(x, y), q = b.at(x, y);
            if (p.r == q.r && p.g == q.g && p.b == q.b) continue;
            if (x >= x0 && x < x1 && y >= y0 && y < y1) ++inside; else ++outside;
        }
}

static void meanColour(const Image &img, double &r, double &g, double &b)
{
    r = g = b = 0.0;
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            r += c.r; g += c.g; b += c.b;
        }
    const double n = double(img.width) * double(img.height);
    r /= n; g /= n; b /= n;
}

/// A tangent-space normal map with a strong horizontal gradient: R sweeps from
/// 0 to 1 across the tile, so the decoded x offset sweeps from -1 to +1. That
/// is a displacement field with an unmistakable direction, which is what makes
/// "the background moved" a statement about the FIELD and not about noise.
static std::vector<unsigned char> rampNormalMap(unsigned size)
{
    std::vector<unsigned char> px(size_t(size) * size * 4);
    for (unsigned y = 0; y < size; ++y)
        for (unsigned x = 0; x < size; ++x) {
            const size_t i = (size_t(y) * size + x) * 4;
            px[i + 0] = (unsigned char)((x * 255u) / (size - 1));   // R: -1 .. +1
            px[i + 1] = 128;                                        // G: no y offset
            px[i + 2] = 255;
            px[i + 3] = 255;
        }
    return px;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-distortion-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("distort", 128, 128, Colour(0, 0, 0));
    Scene *s = engine->createScene("distort");
    REQUIRE(view && s);
    view->setScene(s);
    s->setAmbient(Colour(0.0f, 0.0f, 0.0f), Colour(0.0f, 0.0f, 0.0f));

    const MeshId cubeMesh = s->createMesh(enginetest::unitCubeMesh());
    REQUIRE(cubeMesh);

    // THE CHECKERBOARD WALL, as emissive cubes at z = -4. A warp needs contrast
    // to be visible at all, and this gives every pixel a neighbour unlike it.
    {
        MaterialId light = 0, dark = 0;
        {
            PbrParams p; p.albedo = Colour(0, 0, 0); p.emissive = Colour(0.9f, 0.9f, 0.9f);
            light = s->createPbrMaterial(p);
        }
        {
            PbrParams p; p.albedo = Colour(0, 0, 0); p.emissive = Colour(0.05f, 0.05f, 0.05f);
            dark = s->createPbrMaterial(p);
        }
        REQUIRE(light && dark);
        for (int gy = -5; gy <= 5; ++gy)
            for (int gx = -5; gx <= 5; ++gx) {
                const NodeId n = s->createNode();
                REQUIRE(n && s->attachMesh(n, cubeMesh, ((gx + gy) & 1) ? light : dark));
                enginetest::setNodeScale(s, n, Vec3(0.5f, 0.5f, 0.1f));
                enginetest::setNodePosition(s, n, Vec3(float(gx) * 0.5f, float(gy) * 0.5f, -4.0f));
            }
    }
    enginetest::testCameraLookAt(view, Vec3(0.0f, 0.0f, 2.0f), Vec3(0.0f, 0.0f, -4.0f));

    // ---- the baseline: the wall alone, no emitter, no chain ----------------
    render(engine.get(), 4);
    Image wallOnly;
    REQUIRE(view->readPixels(wallOnly));
    writePpm(wallOnly, "distortion-wall.ppm");
    const unsigned genPlain = view->workspaceGeneration();

    // ---- the emitter: a flat quad with a Distortion material ---------------
    const TextureId map = [&] {
        const auto px = rampNormalMap(64);
        return s->createTexture(64, 64, px.data(), /*srgb*/ false);
    }();
    REQUIRE(map);

    MaterialId distortMat = 0;
    {
        PbrParams p;
        p.shadingModel = ShadingModel::Distortion;
        p.alpha = 1.0f;                 // the material's own strength
        p.twoSided = true;
        distortMat = s->createPbrMaterial(p);
        REQUIRE(distortMat);
        REQUIRE(s->setPbrTexture(distortMat, PbrTextureSlot::Normal, map));
    }
    const NodeId emitter = s->createNode();
    REQUIRE(emitter && s->attachMesh(emitter, cubeMesh, distortMat));
    // A flat slab covering the middle third of the frame, in FRONT of the wall.
    enginetest::setNodeScale(s, emitter, Vec3(1.2f, 1.2f, 0.05f));
    enginetest::setNodePosition(s, emitter, Vec3(0.0f, 0.0f, -1.0f));

    // ---- 5. THE PASSTHROUGH NEGATIVE ---------------------------------------
    // The emitter is in the scene and the chain is OFF — which is every
    // offscreen view in this application. The frame must be byte-identical to
    // the wall alone.
    render(engine.get(), 4);
    Image passthrough;
    REQUIRE(view->readPixels(passthrough));
    writePpm(passthrough, "distortion-passthrough.ppm");
    CHECK_MSG(view->workspaceGeneration() == genPlain,
              "adding a distortion emitter does not rebuild the passthrough workspace");
    CHECK_MSG(pixelDiff(wallOnly, passthrough) == 0,
              "distortion_item_never_draws_in_the_passthrough_shape: %u pixels differ",
              pixelDiff(wallOnly, passthrough));

    // ---- 1. STRENGTH 0 IS BYTE-IDENTICAL -----------------------------------
    PostFxDesc base;
    base.allowOffscreen = true;   // the deliberate door; see tests/looks' header
    {
        PostFxDesc d = base;
        d.distortion = true;
        d.distortionStrength = 0.0f;
        view->setPostFx(d);
        const unsigned genOn = view->workspaceGeneration();
        CHECK_MSG(genOn != genPlain,
                  "turning distortion on IS a shape change (%u -> %u)", genPlain, genOn);
        render(engine.get(), 4);
        Image zero;
        REQUIRE(view->readPixels(zero));
        writePpm(zero, "distortion-strength0.ppm");
        CHECK_MSG(pixelDiff(wallOnly, zero) == 0,
                  "strength 0 is byte-identical to no distortion: %u pixels differ",
                  pixelDiff(wallOnly, zero));

        // ---- 6. the uniform half of the split ------------------------------
        PostFxDesc up = d;
        up.distortionStrength = 1.0f;
        view->setPostFx(up);
        CHECK_MSG(view->workspaceGeneration() == genOn,
                  "changing the STRENGTH must not rebuild the workspace (%u -> %u)",
                  genOn, view->workspaceGeneration());
    }

    // ---- 2 + 3. it warps the background, where the emitter is --------------
    {
        render(engine.get(), 4);
        Image warped;
        REQUIRE(view->readPixels(warped));
        writePpm(warped, "distortion-warped.ppm");

        const unsigned moved = pixelDiff(wallOnly, warped);
        CHECK_MSG(moved > (warped.width * warped.height) / 50,
                  "distortion_warps_the_background: %u of %u pixels moved",
                  moved, warped.width * warped.height);

        // ...and only where the emitter is. The slab covers roughly the middle
        // half of a 128x128 frame; the box is generous on purpose (the warp
        // fetches from OUTSIDE the emitter's own silhouette, which is the whole
        // point), and the assertion is about the FAR field being untouched.
        unsigned inside = 0, outside = 0;
        diffInsideOutside(wallOnly, warped, 24, 24, 104, 104, inside, outside);
        CHECK_MSG(inside > outside * 4,
                  "the warp is LOCAL to the emitter: %u pixels changed inside it, %u outside",
                  inside, outside);

        // 3. the emitter contributed no colour of its own: the frame's mean is
        // still the wall's. A displacement map drawn as colour (pale blue,
        // ~(0.5, 0.5, 1.0)) over a quarter of the frame would move the blue
        // mean by ~0.1 — two orders of magnitude more than resampling does.
        double wr, wg, wb, dr, dg, db;
        meanColour(wallOnly, wr, wg, wb);
        meanColour(warped, dr, dg, db);
        CHECK_MSG(std::abs(db - wb) < 0.02,
                  "the emitter draws NO colour of its own: mean blue %.4f -> %.4f", wb, db);
        CHECK_MSG(std::abs(dr - wr) < 0.05 && std::abs(dg - wg) < 0.05,
                  "...and the warp resamples rather than regrades (%.4f/%.4f -> %.4f/%.4f)",
                  wr, wg, dr, dg);
    }

    // ---- 4. it is occluded by opaque geometry ------------------------------
    {
        // A solid emissive slab BETWEEN the camera and the emitter. The
        // distortion pass depth-tests against the opaque scene, so the emitter
        // is entirely hidden and the frame must equal the same scene with
        // distortion off.
        const NodeId occluder = s->createNode();
        PbrParams p; p.albedo = Colour(0, 0, 0); p.emissive = Colour(0.4f, 0.2f, 0.1f);
        const MaterialId occMat = s->createPbrMaterial(p);
        REQUIRE(occluder && occMat && s->attachMesh(occluder, cubeMesh, occMat));
        enginetest::setNodeScale(s, occluder, Vec3(4.0f, 4.0f, 0.1f));
        enginetest::setNodePosition(s, occluder, Vec3(0.0f, 0.0f, 0.0f));

        render(engine.get(), 4);
        Image occludedWarp;
        REQUIRE(view->readPixels(occludedWarp));
        writePpm(occludedWarp, "distortion-occluded.ppm");

        PostFxDesc off = base;
        view->setPostFx(off);
        render(engine.get(), 4);
        Image occludedPlain;
        REQUIRE(view->readPixels(occludedPlain));
        CHECK_MSG(pixelDiff(occludedPlain, occludedWarp) == 0,
                  "distortion_is_occluded_by_opaques: %u pixels differ",
                  pixelDiff(occludedPlain, occludedWarp));
        s->removeNode(occluder);
    }

    // ---- and it composes with the rest of the chain -------------------------
    // Distortion runs in LINEAR HDR before the tonemap, so HDR + distortion is
    // the ordering claim §5.3 makes; the assertion is that it still warps.
    {
        PostFxDesc d = base;
        d.hdr = true;
        d.tonemapFixed = true;      // deterministic: no auto-exposure to settle
        view->setPostFx(d);
        render(engine.get(), 5);
        Image gradedPlain;
        REQUIRE(view->readPixels(gradedPlain));

        d.distortion = true;
        d.distortionStrength = 1.0f;
        view->setPostFx(d);
        render(engine.get(), 5);
        Image gradedWarp;
        REQUIRE(view->readPixels(gradedWarp));
        writePpm(gradedWarp, "distortion-hdr.ppm");
        CHECK_MSG(pixelDiff(gradedPlain, gradedWarp) > (gradedWarp.width * gradedWarp.height) / 50,
                  "distortion composes with the tonemapped chain (%u pixels)",
                  pixelDiff(gradedPlain, gradedWarp));
        view->setPostFx(base);
        render(engine.get(), 2);
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
