/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/vr/editorvr.h"

#include <QWidget>

#include "bridge/vrnames.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "viewport/enginerenderdriver.h"
#include "viewport/flyspeedsettings.h"
#include "viewport/flystep.h"
#include "viewport/ieditorviewport.h"

using namespace jahshaka::engine;

namespace {

inline iris::Quat toIris(const Quat &q) { return iris::Quat(q.w, q.x, q.y, q.z); }
inline iris::Vec3 toIris(const Vec3 &v) { return iris::Vec3(v.x, v.y, v.z); }
inline Vec3 toEngine(const iris::Vec3 &v) { return Vec3(v.x(), v.y(), v.z()); }

QVariantMap vec(const iris::Vec3 &v)
{
    return QVariantMap{ { QStringLiteral("x"), double(v.x()) },
                        { QStringLiteral("y"), double(v.y()) },
                        { QStringLiteral("z"), double(v.z()) } };
}

/// THE EDITOR'S OWN FLY, READ BACK OUT OF THE VIEWPORT. The names are the ones
/// EngineSceneViewport::heldFlyKeys prints for exactly these six keys plus
/// Shift, and reading them through the interface is what keeps this object out
/// of the camera controller: the gesture, the key map and the arming (the right
/// mouse button) stay the editor's single definition, and this only changes
/// WHOM the result moves.
flystep::Keys keysFrom(const QStringList &held)
{
    flystep::Keys keys;
    for (const QString &k : held) {
        if (k == QLatin1String("Up"))            keys.forward = true;
        else if (k == QLatin1String("Down"))     keys.back = true;
        else if (k == QLatin1String("Left"))     keys.left = true;
        else if (k == QLatin1String("Right"))    keys.right = true;
        else if (k == QLatin1String("PageUp"))   keys.up = true;
        else if (k == QLatin1String("PageDown")) keys.down = true;
        else if (k == QLatin1String("Shift"))    keys.boost = true;
    }
    return keys;
}

/// THE WEARER'S SPEED IS THE EDITOR'S OWN — the same base and the same
/// multiplier the toolbar dropdown and the wheel set for the viewport's camera.
/// A wearer who has chosen a pace for flying their scene expects that pace with
/// the headset on, and the Player's VR mode takes the Player's for the same
/// reason.
float flySpeed() { return FlySpeedSettings::speed(FlySpeedSettings::Editor); }

}   // namespace

EditorVrPreview::~EditorVrPreview() { end(); }

bool EditorVrPreview::isActive() const
{
    const auto engine = mEngine.lock();
    return mOwnsSession && engine && engine->vrStatus().active;
}

