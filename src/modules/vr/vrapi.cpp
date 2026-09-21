/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/vr/vrapi.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <cstring>

#include "bridge/enginehost.h"
#include "bridge/vrnames.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "scripting/modules/moduleshared.h"   // quatFromJs (the one pose reader)
#include "irisgl/mirror/scenemirror.h"
#include "services/vrworld.h"
#include "viewport/gizmo.h"
#include "viewport/gizmomode.h"
#include "viewport/flystep.h"
#include "services/playerservice.h"
#include "services/services.h"
#include "viewport/enginerenderdriver.h"
#include "viewport/ieditorviewport.h"
#include "viewport/scenepicker.h"

using namespace jahshaka::engine;

namespace {

/// A POSE OUT OF A SCRIPT'S MAP — THE ONE READER, for `vr.inject` and for every
/// pose any vr verb takes. Position as x/y/z (or a nested `position`), rotation
/// as a quaternion {x,y,z,w} or — far easier to write and to read — as `yaw`,
/// `pitch` and `roll` in DEGREES, in the document's own convention (yaw 0,
/// pitch 0 looks down -Z). `valid` defaults to true: a caller who describes a
/// pose means it.
///
/// AN UNKNOWN KEY IS AN ERROR, and a rotation that cannot be read is an error:
/// a pose silently taken as the origin, or as identity, is the "the saved
/// camera collapsed" defect (the unnamed-fourth-number rule — `quatFromJs`
/// enforces it, which is why the quaternion branch goes through that helper
/// rather than reading w with a default of 1).
bool poseFromMap(const QVariantMap &map, VrPose &out, QString *error)
{
    static const QStringList known = { "valid", "x",   "y",     "z",    "position",
                                        "rotation", "yaw", "pitch", "roll" };
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        if (!known.contains(it.key())) {
            if (error)
                *error = QStringLiteral("unknown pose key '%1' — known keys are %2")
                             .arg(it.key(), known.join(QStringLiteral(", ")));
            return false;
        }
    QVariantMap pos = map;
    if (map.contains(QStringLiteral("position")))
        pos = map.value(QStringLiteral("position")).toMap();
    out.position = Vec3(float(pos.value(QStringLiteral("x")).toDouble()),
                        float(pos.value(QStringLiteral("y")).toDouble()),
                        float(pos.value(QStringLiteral("z")).toDouble()));
    iris::Quat rot;
    if (map.contains(QStringLiteral("rotation"))) {
        bool ok = true;
        rot = scriptmod::quatFromJs(map.value(QStringLiteral("rotation")), iris::Quat(), &ok);
        if (!ok) {
            if (error)
                *error = QStringLiteral("rotation is neither a quaternion {x,y,z,w} nor "
                                        "Euler degrees {x,y,z}");
            return false;
        }
    } else {
        // YAW THEN PITCH THEN ROLL, applied in that order — the same product
        // the rig's own suite builds its head poses with
        // (`yaw(90) * pitch(-20)`), so a script and a test mean the same thing.
        const float yaw = float(map.value(QStringLiteral("yaw"), 0.0).toDouble());
        const float pitch = float(map.value(QStringLiteral("pitch"), 0.0).toDouble());
        const float roll = float(map.value(QStringLiteral("roll"), 0.0).toDouble());
        rot = iris::Quat::fromAxisAndAngle(iris::Vec3(0, 1, 0), yaw) *
              iris::Quat::fromAxisAndAngle(iris::Vec3(1, 0, 0), pitch) *
              iris::Quat::fromAxisAndAngle(iris::Vec3(0, 0, 1), roll);
    }
    rot = rot.normalized();
    out.rotation = Quat(rot.x(), rot.y(), rot.z(), rot.scalar());
    out.valid = map.value(QStringLiteral("valid"), true).toBool();
    return true;
}

/// A PROFILE NAME OUT OF A SCRIPT'S STRING (`vr.inject`'s `profile`, stage 3).
///
/// A full OpenXR path is taken as written; the four SHORT NAMES a test actually
/// types are spelled out here rather than in every script, because the thing
/// under test is "what does the editor do with a HAND", not whether somebody
/// can transcribe `/interaction_profiles/ext/hand_interaction_ext`.
///
/// An unknown name is an ERROR: a profile silently taken as "nothing bound"
/// would make a hands test quietly assert the controller behaviour.
bool profileFromName(const QString &raw, VrProfileName &out, QString *error)
{
    const QString name = raw.trimmed();
    if (name.isEmpty()) { out.clear(); return true; }
    if (name.startsWith(QLatin1String("/interaction_profiles/"))) {
        out.assign(name.toUtf8().constData());
        return true;
    }
    static const struct { const char *shortName, *path; } kNames[] = {
        { "hand_interaction", "/interaction_profiles/ext/hand_interaction_ext" },
        { "hands",            "/interaction_profiles/ext/hand_interaction_ext" },
        { "touch",            "/interaction_profiles/oculus/touch_controller" },
        { "simple",           "/interaction_profiles/khr/simple_controller" },
        { "wmr",              "/interaction_profiles/microsoft/motion_controller" },
    };
    for (const auto &n : kNames)
        if (name == QLatin1String(n.shortName)) { out.assign(n.path); return true; }
    if (error)
        *error = QStringLiteral("unknown profile '%1' — give a full "
                                "/interaction_profiles/... path, or one of "
                                "hand_interaction, hands, touch, simple, wmr").arg(raw);
    return false;
}

/// A VIEW'S EFFECTIVE POST DESCRIPTION, AS A MAP (lane EYE-GRADE-1).
///
/// WHAT IT IS FOR: "is the headset graded like the desktop?" is a question
/// about two DESCRIPTIONS, and pixels are a slow and noisy way to ask it. Both
/// views report theirs here — `View::postFx()` is what the view is really
/// carrying, the VR policy already applied — so the answer is an assertion on
/// the fields rather than on a picture, and a difference names ITSELF.
QVariantMap postFxMap(const View &view)
{
    const PostFxDesc &fx = view.postFx();
    const auto lookName = [](LookKind k) {
        switch (k) {
        case LookKind::Desaturate: return QStringLiteral("desaturate");
        case LookKind::GlassWarp:  return QStringLiteral("glassWarp");
        case LookKind::RadialBlur: return QStringLiteral("radialBlur");
        case LookKind::OldMovie:   return QStringLiteral("oldMovie");
        case LookKind::Posterize:  return QStringLiteral("posterize");
        case LookKind::Sharpen:    return QStringLiteral("sharpen");
        case LookKind::FilmGrade:  return QStringLiteral("filmGrade");
        case LookKind::Count:      break;
        }
        return QStringLiteral("unknown");
    };
    QVariantMap m;
    m[QStringLiteral("hdr")] = fx.hdr;
    // THE CHAIN'S OWN AXIS (natural log), not stops: this is the renderer's
    // value, and comparing two views is exactly what it is here for. The
    // document's unit is `world.postFx({exposureEv})`.
    m[QStringLiteral("exposure")] = fx.exposure;
    m[QStringLiteral("exposureMin")] = fx.exposureMin;
    m[QStringLiteral("exposureMax")] = fx.exposureMax;
    m[QStringLiteral("exposureScale")] = fx.exposureScale;
    m[QStringLiteral("tonemapFixed")] = fx.tonemapFixed;
    m[QStringLiteral("meterPattern")] =
        fx.meterPattern == ExposureMeterPattern::Average        ? QStringLiteral("average")
        : fx.meterPattern == ExposureMeterPattern::Spot         ? QStringLiteral("spot")
                                                                : QStringLiteral("centreWeighted");
    m[QStringLiteral("meterLowPercent")] = fx.meterLowPercent;
    m[QStringLiteral("meterHighPercent")] = fx.meterHighPercent;
    m[QStringLiteral("bloom")] = fx.bloom;
    m[QStringLiteral("bloomThreshold")] = fx.bloomThreshold;
    m[QStringLiteral("bloomKnee")] = fx.bloomKnee;
    // Reported even though the VR policy turns bloom OFF in both eyes: this map
    // exists to name a difference between two descriptions, and a field left
    // out of it is a difference nobody can see.
    m[QStringLiteral("bloomAmount")] = fx.bloomAmount;
    m[QStringLiteral("ssao")] = fx.ssao;
    m[QStringLiteral("smaaPreset")] = fx.smaaPreset;
    m[QStringLiteral("ssr")] = fx.ssr;
    m[QStringLiteral("ssrScreenMarch")] = fx.ssrScreenMarch;
    m[QStringLiteral("reflectionRoughnessCutoff")] = fx.reflectionRoughnessCutoff;
    m[QStringLiteral("refractions")] = fx.refractions;
    m[QStringLiteral("distortion")] = fx.distortion;
    m[QStringLiteral("distortionStrength")] = fx.distortionStrength;
    m[QStringLiteral("allowOffscreen")] = fx.allowOffscreen;
    // WHERE THE AUTOMATIC EXPOSURE ACTUALLY SETTLED, as the tonemapper's own
    // multiplier — the only field here that is a READBACK rather than a
    // description, and the only way to compare two METERING views at all (the
    // description says "measure it", not what was measured). 0 when there is
    // nothing to read: no chain, or the fixed form, which measures nothing.
    m[QStringLiteral("exposureMeasured")] = view.measuredExposureScale();
    // ...AND WHETHER THIS VIEW'S PER-FRAME GLOBALS REACH THE FRAME AT ALL
    // (chain::ViewGlobalsListener). Everything the meter, the auto exposure,
    // the bloom threshold, the AO/SSR camera terms and the looks' parameters
    // need rides that one push, immediately before this view's passes; a count
    // that does not climb means this picture is rendering with whatever the
    // last workspace to update left in the process-wide materials.
    m[QStringLiteral("globalsPushes")] = QVariant::fromValue(qulonglong(view.globalsPushes()));
    QVariantList looks;
    for (const LookDesc &l : fx.looks) {
        QVariantMap one;
        one[QStringLiteral("id")] = lookName(l.kind);
        QVariantList p;
        for (float v : l.p) p.append(v);
        one[QStringLiteral("p")] = p;
        looks.append(one);
    }
    m[QStringLiteral("looks")] = looks;
    return m;
}

/// THE ON-SCREEN VIEW OF THE SAME SCENE — the desktop's picture, for the
/// comparison above. The mirror view when a host named one (it is by
/// definition the view the desktop is showing); otherwise the first enabled
/// on-screen view bound to the eye view's scene.
View *desktopViewOf(Engine *e, View *eyes)
{
    if (!e || !eyes) return nullptr;
    if (View *m = e->vrMirrorView()) return m;
    std::vector<View *> views;
    e->listViews(views);
    for (View *v : views)
        if (v && !v->isOffscreen() && v->scene() == eyes->scene()) return v;
    return nullptr;
}

}   // namespace

VrApi::VrApi(ScriptHost &host, const ModuleHost &moduleHost)
    : ApiModule(host), moduleHost(moduleHost)
{
    // THE PROXIES FOLLOW THE DRIVER'S OWN TICK, because they belong to ANY
    // session — including the Player's, whose page the editor viewport is not
    // syncing at all. The editor preview's own per-frame step is installed on
    // the VIEWPORT instead (IEditorViewport::setVrPreviewStep), which is the
    // only place that also runs on a scripted `editor.frame()`.
    //
    // Pushing a status twice in a frame costs nothing: it is a store, and the
    // mirror reads it once when it syncs.
    if (moduleHost.engine && moduleHost.engine->driver())
        connect(moduleHost.engine->driver(), &EngineRenderDriver::beforeFrame, this,
                [this] { pushProxies(); stepInteraction(); });
    installInteraction();
}

