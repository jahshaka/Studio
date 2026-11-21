// engine.compute_job_slots (NONDET-1 1a, items iii and iii-b) - A COMPUTE JOB'S SLOT THAT CHANGES TYPE KEEPS ITS LISTENERS
// TRUE. The fork's HlmsComputeJob (OgreHlmsComputeJob.cpp), driven on the NULL render system (no display, no device):
//   (iii)   a BUFFER written over a slot that holds a texture removes the job from that texture's listeners, as a texture
//           written over a texture does - for a texture slot (setTexBuffer) and a UAV slot (_setUavBuffer). RED before:
//           the listener stayed, and a job destroyed first left the texture a dangling listener.
//   (iii-b) a TEXTURE written over a slot that holds a BUFFER calls nothing on that buffer: the slot is a union
//           (OgreDescriptorSetTexture.h / OgreDescriptorSetUav.h, Slot), and the setter read the buffer's pointer as the
//           old texture (hasEquivalentParameters, removeListener on a buffer object). Shown here by the flip completing
//           and the texture holding exactly the one listener entry the job owes it.
// Each law reads TextureGpu::getListeners() for the job's pointer: one entry a slot that names the texture.
#include <OgreRoot.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsCompute.h>
#include <OgreHlmsComputeJob.h>
#include <OgreTextureGpuManager.h>
#include <OgreTextureGpu.h>
#include <OgreRenderSystem.h>
#include <OgreWindow.h>
#include <OgreAbiUtils.h>
#include <Vao/OgreVaoManager.h>
#include <Vao/OgreTexBufferPacked.h>
#include <Vao/OgreUavBufferPacked.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

