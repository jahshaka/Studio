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
#include "services/playerservice.h"
#include "services/services.h"
#include "viewport/enginerenderdriver.h"
#include "viewport/ieditorviewport.h"

using namespace jahshaka::engine;


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
          "Starts the VR session on the editor's scene and returns true once it exists. "
          "From the next frame the render loop is PACED BY THE RUNTIME (xrWaitFrame), both eyes "
          "are drawn in one pass into a target two eyes wide, each eye is copied into the "
          "runtime's swapchain and one projection layer is submitted.\n\n"
          "`mirror` is \"left\" (the default), \"right\", \"both\" or \"none\" — which half of "
          "the headset's picture is painted over the desktop viewport. `worldScale` is metres of "
          "world per metre of room (1 = life size). `eyeWidth`/`eyeHeight` override the size each "
          "eye is RENDERED at, for measurement only; the runtime's swapchains keep the runtime's "
          "size, so the copy scales.\n\n"
          "REFUSES (false, with app.lastError set) rather than throwing when VR is unavailable, "
          "when a session is already running, or when there is no scene yet.",
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
          "frames, rendered, ipd, mirror, worldScale, asymmetricFov, spaceChanges}",
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
          "session every field is at its zero and `state` is \"idle\" or \"unavailable\".",
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
    Engine *e = engine();
    if (!e) return refuse(QStringLiteral("vr.begin: no engine is running in this process"));
    if (!e->vrAvailable()) {
        const QString why = QString::fromStdString(e->vrInfo().reason);
        return refuse(QStringLiteral("vr.begin: VR is not available (%1)")
                          .arg(why.isEmpty() ? QStringLiteral("this process was not started "
                                                              "with --vr")
                                             : why));
    }
    if (e->vrStatus().active)
        return refuse(QStringLiteral("vr.begin: a session is already running"));
    Scene *scene = moduleHost.viewport ? moduleHost.viewport->engineScene() : nullptr;
    if (!scene) return refuse(QStringLiteral("vr.begin: there is no scene to show yet"));

    VrConfig cfg;
    cfg.mirror = vrnames::mirrorFrom(options.value(QStringLiteral("mirror")).toString(), cfg.mirror);
    if (options.contains(QStringLiteral("worldScale"))) {
        const double s = options.value(QStringLiteral("worldScale")).toDouble();
        if (s > 0.0) cfg.worldScale = float(s);
    }
    cfg.overrideEyeWidth  = options.value(QStringLiteral("eyeWidth"), 0).toUInt();
    cfg.overrideEyeHeight = options.value(QStringLiteral("eyeHeight"), 0).toUInt();
    if (!cfg.overrideEyeWidth != !cfg.overrideEyeHeight)
        return refuse(QStringLiteral("vr.begin: eyeWidth and eyeHeight are set together or "
                                     "not at all"));

    if (!e->beginVrSession(scene, cfg))
        return refuse(QStringLiteral("vr.begin: %1").arg(QString::fromStdString(e->lastError())));

    // THE MIRROR IS THE EDITOR'S OWN VIEWPORT (VR_SPEC §4.3): the desktop keeps
    // drawing its picture and the headset's left eye is painted over it. Phase
    // 3 moves this to the Player's widget, through the same call.
    if (moduleHost.viewport && cfg.mirror != VrMirrorMode::None) {
        std::vector<View *> views;
        e->listViews(views);
        for (View *v : views)
            if (v && !v->isOffscreen() && v->scene() == scene) { e->setVrMirrorView(v); break; }
    }
    // THE LOOP'S CLOCK IS THE RUNTIME NOW (VR_SPEC §4.3): zero interval, vsync
    // off, and renderOneFrame blocks in xrWaitFrame instead. Restored by end().
    if (moduleHost.engine && moduleHost.engine->driver())
        moduleHost.engine->driver()->setVrSessionActive(true);
    return true;
}

bool VrApi::end()
{
    Engine *e = engine();
    if (!e) return refuse(QStringLiteral("vr.end: no engine is running in this process"));
    if (!e->vrStatus().active) return refuse(QStringLiteral("vr.end: no session is running"));
    if (moduleHost.engine && moduleHost.engine->driver())
        moduleHost.engine->driver()->setVrSessionActive(false);
    e->setVrMirrorView(nullptr);
    qWarning("Jahshaka VR: vr.end() called");
    e->endVrSession();
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
    out[QStringLiteral("ipd")] = s.ipd;
    out[QStringLiteral("mirror")] = vrnames::mirror(s.mirror);
    out[QStringLiteral("worldScale")] = s.worldScale;
    out[QStringLiteral("asymmetricFov")] = s.asymmetricFov;
    out[QStringLiteral("spaceChanges")] = QVariant::fromValue(qulonglong(s.spaceChanges));
    return out;
}
