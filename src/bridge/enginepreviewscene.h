#ifndef ENGINEPREVIEWSCENE_H
#define ENGINEPREVIEWSCENE_H

// EnginePreviewScene — the engine Scene + SceneMirror + View lifecycle every
// preview surface in the editor is built on (ENGINEERING_DEBT_SPEC item 6).
//
// WHAT THIS REPLACES. Four classes had grown the same forty lines: create a
// Scene with a worker-thread tier, set its ambient, make a SceneMirror on it,
// turn the editor's light wires off, point the mirror at a document, bind the
// Scene to a View — and, in reverse and in the RIGHT ORDER, take it all apart
// again. Three of them also had the same offscreen-capture routine (create a
// throwaway View, push sky and camera, render until the picture is stable, read
// the pixels back, destroy the View) and the same eight-line QImage conversion.
// Every copy was correct; that was the problem. The Ogre startup order below is
// undocumented upstream and load-bearing, so a fifth preview surface written
// from the wrong copy segfaults, and the fix for one is a fix for one.
//
// ORDER MATTERS, and it is why attach() is not virtual (Engine.h):
//   create   a View must exist BEFORE createScene() — the caller owns that View
//            (a widget's native window) or the subclass adopts one (thumbnails);
//            then Scene, then mirror, then view->setScene().
//   destroy  views (workspaces) before scenes, scenes before the document lets
//            go of its MeshPtrs, and everything before the Engine goes. A
//            MeshPtr outliving Root throws in VaoManager.
// A subclass customises through the hooks, all of which run INSIDE that order:
// configureScene (fresh Scene, before the mirror), configureMirror (fresh
// mirror, before the View is bound), configureView (each bind),
// releaseSubject (first thing in release(), while everything is still alive).
//
// SUBCLASS DESTRUCTORS MUST CALL release() THEMSELVES. ~EnginePreviewScene
// calls it too, but by then the subclass half is gone and the virtual hooks
// resolve to this class's empty ones — the base call is a backstop for a leak,
// not the teardown. release() is idempotent, so both calls are safe.
//
// NOT IN THE FAMILY, deliberately:
//
//   EnginePlayerScene (src/player/) stays its own class, and ADDENDUM 6 of the
//   debt spec is why: the player is not a preview, it is a SECOND FULL COPY of
//   the editor's world — same document, second engine Scene, second mirror,
//   second set of meshes/textures/datablocks, and under Epic a second VCT
//   voxelisation on every page switch. Folding it under this base would make
//   that duplication look intentional and structural instead of temporary. Its
//   real fix is the recorded follow-on: ONE scene shared with the editor, two
//   workspaces, helper suppression driven by a render flag, and mirror
//   ownership moved out of both. What it WOULD take to derive it here, if that
//   follow-on is ever declined: ensureScene() is attach() minus the View bind
//   (the base would need `bool ensureScene()` split out of attach()); its
//   configureMirror would be `setSource(mDocument)` plus
//   invalidateEnvironment(); configureView would be empty (the player takes
//   shadows/ambient/fog/MSAA/GI from applyEnvironment, never from a hardcode);
//   releaseSubject would be empty; and its takeScreenshot would be a
//   renderOffscreen() with a grade argument — which is the ONE thing the base
//   would have to grow, since no preview grades its capture. Roughly 40 lines
//   land in the base, 60 leave the player. It is not done because the class
//   should not exist at all.
//
//   EngineSceneViewport (src/viewport/) is the EDITOR's scene: it is a QWidget
//   with a native render window, a playback host, a gizmo host, a picking
//   surface and a compositor client, and its Scene outlives project loads. The
//   lifecycle overlap is real but it is a tenth of that class.
//
// No Ogre, no GL, no QWidget, no database: every subclass is testable headless
// against an offscreen View.

#include <memory>
#include <string>
#include <QImage>
#include "irisgl/irisglfwd.h"
#include "jahshaka/engine/Engine.h"
#include "bridge/sceneworkerthreads.h"

class SceneMirror;

class EnginePreviewScene
{
public:
    virtual ~EnginePreviewScene();

    EnginePreviewScene(const EnginePreviewScene &) = delete;
    EnginePreviewScene &operator=(const EnginePreviewScene &) = delete;

