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

#include "irisgl/document/assets/mesh.h"
#include "irisgl/core/irisutils.h"

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

class AssetView : public QWidget
{
	Q_OBJECT

public slots:
	void fetchMetadata(AssetGridItem*, bool allowBackfill = true);
	/// Lazy metadata backfill for pre-metadata library rows: computes the
	/// per-type block on a worker thread (assimp/header parse only, no GPU),
	/// persists it into the row's properties JSON on arrival, refreshes the
	/// pane if the tile is still selected.
	void backfillMetadata(AssetGridItem *widget, const QString &guid, int assetType);

	void addAssetItemToProject(AssetGridItem*);
	void moveAssetToDrawer(AssetGridItem*, int drawerId);
	void removeAssetFromProject(AssetGridItem*);

public:
	int gridCount;
	/// `previewViewer` (optional) is the page's preview viewer; null means the
	/// legacy AssetViewer. MainWindow passes the engine one in engine mode.
	AssetView(Database *handle, QWidget *parent = Q_NULLPTR, IAssetViewer *previewViewer = nullptr);
	/// Phase 4: scene-open checks go through the services, not ambient statics.
	void setServices(StudioServices *s) { services = s; }
	/// The one live Project, wired by the shell in MainWindow::setupServices
	/// (Phase 4: was the Globals::project static). Forwarded to the preview
	/// viewer, which reads it for relative animation paths.
	void setProject(Project *p);
	~AssetView();
	void focusInEvent(QFocusEvent *event);
	bool eventFilter(QObject *watched, QEvent *event);
    void copyTextures(const QString &folderGuid);
    void checkForEmptyState();
    void toggleFilterPane(bool);
	void addToJahLibrary(const QString fileName, const QString guid, bool jfx = false);
    void addToLibrary(const QString& main_guid, bool jfx = false);
	void spaceSplits();
    void closeViewer();
	void clearViewer();
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
	void showEvent(QShowEvent *event) override;

	void importJahModel(const QString &filename, bool addToLibrary = true);
	void importModel(const QString &filename, bool jfx = false);
	/// THE import dispatch (ASSET_DRAWERS_SPEC §3): every path (drop pad,
	/// browse dialog) lands here; one switch keyed on ModelTypes per file.
	void importFiles(const QStringList &fileNames);

private:
	/// Images/audio/video: the headless AssetImporter service plus a grid tile.
	void importImageOrAudio(const QString &fileName);
	/// The drawer new imports are filed in: the selected drawer, else
	/// Uncategorized (§3).
	int selectedDrawerId() const;
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

    QPushButton *normalize;
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
    QMediaPlayer *mediaPlayer;
    QAudioOutput *audioOutput;
    WaveformWidget *waveform = nullptr;
    QString waveformGuid;   // the guid the waveform (or its pending decode) is for

    // Video page (PreviewPage::Video) — ASSET_MEDIA_SPEC §2.
    VideoPreviewWidget *assetVideoViewer = nullptr;

    // Placeholder page (PreviewPage::Placeholder): icon + name for File rows.
    QWidget *assetFileViewer = nullptr;
    QLabel *fileIconLabel = nullptr;
    QLabel *fileNameLabel = nullptr;

    QWidget *viewersWidget;
    QStackedLayout *viewers;

	QWidget *parent;
};

#endif // ASSETVIEW_H
