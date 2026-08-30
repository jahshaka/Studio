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

#include "irisgl/irisglfwd.h"

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
                   ProjectManager *projectManager,
                   SettingsManager *settings,
                   IEditorViewport *viewport,
                   UndoService *undo,
                   std::function<iris::ScenePtr()> sceneProvider);

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
    QString createProjectShell(const QString &name);

    /// Points the current project at an existing project and preloads its
    /// assets synchronously (the scripted open's data half).
    void prepareOpen(const QString &guid, const QString &name);

    /// Deletes a project: folder tree first (like the widget), then the DB
    /// rows through the guid-parameterised deletes — the current project is
    /// NOT mutated (SCRIPTING_SPEC §1.6.1). Refreshes the desktop.
    bool removeProject(const QString &guid);

    /// The reader half of openProject: reads the scene blob into a document
    /// scene. editorData/postMan are output parameters exactly as SceneReader
    /// hands them over.
    iris::ScenePtr readProjectScene(EditorData **editorData,
                                    iris::PostProcessManagerPtr &postMan);

    /// Blob-only save (SCRIPTING_SPEC §1.6.2): never silently no-ops; the
    /// thumbnail refreshes only when a viewport can render one.
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

private:
    bool sceneOpen = false;
    Database *db;
    Project *project;
    ProjectManager *projectManager;
    SettingsManager *settings;
    IEditorViewport *viewport;
    UndoService *undo;
    std::function<iris::ScenePtr()> sceneProvider;
};

#endif // PROJECTSERVICE_H
