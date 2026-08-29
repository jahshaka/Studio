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
// Globals::project + UiManager, tests wire them to whatever they are testing.

#include <functional>

class MainWindow;
class Database;
class IEditorViewport;
class ProjectManager;
class QUndoStack;

struct ScriptHost
{
    MainWindow      *mainWindow = nullptr;
    Database        *db = nullptr;
    IEditorViewport *viewport = nullptr;        // editor viewport, if one exists
    ProjectManager  *projectManager = nullptr;
    QUndoStack      *undoStack = nullptr;       // for one-undo-step-per-script macros

    /// True when a project is open (a scene is loaded and Globals::project has a
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
