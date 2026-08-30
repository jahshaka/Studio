/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "projectservice.h"

#include <QBuffer>
#include <QDir>
#include <QStandardPaths>

#include "../constants.h"
#include "../core/database/database.h"
#include "../core/guidmanager.h"
#include "../core/project.h"
#include "../core/settingsmanager.h"
#include "../editor/ieditorviewport.h"
#include "../io/scenereader.h"
#include "../io/scenewriter.h"
#include "ui/pages/projectmanager.h"
#include "undoservice.h"


ProjectService::ProjectService(Database *db,
                               Project *project,
                               ProjectManager *projectManager,
                               SettingsManager *settings,
                               IEditorViewport *viewport,
                               UndoService *undo,
                               std::function<iris::ScenePtr()> sceneProvider)
    : db(db), project(project), projectManager(projectManager), settings(settings),
      viewport(viewport), undo(undo), sceneProvider(std::move(sceneProvider))
{
}

QString ProjectService::projectsRoot() const
{
    const auto spath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                       + Constants::PROJECT_FOLDER;
    return settings->getValue("default_directory", spath).toString();
}

QString ProjectService::resolveProjectGuid(const QString &guidOrName, QString *nameOut,
                                           int *hits) const
{
    if (hits) *hits = 0;
    const auto projects = db->fetchProjects(0);
    // guid match first (guids are unique; names may not be)
    for (const auto &p : projects) {
        if (p.guid == guidOrName) {
            if (nameOut) *nameOut = p.name;
            if (hits) *hits = 1;
            return p.guid;
        }
    }
    QString found, foundName;
    int matches = 0;
    for (const auto &p : projects) {
        if (p.name == guidOrName) { found = p.guid; foundName = p.name; ++matches; }
    }
    if (hits) *hits = matches;
    if (matches > 1) return QString();
    if (nameOut) *nameOut = foundName;
    return found;
}

QString ProjectService::createProjectShell(const QString &name)
{
    // The ProjectManager::newProject flow minus the dialog (SCRIPTING_SPEC
    // §1.1): guid, current project, folder, DB row, desktop — the caller then
    // builds the default scene and saves it, so the row never carries the
    // empty scene blob.
    const QString guid = GUIDManager::generateGUID();
    const QString fullProjectPath = QDir(QDir(projectsRoot()).filePath("Projects")).filePath(guid);

    project->setProjectPath(fullProjectPath, name.trimmed());
    project->setProjectGuid(guid);

    QDir projectDir(fullProjectPath);
    if (!projectDir.exists()) projectDir.mkpath(".");

    if (!db->createProject(guid, name.trimmed())) return QString();
    db->updateProjectDesktop(guid, projectManager->getCurrentDesktop());
    return guid;
}

void ProjectService::prepareOpen(const QString &guid, const QString &name)
{
    project->setProjectPath(
        QDir(QDir(projectsRoot()).filePath("Projects")).filePath(guid), name);
    project->setProjectGuid(guid);

    // Synchronous preload (no modal dialog, no QtConcurrent).
    projectManager->loadProjectAssetsSync();
}

bool ProjectService::removeProject(const QString &guid)
{
    QDir dirToRemove(QDir(QDir(projectsRoot()).filePath("Projects")).filePath(guid));
    if (dirToRemove.exists() && !dirToRemove.removeRecursively()) return false;

    db->deleteProject(guid);
    db->deleteFolderAndDependencies(guid);
    db->deleteAssetAndDependencies(guid);

    if (projectManager) projectManager->populateDesktop(true);
    return true;
}

iris::ScenePtr ProjectService::readProjectScene(EditorData **editorData,
                                                iris::PostProcessManagerPtr &postMan)
{
    std::unique_ptr<SceneReader> reader(new SceneReader);
    reader->setDatabaseHandle(db);

    postMan = iris::PostProcessManagerPtr();
    return reader->readScene(project->getProjectFolder(),
                             db->getSceneBlobGlobal(project->getProjectGuid()),
                             postMan,
                             editorData);
}

bool ProjectService::saveProjectBlob()
{
    // The blob-only save (SCRIPTING_SPEC §1.6.2). Unlike saveOpenScene() this
    // NEVER silently no-ops: the scene lives only in the DB projects table,
    // and a scripted or headless save must actually write it. The thumbnail
    // is refreshed only when a viewport can render one (and kept otherwise).
    auto scene = sceneProvider ? sceneProvider() : iris::ScenePtr();
    if (!scene || project->getProjectGuid().isEmpty()) return false;

    SceneWriter writer;
    auto blob = writer.getSceneObject(project->getProjectFolder(),
                                      scene,
                                      iris::PostProcessManagerPtr(),
                                      (viewport && viewport->isInitialized()) ? viewport->getEditorData() : nullptr);

    bool ok;
    if (viewport && viewport->isInitialized()) {
        auto img = viewport->takeScreenshot(Constants::TILE_SIZE * 2);
        QByteArray thumb;
        QBuffer buffer(&thumb);
        buffer.open(QIODevice::WriteOnly);
        img.save(&buffer, "PNG");
        ok = db->updateProject(blob, thumb, project->getProjectGuid());
        projectManager->updateTile(project->getProjectGuid(), thumb);
    } else {
        ok = db->updateProjectBlob(blob, project->getProjectGuid());
    }

    undo->markSaved();
    return ok;
}

void ProjectService::saveOpenScene()
{
    // if the viewport isnt initialized then the scene was never opened in
    // edit mode. This also means no renderer was initialized. There's no
    // need to save (nick)
    if (!viewport->isInitialized()) return;

    SceneWriter writer;
    auto blob = writer.getSceneObject(project->getProjectFolder(),
                                      sceneProvider(),
                                      iris::PostProcessManagerPtr(),
                                      viewport->getEditorData());

    auto img = viewport->takeScreenshot(Constants::TILE_SIZE * 2);
    QByteArray thumb;
    QBuffer buffer(&thumb);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");

    db->updateProject(blob, thumb, project->getProjectGuid());
    projectManager->updateTile(project->getProjectGuid(), thumb);

    undo->markSaved();
}

void ProjectService::saveInitialScene(const QString &projectPath)
{
    SceneWriter writer;
    auto sceneObject = writer.getSceneObject(projectPath,
                                             sceneProvider(),
                                             iris::PostProcessManagerPtr(),
                                             viewport->isInitialized() ? viewport->getEditorData() : nullptr);

    // Headless (scripted project.create): the viewport never initialized — the
    // legacy widget's takeScreenshot would touch a GL context that isn't there.
    QByteArray thumb;
    if (viewport->isInitialized()) {
        auto img = viewport->takeScreenshot(Constants::TILE_SIZE * 2);
        QBuffer buffer(&thumb);
        buffer.open(QIODevice::WriteOnly);
        img.save(&buffer, "PNG");
    }

    db->updateProject(sceneObject, thumb, project->getProjectGuid());

    undo->markSaved();
}

void ProjectService::updateCurrentSceneThumbnail()
{
    auto img = viewport->takeScreenshot(Constants::TILE_SIZE * 2);
    QByteArray thumb;
    QBuffer buffer(&thumb);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");

    db->updateSceneThumbnail(project->getProjectGuid(), thumb);
    projectManager->updateTile(project->getProjectGuid(), thumb);
}
