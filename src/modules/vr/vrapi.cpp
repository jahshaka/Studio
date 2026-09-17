/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/vr/vrapi.h"

#include "bridge/enginehost.h"
#include "bridge/vrnames.h"
#include "irisgl/mirror/scenemirror.h"
#include "viewport/flystep.h"
#include "services/playerservice.h"
#include "services/services.h"
#include "viewport/enginerenderdriver.h"
#include "viewport/ieditorviewport.h"

using namespace jahshaka::engine;

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
                [this] { pushProxies(); });
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
        { "begin", "vr.begin({mirror?, worldScale?, eyeWidth?, eyeHeight?}) -> bool",
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
          "`mirror` is \"none\" (THE DEFAULT HERE), \"left\", \"right\" or \"both\" — which half "
          "of the headset's picture is painted over the desktop viewport. It is off by default "
          "because the editor's own picture is the thing worth showing at the desk, and a mirror "
          "over it would pay for two renders and show one. `worldScale` is metres of world per "
          "metre of room (1 = life size). `eyeWidth`/`eyeHeight` override the size each eye is "
          "RENDERED at, for measurement only; the runtime's swapchains keep the runtime's size, "
          "so the copy scales.\n\n"
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
          "session or a suite walk a wearer through a world with no keyboard in the room. "
          "`player.vrMove` is the Player's half of the same gesture. False when the editor's "
          "preview is not running (a session somebody else started is not this verb's).",
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
        { "state",
          "vr.state() -> {active, state, runtime, version, space, eyeSize:[w,h], refreshHz, "
          "frames, rendered, ipd, mirror, worldScale, asymmetricFov, spaceChanges, head, "
          "hands:{left,right}, handActions, handJoints, proxies, preview}",
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
          "`preview` describes the editor's VR preview (see vr.begin).",
          Needs::Engine },
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
}

// LOCOMOTION AS A VERB, the editor's half (the Player's is player.vrMove).
// Both are callers of one piece of arithmetic (vrorigin::flyDelta) and neither
// is a second path into the engine.
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
    if (!editor.move(keys, float(seconds)))
        return refuse(QStringLiteral("vr.move: the editor's VR preview is not running "
                                     "(vr.state().preview.active says so)"));
    return true;
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
    out[QStringLiteral("ipd")] = s.ipd;
    out[QStringLiteral("mirror")] = vrnames::mirror(s.mirror);
    out[QStringLiteral("worldScale")] = s.worldScale;
    out[QStringLiteral("asymmetricFov")] = s.asymmetricFov;
    out[QStringLiteral("spaceChanges")] = QVariant::fromValue(qulonglong(s.spaceChanges));
    // THE POSES (phase 4). WORLD space, the rig applied — the only frame a
    // caller can reason in — and each one reports its own validity rather than
    // a shared flag: the head latches, a hand does not (VrPose's note).
    out[QStringLiteral("head")] = vrnames::pose(s.headPosition, s.headRotation, s.posesValid);
    QVariantMap hands;
    hands[QStringLiteral("left")] = vrnames::pose(s.hands[VrHandLeft]);
    hands[QStringLiteral("right")] = vrnames::pose(s.hands[VrHandRight]);
    out[QStringLiteral("hands")] = hands;
    out[QStringLiteral("handActions")] = s.handActions;
    out[QStringLiteral("handJoints")] = s.handJoints;
    out[QStringLiteral("proxies")] = showProxies;
    out[QStringLiteral("preview")] = editor.report();
    return out;
}
