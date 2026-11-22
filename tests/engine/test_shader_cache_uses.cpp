// engine.shader_cache_uses.{microcode,hlms,compute} (SHADER-SYSTEM-1, the start preflight's F10; the lead's ruling (B),
// 2026-11-22; NULL Roots, no card, no display) - A CACHE DOES NOT GROW WITHOUT END. Every entry of the three layers that
// keep what a launch loaded (the microcode map, the Hlms recipes, the compute records) carries the launch it was last used
// in (irisgl engine/src/EntryUses.h: a sidecar of ours, Ogre's formats unchanged), and a save drops an entry no launch used
// for 30 launches. One row per layer, each through the layer's real path - a real ShaderCache saving and loading through
// the fork's GpuProgramManager / HlmsUnlit + HlmsDiskCache / HlmsCompute - over 100 launches:
//   (1) an entry USED EVERY LAUNCH survives all 100;
//   (2) an entry used at launch 1 and never again is still there after launch 30's save and GONE after launch 31's;
//   (3) A DAMAGED SIDECAR PRUNES NOTHING: an entry first used at launch 50 would go at launch 80's save; the sidecar is
//       damaged just before launch 80, so launch 80 drops nothing (the entry survives) and rewrites a whole table.
#include "EnginePrivate.h"

#include <OgreAbiUtils.h>
#include <OgreArchiveManager.h>
#include <OgreGpuProgramManager.h>
#include <OgreHlmsAsyncCompiler.h>
#include <OgreHlmsCompute.h>
#include <OgreHlmsComputeJob.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsUnlit.h>
#include <OgreResourceGroupManager.h>
#include <OgreRoot.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unistd.h>

using namespace jahshaka::engine::detail;

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } } while (0)

