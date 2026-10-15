// engine.async_queue — THE ASYNC COMPUTE QUEUE ON THE FORK (ENGINE-UPGRADE-ASYNC stage 1).
//
// The fork's Component VulkanAsyncComputeQueue (RenderSystems/Vulkan) owns a second hardware
// queue and two timeline semaphores: G signalled by the graphics queue at any named point,
// C by the compute queue at the end of each bracket. Stage 1 proves the plumbing with an
// EMPTY bracket per frame (stage 2 records into it):
//
//   frame N-1's end : g = signalPoint("tail"); begin(g); c = end(); waitAtFrameEnd(c)
//   frame N's start : waitAt(c, ALL_GRAPHICS)
//
// What each mode asserts (counts and bytes, never time):
//   (default) THE QUEUE + THE BRACKET + THE BOUND:
//     1. the families are reported and the queue the Component took is the preferred one:
//        a compute-only family when the device has one, else a second queue of a compute
//        family; timeline semaphores enabled;
//     2. 300 frames with no bracket, 300 with the async bracket, 300 with the bracket in the
//        off arm (setEnabled(false)): the three pictures are the same bytes, 300 brackets ran
//        on the compute queue and C reached every value;
//     3. 1,000 more async frames: the compute queue's command buffers == its slot count and
//        its fences unchanged; the graphics queue's command buffers unchanged (bounded).
//   --no-family   THE FALLBACK, run with JAH_VK_ASYNC_COMPUTE=0 (no compute queue requested,
//        what a device without one gets). (The other off arm, "a queue but no timeline
//        semaphores", is proved by READING only: no device here lacks timelines.)
//        The Component exists, hasComputeQueue() is false,
//        the bracket's values still advance, no GPU work, and the picture equals the
//        no-bracket picture.
//   --validation <log>  THE SYNC CHECK, run under VK_LAYER_KHRONOS_validation with
//        synchronization validation on and the layer logging to <log>: 300 async frames,
//        then the log must hold zero messages.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <Compositor/OgreCompositorManager2.h>
#include <OgreFrameListener.h>
#include <OgreHlmsCompute.h>
#include <OgreHlmsComputeJob.h>
#include <OgreHlmsManager.h>
#include <OgreResourceGroupManager.h>
#include <OgreShaderParams.h>
#include <OgreTextureGpuManager.h>
#include <OgreResourceTransition.h>
#include <Vao/OgreAsyncTicket.h>
#include <Vao/OgreUavBufferPacked.h>
#include <Vao/OgreVaoManager.h>
#include <OgreRoot.h>
#include <OgreVulkanAsyncComputeQueue.h>
#include <OgreVulkanDevice.h>
#include <OgreVulkanRenderSystem.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

// The bracket, driven from Ogre's frame events (the engine fires both every frame).
struct Bracket : public Ogre::FrameListener {
    Ogre::VulkanAsyncComputeQueue *acq = nullptr;
    bool on = false;
    Ogre::uint64 pending = 0;     // the last bracket's C value, waited by the next frame
    unsigned brackets = 0;
    bool frameStarted(const Ogre::FrameEvent &) override {
        if (on && pending) acq->waitAt(pending, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT);
        pending = 0;
        return true;
    }
    bool frameEnded(const Ogre::FrameEvent &) override {
        if (!on) return true;
        const Ogre::uint64 g = acq->signalPoint("jah.test.tail");
        acq->begin(g);
        pending = acq->end();
        acq->waitAtFrameEnd(pending);
        ++brackets;
        return true;
    }
};

static uint64_t fnv(const Image &img) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char b : img.rgba) { h ^= b; h *= 1099511628211ull; }
    return h;
}

struct Rig {
    std::unique_ptr<Engine> engine;
    View *view = nullptr;
    Scene *scene = nullptr;
    Ogre::VulkanDevice *device = nullptr;
    Ogre::VulkanAsyncComputeQueue *acq = nullptr;
};

static bool boot(Rig &r, const char *log) {
    std::string err;
    r.engine = Engine::create(enginetest::testEngineConfig(log), err);
    if (!r.engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return false; }
    r.engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    r.view = r.engine->createOffscreenView("async", 160, 120, Colour(0.1f, 0.1f, 0.12f));
    r.scene = r.engine->createScene("async");
    if (!r.view || !r.scene) { std::printf("FAIL: view/scene\n"); return false; }
    r.view->setScene(r.scene);
    // A fixture of our own: a floor, two cubes, a sun with its shadows.
    const NodeId floor = enginetest::addTestCube(r.scene, Colour(0.6f, 0.6f, 0.6f), 0.0f, 0.8f);
    enginetest::setNodeScale(r.scene, floor, Vec3(8, 0.1f, 8));
    enginetest::setNodePosition(r.scene, floor, Vec3(0, -0.05f, 0));
    const NodeId a = enginetest::addTestCube(r.scene, Colour(0.8f, 0.2f, 0.2f), 0.0f, 0.5f);
    enginetest::setNodePosition(r.scene, a, Vec3(-0.8f, 0.5f, 0));
    const NodeId b = enginetest::addTestCube(r.scene, Colour(0.2f, 0.3f, 0.8f), 1.0f, 0.2f);
    enginetest::setNodePosition(r.scene, b, Vec3(0.9f, 0.5f, -0.5f));
    enginetest::addDirectionalLight(r.scene, Vec3(-0.4f, -1.0f, -0.3f), 3.0f);
    enginetest::testCameraLookAt(r.view, Vec3(0, 2.0f, 4.0f), Vec3(0, 0.3f, 0));
    auto *rs = dynamic_cast<Ogre::VulkanRenderSystem *>(Ogre::Root::getSingleton().getRenderSystem());
    r.device = rs ? rs->getVulkanDevice() : nullptr;
    r.acq = r.device ? r.device->getAsyncCompute() : nullptr;
    if (!r.acq) { std::printf("FAIL: no Vulkan device / no async compute Component\n"); return false; }
    return true;
}

