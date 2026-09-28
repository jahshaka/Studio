// engine.descriptor_pools — EVERY RAW DESCRIPTOR POOL IS ITS LAYOUTS, AND HOLDS THEM
// (D6-FORK-TOOLING, audit V2-D F8).
//
// The ray passes own raw Vulkan descriptor pools (Ogre's descriptor types have no
// acceleration structure). Their sizes used to be counted by hand beside the layout
// they serve, so a binding added without its pool line over-allocated the pool: an
// OUT_OF_POOL_MEMORY on a strict driver, silence on this one. Every pool is now a
// DescriptorPoolPlan (irisgl/engine/src/VkDescriptorPools.h) built from the very
// binding arrays its layouts were created from, and it PROVES itself at birth: it
// allocates every set it was planned for at once, then resets.
//
// What each case proves:
//   1. THE ARITHMETIC: a plan's per-type sizes are the sum of its layouts' binding
//      descriptorCounts (arrays included) times their sets, and maxSets their sets.
//   2. ONE CALL MAKES BOTH: makeLayout creates the layout and plans exactly its
//      bindings x sets; the pool proves itself full and is empty again after. And
//      the measurement behind the design, printed (never asserted — it is the
//      driver's): a pool one binding short of its layout, the old hand-count defect,
//      ALLOCATES on NVIDIA; only the validation layer would say so. So the fix is the
//      construction, not a runtime check.
//   3. THE LIVE POOLS: a ray-traced scene (mirror, emissive carded cube, sun, voxel
//      GI with the screen-probe gather, traced SSR) creates the tier's pools, and
//      every pool the process created allocated its MAXIMUM at birth. The trace, the
//      instance job, the reflection and the gather's are required; every one is listed.
// A machine without ray queries runs cases 1-2 and skips 3 (the no-rays picture is
// a supported picture, not a failure).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include "VkDescriptorPools.h"

#include <OgreRoot.h>
#include <OgreVulkanDevice.h>
#include <OgreVulkanRenderSystem.h>

#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using jahshaka::engine::detail::DescriptorPoolPlan;
using jahshaka::engine::detail::DescriptorPoolProof;
using jahshaka::engine::detail::descriptorPoolProofs;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static VkDescriptorSetLayoutBinding binding(uint32_t i, VkDescriptorType t, uint32_t n) {
    VkDescriptorSetLayoutBinding b{};
    b.binding = i;
    b.descriptorType = t;
    b.descriptorCount = n;
    b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    return b;
}

static void arithmeticCase() {
    std::printf("-- 1. the plan's arithmetic\n");
    const VkDescriptorSetLayoutBinding a[3] = {
        binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1),
        binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4),   // an array of 4
        binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1) };
    const VkDescriptorSetLayoutBinding b[2] = {
        binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
        binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2) };
    DescriptorPoolPlan plan;
    plan.add(VK_NULL_HANDLE, a, 3u, 3u);
    plan.add(VK_NULL_HANDLE, b, 2u, 5u);
    CHECK(plan.maxSets() == 8u, "maxSets is the sets of every layout (3 + 5)");
    CHECK(plan.count(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) == 6u,
          "storage buffers: 2 per set x 3 sets");
    CHECK(plan.count(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) == 22u,
          "samplers: the array's 4 x 3 sets + 2 x 5 sets (arrays counted whole)");
    CHECK(plan.count(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) == 5u, "uniform buffers: 1 x 5 sets");
    CHECK(plan.sizes().size() == 3u, "one pool size per descriptor type, merged across layouts");
}

static void oneCallCase(VkDevice dev) {
    std::printf("-- 2. the layout and its pool line come from one call\n");
    const VkDescriptorSetLayoutBinding real[2] = {
        binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1),
        binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3) };
    std::string err;
    DescriptorPoolPlan plan;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    CHECK(plan.makeLayout(dev, real, 2u, 4u, layout, err, "test/one-call") && layout,
          "makeLayout creates the layout");
    if (!layout) return;
    CHECK(plan.maxSets() == 4u && plan.count(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) == 4u &&
              plan.count(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) == 12u,
          "...and planned exactly its bindings x its sets (4 sets: 4 buffers, 12 samplers)");
    VkDescriptorPool pool = VK_NULL_HANDLE;
    CHECK(plan.create(dev, "test/one-call", 0, pool, err) && pool, "the planned pool proves itself full");
    if (pool) {
        std::vector<VkDescriptorSetLayout> layouts(4, layout);
        std::vector<VkDescriptorSet> sets(4, VK_NULL_HANDLE);
        VkDescriptorSetAllocateInfo dai{};
        dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dai.descriptorPool = pool;
        dai.descriptorSetCount = 4;
        dai.pSetLayouts = layouts.data();
        CHECK(vkAllocateDescriptorSets(dev, &dai, sets.data()) == VK_SUCCESS,
              "after its proof the pool is empty again and holds all four sets");
        vkDestroyDescriptorPool(dev, pool, nullptr);
    }
    // THE MEASUREMENT BEHIND THE DESIGN (informational, never a pass/fail: it is the
    // driver's behaviour, not ours). The old defect, a pool one binding short of its
    // layout: does THIS driver refuse to over-allocate it? NVIDIA does not, which is
    // why the fix is the one-call construction and not the allocation proof.
    DescriptorPoolPlan stale;
    stale.add(layout, real, 1u, 4u);
    const bool staleOk = stale.create(dev, "test/stale", 0, pool, err);
    std::printf("   note: a pool one binding short of its layout %s on this driver%s\n",
                staleOk ? "ALLOCATED all its sets" : "was refused",
                staleOk ? " (only the validation layer reports it)" : "");
    if (pool) vkDestroyDescriptorPool(dev, pool, nullptr);
    vkDestroyDescriptorSetLayout(dev, layout, nullptr);
}

