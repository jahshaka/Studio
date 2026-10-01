/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EDITORPAGE_H
#define EDITORPAGE_H

// EditorPage — THE EDITOR PAGE ITSELF (D10-SHELL-MODULES): its nested
// QMainWindow (the docks' home), the viewport bar above the view (Screenshot,
// the camera controls, View Options with the overlays and the Atom / Photon
// views, Play Scene, Simulate Physics), the engine viewport — or the headless
// document-only stand-in — the Player page's widget on the same engine, and the
// player controls strip below. The View Options checkmarks are VIEWS of the
// viewport's overlay state (one door: the verb, the shortcut and the click all
// land on the viewport, and the checks follow its overlaysChanged).

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <functional>
#include <memory>

#include "shell/viewcontroller.h"

class Database;
class EnginePlayerView;
class IEditorViewport;
class MainWindow;
class PlayerWidget;
class QAction;
class QMainWindow;
class QMenu;
class QPushButton;
class QToolButton;
class QWidget;
class QtAwesome;
struct StudioServices;
namespace jahshaka { namespace engine { class Engine; } }

class EditorPage : public QObject
{
    Q_OBJECT
public:
    explicit EditorPage(QObject *parent = nullptr);

    struct Deps {
        MainWindow *shell = nullptr;          ///< the viewport's window
        QtAwesome *icons = nullptr;
        Database *db = nullptr;
        ViewController::CameraControls cameraControls;
        /// The engine viewport exists and the render driver is about to start:
        /// the window watches the Engine and paces the loop.
        std::function<void(const std::shared_ptr<jahshaka::engine::Engine> &)> engineStarted;
    };
    /// Builds the page, the bar, the viewport and the Player's widget.
    void build(const Deps &deps);
    /// The services the bar's buttons and the viewport's drops call (made
    /// after the page: they are constructed against its viewport).
    void setServices(StudioServices *services) { mServices = services; }

    QMainWindow *viewPort() const { return mViewPort; }
    IEditorViewport *viewport() const { return sceneView; }
    PlayerWidget *player() const { return playerView; }
    EnginePlayerView *playerBackend() const { return mPlayerBackend; }
    QPushButton *playSceneButton() const { return playSceneBtn; }
    QWidget *playerControls() const { return mPlayerControls; }
    QAction *gridAction() const { return gridCheckAction; }
    QAction *groundPlaneAction() const { return groundPlaneCheckAction; }

    // ---- the overlays (View Options) --------------------------------------
    /// The ONE place the frame-stats readout is switched: F3, the View Options
    /// row, the Preferences checkbox and editor.setOverlays({stats}) all land
    /// here, and it persists `show_fps` (STATS_OVERLAY_SPEC.md §5.3).
    void setShowFrameStats(bool on);
    /// The physics debug drawer with the menu's checkmark kept in sync
    /// (editor.setOverlays({physicsDebug})).
    void setPhysicsDebugOverlay(bool on);
    /// What the View Options menu's checkmarks show ({grid, groundPlane,
    /// lightWires, stats, physicsDebug}) — editor.overlays().menu.
    QVariantMap viewOptionChecks() const;
    /// The grid, light-wire and physics-debug overlays back to EditorData's
    /// defaults — a new scene and the create run, one body.
    void resetOverlaysToDefaults();
    /// The physics debug checkmark from a scene's saved editor state.
    void setPhysicsDebugChecked(bool on);
    int atomViewMode() const;
    void setAtomViewMode(int mode);
    int photonViewMode() const;
    void setPhotonViewMode(int mode);

    // ---- play --------------------------------------------------------------
    /// The bar's Play Scene button (and Alt+P on the editor).
    void onPlaySceneButton();
    /// The play-button chrome halves of edit/play mode — driven by
    /// PlaybackService's mode signals.
    void applyEditModeUi();
    void applyPlayModeUi();
    /// The player strip's play button reads "pause" (an autoplay reveal).
    void showPlayerPlaying();
    /// The Simulate Physics button back to its idle face (a close).
    void resetSimulationButton();

    /// The viewport container forwards a press to the view (the mouse grab).
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void syncOverlayChecks();
    void takeScreenshot();

    StudioServices *mServices = nullptr;
    QtAwesome *fontIcons = nullptr;
    QMainWindow *mViewPort = nullptr;
    IEditorViewport *sceneView = nullptr;
    PlayerWidget *playerView = nullptr;
    EnginePlayerView *mPlayerBackend = nullptr;
    QWidget *sceneContainer = nullptr;
    QWidget *controlBar = nullptr;
    QWidget *mPlayerControls = nullptr;
    QPushButton *playSceneBtn = nullptr;
    QPushButton *playSimBtn = nullptr;
    QPushButton *restartBtn = nullptr;
    QPushButton *playBtn = nullptr;
    QPushButton *stopBtn = nullptr;
    QMenu *wireFramesMenu = nullptr;
    QToolButton *wireFramesButton = nullptr;
    QAction *wireCheckAction = nullptr;
    QAction *physicsCheckAction = nullptr;
    QAction *gridCheckAction = nullptr;
    QAction *groundPlaneCheckAction = nullptr;   ///< View Options "Ground Plane" (WORLD-MODEL-1)
    QAction *statsCheckAction = nullptr;         // F3 frame-stats readout (persisted)
    /// THE ATOM VIEW sub-menu of View Options (D0-ATOM-VIEW): Off, Triangles,
    /// Levels, Buckets, Objects — exclusive, in AtomView's order; F6 cycles it.
    /// Both call the scene's setAtomView, the one path world.setAtomView takes.
    QVector<QAction *> atomViewActions;
    /// View Options -> Photon View (PHOTON-VIEW-1): Off, Voxels, Probes, Cards,
    /// Screen Probes, Diffuse GI Only, Reflections Only, Ray Hits — exclusive, in
    /// PhotonView's order; F7 cycles through the ones that can paint. Both call
    /// the scene's setPhotonView behind its photonViewRefusal.
    QVector<QAction *> photonViewActions;
};

#endif // EDITORPAGE_H
