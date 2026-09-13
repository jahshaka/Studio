// compute.indirect_dispatch — GPU-DRIVEN COMPUTE DISPATCH (ogre-patch 0032).
//
// THE CLAIM UNDER TEST, in one sentence: a compute job can be sized by the
// GPU — job A counts how many items survived and writes the thread-group count,
// job B runs exactly that many groups — and the answer is the same as if the
// CPU had known the count all along.
//
// WHY IT MATTERS. Every Lumen-shaped stage compacts between passes; Epic calls
// indirect dispatch "essential" and measures up to a 50% tracing speedup from
// the compaction it enables (SPECS/research/LUMEN_SUPPORTING_TECH_2026-09-13.md
// §6). The pin had none: the only dispatch in the Vulkan render system was
// vkCmdDispatch with counts computed on the CPU, and HlmsComputeJob's
// "thread groups based on a texture/uav" is CPU-side arithmetic over that
// resource's DIMENSIONS — it can never see what a shader computed.
//
// WHAT EACH CASE PROVES, on the three sizes the brief asks for:
//   * 0 survivors     — vkCmdDispatchIndirect with x = 0 must run NOTHING. The
//                       degenerate case is the one a compaction hits most often
//                       (an empty tile list) and the one a CPU-sized dispatch
//                       cannot express without a CPU readback.
//   * 7 survivors     — an arbitrary small count no CPU-side rule could have
//                       derived from any resource's dimensions.
//   * 4096 survivors  — the full list, i.e. the worst case a non-indirect
//                       implementation would have had to dispatch every time.
// and for each: the count the GPU READ (gl_NumWorkGroups.x, stamped by every
// group into its own slot) equals the count job A WROTE, the number of stamped
// slots equals it too, and the whole output buffer is byte-identical to the same
// job dispatched from a CPU-side count.
//
// THE BARRIER. The compute-write -> indirect-read dependency is the one edge
// Ogre's BarrierSolver cannot express (its buffer branch only ever sets
// SHADER_READ/SHADER_WRITE, and ogreToVkStageFlags knows only the six shader
// stages, so VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT is unreachable from it), so the
// patch issues it by hand inside _dispatchIndirect. The probe runs the chain a
// fourth time with that barrier SUPPRESSED and reports what came back. That
// result is PRINTED, NEVER ASSERTED, in either direction: a GPU that wins the
// race proves nothing about the next one, and the barrier is unconditional
// precisely because the hazard is not observable on demand.
//
// It boots a real engine and one 8x8 offscreen view (a Vulkan compute queue
// needs a device, a Vulkan device on this pin needs a reachable display, and the
// Hlms — which owns the compute jobs — is only registered once a render target
// exists: startup order is load-bearing). It renders no frame: the only GPU work
// is the four dispatches and their readbacks.
#include "jahshaka/engine/Engine.h"

#include <cstdio>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static void runCase(Engine *e, unsigned survivors)
{
    std::printf("\n== %u survivors ==\n", survivors);
    IndirectDispatchProbe p;
    const bool ok = e->indirectDispatchProbe(survivors, p);
    if (!ok) {
        std::printf("FAIL: probe refused (supported=%d): %s\n", p.supported ? 1 : 0,
                    e->takeLastError().c_str());
        ++failures;
        return;
    }
    std::printf("   requested %u · ran %u · saw %u · cpu-sized ran %u · no-barrier ran %u\n",
                p.groupsRequested, p.groupsRan, p.groupsSeen, p.groupsRanCpuSized,
                p.groupsRanNoBarrier);

    char msg[192];
    std::snprintf(msg, sizeof(msg), "the counting job wrote %u (expected %u)",
                  p.groupsRequested, survivors);
    CHECK(p.groupsRequested == survivors, msg);

    std::snprintf(msg, sizeof(msg), "exactly %u groups ran indirectly (got %u)", survivors,
                  p.groupsRan);
    CHECK(p.groupsRan == survivors, msg);

    // The count the GPU READ out of the buffer, not the one we hoped it would.
    // Zero survivors means no group ran, so nothing could report it.
    std::snprintf(msg, sizeof(msg), "the groups saw gl_NumWorkGroups.x == %u (got %u)", survivors,
                  p.groupsSeen);
    CHECK(p.groupsSeen == (survivors ? survivors : 0u), msg);

    CHECK(p.groupsRanCpuSized == survivors, "the CPU-sized control ran the same count");
    CHECK(p.matchesCpuSized, "the indirect and CPU-sized output buffers are byte-identical");

    std::printf("   [control] barrier suppressed: %u groups, %s\n", p.groupsRanNoBarrier,
                p.noBarrierDiffered ? "DIFFERENT output (the hazard reproduced)"
                                    : "identical output (the hazard did not reproduce here)");
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-compute-indirect-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    // The Hlms (and with it every compute job in the material scripts) is
    // registered when the first render target is created — no view, no jobs.
    View *view = e->createOffscreenView("compute-indirect", 8u, 8u, Colour(0, 0, 0));
    Scene *scene = e->createScene("compute-indirect");
    if (!view || !scene) { std::printf("FAIL: view/scene: %s\n", e->takeLastError().c_str()); return 1; }
    view->setScene(scene);

    {
        IndirectDispatchProbe p;
        e->indirectDispatchProbe(0u, p);
        CHECK(p.supported, "the render system reports GPU-driven dispatch (Vulkan)");
        if (!p.supported) {
            std::printf("FAIL: no indirect dispatch — patch 0032 missing from this engine\n");
            return 1;
        }
    }

    runCase(e, 0u);
    runCase(e, 7u);
    runCase(e, 4096u);

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
