// engine.shader_cache_uses.{microcode,hlms,compute} (SHADER-SYSTEM-1, the start preflight's F10; the prune's law as the lead
// ruled it after the final read, 2026-11-22; NULL Roots, no card, no display) - AGE ALONE NEVER DROPS AN ENTRY, AND A CACHE
// STILL DOES NOT GROW WITHOUT END. Every entry of the three layers that keep what a launch loaded (the microcode map, the
// Hlms recipes, the compute records) carries the launch it was last used in (irisgl engine/src/EntryUses.h: a sidecar of
// ours, Ogre's formats unchanged); a save evicts only when its layers would pass the size cap, the least recently used
// first, never this launch's. One row per layer, each through the layer's real path - a real ShaderCache saving and loading
// through the fork's GpuProgramManager / HlmsUnlit + HlmsDiskCache / HlmsCompute:
//   (1) UNDER THE BUDGET, 100 LAUNCHES THAT USE NOTHING DROP NOTHING: an entry made at launch 1 is there at launch 102
//       (an "unused for 30 launches" rule wore the rig's shared caches: every green row is a launch - the final read);
//   (2) OVER THE BUDGET THE OLDEST GO AND THE JUST-USED STAY: entries last used at launches 1, 3 and 5 (the last made at 2,
//       before the second), a budget one byte under the layers: the launch-1 entry goes and no other; at the next save
//       the launch-3 entry goes and the one made earlier but used at 5 stays; with a budget below this launch's own
//       entries the backstop wipes the directory;
//   (3) A LAUNCH THAT COMPILED NOTHING AND EVICTED NOTHING WRITES NO LAYER AND NO MANIFEST: after the 100 launches and a
//       launch that USES a loaded entry, every file the manifest names and the manifest itself keep their bytes and
//       mtime - the stamp it moved is written to the sidecar alone (fresh_home.sh's promotion hashes the manifest: a
//       row that grew nothing never replaces a shared rig cache);
//   (4) A TORN SIDECAR EVICTS NOTHING: one byte of entry-uses.bin flipped AND the layers over the budget, the launch drops
//       nothing and wipes nothing (a damaged 1-KB file is no reason to throw 256 MB away: the lead) and rewrites a whole
//       table; the eviction runs at the next save whose stamps were read intact.
// The hlms row holds one more law, of the layer's own guard (the final read's item 2):
//   (5) THE PREPROCESSOR's SALT: after ShaderCache::load, the HlmsUnlit's template checksum is the one seeded with the
//       compile path's preprocessor group (JAHSHAKA_COMPILE_PATH_PREPROCESSOR_ID, OgreShaderCache.cpp load) - the hlms
//       layer's ONLY guard against a changed preprocessor now that no fork commit is in its key (without it a
//       preprocessor change silently compiles the old processed source). The salt has no getter and the checksum is its
//       function: the loaded checksum equals the one with that salt set again, and the control - salt zero, the block
//       skipped (upstream's checksum) - differs from it.
// engine.shader_cache_temps (NOQT-CORE-1 stage 1, the engine link; --temps) - A KILLED WRITE's TEMP IS SWEPT AT THE NEXT
// OPEN, A LIVE WRITER's NEVER: every cache write stages "<name>.tmp-<pid>-<serial>" (iris::files) and a write killed before
// its rename used to leave it in the directory for ever, outside the manifest and the cap (OgreShaderCache.cpp load):
//   (6) a temp whose writer was KILLED (a child staged it through iris::files, wrote half and took SIGKILL) and one last
//       written BEFORE THIS BOOT (its pid alive: the boot is the proof) are gone after a writer's open; the temp of a
//       writer ALIVE in another process (a child, half its bytes written, blocked on a pipe) survives the open and its
//       write then completes WHOLE; a file of another shape (warmup.set) is not touched. CONTROLS: the child's death by
//       SIGKILL observed (waitpid) and both stale temps seen on disk before the open.
#include "EnginePrivate.h"
#include "jahshaka_engine_build_id.h"   // JAHSHAKA_COMPILE_PATH_PREPROCESSOR_ID: law (5)

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

