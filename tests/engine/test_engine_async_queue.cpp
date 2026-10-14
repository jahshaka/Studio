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
//        what a device without one gets): the Component exists, hasComputeQueue() is false,
//        the bracket's values still advance, no GPU work, and the picture equals the
//        no-bracket picture.
//   --validation <log>  THE SYNC CHECK, run under VK_LAYER_KHRONOS_validation with
//        synchronization validation on and the layer logging to <log>: 300 async frames,
//        then the log must hold zero messages.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <OgreFrameListener.h>
#include <OgreRoot.h>
#include <OgreVulkanAsyncComputeQueue.h>
#include <OgreVulkanDevice.h>
#include <OgreVulkanRenderSystem.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
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
    r.engine->setFixedFrameDelta(1.0f / 60.0f);
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

static int mainValidation(const char *logPath) {
    std::remove(logPath);   // the layer appends: a previous run's log must not count
    {
        Rig r;
        if (!boot(r, "test-async-queue-validation-ogre.log")) return 1;
        CHECK(r.acq->isAsync(), "the async arm runs under the layer");
        Bracket br;
        br.acq = r.acq;
        br.on = true;
        Ogre::Root::getSingleton().addFrameListener(&br);
        if (!frames(r, 300, nullptr)) return 1;
        r.device->stall();
        Ogre::Root::getSingleton().removeFrameListener(&br);
        CHECK(negativeControls(r.device), "the two negative controls were recorded");
        char msg[160];
        std::snprintf(msg, sizeof msg, "300 async brackets ran (%u)", br.brackets);
        CHECK(br.brackets == 300u, msg);
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
    return mainDefault();
}
