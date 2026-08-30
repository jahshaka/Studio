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
// The one API for entering/leaving play mode, driving scene playback and the
// in-place physics simulation. The play-button chrome is the shell's business:
// it connects to the mode signals.
//
// Transitional (audit §9, Phase 4): the authoritative state still lives in
// UiManager's statics — 31 files read those variables directly and converting
// them is the hub-dissolution phase's job. This service is the mediator every
// NEW call site uses; when Phase 4 flips the storage into the service, only
// this .cpp changes.

#include <QObject>

#include "shell/uimanager.h"   // SceneMode + the transitional state storage

class PlaybackService : public QObject
{
    Q_OBJECT

public:
    explicit PlaybackService(QObject *parent = nullptr) : QObject(parent) {}

    /// State halves of MainWindow::enterEditMode/enterPlayMode — the chrome
    /// (button text/icon) is applied by the shell on the signals.
    void enterEditMode();
    void enterPlayMode();

    bool isPlaying() const;
    SceneMode sceneMode() const;

    // Scene playback (drives the viewport through UiManager for now).
    void playScene();
    void pauseScene();
    void restartScene();
    void stopScene();

    // In-place physics simulation.
    void startSimulation();
    void restartSimulation();
    void stopSimulation();
    bool isSimulationRunning() const;
    void setSimulationRunning(bool running);

signals:
    void editModeEntered();
    void playModeEntered();

private:
};

#endif // PLAYBACKSERVICE_H
