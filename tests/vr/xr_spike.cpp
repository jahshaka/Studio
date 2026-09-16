// VR phase 1a — the OpenXR init-order spike (SPECS/VR_SPEC.md §5, ledger §579).
//
// WHAT IT PROVES (and therefore what it does, in this order):
//   1. xrCreateInstance(XR_KHR_vulkan_enable2) -> xrGetSystem -> the runtime's
//      Vulkan API-version window (xrGetVulkanGraphicsRequirements2KHR).
//   2. xrCreateVulkanInstanceKHR wrapping OUR VkInstanceCreateInfo, and the
//      resulting VkInstance handed to Ogre as a VulkanExternalInstance at
//      Root::loadPlugin (the RS reads `external_instance` in its CONSTRUCTOR).
//   3. xrCreateVulkanDeviceKHR wrapping the VkDeviceCreateInfo that
//      VulkanDevice::buildDeviceCreationRequest() — ogre-patch 0068 — builds:
//      the exact extension list and the exact VkPhysicalDeviceFeatures2 chain
//      createDevice() would have used. The device is handed to Ogre as a
//      VulkanExternalDevice on the FIRST createRenderWindow, together with the
//      request, so Ogre records what was ENABLED and not what the GPU supports.
//   4. A lit box + ground rendered into an Ogre RTT at the runtime's recommended
//      per-eye size, once per eye, with that eye's pose and that eye's
//      projection built from xrLocateViews' fov.
//   5. The RTT copied into the acquired XR swapchain image (the copy-per-eye
//      path, VR_SPEC §2.4 A) on Ogre's own frame command buffer, then
//      xrReleaseSwapchainImage + xrEndFrame with one projection layer.
//
// It also runs in two control modes so the claims are measurable, not asserted:
//   --plain    boot Ogre the ORDINARY way (Ogre creates instance + device) and
//              render the fixed parity pose. Compare the PNG's sha256 with the
//              XR run's: equal = patch 0068 handed Ogre the same device.
//   --probe    the runtime's identity/extensions only; no GPU, no Vulkan.
//
// Everything is deliberately linear and free of abstraction: this file is an
// experiment whose value is that a reader can follow the order.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "OgreAbiUtils.h"
#include "OgreRoot.h"
#include "OgreCamera.h"
#include "OgreItem.h"
#include "OgreLight.h"
#include "OgreMesh2.h"
#include "OgreMeshManager2.h"
#include "OgreSceneManager.h"
#include "OgreSubMesh2.h"
#include "OgreTextureGpuManager.h"
#include "OgreAsyncTextureTicket.h"
#include "OgreArchiveManager.h"
#include "OgreConfigFile.h"
#include "OgreHlmsManager.h"
#include "OgreHlmsPbs.h"
#include "OgreHlmsPbsDatablock.h"
#include "OgreHlmsUnlit.h"
#include "OgreLogManager.h"
#include "OgreWindow.h"
#include "OgreResourceTransition.h"
#include "Compositor/OgreCompositorManager2.h"
#include "Compositor/OgreCompositorWorkspace.h"
#include "Vao/OgreVaoManager.h"
#include "Vao/OgreVertexArrayObject.h"
#include "OgreVulkanDevice.h"
#include "OgreVulkanRenderSystem.h"
#include "OgreVulkanTextureGpu.h"
#include "OgreVulkanQueue.h"

// ---------------------------------------------------------------------------
// Tiny reporting: every line the gate reads starts with a tag, so a caller can
// grep the transcript instead of parsing it.
static int g_failures = 0;
static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::vfprintf(stdout, fmt, ap); va_end(ap);
    std::fputc('\n', stdout); std::fflush(stdout);
}
static void fail(const char *what) { ++g_failures; say("FAIL  %s", what); }
static bool check(bool ok, const char *what) {
    say("%s  %s", ok ? "ok   " : "FAIL ", what);
    if (!ok) ++g_failures;
    return ok;
}

#define XR_TRY(expr, what)                                                       \
    do {                                                                         \
        XrResult _r = (expr);                                                    \
        if (XR_FAILED(_r)) { say("FAIL  %s -> XrResult %d", what, int(_r)); return false; } \
    } while (0)
#define VK_TRY(expr, what)                                                       \
    do {                                                                         \
        VkResult _r = (expr);                                                    \
        if (_r != VK_SUCCESS) { say("FAIL  %s -> VkResult %d", what, int(_r)); return false; } \
    } while (0)

