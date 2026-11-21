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
#include <QDateTime>
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
#include "player/editpose.h"
#include "player/iplayerhost.h"
#include "player/playback.h"

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
    // A FOLDER A KILLED DELETE LEFT ASIDE is swept at start-up, on a worker (the audit's W4): only a later
    // delete in the same directory swept it before, so a root nobody deleted from again kept it for ever.
    // (A project filed at a chosen location is swept by the next delete there.)
    if (settings) removeAsideFoldersLater(QDir(projectsRoot()).filePath(QStringLiteral("Projects")));
}

ProjectService::~ProjectService()
{
    drainFolderDeletes();   // the --script exit never reaches the shutdown order's drain
    // The shell drains at shutdown (ShellLifecycle::stopBackgroundWork), while the
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
// initial save 131 ms, of which the screenshot 25-36 and the rest the
// encode). The picture is the presented frame read back (CLOSE-SHOT-2,
// requestTile) and the QImage it hands back is ENCODED on a worker; the database write and the
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
    // Superseded: its bytes never land. DELETED, not deleteLater'd — nothing
    // pumps after ~MainWindow on the --script exit, and a QFutureWatcher's
    // destructor only disconnects (the worker finishes into its own future).
    delete mThumbEncodes.take(guid);
    auto *watcher = new QFutureWatcher<QByteArray>();
    mThumbEncodes.insert(guid, watcher);
    QObject::connect(watcher, &QFutureWatcher<QByteArray>::finished, watcher,
                     [this, guid, watcher]() { finishThumbnail(guid, watcher, true); });
    const QImage copy = img;        // implicitly shared, read-only on the worker
    watcher->setFuture(QtConcurrent::run([copy]() { return encodeThumbnailPng(copy); }));
}

void ProjectService::finishThumbnail(const QString &guid, QFutureWatcher<QByteArray> *watcher,
                                     bool fromItsSignal)
{
    if (mThumbEncodes.value(guid) != watcher) return;   // superseded meanwhile
    mThumbEncodes.remove(guid);
    watcher->disconnect();
    const QByteArray thumb = watcher->result();
    // Its own finished() is on the stack only when it called us; a drain (the
    // shutdown paths, where nothing pumps afterwards) deletes it outright.
    if (fromItsSignal) watcher->deleteLater();
    else delete watcher;
    if (thumb.isEmpty()) return;    // never wipe the tile with an empty PNG
    db->updateSceneThumbnail(guid, thumb);
    if (projectManager) projectManager->updateTile(guid, thumb);
}

void ProjectService::supersedeThumbnail(const QString &guid)
{
    delete mThumbEncodes.take(guid);    // see storeThumbnailLater
}

int ProjectService::drainThumbnailEncodes()
{
    int drained = 0;
    while (!mThumbEncodes.isEmpty()) {
        const QString guid = mThumbEncodes.constBegin().key();
        QFutureWatcher<QByteArray> *watcher = mThumbEncodes.constBegin().value();
        watcher->waitForFinished();
        finishThumbnail(guid, watcher, false);
        ++drained;
    }
    return drained;
}

QString ProjectService::projectsRoot() const
{
    // One rule, in services/apppaths.h: the data root when a run forces one
    // (so a scripted run stops writing project folders into the developer's
    // Documents), the `default_directory` preference otherwise.
    return AppPaths::projectsRoot(settings->get(settingkeys::defaultDirectory),
                                  Constants::PROJECT_FOLDER);
}

namespace {
/// Two spellings of one folder (a trailing slash, a symlinked root): the
/// canonical paths when both exist, the cleaned absolute ones otherwise.
bool sameFolder(const QString &a, const QString &b)
{
    const QFileInfo fa(a), fb(b);
    const QString ca = fa.canonicalFilePath(), cb = fb.canonicalFilePath();
    if (!ca.isEmpty() && !cb.isEmpty()) return ca == cb;
    return QDir::cleanPath(fa.absoluteFilePath()) == QDir::cleanPath(fb.absoluteFilePath());
}

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
    // ONE ROW, BY THE KEY (D11-LIBRARY-SCALE, audit V2_F F2): every project verb
    // resolves through here, and it used to load EVERY project row — scene and
    // thumbnail BLOBs included — to find one guid. A guid match first (guids are
    // unique; names may not be), then the rows with exactly that name.
    ProjectTileData row;
    if (db->fetchProjectTile(guidOrName, &row)) {
        if (nameOut) *nameOut = row.name;
        if (hits) *hits = 1;
        return row.guid;
    }
    const QVector<ProjectTileData> named = db->fetchProjectsNamed(guidOrName);
    if (hits) *hits = int(named.size());
    if (named.size() != 1) return QString();
    if (nameOut) *nameOut = named.first().name;
    return named.first().guid;
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