int main() {
    arithmeticCase();

    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-descriptor-pools-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("pools", 192, 192, Colour(0, 0, 0));
    Scene *s = e->createScene("pools");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);

    auto *vkRs = dynamic_cast<Ogre::VulkanRenderSystem *>(Ogre::Root::getSingleton().getRenderSystem());
    CHECK(vkRs && vkRs->getVulkanDevice(), "the render system is Vulkan and has its device");
    if (!vkRs || !vkRs->getVulkanDevice()) return 1;
    oneCallCase(vkRs->getVulkanDevice()->mDevice);

    if (!(e->rayQueryAvailable() && e->rayTracing())) {
        std::printf("ok: no ray queries on this device/run (available=%d wanted=%d) — the live "
                    "pools case skips\n", int(e->rayQueryAvailable()), int(e->rayTracing()));
        std::printf("%s\n", failures ? "engine.descriptor_pools: FAILED" : "engine.descriptor_pools: all ok");
        return failures ? 1 : 0;
    }

    std::printf("-- 3. the live pools each allocated their maximum at birth\n");
    s->setAmbient(Colour(0.2f, 0.2f, 0.2f), Colour(0.15f, 0.15f, 0.15f));
    const NodeId wall = s->createNode();
    PbrParams wp;
    wp.albedo = Colour(1, 1, 1);
    wp.metalness = 1.0f;
    wp.roughness = 0.0f;
    CHECK(wall && s->attachMesh(wall, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(wp)),
          "the mirror wall exists");
    enginetest::setNodeScale(s, wall, Vec3(14.0f, 9.0f, 0.3f));
    enginetest::setNodePosition(s, wall, Vec3(0.0f, 2.0f, 5.0f));
    const NodeId cube = s->createNode();
    {
        PbrParams p;
        p.albedo = Colour(0.05f, 0.05f, 0.05f);
        p.emissive = Colour(0.9f, 0.0f, 0.0f);
        p.roughness = 0.6f;
        MeshData md = enginetest::unitCubeMesh();
        for (unsigned a = 0; a < 6u; ++a) {
            MeshCardDesc c;
            c.axis = static_cast<unsigned char>(a);
            c.origin = Vec3(0, 0, 0);
            c.halfU = 0.5f;
            c.halfV = 0.5f;
            c.halfDepth = 0.52f;
            md.cards.push_back(c);
        }
        CHECK(cube && s->attachMesh(cube, s->createMesh(md), s->createPbrMaterial(p)),
              "the emissive carded cube exists");
    }
    enginetest::setNodeScale(s, cube, Vec3(3.0f, 3.0f, 3.0f));
    enginetest::setNodePosition(s, cube, Vec3(0.0f, 2.0f, -11.0f));
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.4f), 2.0f);
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.gather = GiToggle::On;
    CHECK(s->setGlobalIllumination(gi), "voxel GI with the screen-probe gather builds");
    view->setOffscreenContract(OffscreenContract::StillPicture);   // the gather runs offscreen
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;                                                     // the traced reflection
    view->setPostFx(fx);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, -6.0f), Vec3(0.0f, 2.0f, 5.0f));
    for (int i = 0; i < 90; ++i) e->renderOneFrame();

    bool trace = false, tlasWrite = false, reflect = false, gather = false;
    unsigned live = 0, full = 0;
    for (const DescriptorPoolProof &p : descriptorPoolProofs()) {
        if (p.name.rfind("test/", 0) == 0) continue;   // case 2's own pools
        ++live;
        if (p.full) ++full;
        trace = trace || p.name == "rayquery/trace";
        tlasWrite = tlasWrite || p.name == "rayquery/tlas-write";
        reflect = reflect || p.name == "rayquery/reflect";
        gather = gather || p.name == "gather";
        std::printf("   pool %-22s %4u sets %6u descriptors  %s\n", p.name.c_str(), p.sets,
                    p.descriptors, p.full ? "held its maximum" : "REFUSED");
    }
    CHECK(trace && tlasWrite && reflect && gather,
          "the tier's pools were created: trace, instance job, reflection, gather");
    char msg[160];
    std::snprintf(msg, sizeof msg, "every pool the process created held its maximum at birth (%u of %u)",
                  full, live);
    CHECK(live > 0 && full == live, msg);

    std::printf("%s\n", failures ? "engine.descriptor_pools: FAILED" : "engine.descriptor_pools: all ok");
    return failures ? 1 : 0;
}
