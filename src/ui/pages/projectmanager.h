/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include <QDialog>
#include <QFutureWatcher>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPointer>
#include <QStringList>
#include <QWidget>

#include <optional>

#include "irisgl/import/meshprewarm.h"
#include "ui/pages/projectopenmode.h"

// No assimp here: the project manager stopped parsing models when the import
// pipeline landed (ASSET_PIPELINE_SPEC §3.2.3 — see projectmanager.cpp's
// "opening a project no longer runs assimp" note). The three includes and the
// `class aiScene;` forward decl that used to sit here were vestigial, and the
// forward decl declared the wrong tag (assimp's is `struct aiScene`), which
// Clang diagnoses under -Wmismatched-tags. Removed for the macOS port
// (ENGINEERING_DEBT_SPEC item 5, shape 3).

class Database;
class DynamicGrid;
class GridWidget;
class ItemGridWidget;
class ProgressDialog;
class ProjectArchiver;
class QMenu;
class QAction;

namespace Ui {
	class ProjectManager;
}


class SettingsManager;
class ProjectService;
class MainWindow;
class Project;

using AssetList = QPair<QString, QString>;

class ProjectManager : public QWidget
{
    Q_OBJECT

public:
    /// `project` is the one live Project instance, owned by the shell
    /// (Phase 4: was the Globals::project static); isOpenProjectTile() reads it.
    ProjectManager(Database *handle, Project *project, QWidget *parent = nullptr);
    ~ProjectManager();

    /// The project data flows, injected by the shell right after construction
    /// (beside ProjectService::setProjectManager, which is the other half of
    /// the same pairing). The New Scene button's create goes through it.
    void setProjectService(ProjectService *service) { projectService = service; }

    // ---- THE GRID IS A MODEL (CREATE-GAP-1) --------------------------------
    // The desktop's tiles are kept current one project at a time: a create or
    // an import ADDS its tile when the row is made, a delete REMOVES it, a
    // move to another desktop takes it off (or puts it on) this one, a save
    // brings it to the head of the order and shows its new thumbnail. Nothing
    // on that list rebuilds the grid. populateDesktop() — the whole desktop,
    // one query, a widget per row — runs only when the DESKTOP changes: its
    // first showing, and another desktop selected (switchDesktop).

    /// A save's new thumbnail for `id`'s tile (decoded through the tile cache).
    void updateTile(const QString &id, const QByteArray &arr);
    /// The project's row was just made or arrived on this desktop: ONE query
    /// by guid, ONE tile, first in the order. No-op before the first build
    /// (that build will read it) and for a row on another desktop.
    void addTile(const QString &guid);
    /// The project is gone (or left this desktop): its tile goes.
    void removeTile(const QString &guid);
    /// The project now lives on `desktop`: off this desktop's grid, or on it.
    void moveTile(const QString &guid, int desktop);
    /// The project was renamed: its caption follows.
    void renameTile(const QString &guid, const QString &name);
    /// The project's row was just WRITTEN (a save stamps last_written): its
    /// tile moves to the head, where a rebuild would put it — or is added if
    /// this desktop does not show it yet.
    void touchTile(const QString &guid);
    /// The Desktop page is being shown: builds the grid the FIRST time, and
    /// after that rebuilds nothing — it refreshes the open markers and checks
    /// the tile set against the desktop's guids (one blob-free query), fixing
    /// and LOGGING any difference, because a difference means some path
    /// changed the library without its tile verb.
    void enterDesktop();
    /// Rebuilds the whole grid from the database: the DESKTOP CHANGED.
    void populateDesktop();
    /// desktop.gridStats(): builds so far, the last build's cost, the
    /// session's thumbnail decodes, out-of-step entries, tiles now.
    QVariantMap gridStats() const;

    // Re-reads the OPEN state of the live tiles in place — no rebuild, no
    // thumbnail decode, no layout churn (ItemGridWidget::setOpenProject). The
    // close edge calls it (closing FROM the desktop never switches space) and
    // so does every Desktop entry (enterDesktop).
    void refreshOpenTiles();

    bool checkForEmptyState();

	MainWindow *mainWindow = nullptr;
	/// Is this tile the project whose scene is open right now? (highlight rule)
	bool isOpenProjectTile(const QString &guid) const;

    int getCurrentDesktop() const { return currentDesktop; }