// ---------------------------------------------------------------------------
// The scene. One helper builds a v2 box mesh by hand: v1 meshes render NOTHING
// on Vulkan (silently), and pulling a .mesh out of the sample media would tie
// the spike to media it does not otherwise need.
static Ogre::MeshPtr makeBoxMesh(const std::string &name) {
    using namespace Ogre;
    struct V { float px, py, pz, nx, ny, nz, u, v; };
    const float f[6][3] = { {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0} };
    std::vector<V> verts;
    std::vector<uint16> idx;
    for (int i = 0; i < 6; ++i) {
        Vector3 n(f[i][0], f[i][1], f[i][2]);
        Vector3 up = (std::fabs(n.y) > 0.9f) ? Vector3(0, 0, 1) : Vector3(0, 1, 0);
        Vector3 t = up.crossProduct(n).normalisedCopy();
        Vector3 b = n.crossProduct(t);
        const uint16 base = static_cast<uint16>(verts.size());
        const float uv[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
        const float sx[4] = { -1, 1, 1, -1 }, sy[4] = { -1, -1, 1, 1 };
        for (int k = 0; k < 4; ++k) {
            Vector3 p = n * 0.5f + t * (sx[k] * 0.5f) + b * (sy[k] * 0.5f);
            verts.push_back({ p.x, p.y, p.z, n.x, n.y, n.z, uv[k][0], uv[k][1] });
        }
        idx.insert(idx.end(), { base, uint16(base + 1), uint16(base + 2),
                                base, uint16(base + 2), uint16(base + 3) });
    }

    MeshPtr mesh = MeshManager::getSingleton().createManual(
        name, ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
    SubMesh *sub = mesh->createSubMesh();
    VaoManager *vao = Root::getSingleton().getRenderSystem()->getVaoManager();

    VertexElement2Vec decl;
    decl.push_back(VertexElement2(VET_FLOAT3, VES_POSITION));
    decl.push_back(VertexElement2(VET_FLOAT3, VES_NORMAL));
    decl.push_back(VertexElement2(VET_FLOAT2, VES_TEXTURE_COORDINATES));

    // Ogre takes ownership of the buffer contents on success but NOT on throw;
    // the spike would die anyway, so a plain local is honest here.
    VertexBufferPacked *vb = vao->createVertexBuffer(decl, verts.size(), BT_IMMUTABLE,
                                                     verts.data(), false);
    VertexBufferPackedVec vbs; vbs.push_back(vb);
    IndexBufferPacked *ib = vao->createIndexBuffer(IndexBufferPacked::IT_16BIT, idx.size(),
                                                   BT_IMMUTABLE, idx.data(), false);
    VertexArrayObject *v = vao->createVertexArrayObject(vbs, ib, OT_TRIANGLE_LIST);
    sub->mVao[VpNormal].push_back(v);
    sub->mVao[VpShadow].push_back(v);

    mesh->_setBounds(Aabb(Vector3::ZERO, Vector3(0.5f, 0.5f, 0.5f)), false);
    mesh->_setBoundingSphereRadius(1.0f);
    return mesh;
}

/// THE PARITY PICTURE'S SIZE IS FIXED HERE AND NOWHERE ELSE.
/// The parity check compares the picture an OpenXR-created device renders with the
/// picture Ogre's own device renders, byte for byte — so both arms must render at the
/// same resolution, and that resolution must not depend on the runtime. It used to be
/// the runtime's RECOMMENDED EYE SIZE, which happens to be 320x240 under Monado's null
/// compositor and 896x1007 under its xcb one: the check held by coincidence on the
/// first and would have redded on the second with the engine blameless.
static const unsigned kParityW = 320u, kParityH = 240u;

struct SpikeScene {
    Ogre::SceneManager *sceneMgr = nullptr;
    Ogre::Camera *camera = nullptr;
    Ogre::TextureGpu *rtt = nullptr;
    Ogre::CompositorWorkspace *workspace = nullptr;
};

static void makeDatablock(const char *name, float r, float g, float b, float roughness) {
    using namespace Ogre;
    HlmsPbs *pbs = static_cast<HlmsPbs *>(Root::getSingleton().getHlmsManager()->getHlms(HLMS_PBS));
    HlmsPbsDatablock *db = static_cast<HlmsPbsDatablock *>(pbs->createDatablock(
        name, name, HlmsMacroblock(), HlmsBlendblock(), HlmsParamVec()));
    db->setWorkflow(HlmsPbsDatablock::MetallicWorkflow);  // the pin defaults to Specular
    db->setDiffuse(Vector3(r, g, b));
    db->setRoughness(roughness);
    db->setMetalness(0.0f);
}

static bool buildScene(Ogre::Root *root, SpikeScene &out, unsigned w, unsigned h) {
    using namespace Ogre;
    out.sceneMgr = root->createSceneManager(ST_GENERIC, 2u, "spike");
    out.sceneMgr->setAmbientLight(ColourValue(0.22f, 0.24f, 0.28f),
                                  ColourValue(0.10f, 0.10f, 0.12f), Vector3(0, 1, 0));
    out.camera = out.sceneMgr->createCamera("cam");
    out.camera->setNearClipDistance(0.05f);
    out.camera->setFarClipDistance(500.0f);

    makeDatablock("spikeGround", 0.35f, 0.36f, 0.38f, 0.85f);
    makeDatablock("spikeBox", 0.70f, 0.28f, 0.18f, 0.45f);

    MeshPtr box = makeBoxMesh("spikeBox.mesh");

    {   // the ground: the same box, flattened
        Item *item = out.sceneMgr->createItem(box, SCENE_STATIC);
        item->setDatablock("spikeGround");
        SceneNode *n = out.sceneMgr->getRootSceneNode(SCENE_STATIC)->createChildSceneNode(SCENE_STATIC);
        n->setScale(40.0f, 0.1f, 40.0f);
        n->setPosition(0, -0.05f, 0);
        n->attachObject(item);
    }
    {   // the subject
        Item *item = out.sceneMgr->createItem(box, SCENE_DYNAMIC);
        item->setDatablock("spikeBox");
        SceneNode *n = out.sceneMgr->getRootSceneNode()->createChildSceneNode();
        n->setPosition(0, 1.3f, -2.0f);   // eye height in STAGE space, 2 m ahead
        n->setScale(1.0f, 1.0f, 1.0f);
        n->attachObject(item);
    }
    {
        Light *l = out.sceneMgr->createLight();
        SceneNode *n = out.sceneMgr->getRootSceneNode()->createChildSceneNode();
        n->attachObject(l);
        l->setType(Light::LT_DIRECTIONAL);
        l->setDirection(Vector3(-0.4f, -1.0f, -0.55f).normalisedCopy());
        l->setPowerScale(Math::PI);
        l->setDiffuseColour(1.0f, 0.97f, 0.92f);
    }

    TextureGpuManager *tm = root->getRenderSystem()->getTextureGpuManager();
    out.rtt = tm->createTexture("spikeEye", GpuPageOutStrategy::Discard,
                                TextureFlags::RenderToTexture, TextureTypes::Type2D);
    out.rtt->setResolution(w, h);
    // SRGB on purpose, and 8-bit on purpose: vkCmdCopyImage requires the two images
    // to be format-COMPATIBLE (same texel block size), so the eye target's format and
    // the XR swapchain's must agree in bytes per pixel. Monado offers
    // R16G16B16A16_UNORM first and R8G8B8A8_SRGB third (measured, FINDINGS §2); the
    // spike insists on the 8-bit sRGB one and refuses to run otherwise rather than
    // copy 4 bytes per pixel into an 8-byte texel. Phase 2's HDR profile pairs an
    // RGBA16F target with the runtime's 16-bit format instead.
    out.rtt->setPixelFormat(PFG_RGBA8_UNORM_SRGB);
    out.rtt->scheduleTransitionTo(GpuResidency::Resident);

    CompositorManager2 *cm = root->getCompositorManager2();
    cm->createBasicWorkspaceDef("spikeWs", ColourValue(0.16f, 0.20f, 0.28f), IdString());
    out.workspace = cm->addWorkspace(out.sceneMgr, out.rtt, out.camera, "spikeWs", true);
    return out.workspace != nullptr;
}

/// After a copy the RTT sits in CopySrc, and Ogre's own download path REFUSES a
/// texture "already in CopySrc or CopyDst layout, externally set"
/// (VulkanQueue::prepareForDownload). Put it back before reading or rendering.
static void restoreRttLayout(Ogre::TextureGpu *rtt) {
    using namespace Ogre;
    RenderSystem *rs = Root::getSingleton().getRenderSystem();
    BarrierSolver &solver = rs->getBarrierSolver();
    ResourceTransitionArray trans;
    solver.resolveTransition(trans, rtt, ResourceLayout::RenderTarget,
                             ResourceAccess::ReadWrite, 0);
    rs->executeResourceTransition(trans);
}

// ---------------------------------------------------------------------------
static bool readRtt(Ogre::TextureGpu *rtt, std::vector<unsigned char> &rgba,
                    unsigned &w, unsigned &h) {
    using namespace Ogre;
    TextureGpuManager *tm = Root::getSingleton().getRenderSystem()->getTextureGpuManager();
    w = rtt->getWidth(); h = rtt->getHeight();
    AsyncTextureTicket *t = tm->createAsyncTextureTicket(w, h, 1u, TextureTypes::Type2D,
                                                         rtt->getPixelFormat());
    t->download(rtt, 0, true);
    const TextureBox box = t->map(0);
    rgba.resize(size_t(w) * h * 4u);
    for (unsigned y = 0; y < h; ++y)
        std::memcpy(&rgba[size_t(y) * w * 4u], box.at(0, y, 0), w * 4u);
    t->unmap();
    tm->destroyAsyncTextureTicket(t);
    return true;
}

/// A PPM, not a PNG: no encoder dependency, and sha256 over the bytes is what
/// the parity check actually compares.
static void writePpm(const std::string &path, const std::vector<unsigned char> &rgba,
                     unsigned w, unsigned h) {
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << w << " " << h << "\n255\n";
    for (size_t i = 0; i < size_t(w) * h; ++i)
        f.write(reinterpret_cast<const char *>(&rgba[i * 4]), 3);
}

// ---------------------------------------------------------------------------
// Ogre boot, shared by both modes. externalInstance/externalDevice may be null
// (the --plain control), in which case Ogre creates both itself.
static bool bootOgre(Ogre::Root *&root, Ogre::VulkanExternalInstance *extInstance,
                     Ogre::VulkanExternalDevice *extDevice) {
    using namespace Ogre;
    static Ogre::AbiCookie cookie = Ogre::generateAbiCookie();
    root = new Root(&cookie, "", "", "ogre-xr-spike.log", "JahshakaVrSpike");

    NameValuePairList pluginOpts;
    if (extInstance)
        pluginOpts["external_instance"] = StringConverter::toString(uintptr_t(extInstance));
    root->loadPlugin(std::string(JAH_OGRE_PLUGIN_DIR) + "/RenderSystem_Vulkan", false,
                     extInstance ? &pluginOpts : nullptr);

    const RenderSystemList &rsList = root->getAvailableRenderers();
    if (rsList.empty()) { fail("no render system after loadPlugin"); return false; }
    root->setRenderSystem(rsList[0]);
    root->initialise(false);

    NameValuePairList winOpts;
    winOpts["windowType"] = "null";   // surfaceless: the spike draws into an RTT
    if (extDevice)
        winOpts["external_device"] = StringConverter::toString(uintptr_t(extDevice));
    root->createRenderWindow("spike", 1u, 1u, false, &winOpts);

    // Hlms AFTER a render target exists (the startup-order law).
    ArchiveManager &am = ArchiveManager::getSingleton();
    const std::string media = std::string(JAH_OGRE_MEDIA_DIR) + "/";
    String mainPath; StringVector libPaths;
    HlmsUnlit::getDefaultPaths(mainPath, libPaths);
    {
        ArchiveVec libs;
        for (const auto &p : libPaths) libs.push_back(am.load(media + p, "FileSystem", true));
        root->getHlmsManager()->registerHlms(OGRE_NEW HlmsUnlit(am.load(media + mainPath, "FileSystem", true), &libs));
    }
    HlmsPbs::getDefaultPaths(mainPath, libPaths);
    {
        ArchiveVec libs;
        for (const auto &p : libPaths) libs.push_back(am.load(media + p, "FileSystem", true));
        root->getHlmsManager()->registerHlms(OGRE_NEW HlmsPbs(am.load(media + mainPath, "FileSystem", true), &libs));
    }
    return true;
}

static void reportCaps(Ogre::Root *root, const char *tag) {
    using namespace Ogre;
    const RenderSystemCapabilities *caps = root->getRenderSystem()->getCapabilities();
    VulkanRenderSystem *vkRs = static_cast<VulkanRenderSystem *>(root->getRenderSystem());
    VulkanDevice *dev = vkRs->getVulkanDevice();
    say("CAPS  %s device='%s' driver=%u apiVersion=%u.%u.%u", tag,
        dev->mDeviceProperties.deviceName, dev->mDeviceProperties.driverVersion,
        VK_VERSION_MAJOR(dev->mDeviceProperties.apiVersion),
        VK_VERSION_MINOR(dev->mDeviceProperties.apiVersion),
        VK_VERSION_PATCH(dev->mDeviceProperties.apiVersion));
    say("CAPS  %s shaderFloat16=%d storageInputOutput16=%d cacheControl=%d rayQuery=%d", tag,
        int(dev->mDeviceExtraFeatures.shaderFloat16),
        int(dev->mDeviceExtraFeatures.storageInputOutput16),
        int(dev->mDeviceExtraFeatures.pipelineCreationCacheControl),
        int(dev->hasRayQuery()));
    say("CAPS  %s RSC_SHADER_FLOAT16=%d VP_AND_RT_ARRAY_INDEX_FROM_ANY_SHADER=%d numDevExt=%zu", tag,
        int(caps->hasCapability(RSC_SHADER_FLOAT16)),
        int(caps->hasCapability(RSC_VP_AND_RT_ARRAY_INDEX_FROM_ANY_SHADER)),
        dev->mDeviceExtensions.size());
}

// The fixed parity pose: identical in both modes, and not derived from the
// runtime, so the two pictures are comparable byte for byte.
static void setParityPose(Ogre::Camera *cam) {
    cam->setPosition(Ogre::Vector3(0.6f, 1.5f, 2.4f));
    cam->lookAt(Ogre::Vector3(0.0f, 1.3f, -2.0f));
    cam->setCustomProjectionMatrix(false);
    cam->setAspectRatio(1.0f);
    cam->setFOVy(Ogre::Degree(70.0f));
}

/// Render the fixed parity pose into a target of our own at kParityW x kParityH and
/// write it out. Used by BOTH arms, so the two pictures are comparable by construction
/// rather than by the runtime happening to recommend that eye size. The scene's usual
/// workspace is switched off for the duration so only this one draws.
static void renderParityPose(Ogre::Root *root, SpikeScene &sc, const std::string &path) {
    using namespace Ogre;
    const bool needOwnTarget = sc.rtt->getWidth() != kParityW || sc.rtt->getHeight() != kParityH;

    TextureGpu *target = sc.rtt;
    CompositorWorkspace *ws = nullptr;
    if (needOwnTarget) {
        TextureGpuManager *tm = root->getRenderSystem()->getTextureGpuManager();
        target = tm->createTexture("spikeParity", GpuPageOutStrategy::Discard,
                                   TextureFlags::RenderToTexture, TextureTypes::Type2D);
        target->setResolution(kParityW, kParityH);
        target->setPixelFormat(PFG_RGBA8_UNORM_SRGB);
        target->scheduleTransitionTo(GpuResidency::Resident);
        ws = root->getCompositorManager2()->addWorkspace(sc.sceneMgr, target, sc.camera,
                                                         "spikeWs", true);
        sc.workspace->setEnabled(false);
    }

    setParityPose(sc.camera);
    for (int i = 0; i < 4; ++i) root->renderOneFrame();
    root->getRenderSystem()->flushCommands();   // AsyncTextureTicket reads stale VRAM otherwise

    std::vector<unsigned char> px; unsigned rw, rh;
    readRtt(target, px, rw, rh);
    writePpm(path, px, rw, rh);

    // "Something was drawn" = more than the clear colour is present. On the external
    // route this is the whole of patch 0068 hunk 1: at the unpatched pin every frame is
    // vetoed and this picture stays the clear colour for ever.
    size_t distinct = 0;
    const unsigned char c0 = px[0], c1 = px[1], c2 = px[2];
    for (size_t i = 0; i < size_t(rw) * rh; ++i)
        if (px[i * 4] != c0 || px[i * 4 + 1] != c1 || px[i * 4 + 2] != c2) ++distinct;
    say("PARITY %ux%u written, %zu of %u pixels differ from the corner pixel", rw, rh,
        distinct, rw * rh);
    check(distinct > (size_t(rw) * rh) / 20u, "a frame RENDERS (the parity pose drew a scene)");

    if (needOwnTarget) {
        root->getCompositorManager2()->removeWorkspace(ws);
        root->getRenderSystem()->getTextureGpuManager()->destroyTexture(target);
        sc.workspace->setEnabled(true);
    }
}

// ---------------------------------------------------------------------------
// --plain: Ogre creates its own instance and device; render the parity pose.
static int runPlain(const std::string &outDir, unsigned w, unsigned h) {
    Ogre::Root *root = nullptr;
    if (!bootOgre(root, nullptr, nullptr)) return 1;
    reportCaps(root, "plain");
    SpikeScene sc;
    if (!buildScene(root, sc, w, h)) { fail("scene"); return 1; }
    renderParityPose(root, sc, outDir + "/parity-plain.ppm");
    delete root;
    return g_failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
/// The runtime the caller INSISTS on. A gate that trusts whatever
/// ~/.config/openxr/1/ happens to hold is at the mercy of the owner's cable
/// (WiVRn rewrites it on connect) — VR_SPEC §2.6.
static std::string g_expectRuntime;

struct Xr {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    XrSwapchain swapchain[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
    std::vector<XrSwapchainImageVulkanKHR> images[2];
    XrViewConfigurationView viewCfg[2] = {};
    std::string runtimeName;
    int64_t swapchainFormat = 0;

    PFN_xrGetVulkanGraphicsRequirements2KHR GetVulkanGraphicsRequirements2 = nullptr;
    PFN_xrCreateVulkanInstanceKHR CreateVulkanInstance = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR GetVulkanGraphicsDevice2 = nullptr;
    PFN_xrCreateVulkanDeviceKHR CreateVulkanDevice = nullptr;
};

template <class F> static void xrFn(XrInstance i, const char *n, F &out) {
    xrGetInstanceProcAddr(i, n, reinterpret_cast<PFN_xrVoidFunction *>(&out));
}

static bool xrBegin(Xr &xr) {
    uint32_t n = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr);
    std::vector<XrExtensionProperties> exts(n, { XR_TYPE_EXTENSION_PROPERTIES });
    xrEnumerateInstanceExtensionProperties(nullptr, n, &n, exts.data());
    bool hasEnable2 = false, hasVisMask = false, hasDepth = false, hasRefresh = false;
    const bool dumpExts = std::getenv("JAH_XR_DUMP_EXT") != nullptr;
    for (const auto &e : exts) {
        if (dumpExts) say("EXT    %s v%u", e.extensionName, e.extensionVersion);
        if (!std::strcmp(e.extensionName, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME)) hasEnable2 = true;
        if (!std::strcmp(e.extensionName, "XR_KHR_visibility_mask")) hasVisMask = true;
        if (!std::strcmp(e.extensionName, "XR_KHR_composition_layer_depth")) hasDepth = true;
        if (!std::strcmp(e.extensionName, "XR_FB_display_refresh_rate")) hasRefresh = true;
    }
    say("RUNTIME extensions=%u vulkan_enable2=%d visibility_mask=%d layer_depth=%d refresh_rate=%d",
        n, int(hasEnable2), int(hasVisMask), int(hasDepth), int(hasRefresh));
    if (!check(hasEnable2, "runtime advertises XR_KHR_vulkan_enable2")) return false;

    // THE API VERSION IS NEGOTIATED, NOT ASSUMED (VR_SPEC §0 after the Oculus audit,
    // ledger §580). Asking for whatever version the SDK headers happen to carry
    // (XR_CURRENT_API_VERSION — 1.1.47 on this box) is a hard requirement on the
    // runtime: a runtime that implements only OpenXR 1.0 answers
    // XR_ERROR_API_VERSION_UNSUPPORTED and the app simply does not start. Everything
    // this file uses is OpenXR 1.0 core plus XR_KHR_vulkan_enable2, so 1.0 is a real
    // floor and not a pretence: ask for 1.1, fall back to 1.0 on exactly that error.
    const char *want[] = { XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME };
    XrInstanceCreateInfo ici{ XR_TYPE_INSTANCE_CREATE_INFO };
    std::strcpy(ici.applicationInfo.applicationName, "JahshakaVrSpike");
    ici.enabledExtensionCount = 1;
    ici.enabledExtensionNames = want;

    const XrVersion ladder[2] = { XR_MAKE_VERSION(1, 1, 0), XR_MAKE_VERSION(1, 0, 0) };
    XrResult created = XR_ERROR_RUNTIME_FAILURE;
    XrVersion gotVersion = 0;
    for (XrVersion v : ladder) {
        ici.applicationInfo.apiVersion = v;
        created = xrCreateInstance(&ici, &xr.instance);
        if (XR_SUCCEEDED(created)) { gotVersion = v; break; }
        if (created != XR_ERROR_API_VERSION_UNSUPPORTED) break;  // a real failure
        say("XRAPI  the runtime refused OpenXR %d.%d — trying the next one down",
            int(XR_VERSION_MAJOR(v)), int(XR_VERSION_MINOR(v)));
    }
    if (XR_FAILED(created)) { say("FAIL  xrCreateInstance -> XrResult %d", int(created)); return false; }
    say("XRAPI  instance created at OpenXR %d.%d (headers %d.%d)",
        int(XR_VERSION_MAJOR(gotVersion)), int(XR_VERSION_MINOR(gotVersion)),
        int(XR_VERSION_MAJOR(XR_CURRENT_API_VERSION)),
        int(XR_VERSION_MINOR(XR_CURRENT_API_VERSION)));

    // The runtime's NAME AND VERSION, logged unconditionally: the one line that tells a
    // later reader which runtime a transcript came from (§0's "native" definition, and
    // the manifest law — the user manifest is whatever a headset last wrote).
    XrInstanceProperties ip{ XR_TYPE_INSTANCE_PROPERTIES };
    XR_TRY(xrGetInstanceProperties(xr.instance, &ip), "xrGetInstanceProperties");
    xr.runtimeName = ip.runtimeName;
    say("RUNTIME name='%s' version=%llu (%d.%d.%d)", ip.runtimeName,
        (unsigned long long)ip.runtimeVersion,
        int(XR_VERSION_MAJOR(ip.runtimeVersion)), int(XR_VERSION_MINOR(ip.runtimeVersion)),
        int(XR_VERSION_PATCH(ip.runtimeVersion)));
    if (!g_expectRuntime.empty() &&
        !check(xr.runtimeName.find(g_expectRuntime) != std::string::npos,
               ("the runtime is the one the caller named ('" + g_expectRuntime + "')").c_str()))
        return false;

    XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO };
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XR_TRY(xrGetSystem(xr.instance, &sgi, &xr.systemId), "xrGetSystem");
    XrSystemProperties sp{ XR_TYPE_SYSTEM_PROPERTIES };
    XR_TRY(xrGetSystemProperties(xr.instance, xr.systemId, &sp), "xrGetSystemProperties");
    say("SYSTEM '%s' maxSwapchain=%ux%u layers=%u", sp.systemName,
        sp.graphicsProperties.maxSwapchainImageWidth,
        sp.graphicsProperties.maxSwapchainImageHeight,
        sp.graphicsProperties.maxLayerCount);

    uint32_t viewCount = 0;
    XR_TRY(xrEnumerateViewConfigurationViews(xr.instance, xr.systemId,
                                             XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                             0, &viewCount, nullptr),
           "xrEnumerateViewConfigurationViews(count)");
    if (!check(viewCount == 2, "primary stereo has 2 views")) return false;
    for (auto &v : xr.viewCfg) v.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    XR_TRY(xrEnumerateViewConfigurationViews(xr.instance, xr.systemId,
                                             XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                             2, &viewCount, xr.viewCfg),
           "xrEnumerateViewConfigurationViews");
    say("VIEWS  recommended=%ux%u max=%ux%u samples=%u",
        xr.viewCfg[0].recommendedImageRectWidth, xr.viewCfg[0].recommendedImageRectHeight,
        xr.viewCfg[0].maxImageRectWidth, xr.viewCfg[0].maxImageRectHeight,
        xr.viewCfg[0].recommendedSwapchainSampleCount);

    xrFn(xr.instance, "xrGetVulkanGraphicsRequirements2KHR", xr.GetVulkanGraphicsRequirements2);
    xrFn(xr.instance, "xrCreateVulkanInstanceKHR", xr.CreateVulkanInstance);
    xrFn(xr.instance, "xrGetVulkanGraphicsDevice2KHR", xr.GetVulkanGraphicsDevice2);
    xrFn(xr.instance, "xrCreateVulkanDeviceKHR", xr.CreateVulkanDevice);
    return check(xr.GetVulkanGraphicsRequirements2 && xr.CreateVulkanInstance &&
                 xr.GetVulkanGraphicsDevice2 && xr.CreateVulkanDevice,
                 "the four XR_KHR_vulkan_enable2 entry points resolve");
}

// ---------------------------------------------------------------------------
static Ogre::Matrix4 projectionFromFov(const XrFovf &fov, float zNear, float zFar) {
    // The standard OpenXR asymmetric projection, in OGRE's GL-style convention
    // ([-1,1] depth, +Y up): Camera::setCustomProjectionMatrix hands it to
    // RenderSystem::_convertProjectionMatrix, which applies the Vulkan clip-space
    // correction (and our reverse-depth) itself.
    const float l = std::tan(fov.angleLeft), r = std::tan(fov.angleRight);
    const float d = std::tan(fov.angleDown), u = std::tan(fov.angleUp);
    const float w = r - l, hgt = u - d;
    Ogre::Matrix4 m = Ogre::Matrix4::ZERO;
    m[0][0] = 2.0f / w;  m[0][2] = (r + l) / w;
    m[1][1] = 2.0f / hgt; m[1][2] = (u + d) / hgt;
    m[2][2] = -(zFar + zNear) / (zFar - zNear);
    m[2][3] = -(2.0f * zFar * zNear) / (zFar - zNear);
    m[3][2] = -1.0f;
    return m;
}

// ---------------------------------------------------------------------------
// The copy per eye (VR_SPEC §2.4 A), on OGRE'S OWN frame command buffer so it is
// queue-ordered behind the frame that just rendered.
static bool copyEyeToSwapchain(Ogre::VulkanRenderSystem *vkRs, Ogre::TextureGpu *rtt,
                               VkImage dst, unsigned w, unsigned h) {
    using namespace Ogre;
    VulkanDevice *dev = vkRs->getVulkanDevice();

    // THE FRAME'S WRITES MUST BE IN THE TRANSITION BARRIER'S OWN SOURCE SCOPE,
    // AND AFTER renderOneFrame() OGRE NO LONGER KNOWS THEY HAPPENED (measured,
    // 2026-09-16). renderOneFrame() ends in commitAndNextCommandBuffer(NewFrameIdx),
    // which submits the frame AND resets the BarrierSolver's per-frame tracking, so
    // a CopySrc transition asked for afterwards is emitted with
    // `oldAccess == Undefined` -> srcAccessMask 0, srcStage without
    // COLOR_ATTACHMENT_OUTPUT (OgreVulkanRenderSystem.cpp executeResourceTransition),
    // and the synchronization validation layer reports, correctly:
    //   "WRITE_AFTER_WRITE ... a layout transition ... conflicts with a prior write
    //    (COLOR_ATTACHMENT_WRITE) at COLOR_ATTACHMENT_OUTPUT".
    // A preceding global memory barrier does NOT fix it: a layout transition is
    // itself a write, and it must be ordered by the IMAGE barrier's own masks.
    // `assumeTransition` is the pin's own way to tell the solver a truth it has
    // forgotten - the frame really did leave this texture in RenderTarget, written -
    // and the transition it then emits carries the right source scope.
    // (Phase 2's copy belongs INSIDE the frame, in a compositor pass listener where
    // the solver's tracking is still live; then none of this is needed. VR_SPEC §7
    // risk 2 - the copy's sync, not its bandwidth, was the risk, and it was real.)
    {
        BarrierSolver &solver = vkRs->getBarrierSolver();
        solver.assumeTransition(rtt, ResourceLayout::RenderTarget, ResourceAccess::Write, 0);
        ResourceTransitionArray trans;
        solver.resolveTransition(trans, rtt, ResourceLayout::CopySrc, ResourceAccess::Read, 0);
        vkRs->executeResourceTransition(trans);
    }
    dev->mGraphicsQueue.endAllEncoders();
    // getCurrentCmdBuffer NEVER returns null: on a lost device its own checkVkResult
    // throws (the accessor patch 0040 made linkable). There is nothing to test here.
    VkCommandBuffer cmd = dev->mGraphicsQueue.getCurrentCmdBuffer();

    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = dst;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    // The runtime hands the image over in COLOR_ATTACHMENT_OPTIMAL and wants it
    // back that way; its contents are ours to overwrite, so UNDEFINED as the old
    // layout is legal and cheaper than preserving them.
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);

    VkImageCopy region{};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.extent = { w, h, 1 };
    VkImage src = static_cast<VulkanTextureGpu *>(rtt)->getFinalTextureName();
    vkCmdCopyImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &b);

    dev->commitAndNextCommandBuffer(Ogre::SubmissionType::FlushOnly);
    return true;
}

