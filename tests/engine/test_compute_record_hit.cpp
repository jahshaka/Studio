// engine.compute_record_hit (SHADER-SYSTEM-1 stage 3F (b): the fork half of COLD-BOOT-1 stage 3; the lead's ruling P6 and
// its three conditions, the start preflight's F2 and F4) - THE COMPUTE CODE RECORDS IN THE FORK's HlmsCompute, driven on
// the NULL render system (no display, no device; its programs are Ogre's null programs, so a "compile" is the parse and
// the program object - which is all these laws are about). The laws:
//   (0) NO HOOK = UPSTREAM EXACTLY (condition 3): every permutation's template is parsed (a clone's too), nothing filed.
//   (1) WITH THE HOOK, a built permutation files a RECORD; a clone of equal inputs HITS it - no parse, one PSO, the same
//       program.
//   (2) THE HOOK's KEY IS THE ONLY KEY THE HIT USES: a hook that names two different templates alike makes the second hit
//       the first's record.
//   (3) A RECORD WHOSE SOURCE DOES NOT HASH TO ITS PROGRAM KEY IS REJECTED (condition 1): handed back at a launch, it is
//       not filed; the job parses and files the true one.
//   (4) THE FILE-BYTES MEMO IS CLEARED BY clearShaderCache (condition 2; reloadFrom calls it): a template edited on disk
//       is read afresh after it - the next build's source is the edited text.
//   (5) F2a - TWO JOBS OF EQUAL TEXT WHOSE SLOTS DIFFER IN TYPE NEVER SHARE A PROGRAM: under a hook that leaves the root
//       layout out of its key, the second job still misses (the hit compares the layout) and gets its own program.
//   (6) F2b - THE KEY READS THE RENDER SYSTEM's PROPERTIES, set at the parse's start: a precision mode change moves the key
//       of the same job (two records), never the same key over another parse.
//   (7) THE NEXT LAUNCH: a second Root takes the first's records through jahPrewarmCode - code only, on the async compiler
//       - and the job's first dispatch builds its PSO with NO parse.
//   (8) THE JOIN (the final read's item 3a): the same, dispatched BEFORE the prewarmed code is published (its program
//       enters the code cache only at the code job's publish, on the main thread) - the hit joins the job: NO parse.
//   (9) THE SHARED JOB's PUBLISH (stage 7a): an asynchronously published permutation writes none of the job's values
//       (they are its current permutation's) and consumes none; each program gets them whole at its own first dispatch,
//       and a dispatch clears with ITS value - read back from the program's parameters.
#include <OgreAbiUtils.h>
#include <OgreGpuProgramManager.h>
#include <OgreHlmsCompute.h>
#include <OgreHlmsComputeJob.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsAsyncCompiler.h>
#include <OgreShaderParams.h>
#include <OgreRenderSystem.h>
#include <OgreRenderSystemCapabilities.h>
#include <OgreResourceGroupManager.h>
#include <OgreRoot.h>
#include <OgreTextureGpu.h>
#include <OgreTextureGpuManager.h>
#include <OgreWindow.h>
#include <Hash/MurmurHash3.h>
#include <Vao/OgreTexBufferPacked.h>
#include <Vao/OgreVaoManager.h>

#include <cstdio>
#include <exception>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } } while (0)