    // Desktop view mode + slider tiles, public for the scripting API
    // (desktop.viewMode / desktop.setViewMode / desktop.moveTile / desktop.tiles).
    // Mode names: "rows" | "freeform" | "sliders" (persisted per desktop).
    QString desktopViewMode() const { return currentLayoutMode; }
    bool setDesktopViewMode(const QString &name);
    bool moveTileToSliderPos(const QString &guid, int row, int index);   // 0-based row
    QVariantList sliderTilesForApi() const;
    /// The number of filmstrip rows the Sliders view mode stacks (a per-user
    /// setting, not per desktop). setSliderRows re-lays the live desktop out
    /// immediately and returns the clamped (2..10) value that took effect —
    /// the one path Preferences, the desktop API and any future caller share
    /// (VISUAL_PARITY re-audit F8).
    int sliderRows() const;
    int setSliderRows(int rows);

    /// Session AssetManager registrations shared by both load paths.
    /// `prewarm` (optional) carries model files a worker already parsed, so
    /// pinned Objects hydrate without running assimp on this thread.
    void registerProjectSessionAssets(
        const iris::MeshPrewarmPtr &prewarm = iris::MeshPrewarmPtr());

    /// The two halves of registerProjectSessionAssets, so the threaded open
    /// can spread the hydration over several event-loop turns: the membership
    /// sweep (DB) and the per-guid registration of an arbitrary SUBSET.
    QStringList sessionAssetGuids();
    void registerSessionAssetGuids(const QStringList &guids,
                                   const iris::MeshPrewarmPtr &prewarm = iris::MeshPrewarmPtr());

    /// The prewarm PLAN for the session registrations: the CAS paths of every
    /// Object this project's membership will hydrate. DB work — UI thread.
    QStringList plannedSessionModelPaths();

    /// Progress feedback for the THREADED open, driven by SceneOpenRunner's
    /// signals. Pumping is switched OFF for the duration: a pump from inside
    /// a slice re-enters the event loop and can destroy objects the slice is
    /// still using (ProgressDialog::setPumpsEventLoop documents the scar).
    void showOpenProgress(int percent, const QString &text);
    void hideOpenProgress();

public slots:
    // public for the scripting API (app.desktop(n))
    void switchDesktop(int desktop);

public:
    /// THE SAMPLE BROWSER'S OPEN, AS A VERB (API-first, SCRIPTING_SPEC §2.3 —
    /// `project.openSample(name)`). `name` is the sample's base name, which is
    /// also its archive's file name, in either shipped set: the Jahshaka
    /// samples (scenes/<name>.zip) or our ports of Ogre's (scenes/ogre/<name>.zip,
    /// matched on the catalog's base name OR its display title). The dialog's
    /// tiles call this too, so the browser and a script take the same road.
    ///
    /// Imports the archive if the library has never seen it and then opens it
    /// IN THE EDITOR — a sample is something to look at and edit; the Player is
    /// a separate statement (`app.space("player")`, the tile's Play button).
    /// Returns false with `why` filled when there is no such sample.
    bool openSampleByName(const QString &name, QString *why = nullptr);
    /// The one route behind every sample tile and openSampleByName: import the
    /// archive and open the world it carries in the editor.
    bool openSampleArchive(const QString &archivePath, QString *why = nullptr);
    /// Every sample this tree ships, by base name (both tabs) — what
    /// openSampleByName resolves against, and what its refusal lists.
    static QStringList sampleNames();

protected slots:
    void openSampleProject(QListWidgetItem*);
    void newProject();
    void importProjectFromFile(const QString& file = QString(), bool shouldOpen = false);

    void changePreviewSize(QString);


    void openSampleBrowser();
public:
    /// Builds the Sample Scenes dialog's content and returns it UNSHOWN —
    /// openSampleBrowser() runs it modally; app.dialog('sampleBrowser') shows
    /// it (MainWindow::openDialog).
    QDialog *prepareSampleBrowser();

private:
    /// The sample browser's tile list, extracted from openSampleBrowser so the
    /// Ogre-ports tab is the SAME widget with different entries
    /// (SPECS/OGRE_SAMPLES_TAB_SPEC.md §5.1). `entries` maps a preview image
    /// (relative to `dir`) to the sample's base name — which is also the
    /// archive's file name, so Qt::UserRole carries `dir/<name>.zip` and
    /// openSampleProject needs no knowledge of which tab it came from.
    QListWidget *buildSampleList(const QMap<QString, QString> &entries, const QString &dir);

protected slots:

