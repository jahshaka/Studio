// LIGHT ICONS ARE OVERLAYS, NOT SCENE CONTENT (owner report 2026-09-08: "the
// light icons are grey and blurred").
//
// THE DEFECT. A light icon is a BillboardSet2, and a BillboardSet2 draws only
// from a render queue whose mode is PARTICLE_SYSTEM. Upstream arms exactly one
// (15, kParticleSystemDefaultRenderQueueId), and 15 sits inside the OPAQUE
// pass — so the icon went through the tonemapper, the bloom, the ambient
// occlusion and the SMAA edge pass with the rest of the scene. A white glyph
// came out ~24% grey with its edges smeared, while every other helper (gizmo,
// wires, grid) was already immune by living at kOverlayRenderQueue, which the
// chain draws AFTER the whole post chain.
//
// THE FIX, in one sentence: an Overlay-layer billboard set is moved to a SECOND
// particle-system queue (kHelperOverlayRenderQueue = 211) that lives inside the
// overlay pass's range, and is given a depth-test-free datablock so it reads
// like the gizmo. Real emitters stay at 15 — they are scene content and MUST be
// graded with the scene.
//
// WHAT EACH CASE PROVES
//   1. THE SAME PIXEL, POST ON AND OFF. An overlay icon's interior reads pure
//      white with the whole post chain running, and reads the SAME value it
//      reads with no chain at all. A scene-layer billboard in the same frame
//      does NOT: it is graded, which is both the control for case 1 and the
//      proof that PFX2 emitters were left alone.
//   2. ALWAYS ON TOP. An occluder between the camera and the quad hides a
//      scene-layer billboard and does not hide an overlay one.
//   3. NOT IN A MIRROR. A helper-flagged overlay icon is drawn in the main view
//      and absent from a planar reflection, while an ordinary billboard beside
//      it is reflected in the same frame — and clearing the helper flag on THAT
//      one removes it from the mirror too, which is what makes case 3 a
//      statement about the helper channel rather than about billboards.
//      (The reflection-probe pass is the same mechanism twice over —
//      `visibility_mask 0x1` plus `rq_last 200` in JahshakaPcc.compositor — and
//      is pinned for helper geometry by gi.pcc_mirror.)
//   4. THE MIP CHAIN. createTexture(mipmaps) really builds one; the default
//      still builds a single level, so every other caller is untouched.
//   5. A SCENE-LAYER SET IS EXACTLY WHAT IT WAS: still at the particle queue,
//      still depth-tested, still graded (cases 1 and 2 measure this from the
//      other side).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
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
        if (cond) std::printf("ok: " fmt "\n", __VA_ARGS__);                     \
        else { std::printf("FAIL: " fmt "\n", __VA_ARGS__); ++failures; }        \
    } while (0)

