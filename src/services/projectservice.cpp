/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/projectservice.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include "data/constants.h"
#include "services/apppaths.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "data/settingsmanager.h"
#include "irisgl/document/scenegraph/scene.h"
#include "viewport/ieditorviewport.h"
#include "io/scenereader.h"
#include "io/scenewriter.h"
#include "ui/pages/projectmanager.h"
#include "services/undoservice.h"
#include "services/loadtimeline.h"
#include "services/jahlog.h"

#include <QElapsedTimer>


ProjectService::ProjectService(Database *db,
                               Project *project,
                               SettingsManager *settings,
                               IEditorViewport *viewport,
                               UndoService *undo,
                               std::function<iris::ScenePtr()> sceneProvider)
    : db(db), project(project), projectManager(nullptr), settings(settings),
      viewport(viewport), undo(undo), sceneProvider(std::move(sceneProvider))
{
}

QString ProjectService::projectsRoot() const
{
    // One rule, in services/apppaths.h: the data root when a run forces one
    // (so a scripted run stops writing project folders into the developer's
    // Documents), the `default_directory` preference otherwise.
    return AppPaths::projectsRoot(settings->getValue("default_directory", QString()).toString(),
                                  Constants::PROJECT_FOLDER);
}

namespace {
/// THE ONE PLACE `Projects/<guid>` IS APPENDED TO A ROOT. See ProjectService's
/// header for the seven hand-built copies of this expression it replaced and
/// what they cost. Two callers: the resolver below, and the CREATE — which
/// cannot ask the resolver because the row whose location it would read does
/// not exist yet.
QString folderUnder(const QString &root, const QString &guid)
{
    return QDir(QDir(root).filePath(QStringLiteral("Projects"))).filePath(guid);
}
}   // namespace

QString ProjectService::projectFolderFor(const QString &guid) const
{
    const QString recorded = db ? db->projectLocation(guid).trimmed() : QString();
    return folderUnder(recorded.isEmpty() ? projectsRoot() : recorded, guid);
}

