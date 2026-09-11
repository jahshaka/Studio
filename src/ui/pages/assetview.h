/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETVIEW_H
#define ASSETVIEW_H

#include <QWidget>

#include "ui/style/columnedpage.h"

#include "irisgl/document/assets/mesh.h"
#include "irisgl/core/irisutils.h"
#include "services/import/importtypes.h"

class QSplitter;
class QListWidget;

class QLineEdit;
class QComboBox;
class QTreeWidgetItem;
class QFocusEvent;
class ProgressDialog;
class QSlider;
class QMediaPlayer;
class QAudioOutput;
class QScrollArea;
class VideoPreviewWidget;
class WaveformWidget;

#include <QTreeWidget>
#include <QPushButton>
#include <QJsonObject>
#include <QJsonArray>
#include <QApplication>
#include <QResizeEvent>
#include <QGridLayout>
#include <QScrollArea>
#include <qdebug.h>
#include <QLabel>
#include <QButtonGroup>
#include <QStackedLayout>

#ifdef Q_OS_WIN
	#include <Windows.h>
	#define WIN32_MEAN_AND_LEAN
#endif // Q_OS_WIN


enum AssetMetaType
{
	Shader,
	Material,
	Texture,
	Video,
	Cubemap,
	Object,
	SoundEffect,
	Music
};

class AssetViewGrid;
class AssetGridItem;
class DrawerTreeWidget;
class ImportBatchRunner;
class ImportTailQueue;
class QMenu;
class IAssetViewer;
class Database;
struct StudioServices;
class Project;
class SettingsManager;
class PreferencesDialog;

typedef struct directory_tuple
{
    QString path;
    QString guid;
    QString parent_guid;
};

class AssetView : public QWidget, public ColumnedPage
{
	Q_OBJECT

public:
	/// ColumnedPage (ui/style/columnedpage.h): the drawers tree on the left,
	/// the metadata pane on the right — both sized from PanelMetrics.
	QWidget *leftColumn() const override { return _navPane; }
	QWidget *rightColumn() const override { return _metadataPane; }

signals:
	/// A store asset was pinned into the open project (button, Shift+click
	/// or tile context menu). The shell refreshes the editor's project
	/// panel so the membership is visible without a page round-trip.
	void assetAddedToProject(const QString &guid);

	/// THE DRAWER/GRID -> MODULE SEAM (AVATAR_ASSET_SPEC §5.5). A page asks
	/// the SHELL to open an asset in a module; the shell switches space and
	/// calls that module's API. The page never includes mainwindow.h and the
	/// module never learns about the page — the seam the materials module
	/// never got, and the reason "Edit" has only ever existed for shader files
	/// (where it shells out to an external editor).
	void editAssetInModule(const QString &guid, const QString &moduleId,
	                       const QString &scope);

public slots:
	void fetchMetadata(AssetGridItem*, bool allowBackfill = true);
	/// Lazy metadata backfill for pre-metadata library rows: computes the
	/// per-type block on a worker thread (assimp/header parse only, no GPU),
	/// persists it into the row's properties JSON on arrival, refreshes the
	/// pane if the tile is still selected.
	void backfillMetadata(AssetGridItem *widget, const QString &guid, int assetType);

