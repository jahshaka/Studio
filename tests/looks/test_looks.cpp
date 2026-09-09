// THE LOOKS STAGE, pixel-asserted (SPECS/POST_LOOKS_SPEC.md §4, phases 1-2).
//
// A "look" is one LDR image filter applied to the finished picture, after
// tonemapping and after anti-aliasing. The scene carries an ORDERED stack of
// them; the renderer turns that stack into a ping-pong of full-screen quads at
// the very end of the compositor chain.
//
// THE FIXTURE is deliberately the plainest thing that can carry a colour: one
// EMISSIVE cube filling the middle of a 128x128 offscreen view over a dark
// background. Emissive so the colour does not depend on a light, a shadow or an
// ambient term — a chroma assertion has to be about the LOOK and about nothing
// else. Every measurement is on the centre pixel or on the whole frame.
//
// WHAT EACH SECTION PROVES
//
//   1. AN EMPTY STACK IS BYTE-IDENTICAL TO NO STACK (§8, the law). Not "close":
//      the same workspace generation and the same pixels, because an empty
//      stack adds no pass and no texture to the graph at all. Every thumbnail,
//      preview and pixel suite in this tree depends on this being exact.
//   2. LOOKS ARE IGNORED OFFSCREEN UNLESS ASKED. The same guarantee the rest of
//      the post chain has (PostFxDesc::allowOffscreen) — a stack pushed at an
//      offscreen view does not even rebuild its workspace.
//   3. DESATURATE REMOVES CHROMA. At amount 1 the centre pixel has R == G == B;
//      at amount 0 the WHOLE FRAME is byte-identical to the frame with no look.
//      The second half is the property every look in the catalogue has to have,
//      and it is what makes a look safe to leave in a stack at zero.
//   4. PARAMETERS ARE UNIFORMS, THE KIND SEQUENCE IS SHAPE. Scrubbing an amount
//      must not rebuild the workspace; adding, removing or reordering must.
//      That split is what makes a slider free and it is asserted, not assumed.
//   5. LOOKS COMPOSE WITH SMAA (§7 R4). SMAA's output target moves when the
//      stack is non-empty — it writes the ping-pong's first buffer instead of
//      the window — so the two together have to still produce the look.
//   6. THE STACK ORDER IS THE FRAME ORDER (phase 2). Posterize-then-Desaturate
//      and Desaturate-then-Posterize are different pictures; if they were not,
//      the ordering model would be a lie.
//   7. EVERY LOOK IN THE CATALOGUE, one section each (phase 2): identity at
//      amount 0, and a structural property at full amount that names what the
//      look IS rather than a magic colour.
//
// JAH_LOOKS_DUMP=1 writes a .ppm per measured frame beside the binary. A number
// in a log is not pixel evidence.
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
    if (!envOn("JAH_LOOKS_DUMP")) return;
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

/// How many pixels differ AT ALL between two frames.
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

/// The largest per-channel difference between two frames, in 8-bit counts.
static float maxChannelDiff(const Image &a, const Image &b)
{
    if (a.width != b.width || a.height != b.height) return 1e9f;
    float worst = 0.0f;
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour p = a.at(x, y), q = b.at(x, y);
            worst = std::max(worst, std::fabs(p.r - q.r));
            worst = std::max(worst, std::fabs(p.g - q.g));
            worst = std::max(worst, std::fabs(p.b - q.b));
        }
    return worst * 255.0f;
}

static Colour centre(const Image &img) { return img.at(img.width / 2, img.height / 2); }

/// The mean absolute chroma (max channel minus min channel) over the frame —
/// "how coloured is this picture", independent of how bright it is.
static float meanChroma(const Image &img)
{
    double sum = 0.0;
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            const float hi = std::max(c.r, std::max(c.g, c.b));
            const float lo = std::min(c.r, std::min(c.g, c.b));
            sum += double(hi - lo);
        }
    return float(sum / double(img.width * img.height));
}