    if (const QString refusal = createRefusal(name, location); !refusal.isEmpty()) return fail(refusal);

    // WHERE IT LANDS. The user's projects root unless the dialog's Browse (or
    // the verb's `location`) named somewhere else (createRefusal judged it).
    QString root = location.trimmed();
    root = root.isEmpty() ? projectsRoot() : QDir(root).absolutePath();

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
    // project in the owner's library has. THE DIALOG PRE-FILLS ITS LOCATION
    // WITH THE DEFAULT ROOT, so a non-empty `location` is not a choice by
    // itself: it is one only when it names somewhere else (PROJECT-NAMES-1 —
    // every New Scene used to record the default root and so stopped
    // following it).
    if (!location.trimmed().isEmpty() && !sameFolder(root, projectsRoot()))
        db->setProjectLocation(guid, root);
    // THE TILE IS MADE WITH THE PROJECT (CREATE-GAP-1): one row, one tile —
    // the Desktop never has to rebuild to learn a project exists.
    projectManager->addTile(guid);
    if (folderOut) *folderOut = fullProjectPath;
    return guid;
}

QString ProjectService::createRefusal(const QString &name, const QString &location) const
{
    if (name.trimmed().isEmpty()) return QStringLiteral("a non-empty name is required");
    // ONE NAME, ONE PROJECT (PROJECT-NAMES-1): a taken name is refused with
    // the free one named, before anything is minted.
    if (const QString refusal = nameRefusal(name); !refusal.isEmpty()) return refusal;
    // WHERE IT LANDS: checked before a guid is minted or a pointer moves — a
    // create that fails half way through leaves `project` pointing at a folder
    // that does not exist.
    const QString root = location.trimmed();
    if (!root.isEmpty()) {
        const QFileInfo info(root);
        if (!info.exists()) return QStringLiteral("the location '%1' does not exist").arg(root);
        if (!info.isDir()) return QStringLiteral("the location '%1' is not a folder").arg(root);
        if (!info.isWritable()) return QStringLiteral("the location '%1' is not writable").arg(root);
    }
    return QString();
}

QString ProjectService::nextFreeName(const QString &name, const QString &exceptGuid) const
{
    return db ? db->nextFreeProjectName(name, exceptGuid) : name.trimmed();
}

QString ProjectService::nameRefusal(const QString &name, const QString &exceptGuid) const
{
    const QString wanted = name.trimmed();
    const QString free = nextFreeName(wanted, exceptGuid);
    if (wanted.isEmpty() || free == wanted) return QString();
    return QStringLiteral("a project named '%1' already exists; '%2' is free").arg(wanted, free);
}

bool ProjectService::renameProject(const QString &guid, const QString &newName, QString *whyOut)
{
    if (whyOut) whyOut->clear();
    const auto fail = [whyOut](const QString &why) {
        if (whyOut) *whyOut = why;
        return false;
    };
    if (!db) return fail(QStringLiteral("no library in this session"));
    if (newName.trimmed().isEmpty()) return fail(QStringLiteral("a non-empty name is required"));
    ProjectTileData row;
    if (!db->fetchProjectTile(guid, &row))
        return fail(QStringLiteral("no project with guid '%1'").arg(guid));
    if (const QString refusal = nameRefusal(newName, guid); !refusal.isEmpty()) return fail(refusal);
    if (!db->renameProject(guid, newName.trimmed()))
        return fail(QStringLiteral("the database rejected the rename"));
    return true;
}

void ProjectService::pointAtProject(const QString &guid, const QString &name)
{
    project->setProjectPath(projectFolderFor(guid), name);
    project->setProjectGuid(guid);
}

