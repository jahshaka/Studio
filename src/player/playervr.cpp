/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "player/playervr.h"

#include "bridge/vrnames.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "player/playermousecontroller.h"
#include "services/vrworld.h"

using namespace jahshaka::engine;

namespace {

/// The engine's rotation type as the document's. Same four numbers, different
/// order (iris::Quat is Qt's: scalar first).
inline iris::Quat toIris(const Quat &q) { return iris::Quat(q.w, q.x, q.y, q.z); }
inline iris::Vec3 toIris(const Vec3 &v) { return iris::Vec3(v.x, v.y, v.z); }
inline Vec3 toEngine(const iris::Vec3 &v) { return Vec3(v.x(), v.y(), v.z()); }

QVariantMap vec(const iris::Vec3 &v)
{
    return QVariantMap{ { QStringLiteral("x"), double(v.x()) },
                        { QStringLiteral("y"), double(v.y()) },
                        { QStringLiteral("z"), double(v.z()) } };
}

}   // namespace

PlayerVr::PlayerVr(const std::shared_ptr<Engine> &engine) : mEngine(engine) {}

PlayerVr::~PlayerVr() { end(); }

bool PlayerVr::isActive() const
{
    auto engine = mEngine.lock();
    return mOwnsSession && engine && engine->vrStatus().active;
}

bool PlayerVr::begin(Scene *scene, View *mirrorView, const iris::CameraNodePtr &camera,
                     const iris::ScenePtr &document, const QVariantMap &options, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error) *error = why;
        return false;
    };
    auto engine = mEngine.lock();
    if (!engine) return fail(QStringLiteral("no engine is running in this process"));
    if (!engine->vrAvailable())
        return fail(QStringLiteral("VR is not available (%1)")
                        .arg(QString::fromStdString(engine->vrInfo().reason)));
    if (engine->vrStatus().active)
        return fail(QStringLiteral("a VR session is already running"));
    if (!scene) return fail(QStringLiteral("the player has no scene to show yet"));

    VrConfig cfg;
    if (options.contains(QStringLiteral("worldScale"))) {
        const double s = options.value(QStringLiteral("worldScale")).toDouble();
        if (s > 0.0) cfg.worldScale = float(s);
    }
    cfg.overrideEyeWidth  = options.value(QStringLiteral("eyeWidth"), 0).toUInt();
    cfg.overrideEyeHeight = options.value(QStringLiteral("eyeHeight"), 0).toUInt();
    if (!cfg.overrideEyeWidth != !cfg.overrideEyeHeight)
        return fail(QStringLiteral("eyeWidth and eyeHeight are set together or not at all"));
    // THE REFLECTION ROW IS THE PROJECT'S (lane REFLECT-VR-1), the same row the
    // flat Player renders with: the World panel's SSR row, passed here because
    // the session builds its own View and no mirror reaches it. In the headset
    // the row buys RAY-TRACED reflections rather than the screen-space march,
    // which a stereo target cannot carry — see PostFxDesc::ssrScreenMarch.
    if (document) cfg.ssr = document->ssrMode;
    if (options.contains(QStringLiteral("reflections")))
        cfg.ssr = qBound(0, options.value(QStringLiteral("reflections")).toInt(), 2);

    // THE MIRROR IS NAMED BEFORE THE SESSION EXISTS (Engine::setVrMirrorView
    // keeps the wish and applies it on begin), so the very first frame the
    // runtime accepts already reaches the desktop.
    engine->setVrMirrorView(mirrorView);
    if (!engine->beginVrSession(scene, cfg)) {
        engine->setVrMirrorView(nullptr);
        return fail(QString::fromStdString(engine->lastError()));
    }
    mOwnsSession = true;
    mMirrorView = mirrorView;
    mMirrorViewOff = false;
    // THE DESKTOP STOPS RENDERING THE WORLD (VR-2's F7) — BUT NOT YET
    // (lane VR-3b, 2026-09-17, the owner's WiVRn smoke). The View keeps its
    // workspace, its chain and its camera and simply stops executing, so the
    // window costs the mirror's one quad instead of a second render of the
    // scene at window size. That trade only exists once there IS a mirror
    // picture: a runtime answers "no picture" for its first frames (WiVRn does,
    // for as long as it takes to synchronise), and switching the desktop off
    // before the first eye was drawn leaves the window painted by nobody —
    // stale VRAM, which is what the owner photographed. So the switch-off waits
    // for `status().rendered` to move (step(), below), which is the same moment
    // the engine's mirror starts painting.

    // THE RIG STARTS AT THE WORLD ORIGIN AND IS PLACED AT THE FIRST LOCATED
    // POSE — never on the frame a session begins (the runtime has not reached
    // Focused yet, and a pose before that is not one to stand on).
    //
    // WHERE IT IS PLACED IS CAPTURED NOW, not read at placement time: "where
    // the run began" is a pose from BEFORE any physics, any animation and any
    // camera cut had a frame to move it, and it is the same anchor
    // `recenter()` uses. WORLD space, because that is the frame the engine
    // composes the rig in — a camera parented to anything (a socket, a rig, a
    // moving platform) has a local transform that means nothing here.
    // The rig starts where a fresh session's does — at the world origin, facing
    // -Z — and is pushed once so the engine and this object cannot disagree
    // about it before the first frame.
    applyRig(vrorigin::Rig());
    armPlacement(VrStatus());
    mCamera = camera;
    if (camera) {
        mStartPos = camera->getGlobalPosition();
        mStartRot = camera->getGlobalRotation();
    }
    // THE SESSION ADOPTS THE PROJECT'S VR SETTINGS (lane VR-WORLD-1), exactly
    // as the editor's preview does: the fly speed and the rest are document
    // fields (`world.vr`), latched here for the life of the session and
    // overridable for it alone by `vr.locomotion`.
    mDocument = document;
    vrworld::adopt(document);
    return true;
}

