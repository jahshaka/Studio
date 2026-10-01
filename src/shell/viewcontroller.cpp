/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/viewcontroller.h"

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QMainWindow>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>

#include <algorithm>

#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "bridge/enginehost.h"
#include "data/settingsmanager.h"
#include "services/framepacing.h"
#include "ui/style/stylesheet.h"
#include "viewport/enginerenderdriver.h"
#include "viewport/ieditorviewport.h"

ViewController::ViewController(QObject *parent) : QObject(parent)
{
}

void ViewController::setViewport(IEditorViewport *viewport)
{
    mViewport = viewport;
    if (!mViewport) return;
	// A REPORT, NOT A COMMAND. The viewport telling the toolbar what its camera
	// now is must only repaint the button — routing it through the projection
	// toggle would make every such report re-issue a view change (and, since
	// the change is a canonical view now, snap the camera).
	connect(mViewport->events(), &EditorViewportEvents::updateToolbarButton, this, [this]() {
		if (!mViewport || !mViewport->editorCamera()) return;
		syncProjectionButton(mViewport->editorCamera()->isPerspective);
		setViewsButtonLabel(mViewport->cameraView());
	});
}

ViewController::CameraControls ViewController::createCameraControls()
{
    CameraControls controls;

	controls.projection = new QPushButton;
	mProjection = controls.projection;
	controls.projection->setStyleSheet(StyleSheet::ViewportCameraToggle());
	// The icon used to appear only after the first changeProjection() call —
	// invisible on a transparent background, but an empty grey pill under the
	// chrome button spec. The editor camera starts perspective; say so.
	controls.projection->setIcon(QIcon(":/icons/perspective-view-80.png"));
	controls.projection->setToolTip(tr("Perspective view | Toggle to switch to orthogonal view"));
	controls.projection->setIconSize(QSize(17, 17));
	connect(controls.projection, &QPushButton::clicked, this, [this]() {
		if (mViewport && mViewport->editorCamera())
			changeProjection(!mViewport->editorCamera()->isPerspective);
	});

    // Views ▾ — canonical camera views (owner request): Perspective plus the
    // six orthographic axis views. Same path as the view.* shortcuts and the
    // editor.setView verb (applyCameraView).
    controls.views = new QToolButton;
    mViewsButton = controls.views;
    controls.views->setStyleSheet(StyleSheet::ViewportMenuButton());
    mViewsMenu = new QMenu;
    mViewsMenu->setStyleSheet(StyleSheet::QMenuFlat());
    auto viewsGroup = new QActionGroup(mViewsMenu);
    viewsGroup->setExclusive(true);
    const QVector<QPair<QString, QString>> canonicalViews = {
        { QStringLiteral("perspective"), QStringLiteral("Perspective") },
        { QStringLiteral("top"), QStringLiteral("Top") },
        { QStringLiteral("bottom"), QStringLiteral("Bottom") },
        { QStringLiteral("left"), QStringLiteral("Left") },
        { QStringLiteral("right"), QStringLiteral("Right") },
        { QStringLiteral("front"), QStringLiteral("Front") },
        { QStringLiteral("back"), QStringLiteral("Back") },
    };
    for (const auto &entry : canonicalViews) {
        QAction *action = mViewsMenu->addAction(entry.second);
        action->setCheckable(true);
        action->setChecked(entry.first == QLatin1String("perspective"));
        action->setData(entry.first);
        viewsGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, entry]() { applyCameraView(entry.first); });
        mViewsActions.push_back(action);
    }
    controls.views->setMenu(mViewsMenu);
    // The button SHOWS the current view, it does not advertise the menu: a
    // static "Views" label told the user nothing about which view they were
    // in (owner report 2026-09-07). It starts on Perspective — the viewport's
    // own starting view — and follows every path that changes it, the
    // dropdown, the view.* shortcuts and editor.setView alike, because they
    // all land in applyCameraView.
    controls.views->setToolTip(tr("Canonical camera views"));
    setViewsButtonLabel(QStringLiteral("perspective"));
    controls.views->setPopupMode(QToolButton::InstantPopup);

    // Camera ▾ — the switcher (CAMERAS_SPEC D4): the Viewport (explorer) plus
    // every scene camera by name. Choosing a camera PILOTS it; choosing
    // Viewport ejects. It is rebuilt on every open rather than kept in sync,
    // because the list is the document's and the document changes underneath it
    // (a camera added, renamed, deleted, a whole world closed) — and a stale
    // entry would hand the viewport a stale node.
    controls.cameras = new QToolButton;
    mCamerasButton = controls.cameras;
    controls.cameras->setStyleSheet(StyleSheet::ViewportMenuButton());
    mCamerasMenu = new QMenu;
    mCamerasMenu->setStyleSheet(StyleSheet::QMenuFlat());
    connect(mCamerasMenu, &QMenu::aboutToShow, this, &ViewController::rebuildCamerasMenu);
    controls.cameras->setMenu(mCamerasMenu);
    controls.cameras->setText("Camera ");
    controls.cameras->setPopupMode(QToolButton::InstantPopup);
    controls.cameras->setToolTip(tr("Render the viewport through the free explorer or a scene camera "
                                    "(choosing a camera pilots it)"));
    return controls;
}