QVector<VerbInfo> VrApi::verbs() const
{
    return {
        { "available",
          "vr.available() -> {available, reason, runtime, version, system, openxr, "
          "eyeSize:[w,h], refreshHz, visibilityMask, depthLayer}",
          "Whether this process reached an OpenXR runtime, and what the runtime is. "
          "NEVER throws and never blocks: with no loader, no runtime manifest or no headset "
          "on the cable it answers `available: false` and `reason` says which in words.\n\n"
          "VR capability is decided ONCE, at engine boot, and only when the process was "
          "started with `--vr` (or JAHSHAKA_VR=1): the OpenXR route has the RUNTIME create the "
          "Vulkan instance and device the whole engine then runs on, so it cannot be turned on "
          "later, and a plain launch is bit-identical to an engine that has never heard of VR. "
          "Plugging a headset in after launch therefore needs a restart — WiVRn only writes its "
          "runtime manifest when the headset connects.",
          Needs::Engine },
        { "info", "vr.info() -> {…}",
          "The same map as vr.available(). Kept as its own verb because \"is VR available\" and "
          "\"what is this runtime\" are two questions a caller asks at different times, and a "
          "tool schema reads better with both.",
          Needs::Engine },
        { "begin",
          "vr.begin({mirror?, worldScale?, eyeWidth?, eyeHeight?, reflections?, "
          "hiddenAreaMask?, warmUp?, hands?}) -> bool",
          "THE EDITOR'S VR PREVIEW (SPECS/VR_SPEC.md §5 phase 4): starts the VR session on the "
          "editor's scene and returns true once it exists. From the next frame the render loop is "
          "PACED BY THE RUNTIME (xrWaitFrame), both eyes are drawn in one pass into a target two "
          "eyes wide, each eye is copied into the runtime's swapchain and one projection layer is "
          "submitted.\n\n"
          "THE WEARER SEES THE EDITOR WORKING. An editor preview puts the editor's own furniture "
          "in the headset — the grid, the light and camera icons, the SELECTION OUTLINE, the "
          "gizmo — because standing inside the scene while it is authored is the whole point. "
          "(The Player's VR mode shows none of it, exactly as the desktop Player shows none.) "
          "The wearer's two CONTROLLERS are drawn in both modes, and in the desktop viewport as "
          "well, so somebody at the desk can see where they are reaching: `vr.proxies`.\n\n"
          "THE DESKTOP VIEWPORT KEEPS BEING AN EDITOR while the session runs — its own camera, "
          "its own framing, its gizmos and its grid. Two things change: the editor's FLY KEYS "
          "(right button + the arrow cluster, Shift to boost, at the editor's own speed) walk the "
          "WEARER instead of the viewport camera, and nothing writes the head pose back into the "
          "document — take the headset off and the viewport is exactly where you left it. The "
          "Player's VR mode (`vr.toggle`, `player.play({vr:true})`) is the other arrangement: "
          "there the wearer IS the play camera and the desktop becomes a mirror.\n\n"
          "`mirror` is \"left\" (THE DEFAULT: the desktop is a COPY of the left eye, one render pipeline — the owner's rule), \"right\", \"both\" or \"none\" (the desktop as its own editor camera with the wearer's markers in it — a third render, for a second person at the desk) — which half "
          "of the headset's picture is painted over the desktop viewport; with a mirror the "
          "editor's own View is switched off once the headset has drawn, so the frame is the two "
          "eyes and a copy. `worldScale` is metres of world per "
          "metre of room (1 = life size). `eyeWidth`/`eyeHeight` override the size each eye is "
          "RENDERED at, for measurement only; the runtime's swapchains keep the runtime's size, "
          "so the copy scales.\n\n"
          "`warmUp` is HOW MANY STEREO WARM-UP FRAMES the session renders before its first "
          "committed one (default 2, one forward and one back; 0 disables it). They are "
          "rendered from the rig's origin through a wide frustum, with nothing submitted to "
          "the runtime and nothing mirrored, and they exist because the first frame the runtime "
          "asks a picture of used to build every shader and pipeline the two eyes need ON THE "
          "FRAME THREAD: 1,179 ms cold and 89 ms warm on the Grand Showroom, which at 62.5 Hz "
          "is 73 repeated headset frames. The desktop's own warm-up cannot pay it (instanced "
          "stereo is a different shader for the same object) and neither can a warm cache at a "
          "new eye size. `vr.state().warmUp` reports what it cost.\n\n"
          "REFLECTIONS IN THE HEADSET follow the PROJECT by default — the World panel's SSR "
          "row, the same row the desktop viewport renders with — and `reflections` (0 off, 1 "
          "half-resolution, 2 full) overrides it for a measurement. In a headset the row buys "
          "RAY-TRACED reflections per eye rather than the desktop's screen-space march, which a "
          "target holding two eyes side by side cannot carry: a ray is traced in the world from "
          "the eye that owns its pixel, a screen march would walk into the other eye.\n\n"
          "`hiddenAreaMask` is FOR A MEASUREMENT ONLY and defaults to true: the corners of each "
          "eye that the headset's lenses never show are masked out at the near plane so nothing "
          "behind them is shaded (`vr.state().hiddenArea`). There is no user row for it and there "
          "will not be one — nobody can see the pixels it removes — but the saving cannot be "
          "measured without a control arm in the same process at the same pose, which is what "
          "false is for.\n\n"
          "BARE HANDS follow the PROJECT (the World panel's VR section, `world.vr({hands})`) "
          "and are OFF unless it says otherwise: with them off this session suggests no "
          "`ext/hand_interaction_ext` bindings, creates no hand tracker and reports no "
          "skeleton, so a wearer who puts a controller down is left holding nothing rather "
          "than being handed to bare hands mid-session by the runtime; the controllers are "
          "unaffected either way. `hands` overrides the project's row FOR THIS SESSION ONLY "
          "and writes nothing — it is what a suite and a measurement use, exactly like "
          "`hiddenAreaMask`. `vr.state().hands.enabled` says which way a running session "
          "went, and `vr.state().bindings.profiles` is where the bare-hand block is or is "
          "not.\n\n"
          "REFUSES (false, with app.lastError set) rather than throwing when VR is unavailable, "
          "when a session is already running, or when there is no scene yet.",
          Needs::Engine },
        { "proxies", "vr.proxies(on?) -> bool",
          "THE WEARER'S HANDS: a small wand at each controller, drawn IN THE HEADSET — in the "
          "editor's preview and in the Player alike, the way a VR engine draws a wearer's own "
          "controllers — and in the desktop editor viewport, so somebody at the desk can see "
          "where the wearer is reaching. On by default; called with no argument it answers "
          "whether they are on.\n\n"
          "THERE IS NO HEAD MARKER, deliberately: in the headset it would be a box in the "
          "wearer's own eyes, and on the desktop \"where is the wearer\" is already the camera.\n\n"
          "They reach no reflection-probe capture, no shadow map, no GI and no user-grade "
          "screenshot — a controller cannot light the room or turn up in a picture somebody "
          "takes. No document node is created: nothing in the outliner, nothing saved.\n\n"
          "The poses come from the OpenXR action system (a grip pose on the simple-controller "
          "profile, which every runtime maps from whatever the wearer is holding) or, where the "
          "runtime offers hand tracking and no controller answers, from the palm joint. POSES "
          "ONLY — no buttons are read anywhere in this build.",
          Needs::Engine },
        { "move", "vr.move({forward?, back?, left?, right?, up?, down?, boost?, seconds?}) -> bool",
          "MOVES THE WEARER of the editor's VR preview, exactly as holding the editor's fly "
          "keys would: along the HEAD's level heading for forward/back and the horizontal "
          "beside it for left/right, along the world's up for up/down, at the editor's own fly "
          "speed for `seconds` (default one 1/60 s step). It moves the RIG — the room the "
          "wearer stands in — so their own step across the floor still counts on top of it, "
          "and their pitch and roll are never touched.\n\n"
          "The same call the held keys make each frame, which is what lets a script, an MCP "
          "session or a suite walk a wearer through a world with no keyboard in the room.\n\n"
          "IT MOVES WHOEVER IS IN THE HEADSET (the CRUD of `player.vrMove`, which this verb "
          "replaced): the editor's VR preview when that is what is running, and the PLAYER's VR "
          "mode when the run owns the session — one gesture, one verb, whichever host started "
          "it. False when nobody is in VR. (`player.vrRecenter` stays its own verb: \"put me "
          "back where the run began\" is a different product gesture from walking.)",
          Needs::Engine },
        { "proxyPose", "vr.proxyPose(\"left\"|\"right\") -> {drawn, x, y, z, rotation, yaw}",
          "WHERE THE WEARER'S CONTROLLER IS ACTUALLY DRAWN — the world pose of the proxy node "
          "itself, read back out of the scene graph, as against `vr.state().hands` which is "
          "what the runtime REPORTED.\n\n"
          "The two are the same number when everything is right, and that is the point: the "
          "poses do not exist until the runtime has been asked inside the frame, so a marker "
          "positioned from outside the frame loop necessarily lags it (two frames, ~22 ms at "
          "90 Hz, before the engine took the placement over). This verb is how that is "
          "measured rather than assumed — `scripting.e2e`/`vr.verbs_session` asserts the two "
          "agree to a millimetre on the frame a move happens.\n\n"
          "`drawn` is false when there is no proxy node at all (no session has asked for one, "
          "or `vr.proxies(false)`), and the pose is then all zeros.",
          Needs::Engine },
        { "end", "vr.end() -> bool",
          "Ends the session and puts everything back — the mirror, the stereo view, the "
          "both-eyes target, the swapchains, the frame's pacing. False when none was running. "
          "A session that the runtime has already lost (a disconnected headset) ends the same "
          "way; VR cannot be started again in that process, which vr.state() says.",
          Needs::Engine },
        { "toggle", "vr.toggle() -> bool",
          "ENTER OR LEAVE VR — the whole product gesture in one verb (SPECS/VR_SPEC.md §4.5, "
          "phase 3), and what the editor toolbar's VR icon, the Player page's VR button and "
          "the Ctrl+Shift+V binding all call.\n\n"
          "Not in VR: the Player page comes up and the scene starts IN THE HEADSET — "
          "`player.play({vr:true})`, so the wearer stands where the play camera stands, the "
          "desktop mirrors the left eye and the runtime paces the loop. Already in VR: the run "
          "STOPS (`player.stop()`), which takes the headset off and stops the scene, because "
          "that is what a second press of one button means. `player.endVr()` is the other "
          "order — leave VR, keep playing.\n\n"
          "Answers whether the player is IN VR after the call: true on entry, false on exit AND "
          "false on a refusal, with app.lastError saying which (a box with no runtime cannot "
          "enter, and VR capability is fixed at boot — a process not started with --vr never "
          "has any).",
          Needs::Engine },
        { "inject",
          "vr.inject(hand, {valid?, aim?, grip? (defaults to aim), manip?, profile?, joints?, "
          "select?, grab?, menuPressed?, stick?, "
          "stickPressed?, focused?}) -> bool",
          "TEST-FACING: WRITES ONE HAND'S SAMPLE AS IF THE RUNTIME HAD REPORTED IT — the "
          "backbone every VR gesture test in this tree drives (SPECS/VR_INPUT_SPEC.md §2.4).\n\n"
          "The interaction logic above the boundary is arithmetic on two poses and four "
          "booleans, so given this hook it runs — and is asserted — with no headset, no "
          "controller and no runtime at all. What is written REPLACES the whole hand, poses "
          "included, until the next call or until `vr.inject(hand)` with no state clears it. "
          "The poses are already in WORLD space (the frame vr.state() reports); the rig is not "
          "applied to them a second time. `vr.state().input` reads them back, "
          "`hands.left/right` follows, and the controller proxy is drawn where they say.\n\n"
          "`aim` and `grip` are {x, y, z, rotation:{x,y,z,w}} (Euler degrees {x,y,z} are "
          "accepted for the rotation, as everywhere else); `select` and `grab` are 0..1 and "
          "their presses are derived at 0.5 unless given; `stick` is {x, y} in -1..1.\n\n"
          "BARE HANDS (stage 3): `manip` is the frame the hand HOLDS things in — the pinch "
          "point on fingers, the grip in a fist — and defaults to `grip`; `profile` names "
          "the interaction profile the runtime is to be pretended to have bound (a full "
          "/interaction_profiles/... path, or one of hand_interaction, hands, touch, simple, "
          "wmr), which is what decides the press thresholds (0.7/0.3 for a pinch against "
          "0.5/0.4 for a trigger), the manipulation frame and which model is drawn for that "
          "hand; `joints` is up to 26 poses in the XR_EXT_hand_tracking joint order (palm, "
          "wrist, thumb, index, middle, ring, little), which is what the wearer's own hand "
          "is drawn from — read back with `vr.handJoints(hand)`. A hand whose profile is a "
          "HAND draws its skeleton and no controller; a hand re-bound mid-gesture cancels "
          "that gesture, exactly as a lost focus does; and `menuPressed` on a hand profile is "
          "REFUSED — `menu` is unbound there (aim_activate_ext is the pinch itself, so a bare "
          "hand has no modifier in this build), and a sample that pressed it would be driving "
          "a gesture no runtime can report. "
          "`focused` (true by default on every call that writes a hand) is the SESSION's "
          "input focus, not the hand's: the "
          "runtime takes focus away for the whole application — for its own dashboard, or when "
          "the headset comes off — and every control then reads its zero, so a gesture in "
          "flight is CANCELLED on a false rather than committed. It is read back as "
          "`vr.state().inputFocused`, once, and injecting false on either hand is how that "
          "rule is driven with no runtime.\n\n"
          "REFUSED (false, app.lastError) while a session is running and the runtime has a real "
          "interaction profile bound for that hand, unless the process was started with "
          "JAHSHAKA_VR_TEST_INJECT=1: a smoke in a headset can never be fooled by an injection "
          "a script left behind. The wearer's own hardware always wins.",
          Needs::Engine },
        { "handJoints", "vr.handJoints(hand) -> {hand, tracked, count, joints:[...], drawn, bones, profile}",
          "THE WEARER'S OWN HAND, BONE BY BONE (SPECS/VR_INPUT_SPEC.md §7, phase 4b stage 3) — what the runtime reports for that hand, and what is DRAWN for it.\n\n"
          "`tracked`/`count`/`joints` are XR_EXT_hand_tracking's answer: up to 26 poses in world space through the rig, in the extension's own order (palm, wrist, then thumb, index, middle, ring and little from the knuckle out). 0 for a hand holding a controller and for every runtime with no hand tracking — which includes the simulated one this box gates on, so `vr.inject(hand, {joints:[...]})` is how a skeleton is driven here.\n\n"
          "`drawn` is how many of the 24 bone segments the wearer can actually SEE (the mirror's own count) and `bones` how many exist: a hand is drawn as segments on the two helper channels — in every VR eye and in the desktop editor's picture, in no probe capture and in no user screenshot — and never beside a controller model, because the two are alternative drawings of one hand. `profile` is what the runtime bound for it.",
          Needs::Engine },
        { "haptic", "vr.haptic(hand, amplitude?, seconds?) -> bool",
          "BUZZES ONE CONTROLLER (amplitude 0..1, default 1; seconds default 0.05, clamped to "
          "2 s) — the one output a controller has, and what a gesture uses to say \"that "
          "landed\".\n\n"
          "False when no session is running or the hand is not a hand. TRUE, deliberately, when "
          "the runtime took the call and nothing buzzed: a profile with no haptic output at all "
          "(bare hands, a simulated controller) is a supported controller, not an error.",
          Needs::Engine },
        { "state",
          "vr.state() -> {active, state, runtime, version, space, eyeSize:[w,h], refreshHz, "
          "frames, rendered, warmUp:{frames,ms}, ipd, mirror:{mode,showing}, worldScale, "
          "asymmetricFov, "
          "spaceChanges, head, "
          "hands:{left,right}, input:{left,right}, inputFocused, profile, "
          "bindings:{offered, accepted, profiles:[{profile, bindings, accepted}]}, "
          "hiddenArea:{source, fraction:[l,r], triangles:[l,r]}, "
          "swapchainFormat, colourEncodedOnce, postFx:{eye, desktop}, "
          "handActions, handJoints, proxies, preview}",
          "What the session is doing. `state` walks the runtime's own lifecycle — idle, ready, "
          "synchronized, visible, focused, stopping, lost — and `frames` counts the frames the "
          "runtime ACCEPTED (xrEndFrame), which is the only honest measure of a session on a "
          "loaded box: wall-clock time measures the box, not the session. `ipd` is the distance "
          "between the two located eyes and `asymmetricFov` says whether the runtime gives the "
          "eyes different projections (a real headset does; a simulated one usually does not). "
          "`space` is \"stage\" (a floor origin) or \"local\". `spaceChanges` counts the times "
          "the RUNTIME recentred that space under the wearer (a Quest long-press, a guardian "
          "re-setup) — each one is absorbed into the rig so the wearer does not move, and a "
          "count climbing while nobody touched the headset is a runtime problem. With no "
          "session every field is at its zero and `state` is \"idle\" or \"unavailable\".\n\n"
          "`head` and `hands.left` / `hands.right` are POSES in WORLD space — {valid, position, "
          "rotation, yaw} — the runtime's own, composed through the rig, which is the only frame "
          "a caller can reason in. The head's `valid` LATCHES once the session has located "
          "anything (a locomotion rule that stopped dead on one skipped frame would stutter); a "
          "hand's is this frame's answer alone, so a controller that is put down or switched off "
          "leaves nothing behind. `handActions` says the action set was attached — i.e. "
          "controllers CAN report — and `handJoints` that hand tracking supplied a pose. "
          "`preview` describes the editor's VR preview (see vr.begin).\n\n"
          "`mirror` is the DESKTOP: `mode` is which half of the headset's picture this session "
          "was asked to copy (\"left\", \"right\", \"both\" or \"none\") and `showing` is "
          "what the window is painting RIGHT NOW — \"eye\" (the copy; the desktop's own View "
          "is switched off, which is the one-render-pipeline rule) or \"own\" (its own live "
          "camera). It goes to \"own\" within a frame of the runtime ceasing to ask for "
          "pictures — a wearer lifting the headset, an open dashboard, a lost runtime — and "
          "back to \"eye\" on the next drawn frame, with a few frames of hysteresis so a "
          "single skipped frame cannot flap the screen. With `mode:\"none\"` it is always "
          "\"own\": that session never takes the desktop at all.\n\n"
          "`input.left` / `input.right` are the CONTROLS (phase 4b stage 1; `manip`, `profile` "
          "and `jointsTracked` are stage 3's): {valid, aim, grip, "
          "select, selectPressed, grab, grabPressed, menuPressed, stick:{x,y}, stickPressed, "
          "fromInjection}. `aim` is where the hand POINTS (the ray is -Z of its rotation) and "
          "`grip` where it IS — the runtime's two different answers, not one derived from the "
          "other; `grip` is the same pose as `hands`. The presses come from the analogue values "
          "through one threshold with hysteresis, so a trigger resting on the line cannot "
          "chatter. `profile` is the interaction profile the runtime actually bound "
          "(\"/interaction_profiles/oculus/touch_controller\"), empty when it has bound none, "
          "and `bindings` counts the suggested-binding blocks offered and accepted — four are "
          "offered (simple, Touch, WMR and hand interaction) and a runtime takes the ones it "
          "knows — while `bindings.profiles` names each block with how many bindings it "
          "carried and whether the runtime took it, because \"4 of 4\" cannot tell a block "
          "that bound every path it meant to from one that bound half (a path spelled wrong "
          "takes that hardware's control away silently; the bare-hand block is ten — the "
          "grip, aim and pinch poses, select and grab, per hand, and NO menu: "
          "aim_activate_ext is the pinch itself, so a bare hand has no modifier). "
          "`fromInjection` is true for a sample vr.inject wrote. `inputFocused` is the "
          "SESSION's input focus — one bit, because a runtime takes focus away for the whole "
          "application and never for one hand — and a gesture in flight is cancelled on a "
          "false, never committed.\n\n"
          "`hiddenArea` is THE EYE'S OWN MASK (lane HAM-1): the corners a headset's lenses "
          "never show, taken from the runtime's own geometry "
          "(XR_KHR_visibility_mask) and drawn depth-only at the near plane so nothing behind "
          "them is ever shaded. `source` is \"runtime\", \"off\" (nothing asked for it) or "
          "\"none\" (the runtime has no mask to give — which is every runtime without the "
          "extension, and not an error); `fraction` is how much of each eye it covers, "
          "measured on that geometry in the eye's own clip rectangle, and `triangles` how "
          "many triangles that was. The fraction is the HEADSET'S number, not ours — a "
          "simulated HMD and a Quest Pro mask different shapes — so a frame-time saving "
          "measured on one machine cannot be read on another without it.\n\n"
          "`swapchainFormat` and `colourEncodedOnce` are THE COLOUR CONTRACT with the runtime "
          "(lane EYE-GRADE-1). The eye target's bytes are display-encoded, so the session asks "
          "for an sRGB swapchain format — which is what tells an OpenXR runtime exactly that, "
          "so its decode and its display encode cancel and the picture reaching the wearer is "
          "encoded exactly ONCE. `colourEncodedOnce` is false only on a runtime that offered "
          "no such format: it then treats our bytes as linear and encodes them a second time, "
          "the wearer's picture reads about a stop too bright, and the editor says so as a "
          "scene issue as well as in the log.\n\n"
          "`postFx` is THE GRADE, BOTH PICTURES: `postFx.eye` is the EFFECTIVE post "
          "description the session's eye pair is rendering with and `postFx.desktop` the one "
          "the desktop's view is rendering with, field for field in the same spelling "
          "(`desktop` is ABSENT when no on-screen view of that scene exists — a headless run, "
          "or a mirror pointed at nothing). They are the same description — the headset is a "
          "view of the project's scene and is graded by the project, exposure mode and stops, "
          "meter pattern, looks and reflection row alike — except for the VR POLICY list a "
          "side-by-side stereo target cannot carry: `bloom` (one 256x256 blur ladder for both "
          "eyes, 65 taps wide, so one eye's highlights would smear across the other), `ssao` "
          "(one projection for two eyes), `smaaPreset` (its search walks up to 16 texels "
          "across the seam), `ssrScreenMarch` (the march walks the target), `refractions` and "
          "`distortion` (both READ the target at an offset coordinate and fall back only at "
          "the FRAME's edges, so a refractor at an eye's nasal edge shows the other eye), "
          "`hzb`, and the looks whose geometry is measured from the frame's centre — which in "
          "a two-eye target is the inner edge of both. `allowOffscreen` is true in the eye "
          "because the pair is offscreen only in the sense that two eyes share one texture. "
          "Absent with no session. Before this the session wrote its own description by hand "
          "and the whole World panel was inert in the headset.",
          Needs::Engine },
        { "eyeScreenshot",
          "vr.eyeScreenshot(eye, path) -> {path, width, height, eye, center:{r,g,b}}",
          "ONE EYE OF THE RUNNING SESSION, AS THE WEARER SEES IT, written to `path` as a PNG. "
          "`eye` is 0/\"left\" or 1/\"right\".\n\n"
          "It renders the eye MONO — at the eye's own size, through the eye's own pose and "
          "its own projection, with the session view's chain and its measured exposure as a "
          "CONSTANT — and reads it back once the picture stops moving (never after a fixed "
          "frame count). So it is two things at once: the VR screenshot a user wants (\"what "
          "did I see in there\"), and the one place the stereo path's arithmetic is checked "
          "against the engine's ordinary one — the eyes are drawn from a hand-converted VrData "
          "pair, this is drawn through Camera's own projection path, and a session that ever "
          "stopped converting would produce two pictures that disagree about DEPTH.\n\n"
          "IT RENDERS FRAMES — up to ninety of them, on the UI THREAD, so the editor is "
          "unresponsive for as long as they take (a second or two on a rig, less on a real "
          "GPU) and a simulated runtime's head keeps moving through them. A tool and a test "
          "call, never something to put in a loop, and a caller comparing it with the desktop "
          "must take the desktop's shot in the same breath. It is NOT a copy of the bytes the "
          "runtime was handed: it is a fresh mono render of that eye's pose and projection "
          "through the session's chain, with the session's measured exposure frozen so the "
          "two pictures are comparable at all. Refuses with no session, before the eyes have "
          "been located, and on an eye index that is neither 0 nor 1.",
          Needs::Engine },

        // ---- STAGE 1: THE CONTROLLERS (SPECS/VR_INPUT_SPEC.md) ------------
        { "inputState",
          "vr.inputState() -> {hands:[{hand, valid, aim, grip, select, selectPressed, grab, "
          "grabPressed, menuPressed, stick:{x,y,pressed}, fromInjection}], source, focused}",
          "WHAT THE INTERACTION READ THIS FRAME, whoever wrote it — the runtime's action "
          "system or an injection (`vr.inject`). `source` says which, and each hand's "
          "`fromInjection` says it again per hand, so a smoke on a real headset can prove it "
          "is not looking at a stale test value.\n\n"
          "The poses are WORLD space, through the rig, in the same spelling as "
          "`vr.state().head` and `.hands` — {valid, x, y, z, rotation, yaw}. The AIM pose is "
          "the pointing one: its ray leaves the controller along -Z of its rotation, which is "
          "what `vr.hover()` casts. `grip` is where the hand holds the thing, and it is what a "
          "near grab welds an object to.\n\n"
          "`focused` is whether the input is LIVE. A runtime answers nothing outside its "
          "Focused state (the system dashboard is up) and a gesture in flight is CANCELLED "
          "rather than committed when that happens.",
          Needs::Engine },
        { "step",
          "vr.step({seconds?, frames?}) -> bool",
          "RUN ONE INTERACTION FRAME (or `frames` of them) FROM A SCRIPT — the clock of every "
          "VR gesture test, and the companion of `vr.inject` (VR_INPUT_SPEC §2.4 I1).\n\n"
          "`vr.inject` writes what a hand is doing; this reads both hands and DOES it: the aim "
          "ray, the document pick, the press edges, a live gesture's follow, the stick's walk "
          "and turn. So a gesture from a script is `inject` then `step`, as many times as it "
          "has frames — press, step, read, release, step, read — and nothing about the code "
          "under it knows it is not being worn.\n\n"
          "WHY A SCRIPT HAS TO STEP IT AT ALL. The render driver steps the interaction once "
          "per rendered frame, but it STANDS DOWN while an injection is armed (any hand whose "
          "sample carries `fromInjection`): a driver tick stepping a script's held stick again "
          "would integrate it twice and turn the wearer twice per flick. And a script run "
          "holds the render loop still anyway (SCRIPTING_LIVE_SPEC §3.1). With no session "
          "there is no VR loop in the process at all, which is exactly the case every headless "
          "gesture gate runs in.\n\n"
          "`seconds` is the frame this step charges, defaulting to 1/90 — a headset's own "
          "period, so a scripted step costs a worn one's worth of stick travel; it is clamped "
          "like every other frame the rig is moved on. `frames` (default 1) repeats the step "
          "with the same input, which is how a push/pull or a smooth turn is driven for a "
          "measured number of frames without restating the hand. Frames, never a wall clock.",
          Needs::Engine },
        { "hover",
          "vr.hover() -> {hand, id, rootId, name, x, y, z, distance, triangleIndex} | null",
          "WHAT THE DOMINANT HAND'S RAY IS ON, and what a trigger press would therefore "
          "select: `id` is the node the press SELECTS (the whole imported asset unless that "
          "asset is already selected, the editor's own root rule) and `rootId` the top of the "
          "chain the ray actually struck.\n\n"
          "The pick is the DOCUMENT's own — the same one a viewport click uses — so the rules "
          "are the same rules: a LOCKED node is not a hit (the lock IS `pickable`), a hidden "
          "one is not a hit, and the editor's own furniture (the grid, the icons, the gizmo, "
          "the outline) is not pickable at all because none of it is a document node. Lights, "
          "decals and cameras answer through their half-metre origin spheres.\n\n"
          "`triangleIndex` is the triangle under the ray for a mesh hit, -1 for a sphere one. "
          "null means the ray is on nothing — or that no controller is reporting.",
          Needs::Engine },
        { "select", "vr.select({hand?, mode?}) -> bool",
          "WHAT A TRIGGER PRESS DOES, as a verb: cast the hand's ray, resolve what it hit and "
          "select it through the editor's own SelectionService — so the properties column, the "
          "outliner and the selection outline follow exactly as they do for a click.\n\n"
          "`mode` is \"replace\" (the default), \"toggle\" (what holding the menu button "
          "does — the Ctrl of VR) or \"add\". A plain press on empty space DESELECTS; a "
          "modified press on empty space keeps the set, because a slightly missed toggle must "
          "not throw a whole selection away. `hand` is \"left\"/\"right\", and defaults to "
          "the dominant one (`vr.locomotion`).\n\n"
          "False when nothing changed — no controller is reporting, the press landed "
          "somewhere that means nothing, or the PLAYER is hosting the session (it edits "
          "nothing: in the Player only locomotion runs, for a verb exactly as for a button).",
          Needs::Engine },
        { "grab", "vr.grab({hand?}) -> bool",
          "WHAT A SQUEEZE DOES: take hold of what the ray is on — selecting it first if it "
          "was not selected — and carry it with the hand until `vr.release()`. A rigid attach "
          "(owner answer 1): the object is welded to the hand, so a 10 cm hand move moves it "
          "10 cm and a wrist turn turns it about the HAND, carrying its position round.\n\n"
          "Within arm's reach it is a NEAR grab and rides the grip pose. Further out it is a "
          "FAR grab and rides the aim ray at the distance it was grabbed at: the dominant "
          "stick's Y then pushes and pulls it (multiplicatively, so the gesture works the same "
          "at 1 cm and at 100 m) and its X turns it about the world's up — the one rotation a "
          "far grab cannot do with a wrist. A far grab is FILTERED, more the further away it "
          "is, because at range the lever arm multiplies the hand's own tremor.\n\n"
          "The whole selection is carried (its D5-reduced set — a child whose parent is also "
          "selected moves once, not twice), and MENU HELD SNAPS: the gesture's own delta is "
          "quantised by the editor's snap sizes, translation and rotation, never the absolute "
          "position.\n\n"
          "REFUSED (false) when there is nothing to grab, when no controller reports, while a "
          "SCRIPT owns the document (the edit gate — the ray still hovers and still selects "
          "during a run, because those are reads), and in the PLAYER, which edits nothing.\n\n"
          "TWO HANDS ON ONE OBJECT (owner answer 7, the full version with roll): a squeeze "
          "with the OTHER hand while something is held UPGRADES the same gesture — it does not "
          "start a second one. The pair is then read the way a person holds a box: the span "
          "between the palms is the SIZE (spread them to twice the distance and the object is "
          "twice as big, uniformly), the line between them is an AXIS the object turns with, "
          "and rolling both wrists about that line ROLLS the object by the average of the two "
          "rolls. The midpoint's own move carries it. MENU HELD snaps the scale factor by the "
          "editor's scale step as well as the translation and the rotation.\n\n"
          "A light and a camera have no size, so the pair only turns and carries them — the "
          "factor is 1 for those, even inside a mixed selection. Letting go with either hand "
          "drops back to one hand with a FRESH capture, so nothing jumps and nothing is "
          "committed; the object is still one undo step, written from where the FIRST squeeze "
          "found it.\n\n"
          "Far away, the pair scales and turns the object ABOUT ITSELF rather than about the "
          "point between the hands: at ten metres a scale about the wearer's own midpoint "
          "would throw the thing another ten metres away, and a thirty degree turn of the pair "
          "would sweep it five metres sideways.\n\n"
          "A GRAB TAKEN BY THE OFF HAND ALONE IS RELEASED ONLY BY `vr.release`. The squeeze "
          "edges are read from the DOMINANT hand (the other hand's stick is locomotion, and "
          "its squeeze either joins a live gesture or flies where the wearer looks), so "
          "`vr.grab({hand:\"left\"})` with nothing already held is ended by the verb, or "
          "cancelled by a focus loss or the session ending — never by squeezing that hand.",
          Needs::Engine },
        { "release", "vr.release({hand?}) -> bool",
          "ENDS THE GRAB AND COMMITS IT — ONE undo step for the whole gesture, whatever it "
          "moved, in exactly the shape a gizmo drag pushes (one TransformSceneNodeCommand per "
          "object, wrapped in a macro only when there is more than one). So Ctrl+Z after a VR "
          "grab puts everything back where it was, once.\n\n"
          "A gesture that is taken away rather than finished — the session ends, the runtime "
          "takes focus for its own dashboard, a project is closed — is CANCELLED instead: the "
          "objects go back where they were and NOTHING is pushed. Committing half a gesture "
          "the wearer could no longer see would be worse than the snap back.\n\n"
          "WITH BOTH HANDS ON THE OBJECT this hands it back to the one still holding it: the "
          "gesture continues, nothing moves and nothing is committed (see `vr.grab`).\n\n"
          "False when no gesture was running, and in the PLAYER (which edits nothing).",
          Needs::Engine },
        { "locomotion",
          "vr.locomotion({flySpeed?, fly?, turn?, snapTurnDegrees?, smoothTurnDegreesPerSecond?, "
          "dominant?}) -> {flySpeed, fly, turn, snapTurnDegrees, smoothTurnDegreesPerSecond, "
          "dominant, hands, overridden, session}",
          "HOW THE WEARER MOVES, read with no argument and set with one — THE SESSION'S "
          "OVERRIDES over the PROJECT's settings (lane VR-WORLD-1). The defaults live in the "
          "document and are set by `world.vr` or the World panel's VR section; a session adopts "
          "them when it begins; anything set here applies for THIS SESSION ONLY and writes "
          "nothing to the project — which is what makes it safe to try a faster fly with the "
          "headset on. The read reports the EFFECTIVE values, `overridden` names the keys this "
          "session changed, and `session` says whether a session has latched a project's "
          "values (false = the live document is being read, which is what a gate injecting "
          "input at a desktop editor sees).\n\n"
          "`flySpeed` is metres per second AND IT IS THE PROJECT'S BASE, session override and "
          "all: a wearer moves at it times the person's own speed dial (editor.cameraSpeed, an "
          "integer 1-32 where 10 is normal), and `vr.state().flySpeed` is that effective "
          "number. `fly` is \"aim\" (the stick hand's own ray — "
          "Unreal's VR editor, and the default), \"gaze\" (where the wearer looks) or "
          "\"level\" (the head's heading with the pitch thrown away, the comfort option).\n\n"
          "`turn` is \"snap\" (the default — a step per flick of the stick, which is what "
          "nearly every shipping VR tool does because a continuous turn makes a proportion of "
          "people sick) or \"smooth\". Either way the wearer turns about their OWN HEAD and "
          "not about the middle of their room: turning about the rig's origin swings somebody "
          "standing at the edge of their play space sideways through a metre of world they did "
          "not ask to travel.\n\n"
          "`dominant` is \"right\" (the default) or \"left\" and swaps BOTH roles at once: "
          "the dominant hand points, selects and grabs, the other hand's stick walks and "
          "turns. One flag, because two would eventually disagree.\n\n"
          "`hands` is REPORTED HERE AND CANNOT BE SET HERE: a session binds the wearer's bare "
          "hands (or does not) when it is CREATED, because the suggested bindings are attached "
          "to its action sets before its first frame and no runtime can be asked to rebind "
          "them. Setting it is `world.vr({hands:true})` for the project, or "
          "`vr.begin({hands:true})` for one session; asking for it here is refused by name "
          "rather than answered with a yes that would do nothing.\n\n"
          "A number that is not finite, or is zero or negative, is refused by name; a true/false "
          "where a number belongs is refused too (it would otherwise read as 1); one outside a "
          "row's range is clamped to it; and an unknown mode name or an unknown key is refused "
          "with nothing applied.\n\n"
          "AN OVERRIDE SET BEFORE A SESSION STARTS SURVIVES THE BEGIN — asking for a slower fly "
          "and then putting the headset on is one gesture, not two — and every override dies "
          "with the session that used it, whichever way that session ended.\n\n"
          "NOBODY IS MOVED WHILE THEY ARE STILL BEING PLACED. A session begins (and every "
          "recentre) with the host waiting for a located frame it can pair with the rig it "
          "holds, because a correction from a mismatched pair is a teleport; the stick is "
          "refused over those frames and a flick held across them is answered on the first "
          "frame after the placement lands.",
          Needs::Document },
        { "teleport",
          "vr.teleport({to?, hand?, arm?, cancel?}) -> bool",
          "TELEPORT THE WEARER (SPECS/VR_INPUT_SPEC.md §6 row L4; the owner's answer 8). The "
          "FLY is still the default way to move — this is additive, on the other stick.\n\n"
          "THE GESTURE, as a controller makes it: push the DOMINANT thumbstick FORWARD and a "
          "thrown arc appears from the hand (a marker fired at 10 m/s, brought down by "
          "gravity, drawn to where it lands); let the stick go and the wearer is standing "
          "there. The arc is GREEN where a person may stand and RED where they may not — "
          "nothing under it, or a face steeper than 45 degrees from level, which is where "
          "every engine puts the line between a ramp and a wall. `menu`, or a flick of the "
          "stick backwards, puts it away.\n\n"
          "WHY THAT BINDING: the dominant stick does nothing at all unless the wearer is "
          "holding something (then it pushes a far grab in and out and turns it), and pushing "
          "it forward to throw an arc is Unreal's and SteamVR's convention — the thing "
          "somebody who has worn a headset before will try first. No new OpenXR action and no "
          "new suggested binding: an action cannot be added after the session's sets are "
          "attached, and the stick this rides was bound in stage 1. A profile with NO stick "
          "(khr/simple_controller) has no teleport from the controller, exactly as it has no "
          "grab there — the verbs below drive it instead.\n\n"
          "THE FOUR FORMS. `{arm:true, hand?}` traces the arc and shows it (the hand defaults "
          "to the dominant one); `{}` takes the landing the armed arc found; `{cancel:true}` "
          "puts it away; and `{to:{x,y,z}}` stands the wearer at a point on the ground with no "
          "arc and no slope test, which is what a script naming a place means. Read the armed "
          "state with `vr.inputState().teleport` — armed, valid, the landing, the surface "
          "normal, how many line segments are actually drawn, and the counts.\n\n"
          "THE WEARER ARRIVES LEVEL AND FACING THE WAY THEY ALREADY FACE: the rig is moved, "
          "never turned (a teleport that also spun the room is the fastest way to lose "
          "somebody), and their own height above their floor is carried across — a person who "
          "is standing arrives standing. It moves whoever is in the headset: the editor's VR "
          "preview and the PLAYER alike, because moving the wearer is not an edit.\n\n"
          "REFUSES (false) when there is no session and so no rig (Engine::setVrOrigin does "
          "nothing without one), while a host is still placing the wearer, when nothing is "
          "armed, and when the landing was refused.",
          Needs::Document },
        { "interactionMode",
          "vr.interactionMode() -> {dominant, turn, snapTurnDegrees, smoothTurnDegreesPerSecond, "
          "flySpeed, fly, grabbing, hand, hand2, twoHanded, scale, rollDegrees, hovering, far, snapping, "
          "distance, nodes, source, installed, rig:{live,x,y,z,yaw}, gizmo:{…}, teleport:{…}, "
          "selects, grabs, commits, cancels, turns, twoHands}",
          "WHAT THE INTERACTION IS DOING. `grabbing` and `hovering` are this moment's state; "
          "`far`, `snapping`, `distance` and `nodes` describe a gesture in flight; and the "
          "five tallies are COUNTS, monotonic for as long as the process lives — a suite "
          "brackets a gesture with them instead of timing it, which is the house rule for "
          "anything a loaded box could slow down (a wall clock measures the box, not the "
          "gesture).\n\n"
          "`rig` is WHERE THE WEARER'S ROOM STANDS in the world, read back out of the engine "
          "(which owns it) rather than remembered here — the number locomotion moves, beside "
          "the `turns` count that says a flick of the stick was answered. `live` false means "
          "there is no session, and therefore no rig at all: Engine::setVrOrigin does nothing "
          "without one, so a stick pushed outside a session walks nobody and says so.\n\n"
          "The six locomotion values are the EFFECTIVE ones — the project's (`world.vr`) with "
          "this session's `vr.locomotion` overrides applied.\n\n"
          "`installed` is whether the interaction is stepping at all: it is installed when a "
          "session begins and removed when it ends, and in the PLAYER only locomotion runs "
          "(the Player edits nothing — no selection, no grab, no transform writes), exactly as "
          "the desktop Player shows no editor furniture.",
          Needs::Document },
        { "gizmo",
          "vr.gizmo() -> {present, mode, armed, dragging, handle, scale, halfAngleDegrees, "
          "toleranceDegrees, eye:{x,y,z}, drags, commits, modes}",
          "THE EDITOR'S GIZMO IN THE WEARER'S HANDS (VR phase 4b stage 2). It is the SAME "
          "gizmo the mouse drags — the same handles, the same frozen drag frame, the same "
          "group delta and the same ONE undo entry per drag — pointed at with the dominant "
          "hand's aim ray instead of a cursor. A trigger press with the ray on a handle drags "
          "that handle (a press anywhere else selects, exactly as on the desk, where the "
          "gizmo's hit test also runs first); `menu` HELD snaps it to the grid; a SHORT press "
          "of `menu` cycles translate -> rotate -> scale through editor.setGizmoMode, so the "
          "toolbar follows.\n\n"
          "`handle` is what the ray is on right now (\"x\", \"xy\", \"screen\", "
          "\"center\", ...) or empty — A PRESS'S ANSWER, without pressing, which is why it "
          "carries the same refusals a press does: it is empty in the Player (which edits "
          "nothing), while the DESK is holding the same gizmo (a drag belongs to whoever "
          "started it), and while a script owns the document (the edit gate). With nothing "
          "selected there is no gizmo to be on at all.\n\n"
          "THE TWO NUMBERS ARE VR'S OWN, not the desktop's converted (which is what made the "
          "first cut three times too big). `halfAngleDegrees` is the angle the translate "
          "gizmo's arrows subtend at the wearer's eye — 8 degrees, roughly a fist at arm's "
          "length — and `scale` is the world size that produces it at the current distance, so "
          "the handles are the same apparent size standing over an object as across the room "
          "and do not change with the headset's field of view. `toleranceDegrees` is how far "
          "off a handle the ray may be and still take it: 0.6 degrees, the POINTER's own "
          "precision (a controller's ray wanders a few tenths of a degree in a steady hand), "
          "about 6 mm at arm's length. One constant each, in src/viewport/gizmoray.h.\n\n"
          "`armed` is whether a pointer is driving the gizmo at all. While one is, the gizmo "
          "is sized for the wearer on BOTH surfaces: there is one gizmo object in the process "
          "and it has one size, and the desk goes on drawing and dragging that same object "
          "(its picture and its pick therefore never disagree). The counters are COUNTS, "
          "monotonic for the life of the process, which is what a suite brackets a gesture "
          "with.",
          Needs::Document },
    };
}

