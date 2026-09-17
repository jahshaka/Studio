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

#include <QVariantMap>
#include <memory>

#include "jahshaka/engine/Engine.h"
#include "modules/studiomodule.h"
#include "modules/vr/editorvr.h"
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
    /// Are the controller proxies drawn at all? `vr.proxies(false)` is the
    /// off switch, and it survives a session ending — a user who turned the
    /// markers off does not want them back on the next time they put a headset
    /// on.
    bool showProxies = true;
};

#endif // VRAPI_H