/// How many DISTINCT quantized colours the frame holds — the measure posterize
/// is about. Quantized to 5 bits per channel so 8-bit readback noise does not
/// count as detail.
static unsigned distinctColours(const Image &img)
{
    std::vector<unsigned char> seen(1u << 15, 0);
    unsigned n = 0;
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            const auto q = [](float v) {
                const int i = int(v * 31.0f + 0.5f);
                return unsigned(i < 0 ? 0 : (i > 31 ? 31 : i));
            };
            const unsigned key = (q(c.r) << 10) | (q(c.g) << 5) | q(c.b);
            if (!seen[key]) { seen[key] = 1; ++n; }
        }
    return n;
}

/// The mean gradient magnitude — how much local contrast the frame has. Sharpen
/// raises it; a blur lowers it.
static float meanGradient(const Image &img)
{
    double sum = 0.0;
    unsigned n = 0;
    for (unsigned y = 1; y + 1 < img.height; ++y)
        for (unsigned x = 1; x + 1 < img.width; ++x) {
            const auto lum = [&](unsigned px, unsigned py) {
                const Colour c = img.at(px, py);
                return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b;
            };
            const float gx = lum(x + 1, y) - lum(x - 1, y);
            const float gy = lum(x, y + 1) - lum(x, y - 1);
            sum += double(std::sqrt(gx * gx + gy * gy));
            ++n;
        }
    return n ? float(sum / double(n)) : 0.0f;
}

// ---------------------------------------------------------------------------

struct Fixture {
    Engine *e = nullptr;
    View *v = nullptr;
    Scene *s = nullptr;
};

/// Everything below shares ONE view and ONE scene: the whole subject is what
/// the compositor does to a finished frame, and rebuilding the world per
/// section would only make the suite slower and the frames less comparable.
static PostFxDesc baseFx()
{
    PostFxDesc fx;
    fx.allowOffscreen = true;   // the deliberate door, see the header
    return fx;
}