#include "core/files.h"   // the staged primitives the temps' writers use (law 6)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
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
bool exists(const std::string &path) { struct stat st {}; return ::stat(path.c_str(), &st) == 0; }
void mkdirs(const std::string &dir) {
    if (std::system(("mkdir -p '" + dir + "'").c_str()) != 0) throw std::runtime_error("mkdir " + dir);
}

/// The manifest and every file it names: name -> (bytes, mtime in ns). Two equal snapshots = nothing was written.
using Snapshot = std::map<std::string, std::pair<std::string, long long>>;
Snapshot snapshot(const std::string &cacheDir) {
    Snapshot s;
    auto take = [&](const std::string &name) {
        struct stat st {};
        if (::stat((cacheDir + "/" + name).c_str(), &st) != 0) return;
        s[name] = { readFile(cacheDir + "/" + name),
                    (long long)st.st_mtim.tv_sec * 1000000000ll + (long long)st.st_mtim.tv_nsec };
    };
    take("cache-manifest.txt");
    std::istringstream m(readFile(cacheDir + "/cache-manifest.txt"));
    for (std::string line; std::getline(m, line);) {
        std::istringstream ls(line);
        std::string tag, name;
        ls >> tag >> name;
        if (tag == "file") take(name);
    }
    return s;
}
/// The layers' bytes: the sum of the sizes the manifest names (the total the size cap is held against).
unsigned long long layerBytes(const std::string &cacheDir) {
    unsigned long long total = 0;
    std::istringstream m(readFile(cacheDir + "/cache-manifest.txt"));
    for (std::string line; std::getline(m, line);) {
        std::istringstream ls(line);
        std::string tag, name;
        unsigned long long bytes = 0;
        ls >> tag >> name >> bytes;
        if (tag == "file") total += bytes;
    }
    return total;
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

/// One layer's real path over one cache directory, for the life of one toy (a fresh Ogre state each): begin(maxBytes) is
/// a launch's boot and the cache's load, present(name) reads the cache's state after it, use(name) is that launch's use
/// of an entry (the first use of a new one makes it), end() the save and the launch's teardown.
struct Toy {
    virtual ~Toy() = default;
    virtual void begin(unsigned long long maxBytes) = 0;
    virtual bool present(const char *name) = 0;
    virtual void use(const char *name) = 0;
    virtual void end() = 0;
};
using MakeToy = std::function<std::unique_ptr<Toy>(const std::string &cacheDir)>;

constexpr unsigned long long kRoomy = 256ull * 1024ull * 1024ull;   // the app's cap: these toys' layers are bytes

/// The four laws, the same for every layer.
void runLaws(const std::string &dir, const MakeToy &make) {
    {   // (1), (3), (4): under the budget
        const std::string c = dir + "/under";
        mkdirs(c);
        std::unique_ptr<Toy> t = make(c);
        t->begin(kRoomy); t->use("A"); t->use("B"); t->end();       // launch 1 makes A and B
        const Snapshot made = snapshot(c);
        for (int launch = 2; launch <= 101; ++launch) { t->begin(kRoomy); t->end(); }   // 100 launches that use nothing
        const std::string usesBefore = readFile(c + "/entry-uses.bin");
        t->begin(kRoomy);
        const bool a = t->present("A"), b = t->present("B");
        t->use("A");                                                 // launch 102: a loaded entry used, nothing compiled
        t->end();
        CHECK(a && b, "(1) under the budget, 100 launches that used nothing dropped nothing (A %d, B %d at launch 102)", a, b);
        const Snapshot after = snapshot(c);
        CHECK(!made.empty() && after == made,
              "(3) 101 launches that compiled nothing left every layer file and the manifest at their bytes and mtime "
              "(%zu files)", made.size());
        CHECK(readFile(c + "/entry-uses.bin") != usesBefore, "(3) ...and the stamp launch 102 moved was written: the sidecar alone");

        std::string torn = readFile(c + "/entry-uses.bin");
        if (torn.size() > 20) torn[torn.size() / 2] ^= 0x5a;          // one byte: the table's checksum no longer holds
        writeFile(c + "/entry-uses.bin", torn);
        t->begin(1u); t->use("A"); t->end();                           // a torn sidecar AND a budget the layers pass
        const unsigned long long layers = layerBytes(c);
        t->begin(layers - 1u);
        const bool a2 = t->present("A"), b2 = t->present("B");
        t->use("A");
        t->end();                                                      // the next save, its stamps trustworthy again
        CHECK(a2 && b2 && layers > 0u,
              "(4) a TORN sidecar over the budget evicted nothing and wiped nothing (A %d, B %d, %llu bytes of layers)",
              a2, b2, layers);
        const std::string rebuilt = readFile(c + "/entry-uses.bin");
        UseTable table;
        std::uint32_t launch = 0;
        CHECK(rebuilt != torn && readUseTable(rebuilt.data(), rebuilt.size(), table, launch) && table.size() >= 1u,
              "(4) ...and a whole table was rewritten (%zu stamps, launch %u)", table.size(), launch);
        t->begin(kRoomy);
        const bool a3 = t->present("A"), b3 = t->present("B");
        t->end();
        CHECK(a3 && !b3,
              "(4) ...and the eviction ran at the next save with stamps: the entry this launch did not use went (%d), the "
              "used one stayed (%d)", !b3, a3);
    }
    {   // (2): over the budget
        const std::string c = dir + "/over";
        mkdirs(c);
        std::unique_ptr<Toy> t = make(c);
        t->begin(kRoomy); t->use("A"); t->use("B"); t->end();        // B last used at 1
        t->begin(kRoomy); t->use("A"); t->use("D"); t->end();        // D made at 2...
        t->begin(kRoomy); t->use("A"); t->use("C"); t->end();        // C last used at 3
        t->begin(kRoomy); t->use("A"); t->end();
        const unsigned long long four = layerBytes(c);
        t->begin(four - 1u); t->use("A"); t->use("D"); t->end();     // ...and used at 5; one byte over: the oldest goes
        const unsigned long long five = layerBytes(c);
        t->begin(five - 1u);
        const bool b5 = t->present("B"), c5 = t->present("C"), d5 = t->present("D"), a5 = t->present("A");
        t->use("A");
        t->end();                                                     // one byte over again
        CHECK(!b5 && c5 && d5 && a5 && five < four,
              "(2) one byte over the budget: the entry last used at launch 1 went (%d), those at 3 and 5 and this launch's "
              "stayed (%d %d %d); the layers %llu -> %llu bytes", !b5, c5, d5, a5, four, five);
        t->begin(1u);
        const bool c6 = t->present("C"), d6 = t->present("D"), a6 = t->present("A");
        t->use("A");
        t->end();                                                     // a budget below this launch's own entries
        CHECK(!c6 && d6 && a6,
              "(2) the next save took the entry last used at 3 (%d) and kept the one MADE before it but used at 5 (%d) and "
              "this launch's (%d): least recently USED first", !c6, d6, a6);
        CHECK(!exists(c + "/cache-manifest.txt"),
              "(2) a budget this launch's own entries pass: the backstop wiped the directory (the manifest is gone)");
    }
}

// ---- the microcode layer: the fork's GpuProgramManager map, through a real ShaderCache's load and save ----
struct MicrocodeToy final : Toy {
    std::string dir;
    Boot boot;
    std::unique_ptr<ShaderCache> cache;
    explicit MicrocodeToy(const std::string &cacheDir) : dir(cacheDir), boot(cacheDir + "-ogre.log") {}
    Ogre::GpuProgramManager &gpm() { return Ogre::GpuProgramManager::getSingleton(); }
    void begin(unsigned long long maxBytes) override {
        cache = std::make_unique<ShaderCache>();
        cache->configure(dir, std::string(), maxBytes);
        cache->load(boot.root.get());
    }
    bool present(const char *name) override {
        Ogre::GpuProgramManager::MicrocodeMap map;
        gpm().copyMicrocodeCache(map);
        for (const auto &e : map)
            if (std::memcmp(e.second->getPtr(), name, std::strlen(name) + 1) == 0) return true;
        return false;
    }
    void use(const char *name) override {
        const std::string source = std::string("source of ") + name;
        const Ogre::GpuProgramManager::Microcode *hit = nullptr;
        if (gpm().getMicrocodeFromCache(source, &hit)) return;        // a hit is a use
        Ogre::GpuProgramManager::Microcode m = gpm().createMicrocode(16u);
        std::memset(m->getPtr(), 0, 16u);
        std::memcpy(m->getPtr(), name, std::strlen(name));
        gpm().addMicrocodeToCache(source, m);                         // built = used
    }
    void end() override { cache->saveWhole(boot.root.get()); cache->flushWrites(30000u); cache->finishWrites(); cache.reset(); }
};

// ---- the Hlms layer: a real HlmsUnlit's recipes through HlmsDiskCache, a draw's use by the fork's test door ----
struct HlmsToy final : Toy {
    std::string dir;
    Boot boot;
    Ogre::ArchiveVec libs;
    Ogre::Archive *data = nullptr;
    Ogre::HlmsUnlit *unlit = nullptr;
    std::unique_ptr<ShaderCache> cache;
    HlmsToy(const std::string &cacheDir, const std::string &templates) : dir(cacheDir), boot(cacheDir + "-ogre.log") {
        data = Ogre::ArchiveManager::getSingleton().load(templates, "FileSystem", true);
        unlit = OGRE_NEW Ogre::HlmsUnlit(data, &libs);
        boot.root->getHlmsManager()->registerHlms(unlit);
    }
    void begin(unsigned long long maxBytes) override {
        unlit->reloadFrom(data, &libs);   // a new process holds no recipe until the cache's load restores them
        cache = std::make_unique<ShaderCache>();
        cache->configure(dir, std::string(), maxBytes);
        cache->load(boot.root.get());
    }
    bool present(const char *name) override {
        const Ogre::IdString key(std::string("jah_uses_") + name);
        for (const auto &recipe : unlit->getShaderCodeCache())
            for (const Ogre::HlmsProperty &p : recipe.mergedCache.setProperties)
                if (p.keyName == key) return true;
        return false;
    }
    void use(const char *name) override {
        Ogre::HlmsPropertyVec p;
        p.push_back(Ogre::HlmsProperty(Ogre::IdString(std::string("jah_uses_") + name), 1));
        unlit->_jahTestRecipe(p, std::string("void main() { /* ") + name + " */ }\n", true);
    }
    void end() override { cache->saveWhole(boot.root.get()); cache->flushWrites(30000u); cache->finishWrites(); cache.reset(); }
};

// ---- the compute layer: the fork's HlmsCompute records through the engine's adapter, a dispatch's hit or build ----
struct ComputeToy final : Toy {
    std::string dir, media;
    int launches = 0;
    std::unique_ptr<Boot> boot;
    std::unique_ptr<ComputeCodeStore> store;
    std::unique_ptr<ShaderCache> cache;
    ComputeToy(const std::string &cacheDir, const std::string &mediaDir) : dir(cacheDir), media(mediaDir) {}
    void begin(unsigned long long maxBytes) override {
        boot = std::make_unique<Boot>(dir + "-ogre-" + std::to_string(++launches % 2) + ".log");
        Ogre::HlmsCompute *hc = boot->root->getHlmsManager()->getComputeHlms();
        setForkComputeKeyHook(true);
        store = makeForkComputeStore(hc);
        cache = std::make_unique<ShaderCache>();
        cache->configure(dir, std::string(), maxBytes);
        cache->setComputeStore(store.get());
        cache->load(boot->root.get());
        Ogre::ResourceGroupManager &rgm = Ogre::ResourceGroupManager::getSingleton();
        rgm.addResourceLocation(media, "FileSystem", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, false);
        rgm.initialiseAllResourceGroups(true);
        cache->prewarmCompute();   // no compiler running: the records are filed, their code compiles at a parse
    }
    bool present(const char *name) override {
        std::vector<ComputeRecordPtr> records;
        store->snapshot(records);
        for (const ComputeRecordPtr &r : records)
            if (r->name == std::string("uses/") + name) return true;
        return false;
    }
    void use(const char *name) override {
        Ogre::HlmsCompute *hc = boot->root->getHlmsManager()->getComputeHlms();
        const std::string jobName = std::string("uses/") + name;
        Ogre::HlmsComputeJob *job = hc->createComputeJob(jobName, jobName, std::string("JahUses") + name, Ogre::StringVector());
        job->setThreadsPerGroup(1u, 1u, 1u);
        job->setNumThreadGroups(1u, 1u, 1u);
        hc->dispatch(job, nullptr, nullptr);
        hc->destroyComputeJob(jobName);
    }
    void end() override {
        cache->saveWhole(boot->root.get());
        cache->flushWrites(30000u);
        cache->finishWrites();
        cache->setComputeStore(nullptr);
        cache.reset();
        store.reset();
        setForkComputeKeyHook(false);
        boot.reset();
    }
};

/// (6) A KILLED WRITE's TEMP IS SWEPT AT THE NEXT OPEN, A LIVE WRITER's NEVER (the header). A first launch saves a
/// cache (a manifest: the open under test then WIPES nothing - a wipe would take every temp and prove nothing); the
/// children are forked after its Root is gone; the live one is killed by the guard if a law throws first.
void runTemps(const std::string &dir) {
    const std::string c = dir + "/temps";
    mkdirs(c);
    { MicrocodeToy first(c); first.begin(kRoomy); first.use("A"); first.end(); }
    const std::string manifest = readFile(c + "/cache-manifest.txt");
    std::string killed;                       // (6) the killed writer's temp
    bool killedBySignal = false;
    {
        int names[2];
        if (::pipe(names) != 0) throw std::runtime_error("pipe");
        const pid_t child = ::fork();
        if (child == 0) {
            const std::string tmp = iris::files::utf8(iris::files::stagingTempPath(iris::files::path(c + "/pipeline.cache")));
            { std::ofstream(tmp, std::ios::binary) << "the first half of a pipeline blob"; }
            if (::write(names[1], tmp.data(), tmp.size()) != ssize_t(tmp.size())) ::_exit(2);
            ::close(names[1]);
            ::kill(::getpid(), SIGKILL);      // killed between its write and its rename
            ::_exit(3);
        }
        ::close(names[1]);
        char buf[4096];
        const ssize_t n = ::read(names[0], buf, sizeof buf);
        ::close(names[0]);
        if (n > 0) killed.assign(buf, size_t(n));
        int status = 0;
        ::waitpid(child, &status, 0);
        killedBySignal = WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL;
    }
    struct LiveWriter {
        pid_t pid = -1;
        int go = -1;
        std::string tmp, target;
        ~LiveWriter() { if (pid > 0) { ::kill(pid, SIGKILL); ::waitpid(pid, nullptr, 0); } }
    } live;
    live.target = c + "/texture-channels.txt";
    const std::string whole = std::string(4096, 'a') + std::string(4096, 'b');
    {
        int ready[2], go[2];
        if (::pipe(ready) != 0 || ::pipe(go) != 0) throw std::runtime_error("pipe");
        const pid_t child = ::fork();
        if (child == 0) {
            ::close(ready[0]);
            ::close(go[1]);
            const std::filesystem::path tmp = iris::files::stagingTempPath(iris::files::path(live.target));
            const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0 || ::write(fd, whole.data(), 4096) != 4096) ::_exit(2);
            const std::string name = iris::files::utf8(tmp);
            if (::write(ready[1], name.data(), name.size()) != ssize_t(name.size())) ::_exit(2);
            ::close(ready[1]);
            char b;
            if (::read(go[0], &b, 1) != 1) ::_exit(2);   // the parent's open runs while this write is in flight
            if (::write(fd, whole.data() + 4096, 4096) != 4096 || ::close(fd) != 0) ::_exit(2);
            ::_exit(iris::files::fsyncPath(tmp) && iris::files::atomicRename(tmp, iris::files::path(live.target)) ? 0 : 1);
        }
        ::close(ready[1]);
        ::close(go[0]);
        live.pid = child;
        live.go = go[1];
        char buf[4096];
        const ssize_t n = ::read(ready[0], buf, sizeof buf);
        ::close(ready[0]);
        if (n > 0) live.tmp.assign(buf, size_t(n));
    }
    // A temp last written before this boot, its pid ALIVE (the live writer's): only the boot says it is stale.
    const std::string beforeBoot = c + "/hlms.1.bin.tmp-" + std::to_string(live.pid) + "-9";
    writeFile(beforeBoot, "a write of the last boot");
    const timeval old[2] = { { 1000000000, 0 }, { 1000000000, 0 } };   // 2001
    ::utimes(beforeBoot.c_str(), old);
    writeFile(c + "/warmup.set", "a host's set: no temp");
    const bool staleSeen = !killed.empty() && exists(killed) && exists(beforeBoot);
    const bool liveSeen = !live.tmp.empty() && exists(live.tmp);

    {   // the writer's open
        Boot boot(c + "-ogre.log");
        ShaderCache cache;
        cache.configure(c, std::string(), kRoomy);
        cache.load(boot.root.get());
        cache.finishWrites();
    }
    CHECK(killedBySignal && staleSeen,
          "(6) controls: the writer died by SIGKILL between its write and its rename, and both stale temps were on disk "
          "before the open (%s)", killed.c_str());
    CHECK(!manifest.empty() && readFile(c + "/cache-manifest.txt") == manifest,
          "(6) controls: the open wiped nothing - the first launch's manifest stands (%zu bytes)", manifest.size());
    CHECK(staleSeen && !exists(killed), "(6) the KILLED writer's temp is gone after the open");
    CHECK(staleSeen && !exists(beforeBoot), "(6) the temp last written BEFORE THIS BOOT is gone, its pid alive or not");
    CHECK(liveSeen && exists(live.tmp), "(6) the LIVE writer's temp survived the open in another process (%s)",
          live.tmp.c_str());
    CHECK(exists(c + "/warmup.set"), "(6) a file of another shape is not touched (warmup.set)");
    char b = 'g';
    const bool told = ::write(live.go, &b, 1) == 1;
    ::close(live.go);
    int status = 0;
    ::waitpid(live.pid, &status, 0);
    live.pid = -1;
    CHECK(told && WIFEXITED(status) && WEXITSTATUS(status) == 0 && readFile(live.target) == whole && !exists(live.tmp),
          "(6) ...and the live write then completed WHOLE (%zu of %zu bytes, its temp renamed away)",
          readFile(live.target).size(), whole.size());
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
    if (mode == "--microcode") {
        std::printf("\n== the microcode layer ==\n");
        runLaws(dir, [](const std::string &c) { return std::unique_ptr<Toy>(new MicrocodeToy(c)); });
    } else if (mode == "--hlms") {
        std::printf("\n== the hlms layer ==\n");
        mkdirs(dir + "/unlit");
        writeFile(dir + "/unlit/VertexShader_vs.glsl", "#version 450\nvoid main() {}\n");
        runLaws(dir, [&dir](const std::string &c) { return std::unique_ptr<Toy>(new HlmsToy(c, dir + "/unlit")); });
        HlmsToy t(dir + "/salt", dir + "/unlit");
        t.begin(kRoomy);
        const std::string id = JAHSHAKA_COMPILE_PATH_PREPROCESSOR_ID;
        const Ogre::uint64 salt[2] = { std::stoull(id.substr(0, 16), nullptr, 16), std::stoull(id.substr(16, 16), nullptr, 16) };
        const Ogre::uint64 zero[2] = { 0u, 0u };
        Ogre::uint64 loaded[2], expected[2], unsalted[2];
        t.unlit->getTemplateChecksum(loaded);
        t.unlit->setJahTemplateSalt(zero);
        t.unlit->getTemplateChecksum(unsalted);
        t.unlit->setJahTemplateSalt(salt);
        t.unlit->getTemplateChecksum(expected);
        t.end();
        CHECK(loaded[0] == expected[0] && loaded[1] == expected[1],
              "(5) after the load the template checksum is the one salted with the preprocessor group %.16s (%016llx)",
              id.c_str(), (unsigned long long)loaded[0]);
        CHECK(unsalted[0] != loaded[0] || unsalted[1] != loaded[1],
              "(5) ...and the control, the salt zero (the block skipped), differs from it (%016llx): the law can fail",
              (unsigned long long)unsalted[0]);
    } else if (mode == "--compute") {
        std::printf("\n== the compute layer ==\n");
        mkdirs(dir + "/media");
        for (const char *n : { "A", "B", "C", "D" })
            writeFile(dir + "/media/JahUses" + n + ".glsl",
                      std::string("#version 450\n// ") + n + "\nlayout( local_size_x = 1 ) in;\nvoid main() {}\n");
        runLaws(dir, [&dir](const std::string &c) { return std::unique_ptr<Toy>(new ComputeToy(c, dir + "/media")); });
    } else if (mode == "--temps") {
        std::printf("\n== the stale temps ==\n");
        runTemps(dir);
    } else {
        std::printf("usage: test_shader_cache_uses --microcode | --hlms | --compute | --temps\n");
        return 64;
    }
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
