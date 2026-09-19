/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROJECTSERVICE_H
#define PROJECTSERVICE_H

// ProjectService — the project data flows (APP_ARCHITECTURE_AUDIT §3.3).
//
// The data halves of MainWindow's newProject/openProject/save* and of
// ProjectApi's create/open/remove: project rows, folders, the scene blob,
// tile thumbnails, saved-state bookkeeping. The UI flows (switching spaces,
// building the default scene, dock states) stay in the shell, which calls in
// here. ProjectApi calls the same methods — the audit's rule that the widget
// path and the scripting path share one implementation.
//
// QObject-free; headless-safe except where a method documents that it renders
// a thumbnail through the viewport.

#include <functional>

#include <QString>
#include <QStringList>

#include "irisgl/irisglfwd.h"
#include "irisgl/import/meshprewarm.h"

class Database;
class Project;
class ProjectManager;
class SettingsManager;
class IEditorViewport;
class UndoService;
class EditorData;

class ProjectService
{
public:
    ProjectService(Database *db,
                   Project *project,
                   SettingsManager *settings,
                   IEditorViewport *viewport,
                   UndoService *undo,
                   std::function<iris::ScenePtr()> sceneProvider);

    /// The desktop page arrives after the services (the shell builds services
    /// first so pages and modules can be constructed against them); wired as
    /// soon as the page exists.
    void setProjectManager(ProjectManager *pm) { projectManager = pm; }

    /// The user's projects root (default_directory setting or Documents).
    QString projectsRoot() const;

    /// Resolves a guid-or-exact-name to a project guid. Returns the guid, or
    /// empty when not found; *hits gets the number of name matches (>1 means
    /// ambiguous — the caller decides how to report it).
    QString resolveProjectGuid(const QString &guidOrName, QString *nameOut = nullptr,
                               int *hits = nullptr) const;

    /// The data half of project creation (was inline in ProjectApi::create):
    /// guid, current-project pointers, folder, DB row, desktop assignment.
    /// Returns the new guid, or empty when the DB rejects the row. The caller
    /// (shell or ProjectApi) follows with the new-scene UI flow.
    ///
    /// THE ONE CREATE ROUTE (owner review R1, 2026-09-18). The desktop page
    /// carried a SECOND copy of this — guid, folder, `db->createProject`,
    /// `updateProjectDesktop`, inline in ProjectManager::newProject — guarded
    /// by `if (!name.isEmpty() || !name.isNull())`, a condition that is TRUE
    /// for the empty string (it is not null) and therefore minted a nameless
    /// project from an emptied name box. Both are gone: the page calls this.
    ///
    /// `location` is the folder the project's own directory is created under
    /// (the Browse button's answer). Empty means the user's projects root —
    /// exactly what every caller got before the argument existed. It must name
    /// an existing, writable directory; `whyOut` says which of those it failed
    /// when the call returns empty, so a verb can refuse BY NAME instead of
    /// returning a bare false.
    QString createProjectShell(const QString &name, const QString &location = QString(),
                               QString *whyOut = nullptr);

    /// Points the current project at an existing project. NO preload: the
    /// open registers the session assets in its own slices, with a worker's
    /// parsed models in hand (OPEN-ASSIMP-1 — the `prepareOpen` that used to
    /// sit beside this and parse on the calling thread is gone).
    void pointAtProject(const QString &guid, const QString &name);

    /// Deletes a project: folder tree first (like the widget), then the DB
    /// rows through the guid-parameterised deletes — the current project is
    /// NOT mutated (SCRIPTING_SPEC §1.6.1). Refreshes the desktop.
    bool removeProject(const QString &guid);

    /// The reader half of openProject: reads the scene blob into a document
    /// scene. editorData/postMan are output parameters exactly as SceneReader
    /// hands them over.
    /// `prewarm` (optional) carries the model files a worker thread already
    /// parsed (irisgl/import/meshprewarm.h) — the reader then builds meshes
    /// out of ready iris::SceneSource parses instead of running the importer on this thread.
    iris::ScenePtr readProjectScene(EditorData **editorData,
                                    iris::PostProcessManagerPtr &postMan,
                                    const iris::MeshPrewarmPtr &prewarm = iris::MeshPrewarmPtr());

    /// The open PLAN for the threaded path: every model file the project's
    /// scene blob references, CAS-resolved. DB work, so it runs on the thread
    /// that owns the default connection — the caller's.
    QStringList plannedModelPaths() const;

    /// Blob-only save (SCRIPTING_SPEC §1.6.2): never silently no-ops; the
    /// thumbnail refreshes only when a viewport can render one.
    /// Run before every SCENE WRITE (all three saves below). The shell ends the
    /// live material hover preview here: the preview borrows a mesh's material
    /// slot without touching the document, and a save taken mid-hover would
    /// otherwise write the BORROWED material into the project — the one state
    /// the user never chose. A hook, like UndoService's, so this service stays
    /// free of the preview and headless hosts leave it unset.
    void setPreWriteHook(std::function<void()> hook) { mPreWrite = std::move(hook); }

    bool saveProjectBlob();

    /// The regular editor save: scene blob + viewport thumbnail + desktop
    /// tile. No-ops when the viewport never initialized (nothing to save).
    void saveOpenScene();

    /// First save of a fresh project into projectPath (was
    /// MainWindow::saveScene(filename, projectPath)).
    void saveInitialScene(const QString &projectPath);

    /// Screenshot -> scene thumbnail + desktop tile.
    void updateCurrentSceneThumbnail();

    /// Whether a project's scene is currently open in the editor (was
    /// UiManager::isSceneOpen — Phase 4 moved the state into the service
    /// that owns the open/close flow).
    bool isSceneOpen() const { return sceneOpen; }
    void setSceneOpen(bool open) { sceneOpen = open; }

    /// The one live Project instance (Phase 4: was the Globals::project
    /// static). Constructed by the shell and mutated in place — every holder
    /// of the aggregate reads the same object.
    Project *current() const { return project; }

private:
    bool sceneOpen = false;
    Database *db = nullptr;
    Project *project;
    ProjectManager *projectManager;
    SettingsManager *settings;
    IEditorViewport *viewport;
    UndoService *undo;
    std::function<iris::ScenePtr()> sceneProvider;
    std::function<void()> mPreWrite;
};

#endif // PROJECTSERVICE_H