namespace {

/// The hook under test: a content key of what it is given. `layout` false leaves the root layout out (F2a's case);
/// `constant` names every input alike (law 2).
bool gHookLayout = true, gHookConstant = false;
void hook(const Ogre::HlmsCompute::JahKeyInputs &in, Ogre::uint64 out[2]) {
    std::string b = gHookConstant ? std::string("same") : *in.templateText;
    if (!gHookConstant) {
        for (const auto &p : *in.pieceFiles) b += "|F" + p.first + "=" + *p.second;
        for (const auto &c : *in.cppPieces) b += "|C" + std::to_string(c.first.mHash) + "=" + c.second;
        for (const auto &p : *in.properties) b += "|P" + std::to_string(p.keyName.mHash) + "=" + std::to_string(p.value);
        if (gHookLayout) b += "|L" + *in.rootLayout;
    }
    Ogre::MurmurHash3_x64_128(b.data(), int(b.size()), 0x1234u, out);
}

void writeFile(const std::string &path, const std::string &bytes) { std::ofstream(path, std::ios::binary) << bytes; }

std::string templateText(const std::string &marker) {
    return "#version 450\n// " + marker + "\nlayout( local_size_x = 1 ) in;\nvoid main() { @insertpiece( Body ) }\n";
}

/// Every parameters object a PSO was given, in the order they were made (law 9 reads the values a dispatch applied).
std::vector<Ogre::GpuProgramParametersSharedPtr> gMadeParams;
bool gCaptureParams = false;   // law 9 only, released before its Root goes

/// THE NULL RENDER SYSTEM MAKES NO GpuProgramManager (each real one makes its own) and a PSO's parameters come from one:
/// one that creates nothing stands in. Its parameters DECLARE the shared clear job's two names - fClearValue (float4)
/// and uClearValue (uint4), each at offset 0 of its buffer - so a value applied to a program is READ BACK from it (law 9);
/// the laws before it apply none.
class NullGpuProgramManager final : public Ogre::GpuProgramManager {
public:
    Ogre::GpuProgramParametersSharedPtr createParameters() override {
        Ogre::GpuProgramParametersSharedPtr p = Ogre::GpuProgramManager::createParameters();
        Ogre::GpuNamedConstantsPtr named(OGRE_NEW Ogre::GpuNamedConstants());
        Ogre::GpuConstantDefinition f;
        f.constType = Ogre::GCT_FLOAT4;
        f.physicalIndex = 0u;
        f.logicalIndex = 0u;
        f.elementSize = 4u;
        f.arraySize = 1u;
        named->map.emplace("fClearValue", f);
        Ogre::GpuConstantDefinition u = f;
        u.constType = Ogre::GCT_UINT4;
        named->map.emplace("uClearValue", u);
        named->floatBufferSize = 4u;
        named->uintBufferSize = 4u;
        p->_setNamedConstants(named);
        if (gCaptureParams) gMadeParams.push_back(p);
        return p;
    }
private:
    Ogre::Resource *createImpl(const Ogre::String &, Ogre::ResourceHandle, const Ogre::String &, bool,
                               Ogre::ManualResourceLoader *, Ogre::GpuProgramType, const Ogre::String &) override {
        return nullptr;
    }
    Ogre::Resource *createImpl(const Ogre::String &, Ogre::ResourceHandle, const Ogre::String &, bool,
                               Ogre::ManualResourceLoader *, const Ogre::NameValuePairList *) override {
        return nullptr;
    }
};

struct Launch {
    std::unique_ptr<Ogre::Root> root;
    std::unique_ptr<NullGpuProgramManager> programs;   // declared after the Root: destroyed before it
    Ogre::HlmsCompute *hc = nullptr;
    Ogre::RenderSystem *rs = nullptr;
    bool boot(const std::string &dir, const char *log) {
        static const Ogre::AbiCookie cookie = Ogre::generateAbiCookie();
        root = std::make_unique<Ogre::Root>(&cookie, "", "", dir + "/" + log, "hit");
        root->loadPlugin(std::string(JAHSHAKA_TEST_PLUGIN_DIR) + "/RenderSystem_NULL", false, nullptr);
        if (root->getAvailableRenderers().empty()) return false;
        root->setRenderSystem(root->getAvailableRenderers().front());
        root->initialise(false);
        root->createRenderWindow("hit", 1u, 1u, false);
        programs = std::make_unique<NullGpuProgramManager>();
        rs = root->getRenderSystem();
        hc = root->getHlmsManager()->getComputeHlms();
        Ogre::ResourceGroupManager &rgm = Ogre::ResourceGroupManager::getSingleton();
        rgm.addResourceLocation(dir, "FileSystem", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, false);
        rgm.initialiseAllResourceGroups(true);
        return rs && hc;
    }
};

Ogre::HlmsComputeJob *makeJob(Ogre::HlmsCompute *hc, const std::string &name, const std::string &source) {
    Ogre::StringVector pieces{ "JahHitBody_piece_cs.any" };
    Ogre::HlmsComputeJob *job = hc->createComputeJob(name, name, source, pieces);
    job->setThreadsPerGroup(1u, 1u, 1u);
    job->setNumThreadGroups(1u, 1u, 1u);
    job->setInformHlmsOfTextureData(false);   // the slots reach no property: only the root layout tells their types
    return job;
}

}  // namespace

