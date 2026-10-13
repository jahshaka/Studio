// shadercache.fixture_warm — AN ENGINE FIXTURE BOOTS ON THE WARM SHARED CACHE, AND A CUSTOM PIECE NEVER COSTS IT
// (ENGINE-UPGRADE-1 stage 1, BOOT). A law in counts, one process, three engine boots through the ONE fixture config
// helper (enginetesthelpers.h testEngineConfig — no override: the harness hook decides the cache):
//   1. the harness gave the fixture its cache: jah_gpu_row CLASS engine runs the row through fresh_home.sh --warm and
//      exports the home's linked shared cache as JAHSHAKA_SHADER_CACHE_DIR; the hook copies it into the config;
//   2. boot one renders a lit cube with a plain material AND a material carrying a custom piece (a file in a folder
//      only this boot registers — a project's piece, a shader graph's emitted piece), then saves the cache;
//   3. boot two renders the same scene WITHOUT the piece: every shader it needs is in the cache boot one wrote, so it
//      compiles NOTHING (compiledThisRun == 0) and loads the boot's low-level set and its Hlms shaders from the cache
//      (loadedThisRun >= 80);
//   4. ...and the piece cost its OWN entries only: boot two's Ogre log (the fixture's own file, the cache's evidence —
//      the counters above come from the same sentences) names the dropped entries (>= 1) and the loaded rest (>= 1),
//      and never "load failed";
//   5. a third boot, the same scene again, compiles nothing either: boot two left the cache whole.
// On the base (82d38ccb5) this row is RED twice over, measured 2026-10-13 with this file built against the base's
// libJahshakaEngined.a and Ogre install (a13a57617):
//   - as the base is (the hook a no-op): no cache at all — boot two compiled=89 fromCache=0, boot three compiled=89;
//   - with only the hook (the base engine + fork): boot two logged "load failed (Cannot locate a resource group called
//     'Jahshaka' for resource 'fixture_warm_tint.piece_ps.glsl') — starting cold", wiped the files (744,522 -> 184,978
//     bytes) and still compiled 0 from what it had read; boot three compiled=89 fromCache=0 — the whole cache, a
//     user's included, gone after any session that used a shader-graph material.
// On the branch: boots two and three compiled=0 fromCache=89, the piece's 1 entry dropped and the rest loaded.
// The fix: the fork's HlmsDiskCache::applyTo drops only the entries that use a missing piece (fork d7a54a8c6) and the
// engine creates the "Jahshaka" group before the load (OgreEngine.cpp).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>

using namespace jahshaka::engine;

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } std::fflush(stdout); } while (0)

namespace {

/// One boot: a 64x64 offscreen view of a lit cube; `piecePath` non-empty = a second cube whose material carries
/// that pixel piece. Three frames, the cache saved and its write waited for, the stats returned.
ShaderCacheStats boot(const char *what, const std::string &piecePath)
{
    ShaderCacheStats stats;
    std::string error;
    auto e = Engine::create(enginetest::testEngineConfig("test_fixture_warm-ogre.log"), error);
    CHECK(e != nullptr, "[%s] Engine::create (%s)", what, error.c_str());
    if (!e) return stats;
    View *v = e->createOffscreenView("view", 64, 64, Colour(0.0f, 0.0f, 0.0f));
    Scene *s = v ? e->createScene("scene") : nullptr;
    if (v && s) {
        s->setAtomDrawEnabled(false);
        s->setAmbient(Colour(0.3f, 0.3f, 0.3f), Colour(0.2f, 0.2f, 0.2f));
        enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.4f, -1.0f), 2.0f);
        const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
        PbrParams params;
        params.albedo = Colour(0.8f, 0.8f, 0.8f);
        params.roughness = 0.6f;
        const NodeId plain = s->createNode();
        s->attachMesh(plain, mesh, s->createPbrMaterial(params));
        if (!piecePath.empty()) {
            const MaterialId pm = s->createPbrMaterial(params);
            CHECK(s->setMaterialCustomPiece(pm, piecePath, CustomPieceStage::PixelPreLights),
                  "[%s] the custom piece binds (%s)", what, e->lastError().c_str());
            const NodeId pieced = s->createNode();
            s->attachMesh(pieced, mesh, pm);
        }
        v->setScene(s);
        enginetest::testCameraLookAt(v, Vec3(0.0f, 0.0f, 4.0f), Vec3(0.0f, 0.0f, 0.0f));
        for (int i = 0; i < 3; ++i) e->renderOneFrame();
    }
    e->saveShaderCache();
    CHECK(e->flushShaderCache(30000), "[%s] the cache's write finished", what);
    stats = e->shaderCacheStats();
    if (s) e->destroyScene(s);
    if (v) e->destroyView(v);
    e.reset();
    std::printf("    [%s] compiled=%u fromCache=%u hlms=%u files=%u bytes=%llu\n", what, stats.compiledThisRun,
                stats.loadedThisRun, stats.hlmsCachesLoaded, stats.files, (unsigned long long)stats.sizeBytes);
    std::fflush(stdout);
    return stats;
}

}   // namespace

