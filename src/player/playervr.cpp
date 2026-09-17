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
#include "player/playermousecontroller.h"
#include "viewport/flyspeedsettings.h"

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

/// THE WEARER'S SPEED IS THE PLAYER'S OWN (player.flySpeed().speed): 25 world
/// units per second at 1x, stepped by the same dropdown, the same wheel and the
/// same verb the desktop player uses. Nothing VR-specific — a world is a world,
/// and a wearer who has set their speed for the flat Player expects the same
/// pace with the headset on.
float flySpeed() { return FlySpeedSettings::speed(FlySpeedSettings::Player); }

}   // namespace

PlayerVr::PlayerVr(const std::shared_ptr<Engine> &engine) : mEngine(engine) {}

PlayerVr::~PlayerVr() { end(); }

bool PlayerVr::isActive() const
{
    auto engine = mEngine.lock();
    return mOwnsSession && engine && engine->vrStatus().active;
}

bool PlayerVr::begin(Scene *scene, View *mirrorView, const iris::CameraNodePtr &camera,
                     const QVariantMap &options, QString *error)
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
    // THE DESKTOP STOPS RENDERING THE WORLD (VR-2's F7, deferred to here). The
    // View keeps its workspace, its chain and its camera — it simply does not
    // execute, so the window costs the mirror's one quad instead of a second
    // render of the scene at window size. It comes straight back on when the
    // session ends, with the chain the mirror has kept pushing to it all along.
    if (mMirrorView) mMirrorView->setEnabled(false);

    // THE RIG STARTS AT THE WORLD ORIGIN AND IS PLACED ON THE CAMERA at the
    // first frame the runtime locates a pose — which is never the frame a
    // session begins on (the runtime has not reached Focused yet, and a pose
    // before that is not one to stand on).
    mRig = vrorigin::Rig();
    mPlacePending = true;
    mCamera = camera;
    applyRig();
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
}

void PlayerVr::restoreMirrorView()
{
    if (mMirrorView) mMirrorView->setEnabled(true);
    mMirrorView = nullptr;
}

void PlayerVr::applyRig()
{
    if (auto engine = mEngine.lock()) engine->setVrOrigin(toEngine(mRig.position), mRig.yaw);
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
    if (camera) mCamera = camera;
    if (!st.posesValid) return;      // nothing located yet: nowhere to stand

    const iris::Vec3 head = toIris(st.headPosition);
    const iris::Quat headRot = toIris(st.headRotation);

    if (mPlacePending) {
        // WHERE THE PLAYER'S CAMERA STANDS IS WHERE THE HEAD IS. Position and
        // heading; the wearer keeps their own pitch, their own roll and their
        // own offset from the middle of their room.
        if (mCamera) {
            mRig = vrorigin::placedOn(mRig, head, headRot,
                                      mCamera->getLocalPos(), mCamera->getLocalRot());
            applyRig();
        }
        mPlacePending = false;
        // NO CAMERA WRITE ON THIS FRAME, deliberately: the rig has just been
        // chosen so that the head lands on the camera, and the pose above is
        // the one measured BEFORE it. Writing it would move the camera to where
        // the wearer was standing a frame ago, which is the jump the placement
        // exists to avoid. The next frame's pose carries the rig.
        return;
    }

    // ONE FRAME OF FLIGHT, from the keys the player's controller is already
    // reading. The controller has ALSO flown the camera with them a moment ago
    // (PlayerMouseController::update); that move is discarded by the camera
    // write below, which is what makes the rig the one thing that moves the
    // wearer. Two things must not both move them, and the rig is the one that
    // keeps their feet on the room's floor.
    const iris::Vec3 delta = vrorigin::flyDelta(headRot, PlayerMouseController::heldFlyKeys(),
                                                flySpeed(), vrorigin::frameSeconds(dt));
    if (!delta.isNull()) {
        mRig.position += delta;
        applyRig();
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
    if (mCamera) {
        mCamera->setLocalPos(head);
        mCamera->setLocalRot(headRot);
        mCamera->update(0);
    }
}

bool PlayerVr::move(const flystep::Keys &keys, float seconds)
{
    if (!isActive()) return false;
    auto engine = mEngine.lock();
    const VrStatus st = engine->vrStatus();
    const iris::Vec3 delta =
        vrorigin::flyDelta(toIris(st.headRotation), keys, flySpeed(), seconds);
    if (delta.isNull()) return true;    // nothing held is not a failure
    mRig.position += delta;
    applyRig();
    return true;
}

bool PlayerVr::recenter(const iris::CameraNodePtr &camera)
{
    if (!isActive()) return false;
    if (camera) mCamera = camera;
    // Deferred to the next located frame, exactly like the first placement —
    // the pose this frame's `vrStatus` holds may be a frame old, and a recentre
    // that used it would leave the wearer that much off the mark.
    mPlacePending = true;
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
    out[QStringLiteral("worldScale")] = 1.0;
    QVariantMap zero = vec(iris::Vec3());
    zero[QStringLiteral("yaw")] = 0.0;
    out[QStringLiteral("origin")] = zero;
    out[QStringLiteral("head")] = zero;
    out[QStringLiteral("flySpeed")] = double(flySpeed());
    return out;
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
    out[QStringLiteral("worldScale")] = double(st.worldScale);
    // THE RIG AS THE ENGINE HOLDS IT, not as this object remembers it: the
    // point of reporting it is to show that the two agree.
    QVariantMap origin = vec(toIris(st.origin));
    origin[QStringLiteral("yaw")] = double(st.originYaw);
    out[QStringLiteral("origin")] = origin;
    QVariantMap head = vec(toIris(st.headPosition));
    head[QStringLiteral("yaw")] = double(vrorigin::yawDegrees(toIris(st.headRotation)));
    out[QStringLiteral("head")] = head;
    out[QStringLiteral("flySpeed")] = double(flySpeed());
    return out;
}