    /// Creates the engine Scene and its mirror (once) and binds them to `view`
    /// — which must ALREADY EXIST (see the order note above). Idempotent. False
    /// if the Engine is gone or the Scene could not be created.
    ///
    /// RE-BINDING, AND THE ONLY WAY IT REALLY HAPPENS. Called with a View other
    /// than the bound one, attach() moves the Scene rather than rebuilding it,
    /// and unbinds the old View first. That is correct only while the old View
    /// is ALIVE — and production never delivers it that way: the one caller
    /// that re-binds is EngineViewWidget::recreateViewForNewWindow() (a
    /// floatable dock torn off, QEvent::WinIdChange), which destroys the old
    /// View before making the new one. So the host must call forgetView()
    /// first, from viewAboutToBeDestroyed(); attach() then takes the plain
    /// "no View bound" path and never touches the freed pointer. Passing a live
    /// second View still works and is what preview.lifecycle covers, but no
    /// widget does it.
    bool attach(jahshaka::engine::View *view);

    /// Drops this object's pointer to the currently bound View WITHOUT touching
    /// the engine, because the caller is about to destroy that View itself
    /// (EngineViewWidget::viewAboutToBeDestroyed). The Scene and the mirror
    /// stay: the next attach() re-binds them to the replacement View.
    ///
    /// A View this object OWNS is not forgotten — nobody else may destroy it,
    /// so the request cannot be honest and is ignored.
    void forgetView();

    /// Destroys the mirror and the engine Scene — and the View too, if this
    /// object created it — while the Engine is still alive. Safe to call
    /// repeatedly and safe to call after the Engine has gone.
    void release();

    jahshaka::engine::Scene *engineScene() const { return mScene; }
    jahshaka::engine::View  *view() const { return mView; }

protected:
    /// `namePrefix` names the engine Scene ("<prefix>-<this>", unique per
    /// instance so two live previews never collide); `tier` is how many worker
    /// threads it gets (bridge/sceneworkerthreads.h).
    EnginePreviewScene(const std::shared_ptr<jahshaka::engine::Engine> &engine,
                       const char *namePrefix, sceneworkers::Tier tier);

    // ---- hooks (all run inside the load-bearing order) ----
    /// A freshly created Scene, before it has a mirror: ambient, reflection
    /// budgets — anything that must be set once and never changes.
    virtual void configureScene(jahshaka::engine::Scene *scene) = 0;
    /// A freshly created mirror, after setLightWires(false) (no preview shows
    /// editor wires) and before the View is bound: the source document, grid
    /// colours, overlays.
    virtual void configureMirror(SceneMirror *mirror) = 0;
    /// Each time a View is bound: shadows, mostly.
    virtual void configureView(jahshaka::engine::View *view) { (void)view; }
    /// The first thing release() does, while the mirror and Scene still exist.
    /// `sceneAlive` is false when the Engine or the Scene has already gone —
    /// anything that talks to the engine must be skipped then.
    virtual void releaseSubject(bool sceneAlive) { (void)sceneAlive; }
    /// What renderOffscreen() pushes into the shot View: the subclass's own
    /// document update / mirror sync / camera push.
    virtual void prepareOffscreen(jahshaka::engine::View *shot, int width, int height)
    { (void)shot; (void)width; (void)height; }

    // ---- shared machinery ----
    SceneMirror *mirror() const { return mMirror.get(); }
    std::shared_ptr<jahshaka::engine::Engine> engine() const { return mEngine.lock(); }

    /// The per-frame push every preview's step() ends with: aspect ratio from
    /// the view's pixels, then document -> engine, sky -> view, camera -> view.
    /// Does nothing without a bound View (there is nothing to push into).
    void pushFrame(const iris::CameraNodePtr &camera, int width, int height);

    /// Renders the current Scene into a THROWAWAY View of this size and reads
    /// it back. Works before the subject has ever been shown: with no Scene yet
    /// the shot View becomes the first View, which is what lets the Scene be
    /// created at all. `tag` names the view (plus this pointer and a serial, so
    /// repeated shots never reuse a name). The subclass pushes its own state in
    /// prepareOffscreen(); `mView` is the shot for the duration of that call,
    /// and is restored afterwards.
    QImage renderOffscreen(const char *tag, int width, int height,
                           const jahshaka::engine::Colour &background, bool shadows);

    /// Engine image -> QImage (RGBA8888). Null for an empty or short buffer.
    static QImage toQImage(const jahshaka::engine::Image &img);

    /// Take ownership of a View this object created (the thumbnail renderer's
    /// permanent offscreen surface): release() destroys it.
    void adoptView(jahshaka::engine::View *view);

    std::weak_ptr<jahshaka::engine::Engine> mEngine;
    jahshaka::engine::View  *mView  = nullptr;
    jahshaka::engine::Scene *mScene = nullptr;
    std::unique_ptr<SceneMirror> mMirror;

private:
    const char *mNamePrefix;
    int  mWorkerThreads;
    bool mOwnsView = false;
    unsigned mShotSerial = 0;
};

#endif   // ENGINEPREVIEWSCENE_H
