// engine.atom_negative_control — stage 0's P-B as a PERMANENT suite (SPECS/atom/
// D1_HLMS_ATOM_AND_VISBUF_DESIGN.md §1; spikes/atom-stage0/FINDINGS.md §7.4).
//
// THE CLAIM. A custom pass of ours — the engine's ONE CompositorPassProvider
// (irisgl/engine/src/AtomPass.h), multiplexed on customId — may record raw Vulkan
// into Ogre's open command buffer (its own compute pipeline, graphics pipeline,
// render pass, descriptor sets, push constants and index buffer) and OGRE'S NEXT
// PASS IS UNTOUCHED: byte-identical, every one of 120 consecutive frames, to the same
// pass in a workspace where our pass never ran. What makes that true is the
// provider's epilogue — `_setPipelineStateObject(nullptr)` + `RenderQueue::
// clearState()` — and this suite is what keeps it true when the provider, the pin or
// a recorder changes.
//
// THE CONSTRUCTION. One scene (a PBS cube and a sun), two workspaces on it with the
// view's camera, appended after the view's:
//   WITH     [custom atom_negative_probe] then a render_scene pass, into target A
//   WITHOUT  the same render_scene pass alone, into target B
// Our recorder, every frame: a barrier, a compute dispatch writing a triangle (and the
// frame number) into OUR storage buffer, a barrier, OUR render pass on OUR image
// drawing that triangle with OUR graphics pipeline through OUR index buffer. The
// storage buffer is host-visible, so the suite also proves the recorder RAN, every
// frame (the frame number it wrote).
//
// VALIDATION: the `_validation` entries run the same binary under
// VK_LAYER_KHRONOS_validation (RTX and lavapipe) and fail on any report (CTest's
// FAIL_REGULAR_EXPRESSION) — the spike's recipe as a gate.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include "AtomPass.h"
#include "EnginePrivate.h"

#include <Compositor/OgreCompositorManager2.h>
#include <Compositor/OgreCompositorNodeDef.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <Compositor/OgreCompositorWorkspaceDef.h>
#include <Compositor/Pass/PassScene/OgreCompositorPassSceneDef.h>
#include <OgreCamera.h>
#include <OgreImage2.h>
#include <OgreRoot.h>
#include <OgreSceneManager.h>
#include <OgreTextureBox.h>
#include <OgreTextureGpuManager.h>
#include <OgreVulkanDevice.h>
#include <OgreVulkanRenderSystem.h>

#include <vulkan/vulkan.h>

#include "validation_probe.h"

#include <cstdio>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "atom_negative_comp_spv.h"
#include "atom_negative_frag_spv.h"
#include "atom_negative_vert_spv.h"

using namespace jahshaka::engine;
using jahshaka::engine::detail::AtomPassContext;
using jahshaka::engine::detail::AtomPassProvider;
using jahshaka::engine::detail::AtomPassRecorder;
using jahshaka::engine::detail::OgreView;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[512];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static const unsigned kW = 256, kH = 256, kImg = 64;
static const int kFrames = 120;

// ---------------------------------------------------------------------------
// OUR PIPELINE — raw Vulkan, every object ours.
// ---------------------------------------------------------------------------
struct Ours {
    VkDevice dev = VK_NULL_HANDLE;
    VkBuffer ssbo = VK_NULL_HANDLE, index = VK_NULL_HANDLE;
    VkDeviceMemory ssboMem = VK_NULL_HANDLE, indexMem = VK_NULL_HANDLE, imageMem = VK_NULL_HANDLE;
    void *ssboMap = nullptr;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkRenderPass rp = VK_NULL_HANDLE;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkPipelineLayout compLayout = VK_NULL_HANDLE, gfxLayout = VK_NULL_HANDLE;
    VkPipeline comp = VK_NULL_HANDLE, gfx = VK_NULL_HANDLE;
    bool imageInitialised = false;
    unsigned recorded = 0;
    uint32_t frame = 0;
};

static uint32_t memoryType(const VkPhysicalDeviceMemoryProperties &props, uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}

static VkShaderModule module(VkDevice dev, const uint32_t *code, size_t bytes)
{
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(dev, &ci, nullptr, &m);
    return m;
}

