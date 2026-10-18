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

#include <chrono>
#include <functional>
#include <thread>

#include "jahshaka/engine/Engine.h"
#include "services/framemonitor.h"

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
    // FRAMES NOBODY SAW (RENDER_LOOP_MONITOR_SPEC §4.2's frame reason): a
    // thumbnail, a screenshot or a preview readback renders the whole engine
    // and is never displayed. A capture that could not tell those from the
    // owner's own frames would read as a loop running at twice the rate, with
    // half its frames mysteriously cheap. The cause is set before EVERY
    // renderOneFrame because the engine consumes it and resets to Driver.
    auto markOffscreen = [engine] {
        if (framemonitor::active())
            engine->submitFrame(jahshaka::engine::FrameInput::withCause(jahshaka::engine::FrameCause::Offscreen));
    };
    for (unsigned i = 0; i < minFrames; ++i) { markOffscreen(); engine->renderOneFrame(); ++rendered; }
    // renderOneFrame already waited at its head, so the counter read here is
    // taken with the streaming worker idle: any movement it shows afterwards
    // was caused by the frames above, which is exactly the condition of
    // interest.
    // THE STREAMING COUNTERS ARE A REQUEST (U3 stage 3): the worker's own, read on the engine's side — and the wait
    // and the reads after it are ONE request, so the answer is the wait's.
    unsigned long long seen = 0;
    engine->request([&](jahshaka::engine::Engine &e) { seen = e.textureLoadRequests(); });
    for (unsigned extra = 0; extra < maxExtra; ++extra) {
        bool gaveUp = false;
        unsigned long long now = 0;
        // THE WAIT IS THE HOST'S (WAIT-TEXTURE-LOADS-1): the render thread takes ONE step of the bounded drain per
        // request (Engine::pumpTextureLoads) and this thread waits between them - the render thread is never held by
        // the wait, so a free-running view's frames go on beside it. The drain, its budget and its cadence are the
        // ones waitForTextureLoads ran in one piece; only where the millisecond's sleep happens has moved.
        for (int step = 1; step == 1;) {
            engine->request([&](jahshaka::engine::Engine &e) {
                step = e.pumpTextureLoads();
                if (step != 1) {
                    gaveUp = e.textureWaitTimeouts() > 0u;
                    now = e.textureLoadRequests();
                }
            });
            if (step == 1) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // A WAIT THAT GAVE UP ENDS THE LOOP (defect 2026-09-08). Engine.h's
        // texture-wait contract says the wait is bounded: it drains while the
        // pending set shrinks and then stops, loudly. If that happened, the
        // load-request counter can keep moving without anything ever becoming
        // resident, and re-rendering would only spend maxExtra more frames
        // learning the same thing. The caller gets the picture it can have.
        if (gaveUp) break;
        if (now == seen) break;
        seen = now;
        markOffscreen();
        engine->renderOneFrame();
        ++rendered;
    }
    return rendered;
}

// ---- THE PICTURE SERVICE (ENGINE-UPGRADE-U3 stage 3; ONE_PICTURE E3) ------------------------------------------------
// EVERY PICTURE LEAVES THE ENGINE THROUGH HERE: the editor's photo, the player's shot, the preview tiles and the
// thumbnails ask for one — {the view, the settle policy} — and receive the image. ONE settle (the photo's inline copy
// of it is gone), one readback. Served inline on one thread for now (the frames are rendered here and the image is
// handed to `done` before this returns); stage 4 serves the request on the render thread behind a fence and delivers
// it on the game thread.
struct PictureRequest {
    jahshaka::engine::Scene *scene = nullptr;   ///< whose GI the settle waits on (giAtRest, the one predicate)
    jahshaka::engine::View  *view = nullptr;    ///< what is read back (null = settle only)
    /// THE SETTLE (PHOTON-GATHER-1d): with the engine's clock frozen (a photograph does not advance the world), draw
    /// the shot's first frame unconditionally where its own history starts young (`firstFrame`: a gathering scene, a
    /// GI configuration the shot's record changes), then frames until the scene is at rest (`untilRest`), at most
    /// `settleCap`; the clock handed back after (`restoreDelta` < 0: the delta the engine had).
    unsigned settleCap = 2000u;
    bool     firstFrame = false;
    bool     untilRest = false;
    float    restoreDelta = -1.0f;
    bool     hdr = false;                       ///< the linear radiance too (View::readPixelsHdr)
    /// THE STILL PICTURE'S POLICY (View::setOffscreenContract StillPicture): a scene that gathers draws the first frame
    /// and waits for rest; a scene that does not takes the path it always took (no settle frames).
    static PictureRequest stillPicture(jahshaka::engine::Scene *scene, jahshaka::engine::View *view,
                                       unsigned cap = 2000u)
    {
        PictureRequest r;
        r.scene = scene;
        r.view = view;
        r.settleCap = cap;
        r.firstFrame = r.untilRest = scene && scene->published().gi.gather.on;   // the snapshot (U3 stage 2)
        return r;
    }
};
struct Picture {
    bool                    ok = false;
    jahshaka::engine::Image  rgba;
    jahshaka::engine::ImageF hdr;
    unsigned                frames = 0;        ///< the settle's and the stable frames' count
};
inline void takePicture(jahshaka::engine::Engine *engine, const PictureRequest &req,
                        const std::function<void(Picture &&)> &done)
{
    Picture pic;
    if (engine && (req.firstFrame || req.untilRest)) {
        const float delta = req.restoreDelta >= 0.0f ? req.restoreDelta : engine->fixedFrameDelta();
        engine->submitFrame(jahshaka::engine::FrameInput::delta(0.0f));
        // FRAMES NOBODY SAW: the settle renders outside the driver's tick, and the monitor must not read them as driven.
        const auto frame = [&] {
            if (framemonitor::active())
                engine->submitFrame(jahshaka::engine::FrameInput::withCause(jahshaka::engine::FrameCause::Offscreen));
            engine->renderOneFrame();
            ++pic.frames;
        };
        unsigned i = 0;
        if (req.firstFrame) { frame(); ++i; }
        if (req.untilRest)
            for (; i < req.settleCap && !(req.scene && req.scene->published().gi.giAtRest); ++i) frame();
        engine->submitFrame(jahshaka::engine::FrameInput::delta(delta));
    }
    // Plus whatever the texture load-request counter still owes (THREADING_ADOPTION_SPEC.md P2 item 4).
    pic.frames += renderStableFrames(engine);
    if (req.view) {
        // THE READBACK IS A REQUEST (U3 stage 4a): answered on the engine's side.
        engine->request([&](jahshaka::engine::Engine &) {
            pic.ok = req.view->readPixels(pic.rgba);
            if (req.hdr) req.view->readPixelsHdr(pic.hdr);
        });
    }
    if (done) done(std::move(pic));
}

#endif   // STABLEOFFSCREENRENDER_H