bool ProjectService::projectLocationMissing(const QString &guid, QString *whyOut) const
{
    if (whyOut) whyOut->clear();
    const QString recorded = db ? db->projectLocation(guid).trimmed() : QString();
    // No recorded location = the default root, which is created on demand.
    if (recorded.isEmpty()) return false;
    if (QFileInfo(recorded).isDir()) return false;
    if (whyOut)
        *whyOut = QStringLiteral("this project lives at '%1', which is not there — reconnect the "
                                 "drive or folder it is on and try again")
                      .arg(recorded);
    return true;
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

QString ProjectService::createProjectShell(const QString &name, const QString &location,
                                           QString *whyOut)
{
    // The ProjectManager::newProject flow minus the dialog (SCRIPTING_SPEC
    // §1.1): guid, current project, folder, DB row, desktop — the caller then
    // builds the default scene and saves it, so the row never carries the
    // empty scene blob.
    if (whyOut) whyOut->clear();
    const auto fail = [whyOut](const QString &why) {
        if (whyOut) *whyOut = why;
        return QString();
    };

    if (name.trimmed().isEmpty())
        return fail(QStringLiteral("a non-empty name is required"));

    // WHERE IT LANDS. The user's projects root unless the dialog's Browse (or
    // the verb's `location`) named somewhere else. Checked before a guid is
    // minted or a pointer moves: a create that fails half way through leaves
    // `project` pointing at a folder that does not exist.
    QString root = location.trimmed();
    if (root.isEmpty()) {
        root = projectsRoot();
    } else {
        const QFileInfo info(root);
        if (!info.exists())
            return fail(QStringLiteral("the location '%1' does not exist").arg(root));
        if (!info.isDir())
            return fail(QStringLiteral("the location '%1' is not a folder").arg(root));
        if (!info.isWritable())
            return fail(QStringLiteral("the location '%1' is not writable").arg(root));
        root = QDir(root).absolutePath();
    }

    const QString guid = GUIDManager::generateGUID();
    const QString fullProjectPath = folderUnder(root, guid);

    project->setProjectPath(fullProjectPath, name.trimmed());
    project->setProjectGuid(guid);

    QDir projectDir(fullProjectPath);
    if (!projectDir.exists() && !projectDir.mkpath("."))
        return fail(QStringLiteral("the project folder '%1' could not be created")
                        .arg(fullProjectPath));

    if (!db->createProject(guid, name.trimmed()))
        return fail(QStringLiteral("the database rejected the project row"));
    db->updateProjectDesktop(guid, projectManager->getCurrentDesktop());
    // AND WHERE IT WENT IS RECORDED (fix round F1). Only when the user CHOSE a
    // root: a project on the default root stores nothing, so it follows a
    // machine whose projects folder moves (a `default_directory` change, a
    // --data-root run) exactly as it always did — which is the behaviour every
    // project in the owner's library has.
    if (!location.trimmed().isEmpty()) db->setProjectLocation(guid, root);
    return guid;
}

void ProjectService::pointAtProject(const QString &guid, const QString &name)
{
    project->setProjectPath(projectFolderFor(guid), name);
    project->setProjectGuid(guid);
}

bool ProjectService::removeProject(const QString &guid)
{
    // THE PROJECT'S OWN FOLDER, wherever it is (fix round F1): this rebuilt the
    // path from the default root, so deleting a project created at a chosen
    // location dropped its rows and left the real folder on disk forever.
    QDir dirToRemove(projectFolderFor(guid));
    if (dirToRemove.exists() && !dirToRemove.removeRecursively()) return false;

    db->deleteProject(guid);
    db->deleteFolderAndDependencies(guid);
    db->deleteAssetAndDependencies(guid);

    if (projectManager) projectManager->populateDesktop(true);
    return true;
}

iris::ScenePtr ProjectService::readProjectScene(EditorData **editorData,
                                                iris::PostProcessManagerPtr &postMan,
                                                const iris::MeshPrewarmPtr &prewarm)
{
    std::unique_ptr<SceneReader> reader(new SceneReader);
    reader->setDatabaseHandle(db);
    reader->setProject(project);
    reader->setPrewarm(prewarm);

    postMan = iris::PostProcessManagerPtr();
    QByteArray blob;
    {
        LoadTimeline::Accumulate blobRead(QStringLiteral("db:sceneBlob"));
        blob = db->getSceneBlobGlobal(project->getProjectGuid());
    }
    iris::ScenePtr scene = reader->readScene(project->getProjectFolder(), blob,
                                             postMan, editorData);

    // A REPAIRED LOAD IS A DIRTY DOCUMENT (the GLB texture-loss defect,
    // io/scenereader.cpp): the reader healed texture slots that the stored
    // blob still has wrong, so what is in memory is right and what is on disk
    // is not. Nothing was pushed on the undo stack — there is no command — so
    // say it explicitly, or the close prompt discards the repair and the next
    // open pays for it again.
    if (reader->repairedTextureSlots() > 0) {
        JahLog::write(JahLog::scene, JahLog::Level::Display,
                      QStringLiteral("=== SCENE REPAIR === %1 texture slot(s) named an "
                                     "object instead of a texture; save to make it stick")
                          .arg(reader->repairedTextureSlots()));
        if (undo) undo->markContentRepaired();
    }
    return scene;
}

QStringList ProjectService::plannedModelPaths() const
{
    if (!db || !project || project->getProjectGuid().isEmpty()) return QStringList();
    SceneReader reader;
    reader.setDatabaseHandle(db);
    reader.setProject(project);
    QJsonObject projectObj;
    {
        LoadTimeline::Accumulate blobRead(QStringLiteral("db:sceneBlob"));
        projectObj = QJsonDocument::fromJson(
                         db->getSceneBlobGlobal(project->getProjectGuid())).object();
    }
    return reader.collectMeshSources(projectObj);
}

// THE SCENE SAVE IS ALREADY CRASH-ATOMIC — DO NOT "FIX" IT INTO SOMETHING THAT
// IS NOT. (STABILITY_PROGRAM_SPEC.md §1.4, Lane 2.)
//
// The blob does not go to a file; it goes to SQLite, as ONE statement —
// Database::updateProject / updateProjectBlob are a single
// `UPDATE projects SET scene=?, … WHERE guid=?` in autocommit, and there is no
// `PRAGMA journal_mode` anywhere in the tree, so SQLite runs its default
// rollback journal. A `kill -9` mid-UPDATE therefore leaves the PREVIOUS blob
// intact by construction: there is no window in which the project row holds
// half a scene.
//
// This is worth writing down because the property is invisible, unguarded and
// easy to destroy. Splitting the save into several statements (blob here,
// thumbnail there, a pin sweep after) reintroduces exactly the "the project
// opens empty after a crash" data loss the audit remembers — unless the whole
// sequence is wrapped in a DbTransaction (database.h). Anything that makes this
// multi-statement MUST do that.
//
// There is deliberately no test asserting this, because there is nothing
// honest for one to assert: a kill -9 gate here would be re-proving SQLite's
// atomicity, and it would still pass on a save that had been split into three
// unwrapped statements — the failure is a statement COUNT, which no runtime
// assertion can see. The guard is this comment and the review it asks for.
//
// The write in the save/store path that could NOT tear is the CAS object copy;
// that one is fixed at src/services/assetcas.cpp storeObject.
bool ProjectService::saveProjectBlob()
{
    // The SAVE BLOCK (SESSION_LOG_SPEC §5) is emitted from here, not from the
    // verb: this is what project.save calls AND what the UI save path calls, so
    // one record covers both.
    if (mPreWrite) mPreWrite();      // a save writes the ORIGINAL material
    QElapsedTimer clock;
    clock.start();
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
    // A null screenshot (engine scene mid-swap, or a readback failure) must
    // never overwrite the stored tile with an empty PNG — fall back to the
    // blob-only save.
    QImage img;
    if (viewport && viewport->isInitialized())
        img = viewport->takeScreenshot(Constants::TILE_SIZE * 2);
    if (!img.isNull()) {
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
    JahLog::write(JahLog::scene, ok ? JahLog::Level::Display : JahLog::Level::Error,
            QStringLiteral("=== SCENE SAVE === '%1' (%2) %3 in %4 ms, blob %5 bytes")
                .arg(project->getProjectName(), project->getProjectGuid(),
                     ok ? QStringLiteral("ok") : QStringLiteral("FAILED"))
                .arg(clock.elapsed()).arg(blob.size()));
    return ok;
}

void ProjectService::saveOpenScene()
{
    if (mPreWrite) mPreWrite();      // a save writes the ORIGINAL material
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
    if (img.isNull()) {                 // never store an empty tile
        db->updateProjectBlob(blob, project->getProjectGuid());
        undo->markSaved();
        return;
    }
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
    if (mPreWrite) mPreWrite();      // a save writes the ORIGINAL material
    SceneWriter writer;
    auto sceneObject = writer.getSceneObject(projectPath,
                                             sceneProvider(),
                                             iris::PostProcessManagerPtr(),
                                             viewport->isInitialized() ? viewport->getEditorData() : nullptr);

    // Headless (scripted project.create): the viewport never initialized — the
    // legacy widget's takeScreenshot would touch a GL context that isn't there.
    QByteArray thumb;
    if (viewport->isInitialized()) {
        // A 256-PIXEL TILE DOES NOT NEED GLOBAL ILLUMINATION (defect
        // 2026-09-08). takeScreenshot pushes the document's environment into
        // the shot view, and pushing it is what ARMS GI for the scene — so on a
        // brand-new project at the default Epic tier this line was the first
        // thing in the process to build a VCT volume and a per-pixel PCC probe
        // grid, synchronously, on the UI thread: measured 7.4 s inside
        // PccPerPixelGridPlacement::buildStart, all of it charged to
        // `project.create`, for a thumbnail in which not one probe is visible.
        //
        // So the first arm is DEFERRED, not skipped: the document's GI mode is
        // parked at OFF for the length of the shot and restored immediately
        // after, which leaves the live viewport's next environment push to arm
        // GI on a frame the user is actually waiting on rather than inside
        // project creation. The mirror pushes GI on CHANGE
        // (scenemirror.cpp:3330), so the restore is what re-arms it, and
        // nothing is torn down here: on a new project GI has never been built
        // when this runs.
        const iris::ScenePtr scene = sceneProvider();
        const iris::GiMode parkedGi = scene ? scene->giMode : iris::GiMode::OFF;
        if (scene) scene->giMode = iris::GiMode::OFF;
        auto img = viewport->takeScreenshot(Constants::TILE_SIZE * 2);
        if (scene) scene->giMode = parkedGi;
        if (!img.isNull()) {
            QBuffer buffer(&thumb);
            buffer.open(QIODevice::WriteOnly);
            img.save(&buffer, "PNG");
        }
    }

    db->updateProject(sceneObject, thumb, project->getProjectGuid());

    undo->markSaved();
}

void ProjectService::updateCurrentSceneThumbnail()
{
    auto img = viewport->takeScreenshot(Constants::TILE_SIZE * 2);
    if (img.isNull()) return;           // never wipe the tile with an empty PNG
    QByteArray thumb;
    QBuffer buffer(&thumb);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");

    db->updateSceneThumbnail(project->getProjectGuid(), thumb);
    projectManager->updateTile(project->getProjectGuid(), thumb);
}