static bool createOurs(Ogre::VulkanDevice *device, Ours &o)
{
    o.dev = device->mDevice;
    const VkPhysicalDeviceMemoryProperties &mp = device->mDeviceMemoryProperties;
    auto makeBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer &buf, VkDeviceMemory &mem) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(o.dev, &bi, nullptr, &buf) != VK_SUCCESS) return false;
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(o.dev, buf, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = memoryType(mp, req.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(o.dev, &ai, nullptr, &mem) != VK_SUCCESS) return false;
        return vkBindBufferMemory(o.dev, buf, mem, 0) == VK_SUCCESS;
    };
    if (!makeBuffer(64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, o.ssbo, o.ssboMem)) return false;
    if (!makeBuffer(16, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, o.index, o.indexMem)) return false;
    vkMapMemory(o.dev, o.ssboMem, 0, VK_WHOLE_SIZE, 0, &o.ssboMap);
    std::memset(o.ssboMap, 0, 64);
    {
        void *p = nullptr;
        vkMapMemory(o.dev, o.indexMem, 0, VK_WHOLE_SIZE, 0, &p);
        const uint32_t idx[4] = { 0u, 1u, 2u, 0u };
        std::memcpy(p, idx, sizeof(idx));
        vkUnmapMemory(o.dev, o.indexMem);
    }
    // OUR IMAGE, our render pass, our framebuffer.
    {
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = VK_FORMAT_R8G8B8A8_UNORM;
        ii.extent = { kImg, kImg, 1 };
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(o.dev, &ii, nullptr, &o.image) != VK_SUCCESS) return false;
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(o.dev, o.image, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = memoryType(mp, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX) ai.memoryTypeIndex = memoryType(mp, req.memoryTypeBits, 0);
        if (vkAllocateMemory(o.dev, &ai, nullptr, &o.imageMem) != VK_SUCCESS) return false;
        vkBindImageMemory(o.dev, o.image, o.imageMem, 0);
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = o.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(o.dev, &vi, nullptr, &o.view) != VK_SUCCESS) return false;
        // initialLayout == finalLayout: the pass performs no transition of its own
        // (the first frame's UNDEFINED -> COLOR_ATTACHMENT is an explicit barrier).
        VkAttachmentDescription at{};
        at.format = VK_FORMAT_R8G8B8A8_UNORM;
        at.samples = VK_SAMPLE_COUNT_1_BIT;
        at.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        at.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        at.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        at.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        at.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sp{};
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1;
        sp.pColorAttachments = &ref;
        VkRenderPassCreateInfo rpi{};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpi.attachmentCount = 1;
        rpi.pAttachments = &at;
        rpi.subpassCount = 1;
        rpi.pSubpasses = &sp;
        if (vkCreateRenderPass(o.dev, &rpi, nullptr, &o.rp) != VK_SUCCESS) return false;
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = o.rp;
        fi.attachmentCount = 1;
        fi.pAttachments = &o.view;
        fi.width = kImg;
        fi.height = kImg;
        fi.layers = 1;
        if (vkCreateFramebuffer(o.dev, &fi, nullptr, &o.fb) != VK_SUCCESS) return false;
    }
    // OUR DESCRIPTOR SET: the one storage buffer, seen by compute and vertex.
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 1;
        li.pBindings = &b;
        if (vkCreateDescriptorSetLayout(o.dev, &li, nullptr, &o.setLayout) != VK_SUCCESS) return false;
        VkDescriptorPoolSize ps{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets = 1;
        pi.poolSizeCount = 1;
        pi.pPoolSizes = &ps;
        if (vkCreateDescriptorPool(o.dev, &pi, nullptr, &o.pool) != VK_SUCCESS) return false;
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = o.pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &o.setLayout;
        if (vkAllocateDescriptorSets(o.dev, &ai, &o.set) != VK_SUCCESS) return false;
        VkDescriptorBufferInfo bi{ o.ssbo, 0, VK_WHOLE_SIZE };
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = o.set;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w.pBufferInfo = &bi;
        vkUpdateDescriptorSets(o.dev, 1, &w, 0, nullptr);
    }
    // OUR PIPELINES.
    {
        VkPushConstantRange pcc{ VK_SHADER_STAGE_COMPUTE_BIT, 0, 4 };
        VkPipelineLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount = 1;
        li.pSetLayouts = &o.setLayout;
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &pcc;
        if (vkCreatePipelineLayout(o.dev, &li, nullptr, &o.compLayout) != VK_SUCCESS) return false;
        VkPushConstantRange pcg{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4 };
        li.pPushConstantRanges = &pcg;
        if (vkCreatePipelineLayout(o.dev, &li, nullptr, &o.gfxLayout) != VK_SUCCESS) return false;

        VkShaderModule cm = module(o.dev, test_comp, sizeof(test_comp));
        VkComputePipelineCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.module = cm;
        ci.stage.pName = "main";
        ci.layout = o.compLayout;
        const VkResult rc = vkCreateComputePipelines(o.dev, VK_NULL_HANDLE, 1, &ci, nullptr, &o.comp);
        vkDestroyShaderModule(o.dev, cm, nullptr);
        if (rc != VK_SUCCESS) return false;

        VkShaderModule vm = module(o.dev, test_vert, sizeof(test_vert));
        VkShaderModule fm = module(o.dev, test_frag, sizeof(test_frag));
        VkPipelineShaderStageCreateInfo st[2]{};
        st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        st[0].module = vm;
        st[0].pName = "main";
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        st[1].module = fm;
        st[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo vin{};
        vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport vp{ 0.0f, 0.0f, float(kImg), float(kImg), 0.0f, 1.0f };
        VkRect2D sc{ { 0, 0 }, { kImg, kImg } };
        VkPipelineViewportStateCreateInfo vs{};
        vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount = 1;
        vs.pViewports = &vp;
        vs.scissorCount = 1;
        vs.pScissors = &sc;
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;
        VkGraphicsPipelineCreateInfo gi{};
        gi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gi.stageCount = 2;
        gi.pStages = st;
        gi.pVertexInputState = &vin;
        gi.pInputAssemblyState = &ia;
        gi.pViewportState = &vs;
        gi.pRasterizationState = &rs;
        gi.pMultisampleState = &ms;
        gi.pColorBlendState = &cb;
        gi.layout = o.gfxLayout;
        gi.renderPass = o.rp;
        const VkResult rg = vkCreateGraphicsPipelines(o.dev, VK_NULL_HANDLE, 1, &gi, nullptr, &o.gfx);
        vkDestroyShaderModule(o.dev, vm, nullptr);
        vkDestroyShaderModule(o.dev, fm, nullptr);
        if (rg != VK_SUCCESS) return false;
    }
    return true;
}