void PlayerVr::end()
{
    auto engine = mEngine.lock();
    if (engine && mOwnsSession) {
        engine->setVrMirrorView(nullptr);
        if (engine->vrStatus().active) engine->endVrSession();
    }
    mOwnsSession = false;
    mPlacePending = false;
    restoreMirrorView();
    mCamera.clear();
    mDocument.clear();
    // The session's locomotion overrides go with the session (VR-WORLD-1).
    vrworld::release();
}

void PlayerVr::restoreMirrorView()
{
    // NOT `setEnabled(true)` (lead review F4). This object switched the view
    // off; what it must be switched back TO is whatever its owner is showing by
    // then, and a session can end while the Player page is hidden — leaving a
    // hidden on-screen view rendering and presenting a picture nobody can see,
    // every frame, for the rest of the run. The widget owns that answer.
    if (mMirrorView && mMirrorViewOff && mRestoreView) mRestoreView();
    mMirrorViewOff = false;
    mMirrorView = nullptr;
}

void PlayerVr::armPlacement(const jahshaka::engine::VrStatus &st)
{
    mPlacePending = true;
    // The next locate is the first one that can use the rig this object holds
    // (the host pushes before the frame, the engine composes inside it), so it
    // is the first whose head can be paired with that rig.
    mPlaceAfterRendered = st.rendered + 1ull;
}

vrorigin::Rig PlayerVr::rigOf(const VrStatus &st)
{
    // THE ENGINE'S ORIGIN IS THE ONE TRUTH (see the class header). Not a cached
    // copy, and not lagged the way the head is: `origin` is what the engine
    // HOLDS at this moment — whatever the host last pushed, composed with any
    // runtime recentre the pump absorbed since.
    vrorigin::Rig rig;
    rig.position = toIris(st.origin);
    rig.yaw = st.originYaw;
    return rig;
}

void PlayerVr::applyRig(const vrorigin::Rig &rig)
{
    if (auto engine = mEngine.lock()) engine->setVrOrigin(toEngine(rig.position), rig.yaw);
}

