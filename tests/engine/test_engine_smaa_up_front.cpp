// engine.smaa_up_front — NO SMAA PROGRAM IS COMPILED IN A FRAME (SHADER-SYSTEM-1 7a, the SMAA item). The SMAA preset is
// a DOCUMENT's setting (iris::Scene::smaaPreset), unknowable when the engine is created, and a preset is applied by
// reloading the six SMAA programs with its defines (chain::initSmaa, from applyRecompileGlobals at the top of a frame):
// on a cache that lacks the preset that reload is six glslang compiles INSIDE the frame (the startup gate's Basic world:
// spikes/shader-system/ss7/gate-measure). The engine compiles every preset (0-3) where it parses the materials, so a
// frame's reload is six microcode-cache hits (the key carries the defines: OgreVulkanProgram.cpp:171-174, :740).
// THE SCRIPT: one engine on its OWN shader cache, EMPTY at the start (an app's first launch: every SMAA program cold,
// so the up-front compile is the only thing that can have made a preset's programs before its frame; NOT the cache
// off - with no cache Ogre keeps no microcode at all, OgreShaderCache.cpp's load turns the saving on only for an
// enabled cache, and every reload compiles again; the app always has one); an offscreen Live view in asynchronous mode
// (the editor's), the chain allowed offscreen; a lit cube. Phase A: the chain without SMAA, frames until the background
// compiler holds nothing and no pipeline is built for kQuietFrames frames. Phase B: SMAA at preset 2 (High: the editor's
// at the High tier, a document's usual first ask). Phase C: SMAA at preset 0 (Low: a different preset asked later in
// the same launch - the same reload a launch whose document asks Low makes at its first SMAA frame).
// THE LAW: (1) each SMAA phase compiled 0 shaders on a thread other than the background compiler's (the
// published compiledInForeground, a process total read before and after the phase: everything a frame waited for).
// RED before the up-front compile: 6 in each phase.
// PRINTED, NOT A LAW (the lead's ruling waits on it): the graphics pipelines each SMAA phase built (the fork's
// process-wide count and driver time; the background compiler is empty and the scene unchanged, so they are the frame's
// own: the SMAA quads' low-level pipelines, built on the render thread at their first draw, HlmsLowLevel has no async
// path). Run it with the driver's shader cache empty (__GL_SHADER_DISK_CACHE_PATH at an empty directory) and warm.
// `--keep` (by hand only): the cache the last run saved is kept, not emptied - the up-front compile's cost on a WARM
// app cache (the engine log's "SMAA presets" line).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

/// A phase is over when nothing is pending on the background compiler and no graphics pipeline was built for this many
/// frames in a row: a workspace rebuilt by setPostFx draws at the next frame, its quads' pipelines built in that frame;
/// the published status is a frame late. Three quiet frames bound both.
static constexpr int kQuietFrames = 3;
/// The hang guard of a phase (never a bar): the ray tier's pipeline builds are `pending` too, and a phase waits for
/// them (their consumers' pipelines would otherwise land in the next phase's count).
static constexpr int kPhaseCap = 3000;

struct PhaseCounts {
    unsigned foreground = 0;
    unsigned long long psos = 0;
    double psoMs = 0.0;
    int frames = 0;
    bool quiet = false;
    unsigned pending = 0;      ///< at the phase's end: the background compiler's and the ray builds'
    unsigned rayBuilds = 0;    ///< ...of them, the ray pipeline builds in flight
};

static PhaseCounts runPhase(Engine *e)
{
    const AsyncShaderStats before = e->published().shaders;
    PhaseCounts c;
    unsigned long long lastPsos = before.graphicsPsos;
    int quiet = 0;
    for (; c.frames < kPhaseCap; ++c.frames) {
        enginetest::frames(e, 1);
        const AsyncShaderStats now = e->published().shaders;
        quiet = (now.pending == 0u && now.graphicsPsos == lastPsos) ? quiet + 1 : 0;
        lastPsos = now.graphicsPsos;
        if (c.frames >= kQuietFrames && quiet >= kQuietFrames) { c.quiet = true; ++c.frames; break; }
    }
    const AsyncShaderStats after = e->published().shaders;
    c.foreground = after.compiledInForeground - before.compiledInForeground;
    c.psos = after.graphicsPsos - before.graphicsPsos;
    c.psoMs = after.graphicsPsoMs - before.graphicsPsoMs;
    c.pending = after.pending;
    c.rayBuilds = after.rayBuildsInFlight;
    return c;
}

