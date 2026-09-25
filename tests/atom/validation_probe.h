// THE VALIDATION ENTRIES' POSITIVE CONTROL (tests/atom). With VK_LAYER_KHRONOS_validation
// loaded, a device's own entry points resolve INTO the layer's library (it intercepts
// vkGetDeviceProcAddr). An entry that asks for validation (JAH_ATOM_EXPECT_VALIDATION=1)
// and finds the driver's function instead ran unvalidated — a "no report" from it would
// read as green — so the suite fails it.
#pragma once

#include <OgreRenderSystem.h>
#include <OgreRoot.h>
#include <OgreVulkanDevice.h>
#include <OgreVulkanRenderSystem.h>

#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace atomtest {

/// Prints where vkCmdDraw resolves; returns false only when validation was EXPECTED
/// (JAH_ATOM_EXPECT_VALIDATION) and the layer is not active.
inline bool validationProbe()
{
    auto *vkRs = dynamic_cast<Ogre::VulkanRenderSystem *>(Ogre::Root::getSingleton().getRenderSystem());
    if (!vkRs) return !std::getenv("JAH_ATOM_EXPECT_VALIDATION");
    const auto fn = reinterpret_cast<void *>(vkGetDeviceProcAddr(vkRs->getVulkanDevice()->mDevice, "vkCmdDraw"));
    Dl_info info{};
    const std::string lib = (fn && dladdr(fn, &info) && info.dli_fname) ? info.dli_fname : "?";
    const bool active = lib.find("VkLayer_khronos_validation") != std::string::npos;
    std::printf("  vkCmdDraw resolves into %s (validation layer %s)\n", lib.c_str(), active ? "ACTIVE" : "not active");
    return active || !std::getenv("JAH_ATOM_EXPECT_VALIDATION");
}

}  // namespace atomtest