namespace {
/// The prefix of a project folder renamed aside for its removal, beside the
/// guid folders of the same Projects directory (a dot: no project's guid).
const QString kAsidePrefix = QStringLiteral(".removing-");
}   // namespace

bool ProjectService::removeProject(const QString &guid, QString *whyOut)
{
    if (whyOut) whyOut->clear();
    const auto fail = [whyOut](const QString &why) {
        if (whyOut) *whyOut = why;
        return false;
    };
    if (!db) return fail(QStringLiteral("no library in this session"));
    ProjectTileData row;
    if (guid.isEmpty() || !db->fetchProjectTile(guid, &row))
        return fail(QStringLiteral("no project with guid '%1'").arg(guid));
    if (sceneOpen && project && project->getProjectGuid() == guid)
        return fail(QStringLiteral("this project is open — close it first"));
    QString whyMissing;
    if (projectLocationMissing(guid, &whyMissing)) return fail(whyMissing);

    // THE FOLDER LEAVES AT ONCE, ITS BYTES ON A WORKER (STUDIO-HYGIENE-1). The
    // project's OWN folder, wherever it is (fix round F1), renamed aside in its
    // own directory: one rename(2), so the project is gone from disk the moment
    // this returns and a failure (a busy or read-only folder) leaves folder and
    // rows exactly as they were. The recursive delete — a web export is a
    // viewer, a glb and its textures — ran HERE, on the UI thread, and kept
    // going after a failure, leaving half a folder behind its kept rows.
    const QString folder = projectFolderFor(guid);
    const QFileInfo folderInfo(folder);
    const QString projectsDir = folderInfo.absolutePath();
    QString aside;
    if (folderInfo.exists()) {
        aside = QDir(projectsDir).filePath(
            kAsidePrefix + guid + QLatin1Char('-') + QString::number(QDateTime::currentMSecsSinceEpoch()));
        if (!QDir().rename(folder, aside))
            return fail(QStringLiteral("the project folder '%1' could not be moved aside for removal "
                                       "(is it open in another program?)")
                            .arg(folder));
    }

    // THE ROWS GO TOGETHER OR NOT AT ALL (the audit's W4): one transaction over the three deletes (their own
    // guards nest into it), and a failure puts the folder BACK where it was — a refused delete changes nothing.
    // Their results were ignored: a row delete that failed still returned true with the folder aside.
    bool rowsOk = false;
    {
        DbTransaction tx(db->getDb());
        bool foldersOk = true, assetsOk = true;
        const bool projectOk = db->deleteProject(guid);
        db->deleteFolderAndDependencies(guid, &foldersOk);
        db->deleteAssetAndDependencies(guid, &assetsOk);
        rowsOk = projectOk && foldersOk && assetsOk && tx.commit();
    }
    if (!rowsOk) {
        if (!aside.isEmpty() && !QDir().rename(aside, folder))
            qWarning("project delete: the rows of %s were kept but its folder could not be put back from '%s' "
                     "(the next start-up, or the next delete there, puts it back: a kept row claims it)",
                     qUtf8Printable(guid), qUtf8Printable(aside));
        return fail(QStringLiteral("the library refused to delete the project's rows; nothing was deleted"));
    }

    supersedeThumbnail(guid);
    if (projectManager) projectManager->removeTile(guid);
    removeAsideFoldersLater(projectsDir);
    return true;
}

