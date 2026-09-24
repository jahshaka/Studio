// gi.scene_switch — WHAT THE SHADER READS IS THE SCENE BEING DRAWN
// (PHOTON-SCENE-SWITCH-1).
//
// HlmsPbs holds ONE VctLighting, ONE IrradianceField and ONE parallax-corrected
// cubemap pointer for the whole process. They used to be bound by "the last
// scene to build" and taken back by a verb on a page return, so a second scene
// drawn in the same frame shaded through the FIRST scene's voxels, field and
// probes (gi.pcc_second_scene's mirror read another scene's probes, 0.106 /
// 0.149 / 0.106 against its own green 1.0). Every PBS pass now binds its own
// scene's arms (SceneGiBinding), for every view of every scene, every frame.
//
// THE FIXTURE: two rooms, one BLUE and one RED (floor, walls and a caster all in
// the room's colour, a white mirror cube at the origin of BOTH — inside the other
// scene's probe box, which is what made the old leak visible), each with its own
// sun, its own hybrid (voxels + a 2x1x2 probe grid + the irradiance field). The
// mirror's lower half reflects the room's floor, so it reads the room's colour from
// whichever probes / cones its pass binds.
//
// ASSERTED EVERY FRAME AFTER A PHASE'S FIRST 3 (frames, never time):
//   * each view's mirror reads ITS room's colour (blue over red for A, red over
//     blue for B);
//   * each room's floor is SHADOWED under its caster (the map; the shadowed
//     class's mean luma under 0.6 of the sunlit class's, both classified
//     analytically from the sun and the boxes).
// PHASES, 60 frames each: (1) views created A, B, then a THUMBNAIL-shaped view of
// B (no post effects), all drawn every frame — A's passes before B's; (2) A and B
// drawn ALTERNATELY, one scene per frame; (3) the views re-created B first — B's
// passes before A's; (4) scene A DESTROYED — B alone, still its own.
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
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

static const unsigned kSize = 160;           // square: groundPointForPixel's frame
static const unsigned kThumb = 96;           // the thumbnail-shaped view
static const float kFov = 45.0f;
static const Vec3 kCamPos(0.0f, 2.6f, 4.2f), kCamTarget(0.0f, 1.2f, 0.0f);
static const Vec3 kSunDir(0.35f, -0.9f, 0.3f);       // travels +x, down, +z
static const Vec3 kCasterPos(1.8f, 1.0f, -0.4f), kCasterSize(0.8f, 2.0f, 0.8f);
static const float kWallHeight = 3.0f;
static const Vec3 kMirrorPos(0.0f, 1.2f, 0.0f);
static const float kMirrorSize = 1.2f;
static const int kPhaseFrames = 60, kSkipFrames = 3;

static NodeId addBox(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = enginetest::addTestCube(s, albedo, 0.0f, 0.9f);
    enginetest::setNodePosition(s, n, pos);
    enginetest::setNodeScale(s, n, scale);
    return n;
}

/// One room in `c`: a 100 m ground (the auto GI box then holds the room with
/// room to spare, so the 2x1x2 probes each photograph the scene — the
/// gi.pcc_second_scene layout), 3 m walls around 10 m, no ceiling (the sun comes
/// in), a caster box, a white mirror cube at the origin, its own sun, its own
/// hybrid.
static bool buildRoom(Scene *s, const Colour &c)
{
    s->setAmbient(Colour(0.03f, 0.03f, 0.03f), Colour(0.03f, 0.03f, 0.03f));
    addBox(s, c, Vec3(0.0f, -0.1f, 0.0f), Vec3(100.0f, 0.2f, 100.0f));
    const float h = kWallHeight, t = 0.2f;
    addBox(s, c, Vec3(-5.1f, 0.5f * h, 0.0f), Vec3(t, h, 10.4f));
    addBox(s, c, Vec3( 5.1f, 0.5f * h, 0.0f), Vec3(t, h, 10.4f));
    addBox(s, c, Vec3(0.0f, 0.5f * h, -5.1f), Vec3(10.4f, h, t));
    addBox(s, c, Vec3(0.0f, 0.5f * h,  5.1f), Vec3(10.4f, h, t));
    addBox(s, c, kCasterPos, kCasterSize);
    PbrParams mp;
    mp.albedo = Colour(1, 1, 1);
    mp.metalness = 1.0f;
    mp.roughness = 0.0f;
    const NodeId mirror = s->createNode();
    s->attachMesh(mirror, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(mp));
    s->setNodeTransform(mirror, kMirrorPos, Quat(), Vec3(kMirrorSize, kMirrorSize, kMirrorSize));
    enginetest::addDirectionalLight(s, kSunDir, 5.0f);
    GiParams gi;
    gi.mode = GiMode::VctPccHybrid;
    gi.quality = GiQuality::Low;
    gi.numBounces = 1;
    gi.pccProbesX = 2; gi.pccProbesY = 1; gi.pccProbesZ = 2;
    gi.updateBudget = 1;
    gi.ddgi = GiToggle::On;
    return s->setGlobalIllumination(gi);
}