static void destroyOurs(Ours &o)
{
    if (!o.dev) return;
    vkDeviceWaitIdle(o.dev);
    if (o.gfx) vkDestroyPipeline(o.dev, o.gfx, nullptr);
    if (o.comp) vkDestroyPipeline(o.dev, o.comp, nullptr);
    if (o.gfxLayout) vkDestroyPipelineLayout(o.dev, o.gfxLayout, nullptr);
    if (o.compLayout) vkDestroyPipelineLayout(o.dev, o.compLayout, nullptr);
    if (o.pool) vkDestroyDescriptorPool(o.dev, o.pool, nullptr);
    if (o.setLayout) vkDestroyDescriptorSetLayout(o.dev, o.setLayout, nullptr);
    if (o.fb) vkDestroyFramebuffer(o.dev, o.fb, nullptr);
    if (o.rp) vkDestroyRenderPass(o.dev, o.rp, nullptr);
    if (o.view) vkDestroyImageView(o.dev, o.view, nullptr);
    if (o.image) vkDestroyImage(o.dev, o.image, nullptr);
    if (o.imageMem) vkFreeMemory(o.dev, o.imageMem, nullptr);
    if (o.ssboMap) vkUnmapMemory(o.dev, o.ssboMem);
    if (o.ssbo) vkDestroyBuffer(o.dev, o.ssbo, nullptr);
    if (o.ssboMem) vkFreeMemory(o.dev, o.ssboMem, nullptr);
    if (o.index) vkDestroyBuffer(o.dev, o.index, nullptr);
    if (o.indexMem) vkFreeMemory(o.dev, o.indexMem, nullptr);
    o = Ours();
}

/// THE RECORDER: everything bound here is ours; the provider restores Ogre's caches.
static void record(Ours &o, Ogre::VulkanDevice *device, AtomPassContext &ctx)
{
    // Ogre's render-pass encoder is already closed by the provider; close whatever
    // other encoder may be open too, so the command buffer is ours to record into.
    device->mGraphicsQueue.endAllEncoders();
    VkCommandBuffer cmd = device->mGraphicsQueue.getCurrentCmdBuffer();
    ++o.frame;
    // The previous frame's vertex read of the buffer precedes this frame's write.
    VkMemoryBarrier war{};
    war.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    war.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    war.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &war,
                         0, nullptr, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, o.comp);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, o.compLayout, 0, 1, &o.set, 0, nullptr);
    vkCmdPushConstants(cmd, o.compLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &o.frame);
    vkCmdDispatch(cmd, 1, 1, 1);
    VkBufferMemoryBarrier raw{};
    raw.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    raw.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    raw.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
    raw.srcQueueFamilyIndex = raw.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    raw.buffer = o.ssbo;
    raw.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &raw, 0,
                         nullptr);
    if (!o.imageInitialised) {
        VkImageMemoryBarrier ib{};
        ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ib.srcAccessMask = 0;
        ib.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ib.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib.image = o.image;
        ib.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &ib);
        o.imageInitialised = true;
    } else {
        // The previous frame's attachment write precedes this frame's clear.
        VkMemoryBarrier waw{};
        waw.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        waw.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        waw.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 1, &waw, 0, nullptr, 0, nullptr);
    }
    VkClearValue clear{};
    clear.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo rb{};
    rb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rb.renderPass = o.rp;
    rb.framebuffer = o.fb;
    rb.renderArea = { { 0, 0 }, { kImg, kImg } };
    rb.clearValueCount = 1;
    rb.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, o.gfx);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, o.gfxLayout, 0, 1, &o.set, 0, nullptr);
    vkCmdPushConstants(cmd, o.gfxLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &o.frame);
    vkCmdBindIndexBuffer(cmd, o.index, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, 3, 1, 0, 0, 0);
    vkCmdEndRenderPass(cmd);
    // We bound a pipeline, descriptor sets and an index buffer: the provider must
    // make Ogre forget its caches.
    ctx.boundRawState = true;
    ++o.recorded;
}