static int run() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) return 1;
    std::string tmplPath = std::string(cwd) + "/record-hit-XXXXXX";
    const char *made = mkdtemp(&tmplPath[0]);
    if (!made) { std::printf("FAIL: no scratch directory\n"); return 1; }
    const std::string dir = made;
    writeFile(dir + "/JahHitA.glsl", templateText("A"));
    writeFile(dir + "/JahHitB.glsl", templateText("B"));
    writeFile(dir + "/JahHitBody_piece_cs.any", "@piece( Body )// body@end\n");

    std::vector<Ogre::HlmsCompute::JahCodeRecord> handedOver;
    {
        Launch l;
        if (!l.boot(dir, "hit-1.log")) { std::printf("FAIL: the NULL boot\n"); return 1; }
        Ogre::HlmsCompute *hc = l.hc;
        std::vector<Ogre::HlmsCompute::JahCodeRecordPtr> recs;

        // (0) NO HOOK
        Ogre::HlmsCompute::msJahComputeKeyHook = nullptr;
        {
            const Ogre::uint64 p0 = hc->getJahParsedInFrame(), s0 = hc->getJahPsoInFrame();
            Ogre::HlmsComputeJob *a = makeJob(hc, "nohook/a", "JahHitA");
            Ogre::HlmsComputeJob *b = makeJob(hc, "nohook/b", "JahHitA");
            hc->dispatch(a, nullptr, nullptr);
            hc->dispatch(b, nullptr, nullptr);
            hc->getJahRecords(recs);
            CHECK(hc->getJahParsedInFrame() - p0 == 2u && hc->getJahPsoInFrame() - s0 == 2u && recs.empty(),
                  "(0) no hook: both jobs parse (%llu), two PSOs (%llu), no record (%zu) - upstream exactly",
                  (unsigned long long)(hc->getJahParsedInFrame() - p0),
                  (unsigned long long)(hc->getJahPsoInFrame() - s0), recs.size());
            hc->destroyComputeJob("nohook/a");
            hc->destroyComputeJob("nohook/b");
        }

        // (1) THE HIT
        Ogre::HlmsCompute::msJahComputeKeyHook = hook;
        {
            const Ogre::uint64 p0 = hc->getJahParsedInFrame(), s0 = hc->getJahPsoInFrame();
            Ogre::HlmsComputeJob *a = makeJob(hc, "hit/a", "JahHitA");
            hc->dispatch(a, nullptr, nullptr);
            const Ogre::uint64 pa = hc->getJahParsedInFrame() - p0;
            hc->getJahRecords(recs);
            const size_t filed = recs.size();
            Ogre::HlmsComputeJob *b = makeJob(hc, "hit/b", "JahHitA");
            hc->dispatch(b, nullptr, nullptr);
            hc->getJahRecords(recs);
            CHECK(pa == 1u && filed == 1u, "(1) the first build parses (%llu) and files one record (%zu)",
                  (unsigned long long)pa, filed);
            CHECK(hc->getJahParsedInFrame() - p0 == 1u && hc->getJahPsoInFrame() - s0 == 2u && recs.size() == 1u,
                  "(1) the clone HITS: no parse (%llu in all), its own PSO (%llu PSOs), still one record (%zu)",
                  (unsigned long long)(hc->getJahParsedInFrame() - p0),
                  (unsigned long long)(hc->getJahPsoInFrame() - s0), recs.size());
            CHECK(!recs.empty() && recs[0]->source.find("// body") != std::string::npos &&
                      recs[0]->files.size() == 2u && recs[0]->sourceFilename == "JahHitA.glsl",
                  "(1) the record holds the generated source (the piece inserted) and the two files it read");
            for (const auto &r : recs) handedOver.push_back(*r);

            // (2) THE HOOK's KEY IS THE ONLY KEY: a hook naming every input alike
            gHookConstant = true;
            Ogre::HlmsComputeJob *c = makeJob(hc, "hit/c", "JahHitB");
            hc->dispatch(c, nullptr, nullptr);   // files under the hook's one key
            Ogre::HlmsComputeJob *d = makeJob(hc, "hit/d", "JahHitA");
            const Ogre::uint64 pd = hc->getJahParsedInFrame();
            hc->dispatch(d, nullptr, nullptr);
            CHECK(hc->getJahParsedInFrame() == pd,
                  "(2) a job of ANOTHER template hits the record the hook names alike: no parse - the hook's key "
                  "is the only key the hit uses");
            gHookConstant = false;
            for (const char *n : { "hit/a", "hit/b", "hit/c", "hit/d" }) hc->destroyComputeJob(n);
        }

        // (4) THE MEMO, cleared by clearShaderCache
        {
            Ogre::HlmsComputeJob *e = makeJob(hc, "memo/e", "JahHitB");
            hc->dispatch(e, nullptr, nullptr);
            writeFile(dir + "/JahHitB.glsl", templateText("B edited"));
            hc->clearShaderCache();
            Ogre::HlmsComputeJob *f = makeJob(hc, "memo/f", "JahHitB");
            const Ogre::uint64 pf = hc->getJahParsedInFrame();
            hc->dispatch(f, nullptr, nullptr);
            hc->getJahRecords(recs);
            bool edited = false;
            for (const auto &r : recs) edited |= r->source.find("// B edited") != std::string::npos;
            CHECK(hc->getJahParsedInFrame() == pf + 1u && edited,
                  "(4) after clearShaderCache the edited template is read afresh: the next build parses its new text");
            hc->destroyComputeJob("memo/e");
            hc->destroyComputeJob("memo/f");
        }

        // (5) F2a: equal text, slots of other types
        {
            l.rs->getMutableCapabilities()->setCapability(Ogre::RSC_EXPLICIT_API);
            Ogre::TextureGpuManager *tm = l.rs->getTextureGpuManager();
            Ogre::TextureGpu *tex = tm->createTexture("hit/tex", Ogre::GpuPageOutStrategy::Discard,
                                                      Ogre::TextureFlags::ManualTexture, Ogre::TextureTypes::Type2D);
            tex->setResolution(4u, 4u);
            tex->setPixelFormat(Ogre::PFG_RGBA8_UNORM);
            Ogre::TexBufferPacked *buf =
                l.rs->getVaoManager()->createTexBuffer(Ogre::PFG_R32_UINT, 64u, Ogre::BT_DEFAULT, nullptr, false);
            gHookLayout = false;
            Ogre::HlmsComputeJob *g = makeJob(hc, "layout/g", "JahHitA");
            Ogre::HlmsComputeJob *h = makeJob(hc, "layout/h", "JahHitA");
            g->setNumTexUnits(1u);
            h->setNumTexUnits(1u);
            Ogre::DescriptorSetTexture2::TextureSlot ts(Ogre::DescriptorSetTexture2::TextureSlot::makeEmpty());
            ts.texture = tex;
            g->setTexture(0u, ts, nullptr, false);
            Ogre::DescriptorSetTexture2::BufferSlot bs(Ogre::DescriptorSetTexture2::BufferSlot::makeEmpty());
            bs.buffer = buf;
            h->setTexBuffer(0u, bs);
            // Under the layout-blind hook both jobs file under ONE key: the record there names the program built
            // last, with the root layout it was built for.
            auto recordOf = [&](Ogre::uint64 key0, Ogre::uint64 key1) -> Ogre::HlmsCompute::JahCodeRecordPtr {
                hc->getJahRecords(recs);
                for (const auto &r : recs)
                    if (r->inputKey[0] == key0 && r->inputKey[1] == key1) return r;
                return nullptr;
            };
            const size_t n0 = (hc->getJahRecords(recs), recs.size());
            hc->dispatch(g, nullptr, nullptr);
            hc->getJahRecords(recs);
            Ogre::HlmsCompute::JahCodeRecordPtr rg;
            for (const auto &r : recs)
                if (r->name == "layout/g") rg = r;
            const Ogre::uint64 ph = hc->getJahParsedInFrame();
            hc->dispatch(h, nullptr, nullptr);
            const Ogre::HlmsCompute::JahCodeRecordPtr rh = rg ? recordOf(rg->inputKey[0], rg->inputKey[1]) : nullptr;
            const bool twoPrograms = rg && rh && rh->name == "layout/h" &&
                                     (rh->programKey[0] != rg->programKey[0] || rh->programKey[1] != rg->programKey[1]) &&
                                     rh->rootLayout != rg->rootLayout;
            CHECK(rg && n0 + 1u == (hc->getJahRecords(recs), recs.size()),
                  "(5) (the layout-blind hook names both jobs alike: one key)");
            CHECK(hc->getJahParsedInFrame() == ph + 1u && twoPrograms,
                  "(5) F2a: equal text, a texture slot vs a buffer slot - the second job MISSES (parsed +%llu) and "
                  "its program is another (%s)",
                  (unsigned long long)(hc->getJahParsedInFrame() - ph), twoPrograms ? "two programs" : "ONE SHARED");
            gHookLayout = true;
            hc->destroyComputeJob("layout/g");
            hc->destroyComputeJob("layout/h");
            l.rs->getVaoManager()->destroyTexBuffer(buf);
            tm->destroyTexture(tex);
            l.rs->getMutableCapabilities()->unsetCapability(Ogre::RSC_EXPLICIT_API);
        }

        // (6) F2b: the render system's properties are the key's
        {
            l.rs->getMutableCapabilities()->setCapability(Ogre::RSC_SHADER_FLOAT16);
            Ogre::HlmsComputeJob *m = makeJob(hc, "rs/m", "JahHitA");
            hc->dispatch(m, nullptr, nullptr);
            hc->getJahRecords(recs);
            const size_t before = recs.size();
            hc->setPrecisionMode(Ogre::Hlms::PrecisionMidf16);
            Ogre::HlmsComputeJob *n = makeJob(hc, "rs/n", "JahHitA");
            hc->dispatch(n, nullptr, nullptr);
            hc->getJahRecords(recs);
            CHECK(recs.size() == before + 1u,
                  "(6) F2b: the same job after a precision-mode change files under a NEW key (%zu -> %zu records)",
                  before, recs.size());
            hc->setPrecisionMode(Ogre::Hlms::PrecisionFull32);
            hc->destroyComputeJob("rs/m");
            hc->destroyComputeJob("rs/n");
        }
        Ogre::HlmsCompute::msJahComputeKeyHook = nullptr;
    }

    // (7) and (3): THE NEXT LAUNCH
    {
        Launch l;
        if (!l.boot(dir, "hit-2.log")) { std::printf("FAIL: the second NULL boot\n"); return 1; }
        Ogre::HlmsCompute *hc = l.hc;
        Ogre::HlmsCompute::msJahComputeKeyHook = hook;
        Ogre::HlmsAsyncCompiler *compiler = l.root->getHlmsManager()->getAsyncCompiler();
        compiler->setNumThreads(1u, 0);
        // (3) a record of the other template, its source tampered after its program key was taken
        std::vector<Ogre::HlmsCompute::JahCodeRecord> records = handedOver;
        CHECK(records.size() == 1u, "(7) the first launch handed over its record");
        Ogre::HlmsCompute::JahCodeRecord forged = records[0];
        forged.inputKey[0] ^= 1u;
        forged.source += "// another compiler's source\n";
        records.push_back(forged);
        hc->jahPrewarmCode(records);
        compiler->waitForAll();
        std::vector<Ogre::HlmsCompute::JahCodeRecordPtr> recs;
        hc->getJahRecords(recs);
        CHECK(recs.size() == 1u, "(3) a record whose source does not hash to its program key is NOT filed (%zu kept)",
              recs.size());
        Ogre::HlmsComputeJob *a = makeJob(hc, "next/a", "JahHitA");
        const Ogre::uint64 p0 = hc->getJahParsedInFrame(), s0 = hc->getJahPsoInFrame();
        hc->dispatch(a, nullptr, nullptr);
        CHECK(hc->getJahParsedInFrame() == p0 && hc->getJahPsoInFrame() == s0 + 1u,
              "(7) the next launch's first dispatch builds its PSO from the prewarmed code: parsed +%llu, PSOs +%llu",
              (unsigned long long)(hc->getJahParsedInFrame() - p0), (unsigned long long)(hc->getJahPsoInFrame() - s0));
        hc->destroyComputeJob("next/a");
        compiler->setNumThreads(0u);
        Ogre::HlmsCompute::msJahComputeKeyHook = nullptr;
    }

    // (8) THE JOIN: the next launch dispatches BEFORE its prewarmed code is published (the atmosphere's synchronous LUT
    // dispatches on a warm boot: four template parses in the gate's frames, shadercache.app.warm's red). A program enters
    // the code cache only at the code job's publish, on the main thread; the hit on a record whose job is in flight joins
    // that job instead of parsing.
    {
        Launch l;
        if (!l.boot(dir, "hit-3.log")) { std::printf("FAIL: the third NULL boot\n"); return 1; }
        Ogre::HlmsCompute *hc = l.hc;
        Ogre::HlmsCompute::msJahComputeKeyHook = hook;
        Ogre::HlmsAsyncCompiler *compiler = l.root->getHlmsManager()->getAsyncCompiler();
        compiler->setNumThreads(1u, 0);
        std::vector<Ogre::HlmsCompute::JahCodeRecord> records = handedOver;
        hc->jahPrewarmCode(records);   // its code job submitted; nothing publishes it before the dispatch below
        Ogre::HlmsComputeJob *a = makeJob(hc, "join/a", "JahHitA");
        const Ogre::uint64 p0 = hc->getJahParsedInFrame(), s0 = hc->getJahPsoInFrame();
        hc->dispatch(a, nullptr, nullptr);
        CHECK(hc->getJahParsedInFrame() == p0 && hc->getJahPsoInFrame() == s0 + 1u,
              "(8) a dispatch while the record's code job is in flight JOINS it: parsed +%llu, PSOs +%llu",
              (unsigned long long)(hc->getJahParsedInFrame() - p0), (unsigned long long)(hc->getJahPsoInFrame() - s0));
        hc->destroyComputeJob("join/a");
        compiler->setNumThreads(0u);
        Ogre::HlmsCompute::msJahComputeKeyHook = nullptr;
    }

    // (9) THE SHARED JOB's PUBLISH (stage 7a): one job, two permutations - A (fClearValue, a float target's clear) and B
    // (uClearValue, a uint target's): the voxel clear's ComputeTools job interleaves them. A is built and dispatched with
    // V0; the job then holds A's NEXT value V1, pending, when the compiler publishes B (a publish runs between two
    // dispatches: HlmsCompute::waitForAsyncPermutation, the frame's publish). 8010ec7bb FORCED the job's values onto every
    // published program: B's took V1 (on a program that declares no fClearValue, Ogre's ITEM_NOT_FOUND - twice on every
    // first launch) and the visit cleared V1's dirty flag, so A's next dispatch, which applies only dirty values on a
    // counter change, kept V0 - A cleared with a STALE value. The law: a publish writes no value and consumes none; each
    // program gets the job's values whole at its own first dispatch; a dispatch clears with ITS value.
    {
        Launch l;
        if (!l.boot(dir, "hit-4.log")) { std::printf("FAIL: the fourth NULL boot\n"); return 1; }
        Ogre::HlmsCompute *hc = l.hc;
        Ogre::HlmsCompute::msJahComputeKeyHook = hook;
        Ogre::HlmsAsyncCompiler *compiler = l.root->getHlmsManager()->getAsyncCompiler();
        compiler->setNumThreads(1u, 0);   // one service thread: the publishes come in request order
        Ogre::HlmsComputeJob *job = makeJob(hc, "clear/shared", "JahHitA");
        const auto pending = [&](const char *name, float base) {   // what ComputeTools::clearUav does before its dispatch
            Ogre::ShaderParams &sp = job->getShaderParams("default");
            sp.mParams.clear();
            Ogre::ShaderParams::Param p;
            p.name = name;
            if (name[0] == 'u') {
                const Ogre::uint32 v[4] = { Ogre::uint32(base), Ogre::uint32(base) + 1u, Ogre::uint32(base) + 2u,
                                            Ogre::uint32(base) + 3u };
                p.setManualValue(v, 4u);
            } else {
                const float v[4] = { base, base + 1.0f, base + 2.0f, base + 3.0f };
                p.setManualValue(v, 4u);
            }
            sp.mParams.push_back(p);
            sp.setDirty();
        };
        const auto stillPending = [&]() {
            const Ogre::ShaderParams &sp = job->getShaderParams("default");
            return !sp.mParams.empty() && sp.mParams[0].isDirty;
        };
        const auto floatOf = [](const Ogre::GpuProgramParametersSharedPtr &p) { return p ? *p->getFloatPointer(0) : -1.0f; };
        const auto uintOf = [](const Ogre::GpuProgramParametersSharedPtr &p) { return p ? *p->getUnsignedIntPointer(0) : 0u; };
        gMadeParams.clear();
        gCaptureParams = true;
        // A: requested, published, dispatched with V0 = 1
        hc->setDeferredDispatch(true);
        job->setProperty("jah_clear_kind", 0);
        pending("fClearValue", 1.0f);
        hc->dispatch(job, nullptr, nullptr);
        compiler->waitForAll();
        hc->setDeferredDispatch(false);
        hc->dispatch(job, nullptr, nullptr);
        const Ogre::GpuProgramParametersSharedPtr a = gMadeParams.size() == 1u ? gMadeParams[0] : nullptr;
        CHECK(a && floatOf(a) == 1.0f, "(9) A built asynchronously and dispatched: its program holds V0 (%g)", floatOf(a));
        // B: requested
        hc->setDeferredDispatch(true);
        job->setProperty("jah_clear_kind", 1);
        pending("uClearValue", 20.0f);
        hc->dispatch(job, nullptr, nullptr);
        // the job back on A with A's next value V1 = 5 pending, when B publishes
        job->setProperty("jah_clear_kind", 0);
        pending("fClearValue", 5.0f);
        compiler->waitForAll();
        hc->setDeferredDispatch(false);
        const Ogre::GpuProgramParametersSharedPtr b = gMadeParams.size() == 2u ? gMadeParams[1] : nullptr;
        CHECK(b && floatOf(b) == 0.0f && uintOf(b) == 0u && stillPending(),
              "(9) B's publish wrote no value into its program (float %g, uint %u; 8010ec7bb: the job's V1) and consumed "
              "none (A's V1 %s)", floatOf(b), uintOf(b), stillPending() ? "still pending" : "CONSUMED");
        hc->dispatch(job, nullptr, nullptr);
        CHECK(floatOf(a) == 5.0f, "(9) A's next dispatch clears with ITS value V1 (%g; 8010ec7bb: the stale V0 = 1)",
              floatOf(a));
        job->setProperty("jah_clear_kind", 1);
        pending("uClearValue", 30.0f);
        hc->dispatch(job, nullptr, nullptr);
        CHECK(uintOf(b) == 30u, "(9) B's own first dispatch clears with B's value (%u)", uintOf(b));
        gCaptureParams = false;
        gMadeParams.clear();
        hc->destroyComputeJob("clear/shared");
        compiler->setNumThreads(0u);
        Ogre::HlmsCompute::msJahComputeKeyHook = nullptr;
    }

    std::system(("rm -rf '" + dir + "'").c_str());
    std::printf(gFailures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", gFailures);
    return gFailures ? 1 : 0;
}

// A LAW THAT FAILS FAILS - it never crashes or aborts the row: an exception (Ogre's included) is a FAIL line and exit 1.
int main() {
    try {
        return run();
    } catch (const std::exception &e) {
        std::printf("FAIL: an exception ended the row: %s\nRESULT: 1 FAILURE(S)\n", e.what());
        return 1;
    }
}