	void addAssetItemToProject(AssetGridItem*);
	void moveAssetToDrawer(AssetGridItem*, int drawerId);
	void deleteAssetFromLibrary(AssetGridItem*);

public:
	int gridCount;
	/// `previewViewer` (optional) is the page's preview viewer; null means the
	/// legacy AssetViewer. MainWindow passes the engine one in engine mode.
	AssetView(Database *handle, QWidget *parent = Q_NULLPTR, IAssetViewer *previewViewer = nullptr);
	/// Phase 4: scene-open checks go through the services, not ambient statics.
	void setServices(StudioServices *s);   // subscribes to the library announcements (assetview.cpp)
	/// The one live Project, wired by the shell in MainWindow::setupServices
	/// (Phase 4: was the Globals::project static). Forwarded to the preview
	/// viewer, which reads it for relative animation paths.
	void setProject(Project *p);
	~AssetView();
	void focusInEvent(QFocusEvent *event);
	bool eventFilter(QObject *watched, QEvent *event);
    void checkForEmptyState();
    void toggleFilterPane(bool);
	void addToJahLibrary(const QString fileName, const QString guid, bool jfx = false);
    void addToLibrary(const QString& main_guid, bool jfx = false);
	void spaceSplits();
    void closeViewer();
	void clearViewer();
	/// Shutdown teardown: close the progress dialog, drop pending viewer
	/// tails, stop media previews, abort a running import batch and join its
	/// worker (bounded). Safe to call repeatedly. False when the worker did
	/// NOT stop in time — the caller must then force the process exit rather
	/// than tear down objects the worker still uses.
	bool shutdownImports(int msTimeout);
	QString getAssetType(int);
	/// Keeps the bottom-right "Add to Project" button honest: enabled only
	/// when a tile is current AND a project is open, with a tooltip saying
	/// which condition is missing (it used to sit silently disabled after
	/// the tile flip made plain clicks non-selecting).
	void updateAddToProjectButton();
	/// Offline-store banner (ASSET_PIPELINE_SPEC §3.1.2): shown while the
	/// configured store root is unreachable; Reconnect re-stats and refreshes.
	/// Byte-needing actions (add-to-project, previews) are disabled meanwhile.
	void refreshStoreBanner();
	/// Tile right-click → Rebuild Thumbnail: re-renders and persists the
	/// tile's thumbnail (3D types through the asset viewer screenshot path,
	/// images via ThumbnailManager, audio/files back to their type icon).
	void rebuildTileThumbnail(AssetGridItem *item);
	/// Tile context menu → "Create Material from Image" (IMAGE_PLANE_SPEC
	/// option B1): mints the companion PBR material asset, pins it into the
	/// open project and adds its library tile.
	void createMaterialFromImageTile(AssetGridItem *item);
	/// "Create Avatar" on a rigged model tile: mints the avatar asset and opens
	/// it in the module, one gesture (D8-A).
	void createAvatarFromModelTile(AssetGridItem *item);
	void showEvent(QShowEvent *event) override;

	/// SELECT AN ASSET BY GUID — the tile gesture, as a call (smoke S4: an
	/// import left the new tile unselected and the user had to hunt for it).
	/// Selects the tile, scrolls it into view and loads its preview, exactly
	/// as a double-click does. False when no tile shows that guid.
	/// `assets.select` is this; the import tail calls it too.
	bool selectAsset(const QString &guid);
	/// The guid of the current tile, empty when nothing is selected
	/// (`assets.selected`).
	QString selectedAssetGuid() const;
	/// The page's preview viewer — `assets.preview`/`assets.fly` read and
	/// steer it (null in a page with no viewer).
	IAssetViewer *previewViewer() const { return viewer; }