jahshaka::engine::Engine *VrApi::engine() const
{
    if (!moduleHost.engine) return nullptr;
    const auto e = moduleHost.engine->engine();
    return e ? e.get() : nullptr;
}

QVariantMap VrApi::available()
{
    QVariantMap out;
    Engine *e = engine();
    const VrInfo info = e ? e->vrInfo() : VrInfo();
    out[QStringLiteral("available")] = e ? e->vrAvailable() : false;
    out[QStringLiteral("reason")] =
        e ? QString::fromStdString(info.reason)
          : QStringLiteral("no engine is running in this process");
    out[QStringLiteral("runtime")] = QString::fromStdString(info.runtime);
    out[QStringLiteral("version")] = QString::fromStdString(info.runtimeVersion);
    out[QStringLiteral("system")] = QString::fromStdString(info.system);
    out[QStringLiteral("openxr")] =
        info.apiMajor ? QStringLiteral("%1.%2").arg(info.apiMajor).arg(info.apiMinor) : QString();
    out[QStringLiteral("eyeSize")] =
        QVariantList{ QVariant(info.eyeWidth), QVariant(info.eyeHeight) };
    out[QStringLiteral("refreshHz")] = info.refreshHz;
    out[QStringLiteral("visibilityMask")] = info.visibilityMask;
    out[QStringLiteral("depthLayer")] = info.depthLayer;
    return out;
}

