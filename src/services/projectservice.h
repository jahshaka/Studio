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
#include <memory>

#include <QFuture>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include "io/missingclipref.h"

#include "irisgl/irisglfwd.h"
#include "irisgl/import/meshprewarm.h"

class Database;
class Project;
class ProjectManager;
class SettingsManager;
class IEditorViewport;
class UndoService;
class EditorData;
class QImage;
template <typename T> class QFutureWatcher;

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

    /// The user's projects root (default_directory setting or Documents). The
    /// DEFAULT — a project created through the New Scene dialog's Browse button
    /// can live somewhere else, and projectFolderFor below is what knows.
    QString projectsRoot() const;

    /// WHERE A PROJECT'S FOLDER IS — THE ONE RESOLVER (SMALL-UI-A fix round F1).
    ///
    /// `<location>/Projects/<guid>`, where `location` is the root recorded with
    /// the project row and the user's projects root when that is empty (which
    /// is every project that predates the Browse button, and every project
    /// created without it). Never empty: it always answers where the folder
    /// WOULD be.
    ///
    /// It exists because the path was hand-built from the DEFAULT root at seven
    /// sites — the desktop tile's open, export and delete, the archive import,
    /// pointAtProject, removeProject and the create — so a project created at a
    /// chosen location could not be found again: after a close and reopen it
    /// pointed at a folder that does not exist, baked maps and exports went to
    /// the wrong place and a delete left the real folder behind. Every one of
    /// those sites calls this now; there is no second copy of the expression.
    QString projectFolderFor(const QString &guid) const;

    /// Does this project record a location that is NOT THERE any more (an
    /// unplugged drive, a folder the user moved)? True with `whyOut` set to a
    /// sentence naming the path. False for every project on the default root —
    /// the default root is created on demand and is never "missing".
    ///
    /// Deliberately NOT folded into projectFolderFor: an open must REFUSE by
    /// name rather than fall back to the default root (which would silently
    /// open an empty world under the same guid) or recreate an empty folder
    /// (which would silently lose the user's work by making it look absent).
    bool projectLocationMissing(const QString &guid, QString *whyOut = nullptr) const;

    /// Resolves a guid-or-exact-name to a project guid. Returns the guid, or
    /// empty when not found; *hits gets the number of name matches. The guid
    /// is the identity and is tried first; a name is a convenience for a
    /// script (names are unique since PROJECT-NAMES-1, but a case-variant or a
    /// library written before it may still hold two — >1 stays ambiguous and
    /// the caller reports it).
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
    ///
    /// The CURRENT project is NOT re-pointed (CREATE-GAP-1): the world that is
    /// open is closed — and autosaved into its OWN row — by the caller, which
    /// then points at the new guid (`pointAtProject`). `folderOut` receives the
    /// new project's folder. The desktop gets the new project's tile here.
    QString createProjectShell(const QString &name, const QString &location = QString(),
                               QString *whyOut = nullptr, QString *folderOut = nullptr);

    /// THE NAME REFUSAL every naming door shows (PROJECT-NAMES-1): empty when
    /// `name` is free (for any project but `exceptGuid`), else a sentence
    /// naming the clash and the free name Database::nextFreeProjectName
    /// offers — "a project named 'A' already exists; 'A 2' is free". The
    /// create and the rename refuse with it; the dialogs show it.
    QString nameRefusal(const QString &name, const QString &exceptGuid = QString()) const;

    /// The free name `name` would become (Database::nextFreeProjectName) —
    /// what project.nextFreeName answers and what the dialogs offer.
    QString nextFreeName(const QString &name, const QString &exceptGuid = QString()) const;

    /// THE ONE RENAME (PROJECT-NAMES-1): refuses an empty name, an unknown
    /// guid and a name another project carries (nameRefusal), each by a
    /// sentence in `whyOut`; then writes the row. The callers follow with
    /// their own surfaces (the open project's caption, the tile's label).
    bool renameProject(const QString &guid, const QString &newName, QString *whyOut = nullptr);

    /// Points the current project at an existing project. NO preload: the
    /// open registers the session assets in its own slices, with a worker's
    /// parsed models in hand (OPEN-ASSIMP-1 — the `prepareOpen` that used to
    /// sit beside this and parse on the calling thread is gone).
    void pointAtProject(const QString &guid, const QString &name);

    /// THE ONE DELETE (STUDIO-HYGIENE-1 stage 2): what project.remove and the
    /// Desktop tile's Delete both call. Refuses, by a sentence in `whyOut`, an
    /// unknown guid, the OPEN project and a project whose recorded location is
    /// not there (a delete that cannot reach the files must not drop the rows
    /// that name them). Then the folder is RENAMED ASIDE (one rename(2) in its
    /// own Projects directory: atomic, and a failure leaves everything as it
    /// was), the rows go through the guid-parameterised deletes (the current
    /// project is NOT mutated, SCRIPTING_SPEC §1.6.1), the tile and any
    /// thumbnail encode in flight with them — and the BYTES are removed on a
    /// worker, never on the calling thread. The worker also takes any folder a
    /// killed process left aside in that directory.
    bool removeProject(const QString &guid, QString *whyOut = nullptr);
    /// Waits for every folder removal in flight (the shutdown paths and the
    /// destructor). Returns how many it waited for.
    int drainFolderDeletes();

    /// The model files the LAST readProjectScene found with no current bake
    /// (FORWARD-ONLY-1): missing from the open, never parsed. The shell raises
    /// one `model.missing` scene issue per file after it binds the scene.
    QStringList missingModels() const { return mMissingModels; }
    /// The skeletal clips the last open could not resolve (CLIP-REF-1).
    QVector<MissingClipRef> missingClips() const { return mMissingClips; }
    /// The open's pinned cascade rows past the renderer's table (SceneReader).
    int cascadeRowsDropped() const { return mCascadeRowsDropped; }

    /// The reader half of an open: builds the document scene from the scene
    /// blob the open runner's worker already read and parsed (`projectObj`,
    /// services/openreads.h — every open's catalog reads are the worker's).
    /// editorData is an output parameter exactly as SceneReader hands it over.
    /// `prewarm` carries the BAKES a worker thread already read
    /// (irisgl/import/meshprewarm.h); the reader never parses a model.
    iris::ScenePtr readProjectScene(const QJsonObject &projectObj,
                                    std::unique_ptr<EditorData> *editorData,
                                    const iris::MeshPrewarmPtr &prewarm);

    /// Run before every SCENE WRITE (save, below). The shell ends the
    /// live material hover preview here: the preview borrows a mesh's material
    /// slot without touching the document, and a save taken mid-hover would
    /// otherwise write the BORROWED material into the project — the one state
    /// the user never chose. A hook, like UndoService's, so this service stays
    /// free of the preview and headless hosts leave it unset.
    void setPreWriteHook(std::function<void()> hook) { mPreWrite = std::move(hook); }
    /// WHO TELLS THE USER a save failed: "the scene could not be saved: <why>"
    /// (a box for a person, a log line for a driven run — the shell's). A hook
    /// for the reason the pre-write one is; headless hosts leave it unset and
    /// read save()'s result.
    void setSaveFailedHook(std::function<void(const QString &)> hook) { mSaveFailed = std::move(hook); }

    /// Why a scene is written — the one thing the doors differ by.
    enum class SaveReason {
        Editor,   ///< Ctrl+S, the toolbar, the close's and the switch's auto-save, an export's pre-save:
                  ///< the tile is the next presented frame (asked for, encoded on a worker). With no
                  ///< world open: nothing to save, true. With no view (headless): the blob alone, when
                  ///< the scene is dirty (a clean one: true, nothing written).
        Create,   ///< a new project's first save: the tile asked for when a view exists.
        Script    ///< project.save: the row AND its tile written when it returns (one frame read now).
    };

    /// THE ONE SAVE DOOR (STUDIO-HYGIENE-1 stage 1): the open project's scene
    /// into its row, for every reason above — what Ctrl+S, the close, the
    /// switch, the export, the create and project.save all call. The hover
    /// preview ends first (setPreWriteHook); the walk, the encode and the one
    /// UPDATE run here, on the calling thread (stage 0 measured the walk at 65 %
    /// of a 21 MB save, so it stays synchronous until the document keeps its
    /// own snapshot — the portable project's spec). TRUE ONLY WHEN THE ROW WAS
    /// WRITTEN (Database::wroteOneProjectRow: one row, by the guid), and only
    /// then is the undo stack marked saved. A failure — no open scene, a guid
    /// that is not the open project's, a row that is gone or refused the write
    /// — returns false with `whyOut` set, tells the save-failed hook "the scene
    /// could not be saved: <why>", and leaves the stack DIRTY, so the close
    /// asks. Every door logs one SAVE line (SESSION_LOG_SPEC §5).
    bool save(const QString &guid, SaveReason reason, QString *whyOut = nullptr);

    /// The presented frame -> scene thumbnail + desktop tile, synchronously
    /// read (one frame), the encode on a worker.
    void updateCurrentSceneThumbnail();

    /// THE PROJECT TILE IS THE PRESENTED FRAME (CLOSE-SHOT-2): the window's
    /// picture (IEditorViewport::requestPresentedFrame — no second render, no
    /// settle) cropped about its centre to the tile's aspect and scaled to the
    /// stored size (Constants::TILE_SIZE * 2). Null in, null out.
    static QImage tileFromFrame(const QImage &frame);
    /// Satisfies a presented-frame request still pending (the shutdown path,
    /// before the last encodes are drained while the engine is alive).
    void settleThumbnailCapture();

    /// Encodes `img` as `guid`'s thumbnail PNG on a worker; on completion,
    /// on this thread, writes the row's thumbnail and the tile. A later call
    /// for the same guid supersedes an encode still in flight (the later
    /// picture wins). A null image stores nothing.
    void storeThumbnailLater(const QString &guid, const QImage &img);
    /// Waits for every thumbnail encode in flight and writes each — the
    /// shutdown path runs it while the database is still open. Returns how
    /// many it finished.
    int drainThumbnailEncodes();
    /// Encodes still in flight (the suites read it).
    int pendingThumbnailEncodes() const { return int(mThumbEncodes.size()); }

    /// Whether a project's scene is currently open in the editor (was
    /// UiManager::isSceneOpen — Phase 4 moved the state into the service
    /// that owns the open/close flow).
    bool isSceneOpen() const { return sceneOpen; }
    void setSceneOpen(bool open) { sceneOpen = open; }

    /// The one live Project instance (Phase 4: was the Globals::project
    /// static). Constructed by the shell and mutated in place — every holder
    /// of the aggregate reads the same object.
    Project *current() const { return project; }

    ~ProjectService();