void ProjectService::removeAsideFoldersLater(const QString &projectsDir)
{
    // Every folder aside in this directory that no worker owns yet: a delete's
    // own, and any a killed process left behind (one directory read, here).
    // ONLY A FOLDER NO ROW CLAIMS IS REMOVED (the final read's B3): a delete killed between its rename and its
    // commit, or a refused one whose put-back rename failed, leaves a LIVE project's folder aside under its kept
    // row; the name (`.removing-<guid>-<ms>`, removeProject) names the guid, and a claimed folder is renamed
    // back, never removed. A library that cannot be read (or none) sweeps nothing.
    if (!db) return;
    QStringList unclaimed;
    for (const QFileInfo &entry : QDir(projectsDir).entryInfoList(
             { kAsidePrefix + QLatin1Char('*') }, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)) {
        const QString path = entry.absoluteFilePath();
        const QString rest = entry.fileName().mid(kAsidePrefix.size());
        const qsizetype dash = rest.lastIndexOf(QLatin1Char('-'));
        const QString guid = dash > 0 ? rest.left(dash) : rest;
        bool readOk = false;
        if (db->hasProjectRow(guid, &readOk)) {
            const QString home = QDir(projectsDir).filePath(guid);
            if (QDir().rename(path, home))
                qWarning("project delete: '%s' is the folder of the project %s, whose row is kept: put back at '%s'",
                         qUtf8Printable(path), qUtf8Printable(guid), qUtf8Printable(home));
            else
                qWarning("project delete: '%s' is the folder of the project %s, whose row is kept, and it could not "
                         "be put back at '%s': left aside, never removed", qUtf8Printable(path), qUtf8Printable(guid),
                         qUtf8Printable(home));
            continue;
        }
        if (!readOk) {
            qWarning("project delete: the library could not be read, so no folder aside in '%s' is removed",
                     qUtf8Printable(projectsDir));
            return;
        }
        unclaimed.append(path);
    }
    QStringList owned;
    {
        QMutexLocker locked(mFoldersAsideLock.get());
        for (const QString &path : unclaimed) {
            if (mFoldersAside->contains(path)) continue;
            mFoldersAside->insert(path);
            owned.append(path);
        }
    }
    if (!owned.isEmpty()) {
        mFolderDeletes.removeIf([](const QFuture<void> &f) { return f.isFinished(); });
        mFolderDeletes.append(QtConcurrent::run(
            [owned, aside = mFoldersAside, lock = mFoldersAsideLock]() {
                for (const QString &path : owned) {
                    if (!QDir(path).removeRecursively())
                        qWarning("project delete: '%s' could not be removed in full; the next delete "
                                 "in its directory tries again", qUtf8Printable(path));
                    QMutexLocker locked(lock.get());
                    aside->remove(path);
                }
            }));
    }
}

int ProjectService::drainFolderDeletes()
{
    const int pending = int(mFolderDeletes.size());
    for (QFuture<void> &future : mFolderDeletes) future.waitForFinished();
    mFolderDeletes.clear();
    return pending;
}

iris::ScenePtr ProjectService::readProjectScene(const QJsonObject &projectObj,
                                                std::unique_ptr<EditorData> *editorData,
                                                const iris::MeshPrewarmPtr &prewarm)
{
    std::unique_ptr<SceneReader> reader(new SceneReader);
    reader->setDatabaseHandle(db);
    reader->setProject(project);
    reader->setPrewarm(prewarm);
    iris::ScenePtr scene = reader->readScene(project->getProjectFolder(), projectObj, editorData);
    mMissingModels = reader->missingModels();
    mMissingClips = reader->missingClips();
    mCascadeRowsDropped = reader->cascadeRowsDropped();
    return scene;
}

