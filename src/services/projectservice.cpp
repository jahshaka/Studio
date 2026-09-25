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
#include <QFutureWatcher>
#include <QImage>
#include <QtConcurrent/QtConcurrentRun>


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

ProjectService::~ProjectService()
{
    // The shell drains at shutdown (shutdownBackgroundWork), while the
    // database is still open; anything still here is only waited for, never
    // written — the rows it would write may already be gone.
    for (auto it = mThumbEncodes.begin(); it != mThumbEncodes.end(); ++it) {
        it.value()->disconnect();
        it.value()->waitForFinished();
        delete it.value();
    }
    mThumbEncodes.clear();
}

// ---- THE THUMBNAIL ENCODE, OFF THE UI THREAD (CREATE-GAP-1) ----------------
//
// A save used to deflate its thumbnail PNG on the UI thread: ~100 ms of every
// close, every Ctrl+S and every create (measured: the create's
// `saveInitialScene` 131 ms, of which the screenshot 25-36 and the rest the
// encode). The screenshot stays where it is — it reads the GPU — and the
// QImage it hands back is ENCODED on a worker; the database write and the
// tile follow on this thread when the worker is done (the DB is SQLite on the
// UI thread; the worker never touches it).
//
// ONE ENCODE PER PROJECT: a second save of the same project while its first
// encode is in flight supersedes it — the older result is dropped on arrival,
// so the later picture always wins whichever worker finishes first.
static QByteArray encodeThumbnailPng(const QImage &img)
{
    QByteArray thumb;
    QBuffer buffer(&thumb);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    return thumb;
}

void ProjectService::storeThumbnailLater(const QString &guid, const QImage &img)
{
    if (guid.isEmpty() || img.isNull()) return;
    if (auto *old = mThumbEncodes.take(guid)) {
        old->disconnect();          // superseded: its bytes never land (the
        old->deleteLater();         // worker finishes into its own future)
    }
    auto *watcher = new QFutureWatcher<QByteArray>();
    mThumbEncodes.insert(guid, watcher);
    QObject::connect(watcher, &QFutureWatcher<QByteArray>::finished, watcher,
                     [this, guid, watcher]() { finishThumbnail(guid, watcher); });
    const QImage copy = img;        // implicitly shared, read-only on the worker
    watcher->setFuture(QtConcurrent::run([copy]() { return encodeThumbnailPng(copy); }));
}

void ProjectService::finishThumbnail(const QString &guid, QFutureWatcher<QByteArray> *watcher)
{
    if (mThumbEncodes.value(guid) != watcher) return;   // superseded meanwhile
    mThumbEncodes.remove(guid);
    watcher->disconnect();
    const QByteArray thumb = watcher->result();
    watcher->deleteLater();
    if (thumb.isEmpty()) return;    // never wipe the tile with an empty PNG
    db->updateSceneThumbnail(guid, thumb);
    if (projectManager) projectManager->updateTile(guid, thumb);
}

void ProjectService::supersedeThumbnail(const QString &guid)
{
    if (auto *old = mThumbEncodes.take(guid)) {
        old->disconnect();
        old->deleteLater();
    }
}