static std::string defineArm(Ogre::CompositorManager2 *cm, const std::string &name, bool withOurs)
{
    const std::string nodeName = name + "/Node", wsName = name + "/Ws";
    if (cm->hasWorkspaceDefinition(wsName)) return wsName;
    Ogre::CompositorNodeDef *n = cm->addNodeDefinition(nodeName);
    n->addTextureSourceName("rt0", 0, Ogre::TextureDefinitionBase::TEXTURE_INPUT);
    n->setNumLocalTextureDefinitions(1);
    {
        auto *td = n->addTextureDefinition("depth");
        td->format = Ogre::PFG_D32_FLOAT;
        td->fsaa = "1";
        td->preferDepthTexture = true;
    }
    {
        Ogre::RenderTargetViewDef *rtv = n->addRenderTextureView("sceneRtv");
        Ogre::RenderTargetViewEntry c0;
        c0.textureName = "rt0";
        rtv->colourAttachments.push_back(c0);
        rtv->depthAttachment.textureName = "depth";
    }
    n->setNumTargetPass(1);
    Ogre::CompositorTargetDef *t = n->addTargetPass("sceneRtv");
    t->setNumPasses(withOurs ? 2 : 1);
    if (withOurs) t->addPass(Ogre::PASS_CUSTOM, Ogre::IdString("atom_negative_probe"));
    auto *p = static_cast<Ogre::CompositorPassSceneDef *>(t->addPass(Ogre::PASS_SCENE));
    p->mShadowNode = Ogre::IdString(OgreView::kShadowNodeName);
    p->setAllClearColours(Ogre::ColourValue(0.05f, 0.05f, 0.08f, 1.0f));
    p->setAllLoadActions(Ogre::LoadAction::Clear);
    p->mStoreActionColour[0] = Ogre::StoreAction::Store;
    p->mStoreActionDepth = Ogre::StoreAction::DontCare;
    p->mStoreActionStencil = Ogre::StoreAction::DontCare;
    p->mFirstRQ = 0u;
    p->mLastRQ = 200u;
    p->mIncludeOverlays = false;
    Ogre::CompositorWorkspaceDef *wd = cm->addWorkspaceDefinition(wsName);
    wd->connectExternal(0, nodeName, 0);
    return wsName;
}

static void readTarget(Ogre::TextureGpu *tex, std::vector<unsigned char> &out)
{
    Ogre::Image2 img;
    img.convertFromTexture(tex, 0u, 0u);
    const Ogre::TextureBox box = img.getData(0u);
    out.resize(size_t(kW) * kH * 4u);
    for (unsigned y = 0; y < kH; ++y) std::memcpy(&out[size_t(y) * kW * 4u], box.at(0, y, 0), size_t(kW) * 4u);
}