    /// The install half of importProjectFromFile: everything that used to
    /// follow the synchronous ProjectArchiver call.
    void onArchiveImportFinished(bool canceled);

    void openProjectFromWidget(ItemGridWidget*, bool playMode);
    void exportProjectFromWidget(ItemGridWidget*);
    void renameProjectFromWidget(ItemGridWidget*);
    void closeProjectFromWidget(ItemGridWidget*);
    void deleteProjectFromWidget(ItemGridWidget*);

    // desktops (DESKTOPS_SPEC.md)
    void moveProjectToDesktop(ItemGridWidget*, int desktop);
    void projectTilePositionChanged(ItemGridWidget*);
    void projectTileSliderChanged(ItemGridWidget*);     // sliders: persist {row, index}

    void searchProjects();

private:
    friend DynamicGrid;     // is this going to be a problem?

signals:
    /// `empty` = the dialog's "Empty scene" checkbox (owner review R1a). It
    /// rides the signal rather than being read back off the dialog because the
    /// dialog is gone by the time the shell builds the scene.
    /// `guid` is the row createProjectShell just made; the shell points the
    /// current project at it only after closing the world that is open.
    void fileToCreate(const QString &guid, const QString &name, const QString &path, bool empty);
    void importProject();
    void exportProject();
    void closeProject();

private:
    /// Hands the pointed-at project to the shell's threaded open, in the space
    /// the ROUTE asked for. `mode` is an argument and never a member: see
    /// ProjectOpenMode above.
    void loadProjectAssets(ProjectOpenMode mode);
    /// An import problem, told to whoever is actually there: a box for a person,
    /// a log line for a DRIVEN run (see the definition).
    void reportImportProblem(const QString &title, const QString &text);

    // desktops (DESKTOPS_SPEC.md + DESKTOP_SLIDER_SPEC.md)
    void setupDesktopControls();
    void applyDesktopLayoutMode(const QString &modeName, bool persist);
    static QString desktopLayoutKey(int desktop);
    static QString normalizedLayoutMode(const QString &name);   // unknown -> "rows"

    int currentDesktop = 1;
    /// The grid has been built once (enterDesktop's first showing); before it,
    /// the incremental verbs have nothing to keep current.
    bool gridBuilt = false;
    int gridBuildCount = 0;
    qint64 lastBuildMs = 0;
    int lastBuildTiles = 0;
    int lastBuildDecodes = 0;
    int outOfStepEntries = 0;
    QString currentLayoutMode = QStringLiteral("rows");
    QMenu *desktopMenu = nullptr;
    QMenu *layoutMenu = nullptr;
    QMenu *tileSizeMenu = nullptr;
    QVector<QAction*> desktopActions;
    QAction *rowsAction = nullptr;
    QAction *freeformAction = nullptr;
    QAction *slidersAction = nullptr;

    Ui::ProjectManager *ui;
    SettingsManager* settings;

    QTimer *searchTimer;
    QString searchTerm;

    Database *db = nullptr;
    Project *project;
    /// THE ONE CREATE ROUTE (owner review R1). The page used to mint the guid,
    /// make the folder and write the project row itself — a second copy of
    /// ProjectService::createProjectShell, with its own `||` name check. Set by
    /// the shell beside ProjectService::setProjectManager.
    ProjectService *projectService = nullptr;

	QPointer<ProgressDialog> progressDialog;

	/// The THREADED archive import (STABILITY_PROGRAM_SPEC Lane 4). Created on
	/// first use and parented here; its destructor joins the worker, and
	/// ProjectArchiver::shutdownArchives cancels it at close.
	ProjectArchiver *archiver = nullptr;
	/// WHAT THE IN-FLIGHT IMPORT SHOULD DO WHEN IT FINISHES: open the imported
	/// world in this space, or — absent — just add its tile to the desktop.
	/// Written by importProjectFromFile at EVERY call, which is the whole point
	/// of it being an optional rather than a pair of bools.
	std::optional<ProjectOpenMode> mImportOpenMode;

    DynamicGrid *dynamicGrid;
    QDialog sampleDialog;

    QMap<QString, QString> assetGuids;
};


#endif // PROJECTMANAGER_H
