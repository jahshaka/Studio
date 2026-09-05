#ifndef OFFSCREENRENDERSCOPE_H
#define OFFSCREENRENDERSCOPE_H

// OffscreenRenderScope — quiet the on-screen views while something renders
// offscreen (fps audit F5).
//
// THE PROBLEM. Engine::renderOneFrame() draws EVERY enabled View; it is the
// only way to make an offscreen View produce pixels, and every readback in this
// program calls it twice (once to build the frame, once because a swapchain-
// less RTT still needs a second pass through the compositor before its contents
// are stable). So each thumbnail, each screenshot, each asset-preview snapshot
// also redraws the whole editor twice — at full scene cost, plus two vsync
// presents that BLOCK the UI thread on the display. A thumbnail queue ticking
// through a library therefore holds the editor at a fraction of its normal
// rate, and the frames it forces are never seen by anyone: they are drawn
// between two ordinary driver ticks and immediately overwritten.
//
// THE FIX, and it is the cheap one: disable every enabled ON-SCREEN View for
// the duration and put them back exactly as they were. View::setEnabled(false)
// is what the viewport widgets already use when they are hidden — it makes
// renderOneFrame skip the view's workspace entirely — so this is not a new
// mechanism, only a new user of it. Offscreen views are left alone: they are
// the ones being rendered, and other offscreen views (a material preview dock,
// an avatar preview) are cheap and may legitimately want their own frames.
//
// RAII, because the alternative has already been written wrong once per call
// site: an early return between the disable and the restore leaves the editor
// black until the next widget show/hide. The scope restores in its destructor,
// on every path, and is a no-op with no engine.
//
// NOT A THREADING PRIMITIVE. Engine calls are UI-thread-only (Engine.h thread
// affinity); this must be constructed and destroyed on that thread, around a
// renderOneFrame() call, and nothing may pump the event loop inside it (a
// driver tick during the scope would draw nothing and skew nothing — but a
// nested scope would restore the outer one's views early, so do not nest).
//
// The recorded follow-up the audit names — Engine::renderView(View*), which
// would make the whole problem disappear — is deliberately NOT this lane's.

#include <memory>
#include <vector>

#include "jahshaka/engine/Engine.h"

class OffscreenRenderScope
{
public:
    explicit OffscreenRenderScope(jahshaka::engine::Engine *engine) : mEngine(engine)
    {
        if (!mEngine) return;
        std::vector<jahshaka::engine::View *> views;
        mEngine->listViews(views);
        for (jahshaka::engine::View *v : views) {
            if (!v || v->isOffscreen() || !v->isEnabled()) continue;
            v->setEnabled(false);
            mSuspended.push_back(v);
        }
    }

    explicit OffscreenRenderScope(const std::shared_ptr<jahshaka::engine::Engine> &engine)
        : OffscreenRenderScope(engine.get()) {}

    ~OffscreenRenderScope()
    {
        // The Engine owns the Views and outlives this scope by construction
        // (nothing inside a readback destroys an on-screen view), so the
        // pointers are still good here.
        for (jahshaka::engine::View *v : mSuspended) v->setEnabled(true);
    }

    OffscreenRenderScope(const OffscreenRenderScope &) = delete;
    OffscreenRenderScope &operator=(const OffscreenRenderScope &) = delete;

    /// How many on-screen views this scope silenced — 0 when the editor was
    /// already hidden (a headless run, a page with no viewport). Tests assert
    /// on it; production code has no reason to ask.
    size_t suspendedCount() const { return mSuspended.size(); }

private:
    jahshaka::engine::Engine *mEngine = nullptr;
    std::vector<jahshaka::engine::View *> mSuspended;
};

#endif   // OFFSCREENRENDERSCOPE_H