// ---------------------------------------------------------------------------
static int runXr(const std::string &outDir, int wantFrames) {
    using namespace Ogre;
    Xr xr;
    if (!xrBegin(xr)) return 1;

    // ---- 1. the runtime's Vulkan API-version window ------------------------
    XrGraphicsRequirementsVulkan2KHR req{ XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR };
    XR_TRY(xr.GetVulkanGraphicsRequirements2(xr.instance, xr.systemId, &req),
           "xrGetVulkanGraphicsRequirements2KHR");
    auto verStr = [](XrVersion v) {
        char b[32];
        std::snprintf(b, sizeof(b), "%d.%d.%d", int(XR_VERSION_MAJOR(v)),
                      int(XR_VERSION_MINOR(v)), int(XR_VERSION_PATCH(v)));
        return std::string(b);
    };
    say("VKREQ  min=%s max=%s", verStr(req.minApiVersionSupported).c_str(),
        verStr(req.maxApiVersionSupported).c_str());
    const uint32_t wantApi = VK_API_VERSION_1_2;   // patch 0038's ray query needs 1.2
    const bool apiOk = XR_MAKE_VERSION(1, 2, 0) >= req.minApiVersionSupported &&
                       XR_MAKE_VERSION(1, 2, 0) <= req.maxApiVersionSupported;
    check(apiOk, "the runtime's requirements window admits Vulkan 1.2");

    // ---- 2. OUR VkInstanceCreateInfo, created BY THE RUNTIME ---------------
    // The instance extension list is the checklist VR_SPEC §2.2 names: it is
    // ours, not Ogre's, because the plain path's list is chosen inside
    // VulkanInstance and the external path copies OUR struct's list verbatim
    // (which is also what decides whether the xcb window backend is usable).
    std::vector<const char *> instExt = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        "VK_KHR_xcb_surface",
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
    std::vector<const char *> instLayers;
    if (const char *v = std::getenv("JAH_XR_VALIDATION"); v && *v == '1')
        instLayers.push_back("VK_LAYER_KHRONOS_validation");

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "JahshakaVrSpike";
    app.pEngineName = "Ogre-Next";
    app.apiVersion = wantApi;
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = uint32_t(instExt.size());
    ici.ppEnabledExtensionNames = instExt.data();
    ici.enabledLayerCount = uint32_t(instLayers.size());
    ici.ppEnabledLayerNames = instLayers.empty() ? nullptr : instLayers.data();

    XrVulkanInstanceCreateInfoKHR xrIci{ XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR };
    xrIci.systemId = xr.systemId;
    xrIci.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
    xrIci.vulkanCreateInfo = &ici;
    VkInstance vkInstance = VK_NULL_HANDLE;
    VkResult vkErr = VK_SUCCESS;
    XR_TRY(xr.CreateVulkanInstance(xr.instance, &xrIci, &vkInstance, &vkErr),
           "xrCreateVulkanInstanceKHR");
    if (!check(vkErr == VK_SUCCESS && vkInstance, "xrCreateVulkanInstanceKHR made the VkInstance"))
        return 1;

    // ---- 3. the physical device the runtime wants --------------------------
    XrVulkanGraphicsDeviceGetInfoKHR gdi{ XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };
    gdi.systemId = xr.systemId;
    gdi.vulkanInstance = vkInstance;
    VkPhysicalDevice physDev = VK_NULL_HANDLE;
    XR_TRY(xr.GetVulkanGraphicsDevice2(xr.instance, &gdi, &physDev),
           "xrGetVulkanGraphicsDevice2KHR");
    VkPhysicalDeviceProperties pdp{};
    vkGetPhysicalDeviceProperties(physDev, &pdp);
    say("VKDEV  runtime picked '%s' (api %u.%u.%u)", pdp.deviceName,
        VK_VERSION_MAJOR(pdp.apiVersion), VK_VERSION_MINOR(pdp.apiVersion),
        VK_VERSION_PATCH(pdp.apiVersion));

    // ---- 4. THE OGRE PLUGIN, on the runtime's instance ---------------------
    // loadPlugin BEFORE the device on purpose: the RS constructor is what runs
    // VulkanInstance::enumerateExtensionsAndLayers(), and ogre-patch 0068's
    // buildDeviceCreationRequest() reads that static list to decide whether
    // VK_KHR_get_physical_device_properties2 is usable.
    VulkanExternalInstance extInstance{};
    extInstance.instance = vkInstance;
    for (const char *e : instExt) {
        VkExtensionProperties p{};
        std::strncpy(p.extensionName, e, VK_MAX_EXTENSION_NAME_SIZE - 1);
        extInstance.instanceExtensions.push_back(p);
    }
    for (const char *l : instLayers) {
        VkLayerProperties p{};
        std::strncpy(p.layerName, l, VK_MAX_EXTENSION_NAME_SIZE - 1);
        extInstance.instanceLayers.push_back(p);
    }

    static Ogre::AbiCookie cookie = Ogre::generateAbiCookie();
    Root *root = new Root(&cookie, "", "", "ogre-xr-spike.log", "JahshakaVrSpike");
    NameValuePairList pluginOpts;
    pluginOpts["external_instance"] = StringConverter::toString(uintptr_t(&extInstance));
    root->loadPlugin(std::string(JAH_OGRE_PLUGIN_DIR) + "/RenderSystem_Vulkan", false, &pluginOpts);
    const RenderSystemList &rsList = root->getAvailableRenderers();
    if (!check(!rsList.empty(), "the Vulkan plugin loaded on the runtime's instance")) return 1;
    root->setRenderSystem(rsList[0]);
    root->initialise(false);

    // ---- 5. THE DEVICE OGRE WOULD HAVE BUILT (ogre-patch 0068) -------------
    uint32_t numExt = 0;
    vkEnumerateDeviceExtensionProperties(physDev, nullptr, &numExt, nullptr);
    FastArray<VkExtensionProperties> availExt;
    availExt.resize(numExt);
    vkEnumerateDeviceExtensionProperties(physDev, nullptr, &numExt, availExt.begin());

    VulkanDeviceCreationRequest request;
    VulkanDevice::buildDeviceCreationRequest(vkInstance, physDev, availExt, request);
    say("DEVREQ %zu extensions, features2=%d", request.extensions.size(), int(request.hasFeatures2));
    check(std::any_of(request.extensions.begin(), request.extensions.end(),
                      [](const char *e) { return !std::strcmp(e, VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME); }),
          "the exported list carries VK_EXT_shader_viewport_index_layer (instanced stereo)");
    check(std::any_of(request.extensions.begin(), request.extensions.end(),
                      [](const char *e) { return !std::strcmp(e, VK_KHR_SWAPCHAIN_EXTENSION_NAME); }),
          "the exported list carries VK_KHR_swapchain");

    // ONE graphics queue, family = the first with VK_QUEUE_GRAPHICS_BIT. That is
    // what Ogre's findGraphicsQueue picks and what VulkanQueue::setExternalQueue
    // can find again by matching vkGetDeviceQueue (it THROWS otherwise).
    uint32_t numFam = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physDev, &numFam, nullptr);
    std::vector<VkQueueFamilyProperties> fam(numFam);
    vkGetPhysicalDeviceQueueFamilyProperties(physDev, &numFam, fam.data());
    uint32_t gfxFamily = uint32_t(-1);
    for (uint32_t i = 0; i < numFam; ++i)
        if (fam[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { gfxFamily = i; break; }
    if (!check(gfxFamily != uint32_t(-1), "a graphics queue family exists")) return 1;
    say("QUEUE  family %u of %u (%u queues, flags 0x%x)", gfxFamily, numFam,
        fam[gfxFamily].queueCount, fam[gfxFamily].queueFlags);

    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = gfxFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = uint32_t(request.extensions.size());
    dci.ppEnabledExtensionNames = request.extensions.begin();
    if (request.hasFeatures2) dci.pNext = request.pNext();
    else                      dci.pEnabledFeatures = &request.features;

    XrVulkanDeviceCreateInfoKHR xrDci{ XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR };
    xrDci.systemId = xr.systemId;
    xrDci.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
    xrDci.vulkanPhysicalDevice = physDev;
    xrDci.vulkanCreateInfo = &dci;
    VkDevice vkDevice = VK_NULL_HANDLE;
    XR_TRY(xr.CreateVulkanDevice(xr.instance, &xrDci, &vkDevice, &vkErr),
           "xrCreateVulkanDeviceKHR");
    if (!check(vkErr == VK_SUCCESS && vkDevice,
               "xrCreateVulkanDeviceKHR made the VkDevice from Ogre's own createInfo"))
        return 1;

    VkQueue gfxQueue = VK_NULL_HANDLE;
    vkGetDeviceQueue(vkDevice, gfxFamily, 0, &gfxQueue);

    VulkanExternalDevice extDevice{};
    extDevice.physicalDevice = physDev;
    extDevice.device = vkDevice;
    extDevice.graphicsQueue = gfxQueue;
    extDevice.presentQueue = gfxQueue;
    extDevice.creationRequest = &request;   // ogre-patch 0068: the ENABLED set
    for (const char *e : request.extensions) {
        VkExtensionProperties p{};
        std::strncpy(p.extensionName, e, VK_MAX_EXTENSION_NAME_SIZE - 1);
        extDevice.deviceExtensions.push_back(p);
    }

    // ---- 6. the first window CONSUMES external_device ----------------------
    NameValuePairList winOpts;
    winOpts["windowType"] = "null";
    winOpts["external_device"] = StringConverter::toString(uintptr_t(&extDevice));
    root->createRenderWindow("spike", 1u, 1u, false, &winOpts);
    reportCaps(root, "xr");

    {   // Hlms, after a render target exists
        ArchiveManager &am = ArchiveManager::getSingleton();
        const std::string media = std::string(JAH_OGRE_MEDIA_DIR) + "/";
        String mainPath; StringVector libPaths;
        HlmsUnlit::getDefaultPaths(mainPath, libPaths);
        {
            ArchiveVec libs;
            for (const auto &p : libPaths) libs.push_back(am.load(media + p, "FileSystem", true));
            root->getHlmsManager()->registerHlms(OGRE_NEW HlmsUnlit(am.load(media + mainPath, "FileSystem", true), &libs));
        }
        HlmsPbs::getDefaultPaths(mainPath, libPaths);
        {
            ArchiveVec libs;
            for (const auto &p : libPaths) libs.push_back(am.load(media + p, "FileSystem", true));
            root->getHlmsManager()->registerHlms(OGRE_NEW HlmsPbs(am.load(media + mainPath, "FileSystem", true), &libs));
        }
    }

    const unsigned eyeW = xr.viewCfg[0].recommendedImageRectWidth;
    const unsigned eyeH = xr.viewCfg[0].recommendedImageRectHeight;
    SpikeScene sc;
    if (!check(buildScene(root, sc, eyeW, eyeH), "the scene built on the runtime's device")) return 1;
    say("RTT    VkImage 0x%llx",
        (unsigned long long)static_cast<VulkanTextureGpu *>(sc.rtt)->getFinalTextureName());

    // ---- 6b. THE BLACK-FRAME PROOF (patch 0068 hunk 1) ---------------------
    // Before anything XR-shaped: does a frame render at all on an external device?
    // Rendered at the FIXED parity size, never the runtime's eye size, so the picture
    // is comparable with the --plain arm's on any runtime and any compositor.
    renderParityPose(root, sc, outDir + "/parity-xr.ppm");

    // ---- 7. the session ----------------------------------------------------
    XrGraphicsBindingVulkan2KHR binding{ XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR };
    binding.instance = vkInstance;
    binding.physicalDevice = physDev;
    binding.device = vkDevice;
    binding.queueFamilyIndex = gfxFamily;
    binding.queueIndex = 0;
    XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
    sci.next = &binding;
    sci.systemId = xr.systemId;
    XR_TRY(xrCreateSession(xr.instance, &sci, &xr.session), "xrCreateSession");
    say("SESSION created on the Ogre device");

    // STAGE space when the runtime has one (its origin is on the FLOOR, so a
    // scene authored with the ground at y = 0 puts the wearer's head where a
    // head is), LOCAL otherwise. Phase 1b measured why: in LOCAL space on
    // WiVRn the Quest Pro's eyes were located at y = -0.70 m, i.e. BELOW the
    // spike's ground plane, and the owner saw the ground from underneath and
    // no cube at all (ledger 585). The simulated HMD never showed it because
    // its head sits at the origin.
    XrReferenceSpaceType spaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    {
        uint32_t nSpaces = 0;
        xrEnumerateReferenceSpaces(xr.session, 0, &nSpaces, nullptr);
        std::vector<XrReferenceSpaceType> spaces(nSpaces);
        if (nSpaces) xrEnumerateReferenceSpaces(xr.session, nSpaces, &nSpaces, spaces.data());
        for (XrReferenceSpaceType t : spaces)
            if (t == XR_REFERENCE_SPACE_TYPE_STAGE) spaceType = t;
    }
    say("SPACE  %s", spaceType == XR_REFERENCE_SPACE_TYPE_STAGE ? "STAGE (floor origin)" : "LOCAL (the runtime offers no STAGE)");
    XrReferenceSpaceCreateInfo rsci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rsci.referenceSpaceType = spaceType;
    rsci.poseInReferenceSpace.orientation.w = 1.0f;
    XR_TRY(xrCreateReferenceSpace(xr.session, &rsci, &xr.space), "xrCreateReferenceSpace");

    uint32_t fmtCount = 0;
    xrEnumerateSwapchainFormats(xr.session, 0, &fmtCount, nullptr);
    std::vector<int64_t> formats(fmtCount);
    xrEnumerateSwapchainFormats(xr.session, fmtCount, &fmtCount, formats.data());
    {
        std::string s;
        for (int64_t f : formats) s += std::to_string(f) + " ";
        say("FORMATS %u offered (VkFormat ids): %s", fmtCount, s.c_str());
    }
    xr.swapchainFormat = 0;
    for (int64_t f : formats)
        if (f == VK_FORMAT_R8G8B8A8_SRGB) { xr.swapchainFormat = f; break; }
    // NO FALLBACK. Taking formats[0] would hand vkCmdCopyImage a 4-byte-per-pixel
    // source and (on Monado) an 8-byte-per-pixel destination — not format-compatible,
    // undefined, and silently wrong. A runtime that does not offer this format needs a
    // matching eye target, which is a change, not a fallback.
    if (!check(xr.swapchainFormat != 0,
               "the runtime offers VK_FORMAT_R8G8B8A8_SRGB (the eye target's format)"))
        return 1;

    for (int eye = 0; eye < 2; ++eye) {
        XrSwapchainCreateInfo swci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
        swci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        swci.format = xr.swapchainFormat;
        swci.sampleCount = 1;
        swci.width = eyeW; swci.height = eyeH;
        swci.faceCount = 1; swci.arraySize = 1; swci.mipCount = 1;
        XR_TRY(xrCreateSwapchain(xr.session, &swci, &xr.swapchain[eye]), "xrCreateSwapchain");
        uint32_t n = 0;
        xrEnumerateSwapchainImages(xr.swapchain[eye], 0, &n, nullptr);
        xr.images[eye].assign(n, { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });
        xrEnumerateSwapchainImages(xr.swapchain[eye], n,
                                   &n, reinterpret_cast<XrSwapchainImageBaseHeader *>(xr.images[eye].data()));
        std::string handles;
        for (const auto &im : xr.images[eye]) {
            char b[32]; std::snprintf(b, sizeof(b), "0x%llx ", (unsigned long long)im.image);
            handles += b;
        }
        say("SWAPCHAIN eye %d: %u images of %ux%u  VkImage %s", eye, n, eyeW, eyeH,
            handles.c_str());
    }

    // ---- 8. the pump -------------------------------------------------------
    int submitted = 0, rendered = 0;
    std::vector<unsigned char> leftInPump;   // the left eye, read inside the pump
    Ogre::Vector3 leftPos; Ogre::Quaternion leftRot; XrFovf leftFov{};
    bool haveLeft = false;
    bool sessionRunning = false, quit = false, ipdChecked = false;
    double copyMsTotal = 0.0; int copyMsCount = 0;
    while (!quit && submitted < wantFrames) {
        XrEventDataBuffer ev{ XR_TYPE_EVENT_DATA_BUFFER };
        while (xrPollEvent(xr.instance, &ev) == XR_SUCCESS) {
            if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                auto *ss = reinterpret_cast<XrEventDataSessionStateChanged *>(&ev);
                xr.state = ss->state;
                say("STATE  %d", int(xr.state));
                if (xr.state == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo bi{ XR_TYPE_SESSION_BEGIN_INFO };
                    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    XR_TRY(xrBeginSession(xr.session, &bi), "xrBeginSession");
                    sessionRunning = true;
                } else if (xr.state == XR_SESSION_STATE_STOPPING) {
                    xrEndSession(xr.session);
                    sessionRunning = false; quit = true;
                } else if (xr.state == XR_SESSION_STATE_EXITING ||
                           xr.state == XR_SESSION_STATE_LOSS_PENDING) {
                    quit = true;
                }
            } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                say("LOST   the XR instance is going away");
                quit = true;
            }
            ev = { XR_TYPE_EVENT_DATA_BUFFER };
        }
        if (!sessionRunning) continue;

        XrFrameWaitInfo fwi{ XR_TYPE_FRAME_WAIT_INFO };
        XrFrameState fs{ XR_TYPE_FRAME_STATE };
        XR_TRY(xrWaitFrame(xr.session, &fwi, &fs), "xrWaitFrame");
        XrFrameBeginInfo fbi{ XR_TYPE_FRAME_BEGIN_INFO };
        XR_TRY(xrBeginFrame(xr.session, &fbi), "xrBeginFrame");

        std::vector<XrCompositionLayerProjectionView> projViews(2);
        XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        bool haveLayer = false;

        if (fs.shouldRender) {
            XrViewState vs{ XR_TYPE_VIEW_STATE };
            XrViewLocateInfo vli{ XR_TYPE_VIEW_LOCATE_INFO };
            vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            vli.displayTime = fs.predictedDisplayTime;
            vli.space = xr.space;
            uint32_t got = 0;
            XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
            XR_TRY(xrLocateViews(xr.session, &vli, &vs, 2, &got, views), "xrLocateViews");

            Vector3 eyePos[2];
            for (int eye = 0; eye < 2; ++eye) {
                const XrPosef &p = views[eye].pose;
                eyePos[eye] = Vector3(p.position.x, p.position.y, p.position.z);
                sc.camera->setPosition(eyePos[eye]);
                sc.camera->setOrientation(Quaternion(p.orientation.w, p.orientation.x,
                                                     p.orientation.y, p.orientation.z));
                sc.camera->setCustomProjectionMatrix(
                    true, projectionFromFov(views[eye].fov, 0.05f, 500.0f));
                root->renderOneFrame();
                ++rendered;

                uint32_t imgIdx = 0;
                XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
                XR_TRY(xrAcquireSwapchainImage(xr.swapchain[eye], &ai, &imgIdx),
                       "xrAcquireSwapchainImage");
                XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                wi.timeout = XR_INFINITE_DURATION;
                XR_TRY(xrWaitSwapchainImage(xr.swapchain[eye], &wi), "xrWaitSwapchainImage");

                const auto t0 = std::chrono::steady_clock::now();
                copyEyeToSwapchain(static_cast<VulkanRenderSystem *>(root->getRenderSystem()),
                                   sc.rtt, xr.images[eye][imgIdx].image, eyeW, eyeH);
                copyMsTotal += std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - t0).count();
                ++copyMsCount;

                restoreRttLayout(sc.rtt);

                // On the LAST pumped frame keep the left eye's picture and its
                // pose: the mono control below re-renders that exact pose with
                // nothing else going on, and the two must agree.
                if (eye == 0 && submitted == wantFrames - 1) {
                    root->getRenderSystem()->flushCommands();
                    unsigned rw, rh;
                    readRtt(sc.rtt, leftInPump, rw, rh);
                    leftPos = eyePos[0];
                    leftRot = Quaternion(views[0].pose.orientation.w, views[0].pose.orientation.x,
                                         views[0].pose.orientation.y, views[0].pose.orientation.z);
                    leftFov = views[0].fov;
                    haveLeft = true;
                }

                XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                XR_TRY(xrReleaseSwapchainImage(xr.swapchain[eye], &ri),
                       "xrReleaseSwapchainImage");

                projViews[eye] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
                projViews[eye].pose = views[eye].pose;
                projViews[eye].fov = views[eye].fov;
                projViews[eye].subImage.swapchain = xr.swapchain[eye];
                projViews[eye].subImage.imageRect.offset = { 0, 0 };
                projViews[eye].subImage.imageRect.extent = { int32_t(eyeW), int32_t(eyeH) };
                projViews[eye].subImage.imageArrayIndex = 0;
            }

            if (!ipdChecked) {
                ipdChecked = true;
                const float ipd = (eyePos[1] - eyePos[0]).length();
                say("IPD    left=(%.4f %.4f %.4f) right=(%.4f %.4f %.4f) |d|=%.4f m",
                    eyePos[0].x, eyePos[0].y, eyePos[0].z,
                    eyePos[1].x, eyePos[1].y, eyePos[1].z, ipd);
                check(ipd > 0.03f && ipd < 0.10f,
                      "the two eyes are one plausible IPD apart in the located views");
                const XrFovf &fl = views[0].fov, &fr = views[1].fov;
                say("FOV    L(%.4f %.4f %.4f %.4f) R(%.4f %.4f %.4f %.4f)",
                    fl.angleLeft, fl.angleRight, fl.angleUp, fl.angleDown,
                    fr.angleLeft, fr.angleRight, fr.angleUp, fr.angleDown);
                // TWO INDEPENDENT THINGS, asserted independently.
                // (1) The POSES differ — that is what makes the two pictures stereo on
                //     every runtime, and it is the only one the simulated HMD can show.
                check((eyePos[1] - eyePos[0]).squaredLength() > 1e-8f,
                      "the two eyes are located at DIFFERENT positions");
                // (2) The PROJECTIONS differ — only where the runtime's fovs differ.
                //     Monado's simulated HMD hands both eyes the same symmetric fov, so
                //     asserting a difference there would assert the runtime, not us;
                //     what is asserted instead is that the two agree exactly when the
                //     fovs do. A real headset (the Quest Pro, phase 1b) has asymmetric
                //     per-eye fovs and takes the other arm.
                const Matrix4 pl = projectionFromFov(fl, 0.05f, 500.0f);
                const Matrix4 pr = projectionFromFov(fr, 0.05f, 500.0f);
                const bool fovsDiffer =
                    std::fabs(fl.angleLeft - fr.angleLeft) > 1e-6f ||
                    std::fabs(fl.angleRight - fr.angleRight) > 1e-6f ||
                    std::fabs(fl.angleUp - fr.angleUp) > 1e-6f ||
                    std::fabs(fl.angleDown - fr.angleDown) > 1e-6f;
                if (fovsDiffer) {
                    check(pl != pr,
                          "the per-eye fovs differ, and so do the per-eye projections");
                } else {
                    say("NOTE   this runtime gives both eyes the SAME fov, so the per-eye "
                        "projections are one matrix and only the poses separate the eyes "
                        "(an asymmetric projection is unexercised until a real headset)");
                    check(pl == pr, "identical fovs produce identical projections");
                }
            }
            haveLayer = true;
            layer.space = xr.space;
            layer.viewCount = 2;
            layer.views = projViews.data();
        }

        const XrCompositionLayerBaseHeader *layers[1] = {
            reinterpret_cast<XrCompositionLayerBaseHeader *>(&layer)
        };
        XrFrameEndInfo fei{ XR_TYPE_FRAME_END_INFO };
        fei.displayTime = fs.predictedDisplayTime;
        fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        fei.layerCount = haveLayer ? 1u : 0u;
        fei.layers = haveLayer ? layers : nullptr;
        XR_TRY(xrEndFrame(xr.session, &fei), "xrEndFrame");
        ++submitted;
        if (submitted == 1) {
            say("FIRST  xrEndFrame accepted");
            // The runtime's own cadence, straight from xrWaitFrame: this is the
            // clock a session paces from (VR_SPEC §4.3), not our timer.
            const double ms = double(fs.predictedDisplayPeriod) / 1.0e6;
            say("PACING predictedDisplayPeriod=%lld ns (%.3f ms, %.1f Hz) shouldRender=%d",
                (long long)fs.predictedDisplayPeriod, ms, ms > 0.0 ? 1000.0 / ms : 0.0,
                int(fs.shouldRender));
        }
    }
    say("FRAMES submitted=%d rendered=%d (eyes)", submitted, rendered);
    check(submitted >= wantFrames, "the required number of frames was submitted");
    if (copyMsCount)
        say("COPY   %.3f ms CPU per eye (record + FlushOnly submit), %d samples",
            copyMsTotal / copyMsCount, copyMsCount);

    // ---- 9. the left eye == a mono render at the left eye's projection -----
    // The left eye was rendered INSIDE the pump, with the previous frame's copy,
    // the swapchain acquire/wait and two layout round-trips around it. The
    // control renders the very same pose and projection with none of that
    // happening. Any difference is the pump disturbing the picture.
    if (haveLeft) {
        sc.camera->setPosition(leftPos);
        sc.camera->setOrientation(leftRot);
        sc.camera->setCustomProjectionMatrix(true, projectionFromFov(leftFov, 0.05f, 500.0f));
        root->renderOneFrame();
        root->getRenderSystem()->flushCommands();
        std::vector<unsigned char> mono; unsigned mw, mh;
        readRtt(sc.rtt, mono, mw, mh);
        writePpm(outDir + "/eye-left-in-pump.ppm", leftInPump, mw, mh);
        writePpm(outDir + "/eye-left-mono.ppm", mono, mw, mh);
        size_t worst = 0, differing = 0;
        for (size_t i = 0; i < mono.size(); ++i) {
            const size_t d = size_t(std::abs(int(mono[i]) - int(leftInPump[i])));
            if (d) ++differing;
            worst = std::max(worst, d);
        }
        say("MONO   left eye vs mono-at-left-projection: %zu of %zu bytes differ, worst %zu/255",
            differing, mono.size(), worst);
        check(worst <= 1, "the left eye equals a mono render at that eye's projection (<= 1/255)");
    } else {
        fail("no left-eye picture was captured inside the pump");
    }

    // ---- 10. teardown, in the order VR_SPEC §4.1 names ---------------------
    for (int eye = 0; eye < 2; ++eye)
        if (xr.swapchain[eye]) xrDestroySwapchain(xr.swapchain[eye]);
    if (xr.space) xrDestroySpace(xr.space);
    if (xr.session) xrDestroySession(xr.session);
    say("TEARDOWN xr session/swapchains destroyed");

    delete root;                       // workspaces -> scenes -> meshes -> Root
    say("TEARDOWN ogre Root deleted (external device NOT destroyed by Ogre)");
    vkDestroyDevice(vkDevice, nullptr);   // ours: Ogre never destroys an external device
    if (xr.instance) xrDestroyInstance(xr.instance);
    vkDestroyInstance(vkInstance, nullptr);
    say("TEARDOWN device/instance destroyed by us");
    return g_failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
