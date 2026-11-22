// sky.clones_share_pipelines — A SECOND SCENE's SKY BUILDS NO LOW-LEVEL PIPELINE (SHADER-SYSTEM-1 7a, R1: the low-level
// pipeline is keyed on its content). Every scene draws its sky, sun disc and height fog through a PER-SCENE CLONE of one
// low-level material (Jahshaka/AtmosphereSky<id>, SunDisc<id>, HeightFogSky<id>: OgreAtmosphere.cpp createQuad /
// createFogQuad, OgreSky.cpp's sun disc) - the clone's constants are the scene's, its programs and blocks the base's.
// HlmsLowLevel keyed a renderable on its blocks and its PASS ID (OgreHlmsLowLevel.cpp calculateHashFor), so each clone
// was a new pipeline built in the frame that first drew it: the startup gate's scene could never warm the editor's
// scene's sky (spikes/shader-system/ss7/gategap-red2/ctest.log: AtmosphereSky4/SunDisc4/HeightFogSky4 built in the
// editor's frames). Keyed on the programs (identity + load generation), the blocks and the vertex layout, the clones
// share one pipeline.
// THE SCRIPT: one engine; scene A (an atmosphere sky, its sun disc, the height fog, a cube) in an asynchronous offscreen
// view (the editor's mode), frames until it settles; then scene B, the same sky in a second view of the same size,
// frames until it settles.
// THE LAW: (1) scene B's frames build 0 low-level pipelines (the published lowLevelBuiltInFrame, a process total read
// before and after B). CONTROL (the zero-pass rule): scene A's frames build at least kSkyPipelines (the sky, the sun
// disc, the fog: else the scene drew no clone and (1) proves nothing). RED before the content key: B built 3.
// SETTLED = the scene's sky drawn (the atmosphere is its sky and its tables are built: the quad shows from then), nothing
// pending on the background compiler, its environment landed, and no low-level pipeline built for kQuietFrames frames in
// a row; kSettleCap is its hang guard (never a bar).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include <cstdio>
using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

/// The quads each scene's sky draws through its own clone: the sky, the sun disc, the height fog.
static constexpr unsigned long long kSkyPipelines = 3;
/// A quad's pipeline is built in the frame that draws it, published at that frame's close: three quiet frames bound the
/// publish's one-frame lag with margin.
static constexpr int kQuietFrames = 3;
static constexpr int kSettleCap = 3000;

struct Settle { int frames = 0; bool settled = false; unsigned long long lowLevel = 0; };

static Settle settle(Engine *e, Scene *s)
{
    Settle r;
    const unsigned long long before = e->published().shaders.lowLevelBuiltInFrame;
    unsigned long long last = before;
    int quiet = 0;
    for (; r.frames < kSettleCap; ++r.frames) {
        enginetest::frames(e, 1);
        const AsyncShaderStats &now = e->published().shaders;
        const PublishedFrame &ps = s->published();
        // the sky is DRAWN (its quad shows once the air is the sky: the tables built) and its environment landed
        const bool skyDrawn = ps.atmosphereStatus.on && ps.atmosphereStatus.skyViewBuilds > 0u;
        const bool still = skyDrawn && now.pending == 0u && !ps.environmentPending && now.lowLevelBuiltInFrame == last;
        quiet = still ? quiet + 1 : 0;
        last = now.lowLevelBuiltInFrame;
        if (quiet >= kQuietFrames) { r.settled = true; ++r.frames; break; }
    }
    r.lowLevel = e->published().shaders.lowLevelBuiltInFrame - before;
    return r;
}

static Scene *skyScene(Engine *e, const char *name, View *v)
{
    Scene *s = e->createScene(name);
    if (!s) return nullptr;
    v->setScene(s);
    v->setCamera(enginetest::testCameraDescLookAt(Vec3(0.0f, 1.6f, 3.0f), Vec3(0.0f, 1.6f, 0.0f)));
    SkyDesc sky;
    sky.mode = SkyMode::Atmosphere;
    sky.sun.enabled = true;
    sky.sun.dir[0] = 0.0f; sky.sun.dir[1] = 0.2f; sky.sun.dir[2] = -0.98f;   // in front of the camera, above the horizon
    HeightFogDesc fog;
    fog.enabled = true;
    const bool ok = s->setSky(sky) && enginetest::addTestCube(s, Colour(0.8f, 0.3f, 0.2f), 0.0f, 0.6f) != 0;
    s->setHeightFog(fog);
    return ok ? s : nullptr;
}

int main()
{
    std::printf("== sky.clones_share_pipelines: a second scene's sky builds no low-level pipeline\n");
    EngineConfig cfg = enginetest::testEngineConfig("test-sky-clones-share-ogre.log");
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();

    View *va = e->createOffscreenView("clones-a", 64, 64, Colour(0, 0, 0), OffscreenContract::Live);
    if (!va) { std::printf("FAIL: view A (%s)\n", e->lastError().c_str()); return 1; }
    va->setAsyncShaders(true);
    Scene *a = skyScene(e, "clones-a", va);
    CHECK(a != nullptr, "scene A built: an atmosphere sky with its sun disc, the height fog, a cube (%s)",
          e->lastError().c_str());
    if (!a) return 1;
    const Settle sa = settle(e, a);
    std::printf("    scene A: %d frames, settled %d, low-level pipelines built %llu\n", sa.frames, sa.settled ? 1 : 0,
                sa.lowLevel);
    CHECK(sa.settled, "scene A settled within %d frames", kSettleCap);
    CHECK(sa.lowLevel >= kSkyPipelines, "CONTROL: scene A built its sky's pipelines (%llu, at least %llu)", sa.lowLevel,
          kSkyPipelines);

    View *vb = e->createOffscreenView("clones-b", 64, 64, Colour(0, 0, 0), OffscreenContract::Live);
    if (!vb) { std::printf("FAIL: view B (%s)\n", e->lastError().c_str()); return 1; }
    vb->setAsyncShaders(true);
    Scene *b = skyScene(e, "clones-b", vb);
    CHECK(b != nullptr, "scene B built: the same sky (%s)", e->lastError().c_str());
    if (!b) return 1;
    const Settle sb = settle(e, b);
    std::printf("    scene B: %d frames, settled %d, low-level pipelines built %llu\n", sb.frames, sb.settled ? 1 : 0,
                sb.lowLevel);
    CHECK(sb.settled, "scene B settled within %d frames", kSettleCap);
    CHECK(sb.lowLevel == 0u, "(1) scene B's sky clones built %llu low-level pipeline(s) (bar 0: they share scene A's)",
          sb.lowLevel);

    enginetest::poseRegistry().erase(a);
    enginetest::poseRegistry().erase(b);
    if (failures) std::printf("sky.clones_share_pipelines: FAIL (%d)\n", failures);
    else std::printf("sky.clones_share_pipelines: PASS\n");
    return failures ? 1 : 0;
}