// THE SCENE SAVE IS ALREADY CRASH-ATOMIC — DO NOT "FIX" IT INTO SOMETHING THAT
// IS NOT. (STABILITY_PROGRAM_SPEC.md §1.4, Lane 2.)
//
// The blob does not go to a file; it goes to SQLite, as ONE statement —
// Database::updateProject / updateProjectBlob are a single
// `UPDATE projects SET scene=?, … WHERE guid=?` (in autocommit, or inside the
// script run's gesture batch, which commits it whole). The library runs in WAL
// mode with synchronous=NORMAL (Database::initializeDatabase,
// applyDurabilityPragmas — FSYNC-2): a committed transaction is in the -wal file
// and a recovery replays the -wal up to the last COMPLETE transaction, so a
// `kill -9` mid-UPDATE leaves the PREVIOUS blob intact by construction: there is
// no window in which the project row holds half a scene. (A power cut may lose
// the last few seconds of commits; it cannot tear one.)
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
bool ProjectService::save(const QString &guid, SaveReason reason, QString *whyOut)
{
    if (whyOut) whyOut->clear();
    QElapsedTimer clock;
    clock.start();
    QByteArray blob;
    QSet<QString> spawned;   // the nodes a run added: not part of the document this save writes
    const auto fail = [&](const QString &why) {
        if (whyOut) *whyOut = why;
        const QString sentence = QStringLiteral("the scene could not be saved: %1").arg(why);
        JahLog::write(JahLog::scene, JahLog::Level::Error,
                      QStringLiteral("=== SCENE SAVE === '%1' (%2) FAILED in %3 ms: %4")
                          .arg(project ? project->getProjectName() : QString(), guid)
                          .arg(clock.elapsed()).arg(why));
        if (mSaveFailed) mSaveFailed(sentence);
        return false;
    };

    {
        LoadTimeline::Accumulate row(QStringLiteral("save:preWrite"));
        if (mPreWrite) mPreWrite();      // a save writes the ORIGINAL material
    }
    const bool hasView = viewport && viewport->isInitialized();
    // AN EDITOR SAVE WITH NO WORLD OPEN (Ctrl+S on the Desktop, after a close or
    // a refused open: file.save works in any space) has nothing to save — a
    // success, silent, nothing written (the audit's W1: it popped a failure box).
    if (reason == SaveReason::Editor && !sceneOpen) return true;
    // ...and one with no view (a headless run: the NULL render system never
    // initializes one) writes the blob when there is something to write — the
    // close's auto-save rides this door, and a no-op here dropped a headless
    // run's edits at its close without a word. Clean: nothing to write.
    if (reason == SaveReason::Editor && !hasView && !(undo && undo->isDirty())) return true;

    if (!db) return fail(QStringLiteral("there is no library in this session"));
    if (guid.isEmpty() || !project || project->getProjectGuid() != guid)
        return fail(QStringLiteral("'%1' is not the open project").arg(guid));
    const iris::ScenePtr scene = sceneProvider ? sceneProvider() : iris::ScenePtr();
    if (!scene) return fail(QStringLiteral("there is no open scene to save"));

    {
        LoadTimeline::Accumulate row(QStringLiteral("save:serialize"));
        SceneWriter writer;
        // A SAVE DURING PLAY OR SIMULATE WRITES THE EDITED SCENE (STUDIO-HYGIENE-1, player/editpose.h): the
        // document's edit pose — whichever of the editor's and the Player's runs started first, else the bodies
        // Simulate holds — is swapped in for the walk and the moved pose put back on EVERY exit of this block
        // (the guard), with no frame between: the mirror reads only the dirty marks at its next sync and finds
        // the pose it had. A node a run added is left out. No command is pushed: the saved state (UndoService)
        // is the edit pose this writes, which is what Stop restores.
        QVector<editpose::Played<iris::SceneNodePtr, PlayBackNodeTransform>> played;
        struct PutBack {
            QVector<editpose::Played<iris::SceneNodePtr, PlayBackNodeTransform>> &played;
            ~PutBack() { editpose::putBack(played); }
        } putBack{ played };
        const auto edit = PlayBack::documentEditPose(
            scene, { viewport ? viewport->playBack() : nullptr, mPlayerHost ? mPlayerHost->playBack() : nullptr });
        if (!edit.pose.isEmpty()) {
            played = editpose::swapIn(scene->nodes.values(), edit.pose, edit.wholeDocument ? &spawned : nullptr);
            writer.setOmittedNodes(spawned);
        }
        blob = writer.getSceneObject(projectFolderFor(guid), scene,
                                     hasView ? viewport->getEditorData() : nullptr);
    }

    bool ok = false;
    QByteArray thumb;
    if (reason == SaveReason::Script && hasView) {
        // THE VERB'S TILE IS WRITTEN WITH ITS ROW (CREATE-GAP-1, CLOSE-SHOT-2):
        // project.save's contract is that the row, thumbnail included, is
        // written when it returns — the presented frame read now (one frame and
        // its fence, never a settle), encoded here. A null frame (a scene
        // mid-swap, a failed readback) never overwrites the stored tile.
        LoadTimeline::Accumulate row(QStringLiteral("save:thumbnail"));
        const QImage img = tileFromFrame(viewport->capturePresentedFrame());
        if (!img.isNull()) thumb = encodeThumbnailPng(img);
    }
    // ...and it supersedes an encode still in flight from an earlier save, which
    // would otherwise land an OLDER picture over this one.
    if (!thumb.isEmpty()) supersedeThumbnail(guid);
    {
        // "SAVED" MEANS COMMITTED (STUDIO-HYGIENE-1 audit). A script run holds the database's gesture batch for
        // its whole length, and the UPDATE inside it was counted (one row) and reported saved while its COMMIT
        // waited for the run's end: a kill or a failed batch commit later in the run left the row at the
        // previous scene with the scene marked clean. A TOP-LEVEL guard stands the batch down — it commits what
        // the batch holds and reopens at the next statement (DbTransaction, database.h) — and this guard's own
        // commit is the save's truth. WAL + synchronous=NORMAL: a commit is a WAL append, no fsync.
        LoadTimeline::Accumulate row(QStringLiteral("save:write"));
        DbTransaction tx(db->getDb());
        ok = thumb.isEmpty() ? db->updateProjectBlob(blob, guid) : db->updateProject(blob, thumb, guid);
        if (ok && !tx.commit()) ok = false;
    }
    if (!ok) {
        // THE ROW DOES NOT HOLD THE DOCUMENT, whatever the stack said: a create's world (its stack just
        // cleared) whose first save failed read CLEAN, and its close dropped it without a word.
        if (undo) undo->markUnsaved();
        ProjectTileData row;
        return fail(db->fetchProjectTile(guid, &row)
                        ? QStringLiteral("the library did not write its row (the log has the database's answer)")
                        : QStringLiteral("the project's row is no longer in the library"));
    }

    // ONLY A WRITTEN ROW IS A SAVED SCENE — and only a row that holds the WHOLE document (the final read's B2): a
    // node a run added was left out, so the scene stays unsaved (the close asks, or saves it after Stop, when the
    // node is part of the document). A user's Add and a simulation's spawn are not told apart.
    if (spawned.isEmpty()) {
        if (undo) undo->markSaved();
    } else {
        if (undo) undo->markUnsaved();
        const QString note = QStringLiteral("saved the scene as edited; %1 object(s) added during Play are saved "
                                            "when Play stops").arg(spawned.size());
        if (whyOut) *whyOut = note;
        JahLog::write(JahLog::scene, JahLog::Level::Warning,
                      QStringLiteral("=== SCENE SAVE === '%1' (%2): %3").arg(project->getProjectName(), guid, note));
    }
    if (projectManager) {
        if (!thumb.isEmpty()) projectManager->updateTile(guid, thumb);
        projectManager->touchTile(guid);
    }
    // THE TILE IS THE NEXT PRESENTED FRAME (CLOSE-SHOT-2) for the editor's and
    // the create's saves: asked for here, answered by the driver's next tick (or
    // by the close's settle) and encoded on a worker — the ask is all the save
    // pays. A create with no view yet takes it after its reveal (ProjectRunner).
    if (reason != SaveReason::Script && hasView) requestTile(guid);
    JahLog::write(JahLog::scene, JahLog::Level::Display,
                  QStringLiteral("=== SCENE SAVE === '%1' (%2) ok in %3 ms, blob %4 bytes")
                      .arg(project->getProjectName(), guid)
                      .arg(clock.elapsed()).arg(blob.size()));
    return true;
}

