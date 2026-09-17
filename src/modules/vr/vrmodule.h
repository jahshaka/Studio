/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef VRMODULE_H
#define VRMODULE_H

// VrModule — the VR feature domain (SPECS/VR_SPEC.md §4.6, phase 2).
//
// A StudioModule with NO PAGE, deliberately: phase 2 delivers the session and
// its verbs, and the UI is phase 3's (the Player's VR mode) and phase 4's (the
// editor preview). New feature domains land as modules rather than MainWindow
// wiring, and a module whose whole interface today is five verbs is still a
// module — that is what makes the UI, when it comes, a caller of the same verbs
// instead of a second path into the engine.
//
// It also owns the session's LIFETIME against the shell's: `shutdown()` ends a
// running session, because a session outliving the editor's scene would hold a
// View and a workspace on a target that is about to die.

#include <QPointer>

#include "modules/studiomodule.h"

class VrApi;

class VrModule : public StudioModule
{
public:
    QString id() const override { return QStringLiteral("vr"); }
    void initialize(ModuleHost &host) override { this->host = host; }
    void registerApi(ScriptEngine &engine) override;
    void shutdown() override;
    /// THE VR BUTTON ON THE EDITOR PAGE (the owner, 2026-09-17, at the first
    /// controller smoke: "the VR button takes me to the Player"): from the
    /// editor page the button is the EDITOR PREVIEW (vr.begin / vr.end), from
    /// the Player page it is the Player's run. Both call the same verbs the
    /// console calls. Returns the preview's state after the toggle.
    bool toggleEditorPreview();
    bool isEditorPreviewActive() const;

private:
    ModuleHost host;
    /// The module's ApiModule, owned by the ScriptEngine — held weakly so
    /// shutdown() can end a session through the object that owns it.
    QPointer<VrApi> api;
};

#endif // VRMODULE_H
