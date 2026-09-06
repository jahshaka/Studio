#ifndef STABLEOFFSCREENRENDER_H
#define STABLEOFFSCREENRENDER_H

// renderStableFrames — "render until the picture cannot change any more"
// (SPECS/THREADING_ADOPTION_SPEC.md P2 item 4).
//
// THE PROBLEM THIS REPLACES. Every offscreen readback in the app rendered a
// FIXED two frames and then read the pixels. Two was right for one reason (a
// swapchain-less RTT needs a second pass through the compositor before its
// contents are stable — see bridge/offscreenrenderscope.h) and was silently
// relied on for a second, different reason: textures were loaded synchronously,
// so by the time anything rendered they were all resident. Since P2 they are
// not: loadTexture SCHEDULES and the frame edge collects. A texture whose load
// request is raised DURING a frame is resident for the NEXT one, so a fixed
// count can, in the rare case, read back a frame drawn without it.
//
// THE FIX IS UPSTREAM'S OWN RECIPE, quoted at OgreTextureGpuManager.h:849-868:
// wait for streaming, snapshot getLoadRequestsCounter(), render, and if the
// counter moved, wait and render again. Applied on top of the existing minimum
// rather than instead of it, because the two frames were never only about
// textures — dropping to one would be a compositor change wearing a texture
// change's clothes, and the pixel suites are the wrong place to discover that.
//
// COST IN THE COMMON CASE: two `isDoneStreaming()` reads and one 64-bit compare.
// Nothing that was not already loading causes an extra frame.
//
// UI THREAD ONLY, like every Engine call, and meant to be used INSIDE an
// OffscreenRenderScope so the on-screen views do not pay for the extra frames.

#include "jahshaka/engine/Engine.h"

/// Renders at least `minFrames` frames, then keeps rendering while the engine's
/// texture load-request counter moves between frames, up to `maxExtra` more.
/// Returns how many frames were actually rendered.
///
/// `maxExtra` is a safety stop, not a policy: a scene that genuinely schedules
/// new textures on every frame (nothing in this app does — the mirror walk that
/// requests them runs before the frame) would otherwise spin here. Reaching it
/// means something is wrong, and the caller still gets a picture.
inline unsigned renderStableFrames(jahshaka::engine::Engine *engine,
                                   unsigned minFrames = 2u, unsigned maxExtra = 4u)
{
    if (!engine) return 0u;
    unsigned rendered = 0u;
    for (unsigned i = 0; i < minFrames; ++i) { engine->renderOneFrame(); ++rendered; }
    // renderOneFrame already waited at its head, so the counter read here is
    // taken with the streaming worker idle: any movement it shows afterwards
    // was caused by the frames above, which is exactly the condition of
    // interest.
    unsigned long long seen = engine->textureLoadRequests();
    for (unsigned extra = 0; extra < maxExtra; ++extra) {
        engine->waitForTextureLoads();
        const unsigned long long now = engine->textureLoadRequests();
        if (now == seen) break;
        seen = now;
        engine->renderOneFrame();
        ++rendered;
    }
    return rendered;
}

#endif   // STABLEOFFSCREENRENDER_H