	/// THE import dispatch (ASSET_DRAWERS_SPEC §3): every path (drop pad,
	/// browse dialog) lands here. Builds one ImportRequest per file and runs
	/// them through ImportBatchRunner — the heavy pipeline half on a worker,
	/// one cancellable progress dialog for the whole drop, the UI live
	/// throughout (the old synchronous importModel/importJahModel/
	/// importImageOrAudio trio collapsed into the per-type finish tails).
	void importFiles(const QStringList &fileNames);

private:
	/// Start the runner + dialog for a prepared request batch.
	void runImportBatch(const QVector<ImportRequest> &requests);
	/// Per-file completion (UI thread, dialog still up): media tiles appear
	/// live; mesh/.jaf viewer tails queue for after the dialog closes.
	void handleImportedFile(const ImportRequest &request, const ImportResult &result);
	/// Engine-dependent tails (viewer preview + rendered thumbnail) — run
	/// AFTER the batch dialog closes, ONE PER EVENT-LOOP TURN through
	/// tailQueue so the app stays responsive; tiles update live as renders
	/// land (the busy state shows on the tile overlay + the status strip).
	void scheduleViewerTails();
	/// One mesh tail item: previews the COMMITTED asset (ImportMeshTail) and
	/// stores its thumbnail through the one thumbnail routine
	/// (assetthumb::storeObject — what `assets.refreshThumbnail` runs).
	void finishMeshTailItem(const ImportResult &result, const QString &fileName);
	/// The old importJahModel tail: per-kind viewer page + library tile.
	void finishJafImport(const ImportResult &result, const QString &fileName);
	/// Images/audio/video: build + wire the library tile for a committed row.
	void addLibraryTileForAsset(const QString &guid);
	/// The drawer new imports are filed in: the selected drawer, else
	/// Uncategorized (§3).
	int selectedDrawerId() const;
	/// The page's ONE toast (bottom-centre of the app window), made on first
	/// use and reused — every message used to leak a top-level window.
	class Toast *libraryToast();
	/// Stops BOTH media players (audio + video). Every selection change and
	/// page switch lands here (ASSET_MEDIA_SPEC §2: viewers stop/clear).
	void stopMediaPreviews();
	void showAudioPreview(const QString &guid, const QString &filePath,
	                      const QString &displayName);
	void showVideoPreview(const QString &filePath, const QString &displayName);
	void showImagePreview(const QString &filePath);
	void showFilePlaceholder(const QString &displayName);
	/// Waveform peaks for the audio preview: cached in the row's properties
	/// JSON under "waveform", computed on QtConcurrent the first time.
	void loadWaveform(const QString &guid, const QString &filePath);
	/// Image viewer fit/1:1/zoom (§2): rescales the canvas pixmap.
	void applyImageZoom();
	// Drawers (ASSET_DRAWERS_SPEC §1/§2). The tree rebuild is the single
	// source of truth for what the left column shows.
	void rebuildDrawerTree();
	void createDrawerUnder(int parentId);
	void deleteDrawer(int drawerId);
	QTreeWidgetItem *findDrawerItem(int drawerId) const;
	QString drawerName(int drawerId) const;
	/// Indented (id, name) pairs for the tile context menu's Move to ▸.
	QVector<QPair<int, QString>> drawerMenuEntries() const;
	/// Refilters the grid to the selected drawer (root -1 = everything).
	void filterFromSelection();
	/// The tile signal plumbing every creation path shares.
	void wireTile(AssetGridItem *gridItem);
	/// Loading overlay (§1): shown on the double-clicked tile until the
	/// viewer's loadFinished callback (or the selection handler's tail) clears it.
	void clearLoadingTile();
    void extractTexturesAndMaterialFromMaterial(
        const QString &filePath,
        QStringList &textureList,
        QJsonObject &mat);

	Database *db;
	Project *project = nullptr;   // the live Project (Phase 4: was Globals::project)
	class Toast *mToast = nullptr;
	/// The last asset an import batch committed — what the batch selects when
	/// it finishes (smoke S4; a mesh selects from its own tail).
	QString lastImportedGuid;
	StudioServices *services = nullptr;
	QSplitter *_splitter;
	QWidget *_filterBar;
	QWidget *_navPane;
	QWidget *_previewPane;
	QWidget *_viewPane;
	QWidget *_metadataPane;

	QWidget *assetDropPad;

    QSplitter *split;

	QListWidget *_assetView;

	QByteArray downloadedImage;
	QVector<QByteArray> iconList;
	QString filename;

    PreferencesDialog* prefsDialog;
    ProgressDialog* progressDialog;

	QPushButton *updateAsset;
	QPushButton *addToProject;
	QWidget *storeOfflineBanner = nullptr;
	QLabel *storeOfflineLabel = nullptr;
    QPushButton *deleteFromLibrary;
	QLabel *renameModel;
	QLineEdit *renameModelField;
	QLabel *tagModel;
	QLineEdit *tagModelField;
	QWidget *renameWidget;
	QWidget *tagWidget;

	QLabel *backdropLabel;
	QComboBox *backdropColor;

    DrawerTreeWidget *treeWidget;
    /// Bottom half of the left column: the selected asset's own node tree
    /// (the model's scene graph, read from its stored node-tree blob).
    QTreeWidget *assetNodeTree = nullptr;
    void populateAssetNodeTree(const QString &guid, int assetType);
	QTreeWidgetItem *rootItem;
	bool drawerTreeUpdating = false;   // guards itemChanged during rebuilds
	AssetGridItem *loadingTile = nullptr;

	AssetViewGrid *fastGrid;
	QWidget *emptyGrid;
	QWidget *filterPane;

	// Threaded import batch (one at a time; the dialog owns the lifetime UX).
	ImportBatchRunner *importRunner = nullptr;
	QStringList importErrors;                 // aggregated, shown after the batch
	struct PendingViewerTail { ImportResult result; QString fileName; };
	QVector<PendingViewerTail> pendingViewerTails;
	QStringList pendingVideoThumbGuids;       // real frame grabs, post-dialog
	// The post-dialog tail pump (one item per event-loop turn) + its subtle
	// busy strip under the grid ("Rendering previews… (n of m)").
	ImportTailQueue *tailQueue = nullptr;
	QLabel *tailStatusLabel = nullptr;