namespace {
size_t entriesOf(Ogre::TextureGpu *t, const Ogre::TextureGpuListener *job) {
    const auto &l = t->getListeners();
    return size_t(std::count(l.begin(), l.end(), job));
}
Ogre::DescriptorSetTexture2::TextureSlot texSlot(Ogre::TextureGpu *t) {
    Ogre::DescriptorSetTexture2::TextureSlot s(Ogre::DescriptorSetTexture2::TextureSlot::makeEmpty());
    s.texture = t;
    return s;
}
Ogre::DescriptorSetUav::TextureSlot uavSlot(Ogre::TextureGpu *t) {
    Ogre::DescriptorSetUav::TextureSlot s(Ogre::DescriptorSetUav::TextureSlot::makeEmpty());
    s.texture = t;
    s.access = Ogre::ResourceAccess::ReadWrite;
    s.pixelFormat = t->getPixelFormat();
    return s;
}
}   // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);   // (a crash keeps every line printed before it)
    // THE NULL RENDER SYSTEM: Root, the plugin, one 1x1 window (it creates the VaoManager and the texture manager, and
    // Root registers its compute Hlms after it); no scene manager.
    const Ogre::AbiCookie cookie = Ogre::generateAbiCookie();
    auto root = std::make_unique<Ogre::Root>(&cookie, "", "", "test-compute-job-slots-ogre.log", "slots");
    root->loadPlugin(std::string(JAHSHAKA_TEST_PLUGIN_DIR) + "/RenderSystem_NULL", false, nullptr);
    if (root->getAvailableRenderers().empty()) { std::printf("FAIL: no NULL render system\n"); return 1; }
    root->setRenderSystem(root->getAvailableRenderers().front());
    root->initialise(false);
    Ogre::Window *window = root->createRenderWindow("slots", 1u, 1u, false);
    Ogre::RenderSystem *rs = root->getRenderSystem();
    Ogre::HlmsCompute *hc = root->getHlmsManager()->getComputeHlms();
    if (!window || !rs || !hc || !rs->getTextureGpuManager() || !rs->getVaoManager()) {
        std::printf("FAIL: the NULL boot (window, render system, compute Hlms, managers)\n");
        return 1;
    }
    Ogre::TextureGpuManager *tm = rs->getTextureGpuManager();
    Ogre::VaoManager *vao = rs->getVaoManager();
    Ogre::TextureGpu *tex = tm->createTexture("slots/tex", Ogre::GpuPageOutStrategy::Discard,
                                              Ogre::TextureFlags::ManualTexture | Ogre::TextureFlags::Uav,
                                              Ogre::TextureTypes::Type2D);
    tex->setResolution(4u, 4u);
    tex->setPixelFormat(Ogre::PFG_RGBA8_UNORM);
    Ogre::TexBufferPacked *texBuf = vao->createTexBuffer(Ogre::PFG_R32_UINT, 64u, Ogre::BT_DEFAULT, nullptr, false);
    Ogre::UavBufferPacked *uavBuf = vao->createUavBuffer(16u, 4u, Ogre::BB_FLAG_UAV, nullptr, false);

    Ogre::DescriptorSetTexture2::BufferSlot tb(Ogre::DescriptorSetTexture2::BufferSlot::makeEmpty());
    tb.buffer = texBuf;
    Ogre::DescriptorSetUav::BufferSlot ub(Ogre::DescriptorSetUav::BufferSlot::makeEmpty());
    ub.buffer = uavBuf;
    ub.access = Ogre::ResourceAccess::ReadWrite;

    // (iii) A BUFFER OVER A TEXTURE: the job stops listening
    {
        std::printf("\n== a buffer written over a texture slot ==\n");
        Ogre::HlmsComputeJob *job = hc->createComputeJob("slots/iii", "slots/iii", "none", Ogre::StringVector());
        const Ogre::TextureGpuListener *listener = job;   // (compared by value after the job is destroyed)
        job->setNumTexUnits(1u);
        job->setNumUavUnits(1u);
        job->setTexture(0u, texSlot(tex), nullptr, false);
        job->_setUavTexture(0u, uavSlot(tex));
        const size_t both = entriesOf(tex, job);
        job->setTexBuffer(0u, tb);
        const size_t afterTex = entriesOf(tex, job);
        job->_setUavBuffer(0u, ub);
        const size_t afterUav = entriesOf(tex, job);
        std::printf("   the job's entries in the texture's listeners: %zu with both slots on it, %zu after the texture "
                    "slot took a buffer, %zu after the UAV slot did\n", both, afterTex, afterUav);
        CHECK(both == 2u, "one listener entry a slot that names the texture");
        CHECK(afterTex == 1u, "A BUFFER OVER A TEXTURE SLOT: the job no longer listens for that slot (setTexBuffer)");
        CHECK(afterUav == 0u, "A BUFFER OVER A UAV SLOT: the job no longer listens for that slot (_setUavBuffer)");
        hc->destroyComputeJob("slots/iii");
        CHECK(entriesOf(tex, listener) == 0u, "...so a destroyed job leaves the texture no listener");
    }

    // (iii-b) A TEXTURE OVER A BUFFER: nothing is called on the buffer, and the texture takes the one entry owed
    {
        std::printf("\n== a texture written over a buffer slot ==\n");
        Ogre::HlmsComputeJob *job = hc->createComputeJob("slots/iiib", "slots/iiib", "none", Ogre::StringVector());
        const Ogre::TextureGpuListener *listener = job;
        job->setNumTexUnits(1u);
        job->setNumUavUnits(1u);
        job->setTexBuffer(0u, tb);
        job->_setUavBuffer(0u, ub);
        job->setTexture(0u, texSlot(tex), nullptr, false);
        const size_t afterTex = entriesOf(tex, job);
        job->_setUavTexture(0u, uavSlot(tex));
        const size_t afterUav = entriesOf(tex, job);
        std::printf("   the job's entries in the texture's listeners: %zu after the texture slot's flip, %zu after the "
                    "UAV slot's\n", afterTex, afterUav);
        CHECK(afterTex == 1u && afterUav == 2u,
              "A TEXTURE OVER A BUFFER SLOT: the flip completes and the texture holds one entry a slot");
        CHECK(job->getTexture(0u) == tex && job->getUavTexture(0u) == tex, "...and both slots name the texture");
        hc->destroyComputeJob("slots/iiib");
        CHECK(entriesOf(tex, listener) == 0u, "...and a destroyed job leaves the texture no listener");
    }

    vao->destroyUavBuffer(uavBuf);
    vao->destroyTexBuffer(texBuf);
    tm->destroyTexture(tex);
    root.reset();
    std::printf(failures ? "\nFAILED (%d failures)\n" : "\nALL PASSED (%d failures)\n", failures);
    return failures ? 1 : 0;
}
