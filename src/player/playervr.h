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
//      THIS OBJECT HOLDS NO RIG THAT SURVIVES A FRAME, and that is a
//      correctness rule rather than a style: THE ENGINE'S ORIGIN IS THE ONE
//      TRUTH. The engine moves it by itself — a runtime recentre is absorbed
//      inside the pump (VrSession::pollEvents) so the wearer does not jump —
//      and a host that kept its own copy would overwrite that absorb with the
//      very next fly key, throwing the wearer back by exactly what the runtime
//      moved, and would then pair a stale rig with a head composed under the
//      new one. So every entry point below READS the rig from `vrStatus()`,
//      applies its delta and pushes the result.
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

#include <functional>
#include <memory>

#include <QString>
#include <QVariantMap>

#include "irisgl/irisglfwd.h"
#include "jahshaka/engine/Engine.h"
#include "services/vrorigin.h"
#include "viewport/flystep.h"

class PlayerVr
{
public:
    /// Holds the engine WEAKLY, like everything else in the player: it never
    /// keeps the Engine alive and every call re-checks that it is still there.
    explicit PlayerVr(const std::shared_ptr<jahshaka::engine::Engine> &engine);
    ~PlayerVr();

    /// Starts the session on `scene`, mirrors it onto `mirrorView` and arms the
    /// rig to be placed at the first located pose on WHERE THE RUN BEGAN — the
    /// world pose of `camera`, read now. False with `error` filled — no
    /// runtime, a session already running, no scene, no view — and NOTHING
    /// changed.
    ///
    /// `camera` is the camera the PLAYER RENDERS THROUGH, resolved by
    /// `iris::Scene::renderCamera` (the active-camera seam's own rule, lead
    /// review F1): with an authored shot armed, the wearer must stand where the
    /// Player's picture was, not where the free camera happens to be parked.
    /// `document` is the SCENE THE PROJECT AUTHORED — needed for exactly one
    /// thing here, and named rather than reached for: the reflection row the
    /// headset renders with is the project's own World-panel row (lane
    /// REFLECT-VR-1; @see VrConfig::ssr). Null is allowed and means the row's
    /// default, off.
    bool begin(jahshaka::engine::Scene *scene, jahshaka::engine::View *mirrorView,
               const iris::CameraNodePtr &camera, const iris::ScenePtr &document,
               const QVariantMap &options, QString *error);

    /// WHO PUTS THE MIRROR VIEW BACK when a session ends — including a session
    /// that ended somewhere else (a script, a lost device). This object
    /// switched the view off; it must not GUESS what to switch it back to,
    /// because by then the page may be hidden (a hidden view left rendering and
    /// presenting every frame is the defect this callback exists to avoid).
    /// The widget knows, so the widget says.
    void setViewRestore(const std::function<void()> &restore) { mRestoreView = restore; }

    /// Ends the session and puts the mirror view back on the screen. Safe when
    /// none is running (and called by the destructor).
    void end();

    /// Is the ENGINE running the session this object started?
    bool isActive() const;

    /// IS THE WEARER STILL BEING PUT SOMEWHERE? True between begin (or a
    /// recentre) and the first locate this object can PAIR with the rig the
    /// engine holds — the frames over which `move()` refuses, because a
    /// correction from a mismatched pair is a teleport (measured: a recentre
    /// after a 75 m fly threw the wearer 75 m past the target).
    ///
    /// Asked by the CONTROLLER's locomotion as well as by this object's own
    /// fly (VrInteraction::Deps::locomotionBlocked, through
    /// `player.state().vr.placing`): the thumbstick writes `setVrOrigin`
    /// itself, so it has to be told what the host is in the middle of.
    bool placing() const { return mPlacePending; }

    /// ONE FRAME, after PlayBack has moved the document and before the mirror
    /// pushes it: reconcile with the engine, place or fly the rig, push it, and
    /// put the head's world pose on the play camera so the document agrees with
    /// the wearer. `dt` is the seconds the frame is charging, and `camera` is
    /// the RESOLVED render camera for this frame (see begin()).
    void step(float dt, const iris::CameraNodePtr &camera);

    /// LOCOMOTION AS A VERB (`vr.move`, dispatched to this host; `player.vrMove` was deleted by VR-INPUT-1S). The same call the held fly keys
    /// make, for `seconds` at the Player's fly speed — which is what lets a
    /// script, an MCP session and the suite walk the wearer through a world
    /// with no keyboard in the room. False when no session is running.
    bool move(const flystep::Keys &keys, float seconds);