void ViewController::followViewport()
{
    if (mViewport) setViewsButtonLabel(mViewport->cameraView());
}

// The camera switcher's list (CAMERAS_SPEC D4). Built on every open from the
// live document; the checkmark shows what the viewport is actually rendering
// through, which is the piloted camera or the explorer.
void ViewController::rebuildCamerasMenu()
{
    if (!mCamerasMenu) return;
    mCamerasMenu->clear();
    auto group = new QActionGroup(mCamerasMenu);
    group->setExclusive(true);

    const iris::CameraNodePtr piloted = mViewport ? mViewport->pilotedCamera()
                                                  : iris::CameraNodePtr();
    QAction *explorer = mCamerasMenu->addAction(tr("Viewport"));
    explorer->setCheckable(true);
    explorer->setChecked(piloted.isNull());
    group->addAction(explorer);
    connect(explorer, &QAction::triggered, this,
            [this]() { if (mViewport) mViewport->pilotCamera(iris::CameraNodePtr()); });

    auto scene = mViewport ? mViewport->getScene() : iris::ScenePtr();
    if (!scene || scene->cameras.isEmpty()) {
        QAction *none = mCamerasMenu->addAction(tr("No scene cameras"));
        none->setEnabled(false);
        return;
    }
    mCamerasMenu->addSeparator();
    // By NAME, and stable: a QHash's order is not, and a menu that reshuffles
    // between opens is unusable.
    QVector<iris::CameraNodePtr> cameras;
    for (const auto &cam : scene->cameras) if (cam) cameras.push_back(cam);
    std::sort(cameras.begin(), cameras.end(),
              [](const iris::CameraNodePtr &a, const iris::CameraNodePtr &b) {
                  if (a->getName() != b->getName()) return a->getName() < b->getName();
                  return a->getGUID() < b->getGUID();
              });
    for (const iris::CameraNodePtr &cam : cameras) {
        QAction *action = mCamerasMenu->addAction(
            cam->getName().isEmpty() ? tr("Camera") : cam->getName());
        action->setCheckable(true);
        action->setChecked(piloted == cam);
        group->addAction(action);
        const QString guid = cam->getGUID();
        connect(action, &QAction::triggered, this, [this, guid]() {
            if (!mViewport) return;
            auto sc = mViewport->getScene();
            if (!sc) return;
            if (auto target = sc->cameras.value(guid)) mViewport->pilotCamera(target);
        });
    }
}

bool ViewController::applyCameraView(const QString &name)
{
    if (!mViewport || !mViewport->setCameraView(name)) return false;

    // The projection button is a VIEW of the state, so it is updated and never
    // asked to re-apply anything (it used to call changeProjection, which is
    // now the command and would recurse).
    const bool perspective = (name == QLatin1String("perspective"));
    syncProjectionButton(perspective);
    // ...and an axis view is remembered, so the toggle's "orthographic" means
    // "back to the one I was in".
    if (!perspective) mLastOrthographicView = name;

    for (QAction *action : mViewsActions)
        action->setChecked(action->data().toString() == name);
    setViewsButtonLabel(name);
    return true;
}