QVariantMap VrApi::info() { return available(); }

bool VrApi::begin(const QVariantMap &options)
{
    // THE WHOLE VERB IS A CALLER OF EditorVrPreview (phase 4): the session, the
    // rig's placement on the editor's render camera, the fly redirect and the
    // proxies are one object's business, and the Player's VR mode is the other
    // one. Two paths into beginVrSession from two places is how the two modes
    // would drift apart.
    // THE KEYS THIS VERB READS, AND A THROW FOR ANY OTHER — the same rule
    // `player.play` has carried since VR phase 3, and the same reason: a
    // MALFORMED CALL is a programming error and must be loud (a misspelt
    // `reflection` for `reflections` silently got the project's row, which is
    // the worst kind of "it works"), while a refusal is reserved for the
    // questions that legitimately have two answers — no headset, a session
    // already running, no scene. Every key here is one EditorVrPreview::begin
    // actually reads.
    static const QStringList known = { QStringLiteral("mirror"),
                                       QStringLiteral("worldScale"),
                                       QStringLiteral("eyeWidth"),
                                       QStringLiteral("eyeHeight"),
                                       QStringLiteral("reflections"),
                                       QStringLiteral("hiddenAreaMask"),
                                       QStringLiteral("warmUp"),
                                       QStringLiteral("hands") };
    for (auto it = options.constBegin(); it != options.constEnd(); ++it)
        if (!known.contains(it.key()))
            return fail(QStringLiteral("vr.begin: unknown option '%1' — known options are %2")
                            .arg(it.key(), known.join(QStringLiteral(", "))));
    // `hands` IS TYPE-CHECKED BY THE TABLE'S OWN RULE (lane HANDS-SWITCH-1's
    // fix round), and by that rule alone: a Flag takes true or false and
    // nothing else. `QVariant::toBool()` would have turned "no", "off" and 1
    // into a wearer with bare hands bound — the very coercion `world.vr`
    // refuses — so the one validator both verbs share is called here, before
    // anything is begun, and a wrong type is a THROW with a line number like
    // every other malformed call.
    if (options.contains(QStringLiteral("hands"))) {
        const vrworld::Row *row = vrworld::row(QStringLiteral("hands"));
        double value = 0.0;
        QString why;
        if (!row || !vrworld::validate(*row, options.value(QStringLiteral("hands")), value, why))
            return fail(QStringLiteral("vr.begin: %1")
                            .arg(why.isEmpty() ? QStringLiteral("hands must be true or false")
                                               : why));
    }
    Engine *e = engine();
    if (!e) return refuse(QStringLiteral("vr.begin: no engine is running in this process"));
    if (!e->vrAvailable() && QString::fromStdString(e->vrInfo().reason).isEmpty())
        return refuse(QStringLiteral("vr.begin: VR is not available (this process was not "
                                     "started with --vr)"));
    QString error;
    if (!editor.begin(moduleHost.engine ? moduleHost.engine->engine() : nullptr,
                      moduleHost.viewport,
                      moduleHost.engine ? moduleHost.engine->driver() : nullptr, options, &error))
        return refuse(QStringLiteral("vr.begin: %1").arg(error));
    return true;
}