bool EditorVrPreview::begin(const std::shared_ptr<Engine> &engine, IEditorViewport *viewport,
                            EngineRenderDriver *driver, const QVariantMap &options, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error) *error = why;
        return false;
    };
    if (!engine) return fail(QStringLiteral("no engine is running in this process"));
    if (!viewport) return fail(QStringLiteral("this session has no editor viewport"));
    if (!engine->vrAvailable())
        return fail(QStringLiteral("VR is not available (%1)")
                        .arg(QString::fromStdString(engine->vrInfo().reason)));
    if (engine->vrStatus().active)
        return fail(QStringLiteral("a session is already running"));
    Scene *scene = viewport->engineScene();
    if (!scene) return fail(QStringLiteral("there is no scene to show yet"));

    VrConfig cfg;
    // THE MIRROR IS OFF BY DEFAULT HERE, and that is the phase's design rather
    // than an omission (see the class header). The desktop viewport goes on
    // drawing the EDITOR's picture — with the wearer's proxies in it — so
    // painting the headset's left eye over the top would pay for two renders
    // and show one, and would hide the very markers this phase adds. A caller
    // who wants the eye asks for it: `vr.begin({mirror:"left"})`.
    cfg.mirror = vrnames::mirrorFrom(options.value(QStringLiteral("mirror")).toString(),
                                     VrMirrorMode::None);
    if (options.contains(QStringLiteral("worldScale"))) {
        const double s = options.value(QStringLiteral("worldScale")).toDouble();
        if (s > 0.0) cfg.worldScale = float(s);
    }
    cfg.overrideEyeWidth  = options.value(QStringLiteral("eyeWidth"), 0).toUInt();
    cfg.overrideEyeHeight = options.value(QStringLiteral("eyeHeight"), 0).toUInt();
    if (!cfg.overrideEyeWidth != !cfg.overrideEyeHeight)
        return fail(QStringLiteral("eyeWidth and eyeHeight are set together or not at all"));
    // THE WEARER SEES THE EDITOR WORKING (owner, 2026-09-17). This is the one
    // caller that asks for the editor's furniture in the headset — the grid,
    // the light and camera icons, the selection outline, the gizmo — because
    // that is what an editor PREVIEW is for. The Player passes nothing and gets
    // VrConfig's default (none), exactly as its desktop window shows none.
    cfg.helpers = true;
    // AND THE WEARER SEES THE REFLECTIONS THE AUTHOR SEES (lane REFLECT-VR-1).
    // The reflection row is the PROJECT's — the World panel's SSR row, which
    // the mirror pushes into this viewport's own view every frame — and this
    // config is the only channel to a view the session makes for itself. A
    // project at Low or Medium (row 0) asks for none and pays for none, exactly
    // as its desktop viewport does.
    //
    // WHAT THE HEADSET DOES WITH IT is not the desktop's screen-space march
    // (impossible in a stereo target) but the RAYS, per eye — see
    // PostFxDesc::ssrScreenMarch. `vr.begin({reflections:n})` overrides the row
    // for a measurement; it is deliberately not a persisted setting.
    if (const iris::ScenePtr doc = viewport->getScene())
        cfg.ssr = doc->ssrMode;
    if (options.contains(QStringLiteral("reflections")))
        cfg.ssr = qBound(0, options.value(QStringLiteral("reflections")).toInt(), 2);
    // THE MIRROR VIEW IS NAMED BEFORE THE SESSION EXISTS (the engine keeps the
    // wish and applies it on begin), and only when one was ASKED for: with
    // `mirror: "none"` there is nothing to paint and the editor's view is left
    // entirely alone.
    if (cfg.mirror != VrMirrorMode::None) {
        std::vector<View *> views;
        engine->listViews(views);
        for (View *v : views)
            if (v && !v->isOffscreen() && v->scene() == scene) { engine->setVrMirrorView(v); break; }
    }
    if (!engine->beginVrSession(scene, cfg)) {
        engine->setVrMirrorView(nullptr);
        return fail(QString::fromStdString(engine->lastError()));
    }
    mEngine = engine;
    mViewport = viewport;
    mViewportAlive = static_cast<QObject *>(viewport->asWidget());
    mViewportIsWidget = mViewportAlive != nullptr;
    mDriver = driver;
    mOwnsSession = true;

    // THE ONE CAMERA RULE (iris::Scene::renderCamera): the rig is placed on the
    // camera a render of this scene ACTUALLY looks through, which in the editor
    // is the viewport's own free camera unless an avatar is possessed. Asking
    // the document rather than assuming is the whole point of that function
    // existing — two copies of a three-term rule is how a wearer ends up
    // standing somewhere the picture never was.
    const iris::CameraNodePtr hostCam = viewport->editorCamera();
    iris::CameraNodePtr cam = hostCam;
    if (hostCam) {
        const iris::ScenePtr doc = hostCam->getScene();
        if (doc) cam = doc->renderCamera(hostCam);
        if (!cam) cam = hostCam;
    }
    if (cam) {
        // WORLD SPACE, captured NOW: "where the editor was looking when the
        // headset went on" is a pose from before any frame of the session, and
        // it is the anchor a later recentre returns to.
        mStartPos = cam->getGlobalPosition();
        mStartRot = cam->getGlobalRotation();
    }
    // The rig starts where a fresh session's does — the world origin, facing -Z
    // — and is pushed once so this object and the engine cannot disagree about
    // it before the first frame.
    applyRig(vrorigin::Rig());
    armPlacement(VrStatus());

    // THE LOOP'S CLOCK IS THE RUNTIME NOW: zero interval, vsync off, and
    // renderOneFrame blocks in xrWaitFrame instead. Restored by end().
    if (mDriver) mDriver->setVrSessionActive(true);
    // ...AND THE VIEWPORT'S FLY KEYS ARE THE WEARER'S: the step below runs in
    // the camera controller's place, once per synced frame. Put back by
    // release().
    mViewport->setVrPreviewStep([this] { step(); });
    // ...AND THE HEADSET COMES OFF WITH THE WORLD (VR-4-FIX finding 1). The
    // session renders the viewport's ENGINE SCENE and holds a raw pointer to
    // it; a project close or an open-in-place destroys that scene. This is the
    // viewport telling us first, while everything is still alive.
    mViewport->setVrPreviewSceneClosing([this] { end(); });
    mFrameTimer.start();
    return true;
}