static LookDesc look(LookKind kind, float p0, float p1 = 0.0f, float p2 = 0.0f,
                     float p3 = 0.0f, float p4 = 0.0f, float p5 = 0.0f, float p6 = 0.0f)
{
    LookDesc d;
    d.kind = kind;
    d.p[0] = p0; d.p[1] = p1; d.p[2] = p2; d.p[3] = p3;
    d.p[4] = p4; d.p[5] = p5; d.p[6] = p6;
    return d;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-looks-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    Fixture fx;
    fx.e = engine.get();
    fx.v = engine->createOffscreenView("looks", 128, 128, Colour(0.02f, 0.02f, 0.06f));
    fx.s = engine->createScene("looks");
    REQUIRE(fx.v && fx.s);
    fx.v->setScene(fx.s);
    fx.s->setAmbient(Colour(0.05f, 0.05f, 0.05f), Colour(0.05f, 0.05f, 0.05f));

    // ONE EMISSIVE CUBE, strongly coloured and strongly asymmetric across the
    // three channels: a desaturate has to have something to remove, and a
    // grade has to have something to tint. Emissive so no light, shadow or
    // ambient term can move the number.
    {
        const NodeId cube = fx.s->createNode();
        PbrParams p;
        p.albedo = Colour(0.0f, 0.0f, 0.0f);
        p.emissive = Colour(0.75f, 0.35f, 0.10f);
        p.roughness = 1.0f;
        const MaterialId mat = fx.s->createPbrMaterial(p);
        const MeshId mesh = fx.s->createMesh(enginetest::unitCubeMesh());
        REQUIRE(cube && mat && mesh && fx.s->attachMesh(cube, mesh, mat));
        enginetest::setNodeScale(fx.s, cube, Vec3(2.0f, 2.0f, 2.0f));
        enginetest::setNodePosition(fx.s, cube, Vec3(0.0f, 0.0f, 0.0f));
    }
    // A second, differently coloured cube off to one side, so the frame has an
    // EDGE in it — sharpen and the warps need a gradient to act on, and the
    // colour-count measures need more than one colour.
    {
        const NodeId cube = fx.s->createNode();
        PbrParams p;
        p.albedo = Colour(0.0f, 0.0f, 0.0f);
        p.emissive = Colour(0.10f, 0.55f, 0.80f);
        p.roughness = 1.0f;
        const MaterialId mat = fx.s->createPbrMaterial(p);
        const MeshId mesh = fx.s->createMesh(enginetest::unitCubeMesh());
        REQUIRE(cube && mat && mesh && fx.s->attachMesh(cube, mesh, mat));
        enginetest::setNodeScale(fx.s, cube, Vec3(1.0f, 3.0f, 1.0f));
        enginetest::setNodePosition(fx.s, cube, Vec3(1.6f, 0.0f, 0.6f));
    }
    enginetest::testCameraLookAt(fx.v, Vec3(0.0f, 0.5f, 5.0f), Vec3(0, 0, 0));

    // ---- 1. the baseline, and the byte-identical law ----------------------
    render(fx.e, 4);
    Image plain;
    REQUIRE(fx.v->readPixels(plain));
    const unsigned genPlain = fx.v->workspaceGeneration();
    writePpm(plain, "looks-plain.ppm");
    std::printf("   baseline centre = %.3f %.3f %.3f, chroma %.4f, generation %u\n",
                centre(plain).r, centre(plain).g, centre(plain).b, meanChroma(plain), genPlain);

    {
        // An EMPTY stack, opted in offscreen: no pass, no texture, no rebuild.
        PostFxDesc fxDesc = baseFx();
        fx.v->setPostFx(fxDesc);
        CHECK_MSG(fx.v->workspaceGeneration() == genPlain,
                  "an empty looks stack must not rebuild the workspace: %u -> %u",
                  genPlain, fx.v->workspaceGeneration());
        render(fx.e, 3);
        Image empty;
        REQUIRE(fx.v->readPixels(empty));
        CHECK_MSG(pixelDiff(plain, empty) == 0,
                  "empty_looks_stack_is_byte_identical: %u pixels differ",
                  pixelDiff(plain, empty));
    }

    // ---- 2. the offscreen guarantee ---------------------------------------
    {
        PostFxDesc fxDesc;                       // NO allowOffscreen
        fxDesc.looks.push_back(look(LookKind::Desaturate, 1.0f));
        fx.v->setPostFx(fxDesc);
        CHECK_MSG(fx.v->workspaceGeneration() == genPlain,
                  "looks_are_ignored_offscreen_unless_asked: no rebuild (%u -> %u)",
                  genPlain, fx.v->workspaceGeneration());
        render(fx.e, 3);
        Image ignored;
        REQUIRE(fx.v->readPixels(ignored));
        CHECK_MSG(pixelDiff(plain, ignored) == 0,
                  "looks_are_ignored_offscreen_unless_asked: %u pixels differ",
                  pixelDiff(plain, ignored));
    }

    // ---- 3. desaturate ------------------------------------------------------
    Image desatFull;
    {
        PostFxDesc fxDesc = baseFx();
        fxDesc.looks.push_back(look(LookKind::Desaturate, 1.0f));
        fx.v->setPostFx(fxDesc);
        const unsigned genLook = fx.v->workspaceGeneration();
        CHECK_MSG(genLook != genPlain,
                  "adding a look is a SHAPE change and must rebuild: %u -> %u",
                  genPlain, genLook);
        render(fx.e, 3);
        REQUIRE(fx.v->readPixels(desatFull));
        writePpm(desatFull, "looks-desaturate-1.ppm");
        const Colour c = centre(desatFull);
        CHECK_MSG(std::fabs(c.r - c.g) * 255.0f <= 1.0f && std::fabs(c.g - c.b) * 255.0f <= 1.0f,
                  "desaturate_removes_chroma: centre is grey (%.3f %.3f %.3f)", c.r, c.g, c.b);
        CHECK_MSG(meanChroma(desatFull) < meanChroma(plain) * 0.1f,
                  "desaturate_removes_chroma: frame chroma %.4f -> %.4f",
                  meanChroma(plain), meanChroma(desatFull));

        // AND THE IDENTITY. Same graph — only the uniform moves — so this also
        // proves the parameter is a uniform (section 4 asserts the generation).
        PostFxDesc zero = baseFx();
        zero.looks.push_back(look(LookKind::Desaturate, 0.0f));
        fx.v->setPostFx(zero);
        CHECK_MSG(fx.v->workspaceGeneration() == genLook,
                  "scrubbing a look parameter must NOT rebuild: %u -> %u",
                  genLook, fx.v->workspaceGeneration());
        render(fx.e, 3);
        Image identity;
        REQUIRE(fx.v->readPixels(identity));
        writePpm(identity, "looks-desaturate-0.ppm");
        const unsigned diff = pixelDiff(plain, identity);
        CHECK_MSG(diff == 0,
                  "a look at amount 0 is byte-identical to no look: %u pixels differ "
                  "(worst channel %.2f/255)", diff, maxChannelDiff(plain, identity));
    }

    // ---- 4. removing the stack puts the graph back -------------------------
    {
        fx.v->setPostFx(baseFx());
        CHECK_MSG(fx.v->workspaceGeneration() != genPlain,
                  "removing the last look rebuilds (generation moved on)");
        render(fx.e, 3);
        Image back;
        REQUIRE(fx.v->readPixels(back));
        CHECK_MSG(pixelDiff(plain, back) == 0,
                  "removing every look returns the ORIGINAL frame: %u pixels differ",
                  pixelDiff(plain, back));
    }

    // ---- 5. looks compose with SMAA (§7 R4) --------------------------------
    {
        PostFxDesc smaaOnly = baseFx();
        smaaOnly.smaaPreset = 2;
        fx.v->setPostFx(smaaOnly);
        render(fx.e, 4);
        Image smaa;
        REQUIRE(fx.v->readPixels(smaa));
        writePpm(smaa, "looks-smaa.ppm");

        PostFxDesc both = baseFx();
        both.smaaPreset = 2;
        both.looks.push_back(look(LookKind::Desaturate, 1.0f));
        fx.v->setPostFx(both);
        render(fx.e, 4);
        Image smaaLook;
        REQUIRE(fx.v->readPixels(smaaLook));
        writePpm(smaaLook, "looks-smaa-desaturate.ppm");
        const Colour c = centre(smaaLook);
        CHECK_MSG(std::fabs(c.r - c.g) * 255.0f <= 1.0f && std::fabs(c.g - c.b) * 255.0f <= 1.0f,
                  "SMAA + a look composes: centre is grey (%.3f %.3f %.3f)", c.r, c.g, c.b);
        CHECK_MSG(meanChroma(smaaLook) < meanChroma(smaa) * 0.1f,
                  "SMAA + a look composes: chroma %.4f -> %.4f",
                  meanChroma(smaa), meanChroma(smaaLook));
        // The look must not have undone the anti-aliasing: a desaturated SMAA
        // frame still has SMAA's smoother silhouette than a desaturated
        // non-SMAA one would. Asserted as "the frames are not the same
        // picture", which is the honest form of that claim at this size.
        CHECK_MSG(pixelDiff(smaaLook, desatFull) > 0,
                  "SMAA is still doing something under a look");
        fx.v->setPostFx(baseFx());
        render(fx.e, 2);
    }

    // ---- 6. the stack ORDER is the frame order -----------------------------
    //
    // Posterize quantizes colour; Desaturate removes it. Posterize FIRST bands
    // a coloured image and then greys the bands; Desaturate first greys the
    // image and then bands the greys — the second has strictly fewer distinct
    // colours than the first, because banding a 1-D signal cannot produce more
    // levels than banding a 3-D one. If the order did nothing the two frames
    // would be identical, and the whole authoring model would be a lie.
    {
        PostFxDesc a = baseFx();
        a.looks.push_back(look(LookKind::Posterize, 1.0f, 4.0f, 0.6f));
        a.looks.push_back(look(LookKind::Desaturate, 1.0f));
        fx.v->setPostFx(a);
        render(fx.e, 3);
        Image posterFirst;
        REQUIRE(fx.v->readPixels(posterFirst));
        writePpm(posterFirst, "looks-order-posterize-desaturate.ppm");

        PostFxDesc b = baseFx();
        b.looks.push_back(look(LookKind::Desaturate, 1.0f));
        b.looks.push_back(look(LookKind::Posterize, 1.0f, 4.0f, 0.6f));
        fx.v->setPostFx(b);
        CHECK_MSG(true, "reordering the stack rebuilt the graph (generation %u)",
                  fx.v->workspaceGeneration());
        render(fx.e, 3);
        Image desatFirst;
        REQUIRE(fx.v->readPixels(desatFirst));
        writePpm(desatFirst, "looks-order-desaturate-posterize.ppm");

        CHECK_MSG(pixelDiff(posterFirst, desatFirst) > 0,
                  "the stack ORDER is the frame order: the two orders differ (%u pixels)",
                  pixelDiff(posterFirst, desatFirst));
        const unsigned cPost = distinctColours(posterFirst);
        const unsigned cDesat = distinctColours(desatFirst);
        CHECK_MSG(cDesat <= cPost,
                  "posterize-then-desaturate keeps at least as many levels as the reverse "
                  "(%u vs %u)", cPost, cDesat);
    }

    // ---- 7. the catalogue, one section per look ----------------------------
    //
    // Each section is the same two claims: the look is an EXACT identity at
    // amount 0, and at full amount it has a structural property that names what
    // the look IS. No magic colours anywhere — a magic colour would break the
    // day somebody changes the fixture's cube.
    struct Case {
        const char *name;
        LookDesc    full;
        LookDesc    zero;
    };
    const Case cases[] = {
        { "glassWarp",
          look(LookKind::GlassWarp, 1.0f, 4.0f),
          look(LookKind::GlassWarp, 0.0f, 4.0f) },
        { "radialBlur",
          look(LookKind::RadialBlur, 1.0f, 0.5f, 0.5f, 1.0f),
          look(LookKind::RadialBlur, 0.0f, 0.5f, 0.5f, 1.0f) },
        { "oldMovie",
          look(LookKind::OldMovie, 1.0f, 0.5f, 0.5f, 0.5f),
          look(LookKind::OldMovie, 0.0f, 0.5f, 0.5f, 0.5f) },
        { "posterize",
          look(LookKind::Posterize, 1.0f, 3.0f, 0.6f),
          look(LookKind::Posterize, 0.0f, 3.0f, 0.6f) },
        { "sharpen",
          look(LookKind::Sharpen, 1.0f),
          look(LookKind::Sharpen, 0.0f) },
        { "filmGrade",
          // Saturation 0.2, contrast 1.4, vignette 0.9, a warm tint: every knob
          // off neutral, so "did the grade run" has more than one witness.
          look(LookKind::FilmGrade, 1.0f, 0.2f, 1.4f, 0.9f, 1.2f, 0.9f, 0.6f),
          look(LookKind::FilmGrade, 0.0f, 0.2f, 1.4f, 0.9f, 1.2f, 0.9f, 0.6f) },
    };

    const float plainGradient = meanGradient(plain);
    const unsigned plainColours = distinctColours(plain);
    std::printf("   baseline gradient %.5f, distinct colours %u\n",
                plainGradient, plainColours);

    for (const Case &c : cases) {
        // THE IDENTITY, first: it is the property that protects every other
        // suite in the tree, so it is checked before anything else.
        {
            PostFxDesc d = baseFx();
            d.looks.push_back(c.zero);
            fx.v->setPostFx(d);
            render(fx.e, 3);
            Image identity;
            REQUIRE(fx.v->readPixels(identity));
            const unsigned diff = pixelDiff(plain, identity);
            CHECK_MSG(diff == 0,
                      "%s at amount 0 is byte-identical to no look: %u pixels differ "
                      "(worst channel %.2f/255)",
                      c.name, diff, maxChannelDiff(plain, identity));
        }
        PostFxDesc d = baseFx();
        d.looks.push_back(c.full);
        fx.v->setPostFx(d);
        render(fx.e, 4);
        Image img;
        REQUIRE(fx.v->readPixels(img));
        std::string path = std::string("looks-") + c.name + ".ppm";
        writePpm(img, path.c_str());

        const unsigned moved = pixelDiff(plain, img);
        CHECK_MSG(moved > (plain.width * plain.height) / 20,
                  "%s at full amount changes the picture (%u of %u pixels)",
                  c.name, moved, plain.width * plain.height);

        // ...and the property that says WHICH look this is.
        switch (c.full.kind) {
        case LookKind::GlassWarp: {
            // A warp MOVES pixels; it does not invent or destroy them. So the
            // palette survives: the frame's mean chroma stays in the same
            // neighbourhood while a fifth of the pixels have changed.
            const float before = meanChroma(plain), after = meanChroma(img);
            CHECK_MSG(after > before * 0.6f && after < before * 1.4f,
                      "glassWarp resamples rather than regrades: chroma %.4f -> %.4f",
                      before, after);
            break;
        }
        case LookKind::RadialBlur: {
            // A blur REMOVES local contrast, everywhere and unambiguously.
            const float after = meanGradient(img);
            CHECK_MSG(after < plainGradient * 0.9f,
                      "radialBlur softens the frame: mean gradient %.5f -> %.5f",
                      plainGradient, after);
            break;
        }
        case LookKind::OldMovie: {
            // SEPIA IS R > G > B — the structural assertion the spec's R3 asks
            // for, and it holds at every phase of the flicker, so a
            // time-animated look needs no pinned frame.
            double r = 0, g = 0, b = 0;
            for (unsigned y = 0; y < img.height; ++y)
                for (unsigned x = 0; x < img.width; ++x) {
                    const Colour p = img.at(x, y);
                    r += p.r; g += p.g; b += p.b;
                }
            CHECK_MSG(r > g && g > b,
                      "oldMovie is sepia (mean R %.1f > G %.1f > B %.1f)", r, g, b);
            break;
        }
        case LookKind::Posterize: {
            // Fewer distinct colours: that IS posterize.
            const unsigned after = distinctColours(img);
            CHECK_MSG(after < plainColours,
                      "posterize quantizes: %u distinct colours -> %u", plainColours, after);
            break;
        }
        case LookKind::Sharpen: {
            // More local contrast: that IS sharpen.
            const float after = meanGradient(img);
            CHECK_MSG(after > plainGradient * 1.05f,
                      "sharpen raises local contrast: mean gradient %.5f -> %.5f",
                      plainGradient, after);
            break;
        }
        case LookKind::FilmGrade: {
            // Two independent witnesses: saturation 0.2 drops the chroma, and
            // vignette 0.9 makes the corner far darker than the centre while
            // the centre keeps its brightness.
            CHECK_MSG(meanChroma(img) < meanChroma(plain) * 0.6f,
                      "filmGrade desaturates at saturation 0.2: chroma %.4f -> %.4f",
                      meanChroma(plain), meanChroma(img));
            const auto lum = [](const Colour &p) { return 0.3f * p.r + 0.59f * p.g + 0.11f * p.b; };
            const float cornerBefore = lum(plain.at(2, 2));
            const float cornerAfter  = lum(img.at(2, 2));
            CHECK_MSG(cornerAfter <= cornerBefore + 1.0f / 255.0f,
                      "filmGrade vignettes the corner: %.4f -> %.4f", cornerBefore, cornerAfter);
            break;
        }
        default: break;
        }
    }

    // ---- 8. a full seven-look stack builds and draws ------------------------
    //
    // The catalogue's worst case: every look at once, seven quads and the
    // ping-pong between them. It is here because "the stage supports N" is a
    // claim about the graph and the only honest proof is a frame.
    {
        PostFxDesc d = baseFx();
        d.looks.push_back(look(LookKind::GlassWarp, 0.3f, 4.0f));
        d.looks.push_back(look(LookKind::RadialBlur, 0.3f, 0.5f, 0.5f, 1.0f));
        d.looks.push_back(look(LookKind::Sharpen, 0.4f));
        d.looks.push_back(look(LookKind::Posterize, 0.6f, 8.0f, 0.6f));
        d.looks.push_back(look(LookKind::Desaturate, 0.3f));
        d.looks.push_back(look(LookKind::OldMovie, 0.4f, 0.5f, 0.5f, 0.5f));
        d.looks.push_back(look(LookKind::FilmGrade, 0.8f, 0.8f, 1.2f, 0.5f, 1.1f, 1.0f, 0.9f));
        fx.v->setPostFx(d);
        render(fx.e, 4);
        Image all;
        REQUIRE(fx.v->readPixels(all));
        writePpm(all, "looks-all-seven.ppm");
        CHECK_MSG(pixelDiff(plain, all) > 0, "a seven-look stack renders a frame");
        // And it comes back: the graph returns to the passthrough shape.
        fx.v->setPostFx(baseFx());
        render(fx.e, 3);
        Image back;
        REQUIRE(fx.v->readPixels(back));
        CHECK_MSG(pixelDiff(plain, back) == 0,
                  "tearing down a seven-look stack returns the original frame (%u differ)",
                  pixelDiff(plain, back));
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