bool VrApi::end()
{
    Engine *e = engine();
    if (!e) return refuse(QStringLiteral("vr.end: no engine is running in this process"));
    if (!e->vrStatus().active) return refuse(QStringLiteral("vr.end: no session is running"));
    qWarning("Jahshaka VR: vr.end() called");
    // A PREVIEW ENDS THROUGH ITS OWN OBJECT (the viewport has to get its fly
    // keys and its helpers back); a session somebody ELSE started — the
    // Player's — is ended the plain way, and that host notices on its next
    // step. One routine for both, shared with the shell's own shutdown.
    endForShutdown();
    return true;
}

// THE SHELL'S END OF A SESSION (finding 7). Through EditorVrPreview when it
// owns one — it has the viewport's fly keys and its own callbacks to give back
// — and the plain way for a session somebody else started (the Player's).
bool VrApi::endForShutdown()
{
    if (editor.end()) return true;
    Engine *e = engine();
    if (!e || !e->vrStatus().active) return false;
    if (moduleHost.engine && moduleHost.engine->driver())
        moduleHost.engine->driver()->setVrSessionActive(false);
    e->setVrMirrorView(nullptr);
    e->endVrSession();
    return true;
}

bool VrApi::proxies(const QVariant &on)
{
    if (on.isValid()) {
        showProxies = on.toBool();
        pushProxies();      // the next frame is not soon enough for a verb's answer
    }
    return showProxies;
}

/// THE ONE PUSH, EVERY FRAME, FOR WHATEVER SESSION IS RUNNING.
///
/// NOT `editor.step()`'s business, and that is the owner's rule rather than
/// tidiness: the controller proxies belong to the WEARER, so they are drawn in
/// the Player's VR mode as well as the editor's preview — and the Player's
/// session is owned by a different object entirely. What both have in common is
/// the engine's `vrStatus()` and the ONE SceneMirror (the Player page is a
/// second view on the editor's scene, and syncs the same mirror), so the push
/// lives here, above both.
void VrApi::pushProxies()
{
    SceneMirror *mirror = moduleHost.viewport ? moduleHost.viewport->sceneMirror() : nullptr;
    if (!mirror) return;
    Engine *e = engine();
    const VrStatus st = e ? e->vrStatus() : VrStatus();
    // A scene that has never worn VR pays one comparison a frame: the mirror's
    // own sync short-circuits on `active` and builds nothing.
    mirror->setVrProxies(showProxies && st.active, st);
    // ...AND THE WEARER'S OWN HANDS, WHEN THERE ARE ANY (stage 3,
    // VR_INPUT_SPEC §7). The joints are NOT on the status — fifty-two poses
    // would ride every host's per-frame copy of it — so they are fetched for a
    // hand that says it has a skeleton and pushed beside it. A hand that does
    // not (every hand holding a controller, and every runtime without hand
    // tracking) costs one bit's test and the mirror takes its bones down.
    for (unsigned h = 0; h < VrHandCount; ++h) {
        if (!e || !st.input[h].jointsTracked) {
            mirror->setVrHandJoints(h, nullptr, 0u);
            continue;
        }
        VrPose joints[kVrHandJointCount];
        const unsigned n = e->vrHandJoints(int(h), joints, kVrHandJointCount);
        mirror->setVrHandJoints(h, n ? joints : nullptr, n);
    }
}

// LOCOMOTION AS A VERB — THE ONLY ONE (the CRUD of `player.vrMove`, stage 1).
//
// The two hosts keep their own implementations because each owns a guard the
// other does not (the editor preview and the Player both defer a pending
// PLACEMENT, and a move arriving in that gap would rebuild the mismatched pair
// the guard exists to avoid), but there is ONE verb over them and one piece of
// arithmetic under them (vrorigin::flyDelta). Neither is a second path into the
// engine, and no caller has to know which host is wearing the headset.
bool VrApi::move(const QVariantMap &intent)
{
    static const QStringList known = { "forward", "back", "left", "right",
                                       "up", "down", "boost", "seconds" };
    for (auto it = intent.constBegin(); it != intent.constEnd(); ++it)
        if (!known.contains(it.key()))
            return fail(QStringLiteral("vr.move: unknown key '%1' — known keys are %2")
                            .arg(it.key(), known.join(QStringLiteral(", "))));
    flystep::Keys keys;
    keys.forward = intent.value(QStringLiteral("forward")).toBool();
    keys.back    = intent.value(QStringLiteral("back")).toBool();
    keys.left    = intent.value(QStringLiteral("left")).toBool();
    keys.right   = intent.value(QStringLiteral("right")).toBool();
    keys.up      = intent.value(QStringLiteral("up")).toBool();
    keys.down    = intent.value(QStringLiteral("down")).toBool();
    keys.boost   = intent.value(QStringLiteral("boost")).toBool();
    // ONE 1/60 s STEP BY DEFAULT — the motion one frame of held keys makes, so
    // a script calling this in a loop walks at the rate a wearer walks at.
    const double seconds = intent.value(QStringLiteral("seconds"), 1.0 / 60.0).toDouble();
    if (seconds < 0.0) return fail(QStringLiteral("vr.move: seconds must not be negative"));
    if (editor.move(keys, float(seconds))) return true;
    // ...AND THE PLAYER'S WEARER IS A WEARER. A session the Player owns is
    // moved through PlayerService, which keeps the Player's placement guard and
    // its own fly speed — the verb dispatches, it does not reimplement.
    PlayerService *player = moduleHost.services ? moduleHost.services->player : nullptr;
    if (player && player->moveVr(keys, float(seconds))) return true;
    return refuse(QStringLiteral("vr.move: nobody is in VR — neither the editor's preview nor "
                                 "the Player has a session (vr.state().preview.active and "
                                 "player.state().vr.active both say so)"));
}

/// WHERE THE MARKER IS, AS AGAINST WHERE THE HAND WAS SAID TO BE.
///
/// The node is the mirror's (it made it); the pose is the SCENE's, because the
/// session moves it inside the frame. Reading it back through the scene rather
/// than remembering what was pushed is the whole value: a lag, a missed frame
/// or a mirror that stopped syncing all show up as a difference from
/// `vr.state().hands`, and nothing else in the editor can see that.
QVariantMap VrApi::proxyPose(const QString &hand)
{
    QVariantMap out;
    const QString h = hand.trimmed().toLower();
    const int index = h == QLatin1String("right") ? 1 : 0;
    NodeId nodes[2] = { 0, 0 };
    if (SceneMirror *mirror = moduleHost.viewport ? moduleHost.viewport->sceneMirror() : nullptr)
        mirror->vrProxyNodes(nodes);
    Scene *scene = moduleHost.viewport ? moduleHost.viewport->engineScene() : nullptr;
    Vec3 position;
    Quat rotation;
    const bool drawn = scene && nodes[index] &&
                       scene->nodeWorldPose(nodes[index], position, rotation);
    out = vrnames::pose(position, rotation, drawn);
    // `valid` on a POSE means "the runtime located it"; here the question is
    // "is there a marker in the scene at all", so it is named for what it is.
    out.remove(QStringLiteral("valid"));
    out[QStringLiteral("drawn")] = drawn;
    return out;
}

// THE INJECTION HOOK AS A VERB (VR_INPUT_SPEC §2.4 I1). Pure engine plumbing:
// it parses the map, hands the struct to `Engine::vrInjectInput` and reports
// what the engine decided — the refusal rule, the world-space contract and the
// "a default state stops injecting" spelling all live below the boundary, where
// the session can enforce them.
bool VrApi::inject(const QVariant &hand, const QVariantMap &state)
{
    Engine *e = engine();
    if (!e) return refuse(QStringLiteral("vr.inject: no engine is running in this process"));
    const int index = vrnames::handFrom(hand);
    if (index < 0)
        // NAMING WHAT WAS PASSED, like every other refusal in this file: a
        // script that said "middle" wants to read "middle" back.
        return fail(QStringLiteral("vr.inject: hand must be \"left\" or \"right\" (or 0/1), "
                                   "not '%1'").arg(hand.toString()));

    static const QStringList known = { "valid", "aim", "grip", "manip", "profile", "joints",
                                       "select", "selectPressed",
                                       "grab", "grabPressed", "menuPressed", "stick",
                                       "stickPressed", "focused" };
    for (auto it = state.constBegin(); it != state.constEnd(); ++it)
        if (!known.contains(it.key()))
            return fail(QStringLiteral("vr.inject: unknown key '%1' — known keys are %2")
                            .arg(it.key(), known.join(QStringLiteral(", "))));

    VrHandState s;
    // AN EMPTY MAP IS THE "STOP INJECTING" SPELLING, which is why `valid`
    // defaults to whether anything was said at all rather than to false.
    s.valid = state.value(QStringLiteral("valid"), !state.isEmpty()).toBool();
    // ONE POSE PARSER IN THIS FILE (the 1E integration's CRUD): this verb used
    // to carry its own, which read x/y/z and `rotation` and silently accepted
    // any other key — so `aim: {nosuchkey: 1}` was a pose at the origin. The
    // one below validates its keys AND takes a rotation the two readable ways
    // (a quaternion, or yaw/pitch/roll in degrees), so the two spellings the
    // two halves of stage 1 grew cannot drift apart.
    QString error;
    auto readPose = [&](const char *key, VrPose &out) {
        if (!state.contains(QLatin1String(key))) return true;
        if (poseFromMap(state.value(QLatin1String(key)).toMap(), out, &error)) return true;
        error = QStringLiteral("vr.inject: %1: %2").arg(QLatin1String(key), error);
        return false;
    };
    if (!readPose("aim", s.aim) || !readPose("grip", s.grip) ||
        !readPose("manip", s.manipPose))
        return fail(error);
    // WHICH PROFILE THE RUNTIME IS PRETENDING TO HAVE BOUND (stage 3). It is
    // what the engine reads to decide a press threshold and a manipulation
    // frame, and what the editor reads to cancel a gesture when a hand changes
    // shape — so an injected hand can be a HAND, not just a controller with no
    // name, on a box where no runtime will ever report one.
    if (state.contains(QStringLiteral("profile")) &&
        !profileFromName(state.value(QStringLiteral("profile")).toString(), s.profile, &error))
        return fail(QStringLiteral("vr.inject: %1").arg(error));
    // A GRAB NEEDS A GRIP (the lead, from the Fable read at merge): the retired
    // Studio-side hook defaulted grip := aim, and a script that says only where
    // the hand POINTS still holds the thing where it points — with no grip the
    // grab would refuse and the session would hide the proxy. Said, not silent.
    if (state.contains(QStringLiteral("aim")) && !state.contains(QStringLiteral("grip"))) s.grip = s.aim;
    s.select = float(state.value(QStringLiteral("select"), 0.0).toDouble());
    s.grab = float(state.value(QStringLiteral("grab"), 0.0).toDouble());
    // THE PRESS IS DERIVED WHEN IT IS NOT SAID, at the same 0.5 the engine's
    // own threshold uses: a script that injects `select: 1` means the trigger
    // is down, and saying so twice is a chance to disagree with itself.
    s.selectPressed = state.contains(QStringLiteral("selectPressed"))
                          ? state.value(QStringLiteral("selectPressed")).toBool()
                          : s.select >= 0.5f;
    s.grabPressed = state.contains(QStringLiteral("grabPressed"))
                        ? state.value(QStringLiteral("grabPressed")).toBool()
                        : s.grab >= 0.5f;
    s.menuPressed = state.value(QStringLiteral("menuPressed"), false).toBool();
    // A BARE HAND HAS NO MENU BUTTON, AND AN INJECTION MAY NOT INVENT ONE
    // (VR-HANDS-1 fix round, item 1). `menu` is unbound on the hand-interaction
    // profile — `aim_activate_ext` IS the pinch, so binding it would make every
    // hand select a toggle (OgreVrSession.cpp's block says why at length) — so a
    // real hand can never report a press there, and a sample that did would be
    // testing a gesture no runtime in this build can produce. REFUSED rather
    // than silently dropped: a suite that thought it was driving a hand
    // modifier should read the reason, not a green line.
    if (s.menuPressed && vrIsHandProfile(s.profile.c_str()))
        return fail(QStringLiteral("vr.inject: menuPressed is not a thing a bare hand can do — "
                                   "`menu` is unbound on %1 (aim_activate_ext is the pinch "
                                   "itself, so a hand has no modifier in this build). Inject it "
                                   "on a controller profile, or leave it out.")
                        .arg(QString::fromStdString(s.profile)));
    const QVariantMap stick = state.value(QStringLiteral("stick")).toMap();
    s.stickX = float(stick.value(QStringLiteral("x"), 0.0).toDouble());
    s.stickY = float(stick.value(QStringLiteral("y"), 0.0).toDouble());
    s.stickPressed = state.value(QStringLiteral("stickPressed"), false).toBool();
    // FOCUS IS THE SESSION'S, NOT THE HAND'S (VR-INPUT-1E-FIX): the runtime
    // takes input focus away for the whole application, so the engine keeps ONE
    // bit for it and this key sets that. It defaults to TRUE and is only
    // written when the script says so — a test that says nothing about focus
    // means "the wearer was there". Injecting it FALSE is how the focus-loss
    // rule — a gesture in flight is cancelled, never committed — is driven with
    // no runtime to take the focus away.
    // ...AND IT IS WRITTEN BY EVERY INJECTION THAT CARRIES A STATE, defaulting
    // to true, so that "a sample that says nothing about focus means the wearer
    // was there" holds for the SESSION bit exactly as it held for the old
    // per-hand one. A bit that only ever moved when the key was present would
    // let one focus-loss test leave every later gesture in the process
    // unfocused — the stale-injection class this whole round is about. A
    // WITHDRAWAL (an empty state) says nothing about focus and leaves it alone;
    // with no hand injected at all the status reports the session's own focus.
    // THE INJECTION FIRST, THE FOCUS ONLY IF IT WAS TAKEN (the lead, from the
    // Fable read at merge): a refused injection (a bound profile, no override —
    // the headset case) must not rewrite the session's focus bit on the way.
    if (!e->vrInjectInput(index, s))
        return refuse(QStringLiteral("vr.inject: %1").arg(QString::fromStdString(e->lastError())));
    if (!state.isEmpty())
        e->vrInjectFocus(state.value(QStringLiteral("focused"), true).toBool());
    // ...AND THE SKELETON, AFTER THE SAMPLE (stage 3): the joints belong to the
    // hand the sample describes, so a refused sample never leaves a skeleton
    // behind, and a WITHDRAWAL (an empty state) takes both away — that clearing
    // happens below the boundary, in vrClearInjectedInput.
    if (state.contains(QStringLiteral("joints"))) {
        const QVariantList list = state.value(QStringLiteral("joints")).toList();
        if (list.size() > int(kVrHandJointCount))
            return fail(QStringLiteral("vr.inject: joints takes at most %1 poses (the "
                                       "XR_EXT_hand_tracking joint set), not %2")
                            .arg(kVrHandJointCount).arg(list.size()));
        VrPose joints[kVrHandJointCount];
        for (int j = 0; j < list.size(); ++j) {
            if (!poseFromMap(list.at(j).toMap(), joints[j], &error))
                return fail(QStringLiteral("vr.inject: joints[%1]: %2").arg(j).arg(error));
        }
        if (!e->vrInjectJoints(index, list.isEmpty() ? nullptr : joints,
                               unsigned(list.size())))
            return refuse(QStringLiteral("vr.inject: %1")
                              .arg(QString::fromStdString(e->lastError())));
    }
    return true;
}