/// `picture` (the lead's verification, ss7-smaa-picture): DOES A PRESET CHANGE REACH THE PICTURE? The low-level
/// renderable hash holds the macroblock, the blendblock and the pass id only (OgreHlmsLowLevel.cpp calculateHashFor),
/// so a quad's pipeline built at the first SMAA frame may keep the old programs after initSmaa reloads them. Three
/// fresh engines (the real destroy between them) and a fourth with SMAA off first, each: one emissive cube rotated 2
/// degrees on black (test_engine's smaa_smooths_edges scene at a SHALLOW angle: its silhouette's runs are ~29 px long,
/// past Low's 4 search steps (two pixels a step) and within High's 16 and Ultra's 32 - SMAA.hlsl:312-330), the FIRST preset's picture,
/// then the SECOND's.
/// Exact display codes compared: fresh 0 vs fresh 3 must differ (else the instrument proves nothing); a switched
/// picture must equal the fresh picture of its own preset.
static int pictureMode()
{
    std::printf("== engine.smaa_up_front picture: does a preset change reach the picture?\n");
    struct Run { int first, second; Image a, b; bool ok = false; };
    Run runs[] = { { 0, 3 }, { 3, 0 }, { 2, 0 }, { -1, 3 } };
    for (Run &r : runs) {
        EngineConfig cfg = enginetest::testEngineConfig("test-engine-smaa-picture-ogre.log",
                                                        enginetest::EngineOverrides().realDestroy());
        std::string err;
        auto engine = Engine::create(cfg, err);
        if (!engine) { std::printf("FAIL: engine (%s)\n", err.c_str()); return 1; }
        Engine *e = engine.get();
        View *v = e->createOffscreenView("smaa-picture", 128, 128, Colour(0, 0, 0), OffscreenContract::Live);
        Scene *s = e->createScene("smaa-picture");
        if (!v || !s) { std::printf("FAIL: view/scene (%s)\n", e->lastError().c_str()); return 1; }
        v->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
        PbrParams glow; glow.albedo = Colour(0, 0, 0); glow.emissive = Colour(1.0f, 1.0f, 1.0f);
        const MaterialId mat = s->createPbrMaterial(glow);
        const NodeId n = s->createNode();
        const bool built = s->attachMesh(n, cube, mat);
        s->setNodeTransform(n, Vec3(0, 0, 0), Quat(0, 0, 0.0174524f, 0.9998477f), Vec3(0.9f, 0.9f, 0.9f));
        enginetest::testCameraLookAt(v, Vec3(0, 0, 3.0f), Vec3(0, 0, 0));
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.smaaPreset = r.first;
        v->setPostFx(fx);
        enginetest::frames(e, 4);
        const bool readA = v->readPixels(r.a);
        fx.smaaPreset = r.second;
        v->setPostFx(fx);
        enginetest::frames(e, 4);
        const bool readB = v->readPixels(r.b);
        r.ok = built && readA && readB && !r.a.rgba.empty() && r.a.rgba.size() == r.b.rgba.size();
        std::printf("    engine %d -> %d: %s\n", r.first, r.second, r.ok ? "read" : "FAILED to draw or read");
        enginetest::poseRegistry().erase(s);
        engine.reset();
    }
    for (const Run &r : runs) if (!r.ok) return 1;
    const auto differ = [](const Image &x, const Image &y) {
        int n = 0;
        for (size_t i = 0; i + 3 < x.rgba.size(); i += 4)
            if (x.rgba[i] != y.rgba[i] || x.rgba[i + 1] != y.rgba[i + 1] || x.rgba[i + 2] != y.rgba[i + 2]) ++n;
        return n;
    };
    const Image &fresh0 = runs[0].a, &fresh3 = runs[1].a, &fresh2 = runs[2].a;
    const Image &sw3 = runs[0].b, &sw0from3 = runs[1].b, &sw0from2 = runs[2].b;
    const Image &off = runs[3].a, &sw3fromOff = runs[3].b;
    std::printf("    SMAA off vs fresh 0 %d, vs fresh 3 %d, vs off -> 3 %d\n", differ(off, fresh0), differ(off, fresh3),
                differ(off, sw3fromOff));
    std::printf("    pixels differing: fresh 0 vs fresh 3 %d, fresh 2 vs fresh 0 %d\n", differ(fresh0, fresh3),
                differ(fresh2, fresh0));
    std::printf("    0 -> 3: vs fresh 3 %d, vs fresh 0 %d\n", differ(sw3, fresh3), differ(sw3, fresh0));
    std::printf("    3 -> 0: vs fresh 0 %d, vs fresh 3 %d\n", differ(sw0from3, fresh0), differ(sw0from3, fresh3));
    std::printf("    2 -> 0: vs fresh 0 %d, vs fresh 2 %d\n", differ(sw0from2, fresh0), differ(sw0from2, fresh2));
    CHECK(differ(off, fresh0) > 0 && differ(off, fresh3) > 0, "the instrument: SMAA changes this edge at 0 and at 3");
    CHECK(differ(fresh0, fresh3) > 0, "the instrument: presets 0 and 3 draw this edge differently when fresh");
    CHECK(differ(sw3, fresh3) == 0, "a change 0 -> 3 draws preset 3's picture");
    CHECK(differ(sw0from3, fresh0) == 0, "a change 3 -> 0 draws preset 0's picture");
    CHECK(differ(sw0from2, fresh0) == 0, "a change 2 -> 0 draws preset 0's picture");
    std::printf("engine.smaa_up_front picture: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "picture") == 0) return pictureMode();
    const bool keep = argc > 1 && std::strcmp(argv[1], "--keep") == 0;
    std::printf("== engine.smaa_up_front: no SMAA program is compiled in a frame\n");
    // ITS OWN CACHE, EMPTY AT EVERY RUN (frame.monitor_pop's pattern): an app's first launch.
    const std::string cacheDir = std::filesystem::current_path().string() + "/smaa-up-front-shadercache";
    if (!keep) {
        std::error_code ec;
        std::filesystem::remove_all(cacheDir, ec);
    }
    EngineConfig cfg = enginetest::testEngineConfig("test-engine-smaa-up-front-ogre.log",
                                                    enginetest::EngineOverrides().shaderCacheDir(cacheDir));
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    View *view = e->createOffscreenView("smaa", 128, 128, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *scene = e->createScene("smaa");
    if (!view || !scene) { std::printf("FAIL: view/scene (%s)\n", e->lastError().c_str()); return 1; }
    view->setAsyncShaders(true);
    view->setScene(scene);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 1.5f, 4.0f), Vec3(0.0f, 0.5f, 0.0f));
    const NodeId box = enginetest::addTestCube(scene, Colour(0.8f, 0.3f, 0.2f), 0.0f, 0.6f);
    const bool built = box && enginetest::addDirectionalLight(scene, Vec3(0.3f, -1.0f, 0.2f), 2.0f) != 0;
    CHECK(built, "the scene built: a cube and a sun (%s)", e->lastError().c_str());
    if (!built) return 1;

    PostFxDesc fx;
    fx.allowOffscreen = true;
    view->setPostFx(fx);
    const PhaseCounts a = runPhase(e);
    std::printf("    phase A (the chain, no SMAA; its counts include the engine's creation, published at the first "
                "frame): %d frames, quiet %d (pending %u, ray builds %u); %u shader(s) compiled off the background "
                "compiler, %llu graphics pipeline(s) %.2f ms\n", a.frames, a.quiet ? 1 : 0, a.pending, a.rayBuilds,
                a.foreground, a.psos, a.psoMs);
    CHECK(a.quiet, "phase A settled within %d frames", kPhaseCap);

    struct Ask { int preset; const char *name; };
    const Ask asks[] = { { 2, "B (preset 2, High)" }, { 0, "C (preset 0, Low)" } };
    for (const Ask &ask : asks) {
        fx.smaaPreset = ask.preset;
        view->setPostFx(fx);
        const PhaseCounts p = runPhase(e);
        std::printf("    phase %s: %d frames, quiet %d (pending %u, ray builds %u); SMAA PIPELINES BUILT IN FRAMES: %llu, "
                    "%.2f ms (driver time)\n", ask.name, p.frames, p.quiet ? 1 : 0, p.pending, p.rayBuilds, p.psos,
                    p.psoMs);
        CHECK(p.quiet, "phase %s settled within %d frames", ask.name, kPhaseCap);
        CHECK(p.foreground == 0u, "(1) phase %s compiled %u shader(s) in its frames (bar 0: every preset's programs "
                                  "were compiled where the engine parsed the materials)", ask.name, p.foreground);
    }
    enginetest::poseRegistry().erase(scene);
    if (failures) std::printf("engine.smaa_up_front: FAIL (%d)\n", failures);
    else std::printf("engine.smaa_up_front: PASS\n");
    return failures ? 1 : 0;
}