static bool gUniform = true;   // the last read picture was one colour (the fixture unseen)
static bool frames(Rig &r, int n, uint64_t *hash) {
    for (int i = 0; i < n; ++i) r.engine->renderOneFrame();
    if (!hash) return true;
    Image img;
    if (!r.view->readPixels(img) || img.rgba.empty()) { std::printf("FAIL: readPixels\n"); return false; }
    *hash = fnv(img);
    gUniform = true;
    for (size_t i = 4; i + 3 < img.rgba.size() && gUniform; i += 4)
        gUniform = std::memcmp(&img.rgba[i], &img.rgba[0], 3) == 0;
    return true;
}

static int mainDefault() {
    Rig r;
    if (!boot(r, "test-async-queue-ogre.log")) return 1;
    Ogre::VulkanDevice *d = r.device;

    std::printf("-- 1. the families and the queue taken\n");
    int computeOnly = -1;
    for (size_t i = 0; i < d->mQueueProps.size(); ++i) {
        const VkQueueFlags f = d->mQueueProps[i].queueFlags;
        std::printf("   family %zu: %s%s%s%s count %u timestampValidBits %u\n", i,
                    (f & VK_QUEUE_GRAPHICS_BIT) ? "G" : "", (f & VK_QUEUE_COMPUTE_BIT) ? "C" : "",
                    (f & VK_QUEUE_TRANSFER_BIT) ? "T" : "", (f & VK_QUEUE_SPARSE_BINDING_BIT) ? "S" : "",
                    d->mQueueProps[i].queueCount, d->mQueueProps[i].timestampValidBits);
        if (computeOnly < 0 && (f & VK_QUEUE_COMPUTE_BIT) && !(f & VK_QUEUE_GRAPHICS_BIT)) computeOnly = int(i);
    }
    std::printf("   graphics family %u queue %u; async compute: %s, family %u queue %u, %zu slots\n",
                d->mGraphicsQueue.getFamilyIdx(), d->mGraphicsQueue.mQueueIdx,
                r.acq->hasComputeQueue() ? "a second queue" : "OFF", r.acq->getFamilyIdx(),
                r.acq->getQueueIdx(), r.acq->getNumSlots());
    CHECK(d->mDeviceExtraFeatures.timelineSemaphore, "timeline semaphores are enabled on the device");
    CHECK(r.acq->hasComputeQueue() && r.acq->isAsync(), "the Component has its compute queue and is async");
    if (computeOnly >= 0)
        CHECK(r.acq->getFamilyIdx() == unsigned(computeOnly),
              "the queue is the device's compute-only family (the preference's first choice)");
    else
        CHECK(r.acq->getQueueIdx() != d->mGraphicsQueue.mQueueIdx ||
                  r.acq->getFamilyIdx() != d->mGraphicsQueue.getFamilyIdx(),
              "no compute-only family: a second queue, never the graphics queue itself");
    if (!r.acq->hasComputeQueue()) return 1;

    std::printf("-- 2. no bracket / the async bracket / the off arm: one picture\n");
    Bracket br;
    br.acq = r.acq;
    Ogre::Root::getSingleton().addFrameListener(&br);
    uint64_t hNone = 0, hAsync = 0, hOff = 0;
    if (!frames(r, 300, &hNone)) return 1;
    CHECK(!gUniform, "the fixture is in the picture (not one colour)");
    br.on = true;
    const Ogre::uint64 c0 = r.acq->getLastComputeValue();
    const Ogre::uint64 n0 = r.acq->getNumAsyncBrackets();
    if (!frames(r, 300, &hAsync)) return 1;
    const Ogre::uint64 asyncRan = r.acq->getNumAsyncBrackets() - n0;
    d->stall();
    const Ogre::uint64 last = r.acq->getLastComputeValue();
    char msg[256];
    std::snprintf(msg, sizeof msg, "300 async frames ran %llu brackets on the compute queue (C %llu -> %llu)",
                  (unsigned long long)asyncRan, (unsigned long long)c0, (unsigned long long)last);
    CHECK(asyncRan == 300u && last == c0 + 300u, msg);
    std::snprintf(msg, sizeof msg, "C reached every value signalled (%llu of %llu)",
                  (unsigned long long)r.acq->getCompletedComputeValue(), (unsigned long long)last);
    CHECK(r.acq->getCompletedComputeValue() == last, msg);
    r.acq->setEnabled(false);
    const Ogre::uint64 n1 = r.acq->getNumAsyncBrackets();
    if (!frames(r, 300, &hOff)) return 1;
    CHECK(r.acq->getNumAsyncBrackets() == n1 && r.acq->getLastComputeValue() == last + 300u,
          "the off arm: the values advance (300), no bracket reaches the compute queue");
    d->stall();
    CHECK(r.acq->getCompletedComputeValue() == last + 300u,
          "...and C is still one timeline: the graphics queue signalled the in-order values");
    std::printf("   hashes: none %016llx async %016llx off %016llx\n", (unsigned long long)hNone,
                (unsigned long long)hAsync, (unsigned long long)hOff);
    CHECK(hNone == hAsync && hAsync == hOff, "the three arms render the same bytes");

    std::printf("-- 3. 1,000 async frames: the compute queue's command buffers and fences are bounded\n");
    r.acq->setEnabled(true);
    if (!frames(r, 20, nullptr)) return 1;
    const size_t gfxCmd0 = d->mGraphicsQueue._jahNumAllocatedCmdBuffers();
    const size_t fences0 = r.acq->getNumFences();
    if (!frames(r, 1000, nullptr)) return 1;
    std::snprintf(msg, sizeof msg, "compute command buffers %zu == slots %zu after 1,000 frames",
                  r.acq->getNumCommandBuffers(), r.acq->getNumSlots());
    CHECK(r.acq->getNumCommandBuffers() == r.acq->getNumSlots(), msg);
    std::snprintf(msg, sizeof msg, "compute fences %zu unchanged (%zu)", r.acq->getNumFences(), fences0);
    CHECK(r.acq->getNumFences() == fences0, msg);
    std::snprintf(msg, sizeof msg, "graphics command buffers %zu unchanged (%zu)",
                  d->mGraphicsQueue._jahNumAllocatedCmdBuffers(), gfxCmd0);
    CHECK(d->mGraphicsQueue._jahNumAllocatedCmdBuffers() == gfxCmd0, msg);

    Ogre::Root::getSingleton().removeFrameListener(&br);
    std::printf("%s\n", failures ? "engine.async_queue: FAILED" : "engine.async_queue: all ok");
    return failures ? 1 : 0;
}

