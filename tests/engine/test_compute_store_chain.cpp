// engine.compute_store_chain (SHADER-SYSTEM-1 stage 3F (c); NULL Roots, no card, no display) - THE COMPUTE LAYER, END TO
// END ON THE CPU: the engine's adapter (ComputeRecord.h makeForkComputeStore + setForkComputeKeyHook) between the fork's
// HlmsCompute records and a real ShaderCache, over three launches in the engine's order (the store and the hook set, the
// cache's load, the compute media's folder, the prewarm on a running async compiler, the dispatch, the save). The laws:
//   (1) a launch that BUILT a compute permutation saves its record: compute.bin holds it;
//   (2) THE NEXT LAUNCH serves it: loaded 1, stale 0, rejected 0 - the fork's program key and file hashes are the ones
//       irisgl's reader re-derives (computeProgramKey, computeFileHash) - and the job's first dispatch builds its PSO
//       with NO template parse;
//   (3) NEGATIVE CONTROL: a launch whose staged template changed prunes the record (stale 1) and the dispatch parses.
#include "EnginePrivate.h"

#include <OgreAbiUtils.h>
#include <OgreGpuProgramManager.h>
#include <OgreHlmsAsyncCompiler.h>
#include <OgreHlmsCompute.h>
#include <OgreHlmsComputeJob.h>
#include <OgreHlmsManager.h>
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

/// The NULL render system makes no GpuProgramManager: one that creates nothing stands in.
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

struct LaunchResult {
    jahshaka::engine::ShaderCacheStats stats;
    unsigned long long parsed = 0, psos = 0;
};

/// One launch in the engine's order.
LaunchResult launch(const std::string &dir, const char *log) {
    LaunchResult out;
    static const Ogre::AbiCookie cookie = Ogre::generateAbiCookie();
    auto root = std::make_unique<Ogre::Root>(&cookie, "", "", dir + "/" + log, "chain");
    root->loadPlugin(std::string(JAHSHAKA_TEST_PLUGIN_DIR) + "/RenderSystem_NULL", false, nullptr);
    root->setRenderSystem(root->getAvailableRenderers().front());
    root->initialise(false);
    root->createRenderWindow("chain", 1u, 1u, false);
    auto programs = std::make_unique<NullGpuProgramManager>();
    Ogre::HlmsCompute *hc = root->getHlmsManager()->getComputeHlms();
    {
        setForkComputeKeyHook(true);
        std::unique_ptr<ComputeCodeStore> store = makeForkComputeStore(hc);
        ShaderCache cache;
        cache.configure(dir + "/cache", std::string(), dir + "/media/");
        cache.setComputeStore(store.get());
        cache.load(root.get());
        Ogre::ResourceGroupManager &rgm = Ogre::ResourceGroupManager::getSingleton();
        rgm.addResourceLocation(dir + "/media", "FileSystem", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
                                false);
        rgm.initialiseAllResourceGroups(true);
        Ogre::HlmsAsyncCompiler *compiler = root->getHlmsManager()->getAsyncCompiler();
        compiler->setNumThreads(1u, 0);
        cache.prewarmCompute();
        compiler->waitForAll();
        compiler->setNumThreads(0u);   // the dispatch below is the frame's own (no deferral)
        Ogre::HlmsComputeJob *job =
            hc->createComputeJob("chain/job", "chain/job", "JahChain", Ogre::StringVector{ "JahChainBody_piece_cs.any" });
        job->setThreadsPerGroup(1u, 1u, 1u);
        job->setNumThreadGroups(1u, 1u, 1u);
        const unsigned long long p0 = hc->getJahParsedInFrame(), s0 = hc->getJahPsoInFrame();
        hc->dispatch(job, nullptr, nullptr);
        out.parsed = hc->getJahParsedInFrame() - p0;
        out.psos = hc->getJahPsoInFrame() - s0;
        out.stats = cache.stats(root.get());
        cache.saveWhole(root.get());
        cache.flushWrites(30000u);
        cache.finishWrites();
        cache.setComputeStore(nullptr);
        hc->destroyComputeJob("chain/job");
    }
    setForkComputeKeyHook(false);
    programs.reset();
    root.reset();
    return out;
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) return 1;
    std::string tmplPath = std::string(cwd) + "/store-chain-XXXXXX";
    const char *made = mkdtemp(&tmplPath[0]);
    if (!made) { std::printf("FAIL: no scratch directory\n"); return 1; }
    const std::string dir = made;
    if (std::system(("mkdir -p '" + dir + "/cache' '" + dir + "/media'").c_str()) != 0) return 1;
    const std::string tmpl = "#version 450\nlayout( local_size_x = 1 ) in;\nvoid main() { @insertpiece( Body ) }\n";
    writeFile(dir + "/media/JahChain.glsl", tmpl);
    writeFile(dir + "/media/JahChainBody_piece_cs.any", "@piece( Body )// body@end\n");

    const LaunchResult first = launch(dir, "chain-1.log");
    CHECK(first.parsed == 1u && first.psos == 1u, "(1) the first launch parses its permutation (%llu) and builds its PSO",
          first.parsed);
    std::vector<char> bytes;
    {
        std::ifstream f(dir + "/cache/compute.bin", std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    const ComputeLayerRead saved = readComputeLayer(bytes.data(), bytes.size());
    CHECK(saved.ok && saved.records.size() == 1u && saved.rejected == 0u,
          "(1) its save wrote compute.bin with the record (%zu, rejected %u)", saved.records.size(), saved.rejected);

    const LaunchResult second = launch(dir, "chain-2.log");
    std::printf("   the second launch: records loaded %u, stale %u, rejected %u; parsed %llu, PSOs %llu\n",
                second.stats.computeRecordsLoaded, second.stats.computeRecordsStale, second.stats.computeRecordsRejected,
                second.parsed, second.psos);
    CHECK(second.stats.computeRecordsLoaded == 1u && second.stats.computeRecordsStale == 0u &&
              second.stats.computeRecordsRejected == 0u,
          "(2) the next launch reads the record: loaded 1, stale 0, rejected 0 (the fork's keys are irisgl's)");
    CHECK(second.parsed == 0u && second.psos == 1u,
          "(2) ...and its first dispatch builds the PSO from the prewarmed code with NO parse (parsed %llu)",
          second.parsed);

    writeFile(dir + "/media/JahChain.glsl", tmpl + "// edited\n");
    const LaunchResult third = launch(dir, "chain-3.log");
    CHECK(third.stats.computeRecordsStale == 1u && third.parsed == 1u,
          "(3) NEGATIVE CONTROL: the template edited - the record is stale (%u) and the dispatch parses (%llu)",
          third.stats.computeRecordsStale, third.parsed);

    std::system(("rm -rf '" + dir + "'").c_str());
    std::printf(gFailures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", gFailures);
    return gFailures ? 1 : 0;
}