void PlayerVr::step(float dt, const iris::CameraNodePtr &camera)
{
    auto engine = mEngine.lock();
    if (!engine || !mOwnsSession) return;
    const VrStatus st = engine->vrStatus();
    if (!st.active) {
        // ENDED FROM SOMEWHERE ELSE — `vr.end()` from a script, a device lost
        // inside the session, the engine tearing a Lost session down. The
        // session is gone; this object's half (the mirror, the switched-off
        // View) has to go back or the Player page stays blank for ever.
        mOwnsSession = false;
        mPlacePending = false;
        engine->setVrMirrorView(nullptr);
        restoreMirrorView();
        return;
    }
    // THE FIRST EYE PICTURE IS WHAT PAYS FOR THE DESKTOP'S (see begin()): from
    // here on the window shows the mirror, so the view that was drawing the
    // world at window size can stop. Before it, the desktop keeps its own
    // picture — a runtime that answers "no picture" for a hundred frames must
    // not leave the user looking at an unpainted window.
    if (!mMirrorViewOff && mMirrorView && st.rendered > 0ull) {
        mMirrorView->setEnabled(false);
        mMirrorViewOff = true;
    }
    if (camera) mCamera = camera;
    if (!st.posesValid) return;      // nothing located yet: nowhere to stand

    const iris::Vec3 head = toIris(st.headPosition);
    const iris::Quat headRot = toIris(st.headRotation);

    if (mPlacePending) {
        // NOT YET: this head was composed with a rig that is no longer the one
        // held here (see mPlaceAfterRendered). Correcting from a mismatched
        // pair is a teleport, not a placement.
        if (st.rendered < mPlaceAfterRendered) return;
        // WHERE THE PLAYER'S CAMERA STOOD IS WHERE THE HEAD IS. Position and
        // heading; the wearer keeps their own pitch, their own roll and their
        // own offset from the middle of their room.
        applyRig(vrorigin::placedOn(rigOf(st), head, headRot, mStartPos, mStartRot));
        mPlacePending = false;
        // NO CAMERA WRITE ON THIS FRAME, deliberately: the rig has just been
        // chosen so that the head lands on the camera, and the pose above is
        // the one measured BEFORE it. Writing it would move the camera to where
        // the wearer was standing a frame ago, which is the jump the placement
        // exists to avoid. The next frame's pose carries the rig.
        return;
    }

    // NOT WHILE A PLACEMENT IS PENDING — a wearer being put back where the run
    // began is not also walking, and a rig moved between the request and the
    // placement is the mismatched pair again.
    //
    // ONE FRAME OF FLIGHT, from the keys the player's controller is already
    // reading. The controller has ALSO flown the camera with them a moment ago
    // (PlayerMouseController::update); that move is discarded by the camera
    // write below, which is what makes the rig the one thing that moves the
    // wearer. Two things must not both move them, and the rig is the one that
    // keeps their feet on the room's floor.
    const iris::Vec3 delta = vrorigin::flyDelta(headRot, PlayerMouseController::heldFlyKeys(),
                                                wearerSpeed(), vrorigin::frameSeconds(dt));
    if (!delta.isNull()) {
        vrorigin::Rig rig = rigOf(st);      // whatever the ENGINE holds, absorb included
        rig.position += delta;
        applyRig(rig);
    }

    // THE DOCUMENT'S CAMERA IS WHERE THE WEARER IS. It is what player.
    // screenshot photographs, what a script reads, and where the desktop
    // Player picks up when the session ends — so the answer to "where am I"
    // is the same one in every space.
    //
    // ONE FRAME BEHIND THE RIG, and that is by construction: `st` was located
    // before this frame's fly moved the rig, and the engine composes the new
    // rig on the next locate. The HEADSET is exact (the composition happens
    // inside the pump); only the desktop's idea of the camera lags, by one
    // frame, which is the same lag every host-side camera read has.
    //
    // A WORLD WRITE, through the one setter that takes both (one parent
    // resolution instead of two, and none at all at the root): the head's pose
    // is in world space and the camera may be parented to anything. An ordinary
    // document write, like the camera controller's own per-frame one — no undo
    // entry, no new cadence — and PlayBack's stop puts the pre-play transforms
    // back at the end of the run, which is what makes it safe to write an
    // AUTHORED camera here: while a session runs the wearer IS the camera the
    // Player renders through, whichever camera that is.
    if (mCamera) {
        mCamera->setGlobalPosRot(head, headRot);
        mCamera->update(0);
    }
}

bool PlayerVr::move(const flystep::Keys &keys, float seconds)
{
    if (!isActive()) return false;
    // A PLACEMENT IN FLIGHT WINS, and it wins here as well as in step(): a
    // wearer being put back where VR began is not also walking, and a rig moved
    // between the request and the placement is the mismatched pair the
    // placement's whole guard exists to avoid — which a `vr.move` arriving in
    // that gap would rebuild, from outside the frame loop, where no guard can
    // see it. Not a refusal: the move is ANSWERED, and what answers it is the
    // teleport the caller asked for a moment earlier.
    if (mPlacePending) return true;
    auto engine = mEngine.lock();
    const VrStatus st = engine->vrStatus();
    const iris::Vec3 delta =
        vrorigin::flyDelta(toIris(st.headRotation), keys, wearerSpeed(), seconds);
    if (delta.isNull()) return true;    // nothing held is not a failure
    vrorigin::Rig rig = rigOf(st);      // whatever the ENGINE holds, absorb included
    rig.position += delta;
    applyRig(rig);
    return true;
}

bool PlayerVr::recenter()
{
    if (!isActive()) return false;
    auto engine = mEngine.lock();
    // Deferred to the next located frame that can be PAIRED with the rig this
    // object holds, exactly like the first placement (armPlacement's note). The
    // target is the session's start pose either way, so the two are one
    // operation.
    armPlacement(engine->vrStatus());
    return true;
}

