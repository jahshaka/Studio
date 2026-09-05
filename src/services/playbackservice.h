/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PLAYBACKSERVICE_H
#define PLAYBACKSERVICE_H

// PlaybackService — the play/edit state machine (APP_ARCHITECTURE_AUDIT §3.3).
//
// OWNS the playback state (Phase 4 flipped the storage out of UiManager's
// statics and deleted the hub): playing/paused, edit-vs-play mode, the
// player-tab flag and the in-place physics simulation flag. Scene playback
// drives the injected viewport; the play-button chrome is the shell's
// business — it connects to the mode signals.

#include <QObject>

class IEditorViewport;

/// Which half of the shared editor/player pair is active.
enum class SceneMode
{
    EditMode,
    PlayMode
};

class PlaybackService : public QObject
{
    Q_OBJECT

public:
    explicit PlaybackService(QObject *parent = nullptr) : QObject(parent) {}

    /// The viewport playback drives. Wired by the shell once the viewport
    /// exists; nullable (headless hosts have none).
    void setViewport(IEditorViewport *viewport) { this->viewport = viewport; }

    /// State halves of MainWindow::enterEditMode/enterPlayMode — the chrome
    /// (button text/icon) is applied by the shell on the signals.
    void enterEditMode();
    void enterPlayMode();

    /// Delegates to the viewport's own flag — the one the event handlers
    /// branch on. The service's `playing` member survives only for the
    /// viewport-less window between construction and setViewport.
    bool isPlaying() const;
    void setPlaying(bool p) { playing = p; }
    SceneMode sceneMode() const { return mode; }
    void setSceneMode(SceneMode m) { mode = m; }

    /// True while the PLAYER tab (not the editor's play-in-place) is active.
    bool isPlayerMode() const { return playerMode; }
    void setPlayerMode(bool on) { playerMode = on; }

    // Scene playback (drives the viewport).
    void playScene();
    void pauseScene();
    void restartScene();
    void stopScene();

    // In-place physics simulation.
    void startSimulation();
    void restartSimulation();
    void stopSimulation();
    bool isSimulationRunning() const { return simulationRunning; }
    void setSimulationRunning(bool running) { simulationRunning = running; }

signals:
    void editModeEntered();
    void playModeEntered();

private:
    IEditorViewport *viewport = nullptr;
    bool playing = false;
    bool playerMode = false;
    bool simulationRunning = false;
    SceneMode mode = SceneMode::EditMode;
};

#endif // PLAYBACKSERVICE_H