bool EditorVrPreview::end()
{
    if (!mOwnsSession) return false;
    const auto engine = mEngine.lock();
    mOwnsSession = false;
    mPlacePending = false;
    if (engine) {
        engine->setVrMirrorView(nullptr);
        if (engine->vrStatus().active) engine->endVrSession();
    }
    release();
    return true;
}

void EditorVrPreview::release()
{
    if (mDriver) mDriver->setVrSessionActive(false);
    // NOT IF THE VIEWPORT HAS ALREADY GONE (see mViewportAlive): this runs from
    // a destructor at shutdown as well as from vr.end(), and a widget torn down
    // by the shell takes the callback with it — there is nothing left to clear.
    if (mViewport && (!mViewportIsWidget || mViewportAlive)) {
        mViewport->setVrPreviewStep(nullptr);
        mViewport->setVrPreviewSceneClosing(nullptr);
    }
    mDriver = nullptr;
    mViewport = nullptr;
    mViewportAlive = nullptr;
    mEngine.reset();
}

void EditorVrPreview::applyRig(const vrorigin::Rig &rig)
{
    if (const auto engine = mEngine.lock()) engine->setVrOrigin(toEngine(rig.position), rig.yaw);
}

vrorigin::Rig EditorVrPreview::rigOf(const VrStatus &st)
{
    // THE ENGINE'S ORIGIN IS THE ONE TRUTH — not a cached copy: `origin` is
    // whatever the host last pushed COMPOSED with any runtime recentre the pump
    // absorbed since, and overwriting it from a host-side memory is how a
    // wearer who long-pressed their Quest ends up somewhere else.
    vrorigin::Rig rig;
    rig.position = toIris(st.origin);
    rig.yaw = st.originYaw;
    return rig;
}

void EditorVrPreview::armPlacement(const VrStatus &st)
{
    mPlacePending = true;
    mPlaceAfterRendered = st.rendered + 1ull;
}