namespace {

void render(Engine *e, int frames = 4)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

/// A white disc on transparent black, RGBA8 — the shape of every bundled light
/// glyph once the mirror has forced it white (SceneMirror::iconTextureFor).
std::vector<unsigned char> discPixels(unsigned n)
{
    std::vector<unsigned char> px(size_t(n) * n * 4u, 0);
    const float c = (float(n) - 1.0f) * 0.5f, r = float(n) * 0.42f;
    for (unsigned y = 0; y < n; ++y)
        for (unsigned x = 0; x < n; ++x) {
            const float dx = float(x) - c, dy = float(y) - c;
            const bool in = dx * dx + dy * dy <= r * r;
            unsigned char *p = &px[(size_t(y) * n + x) * 4u];
            p[0] = p[1] = p[2] = 255;
            p[3] = in ? 255 : 0;
        }
    return px;
}

int to255(float v) { return int(v * 255.0f + 0.5f); }

/// One billboard on its own node at `pos`, `size` across.
NodeId addQuad(Scene *s, TextureId tex, const Vec3 &pos, float size,
               BillboardLayer layer, bool helper, const Colour &tint = Colour(1, 1, 1, 1))
{
    const NodeId n = s->createNode();
    if (!n) return 0;
    if (helper) s->setNodeHelper(n, true);
    if (!s->createBillboardSet(n, tex, false, 1, layer)) return 0;
    BillboardInstance b;
    b.position = pos;
    b.size = size;
    b.colour = tint;
    if (!s->setBillboards(n, &b, 1)) return 0;
    return n;
}

/// Max over a band of rows of (channel - the brighter of the other two): "there
/// is something GREEN here", immune to exposure and to how bright the floor is.
float maxGreenExcess(const Image &img, unsigned y0, unsigned y1)
{
    float best = 0.0f;
    for (unsigned y = y0; y < y1 && y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            const float e = c.g - (c.r > c.b ? c.r : c.b);
            if (e > best) best = e;
        }
    return best;
}

float maxRedExcess(const Image &img, unsigned y0, unsigned y1)
{
    float best = 0.0f;
    for (unsigned y = y0; y < y1 && y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            const float e = c.r - (c.g > c.b ? c.g : c.b);
            if (e > best) best = e;
        }
    return best;
}

/// The runner's evidence line: one scanline through both quads, printed as
/// 8-bit luminance. An overlay icon is a FLAT WHITE PLATEAU; a graded one is a
/// grey lump.
void printScanline(const Image &img, unsigned y, const char *what)
{
    std::printf("   scanline y=%u (%s):\n     ", y, what);
    for (unsigned x = 0; x < img.width; x += 8) {
        const Colour c = img.at(x, y);
        std::printf("%3d ", (to255(c.r) + to255(c.g) + to255(c.b)) / 3);
    }
    std::printf("\n");
}

// ---------------------------------------------------------------------------
// CASE 1 + 2 + 4: one scene, one view, post fx on and off.
void gradingAndDepth(Engine *engine)
{
    View *view = engine->createOffscreenView("icons", 256, 256, Colour(0, 0, 0));
    Scene *s = engine->createScene("icons");
    CHECK(view && s, "offscreen view + scene");
    if (!view || !s) return;
    view->setScene(s);
    s->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.10f, 0.10f, 0.10f));

    // A big lit backdrop, so the tonemapper has a scene to grade and the frame
    // is not black behind the quads.
    const NodeId backdrop = enginetest::addTestCube(s, Colour(0.45f, 0.25f, 0.15f), 0.0f, 0.6f);
    enginetest::setNodeScale(s, backdrop, Vec3(30.0f, 30.0f, 0.5f));
    enginetest::setNodePosition(s, backdrop, Vec3(0.0f, 0.0f, -3.0f));
    enginetest::addDirectionalLight(s, Vec3(-0.2f, -0.6f, -1.0f), 3.0f);

    const std::vector<unsigned char> px = discPixels(64);
    const TextureId mipped = s->createTexture(64, 64, px.data(), true, /*mipmaps*/ true);
    const TextureId plain  = s->createTexture(64, 64, px.data(), true);
    CHECK(mipped && plain, "icon textures created");

    // ---- 4. the mip chain --------------------------------------------------
    CHECK_MSG(s->textureMipmaps(mipped) == 7u,
              "a 64x64 mipmapped texture has the whole chain: %u levels (want 7)",
              s->textureMipmaps(mipped));
    CHECK_MSG(s->textureMipmaps(plain) == 1u,
              "the default is still ONE level, so no other caller moved: %u",
              s->textureMipmaps(plain));
    CHECK(s->textureMipmaps(999999) == 0u, "textureMipmaps(unknown) is 0");

    const NodeId icon    = addQuad(s, mipped, Vec3(-1.2f, 0.6f, 0.0f), 1.0f,
                                   BillboardLayer::Overlay, /*helper*/ true);
    const NodeId control = addQuad(s, mipped, Vec3( 1.2f, 0.6f, 0.0f), 1.0f,
                                   BillboardLayer::Scene, /*helper*/ false);
    CHECK(icon && control, "an OVERLAY icon and a SCENE billboard, same texture");

    enginetest::testCameraLookAt(view, Vec3(0.0f, 0.6f, 5.0f), Vec3(0.0f, 0.6f, 0.0f));
    render(engine);
    Image plainFrame;
    CHECK(view->readPixels(plainFrame), "readPixels (no post chain)");
    // Both quads sit on the middle row; x = 0.6 * width/2 / (5*tan(22.5)) either
    // side of centre. Found once, printed, and asserted rather than trusted.
    const unsigned row = plainFrame.height / 2;
    const unsigned iconX = 54, ctrlX = 202;
    const Colour iconOff = plainFrame.at(iconX, row);
    const Colour ctrlOff = plainFrame.at(ctrlX, row);
    std::printf("   no post : icon %d %d %d   scene-layer %d %d %d\n",
                to255(iconOff.r), to255(iconOff.g), to255(iconOff.b),
                to255(ctrlOff.r), to255(ctrlOff.g), to255(ctrlOff.b));
    printScanline(plainFrame, row, "no post");
    CHECK_MSG(to255(iconOff.r) > 250 && to255(iconOff.g) > 250 && to255(iconOff.b) > 250,
              "with no post chain the icon is white: %d %d %d",
              to255(iconOff.r), to255(iconOff.g), to255(iconOff.b));
    CHECK_MSG(to255(ctrlOff.r) > 250, "…and so is the scene-layer quad beside it: %d",
              to255(ctrlOff.r));

    // ---- 1. the Epic chain -------------------------------------------------
    // tonemapFixed, not auto exposure: the SAME curve, driven by a constant, so
    // the graded value is deterministic frame to frame and machine to machine.
    PostFxDesc fx;
    fx.allowOffscreen = true;      // the only way an offscreen view gets a chain
    fx.hdr = true;
    fx.tonemapFixed = true;
    fx.bloom = true;
    fx.ssao = true;
    fx.smaaPreset = 2;
    view->setPostFx(fx);
    render(engine, 6);
    if (!engine->lastError().empty())
        std::printf("   lastError after enabling the chain: %s\n", engine->lastError().c_str());
    Image graded;
    CHECK(view->readPixels(graded), "readPixels (hdr+bloom+ssao+smaa)");
    const Colour iconOn = graded.at(iconX, row);
    const Colour ctrlOn = graded.at(ctrlX, row);
    std::printf("   post on : icon %d %d %d   scene-layer %d %d %d\n",
                to255(iconOn.r), to255(iconOn.g), to255(iconOn.b),
                to255(ctrlOn.r), to255(ctrlOn.g), to255(ctrlOn.b));
    printScanline(graded, row, "hdr+bloom+ssao+smaa");
    CHECK_MSG(to255(iconOn.r) > 250 && to255(iconOn.g) > 250 && to255(iconOn.b) > 250,
              "THE FIX: the icon is still pure white with the whole chain on: %d %d %d",
              to255(iconOn.r), to255(iconOn.g), to255(iconOn.b));
    CHECK_MSG(std::abs(to255(iconOn.r) - to255(iconOff.r)) <= 2 &&
              std::abs(to255(iconOn.g) - to255(iconOff.g)) <= 2 &&
              std::abs(to255(iconOn.b) - to255(iconOff.b)) <= 2,
              "…and it is the SAME pixel the un-graded frame drew: %d,%d,%d vs %d,%d,%d",
              to255(iconOn.r), to255(iconOn.g), to255(iconOn.b),
              to255(iconOff.r), to255(iconOff.g), to255(iconOff.b));
    // The control is the defect, still reproducible on demand: scene content IS
    // graded. (This is also the "PFX2 untouched" assertion.)
    CHECK_MSG(to255(ctrlOn.r) < to255(ctrlOff.r) - 20,
              "a SCENE-layer billboard is still graded by the chain (%d -> %d), "
              "i.e. real emitters were left alone",
              to255(ctrlOff.r), to255(ctrlOn.r));

    // ---- 2. always on top --------------------------------------------------
    // One occluder per quad, between the camera and the billboards.
    const NodeId blockA = enginetest::addTestCube(s, Colour(0.05f, 0.05f, 0.05f), 0.0f, 0.8f);
    enginetest::setNodeScale(s, blockA, Vec3(2.0f, 2.0f, 0.2f));
    enginetest::setNodePosition(s, blockA, Vec3(-1.2f, 0.6f, 2.0f));
    const NodeId blockB = enginetest::addTestCube(s, Colour(0.05f, 0.05f, 0.05f), 0.0f, 0.8f);
    enginetest::setNodeScale(s, blockB, Vec3(2.0f, 2.0f, 0.2f));
    enginetest::setNodePosition(s, blockB, Vec3(1.2f, 0.6f, 2.0f));
    render(engine, 6);
    Image occluded;
    CHECK(view->readPixels(occluded), "readPixels (occluders in front)");
    const Colour iconBlocked = occluded.at(iconX, row);
    const Colour ctrlBlocked = occluded.at(ctrlX, row);
    std::printf("   occluded: icon %d %d %d   scene-layer %d %d %d\n",
                to255(iconBlocked.r), to255(iconBlocked.g), to255(iconBlocked.b),
                to255(ctrlBlocked.r), to255(ctrlBlocked.g), to255(ctrlBlocked.b));
    CHECK_MSG(to255(iconBlocked.r) > 250,
              "an OVERLAY icon draws through an occluder, like the gizmo: %d",
              to255(iconBlocked.r));
    CHECK_MSG(to255(ctrlBlocked.r) < 120,
              "a SCENE-layer billboard is hidden by the same occluder: %d",
              to255(ctrlBlocked.r));

    engine->destroyScene(s);
    engine->destroyView(view);
}