int ProjectService::drainThumbnailEncodes()
{
    int drained = 0;
    while (!mThumbEncodes.isEmpty()) {
        const QString guid = mThumbEncodes.constBegin().key();
        QFutureWatcher<QByteArray> *watcher = mThumbEncodes.constBegin().value();
        watcher->waitForFinished();
        finishThumbnail(guid, watcher);
        ++drained;
    }
    return drained;
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
                                           QString *whyOut, QString *folderOut)
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

    // THE CURRENT PROJECT IS NOT RE-POINTED HERE (CREATE-GAP-1). It was, and
    // the create closes the world that is open AFTER this returns — so the
    // close's autosave wrote the OLD world into the NEW project's row (and its
    // folder), and the old project lost every edit since its last save
    // (measured: 3 cubes added, create, reopen — gone). The shell re-points
    // after the close (MainWindow::startCreateRun, pointAtProject).
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
    // THE TILE IS MADE WITH THE PROJECT (CREATE-GAP-1): one row, one tile —
    // the Desktop never has to rebuild to learn a project exists.
    projectManager->addTile(guid);
    if (folderOut) *folderOut = fullProjectPath;
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

    supersedeThumbnail(guid);
    if (projectManager) projectManager->removeTile(guid);
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
    // THE VERB'S ENCODE STAYS SYNCHRONOUS (CREATE-GAP-1, stated): this is
    // project.save — a script's call, never a person's key, whose contract is
    // that the row (thumbnail included) is written when it returns. It
    // supersedes an encode still in flight from an earlier save, which would
    // otherwise land an OLDER picture over this one.
    const QString guid = project->getProjectGuid();
    supersedeThumbnail(guid);
    if (!img.isNull()) {
        const QByteArray thumb = encodeThumbnailPng(img);
        ok = db->updateProject(blob, thumb, guid);
        projectManager->updateTile(guid, thumb);
    } else {
        ok = db->updateProjectBlob(blob, guid);
    }
    projectManager->touchTile(guid);

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

    // The ledger's view of a save (CREATE-GAP-1): a create's closing save is
    // inside the create's run now, and these say where its time goes. They
    // are no-ops outside a run.
    const QString guid = project->getProjectGuid();
    QByteArray blob;
    {
        LoadTimeline::Accumulate row(QStringLiteral("saveOpen:serialize"));
        SceneWriter writer;
        blob = writer.getSceneObject(project->getProjectFolder(),
                                     sceneProvider(),
                                     iris::PostProcessManagerPtr(),
                                     viewport->getEditorData());
    }
    QImage img;
    {
        LoadTimeline::Accumulate row(QStringLiteral("saveOpen:thumbnail"));
        img = viewport->takeScreenshot(Constants::TILE_SIZE * 2);
    }
    {
        // THE ESSENTIAL WRITE IS SYNCHRONOUS: the scene is in the row before
        // this returns. Only the thumbnail's PNG encode leaves the thread.
        LoadTimeline::Accumulate row(QStringLiteral("saveOpen:write"));
        db->updateProjectBlob(blob, guid);
    }
    undo->markSaved();
    projectManager->touchTile(guid);
    // A null screenshot never replaces the stored tile with an empty PNG.
    if (!img.isNull()) storeThumbnailLater(guid, img);
}

void ProjectService::saveInitialScene(const QString &projectPath)
{
    {
        LoadTimeline::Accumulate row(QStringLiteral("save:preWrite"));
        if (mPreWrite) mPreWrite();  // a save writes the ORIGINAL material
    }
    SceneWriter writer;
    QByteArray sceneObject;
    {
        LoadTimeline::Accumulate row(QStringLiteral("save:serialize"));
        sceneObject = writer.getSceneObject(projectPath,
                                            sceneProvider(),
                                            iris::PostProcessManagerPtr(),
                                            viewport->isInitialized() ? viewport->getEditorData()
                                                                      : nullptr);
    }

    // Headless (scripted project.create): the viewport never initialized — the
    // legacy widget's takeScreenshot would touch a GL context that isn't there.
    QImage img;
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
        LoadTimeline::Accumulate shot(QStringLiteral("save:thumbnail"));
        img = viewport->takeScreenshot(Constants::TILE_SIZE * 2);
        shot.stop();
        if (scene) scene->giMode = parkedGi;
    }

    const QString guid = project->getProjectGuid();
    {
        LoadTimeline::Accumulate row(QStringLiteral("save:updateProject"));
        db->updateProjectBlob(sceneObject, guid);
    }

    undo->markSaved();
    projectManager->touchTile(guid);
    // The thumbnail's encode is a worker's (see storeThumbnailLater); a
    // headless create has no viewport and so no picture, and keeps the row's
    // empty thumbnail exactly as before.
    storeThumbnailLater(guid, img);
}

void ProjectService::updateCurrentSceneThumbnail()
{
    // Never wipes the tile with an empty PNG: a null shot stores nothing.
    storeThumbnailLater(project->getProjectGuid(),
                        viewport->takeScreenshot(Constants::TILE_SIZE * 2));
}