// THE WEARER'S OWN HAND, READ BACK (stage 3, VR_INPUT_SPEC §7).
//
// TWO ANSWERS IN ONE MAP, and they are different questions: what the RUNTIME
// reports (`tracked`, `joints` — the poses, in world space through the rig) and
// what is DRAWN for it (`drawn` — how many of the twenty-four bone segments the
// mirror is showing). A suite that asserts "the wearer can see their hand" has
// to ask the drawer, not the tracker; the teleport arc's report is the same
// shape for the same reason.
QVariantMap VrApi::handJoints(const QVariant &hand)
{
    QVariantMap out;
    const int index = vrnames::handFrom(hand);
    out[QStringLiteral("hand")] =
        index == 1 ? QStringLiteral("right") : QStringLiteral("left");
    if (index < 0) {
        fail(QStringLiteral("vr.handJoints: hand must be \"left\" or \"right\" (or 0/1), "
                            "not '%1'").arg(hand.toString()));
        return out;
    }
    Engine *e = engine();
    VrPose joints[kVrHandJointCount];
    const unsigned n = e ? e->vrHandJoints(index, joints, kVrHandJointCount) : 0u;
    out[QStringLiteral("tracked")] = n > 0u;
    out[QStringLiteral("count")] = n;
    QVariantList list;
    for (unsigned j = 0; j < n; ++j) list.append(vrnames::pose(joints[j]));
    out[QStringLiteral("joints")] = list;
    unsigned drawn = 0u, nodes = 0u;
    if (SceneMirror *mirror = moduleHost.viewport ? moduleHost.viewport->sceneMirror() : nullptr) {
        drawn = mirror->vrHandBonesShown(unsigned(index));
        nodes = mirror->vrHandBoneNodes(unsigned(index), nullptr, 0u);
    }
    out[QStringLiteral("drawn")] = drawn;
    out[QStringLiteral("bones")] = nodes;
    out[QStringLiteral("profile")] = e ? QString::fromStdString(
                                             e->vrStatus().input[index < 0 ? 0 : index].profile)
                                       : QString();
    return out;
}

// ONE EYE, AS THE WEARER SEES IT (lane EYE-GRADE-1; Engine::vrEyeScreenshot,
// whose header is where the mono control's whole design is written out).
//
// THE ENGINE HAS RENDERED THIS PICTURE SINCE PHASE 2 AND NOTHING COULD ASK FOR
// IT: the capability existed, the verb did not, so the only picture a script
// (or the rig, or an MCP session) could take of a running session was the
// desktop MIRROR — which is one eye's TARGET and not the eye. This verb is what
// makes "does the headset show the project's picture" answerable at all, and
// what `vr.eye_grade` compares against the desktop's own screenshot.
QVariantMap VrApi::eyeScreenshot(const QVariant &eye, const QString &path)
{
    QVariantMap out;
    const int index = vrnames::handFrom(eye);   // the same 0/1 + "left"/"right" reader
    out[QStringLiteral("eye")] =
        index == 1 ? QStringLiteral("right") : QStringLiteral("left");
    Engine *e = engine();
    if (!e) { fail(QStringLiteral("vr.eyeScreenshot: no engine is running in this process")); return out; }
    if (index < 0) {
        fail(QStringLiteral("vr.eyeScreenshot: eye must be \"left\" or \"right\" (or 0/1), "
                            "not '%1'").arg(eye.toString()));
        return out;
    }
    if (path.isEmpty()) { fail(QStringLiteral("vr.eyeScreenshot: a file path is required")); return out; }
    Image img;
    if (!e->vrEyeScreenshot(unsigned(index), img)) {
        fail(QStringLiteral("vr.eyeScreenshot: %1").arg(QString::fromStdString(e->lastError())));
        return out;
    }
    if (!img.width || !img.height || img.rgba.empty()) {
        fail(QStringLiteral("vr.eyeScreenshot: the session returned no image"));
        return out;
    }
    QImage result(int(img.width), int(img.height), QImage::Format_RGBA8888);
    for (unsigned y = 0; y < img.height; ++y)
        std::memcpy(result.scanLine(int(y)), &img.rgba[size_t(y) * img.width * 4u],
                    size_t(img.width) * 4u);
    QFileInfo info(path);
    if (!info.dir().exists()) info.dir().mkpath(QStringLiteral("."));
    if (!result.save(path, "PNG")) {
        fail(QStringLiteral("vr.eyeScreenshot: could not save '%1'").arg(path));
        return out;
    }
    const QColor centre = result.pixelColor(result.width() / 2, result.height() / 2);
    out[QStringLiteral("path")] = info.absoluteFilePath();
    out[QStringLiteral("width")] = result.width();
    out[QStringLiteral("height")] = result.height();
    // WHICH PICTURE THESE BYTES ARE, EXACTLY (the Fable read's F7 — the first
    // cut of this comment said "the eye target's bytes, exactly what is copied
    // into the swapchain", and that is NOT what this verb returns). It is a
    // MONO RE-RENDER of that eye through a throwaway offscreen view, with the
    // session view's chain and its measured exposure frozen as a constant, read
    // back once the picture stops moving — up to ninety frames of real time, on
    // the calling (UI) thread, through which a simulated runtime's head keeps
    // swaying. What it shares with the eye the wearer sees is the grade, the
    // pose and the projection; what it does not share is the frame.
    //
    // THE SPACE, because a number read in the wrong one is the reading nobody
    // notices is wrong (PLAIN-GRADE-1): these are DISPLAY-ENCODED bytes, so
    // they compare with `editor.screenshot(..., 'scene')` and never with the
    // plain grade's linear radiance.
    out[QStringLiteral("center")] = QVariantMap{ { QStringLiteral("r"), centre.red() },
                                                 { QStringLiteral("g"), centre.green() },
                                                 { QStringLiteral("b"), centre.blue() } };
    return out;
}


bool VrApi::haptic(const QVariant &hand, double amplitude, double seconds)
{
    Engine *e = engine();
    if (!e) return refuse(QStringLiteral("vr.haptic: no engine is running in this process"));
    const int index = vrnames::handFrom(hand);
    if (index < 0)
        return fail(QStringLiteral("vr.haptic: hand must be \"left\" or \"right\" (or 0/1), "
                                   "not '%1'").arg(hand.toString()));
    if (!e->vrHaptic(index, float(amplitude), float(seconds)))
        return refuse(QStringLiteral("vr.haptic: %1").arg(QString::fromStdString(e->lastError())));
    return true;
}

bool VrApi::toggle()
{
    // THE VERB IS A CALLER OF THE PLAYER'S CAPABILITY, not a second path into
    // the engine: everything below happens inside PlayerService, which is what
    // the two buttons call as well (SCRIPTING_SPEC §2.3).
    PlayerService *player = moduleHost.services ? moduleHost.services->player : nullptr;
    if (!player || !player->isAvailable())
        return refuse(QStringLiteral("vr.toggle: this session has no player (headless runs "
                                     "have no player backend)"));
    const bool wasActive = player->isVrActive();
    const bool active = player->toggleVr();
    if (!active && !wasActive)
        return refuse(QStringLiteral("vr.toggle: %1")
                          .arg(player->lastError().isEmpty()
                                   ? player->vrUnavailableReason()
                                   : player->lastError()));
    return active;
}

QVariantMap VrApi::state()
{
    QVariantMap out;
    Engine *e = engine();
    const VrStatus s = e ? e->vrStatus() : VrStatus();
    const VrInfo info = e ? e->vrInfo() : VrInfo();
    out[QStringLiteral("active")] = s.active;
    out[QStringLiteral("state")] = vrnames::state(s.state);
    out[QStringLiteral("runtime")] = QString::fromStdString(info.runtime);
    out[QStringLiteral("version")] = QString::fromStdString(info.runtimeVersion);
    out[QStringLiteral("space")] = QString::fromStdString(info.space);
    out[QStringLiteral("eyeSize")] = QVariantList{ QVariant(s.eyeWidth), QVariant(s.eyeHeight) };
    out[QStringLiteral("refreshHz")] = info.refreshHz;
    out[QStringLiteral("frames")] = QVariant::fromValue(qulonglong(s.frames));
    out[QStringLiteral("rendered")] = QVariant::fromValue(qulonglong(s.rendered));
    // THE STEREO WARM-UP (lane VR-WARMUP-1): how many warm-up frames this
    // session has rendered before its first committed one, and what they cost.
    // A count and a measured value, like everything else here.
    out[QStringLiteral("warmUp")] =
        QVariantMap{ { QStringLiteral("frames"), s.warmUpFrames },
                     { QStringLiteral("ms"), s.warmUpMs } };
    out[QStringLiteral("ipd")] = s.ipd;
    // THE COLOUR CONTRACT (lane EYE-GRADE-1): which swapchain format the
    // runtime gave this session, and whether the picture reaching the wearer is
    // therefore encoded exactly once. Reported rather than only logged because
    // it is the difference between the wearer seeing the project's picture and
    // seeing one about a stop too bright, and because a suite must be able to
    // assert it on a runtime nobody is wearing.
    out[QStringLiteral("swapchainFormat")] = QString::fromStdString(s.swapchainFormat);
    out[QStringLiteral("colourEncodedOnce")] = s.colourEncodedOnce;
    // THE WISH AND THE PICTURE (lane MIRROR-LIVE-1): `{mode, showing}` —
    // which half of the headset was asked for, and whether the desktop is
    // showing that copy right now or has taken its own camera back because the
    // runtime stopped drawing.
    out[QStringLiteral("mirror")] = vrnames::mirrorState(s.mirror, s.mirrorShowing);
    out[QStringLiteral("worldScale")] = s.worldScale;
    out[QStringLiteral("asymmetricFov")] = s.asymmetricFov;
    out[QStringLiteral("spaceChanges")] = QVariant::fromValue(qulonglong(s.spaceChanges));
    // THE HIDDEN-AREA MESH (lane HAM-1). The runtime's own answer, per eye:
    // where the shape came from, how much of each eye it covers and how many
    // triangles that was. It is reported rather than merely applied because the
    // FRACTION is the headset's, not ours — a Quest Pro and a simulated HMD
    // mask different amounts, so a saving measured on one cannot be read on the
    // other without this number beside it.
    {
        QVariantMap ham;
        ham[QStringLiteral("source")] = QString::fromStdString(s.hiddenAreaSource);
        ham[QStringLiteral("fraction")] = QVariantList{ QVariant(s.hiddenAreaFraction[0]),
                                                        QVariant(s.hiddenAreaFraction[1]) };
        ham[QStringLiteral("triangles")] = QVariantList{ QVariant(s.hiddenAreaTriangles[0]),
                                                         QVariant(s.hiddenAreaTriangles[1]) };
        out[QStringLiteral("hiddenArea")] = ham;
    }
    // THE POSES (phase 4). WORLD space, the rig applied — the only frame a
    // caller can reason in — and each one reports its own validity rather than
    // a shared flag: the head latches, a hand does not (VrPose's note).
    out[QStringLiteral("head")] = vrnames::pose(s.headPosition, s.headRotation, s.posesValid);
    QVariantMap hands;
    hands[QStringLiteral("left")] = vrnames::pose(s.hands[VrHandLeft]);
    hands[QStringLiteral("right")] = vrnames::pose(s.hands[VrHandRight]);
    // ...AND WHETHER BARE HANDS WERE BOUND AT ALL (lane HANDS-SWITCH-1). It
    // sits beside the two poses because it is the question asked FIRST of a
    // hand that reports nothing: "is this project on hands or on controllers?"
    // It is the session's latched copy of the project's row, so it answers for
    // the session the caller is looking at rather than for the document.
    hands[QStringLiteral("enabled")] = s.handsEnabled;
    out[QStringLiteral("hands")] = hands;
    // THE CONTROLS (phase 4b stage 1): the two poses and every button, in the
    // one spelling vrnames owns.
    QVariantMap input;
    input[QStringLiteral("left")] = vrnames::handState(s.input[VrHandLeft]);
    input[QStringLiteral("right")] = vrnames::handState(s.input[VrHandRight]);
    out[QStringLiteral("input")] = input;
    // FOCUS, ONCE, FOR THE SESSION (VR-INPUT-1E-FIX — it used to be a bit on
    // every hand): the runtime takes input focus away for the whole
    // application, and a gesture in flight is cancelled when this goes false.
    out[QStringLiteral("inputFocused")] = s.inputFocused;
    out[QStringLiteral("profile")] = QString::fromStdString(s.profile);
    QVariantMap bindings;
    bindings[QStringLiteral("offered")] = s.bindingProfiles;
    bindings[QStringLiteral("accepted")] = s.bindingProfilesAccepted;
    // ...AND BLOCK BY BLOCK (stage 3's fix round): which profile, how many
    // bindings it carried and whether the runtime took it. The totals cannot
    // tell a block that bound everything it meant to from one that bound half
    // of them — a path spelled wrong, or an input a pin bump moved, takes that
    // hardware's control away with the totals still reading 4 of 4 — so the
    // COUNT is what a suite pins (the bare-hand block is twelve).
    {
        VrBindingBlock blocks[kVrBindingBlockMax];
        const unsigned n = e ? e->vrBindingBlocks(blocks, kVrBindingBlockMax) : 0u;
        QVariantList list;
        for (unsigned b = 0; b < n; ++b) {
            QVariantMap one;
            one[QStringLiteral("profile")] = QString::fromStdString(blocks[b].profile);
            one[QStringLiteral("bindings")] = blocks[b].bindings;
            one[QStringLiteral("accepted")] = blocks[b].accepted;
            list.append(one);
        }
        bindings[QStringLiteral("profiles")] = list;
    }
    out[QStringLiteral("bindings")] = bindings;
    out[QStringLiteral("handActions")] = s.handActions;
    out[QStringLiteral("handJoints")] = s.handJoints;
    out[QStringLiteral("proxies")] = showProxies;
    // THE TELEPORT, HERE TOO, because "where am I about to go" is a question
    // about the session as much as about the controllers — the same map
    // `vr.inputState().teleport` answers, from the one object that knows.
    out[QStringLiteral("teleport")] = interaction.teleportReport();
    out[QStringLiteral("preview")] = editor.report();
    // THE GRADE, BOTH PICTURES (lane EYE-GRADE-1): the EFFECTIVE post
    // description the session's eye pair is carrying and the one the desktop's
    // view is carrying, in the same spelling. The eye's is the project's
    // description with the VR policy applied (`applyVrViewPolicy`, Types.h), so
    // the two agree field for field except the policy's own short list — which
    // is exactly what `vr.eye_grade` asserts, and what anybody debugging "the
    // headset looks wrong" should read first.
    if (Engine *eng = e) {
        if (View *eyes = eng->vrView()) {
            QVariantMap fx;
            fx[QStringLiteral("eye")] = postFxMap(*eyes);
            if (View *desk = desktopViewOf(eng, eyes))
                fx[QStringLiteral("desktop")] = postFxMap(*desk);
            out[QStringLiteral("postFx")] = fx;
        }
    }
    return out;
}