int main()
{
    std::printf("== shadercache.fixture_warm: a fixture boots warm; a custom piece never costs the cache\n");
    const char *dirEnv = std::getenv("JAHSHAKA_SHADER_CACHE_DIR");
    const std::string dir = dirEnv ? dirEnv : "";
    CHECK(!dir.empty(), "the harness gave the fixture its cache dir (JAHSHAKA_SHADER_CACHE_DIR)");
    if (dir.empty()) {
        std::printf("shadercache.fixture_warm: FAILED\n");
        return 1;
    }
    // The piece lives beside the cache, in the row's home (wiped after the run), in a folder ONLY boot one registers.
    // The cache dir need not exist yet (a cold home): the folder is made path by path, never through "..".
    std::string pieceDir = dir;
    while (pieceDir.size() > 1 && pieceDir.back() == '/') pieceDir.pop_back();
    pieceDir = pieceDir.substr(0, pieceDir.find_last_of('/')) + "/fixture-warm-piece";
    for (size_t at = pieceDir.find('/', 1); ; at = pieceDir.find('/', at + 1)) {
        ::mkdir(pieceDir.substr(0, at).c_str(), 0755);
        if (at == std::string::npos) break;
    }
    const std::string piece = pieceDir + "/fixture_warm_tint.piece_ps.glsl";
    {
        std::ofstream f(piece, std::ios::binary | std::ios::trunc);
        // No at-sign in any comment here (the Hlms parser reads them).
        f << "@piece( custom_ps_emissive )\n"
             "{\n"
             "\tfinalColour += midf3_c( float3( 0.1, 0.2, 0.3 ) );\n"
             "}\n"
             "@end\n";
    }
    const ShaderCacheStats one = boot("boot one, with the piece", piece);
    CHECK(one.enabled, "boot one ran with the cache on (%s)", one.dir.c_str());
    const ShaderCacheStats two = boot("boot two, without it", std::string());
    CHECK(two.compiledThisRun == 0, "boot two compiled nothing: compiledThisRun = %u (the bar: 0)", two.compiledThisRun);
    CHECK(two.loadedThisRun >= 80, "boot two loaded its shaders from the cache: loadedThisRun = %u (the bar: >= 80)",
          two.loadedThisRun);
    // 4. the piece's entries dropped, the rest of the Hlms cache loaded — read from boot two's own Ogre log
    {
        std::ifstream log("test_fixture_warm-ogre.log");
        std::stringstream ss;
        ss << log.rdbuf();
        const std::string text = ss.str();
        unsigned dropped = 0, kept = 0;
        const std::string key = "HlmsDiskCache: dropped ";
        for (size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + 1)) {
            unsigned d = 0, k = 0;
            if (std::sscanf(text.c_str() + at, "HlmsDiskCache: dropped %u entries that use a missing custom piece "
                                                "file; %u load.", &d, &k) == 2) {
                dropped += d;
                kept += k;
            }
        }
        CHECK(!text.empty(), "boot two's Ogre log was read (test_fixture_warm-ogre.log)");
        CHECK(text.find("shader cache: load failed") == std::string::npos,
              "boot two's cache load did not fail (no \"load failed ... starting cold\")");
        CHECK(dropped >= 1 && kept >= 1,
              "the missing piece cost its own entries only: %u dropped, %u loaded (the bar: >= 1 and >= 1)", dropped, kept);
    }
    // 5. ...and boot two left the cache whole: a third boot, the same scene, compiles nothing either. (A failed load
    //    keeps what it already read in memory — the base's boot two compiled 0 — but the writer WIPED the files and
    //    saved only what it could; the cost lands on the NEXT boot.)
    const ShaderCacheStats three = boot("boot three, without it", std::string());
    CHECK(three.compiledThisRun == 0, "boot three compiled nothing: compiledThisRun = %u (the bar: 0)",
          three.compiledThisRun);
    CHECK(three.loadedThisRun >= 80, "boot three loaded its shaders from the cache: loadedThisRun = %u (the bar: >= 80)",
          three.loadedThisRun);
    std::printf("shadercache.fixture_warm: %s\n", gFailures ? "FAILED" : "PASSED");
    return gFailures ? 1 : 0;
}