void EditorVrPreview::step()
{
    const float wall = mFrameTimer.isValid()
                           ? float(double(mFrameTimer.nsecsElapsed()) * 1e-9) : 0.0f;
    mFrameTimer.restart();
    const auto engine = mEngine.lock();
    if (!mOwnsSession || !engine) return;
    const VrStatus st = engine->vrStatus();
    if (!st.active) {
        // ENDED FROM SOMEWHERE ELSE — a runtime that stopped the session, a
        // lost device, the engine tearing a Lost session down. The viewport's
        // fly keys and its pacing have to come back whoever ended it, or the
        // editor's camera stays dead for the rest of the run.
        mOwnsSession = false;
        mPlacePending = false;
        engine->setVrMirrorView(nullptr);
        release();
        return;
    }
    if (!st.posesValid) return;      // nothing located yet: nowhere to stand

    const iris::Quat headRot = toIris(st.headRotation);
    if (mPlacePending) {
        // NOT YET: this head was composed with a rig that is no longer the one
        // held here. Correcting from a mismatched pair is a teleport.
        if (st.rendered < mPlaceAfterRendered) return;
        applyRig(vrorigin::placedOn(rigOf(st), toIris(st.headPosition), headRot,
                                    mStartPos, mStartRot));
        mPlacePending = false;
        return;
    }

    // ONE FRAME OF FLIGHT FOR THE WEARER, from the keys the EDITOR's own fly is
    // holding — and only while that fly is armed (the right mouse button), so
    // the arrow keys mean what they have always meant everywhere else in the
    // editor. The camera controller's own move is suppressed for the duration
    // (EngineSceneViewport::syncFrame), which is what makes this the only thing
    // the gesture moves: two things must not both move the wearer, and the rig
    // is the one that keeps their feet on the room's floor.
    if (!mViewport || !mViewport->flying()) return;
    const flystep::Keys keys = keysFrom(mViewport->heldFlyKeys());
    const iris::Vec3 delta =
        vrorigin::flyDelta(headRot, keys, flySpeed(), vrorigin::frameSeconds(wall));
    if (delta.isNull()) return;
    vrorigin::Rig rig = rigOf(st);      // whatever the ENGINE holds, absorb included
    rig.position += delta;
    applyRig(rig);
}

// LOCOMOTION AS A VERB (vr.move), the same call the held fly keys make in
// step() — and the Player's own move in the other mode, both behind the one
// verb `vr.move` (the CRUD of `player.vrMove`). A script, an MCP
// session and the suite can walk the wearer with no keyboard in the room, which
// is also the only way to MEASURE a rig that a runtime alone never moves.
bool EditorVrPreview::move(const flystep::Keys &keys, float seconds)
{
    const auto engine = mEngine.lock();
    if (!mOwnsSession || !engine) return false;
    const VrStatus st = engine->vrStatus();
    if (!st.active) return false;
    // A PLACEMENT IN FLIGHT WINS (PlayerVr::move's rule, and for its reason): a
    // wearer being put where the editor camera stands is not also walking, and
    // a rig moved between the request and the placement is the mismatched pair
    // the placement's guard exists to avoid. Answered, not refused.
    if (mPlacePending) return true;
    const iris::Vec3 delta = vrorigin::flyDelta(toIris(st.headRotation), keys,
                                                flySpeed(), seconds);
    if (delta.isNull()) return true;    // nothing held is not a failure
    vrorigin::Rig rig = rigOf(st);      // whatever the ENGINE holds, absorb included
    rig.position += delta;
    applyRig(rig);
    return true;
}

QVariantMap EditorVrPreview::report() const
{
    QVariantMap out;
    out[QStringLiteral("active")] = isActive();
    // THROUGH THE SAME GUARD AS EVERY OTHER TOUCH OF THE VIEWPORT (VR-4-FIX
    // finding 7): `mViewport` is a raw pointer to a widget the shell may have
    // destroyed under a session that is still nominally running, and a state
    // verb is exactly the call somebody makes while a window is closing.
    // `mViewportAlive` is what tells "gone" from "never had a widget".
    const bool viewportUsable = mViewport && (!mViewportIsWidget || mViewportAlive);
    out[QStringLiteral("flyRedirected")] = viewportUsable ? mViewport->vrPreview() : false;
    // The editor preview is the one session that opens the ordinary helper
    // channel in the headset (VrConfig::helpers) — "see the editor working".
    out[QStringLiteral("helpers")] = mOwnsSession;
    QVariantMap start = vec(mStartPos);
    start[QStringLiteral("yaw")] = double(vrorigin::yawDegrees(mStartRot));
    out[QStringLiteral("startPose")] = start;
    out[QStringLiteral("placing")] = mPlacePending;
    out[QStringLiteral("flySpeed")] = double(flySpeed());
    return out;
}
