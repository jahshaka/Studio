/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef VIEWCONTROLLER_H
#define VIEWCONTROLLER_H

// ViewController — THE EDITOR'S VIEW OF THE WORLD, AS CONTROLS (D10-SHELL-MODULES).
//
// Two things a person does to the view, owned in one place:
//
// IMMERSIVE FULLSCREEN (F11 the key, editor.fullscreen the verb): the window
// goes fullscreen and, in the editor space, the editor's chrome — its docks
// and toolbar — hides; a second F11 restores exactly what was visible before
// (EDITOR_SHORTCUTS_SPEC §3). The chrome itself belongs to the editor's docks,
// which this asks to hide and come back (FullscreenChrome).
//
// The cameras menu, the canonical views and the projection toggle: the three
// controls of the viewport's bar that say what the editor camera looks
// through. They are views of the VIEWPORT's state — the viewport owns the
// camera, the per-view memory and the axis-view rotation lock — and every path
// that changes a view (the dropdown, the view.* chords, the projection button,
// editor.setView) lands in applyCameraView, so the button and the menu can
// never disagree about which view the editor is in.

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <functional>

class IEditorViewport;
class QAction;
class QMainWindow;
class QMenu;
class QPushButton;
class QToolButton;

class ViewController : public QObject
{
    Q_OBJECT
public:
    explicit ViewController(QObject *parent = nullptr);

    /// The viewport the controls follow (made after the controls are).
    void setViewport(IEditorViewport *viewport);

    /// The three buttons of the viewport bar, built once.
    struct CameraControls {
        QPushButton *projection = nullptr;   ///< the perspective/orthographic toggle
        QToolButton *views = nullptr;        ///< Views ▾ (the canonical views)
        QToolButton *cameras = nullptr;      ///< Camera ▾ (the explorer + scene cameras)
    };
    CameraControls createCameraControls();

    /// Views dropdown / view.* shortcuts / editor.setView verb — ONE path:
    /// snaps the editor camera to a canonical view ("top", "bottom", "left",
    /// "right", "front", "back", "perspective"), switches projection (axis
    /// views are orthographic) and keeps the toolbar + dropdown checks in
    /// sync. Returns false for an unknown name.
    bool applyCameraView(const QString &name);
    /// The PROJECTION TOGGLE, and it is a CANONICAL VIEW change (hygiene lane,
    /// 2026-09-09). `true` = perspective; `false` = the last orthographic axis
    /// view this window was in, "top" until there has been one.
    void changeProjection(bool perspective);
    /// The Views dropdown's label = the view it is currently in. Re-read from
    /// the viewport on every editor entry: a scene open resets it to
    /// perspective (per-view camera memory is per scene session).
    void followViewport();
    /// Rebuilds the camera switcher's list from the live document
    /// (CAMERAS_SPEC D4). Connected to the menu's aboutToShow.
    void rebuildCamerasMenu();

    // ---- immersive fullscreen -------------------------------------------
    /// What fullscreen asks of the editor's chrome.
    struct FullscreenChrome {
        /// The editor's dock layout WITH its chrome, before fullscreen takes it
        /// away: a quit from immersive fullscreen must not store an editor with
        /// no panels (lane SPACE-1).
        std::function<void()> captureLayout;
        /// Hide the docks and the toolbar (only on the editor space), recording
        /// what was visible and which bottom tab was in front.
        std::function<void()> hide;
        /// Put back exactly what hide() recorded, the front tab last.
        std::function<void()> restore;
    };
    void setWindow(QMainWindow *window, const FullscreenChrome &chrome);
    /// IMMERSIVE FULLSCREEN IS TWO THINGS — a window state and a set of hidden
    /// docks. The key flips it; setImmersiveFullscreen is the idempotent form
    /// the verb needs.
    void toggleImmersiveFullscreen();
    void setImmersiveFullscreen(bool on);
    bool isImmersiveFullscreen() const { return mImmersive; }
    /// The LEAVE half of the toggle, callable on its own. `restoreWindow` is
    /// false when the window state has already been changed by somebody else
    /// (windowStateChanged's case): the docks and the flag come back, the
    /// window is left exactly as it was found.
    void leaveImmersiveFullscreen(bool restoreWindow);
    /// The window's state changed (QMainWindow::changeEvent): the window state
    /// can be left without this class being asked (RR2, 2026-09-14) —
    /// `app.resizeWindow()` calls showNormal() before it resizes, and a window
    /// manager's own control does the same. The flag then said "fullscreen"
    /// while the window was not, so the next F11 did nothing at all. This
    /// watches the state it does not own and puts the chrome back.
    void windowStateChanged();

private:
    void setViewsButtonLabel(const QString &view);
    /// Icon + tooltip only: what the projection button LOOKS like. Split out of
    /// changeProjection so the viewport can report a projection it changed
    /// itself without that report turning into a command.
    void syncProjectionButton(bool perspective);

    IEditorViewport *mViewport = nullptr;
    QPointer<QPushButton> mProjection;
    QPointer<QToolButton> mViewsButton;
    QMenu *mViewsMenu = nullptr;
    QVector<QAction *> mViewsActions;   // checkable, ordered as built
    /// Where the projection toggle goes when it is asked for "orthographic":
    /// the last axis view this window was in, so Perspective -> Front ->
    /// Perspective -> (toggle) returns to Front rather than jumping to Top.
    QString mLastOrthographicView = QStringLiteral("top");
    /// The CAMERA SWITCHER (CAMERAS_SPEC D4), beside Views: "Viewport" (the
    /// free explorer) plus every scene camera by name. Rebuilt from the
    /// document each time it opens — cameras are added, renamed and deleted
    /// while the menu exists, and a stale list would pilot a dead node.
    QPointer<QToolButton> mCamerasButton;
    QMenu *mCamerasMenu = nullptr;

    QMainWindow *mWindow = nullptr;
    FullscreenChrome mChrome;
    // F11 immersive fullscreen restore state (EDITOR_SHORTCUTS_SPEC §3)
    bool mImmersive = false;
    /// ENTERING, and not there yet (round-2 review, item 5). `showFullScreen()`
    /// is a REQUEST: a window manager answers it with its own sequence, and a
    /// maximized window can be handed an intermediate state that does not carry
    /// the fullscreen flag — which windowStateChanged would read as "somebody
    /// took us out of fullscreen" and restore every dock INSIDE the fullscreen
    /// window. The latch is set when the toggle asks and cleared by the first
    /// state change that reports fullscreen; until then a non-fullscreen state
    /// is the transition, not a departure.
    bool mEntering = false;
    bool mPreFullscreenMaximized = false;
};

#endif // VIEWCONTROLLER_H