// ---------------------------------------------------------------------------
// CASE 3: the mirror. Shaped like tests/planar's helper case, but the object
// under test is a billboard set rather than an Item.
void notInReflections(Engine *engine)
{
    View *view = engine->createOffscreenView("icons-mirror", 256, 256, Colour(0, 0, 0));
    Scene *s = engine->createScene("icons-mirror");
    CHECK(view && s, "mirror view + scene");
    if (!view || !s) return;
    view->setScene(s);
    s->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.10f, 0.10f, 0.10f));

    const NodeId floor = enginetest::addTestCube(s, Colour(1, 1, 1), 1.0f, 0.0f);
    enginetest::setNodeScale(s, floor, Vec3(12.0f, 0.2f, 12.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.4f), 3.0f);

    const std::vector<unsigned char> px = discPixels(64);
    const TextureId tex = s->createTexture(64, 64, px.data(), true, true);
    // GREEN = the light icon (overlay layer, helper). RED = an ordinary
    // billboard. One frame answers both halves of the question.
    const NodeId icon = addQuad(s, tex, Vec3(-1.3f, 2.2f, 0.0f), 1.4f,
                                BillboardLayer::Overlay, /*helper*/ true,
                                Colour(0.0f, 1.0f, 0.0f, 1.0f));
    const NodeId ordinary = addQuad(s, tex, Vec3(1.3f, 2.2f, 0.0f), 1.4f,
                                    BillboardLayer::Scene, /*helper*/ false,
                                    Colour(1.0f, 0.0f, 0.0f, 1.0f));
    CHECK(icon && ordinary, "a helper icon and an ordinary billboard above the mirror");

    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.4f, 7.0f), Vec3(0.0f, 0.6f, 0.0f));

    PlanarReflectionParams pr;
    pr.budget = 2;
    pr.resolution = 512;
    pr.mipmaps = true;
    pr.shadows = false;
    pr.maxDistance = 12.0f;
    CHECK(s->setPlanarReflections(pr), "planar reflections armed");
    CHECK(s->setNodePlanarReflector(floor, true), "the floor is a reflector");

    render(engine, 4);
    Image img;
    CHECK(view->readPixels(img), "readPixels (mirror)");
    const unsigned half = img.height / 2;
    const float greenTop = maxGreenExcess(img, 0, half);
    const float greenBot = maxGreenExcess(img, half, img.height);
    const float redTop   = maxRedExcess(img, 0, half);
    const float redBot   = maxRedExcess(img, half, img.height);
    std::printf("   green (icon) top %.3f bottom %.3f | red (ordinary) top %.3f bottom %.3f\n",
                greenTop, greenBot, redTop, redBot);
    CHECK_MSG(greenTop > 0.20f, "the helper icon IS drawn in the main view: %.3f", greenTop);
    CHECK_MSG(redTop > 0.20f, "so is the ordinary billboard: %.3f", redTop);
    CHECK_MSG(redBot > 0.05f, "an ORDINARY billboard IS reflected in the mirror: %.3f", redBot);
    CHECK_MSG(greenBot < 0.02f, "a HELPER icon is NOT reflected: %.3f", greenBot);

    // The same object, now marked a helper: its reflection must go. This is
    // what makes the assertion above about the helper CHANNEL rather than about
    // where the two quads happen to sit.
    s->setNodeHelper(ordinary, true);
    render(engine, 4);
    CHECK(view->readPixels(img), "readPixels (ordinary billboard marked helper)");
    const float redBotHelper = maxRedExcess(img, half, img.height);
    const float redTopHelper = maxRedExcess(img, 0, half);
    std::printf("   ordinary marked helper: red top %.3f bottom %.3f\n",
                redTopHelper, redBotHelper);
    CHECK_MSG(redTopHelper > 0.20f, "…it still draws in the main view: %.3f", redTopHelper);
    CHECK_MSG(redBotHelper < redBot - 0.03f,
              "…and marking it a helper removed it from the mirror: %.3f -> %.3f",
              redBot, redBotHelper);

    engine->destroyScene(s);
    engine->destroyView(view);
}

}   // namespace

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-light-icons-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    gradingAndDepth(engine.get());
    notInReflections(engine.get());

    std::printf(failures ? "\n%d FAILURES\n" : "\nall light-icon assertions passed\n", failures);
    return failures ? 1 : 0;
}