void ViewController::setViewsButtonLabel(const QString &view)
{
    if (!mViewsButton) return;
    // The label is the checked action's own text, so the button and the menu
    // can never spell the same view differently.
    for (QAction *action : mViewsActions) {
        if (action->data().toString() != view) continue;
        mViewsButton->setText(action->text() + QStringLiteral(" "));
        return;
    }
    mViewsButton->setText(QStringLiteral("Perspective "));
}

// THE PROJECTION TOGGLE IS A VIEW CHANGE (hygiene lane, 2026-09-09).
//
// Three defects in one small function, all of them the same mistake — it did
// the work itself instead of asking the viewport:
//
//  1. It wrote `mViewport->getScene()->camera`, the SCENE's camera node. The
//     explorer this viewport flies is a different node (EngineSceneViewport::
//     editorCamera), so the button changed a camera nothing was looking
//     through and the picture did not change at all until something else
//     happened to re-push.
//  2. It never went through setCameraView, so the AXIS-VIEW ROTATION LOCK was
//     never armed or disarmed (the lock reads the projection precisely because
//     this button used to bypass it — enginesceneviewport.cpp says so at
//     cameraRotationLocked) and the per-view camera memory was not consulted.
//  3. It left the Views label reading "Perspective" over an orthographic
//     picture, because only applyCameraView relabels it.
//
// So it now asks for a canonical view, and "orthographic" means the last AXIS
// view this window was in (Top on a fresh window). That is the same state the
// Views menu produces, which is the point: two controls that mean the same
// thing must not be able to leave the editor in two different states.
void ViewController::changeProjection(bool val)
{
	applyCameraView(val ? QStringLiteral("perspective") : mLastOrthographicView);
}

void ViewController::syncProjectionButton(bool perspective)
{
	if (!mProjection) return;
	if (perspective) {
		mProjection->setIcon(QIcon(":/icons/perspective-view-80.png"));
		mProjection->setToolTip(tr("Perspective view | Toggle to switch to orthogonal view"));
	} else {
		mProjection->setIcon(QIcon(":/icons/orthogonal-view-80.png"));
		mProjection->setToolTip(tr("Orthogonal view | Toggle to switch to perspective view"));
	}
}

// ---- immersive fullscreen ----------------------------------------------------

void ViewController::setWindow(QMainWindow *window, const FullscreenChrome &chrome)
{
    mWindow = window;
    mChrome = chrome;
}

void ViewController::setImmersiveFullscreen(bool on)
{
    if (mImmersive == on) return;
    toggleImmersiveFullscreen();
}

// F11: immersive fullscreen — the window goes fullscreen and (in the editor
// space) the docks and toolbar hide; a second F11 restores exactly what was
// visible before (EDITOR_SHORTCUTS_SPEC §3).
void ViewController::toggleImmersiveFullscreen()
{
    if (!mWindow) return;
    if (mImmersive) { leaveImmersiveFullscreen(true); return; }
    mImmersive = true;
    mEntering = true;      // until the window manager says we are there
    if (mChrome.captureLayout) mChrome.captureLayout();
    mPreFullscreenMaximized = mWindow->isMaximized();
    if (mChrome.hide) mChrome.hide();
    mWindow->showFullScreen();
}

void ViewController::leaveImmersiveFullscreen(bool restoreWindow)
{
    // FIRST, so that the showNormal()/showMaximized() below — and any state
    // change somebody else made — cannot re-enter through windowStateChanged.
    mImmersive = false;
    mEntering = false;
    if (mChrome.restore) mChrome.restore();
    if (restoreWindow && mWindow)
        mPreFullscreenMaximized ? mWindow->showMaximized() : mWindow->showNormal();
}