int main()
{
    std::printf("== engine.atom_negative_control: Ogre's next pass is byte-identical with our pipeline "
                "bound in the same frame, %d frames\n", kFrames);
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-atom-negative-control-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("negative", kW, kH, Colour(0, 0, 0));
    Scene *scene = e->createScene("negative");
    view->setScene(scene);
    enginetest::testCameraLookAt(view, Vec3(1.6f, 1.4f, 2.6f), Vec3(0.0f, 0.0f, 0.0f));
    scene->setAmbient(Colour(0.2f, 0.2f, 0.25f), Colour(0.05f, 0.05f, 0.05f));
    const NodeId cube = enginetest::addTestCube(scene, Colour(0.7f, 0.45f, 0.3f), 0.0f, 0.5f);
    enginetest::addDirectionalLight(scene, Vec3(-0.4f, -0.8f, -0.3f), 1.2f);
    CHECK(cube != 0, "the fixture cube exists");
    for (int i = 0; i < 3; ++i) e->renderOneFrame();

    Ogre::Root &root = Ogre::Root::getSingleton();
    auto *vkRs = dynamic_cast<Ogre::VulkanRenderSystem *>(root.getRenderSystem());
    CHECK(vkRs != nullptr, "the render system is Vulkan");
    if (!vkRs) return 1;
    Ogre::VulkanDevice *device = vkRs->getVulkanDevice();
    AtomPassProvider *provider = AtomPassProvider::instance();
    CHECK(provider != nullptr && root.getCompositorManager2()->getCompositorPassProvider() == provider,
          "the engine's CompositorPassProvider is installed (registerHlms)");
    if (!provider) return 1;

    // THE POSITIVE CONTROL for the validation entries (validation_probe.h).
    CHECK(atomtest::validationProbe(), "the validation layer is ACTIVE when the entry asks for it");

    Ours ours;
    const bool built = createOurs(device, ours);
    CHECK(built, "our compute + graphics pipelines, render pass, image, buffers and descriptor set exist");
    if (!built) { destroyOurs(ours); return 1; }
    provider->setRecorder("atom_negative_probe",
                          [&ours, device](AtomPassContext &ctx) { record(ours, device, ctx); });

    Ogre::TextureGpuManager *tm = root.getRenderSystem()->getTextureGpuManager();
    auto makeTarget = [&](const char *name) {
        Ogre::TextureGpu *t = tm->createTexture(name, Ogre::GpuPageOutStrategy::Discard,
                                                Ogre::TextureFlags::RenderToTexture, Ogre::TextureTypes::Type2D);
        t->setResolution(kW, kH);
        t->setPixelFormat(Ogre::PFG_RGBA8_UNORM_SRGB);
        t->setNumMipmaps(1u);
        t->_transitionTo(Ogre::GpuResidency::Resident, nullptr);
        return t;
    };
    Ogre::TextureGpu *withTex = makeTarget("atomNegWith");
    Ogre::TextureGpu *withoutTex = makeTarget("atomNegWithout");
    Ogre::CompositorManager2 *cm = root.getCompositorManager2();
    Ogre::SceneManager *sm = static_cast<Ogre::SceneManager *>(scene->nativeSceneManager());
    Ogre::Camera *cam = static_cast<OgreView *>(view)->camera();
    Ogre::CompositorWorkspace *withWs = cm->addWorkspace(sm, withTex, cam, defineArm(cm, "AtomNegWith", true), true);
    Ogre::CompositorWorkspace *withoutWs =
        cm->addWorkspace(sm, withoutTex, cam, defineArm(cm, "AtomNegWithout", false), true);

    std::vector<unsigned char> a, b;
    int identical = 0, recordedRight = 0;
    size_t worstBytes = 0, covered = 0;
    for (int f = 0; f < kFrames; ++f) {
        e->renderOneFrame();
        readTarget(withTex, a);
        readTarget(withoutTex, b);
        size_t diff = 0;
        for (size_t i = 0; i < a.size(); ++i) diff += a[i] != b[i];
        if (diff == 0) ++identical;
        worstBytes = std::max(worstBytes, diff);
        if (f == 0)   // the fixture's pixels: everything the pass drew over its clear colour
            for (size_t i = 0; i < a.size(); i += 4)
                covered += std::memcmp(&a[i], &a[0], 3) != 0;
        // The recorder RAN this frame: the frame number its dispatch wrote.
        uint32_t written = 0;
        std::memcpy(&written, ours.ssboMap, 4);
        if (written == ours.frame && ours.recorded == unsigned(f + 1)) ++recordedRight;
    }
    CHECK_MSG(recordedRight == kFrames, "our pipeline ran in every frame (%d of %d frames wrote their own frame number)",
              recordedRight, kFrames);
    CHECK_MSG(covered > 1000, "Ogre's pass drew the fixture (%zu lit pixels)", covered);
    CHECK_MSG(identical == kFrames, "Ogre's next pass byte-identical to the pass without ours: %d of %d frames "
                                    "(worst %zu differing bytes)",
              identical, kFrames, worstBytes);

    cm->removeWorkspace(withWs);
    cm->removeWorkspace(withoutWs);
    provider->setRecorder("atom_negative_probe", AtomPassRecorder());
    destroyOurs(ours);
    tm->destroyTexture(withTex);
    tm->destroyTexture(withoutTex);
    e->destroyView(view);
    e->destroyScene(scene);
    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
