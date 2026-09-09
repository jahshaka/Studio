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

/// A camera-facing quad WITH UVS, which the shared unit-cube helper does not
/// have ("positions + per-face normals, no uvs", enginetesthelpers.h).
///
/// It exists because without UVs an HlmsUnlit datablock has no coordinate to
/// sample its texture at, so the displacement field would be CONSTANT over the
/// emitter — and a test that passes on a constant field is not a test that the
/// map is read at all. With UVs the ramp below displaces the left edge one way
/// and the right edge the other, which is a claim about the TEXTURE.
static MeshData uvQuadMesh()
{
    MeshData d;
    const float h = 0.5f;
    const float v[4][3] = { { -h, -h, 0.0f }, { h, -h, 0.0f }, { h, h, 0.0f }, { -h, h, 0.0f } };
    const float uv[4][2] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };
    for (int i = 0; i < 4; ++i) {
        d.positions.insert(d.positions.end(), { v[i][0], v[i][1], v[i][2] });
        d.normals.insert(d.normals.end(), { 0.0f, 0.0f, 1.0f });
        d.uvs.insert(d.uvs.end(), { uv[i][0], uv[i][1] });
    }
    d.indices = { 0, 1, 2, 0, 2, 3 };
    return d;
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
    const MeshId quadMesh = s->createMesh(uvQuadMesh());
    REQUIRE(quadMesh);
    const NodeId emitter = s->createNode();
    REQUIRE(emitter && s->attachMesh(emitter, quadMesh, distortMat));
    // A flat quad covering the middle of the frame, in FRONT of the wall.
    enginetest::setNodeScale(s, emitter, Vec3(1.6f, 1.6f, 1.0f));
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

        // THE MAP IS READ PER-TEXEL, not sampled once. The ramp displaces the
        // emitter's left edge one way and its right edge the other, so the two
        // halves of the warped region must BOTH have moved — a constant field
        // (which is what an emitter with no UVs would produce) moves the whole
        // region one way and would pass every other assertion here.
        unsigned leftIn = 0, leftOut = 0, rightIn = 0, rightOut = 0;
        diffInsideOutside(wallOnly, warped, 24, 24, 64, 104, leftIn, leftOut);
        diffInsideOutside(wallOnly, warped, 64, 24, 104, 104, rightIn, rightOut);
        CHECK_MSG(leftIn > 0 && rightIn > 0,
                  "the displacement MAP is sampled across the emitter, not once "
                  "(%u pixels moved in its left half, %u in its right)", leftIn, rightIn);

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

    // ---- 7. DISTORTION ON A BURST (POST_LOOKS_SPEC 4b, riders lane R3) -----
    // The same field, written by PARTICLES. A distortion emitter is a PFX2 def
    // in kDistortionParticleRenderQueue (221, PARTICLE_SYSTEM mode, inside the
    // distortion pass's [220, 222) range) carrying kDistortionBit and the
    // displacement datablock; an ordinary emitter is what it always was.
    //
    // What is asserted, in order: with the pass OFF a distortion burst draws
    // NOTHING (byte-identical to the wall alone — it is invisible to every
    // other pass); with the pass ON it warps the checker where the particles
    // are and contributes no colour; the SAME emitter flipped back to ordinary
    // draws its sprite as colour and is NOT in the field (its frame is
    // byte-identical with and without the pass). The particles are frozen with
    // the scene clock for every A/B, so two frames compare the same quads.
    {
        s->setNodeVisible(emitter, false);          // the quad is section 1-6's
        // Headless frames are ~1 ms of wall clock; a fixed step spawns a cloud in
        // 30 frames. (setParticleTimeScale CANCELS the fixed step — so the freeze
        // below is a time scale of 0, and the step is re-armed after it.)
        engine->setFixedFrameDelta(1.0f / 30.0f);

        ParticleSystemDesc pd;
        pd.quota = 512;
        pd.texture = map;                            // the ramp IS the displacement map
        pd.distortion = true;
        pd.additive = true;                          // ignored by a distortion emitter, on purpose
        ParticleEmitterDesc em;
        em.shape = ParticleEmitterShape::Box;
        // A 0.8 box of 0.5 quads: the cloud spans ~1.3 units, ~65 px of the
        // 128 px frame at z = -1, so the locality box below has a far field
        // to measure against (a 1.6 box of 0.8 quads covered the whole frame:
        // measured 3063 px changed inside the box, 1895 outside).
        em.extents = Vec3(0.8f, 0.8f, 0.05f);
        em.rate = 400.0f;
        em.velocityMin = em.velocityMax = 0.0f;      // a standing cloud, not a fountain
        em.ttlMin = em.ttlMax = 30.0f;
        em.sizeWidth = em.sizeHeight = 0.5f;
        pd.emitters.push_back(em);

        const NodeId burst = s->createNode();
        REQUIRE(burst);
        enginetest::setNodePosition(s, burst, Vec3(0.0f, 0.0f, -1.0f));
        REQUIRE(s->setParticleSystem(burst, pd));

        // Fill the box (30 frames at 400/s = a few hundred quads), then freeze.
        view->setPostFx(base);
        render(engine.get(), 30);
        engine->setParticleTimeScale(0.0f);
        render(engine.get(), 2);
        Image offBurst;
        REQUIRE(view->readPixels(offBurst));
        CHECK_MSG(s->particleCount(burst) > 100u,
                  "the burst is live (%u particles)", s->particleCount(burst));
        CHECK_MSG(pixelDiff(wallOnly, offBurst) == 0,
                  "a distortion BURST draws nothing without the pass: %u px differ from the wall",
                  pixelDiff(wallOnly, offBurst));

        PostFxDesc d = base;
        d.distortion = true;
        d.distortionStrength = 1.0f;
        view->setPostFx(d);
        render(engine.get(), 4);
        Image warpedBurst;
        REQUIRE(view->readPixels(warpedBurst));
        writePpm(warpedBurst, "distortion-burst.ppm");
        {
            const unsigned moved = pixelDiff(wallOnly, warpedBurst);
            CHECK_MSG(moved > (warpedBurst.width * warpedBurst.height) / 50,
                      "distortion_on_a_burst: the particles WARP the checker behind them "
                      "(%u of %u pixels moved)", moved, warpedBurst.width * warpedBurst.height);
            unsigned inside = 0, outside = 0;
            diffInsideOutside(wallOnly, warpedBurst, 20, 20, 108, 108, inside, outside);
            CHECK_MSG(inside > outside * 4,
                      "...and only where the cloud is: %u px changed inside its box, %u outside",
                      inside, outside);
            double wr, wg, wb, br, bg, bb;
            meanColour(wallOnly, wr, wg, wb);
            meanColour(warpedBurst, br, bg, bb);
            CHECK_MSG(std::abs(bb - wb) < 0.02 && std::abs(br - wr) < 0.05 && std::abs(bg - wg) < 0.05,
                      "the burst draws NO colour of its own (mean %.3f/%.3f/%.3f -> %.3f/%.3f/%.3f)",
                      wr, wg, wb, br, bg, bb);
        }

        // THE ORDINARY EMITTER DOES NOT. Same node, same map, distortion off:
        // the topology key changes ("|d"), the def is rebuilt as a plain
        // alpha-blended sprite emitter at RQ 15, and the field never sees it.
        pd.distortion = false;
        pd.additive = false;
        pd.alphaHash = false;
        REQUIRE(s->setParticleSystem(burst, pd));
        engine->setFixedFrameDelta(1.0f / 30.0f);
        render(engine.get(), 30);
        engine->setParticleTimeScale(0.0f);
        render(engine.get(), 2);
        Image ordinaryOn;
        REQUIRE(view->readPixels(ordinaryOn));
        view->setPostFx(base);
        render(engine.get(), 4);
        Image ordinaryOff;
        REQUIRE(view->readPixels(ordinaryOff));
        writePpm(ordinaryOff, "distortion-burst-ordinary.ppm");
        {
            double wr, wg, wb, orr, og, ob;
            meanColour(wallOnly, wr, wg, wb);
            meanColour(ordinaryOff, orr, og, ob);
            CHECK_MSG(pixelDiff(wallOnly, ordinaryOff) > (ordinaryOff.width * ordinaryOff.height) / 50 &&
                      ob > wb + 0.02,
                      "an ORDINARY emitter draws its sprite as colour (mean blue %.3f -> %.3f)",
                      wb, ob);
            CHECK_MSG(pixelDiff(ordinaryOn, ordinaryOff) == 0,
                      "...and is NOT in the field: byte-identical with and without the pass "
                      "(%u px differ)", pixelDiff(ordinaryOn, ordinaryOff));
        }
        engine->setParticleTimeScale(1.0f);           // wall clock, scale 1, fixed step gone
        CHECK(s->removeParticleSystem(burst), "the burst is removed");
        s->removeNode(burst);
        s->setNodeVisible(emitter, true);
        render(engine.get(), 2);
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