    /// TAKE ME BACK TO WHERE VR BEGAN — the product gesture, and the only one
    /// that means anything (lead review F2): the wearer's head is re-placed
    /// onto the pose the render camera had WHEN THE SESSION STARTED, keeping
    /// their own offset from the middle of their room. "Re-place onto the
    /// camera" would be a no-op, because the camera is written FROM the head
    /// every frame.
    ///
    /// WHERE VR BEGAN, not where the RUN began, and the difference is real on
    /// the toggle path: the toggle shows the Player page, which starts the
    /// scene, and only then begins the session — so a scene that moves its
    /// camera on the first tick has moved it before this anchor is taken. That
    /// is the product meaning of the gesture (put me back where I put the
    /// headset on), and it is what the name says. False when no session is
    /// running.
    bool recenter();

    /// What player.state().vr answers with.
    QVariantMap report() const;
    /// The same map for a player that has never been asked for a headset —
    /// every field at its zero. A state verb must answer on every box, and it
    /// must not CREATE a VR mode to say that there is none.
    static QVariantMap idleReport();

private:
    /// Arms a placement for the first locate that can be paired with the rig
    /// the ENGINE holds.
    void armPlacement(const jahshaka::engine::VrStatus &status);
    /// THE RIG, READ BACK FROM THE ENGINE — the only place a rig comes from.
    static vrorigin::Rig rigOf(const jahshaka::engine::VrStatus &status);
    /// Pushes a rig to the engine. Cheap; the engine composes it next frame.
    void applyRig(const vrorigin::Rig &rig);
    /// Puts the mirror view back the way it was found.
    void restoreMirrorView();
    /// THE WEARER'S FLY SPEED in metres per second — the PROJECT's `world.vr`
    /// setting (latched at begin) with this session's `vr.locomotion` override
    /// (VR-WORLD-1).
    float wearerSpeed() const;

    std::weak_ptr<jahshaka::engine::Engine> mEngine;
    /// The DOCUMENT this session was begun on — weak, because a project close
    /// destroys it under a session (the lifetime class VR-4-FIX found twice).
    /// Read for the project's VR settings; null between sessions.
    iris::SceneWPtr mDocument;
    std::function<void()> mRestoreView;
    /// The Player's on-screen View, borrowed. Null once it has been restored.
    jahshaka::engine::View *mMirrorView = nullptr;
    /// Waiting to place the rig (at begin, and at every recenter).
    bool mPlacePending = false;
    /// (WHEN the Player's View stops drawing is the ENGINE's answer since lane
    /// MIRROR-LIVE-1 — it is the only party that knows whether the runtime
    /// asked for a picture this frame. This object names the view and reads
    /// `VrStatus::mirrorShowing`; the `mMirrorViewOff` bookkeeping that used to
    /// live here is gone.)
    /// THE FIRST LOCATE THAT CAN BE TRUSTED FOR A PLACEMENT — `VrStatus::
    /// rendered` at the moment the placement was asked for, plus one.
    ///
    /// A placement is a correction computed from the PAIR (the rig, the head it
    /// composed), and the engine composes the head inside its own pump: a head
    /// reported before the frame that first used the current rig belongs to the
    /// PREVIOUS rig, and correcting with a mismatched pair moves the wearer by
    /// exactly the difference. Measured, and it is not subtle — a recentre
    /// after a 75 m fly threw the wearer 75 m past the target.
    ///
    /// PLUS ONE, AND FROM A VERB THAT IS ONE FRAME CONSERVATIVE. Inside the
    /// frame loop `rendered + 1` is exact: the host pushes before the frame and
    /// the engine composes inside it, so the next locate is the first that can
    /// be paired. A verb arriving BETWEEN frames pushes nothing (it only arms),
    /// so its `+1` waits for a locate that would already have been pairable —
    /// one frame of latency on a teleport nobody can perceive, in exchange for
    /// one rule instead of two.
    unsigned long long mPlaceAfterRendered = 0ull;
    /// This object started the session that is running (so it is this object's
    /// to end, and its half to put back when it goes away).
    bool mOwnsSession = false;
    /// The camera the head is written to, as resolved by the last frame — for
    /// the verbs that arrive between frames.
    iris::CameraNodePtr mCamera;
    /// WHERE VR BEGAN, in WORLD space: the render camera's pose at begin().
    /// The placement and every recentre land the head here.
    iris::Vec3 mStartPos;
    iris::Quat mStartRot;
};

#endif   // PLAYERVR_H
