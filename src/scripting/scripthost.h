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
#include <QString>

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

    /// THE RUN'S ONE UNDO ENTRY. A script run is one undo step — but the entry
    /// must not exist until the run actually records something, or a pure QUERY
    /// (describe the scene, read a property, any MCP tool call) leaves an EMPTY
    /// macro on the stack and eats the user's next Ctrl+Z. That laziness lives
    /// in the host's undo sink (UndoService), which is the one place every
    /// command in the app is pushed through, so ScriptEngine only says WHEN a
    /// run starts and ends. Unset in hosts with no undo sink: no macro then,
    /// and wrapUndoMacro has nothing to wrap.
    std::function<void(const QString &)> beginUndoMacro;
    std::function<void()> endUndoMacro;

    /// AFTER THE RUN, NOT BETWEEN TWO VERBS. Some verbs deliberately defer
    /// their effect "until the script has finished" and used to get that for
    /// free by posting a queued call: the UI thread was blocked inside the JS,
    /// so nothing queued could be delivered until the run returned. It is NOT
    /// free any more — the script runs on a worker and this thread pumps
    /// between verbs, so a queued call lands MID-RUN. (Found the hard way:
    /// app.quit() closed the main window, and with it the script engine and the
    /// host, while the run was still going — a SIGSEGV in app.shutdown_order.)
    /// Verbs that mean "afterwards" must say so through this. Unset means there
    /// is no run to wait for: the callback runs on the next event-loop turn.
    std::function<void(std::function<void()>)> afterRun;

    /// THE RENDER LOOP, for the length of one script run (SCRIPTING_LIVE_SPEC
    /// §3.1). Called with true when a run whose policy is Off begins and false
    /// when it ends: the driver skips its ticks in between, which is what a
    /// blocked UI thread used to give for free and what every frame-stepping
    /// test script still needs. Unset (the CLI's document-only hosts, the unit
    /// test) means there is no loop to suspend.
    std::function<void(bool)> driverSuspended;

    /// The last thing a verb refused or threw, whichever came last (ApiModule::
    /// refuse/fail). Read back by app.lastError(): a refusal answers with a
    /// falsy VALUE rather than an exception, so this is where the reason goes.
    /// One session, one slot: it is a diagnostic, not a queue.
    QString lastError;

    /// WHAT THE VERB JUST THREW, waiting to be rethrown in the script (see
    /// ApiModule::fail). Written by fail(), read and cleared by the script
    /// bridge around every single verb call, so it never outlives one. Distinct
    /// from lastError, which is a diagnostic a script can read back at leisure
    /// and which a refusal writes too.
    QString pendingError;

    bool isProjectOpen() const { return projectOpen && projectOpen(); }
    bool isEngineReady() const { return engineReady && engineReady(); }
};

#endif // SCRIPTHOST_H
