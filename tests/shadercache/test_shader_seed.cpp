// shadercache.seed — THE SHIPPED MICROCODE SEED'S CONTRACT, at the engine (COLD-BOOT-1 S1).
//
// EngineConfig::shaderCacheSeedDir is a read-only fallback for the microcode layer alone: read when the cache
// directory holds no microcode layer at this build's key, in that layer's slot of upstream's load order; never
// written, locked or wiped; a seed made at another key is a miss. The app's pair (test_shader_seed_boot.cpp) proves
// the gate's numbers; this proves every branch of the rule in seconds, each a REAL engine cycle (create, three
// frames of an empty scene — the low-level materials and the compositor chain, ~40 shaders — save, destroy) against
// real directories:
//   cold     no seed: "off", the cycle compiles its set (C, the cold count) and saves a microcode layer;
//   made     scripts/shadercache/make-seed.sh turns that directory into a seed (the ship's producer);
//   loaded   an empty directory + the seed: "loaded", the map is the seed's, glslang compiles NOTHING, and the
//            directory saves no microcode layer of its own (nothing new compiled: the seed stays its fallback);
//   unused   the cold cycle's directory (its own layer) + the seed: "unused", the seed not read;
//   stale    the seed's manifest at another microcode key: "stale", not read — C compiles;
//   corrupt  one byte of the seed's blob flipped: "corrupt" (the checksum), not read — C compiles;
//   absent   a seed directory that does not exist: "absent" — C compiles;
// and after all of them the seed is byte-identical, with no file added (no lock, no tmp, no save).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace jahshaka::engine;
namespace fs = std::filesystem;

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } } while (0)

namespace {

int gFrames = 0;   // the frames this suite rendered (its FRAMES declaration)

/// One engine lifetime against `cacheDir` with `seedDir` (empty = no seed): what the cache reported after the save.
ShaderCacheStats cycle(const char *what, const std::string &cacheDir, const std::string &seedDir)
{
    ShaderCacheStats stats;
    EngineConfig cfg = enginetest::testEngineConfig("test-shader-seed-ogre.log",
                                                    enginetest::EngineOverrides().shaderCacheDir(cacheDir));
    // THE SEED IS THIS SUITE'S SUBJECT, a config field the helper has no override for: stated on the config.
    cfg.shaderCacheSeedDir = seedDir;
    std::string err;
    auto e = Engine::create(cfg, err);
    if (!e) { std::printf("FAIL: %s: Engine::create: %s\n", what, err.c_str()); ++gFailures; return stats; }
    View *v = e->createOffscreenView("seed", 64, 64, Colour(0.0f, 0.0f, 0.0f));
    Scene *s = v ? e->createScene("seed") : nullptr;
    if (v && s) {
        v->setScene(s);
        enginetest::frames(e, 3);
        gFrames += 3;
    }
    e->saveShaderCache();
    const bool flushed = e->flushShaderCache(30000);
    e->request([&](Engine &x) { stats = x.shaderCacheStats(); });
    if (s) e->destroyScene(s);
    if (v) e->destroyView(v);
    e.reset();
    std::printf("    [%-7s] seed=%-8s seedEntries=%u compiled=%u fromCache=%u microcode=%d(%u) files=%u%s\n", what,
                stats.seedState.c_str(), stats.seedEntries, stats.compiledThisRun, stats.loadedThisRun,
                stats.microcodeLoaded ? 1 : 0, stats.microcodeEntries, stats.files, flushed ? "" : " (FLUSH TIMED OUT)");
    std::fflush(stdout);
    CHECK(flushed && stats.enabled, "%s: the cache is on and its write finished", what);
    return stats;
}

std::map<std::string, std::string> snapshot(const std::string &dir)
{
    std::map<std::string, std::string> m;
    std::error_code ec;
    for (const auto &ent : fs::directory_iterator(dir, ec)) {
        std::ifstream f(ent.path(), std::ios::binary);
        std::ostringstream b;
        b << f.rdbuf();
        m[ent.path().filename().string()] = b.str();
    }
    return m;
}

std::string fresh(const std::string &dir)
{
    std::error_code ec;
    fs::remove_all(dir, ec);
    return dir;
}

bool copyDir(const std::string &from, const std::string &to)
{
    std::error_code ec;
    fs::remove_all(to, ec);
    fs::copy(from, to, fs::copy_options::recursive, ec);
    return !ec;
}

unsigned entriesOf(const std::string &microcodeFile)
{
    std::ifstream f(microcodeFile, std::ios::binary);
    unsigned n = 0;
    f.read(reinterpret_cast<char *>(&n), sizeof n);
    return f ? n : 0u;
}

}  // namespace