private:
    void finishThumbnail(const QString &guid, QFutureWatcher<QByteArray> *watcher,
                         bool fromItsSignal);
    /// Drops `guid`'s encode in flight (a synchronous write or a delete
    /// replaces what it would have written).
    void supersedeThumbnail(const QString &guid);
    QHash<QString, QFutureWatcher<QByteArray> *> mThumbEncodes;
    /// The folder removals in flight (removeProject), and the aside paths they
    /// own — a later delete never hands a second worker the same folder.
    QList<QFuture<void>> mFolderDeletes;
    std::shared_ptr<QSet<QString>> mFoldersAside = std::make_shared<QSet<QString>>();
    std::shared_ptr<QMutex> mFoldersAsideLock = std::make_shared<QMutex>();
    /// What an asynchronous tile request holds instead of `this`: the viewport
    /// may answer after this service is gone.
    std::shared_ptr<char> mAlive = std::make_shared<char>(0);
    /// Asks the viewport for the presented frame and stores it as `guid`'s tile
    /// when it lands (storeThumbnailLater).
    void requestTile(const QString &guid);

    bool sceneOpen = false;
    Database *db = nullptr;
    Project *project;
    ProjectManager *projectManager;
    SettingsManager *settings;
    IEditorViewport *viewport;
    UndoService *undo;
    std::function<iris::ScenePtr()> sceneProvider;
    std::function<void()> mPreWrite;
    std::function<void(const QString &)> mSaveFailed;
    QStringList mMissingModels;
    QVector<MissingClipRef> mMissingClips;
    int mCascadeRowsDropped = 0;
};

#endif // PROJECTSERVICE_H
