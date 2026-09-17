/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PLAYERVR_H
#define PLAYERVR_H

// PlayerVr — THE PLAYER'S VR MODE (SPECS/VR_SPEC.md §4.5, phase 3).
//
// Phase 2 gave the engine a VR session and five verbs to drive it, and the
// session mirrored onto whatever View the caller named. This is the product
// shape of it: the PLAYER runs in the headset. Press the icon and the Player
// page comes up, the scene starts, the wearer is standing where the play camera
// stood, the desktop shows what the left eye sees, and the fly keys walk them
// through the world in the direction they are LOOKING.
//
// THREE THINGS IT OWNS, and nothing else:
//
//   1. THE SESSION'S LIFETIME inside a play run. begin() starts it on the ONE
//      scene (the editor's — the Player is a second View on it since PLAYER-1),
//      end() stops it, and step() RECONCILES every frame with what the engine
//      says: a session ended from anywhere else (`vr.end()`, a lost device, the
//      shell shutting down) is noticed and this object's half is put back. That
//      reconciliation is the same lesson as the driver's (VR-2's F5): the host
//      is not the only thing that can end a session.
//
//   2. THE RIG — where the wearer's room stands in the world. Placed onto the
//      play camera on the first frame the runtime reports a pose, then moved by
//      the fly keys along the HEAD's level heading, and pushed to the engine
//      through Engine::setVrOrigin. The arithmetic is in vrorigin.h, with no
//      engine in it, so it is asserted without a headset.
//
//   3. THE MIRROR. The Player's on-screen View becomes the mirror target and is
//      SWITCHED OFF while the session runs: the desktop then costs one quad
//      that copies the left eye, instead of a second full render of the world
//      at window size, and the window still presents because the mirror is its
//      own workspace over the same target (see VrSession::syncMirror).
//
// WHAT IT DOES NOT OWN: the runtime, the stereo render, the pacing, the eye
// copies (the engine's session), and the input bindings (phase 4 — there are no
// controllers and no hands here, by the brief).

#include <memory>

#include <QString>
#include <QVariantMap>

#include "irisgl/irisglfwd.h"
#include "jahshaka/engine/Engine.h"
#include "player/vrorigin.h"
#include "viewport/flystep.h"

class PlayerVr
{
public:
    /// Holds the engine WEAKLY, like everything else in the player: it never
    /// keeps the Engine alive and every call re-checks that it is still there.
    explicit PlayerVr(const std::shared_ptr<jahshaka::engine::Engine> &engine);
    ~PlayerVr();

    /// Starts the session on `scene`, mirrors it onto `mirrorView` and arms the
    /// rig to be placed on `camera` at the first located pose. False with
    /// `error` filled — no runtime, a session already running, no scene, no
    /// view — and NOTHING changed.
    bool begin(jahshaka::engine::Scene *scene, jahshaka::engine::View *mirrorView,
               const iris::CameraNodePtr &camera, const QVariantMap &options, QString *error);

    /// Ends the session and puts the mirror view back on the screen. Safe when
    /// none is running (and called by the destructor).
    void end();

    /// Is the ENGINE running the session this object started?
    bool isActive() const;

    /// ONE FRAME, after PlayBack has moved the document and before the mirror
    /// pushes it: reconcile with the engine, place or fly the rig, push it, and
    /// put the head's world pose on the play camera so the document agrees with
    /// the wearer. `dt` is the seconds the frame is charging.
    void step(float dt, const iris::CameraNodePtr &camera);

    /// LOCOMOTION AS A VERB (player.vrMove). The same call the held fly keys
    /// make, for `seconds` at the Player's fly speed — which is what lets a
    /// script, an MCP session and the suite walk the wearer through a world
    /// with no keyboard in the room. False when no session is running.
    bool move(const flystep::Keys &keys, float seconds);

    /// Re-place the rig on the play camera, as the first frame did: "I am here,
    /// facing this way". False when no session is running.
    bool recenter(const iris::CameraNodePtr &camera);

    /// What player.state().vr answers with.
    QVariantMap report() const;
    /// The same map for a player that has never been asked for a headset —
    /// every field at its zero. A state verb must answer on every box, and it
    /// must not CREATE a VR mode to say that there is none.
    static QVariantMap idleReport();

private:
    /// Pushes the rig to the engine. Cheap; the engine composes it next frame.
    void applyRig();
    /// Puts the mirror view back the way it was found.
    void restoreMirrorView();

    std::weak_ptr<jahshaka::engine::Engine> mEngine;
    /// The Player's on-screen View, borrowed. Null once it has been restored.
    jahshaka::engine::View *mMirrorView = nullptr;
    vrorigin::Rig mRig;
    /// Waiting for the first located pose to place the rig on the camera.
    bool mPlacePending = false;
    /// This object started the session that is running (so it is this object's
    /// to end, and its half to put back when it goes away).
    bool mOwnsSession = false;
    /// The camera the rig was last placed on / flown from, for the verbs that
    /// arrive between frames.
    iris::CameraNodePtr mCamera;
};

#endif   // PLAYERVR_H