static int runProbe() {
    Xr xr;
    if (!xrBegin(xr)) return 1;
    if (xr.instance) xrDestroyInstance(xr.instance);
    return g_failures ? 1 : 0;
}

int main(int argc, char **argv) {
    std::string mode = "xr", outDir = ".";
    int frames = 60;
    int pw = int(kParityW), ph = int(kParityH);
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--plain") mode = "plain";
        else if (a == "--probe") mode = "probe";
        else if (a == "--out" && i + 1 < argc) outDir = argv[++i];
        else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (a == "--size" && i + 2 < argc) { pw = std::atoi(argv[++i]); ph = std::atoi(argv[++i]); }
        else if (a == "--expect-runtime" && i + 1 < argc) g_expectRuntime = argv[++i];
    }
    say("MODE   %s frames=%d out=%s runtimeJson=%s", mode.c_str(), frames, outDir.c_str(),
        std::getenv("XR_RUNTIME_JSON") ? std::getenv("XR_RUNTIME_JSON") : "(unset)");
    int rc = 1;
    try {
        if (mode == "probe") rc = runProbe();
        else if (mode == "plain") rc = runPlain(outDir, unsigned(pw), unsigned(ph));
        else rc = runXr(outDir, frames);
    } catch (Ogre::Exception &e) {
        say("FAIL  Ogre exception: %s", e.getFullDescription().c_str());
        rc = 1;
    } catch (std::exception &e) {
        say("FAIL  exception: %s", e.what());
        rc = 1;
    }
    say("RESULT %s (%d failures)", rc == 0 ? "PASS" : "FAIL", g_failures);
    return rc;
}