int main()
{
    std::printf("== shadercache.seed: the shipped microcode seed's contract, engine cycles on real directories\n");
    const std::string root = fs::current_path().string() + "/seed-contract";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    const std::string seed = root + "/seed";

    // ---- cold: no seed -------------------------------------------------------------------------------------------
    const std::string dirCold = fresh(root + "/cold");
    const ShaderCacheStats cold = cycle("cold", dirCold, "");
    const unsigned C = cold.compiledThisRun;
    CHECK(cold.seedState == "off" && cold.seedDir.empty(), "no seed configured reads 'off' ('%s')", cold.seedState.c_str());
    CHECK(C > 0 && fs::exists(dirCold + "/microcode.cache"), "the cold cycle compiled its set (%u) and saved a microcode layer", C);

    // ---- made: the ship's producer -------------------------------------------------------------------------------
    const std::string mk = std::string("sh '") + JAHSHAKA_TEST_SOURCE_DIR + "/scripts/shadercache/make-seed.sh' '" +
                           dirCold + "' '" + seed + "'";
    const int mkRc = std::system(mk.c_str());
    CHECK(mkRc == 0 && fs::exists(seed + "/microcode.cache") && fs::exists(seed + "/cache-manifest.txt"),
          "make-seed.sh made the seed from the cold cycle's directory (rc %d)", mkRc);
    const unsigned seedFileEntries = entriesOf(seed + "/microcode.cache");
    const auto pristine = snapshot(seed);
    CHECK(pristine.size() == 2, "the seed is two files: the blob and its manifest (%zu)", pristine.size());

    // ---- loaded: an empty directory + the seed ------------------------------------------------------------------
    const std::string dirLoaded = fresh(root + "/loaded");
    const ShaderCacheStats loaded = cycle("loaded", dirLoaded, seed);
    CHECK(loaded.seedState == "loaded" && loaded.seedDir == seed, "an empty directory reads the seed ('%s')",
          loaded.seedState.c_str());
    CHECK(loaded.seedEntries == seedFileEntries && seedFileEntries > 0 && loaded.microcodeLoaded &&
              loaded.microcodeEntries == seedFileEntries,
          "the live map is the seed's whole blob (%u of %u entries)", loaded.seedEntries, seedFileEntries);
    CHECK(loaded.compiledThisRun == 0 && loaded.loadedThisRun > 0,
          "glslang compiled NOTHING: the seed served the set the cold cycle compiled (%u compiled, %u from it)",
          loaded.compiledThisRun, loaded.loadedThisRun);
    CHECK(!fs::exists(dirLoaded + "/microcode.cache"),
          "nothing new compiled, so the directory saved no microcode layer of its own: the seed stays its fallback");

    // ---- unused: the directory's own layer wins ------------------------------------------------------------------
    const ShaderCacheStats unused = cycle("unused", dirCold, seed);
    CHECK(unused.seedState == "unused" && unused.seedEntries == 0 && unused.microcodeLoaded,
          "a directory with its own microcode layer leaves the seed unread ('%s')", unused.seedState.c_str());
    CHECK(unused.compiledThisRun == 0, "...and compiles nothing (%u)", unused.compiledThisRun);

    // ---- stale: another microcode key is a miss ------------------------------------------------------------------
    const std::string seedStale = root + "/seed-stale";
    copyDir(seed, seedStale);
    {
        std::ifstream in(seedStale + "/cache-manifest.txt");
        std::string text, line;
        while (std::getline(in, line)) {
            if (line.rfind("file microcode.cache ", 0) == 0) line = line.substr(0, line.rfind(' ')) + " 00000000000000000000000000000000";
            text += line + "\n";
        }
        std::ofstream(seedStale + "/cache-manifest.txt", std::ios::trunc) << text;
    }
    const ShaderCacheStats stale = cycle("stale", fresh(root + "/stale"), seedStale);
    CHECK(stale.seedState == "stale" && !stale.microcodeLoaded && stale.seedEntries == 0,
          "a seed made at another microcode key is not read ('%s')", stale.seedState.c_str());
    CHECK(stale.compiledThisRun == C, "...a miss: the whole cold set compiles (%u of %u)", stale.compiledThisRun, C);

    // ---- corrupt: the checksum guards the seed as it guards the directory ----------------------------------------
    const std::string seedBad = root + "/seed-corrupt";
    copyDir(seed, seedBad);
    {
        std::fstream f(seedBad + "/microcode.cache", std::ios::in | std::ios::out | std::ios::binary);
        f.seekg(0, std::ios::end);
        const std::streamoff mid = f.tellg() / 2;
        char c = 0;
        f.seekg(mid);
        f.read(&c, 1);
        c = char(c ^ 0x5A);
        f.seekp(mid);
        f.write(&c, 1);
    }
    const ShaderCacheStats bad = cycle("corrupt", fresh(root + "/corrupt"), seedBad);
    CHECK(bad.seedState == "corrupt" && !bad.microcodeLoaded, "a flipped byte in the seed's blob is caught, not read ('%s')",
          bad.seedState.c_str());
    CHECK(bad.compiledThisRun == C, "...and the cold set compiles (%u of %u)", bad.compiledThisRun, C);

    // ---- absent: a build without a seed --------------------------------------------------------------------------
    const ShaderCacheStats absent = cycle("absent", fresh(root + "/absent"), root + "/no-such-seed");
    CHECK(absent.seedState == "absent" && !absent.microcodeLoaded, "a seed directory that does not exist reads 'absent' ('%s')",
          absent.seedState.c_str());
    CHECK(absent.compiledThisRun == C, "...and the cold set compiles (%u of %u)", absent.compiledThisRun, C);
    CHECK(!fs::exists(root + "/no-such-seed"), "the engine did not create the seed directory");

    // ---- never written --------------------------------------------------------------------------------------------
    CHECK(snapshot(seed) == pristine, "after the cycles that read or skipped it the seed is byte-identical, nothing added");

    std::printf("SHADER_SEED frames=%d cold=%u seedEntries=%u\n", gFrames, C, seedFileEntries);
    std::printf(gFailures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", gFailures);
    return gFailures ? 1 : 0;
}
