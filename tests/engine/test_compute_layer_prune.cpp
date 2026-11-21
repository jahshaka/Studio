// engine.compute_layer_prune (SHADER-SYSTEM-1 stage 3F, the start preflight's F1; a NULL Root, no card, no display) -
// THE COMPUTE LAYER's STALE PRUNE RUNS WHERE THE COMPUTE MEDIA IS FOUND. A record is stale when one of the files its parse
// read (its template, a piece) is now absent or holds other bytes (ComputeRecord.h, pruneStaleRecords), and the files are
// looked up through Ogre's resource groups - the compute jobs' own lookup. The engine LOADS the cache (OgreEngine.cpp,
// mShaderCache.load) before it registers the compute media's folders (registerCommonMaterials), and prewarms the records
// after (mShaderCache.prewarmCompute): a prune at load finds no file at all and drops EVERY record as stale, so a warm
// launch served none. The laws, in the engine's own order (load, then the folders, then the prewarm):
//   (1) a record whose files are staged unchanged is KEPT and handed to the store: loaded 1, stale counts only (2);
//   (2) a record one of whose files holds other bytes is PRUNED (the prune still works: its negative control);
//   (3) the store receives exactly the kept record.
// RED on the code before the fix: loaded 0, stale 2, the store given nothing.
#include "EnginePrivate.h"

#include <OgreAbiUtils.h>
#include <OgreGpuProgramManager.h>
#include <OgreResourceGroupManager.h>
#include <OgreRoot.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>

using namespace jahshaka::engine::detail;

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } } while (0)

namespace {

/// THE NULL RENDER SYSTEM MAKES NO GpuProgramManager (each real one makes its own), and the cache's load enables Ogre's
/// microcode saving through it: one that creates nothing stands in.
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

/// A store that files what it is given: the shader cache's two calls are all it answers.
class FakeStore final : public ComputeCodeStore {
public:
    std::vector<ComputeRecordPtr> records;
    std::uint64_t gen = 0;
    std::uint64_t generation() const override { return gen; }
    void snapshot(std::vector<ComputeRecordPtr> &out) const override { out = records; }
    void prewarm(std::vector<ComputeCodeRecord> &&in) override {
        for (ComputeCodeRecord &r : in) records.push_back(std::make_shared<ComputeCodeRecord>(std::move(r)));
        ++gen;
    }
};

ComputeCodeRecord record(const std::string &name, const std::string &fileName, const std::string &fileBytes) {
    ComputeCodeRecord r;
    r.name = name;
    r.sourceFilename = fileName;
    r.source = "void main() { /* " + name + " */ }\n";
    r.rootLayout = std::string(16, '\x03');
    r.explicitApi = true;
    r.threadsPerGroup[0] = r.threadsPerGroup[1] = r.threadsPerGroup[2] = 1;
    r.numThreadGroups[0] = r.numThreadGroups[1] = r.numThreadGroups[2] = 1;
    r.files = { { fileName, computeFileHash(fileBytes) } };
    ComputeKeyInputs in;
    in.templateText = fileBytes;
    r.inputKey = computeRecordInputKey(in);
    r.programKey = computeProgramKey(r.source, r.rootLayout, r.explicitApi);
    return r;
}

void writeFile(const std::string &path, const std::string &bytes) { std::ofstream(path, std::ios::binary) << bytes; }

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    // The scratch directory lives in the row's own working directory (its build directory), removed at the end.
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) { std::printf("FAIL: no working directory\n"); return 1; }
    std::string tmplPath = std::string(cwd) + "/compute-prune-XXXXXX";
    const char *made = mkdtemp(&tmplPath[0]);
    if (!made) { std::printf("FAIL: no scratch directory\n"); return 1; }
    const std::string root = made, cacheDir = root + "/cache", mediaDir = root + "/media";
    if (std::system(("mkdir -p '" + cacheDir + "' '" + mediaDir + "'").c_str()) != 0) return 1;
    // The staged compute media: one template as the record read it, one EDITED since its record was made.
    writeFile(mediaDir + "/JahPruneKept_cs.glsl", "kept v1\n");
    writeFile(mediaDir + "/JahPruneEdited_cs.glsl", "edited v2\n");

    const Ogre::AbiCookie cookie = Ogre::generateAbiCookie();
    auto ogre = std::make_unique<Ogre::Root>(&cookie, "", "", root + "/ogre.log", "prune");
    ogre->loadPlugin(std::string(JAHSHAKA_TEST_PLUGIN_DIR) + "/RenderSystem_NULL", false, nullptr);
    if (ogre->getAvailableRenderers().empty()) { std::printf("FAIL: no NULL render system\n"); return 1; }
    ogre->setRenderSystem(ogre->getAvailableRenderers().front());
    ogre->initialise(false);
    ogre->createRenderWindow("prune", 1u, 1u, false);
    auto programs = std::make_unique<NullGpuProgramManager>();

    // A launch that SAVED both records (the store files them; the save writes compute.bin and the manifest).
    {
        FakeStore store;
        store.records.push_back(std::make_shared<ComputeCodeRecord>(record("kept", "JahPruneKept_cs.glsl", "kept v1\n")));
        store.records.push_back(
            std::make_shared<ComputeCodeRecord>(record("edited", "JahPruneEdited_cs.glsl", "edited v1\n")));
        store.gen = 1;
        ShaderCache c;
        c.configure(cacheDir, std::string(), mediaDir + "/");
        c.load(ogre.get());
        c.setComputeStore(&store);
        CHECK(c.saveWhole(ogre.get()) && c.flushWrites(30000u), "the first launch saved its two records");
        c.finishWrites();
    }

    // THE NEXT LAUNCH, in the engine's order: the load, then the compute media's folder, then the prewarm.
    FakeStore store;
    ShaderCache c;
    c.configure(cacheDir, std::string(), mediaDir + "/");
    c.load(ogre.get());
    Ogre::ResourceGroupManager &rgm = Ogre::ResourceGroupManager::getSingleton();
    rgm.addResourceLocation(mediaDir, "FileSystem", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, false);
    rgm.initialiseAllResourceGroups(true);
    c.setComputeStore(&store);
    c.prewarmCompute();
    const jahshaka::engine::ShaderCacheStats s = c.stats(ogre.get());
    std::printf("   after the prewarm: records loaded %u, stale %u, rejected %u; the store holds %zu\n",
                s.computeRecordsLoaded, s.computeRecordsStale, s.computeRecordsRejected, store.records.size());
    CHECK(s.computeRecordsLoaded == 1u, "(1) the record whose file is staged unchanged is KEPT (loaded %u)",
          s.computeRecordsLoaded);
    CHECK(s.computeRecordsStale == 1u, "(2) the record whose file changed is PRUNED, and only it (stale %u)",
          s.computeRecordsStale);
    CHECK(store.records.size() == 1u && store.records[0]->name == "kept",
          "(3) the store is handed exactly the kept record (%zu)", store.records.size());
    c.finishWrites();
    programs.reset();
    ogre.reset();
    std::system(("rm -rf '" + root + "'").c_str());
    std::printf(gFailures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", gFailures);
    return gFailures ? 1 : 0;
}
