// THE VALIDATION LAYER'S PROOF, FOR A TEST BINARY (TESTING-CLEANUP-2 H4; Engine::validation,
// Types.h ValidationStatus). A row that runs under the Khronos validation layer and fails on
// "Validation Error" in its log proves nothing when the layer never loaded — no layer, no error,
// green. Every layered row calls this once its engine is up: it prints the readout and answers
// false when the layer was asked for (the environment, or the row's JAH_EXPECT_VALIDATION=1) and
// is not live on the device. JAH_EXPECT_VALIDATION is what keeps a row honest when its environment
// line is edited away: the row then fails instead of running unlayered.
//
// IT BITES (measured 2026-10-09, :71): sky.env_layout's binary with JAH_EXPECT_VALIDATION=1 and NO
// layer in its environment -> "requested no, expected yes, ACTIVE NO" and exit 1; under its row's
// VK_INSTANCE_LAYERS -> ACTIVE yes, vkCmdDraw resolving into libVkLayer_khronos_validation.so (so
// the legacy variable DOES load the layer on this loader). Read it once a VIEW exists: before the
// first view Ogre has no Vulkan device and the readout is "no Vulkan device" (measured: both rows
// red until the call moved below createOffscreenView).
#pragma once

#include "jahshaka/engine/Engine.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace enginetest {

inline bool validationProof(const jahshaka::engine::Engine &engine)
{
    const jahshaka::engine::ValidationStatus v = engine.validation();
    const char *expect = std::getenv("JAH_EXPECT_VALIDATION");
    const bool expected = expect && *expect && std::string(expect) != "0";
    std::string layers;
    for (const std::string &l : v.layers) layers += (layers.empty() ? "" : ", ") + l;
    std::printf("  validation: requested %s, expected %s, ACTIVE %s (vkCmdDraw resolves into %s); "
                "layer libraries loaded: %s\n",
                v.requested ? "yes" : "no", expected ? "yes" : "no", v.active ? "yes" : "NO",
                v.drawEntry.empty() ? "(no Vulkan device)" : v.drawEntry.c_str(),
                layers.empty() ? "none" : layers.c_str());
    return v.active || !(v.requested || expected);
}

}   // namespace enginetest