// ===================================================================================================
// STAGE 2 — THE RECORDING: a fixture dispatch in the bracket, read by the graphics queue after waitAt.
//
// Two compute jobs of our own (written to disk by the test, nothing staged):
//   AsyncTest/Fill    — in the bracket: dst[i] = seed.x * 1000003 + i * seed.y, seed.x = the frame's
//                       index — a per-frame SHADER PARAM, i.e. data in the auto-param buffer of the
//                       graphics frame ring (the dynamic data the frame-data rule protects);
//   AsyncTest/Consume — on the GRAPHICS queue at the next frame's start, after waitAt: acc[i] += dst[i].
// THE PROTOCOL IS THE ONE-FRAME-BEHIND ONE: the bracket opens at frame N-1's frameRenderingQueued
// (after the frame's passes, before its swap — G = that point), graphics frame N waits C at its start.
// After K brackets acc has a CLOSED FORM: acc[i] = sum over f of (f * 1000003 + 7 i) mod 2^32 —
// any lost, doubled, stale or torn bracket breaks it.
static const uint32_t kN = 4096u;
static const char *kFillSrc = R"GLSL(
@insertpiece( SetCrossPlatformSettings )
vulkan( layout( ogre_P0 ) uniform Params { )
	uniform uvec4 seed;
vulkan( }; )
layout( std430, ogre_U0 ) writeonly restrict buffer dstLayout { uint dst[]; };
layout( local_size_x = @value( threads_per_group_x ),
		local_size_y = @value( threads_per_group_y ),
		local_size_z = @value( threads_per_group_z ) ) in;
void main()
{
	const uint i = gl_GlobalInvocationID.x;
	if( i < seed.z )
		dst[i] = seed.x * 1000003u + i * seed.y;
}
)GLSL";
static const char *kConsumeSrc = R"GLSL(
@insertpiece( SetCrossPlatformSettings )
vulkan( layout( ogre_P0 ) uniform Params { )
	uniform uvec4 count;
vulkan( }; )
layout( std430, ogre_U0 ) readonly restrict buffer srcLayout { uint src[]; };
layout( std430, ogre_U1 ) restrict buffer accLayout { uint acc[]; };
layout( local_size_x = @value( threads_per_group_x ),
		local_size_y = @value( threads_per_group_y ),
		local_size_z = @value( threads_per_group_z ) ) in;
void main()
{
	const uint i = gl_GlobalInvocationID.x;
	if( i < count.x )
		acc[i] += src[i];
}
)GLSL";

static Ogre::DescriptorSetUav::BufferSlot uavSlot(Ogre::UavBufferPacked *b, Ogre::ResourceAccess::ResourceAccess a) {
    Ogre::DescriptorSetUav::BufferSlot slot = Ogre::DescriptorSetUav::BufferSlot::makeEmpty();
    slot.buffer = b;
    slot.access = a;
    return slot;
}

struct Jobs {
    Ogre::HlmsCompute *hc = nullptr;
    Ogre::HlmsComputeJob *fill = nullptr, *consume = nullptr;
    Ogre::UavBufferPacked *dst = nullptr, *acc = nullptr;
};