	// Tiles/List view mode (owner request): the grey "View ▾" popup beside
	// Backdrop; the list mirrors the editor panel's list mode with
	// name/type/size columns. Persisted as assetView/viewMode.
	QPushButton *viewModeButton = nullptr;
	QMenu *viewModeMenu = nullptr;
	QAction *viewTilesAction = nullptr;
	QAction *viewListAction = nullptr;
	QTreeWidget *assetListView = nullptr;
	void setAssetViewMode(const QString &mode, bool persist = true);
	void rebuildAssetList();
	QString assetViewMode = QStringLiteral("tiles");

	// FIT TO SIZE (services/fitsize.h): the "Imported size: 17.3 m -> fitted to
	// 1.75 m [Re-measure] [Reset] [Set...]" row. Model rows only; hidden for
	// everything else. Replaced a dead "Normalize" button that was constructed,
	// styled and never put in a layout or connected to anything.
	QWidget *fitRow = nullptr;
	QLabel *fitLabel = nullptr;
	QPushButton *fitRemeasure = nullptr;
	QPushButton *fitReset = nullptr;
	QPushButton *fitSet = nullptr;
	/// Renders (or hides) the fit row for the selected tile's metadata block.
	void refreshFitRow(const QString &guid, int assetType, const QJsonObject &meta);
	/// Runs one assets.setFit-equivalent write and re-renders the pane.
	void applyFitChange(const QString &guid, const QVariantMap &options);
	QLabel *metadataMissing;
	QLabel *metadataDetails;   // the two-column metadata table (all rows)

    SettingsManager* settings;
	IAssetViewer *viewer;
    AssetGridItem *selectedGridItem;
	QTimer *searchTimer;
	QString searchTerm;
	QLineEdit *le;

    // Image page (PreviewPage::Image): scrollable canvas with fit / 1:1 /
    // wheel zoom (ASSET_MEDIA_SPEC §2).
    QWidget *assetImageViewer;
    QLabel *assetImageCanvas;
    QScrollArea *imageScroll = nullptr;
    QPushButton *imageFitButton = nullptr;
    QPushButton *imageActualButton = nullptr;
    QLabel *imageZoomLabel = nullptr;
    QPixmap imageOriginal;
    bool imageFitMode = true;
    double imageZoom = 1.0;

    // Audio page (PreviewPage::Audio): filename, waveform (click-to-seek,
    // playhead), play/pause, seek, time. QMediaPlayer + QAudioOutput.
    QWidget *assetAudioViewer;
    QLabel *audioNameLabel;
    QPushButton *audioPlayButton;
    QSlider *audioSeekSlider;
    QLabel *audioTimeLabel;
    // Built on FIRST AUDIO PLAYBACK, not in the constructor: constructing a
    // QMediaPlayer loads the Qt multimedia (ffmpeg) backend and constructing a
    // QAudioOutput enumerates audio devices, which on this box means a
    // pipewire/PulseAudio probe on the startup path
    // (STABILITY_PROGRAM_SPEC §1.7c). Nothing on the Assets page needs either
    // until someone actually plays a sound. Always nullptr-checked or reached
    // through ensureAudioPlayer(); never assume they exist.
    QMediaPlayer *mediaPlayer = nullptr;
    QAudioOutput *audioOutput = nullptr;
    void ensureAudioPlayer();   // idempotent; builds the player + its wiring
    WaveformWidget *waveform = nullptr;
    QString waveformGuid;   // the guid the waveform (or its pending decode) is for

    // Video page (PreviewPage::Video) — ASSET_MEDIA_SPEC §2.
    VideoPreviewWidget *assetVideoViewer = nullptr;

    // Placeholder page (PreviewPage::Placeholder): icon + name for File rows.
    QWidget *assetFileViewer = nullptr;
    QLabel *fileIconLabel = nullptr;
    QLabel *fileNameLabel = nullptr;

    // Empty state (stack index 5, after the routed pages): what shows with
    // NOTHING selected — centered muted text, no stray icon (the old initial/
    // cleared state could land on the Placeholder page's blue "S" file icon).
    QWidget *assetEmptyViewer = nullptr;

    QWidget *viewersWidget;
    QStackedLayout *viewers;

	QWidget *parent;
};

#endif // ASSETVIEW_H
