/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROJECTRUNNER_H
#define PROJECTRUNNER_H

// ProjectRunner — THE OPEN, THE CREATE AND THE CLOSE OF A WORLD, AS A RUNNER
// (D10-SHELL-MODULES; the ProjectService's runner).
//
// A world arrives in STAGES — the cover and the teardown of the previous world,
// the document read (its model parses on a worker, OPEN-ASSIMP-1), the bind to
// the viewports and the panels, the panels' rebuild, the uploads behind the
// cover, the reveal — and leaves through one close. This class owns the ORDER,
// the slices (services/sceneopenrunner.h: one stage per event-loop turn, a
// frame at every boundary), the budgets, the prewarm and the drains that keep
// two worlds from interleaving through one set of slices. What only the window
// can do — bind a scene to its panels, switch its page, dress its toolbar —
// is the Host's, so the order is written once and the shell keeps only the
// stage bodies.
//
// ORDER MATTERS HERE (the viewport desktop-bleed defect, 2026-09-03).
// Everything that can be done before the page switch IS done before it: the
// document read, the session registrations and the viewport's scene binding all
// happen while the desktop page — with its progress dialog — is still what the
// user sees. The page switch is the LAST step, and even then the engine has not
// put a frame of this world on screen yet, so the viewport wears its loading
// cover until it has — an overlay the ENGINE draws into the frame it was going
// to present anyway (irisgl/engine/src/OgreOverlayHud.cpp; owner decision D2).
// Without a cover, the viewport's native window shows whatever pixels were on
// that part of the screen before it was mapped: a copy of the desktop page.
//
// The synchronous open and the threaded one call exactly the same stages, in
// exactly the same order — that is what keeps `project.open()` and every
// headless script behaving as they always did.

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <memory>

#include "irisgl/irisglfwd.h"
#include "irisgl/import/meshprewarm.h"
#include "services/scenetemplate.h"

class Database;
class EditorData;
class IEditorViewport;
class Project;
class ProjectArchiver;
class ProjectService;
class SceneOpenRunner;
class SettingsManager;

class ProjectRunner : public QObject
{
    Q_OBJECT
public:
    /// THE WINDOW'S HALF of a world arriving and leaving (the shell implements it).
    class Host
    {
    public:
        virtual ~Host() = default;
        /// The previous world's teardown under the cover (the load-in-place path).
        virtual void teardownWorld() = 0;
        /// The world read from the project row, bound to the viewports and the
        /// panels that follow the scene; `editorData` is the scene's saved
        /// editor state (null for none).
        virtual void bindWorld(const iris::ScenePtr &scene, EditorData *editorData, bool playMode) = 0;
        /// A brand-new world (a create), bound the same way at the defaults.
        virtual void bindNewWorld(const iris::ScenePtr &scene) = 0;
        /// The new world of a create: the template's document.
        virtual iris::ScenePtr createWorld(SceneTemplate kind) = 0;
        /// The asset panel rebuilt for the world; `fresh` = a create's (its
        /// undo history starts empty and the title follows).
        virtual void buildPanels(bool fresh) = 0;
        /// The page switch, the root selection and autoplay. Always last. False
        /// when the reveal stopped early because the editor's view could not be
        /// created (the window was sent back to the Desktop).
        virtual bool revealWorld(bool playMode) = 0;
        /// The first thing a close does, BEFORE the drain of an open in flight
        /// (the hover preview's borrowed material goes back first).
        virtual void prepareClose() = 0;
        /// The close's whole teardown, the open's drain already done.
        /// `reopenInPlace` = the first half of an open: the page stays.
        virtual void closeWorld(bool reopenInPlace) = 0;
        /// The project's session membership (the Desktop page's).
        virtual QStringList plannedSessionModelPaths() = 0;
        virtual QStringList sessionAssetGuids() = 0;
        virtual void registerSessionAssets(const iris::MeshPrewarmPtr &prewarm) = 0;
        virtual void registerSessionAssetGuids(const QStringList &guids,
                                               const iris::MeshPrewarmPtr &prewarm) = 0;
        virtual void showOpenProgress(int percent, const QString &text) = 0;
        /// THE OPEN'S COMPILE LINE (SHADER-WARM-2): "Compiling shaders — n",
        /// called from INSIDE the warm-up frame after each shader it compiles on
        /// the UI thread — so it repaints the dialog itself and must not pump
        /// events (a frame inside a frame).
        virtual void showOpenCompileProgress(unsigned compiled) = 0;
        virtual void hideOpenProgress() = 0;
        /// The open world's force save (autosave), for an export of it.
        virtual void saveOpenWorld() = 0;
        /// The world that is bound to the window, or null.
        virtual iris::ScenePtr openWorld() const = 0;
    };

    ProjectRunner(Database *db, Project *project, ProjectService *projectService,
                  SettingsManager *settings, IEditorViewport *viewport, Host *host,
                  QObject *parent = nullptr);
    ~ProjectRunner() override;