void ProjectService::updateCurrentSceneThumbnail()
{
    // The editor page is on its way out, so its view will draw no next tick:
    // the synchronous door (one frame, its fence). Never wipes the tile with an
    // empty PNG: a null frame stores nothing.
    storeThumbnailLater(project->getProjectGuid(),
                        tileFromFrame(viewport->capturePresentedFrame()));
}

QImage ProjectService::tileFromFrame(const QImage &frame)
{
    if (frame.isNull()) return QImage();
    const QSize tile = Constants::TILE_SIZE * 2;
    // The largest rectangle of the tile's aspect inside the frame, about its centre.
    const QSize crop = tile.scaled(frame.size(), Qt::KeepAspectRatio);
    const QRect r(QPoint((frame.width() - crop.width()) / 2, (frame.height() - crop.height()) / 2),
                  crop);
    return frame.copy(r).scaled(tile, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void ProjectService::requestTile(const QString &guid)
{
    if (!viewport || guid.isEmpty()) return;
    const std::weak_ptr<char> alive = mAlive;
    viewport->requestPresentedFrame([this, alive, guid](const QImage &frame) {
        if (alive.expired()) return;
        storeThumbnailLater(guid, tileFromFrame(frame));
    });
}

void ProjectService::settleThumbnailCapture()
{
    if (viewport) viewport->settlePresentedFrame();
}