static bool makeJobs(Jobs &j) {
    const std::string dir = "async-record-media";
    std::system(("mkdir -p " + dir).c_str());
    { std::ofstream(dir + "/AsyncTestFill_cs.glsl") << kFillSrc; }
    { std::ofstream(dir + "/AsyncTestConsume_cs.glsl") << kConsumeSrc; }
    Ogre::ResourceGroupManager &rgm = Ogre::ResourceGroupManager::getSingleton();
    rgm.addResourceLocation(dir, "FileSystem", "AsyncRecordTest", false);
    rgm.initialiseResourceGroup("AsyncRecordTest", true);
    j.hc = Ogre::Root::getSingleton().getHlmsManager()->getComputeHlms();
    Ogre::StringVector pieces;
    pieces.push_back("CrossPlatformSettings_piece_all");
    j.fill = j.hc->createComputeJob("AsyncTest/Fill", "AsyncTest/Fill", "AsyncTestFill_cs", pieces);
    j.consume = j.hc->createComputeJob("AsyncTest/Consume", "AsyncTest/Consume", "AsyncTestConsume_cs", pieces);
    for (Ogre::HlmsComputeJob *job : { j.fill, j.consume }) {
        job->setThreadsPerGroup(64u, 1u, 1u);
        job->setNumThreadGroups(kN / 64u, 1u, 1u);
    }
    j.fill->setNumUavUnits(1u);
    j.consume->setNumUavUnits(2u);
    Ogre::ShaderParams &fp = j.fill->getShaderParams("default");
    Ogre::ShaderParams::Param seed;
    seed.name = "seed";
    const Ogre::uint32 z[4] = { 0u, 7u, kN, 0u };
    seed.setManualValue(z, 4u);
    fp.mParams.push_back(seed);
    fp.setDirty();
    Ogre::ShaderParams &cp = j.consume->getShaderParams("default");
    Ogre::ShaderParams::Param count;
    count.name = "count";
    const Ogre::uint32 c[4] = { kN, 0u, 0u, 0u };
    count.setManualValue(c, 4u);
    cp.mParams.push_back(count);
    cp.setDirty();
    Ogre::VaoManager *vao = Ogre::Root::getSingleton().getRenderSystem()->getVaoManager();
    std::vector<Ogre::uint32> zeros(kN, 0u);
    j.dst = vao->createUavBuffer(kN, 4u, 0u, zeros.data(), false);
    j.acc = vao->createUavBuffer(kN, 4u, 0u, zeros.data(), false);
    j.fill->_setUavBuffer(0u, uavSlot(j.dst, Ogre::ResourceAccess::Write));
    j.consume->_setUavBuffer(0u, uavSlot(j.dst, Ogre::ResourceAccess::Read));
    j.consume->_setUavBuffer(1u, uavSlot(j.acc, Ogre::ResourceAccess::ReadWrite));
    return j.fill && j.consume && j.dst && j.acc;
}

// The jobs live in HlmsCompute and hold raw pointers to the buffers: unbound, then the buffers go,
// then the jobs, all before the engine.
static void releaseJobs(Jobs &j) {
    const Ogre::DescriptorSetUav::BufferSlot empty = Ogre::DescriptorSetUav::BufferSlot::makeEmpty();
    if (j.fill) j.fill->_setUavBuffer(0u, empty);
    if (j.consume) { j.consume->_setUavBuffer(0u, empty); j.consume->_setUavBuffer(1u, empty); }
    Ogre::RenderSystem *rs = Ogre::Root::getSingleton().getRenderSystem();
    rs->getVaoManager()->destroyUavBuffer(j.dst);
    rs->getVaoManager()->destroyUavBuffer(j.acc);
    if (j.hc) {
        j.hc->destroyComputeJob("AsyncTest/Fill");
        j.hc->destroyComputeJob("AsyncTest/Consume");
    }
    j = Jobs();
}

static void dispatch(Ogre::HlmsCompute *hc, Ogre::HlmsComputeJob *job) {
    Ogre::RenderSystem *rs = Ogre::Root::getSingleton().getRenderSystem();
    Ogre::ResourceTransitionArray &rt = rs->getBarrierSolver().getNewResourceTransitionsArrayTmp();
    job->analyzeBarriers(rt);
    rs->executeResourceTransition(rt);
    hc->dispatch(job, 0, 0);
}