// ---- the analytic floor classes --------------------------------------------
/// Does o + t d, t in [0, tMax], pass through the box?
static bool rayHitsBox(const Vec3 &o, const Vec3 &d, const Vec3 &mn, const Vec3 &mx, float tMax = 1e30f)
{
    float t0 = 0.0f, t1 = tMax;
    const float oo[3] = { o.x, o.y, o.z }, dd[3] = { d.x, d.y, d.z };
    const float lo[3] = { mn.x, mn.y, mn.z }, hi[3] = { mx.x, mx.y, mx.z };
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(dd[a]) < 1e-9f) { if (oo[a] < lo[a] || oo[a] > hi[a]) return false; continue; }
        float ta = (lo[a] - oo[a]) / dd[a], tb = (hi[a] - oo[a]) / dd[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta); t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    return true;
}
static void boxOf(const Vec3 &c, const Vec3 &size, float grow, Vec3 &mn, Vec3 &mx)
{
    mn = Vec3(c.x - 0.5f * size.x - grow, c.y - 0.5f * size.y - grow, c.z - 0.5f * size.z - grow);
    mx = Vec3(c.x + 0.5f * size.x + grow, c.y + 0.5f * size.y + grow, c.z + 0.5f * size.z + grow);
}

struct Classes { std::vector<unsigned> shadow, lit; };
/// Every floor pixel the camera sees on the open floor (|x|, |z| < 3.5): SHADOWED
/// when its sun ray passes through the caster or the mirror (shrunk by 8 cm),
/// SUNLIT when it clears both (grown by 25 cm) and every wall.
static Classes classify()
{
    Classes out;
    const Vec3 toSun(-kSunDir.x, -kSunDir.y, -kSunDir.z);
    struct Box { Vec3 c, size; };
    const Box casters[2] = { { kCasterPos, kCasterSize },
                             { kMirrorPos, Vec3(kMirrorSize, kMirrorSize, kMirrorSize) } };
    const float h = kWallHeight;
    const Box walls[4] = { { Vec3(-5.1f, 0.5f * h, 0.0f), Vec3(0.2f, h, 10.4f) },
                           { Vec3( 5.1f, 0.5f * h, 0.0f), Vec3(0.2f, h, 10.4f) },
                           { Vec3(0.0f, 0.5f * h, -5.1f), Vec3(10.4f, h, 0.2f) },
                           { Vec3(0.0f, 0.5f * h,  5.1f), Vec3(10.4f, h, 0.2f) } };
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Vec3 g = enginetest::groundPointForPixel(kCamPos, kCamTarget, x, y, kSize, 0.0f, kFov);
            if (std::fabs(g.x) > 3.5f || std::fabs(g.z) > 3.5f) continue;   // the open floor only
            const Vec3 view(g.x - kCamPos.x, g.y - kCamPos.y, g.z - kCamPos.z);
            bool seen = true, deep = false, near = false;
            for (const Box &k : casters) {
                Vec3 mn, mx;
                boxOf(k.c, k.size, 0.05f, mn, mx);   // the camera must SEE the point
                if (rayHitsBox(kCamPos, view, mn, mx, 1.0f)) seen = false;
                const Vec3 up(g.x, 0.001f, g.z);
                boxOf(k.c, k.size, -0.08f, mn, mx);
                if (rayHitsBox(up, toSun, mn, mx)) deep = true;
                boxOf(k.c, k.size, 0.25f, mn, mx);
                if (rayHitsBox(up, toSun, mn, mx)) near = true;
            }
            if (!seen) continue;
            if (deep) { out.shadow.push_back(y * kSize + x); continue; }
            if (near) continue;
            bool wallShade = false;
            for (const Box &w : walls) {
                Vec3 mn, mx;
                boxOf(w.c, w.size, 0.25f, mn, mx);
                if (rayHitsBox(Vec3(g.x, 0.001f, g.z), toSun, mn, mx)) wallShade = true;
            }
            if (!wallShade) out.lit.push_back(y * kSize + x);
        }
    return out;
}

static float luma(const Image &img, unsigned i)
{
    return 0.2126f * img.rgba[i * 4u] + 0.7152f * img.rgba[i * 4u + 1] + 0.0722f * img.rgba[i * 4u + 2];
}
static float meanLuma(const Image &img, const std::vector<unsigned> &px)
{
    double sum = 0.0;
    for (unsigned i : px) sum += luma(img, i);
    return px.empty() ? 0.0f : float(sum / double(px.size()));
}