namespace {

class NullGpuProgramManager final : public Ogre::GpuProgramManager {
    Ogre::Resource *createImpl(const Ogre::String &, Ogre::ResourceHandle, const Ogre::String &, bool,
                               Ogre::ManualResourceLoader *, Ogre::GpuProgramType, const Ogre::String &) override {
        return nullptr;
    }
    Ogre::Resource *createImpl(const Ogre::String &, Ogre::ResourceHandle, const Ogre::String &, bool,
                               Ogre::ManualResourceLoader *, const Ogre::NameValuePairList *) override {
        return nullptr;
    }
};

void writeFile(const std::string &path, const std::string &bytes) { std::ofstream(path, std::ios::binary) << bytes; }
std::string readFile(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

struct Boot {
    std::unique_ptr<Ogre::Root> root;
    std::unique_ptr<NullGpuProgramManager> programs;   // after the Root: destroyed before it
    explicit Boot(const std::string &log) {
        static const Ogre::AbiCookie cookie = Ogre::generateAbiCookie();
        root = std::make_unique<Ogre::Root>(&cookie, "", "", log, "uses");
        root->loadPlugin(std::string(JAHSHAKA_TEST_PLUGIN_DIR) + "/RenderSystem_NULL", false, nullptr);
        if (root->getAvailableRenderers().empty()) throw std::runtime_error("no NULL render system");
        root->setRenderSystem(root->getAvailableRenderers().front());
        root->initialise(false);
        root->createRenderWindow("uses", 1u, 1u, false);
        programs = std::make_unique<NullGpuProgramManager>();
    }
    ~Boot() { programs.reset(); root.reset(); }
};

/// What a layer's toy does: present(name) reads the cache's state at a launch's start (after the load), use(name)
/// is that launch's use of an entry (the first use of a new one makes it).
struct Layer {
    std::function<void(int launch)> begin;                 // the launch's boot and the cache's load
    std::function<bool(const char *)> present;
    std::function<void(const char *)> use;
    std::function<void()> end;                             // the save and the launch's teardown
};

/// The 100 launches and the three laws, the same for every layer.
void runLaunches(const std::string &dir, Layer &l) {
    bool aAll = true, bAt31 = false, bAt32 = true, dAt81 = false, tableRebuilt = false;
    for (int launch = 1; launch <= 100; ++launch) {
        l.begin(launch);
        if (launch > 1) aAll = aAll && l.present("A");
        if (launch == 31) bAt31 = l.present("B");          // launch 30's save kept it
        if (launch == 32) bAt32 = l.present("B");          // launch 31's save dropped it
        if (launch == 81) dAt81 = l.present("D");          // launch 80 (a damaged table) dropped nothing
        l.use("A");
        if (launch == 1) l.use("B");
        if (launch == 50) l.use("D");
        l.end();
        if (launch == 79) {
            std::string t = readFile(dir + "/cache/entry-uses.bin");
            if (t.size() > 20) t[t.size() / 2] ^= 0x5a;     // one byte: the table's checksum no longer holds
            writeFile(dir + "/cache/entry-uses.bin", t);
        }
        if (launch == 80) {
            const std::string t = readFile(dir + "/cache/entry-uses.bin");
            UseTable table;
            tableRebuilt = readUseTable(t.data(), t.size(), table) && !table.empty();
        }
    }
    CHECK(aAll, "(1) the entry used every launch is there at every one of 100 launches");
    CHECK(bAt31 && !bAt32, "(2) the entry used only at launch 1: kept by launch 30's save (%d), gone after launch 31's (%d)",
          bAt31, !bAt32);
    CHECK(dAt81, "(3) a DAMAGED table prunes nothing: the entry launch 80's save would have dropped is there at launch 81");
    CHECK(tableRebuilt, "(3) ...and launch 80 rewrote a whole table");
}

// ---- the microcode layer: the fork's GpuProgramManager map, through a real ShaderCache's load and save ----
void microcode(const std::string &dir) {
    std::printf("\n== the microcode layer ==\n");
    Boot boot(dir + "/microcode-ogre.log");
    Ogre::GpuProgramManager &gpm = Ogre::GpuProgramManager::getSingleton();
    std::unique_ptr<ShaderCache> cache;
    auto blob = [&](const char *name) {
        Ogre::GpuProgramManager::Microcode m = gpm.createMicrocode(16u);
        std::memset(m->getPtr(), 0, 16u);
        std::memcpy(m->getPtr(), name, std::strlen(name));
        return m;
    };
    Layer l;
    l.begin = [&](int) { cache = std::make_unique<ShaderCache>(); cache->configure(dir + "/cache", std::string()); cache->load(boot.root.get()); };
    l.present = [&](const char *name) {
        Ogre::GpuProgramManager::MicrocodeMap map;
        gpm.copyMicrocodeCache(map);
        for (const auto &e : map)
            if (std::memcmp(e.second->getPtr(), name, std::strlen(name) + 1) == 0) return true;
        return false;
    };
    l.use = [&](const char *name) {
        const std::string source = std::string("source of ") + name;
        const Ogre::GpuProgramManager::Microcode *hit = nullptr;
        if (!gpm.getMicrocodeFromCache(source, &hit)) gpm.addMicrocodeToCache(source, blob(name));   // built = used
    };
    l.end = [&]() { cache->saveWhole(boot.root.get()); cache->flushWrites(30000u); cache->finishWrites(); cache.reset(); };
    runLaunches(dir, l);
}

// ---- the Hlms layer: a real HlmsUnlit's recipes through HlmsDiskCache, a draw's use by the fork's test door ----
void hlms(const std::string &dir) {
    std::printf("\n== the hlms layer ==\n");
    if (std::system(("mkdir -p '" + dir + "/unlit'").c_str()) != 0) throw std::runtime_error("mkdir");
    writeFile(dir + "/unlit/VertexShader_vs.glsl", "#version 450\nvoid main() {}\n");
    Boot boot(dir + "/hlms-ogre.log");
    Ogre::ArchiveManager &am = Ogre::ArchiveManager::getSingleton();
    Ogre::ArchiveVec libs;
    Ogre::Archive *data = am.load(dir + "/unlit", "FileSystem", true);
    auto *unlit = OGRE_NEW Ogre::HlmsUnlit(data, &libs);
    boot.root->getHlmsManager()->registerHlms(unlit);
    std::unique_ptr<ShaderCache> cache;
    auto props = [](const char *name) {
        Ogre::HlmsPropertyVec p;
        p.push_back(Ogre::HlmsProperty(Ogre::IdString(std::string("jah_uses_") + name), 1));
        return p;
    };
    Layer l;
    l.begin = [&](int) {
        unlit->reloadFrom(data, &libs);   // a new process holds no recipe until the cache's load restores them
        cache = std::make_unique<ShaderCache>();
        cache->configure(dir + "/cache", std::string());
        cache->load(boot.root.get());
    };
    l.present = [&](const char *name) {
        const Ogre::IdString key(std::string("jah_uses_") + name);
        for (const auto &recipe : unlit->getShaderCodeCache())
            for (const Ogre::HlmsProperty &p : recipe.mergedCache.setProperties)
                if (p.keyName == key) return true;
        return false;
    };
    l.use = [&](const char *name) {
        unlit->_jahTestRecipe(props(name), std::string("void main() { /* ") + name + " */ }\n", true);
    };
    l.end = [&]() { cache->saveWhole(boot.root.get()); cache->flushWrites(30000u); cache->finishWrites(); cache.reset(); };
    runLaunches(dir, l);
}

// ---- the compute layer: the fork's HlmsCompute records through the engine's adapter, a dispatch's hit or build ----
void compute(const std::string &dir) {
    std::printf("\n== the compute layer ==\n");
    if (std::system(("mkdir -p '" + dir + "/media'").c_str()) != 0) throw std::runtime_error("mkdir");
    for (const char *n : { "A", "B", "D" })
        writeFile(dir + "/media/JahUses" + n + ".glsl",
                  std::string("#version 450\n// ") + n + "\nlayout( local_size_x = 1 ) in;\nvoid main() {}\n");
    std::unique_ptr<Boot> boot;
    std::unique_ptr<ComputeCodeStore> store;
    std::unique_ptr<ShaderCache> cache;
    Layer l;
    l.begin = [&](int launch) {
        boot = std::make_unique<Boot>(dir + "/compute-ogre-" + std::to_string(launch % 2) + ".log");
        Ogre::HlmsCompute *hc = boot->root->getHlmsManager()->getComputeHlms();
        setForkComputeKeyHook(true);
        store = makeForkComputeStore(hc);
        cache = std::make_unique<ShaderCache>();
        cache->configure(dir + "/cache", std::string());
        cache->setComputeStore(store.get());
        cache->load(boot->root.get());
        Ogre::ResourceGroupManager &rgm = Ogre::ResourceGroupManager::getSingleton();
        rgm.addResourceLocation(dir + "/media", "FileSystem", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, false);
        rgm.initialiseAllResourceGroups(true);
        cache->prewarmCompute();   // no compiler running: the records are filed, their code compiles at a parse
    };
    l.present = [&](const char *name) {
        std::vector<ComputeRecordPtr> records;
        store->snapshot(records);
        for (const ComputeRecordPtr &r : records)
            if (r->name == std::string("uses/") + name) return true;
        return false;
    };
    l.use = [&](const char *name) {
        Ogre::HlmsCompute *hc = boot->root->getHlmsManager()->getComputeHlms();
        const std::string jobName = std::string("uses/") + name;
        Ogre::HlmsComputeJob *job = hc->createComputeJob(jobName, jobName, std::string("JahUses") + name, Ogre::StringVector());
        job->setThreadsPerGroup(1u, 1u, 1u);
        job->setNumThreadGroups(1u, 1u, 1u);
        hc->dispatch(job, nullptr, nullptr);
        hc->destroyComputeJob(jobName);
    };
    l.end = [&]() {
        cache->saveWhole(boot->root.get());
        cache->flushWrites(30000u);
        cache->finishWrites();
        cache->setComputeStore(nullptr);
        cache.reset();
        store.reset();
        setForkComputeKeyHook(false);
        boot.reset();
    };
    runLaunches(dir, l);
}

}  // namespace

static int run(const std::string &mode) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) return 1;
    std::string tmplPath = std::string(cwd) + "/cache-uses-XXXXXX";
    const char *made = mkdtemp(&tmplPath[0]);
    if (!made) { std::printf("FAIL: no scratch directory\n"); return 1; }
    const std::string dir = made;
    if (std::system(("mkdir -p '" + dir + "/cache'").c_str()) != 0) return 1;
    if (mode == "--microcode") microcode(dir);
    else if (mode == "--hlms") hlms(dir);
    else if (mode == "--compute") compute(dir);
    else { std::printf("usage: test_shader_cache_uses --microcode | --hlms | --compute\n"); return 64; }
    std::system(("rm -rf '" + dir + "'").c_str());
    std::printf(gFailures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", gFailures);
    return gFailures ? 1 : 0;
}

// A LAW THAT FAILS FAILS - it never crashes or aborts the row: an exception (Ogre's included) is a FAIL line and exit 1.
int main(int argc, char **argv) {
    try {
        return run(argc > 1 ? argv[1] : "");
    } catch (const std::exception &e) {
        std::printf("FAIL: an exception ended the row: %s\nRESULT: 1 FAILURE(S)\n", e.what());
        return 1;
    }
}
