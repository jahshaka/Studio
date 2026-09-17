/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PLAYERSERVICE_H
#define PLAYERSERVICE_H

// PlayerService — the Player space's state, as a service (verb-coverage audit
// F1).
//
// PlaybackService owns play-IN-PLACE (the editor viewport running the scene
// where it stands). This owns the other one: the PLAYER PAGE — a second VIEW on
// the editor's engine scene (lane PLAYER-1), with its own camera and its own
// PlayBack. They are genuinely different things — editor.playing() and
// player.playing() can differ — and the verbs must not be able to confuse them.
//
// The service is the seam the verbs call, exactly as editor.play calls
// PlaybackService: no ApiModule reaches for the widget. The widget is the other
// consumer — PlayerWidget listens on the signals so the play button's icon
// follows a scripted play, which is the same "the page is a VIEW over the
// verbs" rule the avatar and materials modules already follow.
//
// Nullable host: headless runs have no player backend at all, so every call is
// a clean no-op / false and the verbs report "not available in this session".

#include <QImage>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <functional>

#include "viewport/flystep.h"

class IPlayerHost;

class PlayerService : public QObject
{
    Q_OBJECT

public:
    explicit PlayerService(QObject *parent = nullptr) : QObject(parent) {}

    /// Wired by the shell once the player backend exists; null in headless
    /// runs (and in the document-only stand-in sessions).
    void setHost(IPlayerHost *host) { mHost = host; }
    bool isAvailable() const { return mHost != nullptr; }

    /// Is the player's scene RUNNING right now?
    bool isPlaying();
    /// Is the player page the visible space?
    bool isActive() const;

    /// Start / stop / restart the player's scene. Idempotent, and each emits
    /// playingChanged only when the state really moved — the widget's icon
    /// must not flicker on a redundant call.
    ///
    /// `vr` asks for the run to be IN THE HEADSET (SPECS/VR_SPEC.md §4.5): the
    /// scene starts AND a VR session begins on it, and a box with no runtime
    /// REFUSES — it does not silently play flat. A caller that wants "VR if
    /// there is any" asks vrAvailable() first; a caller that wants the flat
    /// player calls play() as it always did, which is untouched.
    bool play(bool vr = false);
    bool stop();
    bool restart();

    // ---- THE VR MODE (SPECS/VR_SPEC.md §4.5, phase 3) ---------------------
    /// Can this process do VR at all? Fixed at boot (the engine asks the
    /// runtime once, and only when the process was started with --vr), so the
    /// UI can enable or disable its icon and never has to re-ask.
    bool vrAvailable() const;
    /// Why not, in words, when it cannot — straight from the runtime or the
    /// loader, plus the sentence a user can act on when this run simply never
    /// asked for a headset.
    QString vrUnavailableReason() const;
    /// Is the player running in the headset right now?
    bool isVrActive() const;
    /// Ends the VR session and LEAVES THE SCENE PLAYING. The other order —
    /// stop() — ends both (see stop()).
    bool endVr();
    /// THE TOGGLE, and the one implementation of it: not in VR, show the Player
    /// page and run the scene in the headset; in VR, stop the run. Answers
    /// whether the player is in VR AFTER the call — a refusal (no runtime) is
    /// therefore false with lastError() filled, which is the same false a stop
    /// gives, and is why the ICON reads isVrActive() rather than this.
    bool toggleVr();
    /// Moves the wearer as the fly keys would, for `seconds`.
    bool moveVr(const flystep::Keys &keys, float seconds);
    /// "I am standing here, facing this way."
    bool recenterVr();
    /// What player.state().vr reports; answerable in every session.
    QVariantMap vrReport() const;
    /// What player.state().camera reports: where the player is looking from and
    /// through which camera (PLAYER-SPAWN-1). Empty in a session with no player
    /// backend, and before a document is open.
    QVariantMap cameraReport() const;
    /// The last refusal, for a caller that asked for VR and was told no.
    const QString &lastError() const { return mLastError; }

    /// SHOW THE PLAYER PAGE — the one thing the shell can do and this service
    /// cannot. Wired by MainWindow (switchSpace(PLAYER)); unset in headless
    /// sessions, where showSpace() answers false and changes nothing.
    ///
    /// It exists for the VR toggle, whose whole contract is "put me in the
    /// Player, in the headset" from a script, an MCP session or a button — and
    /// a module may not include mainwindow.h to do it.
    void setSpaceActivator(const std::function<void()> &show) { mShowSpace = show; }
    bool showSpace();

    /// Offscreen readback of the PLAYER's scene through the player's camera.
    /// `grade` is IEditorViewport::ScreenshotGrade as an int.
    QImage screenshot(int width, int height, int grade);
    /// Deterministic stepping of the player (editor.frame's shape). False
    /// when the player has no on-screen view to render into yet.
    bool stepFrames(int n, float dt);

signals:
    void playingChanged(bool playing);

private:
    IPlayerHost *mHost = nullptr;
    std::function<void()> mShowSpace;
    QString mLastError;
};

#endif // PLAYERSERVICE_H
