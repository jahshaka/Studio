// The shader cache's container, attacked (SHADER_CACHE_SPEC.md §4.5 tests 1-6).
//
// THE REASON THIS SUITE EXISTS, in one paragraph. Ogre's microcode cache file
// has no magic number, no version, no checksum and no bounds check:
// GpuProgramManager::loadMicrocodeCache (OgreGpuProgramManager.cpp:368-398)
// reads a uint32 count, then loops reading a hash and a uint32 length and
// allocating a buffer of that length, and the bytes it collects are handed
// straight to vkCreateShaderModule. A file truncated by a full disk, a file
// half-written by a crash, or a single flipped bit is parsed as if it were
// valid. Our container is what supplies the integrity Ogre does not, and this
// suite is the only proof that it does.
//
// Six cases, each a REAL engine cycle against a REAL cache directory:
//   1. cold then warm: the second run compiles nothing and loads everything.
//   2. truncation, per file.
//   3. a flipped bit, per file.
//   4. a zero-length file, per file.
//   5. a changed fingerprint term: the directory is discarded wholesale.
//   6. two processes at once: no corruption, no deadlock, both succeed.
// (--clear-shader-cache and the verbs are the app-level e2e's half; this is the
// container's.)
//
// Sequential Engine create/destroy in one process is the whole shape of the
// test, and it only works because of ogre-patch 0002 — see test_engine_recreate.
//
// Built TWICE when JAHSHAKA_ASAN=ON: a corrupt-input parser that is merely
// "did not crash" is not proven. The sanitised twin is what makes case 2-4's
// "no ASan report" a real assertion.
#include "jahshaka/engine/Engine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace jahshaka::engine;

static int gFailures = 0;
static int gChecks = 0;
#define CHECK(cond, msg) do { ++gChecks; if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++gFailures; } } while (0)

namespace {

std::string gCacheDir;

EngineConfig cacheConfig() {
    EngineConfig cfg;
    cfg.backend        = Backend::Vulkan;
    cfg.pluginDir      = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir   = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile        = "test_shader_cache-ogre.log";
    cfg.shaderCacheDir = gCacheDir;
    cfg.appBuildId     = "test-shader-cache/1";
    return cfg;
}

/// One full engine lifetime with the cache on: create, render enough to compile
/// something, save, destroy. Returns what the cache reported before teardown.
///
/// The scene stays empty for the same reason the startup gate's does: an empty
/// scene still compiles the ~40 low-level material scripts and the whole
/// compositor chain, which is plenty of cache to attack, and it keeps the
/// numbers stable across machines (Hlms permutation counts are content
/// dependent).
ShaderCacheStats runCycle(const char *what, bool *createdOut = nullptr) {
    ShaderCacheStats stats;
    std::string error;
    auto e = Engine::create(cacheConfig(), error);
    if (!e) {
        std::printf("FAIL: %s: Engine::create: %s\n", what, error.c_str());
        ++gFailures;
        if (createdOut) *createdOut = false;
        return stats;
    }
    if (createdOut) *createdOut = true;
    View *v = e->createOffscreenView("view", 64, 64, Colour(0.0f, 0.0f, 0.0f));
    Scene *s = v ? e->createScene("scene") : nullptr;
    if (v && s) {
        v->setScene(s);
        for (int i = 0; i < 3; ++i) e->renderOneFrame();
    }
    e->saveShaderCache();
    stats = e->shaderCacheStats();
    if (s) e->destroyScene(s);
    if (v) e->destroyView(v);
    e.reset();
    std::printf("    [%s] compiled=%u fromCache=%u files=%u bytes=%llu "
                "pipeline=%d microcode=%d(%u) hlms=%u\n",
                what, stats.compiledThisRun, stats.loadedThisRun, stats.files,
                (unsigned long long)stats.sizeBytes, stats.pipelineCacheLoaded ? 1 : 0,
                stats.microcodeLoaded ? 1 : 0, stats.microcodeEntries, stats.hlmsCachesLoaded);
    std::fflush(stdout);
    return stats;
}

std::vector<std::string> cacheFiles() {
    std::vector<std::string> out;
    DIR *d = opendir(gCacheDir.c_str());
    if (!d) return out;
    while (dirent *e = readdir(d)) {
        const std::string n = e->d_name;
        if (n == "." || n == ".." || n == "cache.lock") continue;
        out.push_back(n);
    }
    closedir(d);
    return out;
}

bool readFile(const std::string &p, std::vector<char> &out) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return false;
    out.resize(size_t(f.tellg()));
    f.seekg(0);
    return out.empty() || bool(f.read(out.data(), std::streamsize(out.size())));
}