    // ---- the open ----------------------------------------------------------
    /// The BLOCKING open: returns with the world open, which is the contract
    /// `project.open()` and every headless script are written against. Its
    /// MODEL PARSES run on a worker while this thread pumps (OPEN-ASSIMP-1).
    void open(bool playMode);
    /// The RESPONSIVE open: the same worker parse, and the install run one
    /// slice per event-loop turn so the window keeps pumping. Returns at once.
    void openAsync(bool playMode);
    /// True while an asynchronous open (or create) is in flight.
    bool isOpening() const;
    /// Runs an in-flight threaded open TO COMPLETION before the caller does
    /// anything else, with the event loop pumped (user input excluded). A
    /// caller about to close the project and point it at another world MUST
    /// call this FIRST. True when nothing is (or is still) in flight.
    bool waitForOpen();
    /// The open's slice-boundary counters (lane OPEN-FRAMES-1, app.openStats).
    unsigned sliceBoundaries() const;
    unsigned sliceBoundaryFrames() const { return mSliceBoundaryFrames; }

    // ---- the create ----------------------------------------------------------
    /// Creates the world of the project `guid` (a row createProjectShell has
    /// just made): closes the world that is open — its autosave lands in ITS
    /// OWN row — then points the current project at `guid` and runs the create.
    void create(const QString &guid, const QString &filename, const QString &projectPath,
                SceneTemplate kind);
    /// THE SAME CREATE, WITHOUT THE DRAIN (project.createAsync).
    void createAsync(const QString &guid, const QString &filename, const QString &projectPath,
                     SceneTemplate kind);

    // ---- the close -----------------------------------------------------------
    /// WHAT A CLOSE IS FOR (VIEW-REBUILD-1, 2026-09-21).
    ///
    /// `reopenInPlace` false is a close the user asked for: the world goes and
    /// the window lands on the Desktop, which is the only page left that means
    /// anything.
    ///
    /// `reopenInPlace` true is the FIRST HALF OF AN OPEN — every close-then-open
    /// caller (project.open, project.openAsync, a desktop tile, the
    /// import-and-open) tears the current world down through this same function
    /// before pointing the project at the next one. Measured on the rig
    /// (spikes/view-rebuild-1/): the space switch that ends a Desktop close hid
    /// the editor PAGE — a native X ancestor of the viewport's own window,
    /// because Qt gives every ancestor of a WA_NativeWindow widget a window of
    /// its own — so the viewport was UNVIEWABLE for 499-2,973 ms of an open in
    /// place, its rect walked five times as the docks came back, and nothing
    /// the engine drew (a cover, STALE-VIEW-1's background, the first frame of
    /// the new world) could reach a window that is not on screen. The user saw
    /// the app's watermark. The teardown is identical either way; only the page
    /// stays.
    ///
    /// An open in flight is drained first, then the host's teardown runs.
    void close(bool reopenInPlace);

    // ---- the export ----------------------------------------------------------
    /// THE ONE EXPORT OF A PROJECT BY GUID (threaded, the window's archiver):
    /// what the tile's Export and `desktop.exportTile` both run. Reads the
    /// project's ROW; the current project, the open world and its autosave are
    /// untouched — except that exporting the project that IS open first saves
    /// it, so the archive carries what is on screen. False (and `why`) when an
    /// archive operation is already running or the project is unknown.
    bool startExport(const QString &guid, const QString &zipPath, QString *why = nullptr);
    /// The progress dialog's Cancel.
    void cancelExport();

    // ---- teardown ------------------------------------------------------------
    /// The window close's settle: finish within the budget, then abandon.
    void settle(int budgetMs);
    /// Step 2 of the shutdown order: abort, then join. False = did not stop.
    bool stop(int budgetMs);

signals:
    /// An export is under way (the shell puts its progress up).
    void exportStarted();
    void exportProgress(int percent, const QString &text);
    /// `ok` false with `error` when it failed; `canceled` when the user stopped it.
    void exportFinished(bool canceled, bool ok, const QString &error);

private:
    void startRunnerIfNeeded();
    void startOpenRun(bool playMode);
    void startCreateRun(const QString &guid, const QString &filename, const QString &projectPath,
                        SceneTemplate kind);
    void stageBegin();
    void stageRead(const iris::MeshPrewarmPtr &prewarm);
    void stageBind(bool playMode);
    void stageReveal(bool playMode);
    QStringList plannedOpenModelPaths();
    /// The clip files the open's scene names (ProjectService::plannedClipPaths).
    QStringList plannedOpenClipPaths();
    iris::MeshPrewarmPtr prewarmModelsPumped();

    Database *mDb = nullptr;
    Project *mProject = nullptr;
    ProjectService *mProjectService = nullptr;
    SettingsManager *mSettings = nullptr;
    IEditorViewport *mViewport = nullptr;
    Host *mHost = nullptr;
    SceneOpenRunner *mRunner = nullptr;
    /// Counted here rather than in the runner because only the shell's
    /// viewport knows whether it drew.
    unsigned mSliceBoundaryFrames = 0;
    iris::ScenePtr mPendingScene;
    EditorData *mPendingEditorData = nullptr;
    /// The THREADED project-archive export (STABILITY_PROGRAM_SPEC Lane 4).
    /// Created on first use; the shutdown order's step 2 cancels and joins it
    /// (ProjectArchiver::shutdownArchives).
    ProjectArchiver *mArchiver = nullptr;
    /// The archiver's export target: a Project naming the row being exported,
    /// never the live one (an export used to re-point the live project).
    std::unique_ptr<Project> mExportTarget;
};

#endif // PROJECTRUNNER_H
