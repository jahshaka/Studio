/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef VRAPI_H
#define VRAPI_H

// vr.* — the VR session's verbs (SPECS/VR_SPEC.md §4.6, phase 2).
//
// API-FIRST, and this file is the whole editor-facing surface of phase 2:
// there is no toolbar button, no menu item and no panel yet (phase 3 gives the
// Player one, and both call THESE verbs — SCRIPTING_SPEC §2.3). A session can
// be started, inspected and stopped from the console, from a script, from the
// MCP server and from the suite, which is what makes `vr.session` and
// `scripting.e2e.vr_verbs` able to test it at all.
//
// NOTHING HERE THROWS and nothing here hangs. A box with no runtime, a box
// whose headset is unplugged, a build with no OpenXR at all: `available()`
// answers a map with `available: false` and a reason in words, `begin()`
// refuses with false and records the reason as app.lastError. That is the
// refuse() contract, and it is the difference between "the editor works
// without a headset" being a claim and being a test.

#include <QElapsedTimer>
#include <QVariantMap>
#include <memory>

#include "jahshaka/engine/Engine.h"
#include "modules/studiomodule.h"
#include "modules/vr/editorvr.h"
#include "modules/vr/vrinteraction.h"
#include "scripting/apimodule.h"

class VrApi : public ApiModule
{
    Q_OBJECT
public:
    VrApi(ScriptHost &host, const ModuleHost &moduleHost);

    QString jsName() const override { return QStringLiteral("vr"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QVariantMap available();
    Q_INVOKABLE QVariantMap info();
    Q_INVOKABLE bool begin(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE bool end();
    Q_INVOKABLE QVariantMap state();
    Q_INVOKABLE bool toggle();
    Q_INVOKABLE bool proxies(const QVariant &on = QVariant());
    Q_INVOKABLE bool move(const QVariantMap &intent = QVariantMap());
    Q_INVOKABLE QVariantMap proxyPose(const QString &hand);
    Q_INVOKABLE bool inject(const QVariant &hand, const QVariantMap &state = QVariantMap());
    Q_INVOKABLE bool haptic(const QVariant &hand, double amplitude = 1.0,
                            double seconds = 0.05);

    // ---- STAGE 1: THE CONTROLLERS' INTERACTION (VR_INPUT_SPEC §2.4) -------
    //
    // API-FIRST, and in stage 1 the API is the WHOLE surface: there is no VR
    // menu, no palette and no button for any of this. The controller edges
    // inside VrInteraction call exactly these operations, the suites call them,
    // and the MCP calls them (SCRIPTING_SPEC §2.3).
    Q_INVOKABLE QVariantMap inputState();
    Q_INVOKABLE bool inputInject(const QVariantMap &state);
    Q_INVOKABLE QVariant hover();
    Q_INVOKABLE bool select(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE bool grab(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE bool release(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap locomotion(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap interactionMode();

    /// THE SHELL IS CLOSING (VR-4-FIX finding 7). Ends whatever this module
    /// started, through the object that owns it — the preview has a viewport's
    /// fly keys and helper state to give back, and VrModule::shutdown used to
    /// reach past it straight into the engine. Returns whether anything ended.
    bool endForShutdown();

private:
    /// The running engine, or null (headless runs, or before the engine starts).
    jahshaka::engine::Engine *engine() const;

    /// THE CONTROLLER PROXIES, PUSHED FOR ANY SESSION (owner, 2026-09-17):
    /// the editor's preview AND the Player's VR mode, because the wearer's own
    /// hands belong in both. The push is per frame, into the ONE SceneMirror
    /// the editor viewport owns — which is also the mirror the Player page
    /// syncs, the two being two views on one scene.
    void pushProxies();

    ModuleHost moduleHost;
    /// THE EDITOR'S VR PREVIEW (phase 4). Owned here because the verbs are the
    /// only way in and out of it; stepped from the render driver's beforeFrame,
    /// which this object connects to once.
    EditorVrPreview editor;
    // ---- STAGE 1 ---------------------------------------------------------
    /// Wires the interaction service to this session's services. Called once,
    /// from the constructor.
    void installInteraction();
    /// Installs or removes the interaction on the session's edge. Called from
    /// the driver's tick AND from every verb, because a script run stops the
    /// tick (see the implementation's note).
    void syncInteractionSession();
    /// ONE INTERACTION FRAME, from the driver's own tick — beside the proxies,
    /// and for the same reason they are there: a session may belong to the
    /// editor's preview or to the Player, and this is the one place above both
    /// that runs once per rendered frame.
    void stepInteraction();
    /// "left"/"right" -> a VrHand; the DOMINANT hand when the caller said
    /// nothing at all, and `*ok = false` for anything else (which the verbs
    /// turn into a throw, naming what was passed).
    unsigned handFrom(const QVariant &value, bool *ok = nullptr) const;

    /// THE WEARER'S CONTROLLERS ON THE SCENE (VR_INPUT_SPEC stage 1). Owned
    /// here because the verbs are the only way into it, exactly like the
    /// editor's preview above.
    VrInteraction interaction;
    /// The runtime-backed source; the injection store lives inside the service.
    VrEngineInput engineInput;
    /// Was a session active at the last tick — the edge that installs and
    /// removes the interaction.
    bool interactionSessionActive = false;
    /// The wall clock of the frame just gone, clamped by vrorigin::frameSeconds
    /// before it may move anybody (the rule the Player's VR mode follows).
    QElapsedTimer interactionClock;

    /// Are the controller proxies drawn at all? `vr.proxies(false)` is the
    /// off switch, and it survives a session ending — a user who turned the
    /// markers off does not want them back on the next time they put a headset
    /// on.
    bool showProxies = true;
};

#endif // VRAPI_H