// ===========================================================================
// STAGE 1: THE CONTROLLERS' INTERACTION (SPECS/VR_INPUT_SPEC.md)
//
// Everything below is a CALLER of VrInteraction, which is a caller of the
// editor's own picker, selection service, snap sizes and undo spine. There is
// no second implementation of any of it here — a verb that re-picked or
// re-selected would be the way the VR editor and the desktop editor come to
// disagree about what a press does.
// ===========================================================================

namespace {

QVariantMap handStateMap(const VrHandState &state, unsigned hand)
{
    // THE ONE HAND-STATE SPELLING (vrnames::handState — what `vr.state().input`
    // answers with), plus the hand's NAME, because this verb answers a LIST and
    // a caller iterating it needs to know which hand it is holding. A second
    // hand-built map beside that helper is how two verbs come to report the
    // same struct differently; there was one here until the 1E integration.
    QVariantMap out = vrnames::handState(state);
    out[QStringLiteral("hand")] =
        hand == VrHandRight ? QStringLiteral("right") : QStringLiteral("left");
    return out;
}

}   // namespace

/// THE SERVICE'S DEPENDENCIES, INJECTED ONCE.
///
/// Every one of them is a CALLABLE where the thing behind it can come and go —
/// the engine boots after the modules are built, a project switch replaces the
/// scene, a session changes which host owns the wearer — because a service
/// holding what it was handed at construction is the lifetime class this module
/// has already been bitten by twice (VR-4-FIX findings 1 and 7).
void VrApi::installInteraction()
{
    VrInteraction::Deps deps;
    deps.scene = [this] {
        return moduleHost.viewport ? moduleHost.viewport->getScene() : iris::ScenePtr();
    };
    deps.selection = moduleHost.services ? moduleHost.services->selection : nullptr;
    deps.services = moduleHost.services;
    deps.engine = [this] { return engine(); };
    // THE SCENE THE WEARER'S HELPERS ARE DRAWN ON (the teleport arc): the
    // editor viewport's own engine scene, which is also the scene the Player's
    // second View shows — one scene, two views (PLAYER-1) — so an arc armed in
    // either host is in the wearer's eyes and in the desktop picture.
    deps.engineScene = [this]() -> jahshaka::engine::Scene * {
        return moduleHost.viewport ? moduleHost.viewport->engineScene() : nullptr;
    };
    // HOW THE WEARER MOVES IS THE PROJECT'S (lane VR-WORLD-1): the fly speed,
    // the fly direction, the turn, its step or rate and the dominant hand are
    // document fields (`world.vr`), adopted by a session when it begins and
    // overridable for that session by `vr.locomotion`. Before this they were
    // the DESKTOP camera's own multiplier and four session-only fields with
    // defaults of their own — one wearer with two speeds, and nothing a
    // project could carry. The person's speed DIAL (CameraSpeed, FLYSPEED-1)
    // multiplies what this resolves to, at the three sites that fly a wearer;
    // these Settings stay the project's own numbers.
    deps.locomotion = [this] {
        return vrworld::resolve(moduleHost.viewport ? moduleHost.viewport->getScene()
                                                    : iris::ScenePtr());
    };
    // THE PLAYER EDITS NOTHING. With a session running that this module's
    // preview does not own, the host is the Player — so the ray, the select and
    // the grab stay out of it and only the stick works. With NO session at all
    // (a gate driving injected input at a desktop editor) the editor half is
    // the right half, which is what makes every gesture gateable.
    deps.playerMode = [this] {
        Engine *e = engine();
        return e && e->vrStatus().active && !editor.isActive();
    };
    // MAY THE WEARER BE MOVED THIS FRAME? Both hosts place the rig on the first
    // located frame they can pair with the rig the engine holds, and both
    // refuse their own fly until it lands (EditorVrPreview::move,
    // PlayerVr::move) — the stick wrote `setVrOrigin` from the interaction and
    // walked straight past that, which is an origin/head mismatch for the
    // frames before the placement and a turn the placement then overwrote.
    // Asked of whichever host owns the session; a host that is not running
    // places nobody.
    deps.locomotionBlocked = [this] {
        if (editor.isActive()) return editor.placing();
        PlayerService *player = moduleHost.services ? moduleHost.services->player : nullptr;
        if (player && player->isVrActive()) return player->isVrPlacing();
        return false;
    };
    // ---- THE GIZMO (VR_INPUT_SPEC §5.2, stage 2) -------------------------
    //
    // THE VIEWPORT'S OWN GIZMO, and its own mode setter. Nothing is duplicated
    // for VR: the wearer points at the object the editor is already drawing.
    deps.gizmo = [this]() -> Gizmo * {
        return moduleHost.viewport ? moduleHost.viewport->activeGizmo() : nullptr;
    };
    deps.gizmoMode = [this] {
        return moduleHost.viewport ? moduleHost.viewport->gizmoMode() : QString();
    };
    // ...THROUGH THE ONE ROUTE (viewport/gizmomode.h), which is exactly what
    // `editor.setGizmoMode` calls: the shell's slot when there is a shell, so
    // the TOOLBAR follows a mode cycled from the headset as it follows the
    // W/E/R keys, and straight to the viewport when there is none. This used to
    // re-spell the three slot names here, which is two copies of one route.
    deps.setGizmoMode = [this](const QString &mode) {
        gizmomode::apply(moduleHost.shellWidget, moduleHost.viewport, mode);
    };
    interaction.setDeps(deps);
    engineInput.setEngine(engine());
    interaction.setSource(&engineInput);
}

/// A SESSION BEGAN OR ENDED — INSTALL OR REMOVE THE INTERACTION.
///
/// ASKED BY WHOEVER GETS THERE FIRST, and that is deliberate rather than tidy.
/// The obvious place is the render driver's tick, which is where the per-frame
/// step lives — but a script run holds that loop still by design
/// (SCRIPTING_LIVE_SPEC §3.1: policy Off skips the whole tick, beforeFrame
/// included) and `editor.frame()` bypasses the driver altogether. A session
/// started from a script would therefore have an interaction that was never
/// installed, and one ENDED from a script would leave a gesture holding an
/// object that nothing was going to put back. So the edge is reconciled here,
/// from the driver's tick AND from every verb that reads or drives the
/// interaction: it is one comparison of a bool, and it cannot be missed.
void VrApi::syncInteractionSession()
{
    Engine *e = engine();
    engineInput.setEngine(e);
    const bool active = e && e->vrStatus().active;
    if (active == interactionSessionActive) return;
    interactionSessionActive = active;
    if (active) { interaction.begin(); interactionClock.start(); }
    else        { interaction.end(); interactionClock.invalidate(); }
}

void VrApi::stepInteraction()
{
    syncInteractionSession();
    if (!interactionSessionActive) return;
    // AN ARMED INJECTION OWNS THE STEPPING, and that is what makes a suite
    // deterministic: with a script writing the hands, one injected sample IS
    // one interaction frame (`vr.step()` is the clock), so a driver tick
    // stepping the same values again would integrate the stick twice and turn
    // the wearer twice per flick. Nothing is injected in a real session — the
    // engine refuses it while a bound profile reports (VR_INPUT_SPEC §2.4 I1)
    // — so the worn path is unchanged and driver-paced.
    //
    // ASKED OF THE SAMPLES, not of a flag of ours (`fromInjection`, which the
    // engine sets on every sample it wrote for a test): the store is the
    // engine's since the two halves of stage 1 met, so the question "is a test
    // driving this?" has exactly one answer in the process.
    if (interaction.injectionArmed()) return;
    // THE FRAME JUST GONE, and it is clamped before it may move anybody
    // (vrorigin::frameSeconds): a UI-thread block arrives here as one enormous
    // dt, and in a headset that is an involuntary lurch rather than a nuisance.
    const float seconds = interactionClock.isValid()
                              ? float(double(interactionClock.nsecsElapsed()) * 1e-9)
                              : -1.0f;
    interactionClock.restart();
    interaction.step(seconds);
}

unsigned VrApi::handFrom(const QVariant &value, bool *ok) const
{
    if (ok) *ok = true;
    // NOTHING SAID IS THE DOMINANT HAND — the one this file adds to the shared
    // reader, because a verb about a gesture is about the hand that gestures.
    if (!value.isValid() || value.toString().trimmed().isEmpty())
        return interaction.dominantHand();
    // ...AND THE REST IS `vrnames::handFrom`, the reader `vr.inject` and
    // `vr.haptic` use: "left"/"l"/0, "right"/"r"/1, and -1 for anything else. A
    // second spelling of the same question in one file is how "r" came to work
    // in one verb and throw in another.
    const int index = vrnames::handFrom(value);
    if (index < 0) {
        if (ok) *ok = false;
        return interaction.dominantHand();
    }
    return unsigned(index);
}

QVariantMap VrApi::inputState()
{
    syncInteractionSession();
    QVariantMap out;
    QVariantList hands;
    for (unsigned i = 0; i < VrHandCount; ++i)
        hands.append(handStateMap(interaction.handState(i), i));
    out[QStringLiteral("hands")] = hands;
    out[QStringLiteral("source")] = interaction.sourceName();
    Engine *e = engine();
    const VrStatus st = e ? e->vrStatus() : VrStatus();
    // The focus the interaction actually consulted (the injection's when armed,
    // the session's otherwise) - the lead, from the Fable read at merge.
    out[QStringLiteral("focused")] = interaction.activeSource() ? interaction.activeSource()->focused()
                                                          : (st.state == VrState::Focused);
    out[QStringLiteral("session")] = st.active;
    // THE ARMED THROW (§6 row L4), beside the hands that aimed it: armed,
    // valid, the landing and its normal, the segments actually drawn, and the
    // counts a suite brackets a gesture with.
    out[QStringLiteral("teleport")] = interaction.teleportReport();
    return out;
}

bool VrApi::step(const QVariantMap &options)
{
    syncInteractionSession();
    // A WORN SESSION IS STEPPED BY THE DRIVER (the lead, from the Fable read at
    // merge): under the Live policy the driver keeps ticking, so a console
    // `vr.step({frames:1000})` would integrate the wearer's REAL stick for 11 s
    // in one hop on top of the driver's own. A scripted step is for injected
    // input; with a real session and no injection it is refused, by name.
    if (interactionSessionActive && !interaction.injectionArmed())
        return refuse(QStringLiteral("vr.step: a worn session is stepped by the driver; "
                                     "inject input first (vr.inject) to step it from a script"));
    static const QStringList known = { "seconds", "frames" };
    for (auto it = options.constBegin(); it != options.constEnd(); ++it)
        if (!known.contains(it.key()))
            return fail(QStringLiteral("vr.step: unknown key '%1' — known keys are %2")
                            .arg(it.key(), known.join(QStringLiteral(", "))));
    // THE FRAME A SCRIPTED STEP CHARGES. Negative means "the nominal frame" to
    // VrInteraction (1/90 s, a headset's own period), which is what a step with
    // no clock in the room should cost; an explicit `seconds` is clamped by
    // vrorigin::frameSeconds below it, exactly like a driver-paced one.
    const double seconds = options.value(QStringLiteral("seconds"), -1.0).toDouble();
    const int frames = options.contains(QStringLiteral("frames"))
                           ? options.value(QStringLiteral("frames")).toInt()
                           : 1;
    if (frames < 1 || frames > 100000)
        return fail(QStringLiteral("vr.step: frames must be between 1 and 100000, not %1")
                        .arg(frames));
    // INSTALLED BY A STEP WHEN THERE IS NO SESSION TO INSTALL IT. Every
    // headless gesture gate is this case: no runtime, no loop, and the
    // interaction is nevertheless the real one.
    if (!interaction.installed()) interaction.begin();
    for (int i = 0; i < frames; ++i) interaction.step(float(seconds));
    // ...AND IT LETS GO WHEN THE HANDS DO (the lead's fix round, item 7). With
    // no session there is no lifecycle edge to end the interaction on: a script
    // that injected a hand, stepped, and withdrew it with `vr.inject(hand)`
    // left the last armed state standing — and the gizmo with it, sized for a
    // wearer who is not there, for the life of the process. `end()` is the same
    // call the session edge makes: it cancels a gesture in flight, disarms the
    // gizmo and clears the ray. A worn session is untouched (this path only
    // runs when there is none).
    if (!interactionSessionActive) {
        bool anyHand = false;
        for (unsigned i = 0; i < VrHandCount; ++i)
            if (interaction.handState(i).valid) { anyHand = true; break; }
        if (!anyHand && interaction.installed()) interaction.end();
    }
    return true;
}