struct Recorder : public Ogre::FrameListener {
    Ogre::VulkanAsyncComputeQueue *acq = nullptr;
    Jobs *j = nullptr;
    int bracketsLeft = 0;         // brackets still to open
    Ogre::uint64 pending = 0;     // the last bracket's C value, consumed by the next frame
    Ogre::uint32 frameIndex = 0;  // the seed of the next bracket
    unsigned opened = 0, consumed = 0;
    int toggleEvery = 0;          // >0: setEnabled flips every toggleEvery frames (on -> off -> on)
    unsigned frames = 0, toggles = 0;
    bool frameStarted(const Ogre::FrameEvent &) override {
        ++frames;
        if (toggleEvery > 0 && frames % unsigned(toggleEvery) == 0u) {
            acq->setEnabled(!acq->isEnabled());
            ++toggles;
        }
        if (pending) {
            acq->waitAt(pending, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            dispatch(j->hc, j->consume);
            pending = 0;
            ++consumed;
        }
        return true;
    }
    bool frameRenderingQueued(const Ogre::FrameEvent &) override {
        if (bracketsLeft <= 0) return true;
        const Ogre::uint64 g = acq->signalPoint("jah.test.tail");
        acq->begin(g, "jah.test.fill");
        Ogre::ShaderParams &fp = j->fill->getShaderParams("default");
        const Ogre::uint32 seed[4] = { frameIndex, 7u, kN, 0u };
        fp.mParams[0].setManualValue(seed, 4u);
        fp.setDirty();
        dispatch(j->hc, j->fill);
        pending = acq->end();
        ++frameIndex;
        ++opened;
        --bracketsLeft;
        return true;
    }
};

static bool readAcc(Jobs &j, std::vector<Ogre::uint32> &out) {
    out.assign(kN, 0u);
    Ogre::AsyncTicketPtr t = j.acc->readRequest(0u, kN);
    std::memcpy(out.data(), t->map(), kN * 4u);
    t->unmap();
    return true;
}

// One arm: K brackets one frame behind, the last consumed, then acc read and checked against the
// closed form. Returns the number of wrong words.
static unsigned runArm(Rig &r, Jobs &j, Recorder &rec, int K, int toggleEvery, const char *tag) {
    std::vector<Ogre::uint32> zeros(kN, 0u);
    j.acc->upload(zeros.data(), 0u, kN);   // a copy: outside every bracket
    rec.bracketsLeft = K;
    rec.frameIndex = 0;
    rec.opened = rec.consumed = 0;
    rec.toggleEvery = toggleEvery;
    rec.frames = rec.toggles = 0;
    for (int f = 0; f < K + 2; ++f) r.engine->renderOneFrame();
    rec.toggleEvery = 0;
    std::vector<Ogre::uint32> acc;
    readAcc(j, acc);
    unsigned wrong = 0;
    for (uint32_t i = 0; i < kN; ++i) {
        Ogre::uint32 want = 0u;
        for (int f = 0; f < K; ++f) want += Ogre::uint32(f) * 1000003u + i * 7u;
        if (acc[i] != want) ++wrong;
    }
    char msg[200];
    std::snprintf(msg, sizeof msg, "%s: %u brackets opened, %u consumed, acc == the closed form in %u of %u words%s",
                  tag, rec.opened, rec.consumed, kN - wrong, kN,
                  toggleEvery ? (" (" + std::to_string(rec.toggles) + " on/off switches)").c_str() : "");
    CHECK(rec.opened == unsigned(K) && rec.consumed == unsigned(K) && wrong == 0u, msg);
    return wrong;
}

static int mainRecord() {
    Rig r;
    if (!boot(r, "test-async-record-ogre.log")) return 1;
    for (int i = 0; i < 10; ++i) r.engine->renderOneFrame();
    Jobs j;
    CHECK(makeJobs(j), "the two fixture compute jobs and their buffers exist");
    CHECK(r.device->mJahShareFamilyCount == 2u,
          "the pools and flagged images are CONCURRENT over the graphics and the compute family");
    Recorder rec;
    rec.acq = r.acq;
    rec.j = &j;
    Ogre::Root::getSingleton().addFrameListener(&rec);
    const int K = 240;
    std::printf("-- 1. the in-order arm (setEnabled(false)) — the reference\n");
    r.acq->setEnabled(false);
    runArm(r, j, rec, K, 0, "in order");
    std::printf("-- 2. the async arm, one frame behind\n");
    r.acq->setEnabled(true);
    const Ogre::uint64 n0 = r.acq->getNumAsyncBrackets();
    const Ogre::uint64 rec0 = r.acq->getNumSlotRecycles();
    runArm(r, j, rec, K, 0, "async");
    char msg[220];
    std::snprintf(msg, sizeof msg, "the async arm's brackets ran on the compute queue (%llu)",
                  (unsigned long long)(r.acq->getNumAsyncBrackets() - n0));
    CHECK(r.acq->getNumAsyncBrackets() - n0 == Ogre::uint64(K), msg);
    std::printf("-- 3. THE FRAME-DATA RULE: a slot whose bracket was waited by the next frame is never recycled early\n");
    const Ogre::uint64 recycles = r.acq->getNumSlotRecycles() - rec0;
    std::snprintf(msg, sizeof msg, "%llu recycles of a slot that recorded a bracket (>= %d), %llu had to wait, %llu violations",
                  (unsigned long long)recycles, K - 4, (unsigned long long)r.acq->getNumRecyclesHeld(),
                  (unsigned long long)r.acq->getNumRecycleViolations());
    CHECK(recycles >= Ogre::uint64(K - 4) && r.acq->getNumRecycleViolations() == 0u, msg);
    std::printf("-- 4. on -> off -> on every 40 frames (C migrating between the queues)\n");
    runArm(r, j, rec, K, 40, "toggled");
    r.acq->setEnabled(true);
    Ogre::Root::getSingleton().removeFrameListener(&rec);
    r.device->stall();
    releaseJobs(j);
    std::printf("%s\n", failures ? "engine.async_record: FAILED" : "engine.async_record: all ok");
    return failures ? 1 : 0;
}

// THE GRAPHICS-ONLY CHECKS ARE RUNTIME-ACTIVE: inside a bracket (either arm) a graphics-only command
// throws an Ogre::Exception that NAMES it, the bracket is still closable, and the frame after works.
static bool expectThrow(const char *what, const char *needle, const std::function<void()> &f) {
    try { f(); }
    catch (const Ogre::Exception &e) {
        const bool named = e.getFullDescription().find(needle) != std::string::npos;
        char msg[300];
        std::snprintf(msg, sizeof msg, "%s throws, naming '%s'", what, needle);
        CHECK(named, msg);
        if (!named) std::printf("   (it said: %s)\n", e.getFullDescription().substr(0, 240).c_str());
        return named;
    }
    char msg[200];
    std::snprintf(msg, sizeof msg, "%s throws", what);
    CHECK(false, msg);
    return false;
}

static int mainGraphicsCheck() {
    Rig r;
    if (!boot(r, "test-async-graphics-check-ogre.log")) return 1;
    for (int i = 0; i < 10; ++i) r.engine->renderOneFrame();
    Jobs j;
    if (!makeJobs(j)) { std::printf("FAIL: jobs\n"); return 1; }
    Ogre::RenderSystem *rs = Ogre::Root::getSingleton().getRenderSystem();
    std::vector<Ogre::uint32> zeros(kN, 0u);
    for (int arm = 0; arm < 2; ++arm) {
        r.acq->setEnabled(arm == 0);
        std::printf("-- the %s arm\n", arm == 0 ? "async" : "in-order");
        r.acq->begin(r.acq->signalPoint("jah.test.check"));
        dispatch(j.hc, j.fill);                                      // compute work: fine
        // A ROUTED COPY (ENGINE-UPGRADE-ASYNC-3 stage 1): a staging upload records on the record
        // queue, legal on a compute family; the graphics queue's own copy encoder is still refused.
        {
            bool threw = false;
            try { j.acc->upload(zeros.data(), 0u, kN); } catch (const Ogre::Exception &) { threw = true; }
            CHECK(!threw, "an upload inside the bracket is a routed copy (no throw)");
        }
        expectThrow("the graphics queue's copy encoder inside the bracket", "a copy", [&] {
            r.device->mGraphicsQueue.getCopyEncoder(j.acc, nullptr, false, Ogre::CopyEncTransitionMode::Auto);
        });
        expectThrow("a graphics PSO bind inside the bracket", "a graphics pipeline bind",
                    [&] { rs->_setPipelineStateObject(nullptr); });
        expectThrow("a second begin", "a bracket is already open", [&] { r.acq->begin(0u); });
        expectThrow("setEnabled inside the bracket", "setEnabled inside a bracket", [&] { r.acq->setEnabled(true); });
        const Ogre::uint64 c = r.acq->end();
        expectThrow("waitAt on a value no bracket signalled", "no bracket has signalled",
                    [&] { r.acq->waitAt(c + 5u, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT); });
        expectThrow("waitAtFrameEnd on a value no bracket signalled", "no bracket has signalled",
                    [&] { r.acq->waitAtFrameEnd(c + 5u); });
        expectThrow("end without begin", "end without begin", [&] { r.acq->end(); });
        r.acq->waitAt(c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        for (int i = 0; i < 5; ++i) r.engine->renderOneFrame();
        j.acc->upload(zeros.data(), 0u, kN);                         // outside: fine
    }
    // A BRACKET A THROW LEAVES OPEN (the read's W3): the engine catches and goes on, so the next
    // frame's top closes it (loudly) instead of the blocked graphics queue throwing every frame.
    for (int arm = 0; arm < 2; ++arm) {
        r.acq->setEnabled(arm == 0);
        const Ogre::uint64 before = r.acq->getLastComputeValue();
        try {
            r.acq->begin(r.acq->signalPoint("jah.test.leak"), "jah.test.leaked");
            dispatch(j.hc, j.fill);
            rs->_setPipelineStateObject(nullptr);                    // throws: the bracket is left open
        } catch (const Ogre::Exception &) {}
        const bool leaked = r.acq->isOpen();
        const std::string errBefore = r.engine->lastError();   // a frame that throws replaces it
        r.engine->renderOneFrame();
        const std::string err = r.engine->lastError() == errBefore ? std::string() : r.engine->lastError();
        char msg[300];
        std::snprintf(msg, sizeof msg, "%s arm: a bracket left open by a throw is closed at the next frame's top "
                      "(open before %d, after %d, C +%llu, the frame's error '%s')", arm == 0 ? "async" : "in-order",
                      int(leaked), int(r.acq->isOpen()),
                      (unsigned long long)(r.acq->getLastComputeValue() - before), err.substr(0, 120).c_str());
        CHECK(leaked && !r.acq->isOpen() && r.acq->getLastComputeValue() == before + 1u && err.empty(), msg);
        for (int i = 0; i < 3; ++i) r.engine->renderOneFrame();
        // ...and THE SCOPE GUARD closes it where the throw leaves the scope.
        try {
            Ogre::VulkanAsyncComputeBracket bracket(r.acq, r.acq->signalPoint("jah.test.scope"), "jah.test.scoped");
            dispatch(j.hc, j.fill);
            rs->_setPipelineStateObject(nullptr);                    // throws
            bracket.end();
        } catch (const Ogre::Exception &) {}
        std::snprintf(msg, sizeof msg, "%s arm: the scope guard closed the bracket the throw left (C +%llu)",
                      arm == 0 ? "async" : "in-order",
                      (unsigned long long)(r.acq->getLastComputeValue() - before));
        CHECK(!r.acq->isOpen() && r.acq->getLastComputeValue() == before + 2u, msg);
        {
            Ogre::VulkanAsyncComputeBracket bracket(r.acq, r.acq->signalPoint("jah.test.scope2"));
            dispatch(j.hc, j.fill);
            const Ogre::uint64 c = bracket.end();
            r.acq->waitAt(c, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            expectThrow("a second end of a scoped bracket", "already ended", [&] { bracket.end(); });
        }
        for (int i = 0; i < 3; ++i) r.engine->renderOneFrame();
        CHECK(r.engine->lastError() == errBefore, "the frames after the leaks rendered without an error");
    }
    r.acq->setEnabled(true);
    r.device->stall();
    releaseJobs(j);
    CHECK(true, "the frames after every refused command rendered (the bracket closed cleanly)");
    std::printf("%s\n", failures ? "engine.async_graphics_check: FAILED" : "engine.async_graphics_check: all ok");
    return failures ? 1 : 0;
}

static int mainNoFamily() {
    const char *env = std::getenv("JAH_VK_ASYNC_COMPUTE");
    if (!env || std::strcmp(env, "0") != 0) {
        std::printf("FAIL: --no-family runs with JAH_VK_ASYNC_COMPUTE=0\n");
        return 1;
    }
    Rig r;
    if (!boot(r, "test-async-queue-nofamily-ogre.log")) return 1;
    CHECK(!r.acq->hasComputeQueue() && !r.acq->isAsync() && r.device->mComputeQueues.empty(),
          "no compute queue requested: the Component is its off arm");
    CHECK(r.acq->getFamilyIdx() == r.device->mGraphicsQueue.getFamilyIdx(),
          "...and names the graphics family (where its work records, in order)");
    Bracket br;
    br.acq = r.acq;
    Ogre::Root::getSingleton().addFrameListener(&br);
    uint64_t hNone = 0, hBracket = 0;
    if (!frames(r, 300, &hNone)) return 1;
    br.on = true;
    if (!frames(r, 300, &hBracket)) return 1;
    CHECK(br.brackets == 300u && r.acq->getLastComputeValue() == 300u && r.acq->getNumAsyncBrackets() == 0u,
          "300 brackets: the values advance, none reaches a GPU queue");
    CHECK(r.acq->getNumCommandBuffers() == 0u, "no compute command buffer exists");
    CHECK(hNone == hBracket, "the picture equals the no-bracket picture");
    Ogre::Root::getSingleton().removeFrameListener(&br);
    std::printf("%s\n", failures ? "engine.async_queue_nofamily: FAILED" : "engine.async_queue_nofamily: all ok");
    return failures ? 1 : 0;
}


// THE NEGATIVE CONTROLS of the validation row: one plain validation error (a binary semaphore's
// counter read: VUID-vkGetSemaphoreCounterValue-semaphore-03255) and one SYNCHRONIZATION hazard
// (two fills of one buffer, no barrier between: SYNC-HAZARD-WRITE-AFTER-WRITE), recorded on the
// compute queue the Component owns. A row whose log stays empty with these in it proves nothing.
static bool negativeControls(Ogre::VulkanDevice *d) {
    VkDevice dev = d->mDevice;
    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore binary = VK_NULL_HANDLE;
    if (vkCreateSemaphore(dev, &sci, nullptr, &binary) != VK_SUCCESS) return false;
    uint64_t v = 0;
    (void)vkGetSemaphoreCounterValue(dev, binary, &v);
    vkDestroySemaphore(dev, binary, nullptr);

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = 4096;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buf = VK_NULL_HANDLE;
    if (vkCreateBuffer(dev, &bci, nullptr, &buf) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, buf, &req);
    uint32_t type = 0;
    for (; type < d->mDeviceMemoryProperties.memoryTypeCount; ++type)
        if (req.memoryTypeBits & (1u << type)) break;
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = type;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (vkAllocateMemory(dev, &mai, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindBufferMemory(dev, buf, mem, 0);
    Ogre::VulkanQueue *q = d->getAsyncCompute()->getQueue();
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = q->getFamilyIdx();
    VkCommandPool pool = VK_NULL_HANDLE;
    vkCreateCommandPool(dev, &pci, nullptr, &pool);
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(dev, &cai, &cb);
    VkCommandBufferBeginInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cb, &cbi);
    vkCmdFillBuffer(cb, buf, 0, 4096, 1u);
    vkCmdFillBuffer(cb, buf, 0, 4096, 2u);      // the hazard: no barrier between the writes
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    vkQueueSubmit(q->mQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(q->mQueue);
    vkDestroyCommandPool(dev, pool, nullptr);
    vkDestroyBuffer(dev, buf, nullptr);
    vkFreeMemory(dev, mem, nullptr);
    return true;
}

// THE TEXTURE BARRIERS IN THE BRACKET (the stage-2 read's W1): an AsyncComputeShared render target
// handed to the compute queue (RenderTarget -> Uav: the source stage is COLOR_ATTACHMENT_OUTPUT) and
// handed back to a fragment reader (Uav -> Texture for the pixel shader: the destination stage is
// FRAGMENT_SHADER). Recorded on a compute-only family those stages are VUID-vkCmdPipelineBarrier-
// srcStageMask-06461 / -dstStageMask-06462 unless the barrier is fitted to the recording queue.
static bool bracketTextureBarriers(Rig &r) {
    Ogre::RenderSystem *rs = Ogre::Root::getSingleton().getRenderSystem();
    Ogre::TextureGpuManager *tm = rs->getTextureGpuManager();
    Ogre::TextureGpu *t = tm->createTexture(
        "AsyncTest/SharedTarget", Ogre::GpuPageOutStrategy::Discard,
        Ogre::TextureFlags::RenderToTexture | Ogre::TextureFlags::Uav | Ogre::TextureFlags::AsyncComputeShared,
        Ogre::TextureTypes::Type2D);
    t->setResolution(64u, 64u);
    t->setPixelFormat(Ogre::PFG_RGBA16_FLOAT);
    t->scheduleTransitionTo(Ogre::GpuResidency::Resident);
    Ogre::BarrierSolver &bs = rs->getBarrierSolver();
    auto go = [&](Ogre::ResourceLayout::Layout l, Ogre::ResourceAccess::ResourceAccess a, Ogre::uint8 stages) {
        Ogre::ResourceTransitionArray &rt = bs.getNewResourceTransitionsArrayTmp();
        bs.resolveTransition(rt, t, l, a, stages);
        rs->executeResourceTransition(rt);
    };
    unsigned handed = 0;
    for (int f = 0; f < 3; ++f) {
        go(Ogre::ResourceLayout::RenderTarget, Ogre::ResourceAccess::Write, 0u);     // graphics: a target
        r.acq->begin(r.acq->signalPoint("jah.test.target"), "jah.test.texture_barriers");
        go(Ogre::ResourceLayout::Uav, Ogre::ResourceAccess::ReadWrite, Ogre::uint8(1u << Ogre::GPT_COMPUTE_PROGRAM));
        go(Ogre::ResourceLayout::Texture, Ogre::ResourceAccess::Read, Ogre::uint8(1u << Ogre::PixelShader));
        const Ogre::uint64 c = r.acq->end();
        r.acq->waitAt(c, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        r.engine->renderOneFrame();
        ++handed;
    }
    r.device->stall();
    bs.textureDeleted(t);
    tm->destroyTexture(t);
    return handed == 3u;
}

static int mainValidation(const char *logPath) {
    std::remove(logPath);   // the layer appends: a previous run's log must not count
    {
        Rig r;
        if (!boot(r, "test-async-queue-validation-ogre.log")) return 1;
        CHECK(r.acq->isAsync(), "the async arm runs under the layer");
        // THE REAL PATH: the fixture dispatch one frame behind, in order, async, and switched
        // on -> off -> on (C migrating between the queues, G jumping at each enable).
        for (int i = 0; i < 10; ++i) r.engine->renderOneFrame();
        Jobs j;
        CHECK(makeJobs(j), "the fixture jobs exist");
        Recorder rec;
        rec.acq = r.acq;
        rec.j = &j;
        Ogre::Root::getSingleton().addFrameListener(&rec);
        r.acq->setEnabled(false);
        runArm(r, j, rec, 60, 0, "in order");
        r.acq->setEnabled(true);
        runArm(r, j, rec, 120, 0, "async");
        runArm(r, j, rec, 120, 30, "toggled");
        r.acq->setEnabled(true);
        Ogre::Root::getSingleton().removeFrameListener(&rec);
        r.device->stall();
        releaseJobs(j);
        CHECK(bracketTextureBarriers(r),
              "a shared render target handed to the compute queue and back to a fragment reader, 3 frames");
        CHECK(negativeControls(r.device), "the two negative controls were recorded");
    }   // the engine (and the instance) end here, so the layer has said everything
    std::ifstream in(logPath);
    CHECK(bool(in), "the validation layer wrote its log (the layer was loaded)");
    // The layer writes each message as a block: a "Validation Error:/Warning:" head line, the
    // text, its objects, a blank line.
    std::vector<std::string> blocks;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("Validation Error:", 0) == 0 || line.rfind("Validation Warning:", 0) == 0)
            blocks.push_back(line);
        else if (!blocks.empty())
            blocks.back() += "\n" + line;
    }
    unsigned messages = 0, controlError = 0, controlHazard = 0;
    for (const std::string &m : blocks) {
        if (m.find("VUID-vkGetSemaphoreCounterValue-semaphore-03255") != std::string::npos) { ++controlError; continue; }
        if (m.find("SYNC-HAZARD-WRITE-AFTER-WRITE") != std::string::npos &&
            m.find("vkCmdFillBuffer") != std::string::npos) { ++controlHazard; continue; }
        if (messages < 12) std::printf("   %s\n", m.substr(0, 400).c_str());
        ++messages;
    }
    char msg[160];
    std::snprintf(msg, sizeof msg, "the controls were reported: the error %u, the sync hazard %u",
                  controlError, controlHazard);
    CHECK(controlError > 0 && controlHazard > 0, msg);
    std::snprintf(msg, sizeof msg, "validation + synchronization validation: %u messages besides the controls", messages);
    CHECK(messages == 0u, msg);
    std::printf("%s\n", failures ? "engine.async_queue_validation: FAILED" : "engine.async_queue_validation: all ok");
    return failures ? 1 : 0;
}

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // the lines in order with the engine's
    if (argc > 1 && std::strcmp(argv[1], "--no-family") == 0) return mainNoFamily();
    if (argc > 2 && std::strcmp(argv[1], "--validation") == 0) return mainValidation(argv[2]);
    if (argc > 1 && std::strcmp(argv[1], "--record") == 0) return mainRecord();
    if (argc > 1 && std::strcmp(argv[1], "--graphics-check") == 0) return mainGraphicsCheck();
    return mainDefault();
}
