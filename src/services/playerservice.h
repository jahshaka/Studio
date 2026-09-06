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
// where it stands). This owns the other one: the PLAYER PAGE, a second engine
// Scene with its own mirror, its own camera and its own PlayBack. They are
// genuinely different things — editor.playing() and player.playing() can differ
// — and the verbs must not be able to confuse them.
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
    bool play();
    bool stop();
    bool restart();

    /// Offscreen readback of the PLAYER's scene through the player's camera.
    QImage screenshot(int width, int height, bool postFx);
    /// Deterministic stepping of the player (editor.frame's shape). False
    /// when the player has no on-screen view to render into yet.
    bool stepFrames(int n, float dt);

signals:
    void playingChanged(bool playing);

private:
    IPlayerHost *mHost = nullptr;
};

#endif // PLAYERSERVICE_H
