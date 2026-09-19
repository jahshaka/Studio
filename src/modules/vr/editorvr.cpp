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
#include "services/vrworld.h"
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
    // THE SESSION DRAWS THE EDITOR'S SCENE, so it is entitled to ask for it to
    // exist — a `--vr` boot that has not opened the editor page yet has no
    // scene, and that is a page that has not been shown rather than a session
    // that cannot run (SMOKE-FIX-1's fix round). The refusal below stands for
    // the case the engine really cannot make one yet: before any View exists in
    // the process (the pin's startup-order law).
    viewport->ensureEngineScene();
    Scene *scene = viewport->engineScene();
    if (!scene) return fail(QStringLiteral("there is no scene to show yet"));

    VrConfig cfg;
    // THE MIRROR IS OFF BY DEFAULT HERE, and that is the phase's design rather
    // than an omission (see the class header). THE DESKTOP IS A COPY OF THE
    // LEFT EYE BY DEFAULT, exactly as in the Player (the owner, 2026-09-17, at
    // the controller smoke: "do the same for the editor — one render pipeline
    // is better for VR where 90 fps is the target; two renders offset
    // resources; a toggle for multi-user later"): the editor's own on-screen
    // View is switched OFF on the first drawn eye frame and the mirror quad
    // paints the eye, so the frame is the two eyes and a copy. The third
    // render — the desktop as an independent editor camera with the wearer's
    // markers in it, for a second person at the desk — is `mirror:"none"`.
    cfg.mirror = vrnames::mirrorFrom(options.value(QStringLiteral("mirror")).toString(),
                                     VrMirrorMode::Left);
    if (options.contains(QStringLiteral("worldScale"))) {
        const double s = options.value(QStringLiteral("worldScale")).toDouble();
        if (s > 0.0) cfg.worldScale = float(s);
    }
    cfg.overrideEyeWidth  = options.value(QStringLiteral("eyeWidth"), 0).toUInt();
    cfg.overrideEyeHeight = options.value(QStringLiteral("eyeHeight"), 0).toUInt();
    if (!cfg.overrideEyeWidth != !cfg.overrideEyeHeight)
        return fail(QStringLiteral("eyeWidth and eyeHeight are set together or not at all"));
    // THE STEREO WARM-UP, OVERRIDABLE FOR A MEASUREMENT (lane VR-WARMUP-1).
    // VrConfig's default (2 frames) is the product answer and no UI selects it;
    // `vr.begin({warmUp:0})` is the A/B arm that shows what it buys, and the
    // suite's own arm that proves the eye frames compile nothing only because
    // of it.
    if (options.contains(QStringLiteral("warmUp")))
        cfg.warmUpFrames = qMin(options.value(QStringLiteral("warmUp")).toUInt(), 8u);
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
    // THE HIDDEN-AREA MASK (lane HAM-1), ON unless a MEASUREMENT asks for the
    // other arm. It is not a user row and never will be: the pixels it removes
    // are the ones behind the lens barrel, so there is nothing to prefer — but
    // the saving cannot be measured without a control, and the control has to
    // be askable in the same process at the same pose (the rig's own rule).
    if (options.contains(QStringLiteral("hiddenAreaMask")))
        cfg.hiddenAreaMask = options.value(QStringLiteral("hiddenAreaMask")).toBool();
    // BARE HANDS ARE THE PROJECT'S CHOICE (lane HANDS-SWITCH-1; the owner,
    // 2026-09-18, joint) — the World panel's Hands switch, `world.vr({hands})`,
    // off by default. Read from the DOCUMENT here rather than from
    // `vrworld::resolve`, because the row is latched by the session at creation
    // and `vr.locomotion` cannot move it (`Row::sessionFixed`).
    //
    // `vr.begin({hands:true})` overrides it for ONE session — a measurement and
    // a suite's opt-in, like `hiddenAreaMask` above — and writes nothing to the
    // project. IT IS A SESSION OVERRIDE IN THE ORDINARY SENSE and is registered
    // as one BELOW, once the session exists (`vrworld::override`): the first cut
    // wrote only this config, so `vr.state().hands.enabled` said true while
    // `vr.locomotion()` — which resolves the row through the table — said false
    // and listed nothing as overridden. One truth, through the mechanism every
    // other row already uses, and `release()` drops it with the session.
    //
    // AND THE VALUE IS TYPE-CHECKED BY THE TABLE'S OWN RULE, never coerced: a
    // Flag takes true or false, so `hands:"off"` is refused rather than read as
    // true (which is exactly what `QVariant::toBool()` would have done).
    if (const iris::ScenePtr doc = viewport->getScene()) cfg.hands = doc->vrHands;
    bool handsAsked = false, handsWanted = false;
    if (options.contains(QStringLiteral("hands"))) {
        const vrworld::Row *row = vrworld::row(QStringLiteral("hands"));
        double value = 0.0;
        QString why;
        if (!row || !vrworld::validate(*row, options.value(QStringLiteral("hands")), value, why))
            return fail(why.isEmpty() ? QStringLiteral("hands must be true or false") : why);
        handsAsked = true;
        handsWanted = value != 0.0;
        cfg.hands = handsWanted;
    }
    // THE MIRROR VIEW IS NAMED BEFORE THE SESSION EXISTS (the engine keeps the
    // wish and applies it on begin), and only when one was ASKED for: with
    // `mirror: "none"` there is nothing to paint and the editor's view is left
    // entirely alone.
    if (cfg.mirror != VrMirrorMode::None) {
        std::vector<View *> views;
        engine->listViews(views);
        for (View *v : views)
            if (v && !v->isOffscreen() && v->scene() == scene) {
                engine->setVrMirrorView(v);
                mMirrorView = v;
                break;
            }
    }
    mMirrorViewOff = false;
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
    // THE SESSION ADOPTS THE PROJECT'S VR SETTINGS (lane VR-WORLD-1): the fly
    // speed, the fly direction, the turn and the dominant hand are document
    // fields (`world.vr`), latched here so nothing moves under the wearer
    // mid-flight, and overridable for this session alone by `vr.locomotion`.
    // A project switched between sessions is read by the next one.
    vrworld::adopt(viewport->getScene());
    // ...AND `vr.begin({hands:...})` IS ONE OF THIS SESSION'S OVERRIDES
    // (lane HANDS-SWITCH-1's fix round). Registered AFTER the adoption and
    // after the session really began, so a refused begin leaves nothing behind:
    // from here `vr.locomotion()` reports what the session is actually running
    // on, `overridden` names it, and `release()` drops it like any other.
    if (handsAsked) vrworld::override(QStringLiteral("hands"), handsWanted ? 1.0 : 0.0);
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
    // THE SESSION'S LOCOMOTION IS OVER: its overrides go with it and reads fall
    // back to the live document (lane VR-WORLD-1).
    vrworld::release();
    if (mDriver) mDriver->setVrSessionActive(false);
    // THE DESKTOP'S OWN VIEW COMES BACK if the session switched it off (the
    // Player's rule, mirrored here).
    if (mMirrorView && mMirrorViewOff) mMirrorView->setEnabled(true);
    mMirrorView = nullptr;
    mMirrorViewOff = false;
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

/// THE WEARER'S SPEED IS THE PROJECT'S (lane VR-WORLD-1) — `world.vr`'s
/// `flySpeed` in metres per second, with this session's `vr.locomotion`
/// override if it has one, so the keys and the thumbstick fly at exactly one
/// speed. It was the desktop editor's camera speed until VR had a setting of
/// its own.
float EditorVrPreview::wearerSpeed() const
{
    return vrworld::resolve(mViewport ? mViewport->getScene() : iris::ScenePtr()).flySpeed;
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
        // THE DESKTOP BECOMES THE COPY on the first drawn eye frame: until the
        // headset has drawn, the desktop keeps its own picture (VR-3b's rule —
        // never a stale eye), then the eye's mirror replaces the editor's
        // render and the editor View is switched off.
        if (mMirrorView && !mMirrorViewOff && st.rendered > 0ull) {
            mMirrorView->setEnabled(false);
            mMirrorViewOff = true;
        }
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
    const iris::Vec3 delta = vrorigin::flyDelta(headRot, keys, wearerSpeed(),
                                                vrorigin::frameSeconds(wall));
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
                                                wearerSpeed(), seconds);
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
    out[QStringLiteral("flySpeed")] = double(wearerSpeed());
    return out;
}