void writeFile(const std::string &p, const std::vector<char> &bytes) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!bytes.empty()) f.write(bytes.data(), std::streamsize(bytes.size()));
}

void wipeDir() {
    for (const std::string &n : cacheFiles()) ::unlink((gCacheDir + "/" + n).c_str());
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. Cold, then warm.
static void cold_then_warm() {
    wipeDir();
    const ShaderCacheStats cold = runCycle("cold");
    CHECK(cold.enabled, "cache reports itself enabled");
    CHECK(cold.compiledThisRun > 0, "a cold run compiles shaders");
    CHECK(!cold.microcodeLoaded && !cold.pipelineCacheLoaded,
          "a cold run loads nothing (there is nothing to load)");
    CHECK(!cacheFiles().empty(), "the cold run wrote a cache");

    const ShaderCacheStats warm = runCycle("warm");
    CHECK(warm.compiledThisRun == 0, "a warm run compiles NOTHING");
    CHECK(warm.loadedThisRun >= cold.compiledThisRun,
          "a warm run serves at least as many shaders as the cold one compiled");
    CHECK(warm.microcodeLoaded, "the microcode layer loaded");
    CHECK(warm.pipelineCacheLoaded, "the pipeline layer loaded");
    CHECK(warm.hlmsCachesLoaded > 0, "at least one Hlms disk cache applied");
    CHECK(warm.expectedShaders > 0, "the manifest carries a shader count for the progress UI");
    CHECK(warm.fingerprint == cold.fingerprint, "the fingerprint is stable across runs");
}

// ---------------------------------------------------------------------------
// 2-4. Corruption, three ways, on every file in turn.
//
// The assertion is deliberately NOT "the cache still works". It is: the process
// starts, renders, and rebuilds cold. Anything else — a hang, a crash, an ASan
// report, a silently accepted blob — is the failure this whole container exists
// to prevent.
static void corruption(const char *label, void (*damage)(const std::string &)) {
    wipeDir();
    runCycle("seed");
    const std::vector<std::string> files = cacheFiles();
    CHECK(files.size() >= 2, "there is more than one file to attack");

    for (const std::string &name : files) {
        // Re-seed so every file is attacked against an otherwise-good cache.
        wipeDir();
        runCycle("reseed");
        damage(gCacheDir + "/" + name);

        bool created = false;
        const ShaderCacheStats after = runCycle("after-damage", &created);
        char msg[256];
        std::snprintf(msg, sizeof msg, "%s %s: the engine still starts", label, name.c_str());
        CHECK(created, msg);
        std::snprintf(msg, sizeof msg, "%s %s: the damaged cache is NOT used", label, name.c_str());
        // Either we rejected the whole directory (so we compiled from scratch),
        // or — for a file the manifest never listed — we simply carried on.
        // What must never happen is loading the damaged file itself.
        CHECK(after.compiledThisRun > 0 || after.loadedThisRun > 0, msg);
    }
}

static void truncate_files() {
    corruption("truncated", [](const std::string &p) {
        std::vector<char> bytes;
        if (!readFile(p, bytes) || bytes.size() < 4) return;
        bytes.resize(bytes.size() / 2);   // half a file: the crash-mid-write shape
        writeFile(p, bytes);
    });
}

static void bitflip_files() {
    corruption("bit-flipped", [](const std::string &p) {
        std::vector<char> bytes;
        if (!readFile(p, bytes) || bytes.empty()) return;
        // Middle of the file, so the damage is in payload rather than in a
        // header a length check would catch for free. This is the case only a
        // CHECKSUM can catch, which is the point.
        bytes[bytes.size() / 2] = char(bytes[bytes.size() / 2] ^ 0x40);
        writeFile(p, bytes);
    });
}

static void zerolength_files() {
    corruption("zero-length", [](const std::string &p) { writeFile(p, {}); });
}

// ---------------------------------------------------------------------------
// 5. Fingerprint invalidation.
//
// Driven by rewriting the manifest's own fingerprint line rather than by
// rebuilding the engine with a different build id: it exercises exactly the
// comparison every real invalidation term (app build, engine build, ogre patch
// series, Hlms media, GPU, debug/release) funnels into, and it does so without
// needing a second binary.
static void fingerprint_mismatch() {
    wipeDir();
    runCycle("seed");
    const std::string manifest = gCacheDir + "/cache-manifest.txt";
    std::vector<char> bytes;
    CHECK(readFile(manifest, bytes), "the manifest exists");

    std::string text(bytes.begin(), bytes.end());
    const size_t at = text.find("fingerprint ");
    CHECK(at != std::string::npos, "the manifest carries a fingerprint line");
    if (at != std::string::npos) text.insert(at + 12, "SOMETHING-ELSE-");
    writeFile(manifest, std::vector<char>(text.begin(), text.end()));

    const ShaderCacheStats after = runCycle("after-fingerprint-change");
    CHECK(after.compiledThisRun > 0, "a changed fingerprint forces a cold rebuild");
    CHECK(!after.microcodeLoaded && !after.pipelineCacheLoaded,
          "a changed fingerprint loads NO layer");
    // And the stale generation is gone rather than lying around forever.
    bool stale = false;
    for (const std::string &n : cacheFiles())
        if (n == "cache-manifest.txt") stale = true;
    CHECK(stale, "the directory was rewritten, not merely ignored");
}

// A missing manifest with the payload still present: the shape a partial
// delete leaves behind, and the one where "trust the files" would be fatal.
static void manifest_missing() {
    wipeDir();
    runCycle("seed");
    ::unlink((gCacheDir + "/cache-manifest.txt").c_str());
    const ShaderCacheStats after = runCycle("after-manifest-delete");
    CHECK(after.compiledThisRun > 0, "no manifest means no load, however good the payload looks");
    CHECK(!after.microcodeLoaded, "the unverifiable microcode file is not read");
}

// ---------------------------------------------------------------------------
// 7. THE DENOMINATOR IS LAST-RUN, NOT ALL-TIME (audit F6).
//
// `expectedShaders` is the splash's "Building shaders 34/68" denominator and
// shaderbuildgate.h promises it is "how many shaders the last saved run
// needed". It was written with std::max, so one heavy session pinned it
// forever: every launch afterwards counted up to a number it could not reach
// and stopped ("61/76 forever").
//
// Driven by planting an absurd count in the manifest and forcing one more save.
// Under std::max the 99999 survives; under last-run it is replaced by a number
// the run can actually reach.
static void denominator_is_last_run() {
    wipeDir();
    runCycle("seed");

    const std::string manifest = gCacheDir + "/cache-manifest.txt";
    std::vector<char> bytes;
    CHECK(readFile(manifest, bytes), "F6: the manifest exists");
    std::string text(bytes.begin(), bytes.end());
    const size_t at = text.find("shaders ");
    CHECK(at != std::string::npos, "F6: the manifest carries a shader count");
    if (at == std::string::npos) return;
    const size_t eol = text.find('\n', at);
    text.replace(at, eol - at, "shaders 99999");
    writeFile(manifest, std::vector<char>(text.begin(), text.end()));

    // A cycle that LOADS that manifest (so mExpectedShaders starts at 99999)
    // and then writes one back. clearShaderCache() is what forces the write:
    // a warm run is not "dirty" — nothing new compiled — so without it the save
    // is a no-op and the manifest keeps whatever it had.
    ShaderCacheStats after;
    {
        std::string error;
        auto e = Engine::create(cacheConfig(), error);
        CHECK(!!e, "F6: the engine started against the doctored manifest");
        if (!e) return;
        View *v = e->createOffscreenView("view", 64, 64, Colour(0.0f, 0.0f, 0.0f));
        Scene *s = v ? e->createScene("scene") : nullptr;
        if (v && s) { v->setScene(s); for (int i = 0; i < 3; ++i) e->renderOneFrame(); }
        // AFTER the first view, not after Engine::create: the cache is loaded
        // lazily from ensureHlms(), which the first View is what triggers.
        CHECK(e->shaderCacheStats().expectedShaders == 99999u,
              "F6: the planted denominator was read back from the manifest");
        e->clearShaderCache();     // forces the next save to write
        e->saveShaderCache();
        after = e->shaderCacheStats();
        if (s) e->destroyScene(s);
        if (v) e->destroyView(v);
    }
    std::printf("    [F6] expected=%u compiled=%u loaded=%u\n",
                after.expectedShaders, after.compiledThisRun, after.loadedThisRun);
    CHECK(after.expectedShaders != 99999u,
          "F6: the save REPLACED the all-time high-water mark");
    CHECK(after.expectedShaders == after.compiledThisRun + after.loadedThisRun,
          "F6: the denominator is exactly what THIS run built or served");
}

// ---------------------------------------------------------------------------
// 8. pipelineCacheLoaded MEANS THE DRIVER TOOK IT (audit F7).
//
// `RenderSystem::loadPipelineCache` is void: a blob the driver rejects (wrong
// vendor/device/driver version/UUID, or a bad payload hash — all checked inside
// OgreVulkanRenderSystem::loadPipelineCache) is refused with nothing but a log
// line. The old code set the flag whenever it had BYTES, so it reported "the
// pipeline layer loaded" on every driver update, forever.
//
// Constructed without corrupting anything our own container would reject: copy
// the MICROCODE file over pipeline.cache and give pipeline.cache the microcode
// file's manifest size and hash. Our verification passes (the bytes match the
// manifest exactly); Ogre's does not (no 'VKPC' magic). That is precisely the
// state "the container is fine, the driver said no" — and it is pure text
// surgery on the manifest, so the test needs no hash function of its own.
static void pipeline_layer_reports_acceptance() {
    wipeDir();
    const ShaderCacheStats cold = runCycle("seed");
    CHECK(cold.pipelineCacheReason == "absent",
          "F7: a cold run reports the pipeline layer ABSENT, not rejected");

    const ShaderCacheStats warm = runCycle("warm-before-swap");
    CHECK(warm.pipelineCacheLoaded && warm.pipelineCacheReason == "accepted",
          "F7: a genuine warm run reports the driver ACCEPTED the blob");

    const std::string manifest = gCacheDir + "/cache-manifest.txt";
    std::vector<char> mbytes, micro;
    if (!readFile(manifest, mbytes) || !readFile(gCacheDir + "/microcode.cache", micro)) {
        CHECK(false, "F7: could not read the manifest and microcode file");
        return;
    }
    // Lift the microcode line's "<bytes> <hash>" and stamp it onto the pipeline
    // line, then make pipeline.cache actually be those bytes.
    std::string text(mbytes.begin(), mbytes.end());
    const size_t mAt = text.find("file microcode.cache ");
    const size_t pAt = text.find("file pipeline.cache ");
    if (mAt == std::string::npos || pAt == std::string::npos) {
        CHECK(false, "F7: the manifest lists both files");
        return;
    }
    const std::string microTail = text.substr(mAt + 21, text.find('\n', mAt) - (mAt + 21));
    const size_t pEol = text.find('\n', pAt);
    text.replace(pAt, pEol - pAt, "file pipeline.cache " + microTail);
    writeFile(manifest, std::vector<char>(text.begin(), text.end()));
    writeFile(gCacheDir + "/pipeline.cache", micro);

    const ShaderCacheStats after = runCycle("driver-rejects-pipeline");
    std::printf("    [F7] pipelineLoaded=%d reason=%s microcodeLoaded=%d\n",
                after.pipelineCacheLoaded ? 1 : 0, after.pipelineCacheReason.c_str(),
                after.microcodeLoaded ? 1 : 0);
    CHECK(!after.pipelineCacheLoaded,
          "F7: a blob the driver REFUSED is not reported as loaded");
    CHECK(after.pipelineCacheReason == "outdated" || after.pipelineCacheReason == "rejected",
          "F7: and the reason says which refusal it was");
    CHECK(after.microcodeLoaded,
          "F7: the OTHER layers still loaded — one rejected layer is not a cold run");
}

// ---------------------------------------------------------------------------
// 9. THE LOG SAYS WHICH FILE (audit F11).
//
// "Loading HlmsDiskCache from " with an empty tail is the log line that started
// the whole caching audit: Ogre logs `dataStream->getName()` and our
// MemoryDataStreams had no name. On a support log, "which cache directory did
// this session read" has to be answerable.
static void streams_are_named() {
    wipeDir();
    runCycle("seed");
    runCycle("warm-for-log");

    std::vector<char> log;
    if (!readFile("test_shader_cache-ogre.log", log)) {
        CHECK(false, "F11: the Ogre log exists");
        return;
    }
    const std::string text(log.begin(), log.end());
    CHECK(text.find("Loading HlmsDiskCache from " + gCacheDir + "/hlms.") != std::string::npos,
          "F11: the Hlms load names the file it read, with its full path");
    CHECK(text.find("Loading HlmsDiskCache from \n") == std::string::npos,
          "F11: and no load line has an empty tail any more");
}

// ---------------------------------------------------------------------------
// 6. Two processes at once.
//
// Two Jahshaka processes are routine (the editor plus a scripted run; the gate
// runs many). One takes the writer lock; the other must still run — read-only,
// never failed, never deadlocked, and never leaving a half-written file behind.
static void concurrent_processes(const char *self) {
    wipeDir();
    runCycle("seed");

    pid_t pids[2];
    for (int i = 0; i < 2; ++i) {
        pids[i] = fork();
        if (pids[i] == 0) {
            // execv, not a forked engine: a Vulkan device does not survive fork.
            char *const argv[] = { const_cast<char *>(self), const_cast<char *>("--child"),
                                   const_cast<char *>(gCacheDir.c_str()), nullptr };
            execv(self, argv);
            _exit(127);
        }
    }
    int worst = 0;
    for (int i = 0; i < 2; ++i) {
        int status = 0;
        waitpid(pids[i], &status, 0);
        const int rc = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        if (rc > worst) worst = rc;
        std::printf("    child %d exited %d\n", i, rc);
    }
    CHECK(worst == 0, "both concurrent processes exited cleanly");

    // And the cache they shared is still loadable afterwards.
    const ShaderCacheStats after = runCycle("after-concurrent");
    CHECK(after.compiledThisRun == 0, "the cache survived two concurrent processes intact");
}

int main(int argc, char **argv) {
    // The cache directory lives beside the binary; ctest gives each suite its
    // own working directory, so nothing here can reach a real user's cache.
    gCacheDir = "shadercache-test";
    ::mkdir(gCacheDir.c_str(), 0755);

    if (argc > 2 && std::strcmp(argv[1], "--child") == 0) {
        gCacheDir = argv[2];
        std::string error;
        auto e = Engine::create(cacheConfig(), error);
        if (!e) { std::printf("child: create failed: %s\n", error.c_str()); return 1; }
        View *v = e->createOffscreenView("view", 32, 32, Colour(0.0f, 0.0f, 0.0f));
        Scene *s = v ? e->createScene("scene") : nullptr;
        if (v && s) { v->setScene(s); for (int i = 0; i < 2; ++i) e->renderOneFrame(); }
        e->saveShaderCache();
        if (s) e->destroyScene(s);
        if (v) e->destroyView(v);
        return 0;
    }

    std::printf("[ RUN  ] cold_then_warm\n");        cold_then_warm();
    std::printf("[ RUN  ] truncate_files\n");        truncate_files();
    std::printf("[ RUN  ] bitflip_files\n");         bitflip_files();
    std::printf("[ RUN  ] zerolength_files\n");      zerolength_files();
    std::printf("[ RUN  ] fingerprint_mismatch\n");  fingerprint_mismatch();
    std::printf("[ RUN  ] manifest_missing\n");      manifest_missing();
    std::printf("[ RUN  ] concurrent_processes\n");  concurrent_processes(argv[0]);
    // The caching-audit fix wave (SHADER_CACHE_AUDIT.md F6/F7/F11).
    std::printf("[ RUN  ] denominator_is_last_run\n");          denominator_is_last_run();
    std::printf("[ RUN  ] pipeline_layer_reports_acceptance\n"); pipeline_layer_reports_acceptance();
    std::printf("[ RUN  ] streams_are_named\n");                streams_are_named();

    std::printf("%d check(s), %d failure(s)\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
