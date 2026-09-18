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
class MainWindow;
class Project;

using AssetList = QPair<QString, QString>;

class ProjectManager : public QWidget
{
    Q_OBJECT

public:
    /// `project` is the one live Project instance, owned by the shell
    /// (Phase 4: was the Globals::project static). A constructor parameter
    /// because populateDesktop() runs during construction and reaches it
    /// through isOpenProjectTile().
    ProjectManager(Database *handle, Project *project, QWidget *parent = nullptr);
    ~ProjectManager();

	void updateTile(const QString &id, const QByteArray &arr);
	void addImportedTileToDesktop(const QString &guid);
    void populateDesktop(bool reset = false);

    // Re-reads the OPEN state of the live tiles in place — no rebuild, no
    // thumbnail decode, no layout churn (ItemGridWidget::setOpenProject).
    // populateDesktop(true) does this too, as a side effect of rebuilding
    // everything, but the CLOSE edge has no repopulate at all: closing from
    // the desktop returns early in MainWindow::closeProject, and the
    // switchSpace(DESKTOP) path's repopulate is gated on a scene being open —
    // which it no longer is. Without this call the closed project's tile kept
    // its "[ Open ]" caption, its dark blue bar and its Close control.
    void refreshOpenTiles();

    bool checkForEmptyState();
    void cleanupOnClose();

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
    void fileToCreate(const QString &name, const QString &path);
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