QVariantMap PlayerVr::idleReport()
{
    QVariantMap out;
    out[QStringLiteral("active")] = false;
    out[QStringLiteral("state")] = vrnames::state(jahshaka::engine::VrState::Unavailable);
    out[QStringLiteral("mirror")] = vrnames::mirror(jahshaka::engine::VrMirrorMode::None);
    out[QStringLiteral("frames")] = QVariant::fromValue(qulonglong(0));
    out[QStringLiteral("rendered")] = QVariant::fromValue(qulonglong(0));
    out[QStringLiteral("posesValid")] = false;
    out[QStringLiteral("spaceChanges")] = QVariant::fromValue(qulonglong(0));
    out[QStringLiteral("worldScale")] = 1.0;
    QVariantMap zero = vec(iris::Vec3());
    zero[QStringLiteral("yaw")] = 0.0;
    out[QStringLiteral("origin")] = zero;
    // The same shape as the live report (vrnames::pose) — one head, one spelling.
    out[QStringLiteral("head")] = vrnames::pose(VrPose{});
    // NO SESSION, so no project is latched: the documented default, which is
    // what "a player that has never been asked for a headset" can honestly say.
    out[QStringLiteral("flySpeed")] = double(vrworld::resolve(iris::ScenePtr()).flySpeed);
    return out;
}

/// THE WEARER'S SPEED IS THE PROJECT'S (lane VR-WORLD-1): `world.vr`'s
/// `flySpeed` in metres per second — latched when this session began — with the
/// session's `vr.locomotion` override if it has one. The same number the
/// editor's VR preview and the thumbstick fly at, because a wearer has ONE
/// speed in a world however they got into it. It was the desktop Player's
/// 25 u/s camera speed until VR had a setting of its own.
float PlayerVr::wearerSpeed() const
{
    return vrworld::resolve(mDocument.lock()).flySpeed;
}

QVariantMap PlayerVr::report() const
{
    auto engine = mEngine.lock();
    const VrStatus st = engine ? engine->vrStatus() : VrStatus();
    QVariantMap out;
    out[QStringLiteral("active")] = isActive();
    out[QStringLiteral("state")] = vrnames::state(st.state);
    // WHICH VIEW THE HEADSET IS MIRRORED ONTO, by name — "player" is the whole
    // point of phase 3 (the mirror used to be the editor's viewport), so the
    // suite can assert it and a reader can see it.
    out[QStringLiteral("mirrorView")] =
        mMirrorView ? QString::fromStdString(mMirrorView->name()) : QString();
    out[QStringLiteral("mirror")] = vrnames::mirror(st.mirror);
    out[QStringLiteral("frames")] = QVariant::fromValue(qulonglong(st.frames));
    out[QStringLiteral("rendered")] = QVariant::fromValue(qulonglong(st.rendered));
    out[QStringLiteral("posesValid")] = st.posesValid;
    // WAITING TO PUT THE WEARER SOMEWHERE (see placing()). Reported because the
    // thumbstick's locomotion — which lives above this object, in
    // VrInteraction — has to refuse over exactly these frames, and because a
    // `placing` that never clears is what a session with no locate looks like
    // from the outside. The editor's preview reports the same field as
    // `vr.state().preview.placing`.
    out[QStringLiteral("placing")] = mPlacePending;
    // HOW OFTEN THE RUNTIME RECENTRED THE ROOM under this session, absorbed
    // into the rig so the wearer stayed put. Reported because a wearer who did
    // not press anything and sees this climbing is looking at a runtime
    // problem, and because the absorb is otherwise invisible by design.
    out[QStringLiteral("spaceChanges")] = QVariant::fromValue(qulonglong(st.spaceChanges));
    out[QStringLiteral("worldScale")] = double(st.worldScale);
    // THE RIG AS THE ENGINE HOLDS IT, not as this object remembers it: the
    // point of reporting it is to show that the two agree.
    QVariantMap origin = vec(toIris(st.origin));
    origin[QStringLiteral("yaw")] = double(st.originYaw);
    out[QStringLiteral("origin")] = origin;
    // THE POSE SPELLING, NOT A SECOND ONE (VR-4-FIX finding 10): vrnames::pose
    // is what `vr.state().head` and `vr.state().hands` answer with, and this
    // map used to be a hand-built {x,y,z,yaw} beside it — so a caller that
    // learned the shape from one verb found a different shape here, which is
    // exactly what that helper's header says cannot happen. Now it cannot: the
    // rotation and the located flag come with it.
    out[QStringLiteral("head")] =
        vrnames::pose(st.headPosition, st.headRotation, st.posesValid);
    out[QStringLiteral("flySpeed")] = double(wearerSpeed());
    return out;
}
