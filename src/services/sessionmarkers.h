/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SESSIONMARKERS_H
#define SESSIONMARKERS_H

// SessionMarkers — the event markers the shell owns (SESSION_LOG_SPEC §5).
//
// The rule for every marker in the spec is "emit where ALL paths converge".
// Three of them converge in a service and are emitted there (the scene-open
// block in LoadTimeline::end, the save block in ProjectService, the script
// record in ScriptEngine::evaluate). The rest converge in the shell, and this
// is where they live so MainWindow does not grow another three hundred lines.
//
// PLAY BRACKETS are the interesting one. They are driven by PlaybackService's
// EXISTING playModeEntered/editModeEntered signals — nothing new is called from
// the play path — and PLAY STOP carries a delta of app.frameStats across the
// bracket: rendered, skipped, workMs, worstMs, slowFrames. That is the
// fps-decay evidence in its most usable form, because it answers "was this
// play session worse than the last one" without anybody having to have been
// watching.

#include <QObject>
#include <QStringList>

#include "irisgl/irisglfwd.h"

class PlaybackService;

class SessionMarkers : public QObject
{
    Q_OBJECT
public:
    explicit SessionMarkers(QObject *parent = nullptr) : QObject(parent) {}

    /// Connects the play/edit brackets to the service's existing signals.
    void attach(PlaybackService *playback);

    /// One `ui`/Log line per space switch — cheap, and it is the context a
    /// later warning needs ("the crash was on the Materials page").
    static void logSpaceSwitch(const QString &from, const QString &to);

    /// The scene-stats lines appended to the SCENE OPEN block, and the same
    /// counts used by the quit summary. Safe with a null scene.
    static QStringList sceneStats(const iris::ScenePtr &scene);

    /// The close record: session duration and the run's totals, written just
    /// before JahLog's own close bracket.
    static void logQuitSummary();

private:
    void onPlayStart();
    void onPlayStop();

    qint64 mPlayStartMs = 0;
    bool mInPlay = false;
    // The frame-stats snapshot the bracket is measured against.
    quint64 mRendered = 0, mSkipped = 0, mSlow = 0;
    double mWorst = 0.0;
};

#endif   // SESSIONMARKERS_H