QVariant VrApi::hover()
{
    syncInteractionSession();
    const VrInteraction::Hover h = interaction.hover();
    if (!h.hit || !h.node) return jsNull();
    QVariantMap out;
    out[QStringLiteral("hand")] =
        h.hand == VrHandRight ? QStringLiteral("right") : QStringLiteral("left");
    out[QStringLiteral("id")] = h.node->getGUID();
    out[QStringLiteral("name")] = h.node->getName();
    out[QStringLiteral("rootId")] = h.picked ? ScenePicker::pickRoot(h.picked)->getGUID()
                                             : h.node->getGUID();
    out[QStringLiteral("x")] = double(h.point.x());
    out[QStringLiteral("y")] = double(h.point.y());
    out[QStringLiteral("z")] = double(h.point.z());
    out[QStringLiteral("distance")] = double(h.distance);
    out[QStringLiteral("triangleIndex")] = h.triangleIndex;
    return out;
}

bool VrApi::select(const QVariantMap &options)
{
    syncInteractionSession();
    static const QStringList known = { "hand", "mode" };
    for (auto it = options.constBegin(); it != options.constEnd(); ++it)
        if (!known.contains(it.key()))
            return fail(QStringLiteral("vr.select: unknown key '%1' — known keys are %2")
                            .arg(it.key(), known.join(QStringLiteral(", "))));
    bool ok = false;
    const unsigned hand = handFrom(options.value(QStringLiteral("hand")), &ok);
    if (!ok)
        return fail(QStringLiteral("vr.select: hand must be \"left\" or \"right\""));
    const QString modeName =
        options.value(QStringLiteral("mode"), QStringLiteral("replace")).toString().trimmed().toLower();
    VrInteraction::SelectMode mode = VrInteraction::SelectMode::Replace;
    if (modeName == QLatin1String("toggle")) mode = VrInteraction::SelectMode::Toggle;
    else if (modeName == QLatin1String("add")) mode = VrInteraction::SelectMode::Add;
    else if (modeName != QLatin1String("replace"))
        return fail(QStringLiteral("vr.select: mode must be \"replace\", \"toggle\" or "
                                   "\"add\", not '%1'").arg(modeName));
    if (!interaction.select(hand, mode))
        return refuse(interaction.playerHosted()
                          ? QStringLiteral("vr.select: the PLAYER is hosting this session and "
                                           "the Player edits nothing — only locomotion runs")
                          : QStringLiteral("vr.select: nothing changed (no controller is "
                                           "reporting, or the ray is on nothing)"));
    return true;
}

namespace {

/// THE HAND'S NAME, for a refusal that has to say which one.
QString handName(unsigned hand)
{
    return hand == VrHandRight ? QStringLiteral("right") : QStringLiteral("left");
}

}   // namespace

bool VrApi::grab(const QVariantMap &options)
{
    syncInteractionSession();
    static const QStringList known = { "hand" };
    for (auto it = options.constBegin(); it != options.constEnd(); ++it)
        if (!known.contains(it.key()))
            return fail(QStringLiteral("vr.grab: unknown key '%1' — known keys are %2")
                            .arg(it.key(), known.join(QStringLiteral(", "))));
    bool ok = false;
    const unsigned hand = handFrom(options.value(QStringLiteral("hand")), &ok);
    if (!ok) return fail(QStringLiteral("vr.grab: hand must be \"left\" or \"right\""));
    if (!interaction.beginGrab(hand)) {
        if (interaction.playerHosted())
            return refuse(QStringLiteral("vr.grab: the PLAYER is hosting this session and the "
                                         "Player edits nothing — only locomotion runs"));
        // A THIRD MESSAGE, because the second one LIED (the lead's read, item
        // 9): a grab refused because that hand is ALREADY holding the object —
        // or because both hands are on it and there is no third — read
        // "nothing to grab", which sent a reader looking at their ray and
        // their selection for a refusal that was about neither.
        unsigned primary = 0, second = 0;
        if (interaction.gestureHands(&primary, &second)) {
            if (primary == second && hand == primary)
                return refuse(QStringLiteral("vr.grab: the %1 hand is already holding it — "
                                             "squeeze the OTHER hand to take it with both")
                                  .arg(handName(hand)));
            if (primary != second)
                return refuse(QStringLiteral("vr.grab: both hands are already on it (the %1 "
                                             "and the %2) — there is no third hand to add")
                                  .arg(handName(primary), handName(second)));
        }
        return refuse(QStringLiteral("vr.grab: nothing to grab (no controller is "
                                     "reporting, nothing is under the ray and nothing is "
                                     "selected), or a script owns the document"));
    }
    return true;
}

bool VrApi::release(const QVariantMap &options)
{
    syncInteractionSession();
    static const QStringList known = { "hand" };
    for (auto it = options.constBegin(); it != options.constEnd(); ++it)
        if (!known.contains(it.key()))
            return fail(QStringLiteral("vr.release: unknown key '%1' — known keys are %2")
                            .arg(it.key(), known.join(QStringLiteral(", "))));
    bool ok = false;
    const unsigned hand = handFrom(options.value(QStringLiteral("hand")), &ok);
    if (!ok) return fail(QStringLiteral("vr.release: hand must be \"left\" or \"right\""));
    if (!interaction.endGrab(hand)) {
        if (interaction.playerHosted())
            return refuse(QStringLiteral("vr.release: the PLAYER is hosting this session and "
                                         "the Player edits nothing — only locomotion runs"));
        // ...AND THE SAME LIE ON THE WAY OUT (the lead's read, item 9): a
        // release named on a hand that is not the one holding — the second
        // `vr.release({hand:"left"})` after a two-hand hold handed the object
        // back to the right — answered "no grab is running" while a gesture
        // plainly was.
        unsigned primary = 0, second = 0;
        if (interaction.gestureHands(&primary, &second))
            return refuse(QStringLiteral("vr.release: the %1 hand is not holding anything — the "
                                         "%2 hand is")
                              .arg(handName(hand), handName(primary)));
        return refuse(QStringLiteral("vr.release: no grab is running"));
    }
    return true;
}

// SESSION OVERRIDES OVER THE PROJECT'S SETTINGS (lane VR-WORLD-1).
//
// The DEFAULTS are the document's (`world.vr`, services/vrworld.h) and this
// verb keeps only what somebody overrode, by row id — so there is exactly one
// definition of every default and an override writes nothing to the project.
// The keys, their spellings, their ranges and their refusals all come from the
// same table `world.vr` and the World panel's VR section are generated from.
QVariantMap VrApi::locomotion(const QVariantMap &options)
{
    const QStringList known = vrworld::ids();
    for (auto it = options.constBegin(); it != options.constEnd(); ++it)
        if (!known.contains(it.key())) {
            fail(QStringLiteral("vr.locomotion: unknown key '%1' — known keys are %2")
                     .arg(it.key(), known.join(QStringLiteral(", "))));
            return QVariantMap();
        }
    // VALIDATED BEFORE ANYTHING IS APPLIED: a call with one bad value changes
    // nothing at all, exactly as `world.vr` refuses a whole write.
    QVector<QPair<QString, double>> writes;
    for (const vrworld::Row &r : vrworld::rows()) {
        if (!options.contains(r.id)) continue;
        // A ROW A SESSION LATCHED AT CREATION CANNOT BE OVERRIDDEN WHILE IT
        // RUNS (lane HANDS-SWITCH-1; `Row::sessionFixed`). The bare-hand
        // bindings are attached to the session's action sets before its first
        // frame and no runtime can be asked to rebind them, so accepting the
        // override would be a verb that answered "yes" and did nothing for the
        // life of the session. Refused by name, with what to call instead.
        if (r.sessionFixed) {
            fail(QStringLiteral("vr.locomotion: '%1' is fixed for the life of a session — set "
                                "it with world.vr({%1: ...}) for the project, or "
                                "vr.begin({%1: ...}) for one session").arg(r.id));
            return QVariantMap();
        }
        double value = 0.0;
        QString why;
        if (!vrworld::validate(r, options.value(r.id), value, why)) {
            fail(QStringLiteral("vr.locomotion: %1").arg(why));
            return QVariantMap();
        }
        writes.append({ r.id, value });
    }
    for (const auto &w : writes) vrworld::override(w.first, w.second);

    const vrworld::Settings loco =
        vrworld::resolve(moduleHost.viewport ? moduleHost.viewport->getScene()
                                             : iris::ScenePtr());
    QVariantMap out;
    for (const vrworld::Row &r : vrworld::rows()) out[r.id] = vrworld::valueOf(r, loco);
    // WHERE THE DEFAULTS CAME FROM and WHAT THIS SESSION CHANGED — the honest
    // answer to "why is it flying at 15": the project, unless it is listed here.
    out[QStringLiteral("overridden")] = vrworld::overridden();
    out[QStringLiteral("session")] = vrworld::adopted();
    return out;
}

bool VrApi::teleport(const QVariantMap &options)
{
    syncInteractionSession();
    static const QStringList known = { "to", "hand", "arm", "cancel" };
    for (auto it = options.constBegin(); it != options.constEnd(); ++it)
        if (!known.contains(it.key()))
            return fail(QStringLiteral("vr.teleport: unknown key '%1' — known keys are %2")
                            .arg(it.key(), known.join(QStringLiteral(", "))));
    const bool arm = options.value(QStringLiteral("arm"), false).toBool();
    const bool cancel = options.value(QStringLiteral("cancel"), false).toBool();
    const bool named = options.contains(QStringLiteral("to"));
    // THE FORMS ARE EXCLUSIVE, and saying so is cheaper than guessing which one
    // a caller meant: a map with both `to` and `arm` is a script with a bug in
    // it, and answering the wrong one silently is how that bug survives.
    if (int(arm) + int(cancel) + int(named) > 1)
        return fail(QStringLiteral("vr.teleport: say one of `to`, `arm` or `cancel`, not two"));
    // INSTALLED BY A VERB WHEN THERE IS NO SESSION TO INSTALL IT — the same
    // rule `vr.step` follows, and what makes every teleport case gateable on a
    // box with no runtime.
    if (!interaction.installed()) interaction.begin();

    if (named) {
        const QVariantMap to = options.value(QStringLiteral("to")).toMap();
        static const QStringList axes = { "x", "y", "z" };
        for (auto it = to.constBegin(); it != to.constEnd(); ++it)
            if (!axes.contains(it.key()))
                return fail(QStringLiteral("vr.teleport: to: unknown key '%1' — known keys are "
                                           "x, y, z").arg(it.key()));
        bool okX = true, okY = true, okZ = true;
        const double x = to.value(QStringLiteral("x"), 0.0).toDouble(&okX);
        const double y = to.value(QStringLiteral("y"), 0.0).toDouble(&okY);
        const double z = to.value(QStringLiteral("z"), 0.0).toDouble(&okZ);
        if (!okX || !okY || !okZ)
            return fail(QStringLiteral("vr.teleport: to: x, y and z must be numbers"));
        if (!interaction.teleportTo(iris::Vec3(float(x), float(y), float(z))))
            return refuse(QStringLiteral("vr.teleport: nobody is in VR (there is no rig to "
                                         "move without a session), or a host is still placing "
                                         "the wearer"));
        return true;
    }
    if (cancel) {
        if (!interaction.teleportCancel())
            return refuse(QStringLiteral("vr.teleport: no arc is armed"));
        return true;
    }
    if (arm) {
        bool ok = false;
        const unsigned hand = handFrom(options.value(QStringLiteral("hand")), &ok);
        // NAMING WHAT WAS PASSED, like `vr.inject` and unlike the older gesture
        // verbs beside it: a script that said "middle" wants to read "middle"
        // back rather than be told the two right answers.
        if (!ok)
            return fail(QStringLiteral("vr.teleport: hand must be \"left\" or \"right\", "
                                       "not '%1'")
                            .arg(options.value(QStringLiteral("hand")).toString()));
        if (!interaction.teleportArm(hand))
            return refuse(QStringLiteral("vr.teleport: cannot aim a throw (no controller is "
                                         "reporting for that hand, or a grab or a handle drag "
                                         "owns the frame)"));
        return true;
    }
    // THE REPORT IS READ BEFORE THE THROW, because taking one CLEARS it: the
    // refusal has to say which of three things went wrong, and two of them are
    // only knowable from the arc that was armed a moment ago.
    const QVariantMap armedNow = interaction.teleportReport();
    if (!interaction.teleportFire()) {
        if (!armedNow.value(QStringLiteral("armed")).toBool())
            return refuse(QStringLiteral("vr.teleport: no arc is armed (push the dominant "
                                         "thumbstick forward, or call "
                                         "vr.teleport({arm:true}))"));
        const QString reason = armedNow.value(QStringLiteral("reason")).toString();
        return refuse(reason.isEmpty()
                          ? QStringLiteral("vr.teleport: nobody is in VR — there is no rig to "
                                           "move without a session")
                          : QStringLiteral("vr.teleport: the landing was refused — %1")
                                .arg(reason));
    }
    return true;
}

QVariantMap VrApi::interactionMode()
{
    syncInteractionSession();
    return interaction.report();
}

QVariantMap VrApi::gizmo()
{
    syncInteractionSession();
    return interaction.gizmoReport();
}
