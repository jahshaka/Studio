/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTHOST_H
#define SCRIPTHOST_H

// ScriptHost — the ONE context struct every ApiModule receives (SCRIPTING_SPEC §2).
//
// All members are nullable: the same modules run inside the full editor, in the
// --script CLI, and in headless unit tests, each filling in what it has. Verbs
// never assume a member is present — they call requireProject()/requireEngine()
// (ApiModule) which throw a catchable JS error instead of crashing (§1.6.1).
//
// The precondition probes are std::functions rather than direct Globals/UiManager
// reads so the scripting core has NO Studio dependencies: the app wires them to
// the live project + services, tests wire them to whatever they are testing.

#include <functional>

class ApiRegistry;
class MainWindow;
class Database;
class Project;
class IEditorViewport;
class ProjectManager;
class QUndoStack;
struct StudioServices;

struct ScriptHost
{
    MainWindow      *mainWindow = nullptr;
    Database        *db = nullptr;
    /// The one live Project instance (Phase 4: was the Globals::project
    /// static). NULLABLE: hosts that drive only the scripting core leave it
    /// unset, so verbs that touch it must null-check (the modules do).
    Project         *project = nullptr;
    IEditorViewport *viewport = nullptr;        // editor viewport, if one exists
    ProjectManager  *projectManager = nullptr;
    QUndoStack      *undoStack = nullptr;       // for one-undo-step-per-script macros

    /// The service layer (src/services/services.h) — the ApiModules' verbs
    /// call these, not MainWindow (APP_ARCHITECTURE_AUDIT §3.2/§3.3). Null in
    /// hosts that have no services (the scripting core stays Studio-free:
    /// this is a forward declaration only).
    StudioServices  *services = nullptr;

    /// The registry the modules were installed into. Set by ScriptEngine's
    /// constructor, so it is non-null in every host that has a ScriptEngine —
    /// including the headless CLI runs. It exists for ONE reason: a verb that
    /// reports the registry's own metadata problems (app.apiProblems), so
    /// ApiRegistry::validate() finally runs over the REAL module set instead
    /// of only over the scripting unit test's fake module
    /// (AI_SURFACE_PROGRAM_SPEC §2.0). Nothing else should reach for it — a
    /// verb that needs another verb should call the module, not the registry.
    ApiRegistry     *registry = nullptr;

    /// True when a project is open (a scene is loaded and `project` has a
    /// guid). Unset = false: verbs that requireProject() fail cleanly.
    std::function<bool()> projectOpen;

    /// True when the engine viewport is live (engine started, view created).
    /// Unset = false: verbs that requireEngine() fail cleanly.
    std::function<bool()> engineReady;

    /// Called with true/false around the per-run undo macro so the app can
    /// guard operations that must not run inside an open macro (e.g.
    /// UiManager::clearUndoStack). Optional.
    std::function<void(bool)> macroOpenChanged;

    bool isProjectOpen() const { return projectOpen && projectOpen(); }
    bool isEngineReady() const { return engineReady && engineReady(); }
};

#endif // SCRIPTHOST_H