// THE WINDOW STATE THIS CLASS DOES NOT OWN (RR2's finding, lane ENGINE-7 item
// 5). Immersive fullscreen is a window state PLUS a set of hidden docks, and
// anything can take the window out of that state without telling us:
// `app.resizeWindow()` calls showNormal() before resizing, a window manager
// offers its own control, and a desktop environment may un-fullscreen a window
// on a workspace change. The flag then claimed fullscreen while the window was
// windowed with its docks still hidden — and `setImmersiveFullscreen(true)`,
// which is idempotent against the flag, did NOTHING, so F11 was dead until it
// was pressed twice.
void ViewController::windowStateChanged()
{
    if (!mImmersive || !mWindow) return;
    // ARRIVED: from here a state change that is not fullscreen is a departure.
    if (mWindow->isFullScreen()) { mEntering = false; return; }
    // STILL ON THE WAY IN (round-2 review, item 5). showFullScreen() is a
    // request, and a window manager may answer a maximized window with an
    // intermediate state that carries neither flag; restoring the docks there
    // would put the whole editor chrome back INSIDE a window that is about to
    // go fullscreen.
    if (mEntering) return;
    // A MINIMISED fullscreen window is still fullscreen (Qt ORs the minimise
    // bit in), so isFullScreen() stays true and this does not fire for it.
    leaveImmersiveFullscreen(false);
}

// ---- frame pacing ------------------------------------------------------------

void ViewController::startFramePacing(QWidget *window, SettingsManager *settings)
{
    // PANEL-AWARE PACING (fps audit F1, services/framepacing.h). Two inputs:
    // the persisted mode and the refresh rate of the screen this window is on.
    EngineRenderDriver *driver = EngineHost::instance().driver();
    if (!driver) return;
    mPacingWindow = window;

    if (settings) {
        bool ok = false;
        const framepacing::Mode m = framepacing::modeFromName(
            settings->getValue(framepacing::settingsKey(), QString()).toString(), &ok);
        // An absent or unreadable value is not an error: Display is the default
        // and writing one back would invent a preference the user never made.
        if (ok) driver->setPacingMode(m);
    }

    // "Which screen is this window on" is a QWindow question, and the QWindow
    // does not exist until the widget is shown — which is AFTER this runs
    // (the viewport is constructor work). So take what is available now
    // (QWidget::screen(), the primary screen before a show) and hook the
    // screenChanged signal on the next event-loop turns, once the handle is
    // there. NOT createWinId(): forcing a native window early in engine mode is
    // exactly the class of thing AA_DontCreateNativeWidgetSiblings exists to
    // avoid, and this needs no help from it.
    hookPacingScreenSignal(8);
    updatePacingScreen();
}

void ViewController::hookPacingScreenSignal(int retriesLeft)
{
    if (!mPacingWindow) return;
    if (QWindow *handle = mPacingWindow->windowHandle()) {
        connect(handle, &QWindow::screenChanged, this, [this](QScreen *) { updatePacingScreen(); });
        updatePacingScreen();   // the real window may sit on another screen
        return;
    }
    if (retriesLeft <= 0) return;   // a session that never shows a window (scripted, headless)
    QTimer::singleShot(0, this, [this, retriesLeft] { hookPacingScreenSignal(retriesLeft - 1); });
}

void ViewController::updatePacingScreen()
{
    EngineRenderDriver *driver = EngineHost::instance().driver();
    if (!driver || !mPacingWindow) return;
    QWindow *handle = mPacingWindow->windowHandle();
    QScreen *s = handle && handle->screen() ? handle->screen() : mPacingWindow->screen();
    // The rate can change WITHOUT the screen changing (a mode switch, a
    // variable-refresh panel renegotiating), so the connection follows the
    // screen and is remade when the window moves.
    if (s != mPacingScreen) {
        if (mPacingRefreshConnection) disconnect(mPacingRefreshConnection);
        mPacingScreen = s;
        if (s) mPacingRefreshConnection =
            connect(s, &QScreen::refreshRateChanged, this,
                    [](qreal hz) {
                        if (EngineRenderDriver *d = EngineHost::instance().driver())
                            d->setRefreshHz(double(hz));
                    });
    }
    driver->setRefreshHz(s ? double(s->refreshRate()) : 0.0);
}