/// The running tally of one phase's per-frame assertions.
struct Tally {
    int frames = 0, mirrorBad = 0, shadowBad = 0;
    float worstMirror = 1e9f, worstShadowRatio = 0.0f;
};

static Classes gClasses;

/// Evidence pictures (a tool, not a mode): JAH_SCENE_SWITCH_DUMP=<dir>.
static void dump(View *v, const std::string &name)
{
    const char *dir = std::getenv("JAH_SCENE_SWITCH_DUMP");
    Image img;
    if (!dir || !v->readPixels(img)) return;
    FILE *f = std::fopen((std::string(dir) + "/" + name + ".ppm").c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (size_t i = 0; i < size_t(img.width) * img.height; ++i) std::fwrite(&img.rgba[i * 4u], 1, 3, f);
    std::fclose(f);
}

/// One view's frame: its mirror's colour margin (the room's own channel over the
/// other room's) and its floor's shadow ratio.
static void judge(View *v, bool blueRoom, bool thumbnail, Tally &t)
{
    Image img;
    if (!v->readPixels(img)) { ++t.mirrorBad; return; }
    // THE MIRROR'S LOWER HALF (it reflects the room's floor): a patch, averaged.
    double r = 0.0, b = 0.0;
    unsigned n = 0;
    for (unsigned y = unsigned(0.56f * img.height); y < unsigned(0.66f * img.height); ++y)
        for (unsigned x = unsigned(0.42f * img.width); x < unsigned(0.58f * img.width); ++x) {
            const Colour c = img.at(x, y);
            r += c.r; b += c.b; ++n;
        }
    const float margin = n ? float(blueRoom ? (b - r) / n : (r - b) / n) : -1.0f;
    ++t.frames;
    t.worstMirror = std::min(t.worstMirror, margin);
    if (margin < 0.10f) ++t.mirrorBad;
    if (thumbnail) return;   // the floor classes are the full view's pixels
    const float lit = meanLuma(img, gClasses.lit), shadow = meanLuma(img, gClasses.shadow);
    const float ratio = lit > 0.0f ? shadow / lit : 1.0f;
    t.worstShadowRatio = std::max(t.worstShadowRatio, ratio);
    if (!(ratio < 0.6f)) ++t.shadowBad;
}

static void report(const char *phase, const char *who, const Tally &t, bool thumbnail,
                   int expectedFrames = kPhaseFrames - kSkipFrames)
{
    std::printf("    %-28s %-10s %2d frames judged: mirror margin worst %.3f (%d bad)", phase, who,
                t.frames, double(t.worstMirror), t.mirrorBad);
    if (!thumbnail)
        std::printf(", shadow/lit worst %.3f (%d bad)", double(t.worstShadowRatio), t.shadowBad);
    std::printf("\n");
    char msg[256];
    std::snprintf(msg, sizeof msg, "%s: %s reads ITS OWN room every frame%s", phase, who,
                  thumbnail ? "" : " and its floor is shadowed under its caster");
    CHECK(t.frames == expectedFrames && t.mirrorBad == 0 && t.shadowBad == 0, msg);
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-scene-switch-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    const Colour blue(0.12f, 0.22f, 0.85f), red(0.85f, 0.18f, 0.10f);
    const auto makeView = [&](const char *name, Scene *s, unsigned size, bool post) {
        View *v = e->createOffscreenView(name, size, size, Colour(0, 0, 0));
        v->setScene(s);
        v->setShadows(true);
        CameraDesc c = enginetest::testCameraDescLookAt(kCamPos, kCamTarget);
        c.fovDegrees = kFov;
        v->setCamera(c);
        if (post) {                        // the full views carry a post chain;
            PostFxDesc fx;                 // the thumbnail-shaped one does not
            fx.allowOffscreen = true;
            v->setPostFx(fx);
        }
        return v;
    };

    // A VIEW BEFORE ANY SCENE: the startup order (window -> Hlms -> scene manager).
    View *first = e->createOffscreenView("switch_boot", 16, 16, Colour(0, 0, 0));
    Scene *a = e->createScene("switch_a");
    Scene *b = e->createScene("switch_b");
    e->destroyView(first);
    CHECK(a && b, "two scenes");
    View *vA = makeView("switch_a", a, kSize, true);
    View *vB = makeView("switch_b", b, kSize, true);
    View *thumb = makeView("switch_thumb", b, kThumb, false);
    CHECK(vA && vB && thumb, "three offscreen views");
    CHECK(buildRoom(a, blue), "room A (blue) builds its hybrid: voxels, probes, field");
    CHECK(buildRoom(b, red), "room B (red) builds its own");
    for (int i = 0; i < 30; ++i) e->renderOneFrame();
    {
        const GiStatus sa = a->giStatus(), sb = b->giStatus();
        std::printf("    A: vct %d pcc %d (probes %d) ifd %d   B: vct %d pcc %d (probes %d) ifd %d\n",
                    int(sa.vctBound), int(sa.pccBound), sa.probeCount, int(sa.ifdBound),
                    int(sb.vctBound), int(sb.pccBound), sb.probeCount, int(sb.ifdBound));
        CHECK(sa.vctBound && sa.pccBound && sa.ifdBound && sb.vctBound && sb.pccBound && sb.ifdBound,
              "BOTH scenes' passes bind their own voxels, probes and field — at once");
    }
    gClasses = classify();
    std::printf("    floor classes: %zu shadowed px, %zu sunlit px\n", gClasses.shadow.size(),
                gClasses.lit.size());
    CHECK(gClasses.shadow.size() > 150 && gClasses.lit.size() > 1500, "the floor classes are populated");

    // ---- 1. A's passes, then B's, then the thumbnail's — every frame ---------
    {
        Tally ta, tb, tt;
        for (int f = 0; f < kPhaseFrames; ++f) {
            e->renderOneFrame();
            if (f < kSkipFrames) continue;
            judge(vA, true, false, ta);
            judge(vB, false, false, tb);
            judge(thumb, false, true, tt);
        }
        dump(vA, "phase1_a"); dump(vB, "phase1_b"); dump(thumb, "phase1_thumb");
        report("1 A then B then thumbnail", "A", ta, false);
        report("1 A then B then thumbnail", "B", tb, false);
        report("1 A then B then thumbnail", "thumb (B)", tt, true);
    }
    // ---- 2. ALTERNATELY: one scene per frame ---------------------------------
    {
        Tally ta, tb, tt;
        for (int f = 0; f < kPhaseFrames; ++f) {
            const bool drawA = (f % 2) == 0;
            vA->setEnabled(drawA);
            vB->setEnabled(!drawA);
            thumb->setEnabled(!drawA);
            e->renderOneFrame();
            if (f < kSkipFrames) continue;
            if (drawA) judge(vA, true, false, ta);
            else { judge(vB, false, false, tb); judge(thumb, false, true, tt); }
        }
        vA->setEnabled(true); vB->setEnabled(true); thumb->setEnabled(true);
        // Each view is drawn on every OTHER frame of the phase.
        int drawnA = 0, drawnB = 0;
        for (int f = kSkipFrames; f < kPhaseFrames; ++f) ((f % 2) == 0 ? drawnA : drawnB)++;
        report("2 alternating frames", "A", ta, false, drawnA);
        report("2 alternating frames", "B", tb, false, drawnB);
        report("2 alternating frames", "thumb (B)", tt, true, drawnB);
    }
    // ---- 3. B's passes first: the views re-created in the other order --------
    for (View *v : { vA, vB, thumb }) { v->setScene(nullptr); e->destroyView(v); }
    thumb = makeView("switch_thumb2", b, kThumb, false);
    vB = makeView("switch_b2", b, kSize, true);
    vA = makeView("switch_a2", a, kSize, true);
    {
        Tally ta, tb, tt;
        for (int f = 0; f < kPhaseFrames; ++f) {
            e->renderOneFrame();
            if (f < kSkipFrames) continue;
            judge(thumb, false, true, tt);
            judge(vB, false, false, tb);
            judge(vA, true, false, ta);
        }
        report("3 thumbnail then B then A", "A", ta, false);
        report("3 thumbnail then B then A", "B", tb, false);
        report("3 thumbnail then B then A", "thumb (B)", tt, true);
    }
    // ---- 4. A DESTROYED: B alone, still its own ------------------------------
    vA->setScene(nullptr);
    e->destroyView(vA);
    e->destroyScene(a);
    a = nullptr;
    {
        Tally tb, tt;
        for (int f = 0; f < kPhaseFrames; ++f) {
            e->renderOneFrame();
            if (f < kSkipFrames) continue;
            judge(vB, false, false, tb);
            judge(thumb, false, true, tt);
        }
        report("4 A destroyed", "B", tb, false);
        report("4 A destroyed", "thumb (B)", tt, true);
        const GiStatus sb = b->giStatus();
        CHECK(sb.vctBound && sb.pccBound && sb.ifdBound, "4 A destroyed: B's arms are still bound in its passes");
    }
    for (View *v : { vB, thumb }) { v->setScene(nullptr); e->destroyView(v); }
    e->destroyScene(b);

    std::printf(failures ? "\ngi.scene_switch: FAILED (%d)\n" : "\ngi.scene_switch: all ok\n", failures);
    return failures ? 1 : 0;
}
