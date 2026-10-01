/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/qtinterop.h"
#include "bridge/assetthumbnail.h"
#include "services/materialtile.h"
#include "services/thumbnailrebuild.h"
#include "bridge/enginehost.h"
#include "ui/pages/assetview.h"
#include "ui/pages/iassetviewer.h"
#include "ui/pages/headlessassetviewer.h"
#include "ui/pages/importviewertail.h"
#include <QTimer>
#include "ui/dialogs/importsettingsdialog.h"
#include "ui/dialogs/progressdialog.h"
#include "data/settingsmanager.h"
#include "services/assettags.h"
#include "services/materialpresetseeder.h"
#include "services/assettray.h"
#include "ui/dialogs/preferencesdialog.h"
#include "ui/dialogs/preferences/worldsettingswidget.h"

#include "irisgl/core/irisutils.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/core/properties/property.h"
#include "zip.h"

#include <QStackedLayout>
#include <QDirIterator>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QDialog>
#include <QGridLayout>
#include <QSplitter>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCoreApplication>
#include <QEventLoop>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QtAlgorithms>
#include <QFile>
#include <QBuffer>
#include <functional>
#include <QTreeWidget>
#include <QHeaderView>
#include <QListView>
#include <QTreeView>
#include <QGuiApplication>
#include <QTreeWidgetItem>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMenu>
#include <QMimeData>
#include <QTreeWidgetItemIterator>
#include <QDesktopServices>
#include <QTemporaryDir>
#include <QProgressDialog>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QScrollArea>
#include <QSlider>
#include <QWheelEvent>
#include <QFutureWatcher>
#include <QActionGroup>
#include <QLocale>
#include <QPointer>
#include <QtConcurrent>

#include "data/constants.h"
#include "data/settingsmanager.h"
#include "data/database/database.h"
#include "data/project.h"
#include "services/services.h"
#include "services/assetservice.h"
#include "services/assetshare.h"
#include "services/assetstore.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include <QSqlDatabase>
#include "services/projectservice.h"
#include "ui/controls/librarymodel.h"
#include "ui/controls/drawertreewidget.h"
#include "services/assethelper.h"
#include "services/assetimporter.h"
#include "services/import/assetimportservice.h"
#include "services/import/importbatchrunner.h"
#include "ui/dialogs/toast.h"
#include "services/assetdelete.h"
#include "services/materialmembers.h"
#include "services/projectassets.h"
#include "services/imagematerial.h"
#include "services/extentmeasure.h"
#include "services/assetmetadata.h"
#include "services/avatarassets.h"
#include "services/audiopeaks.h"
#include "ui/controls/videopreviewwidget.h"
#include "ui/controls/waveformwidget.h"
#include "ui/pages/previewrouter.h"
#include "io/assetmanager.h"
#include "io/builtinmaterials.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "io/materialreader.h"
#include "services/thumbnailgenerator.h"

#include "data/guidmanager.h"
#include "io/assetmanager.h"
#include "io/scenewriter.h"

#include "ui/dialogs/toast.h"
#include "ui/style/panelmetrics.h"
#include "ui/style/stylesheet.h"
#include "ui/style/thememanager.h"
#include "ui/style/themeroles.h"

void AssetView::focusInEvent(QFocusEvent *event)
{
	Q_UNUSED(event);
}

bool AssetView::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == assetDropPad) {
		switch (event->type()) {
			case QEvent::Drop: {
				auto evt = static_cast<QDropEvent*>(event);
				QList<QUrl> droppedUrls = evt->mimeData()->urls();
				QStringList list;

				for (auto url : droppedUrls) {
					auto fileInfo = QFileInfo(url.toLocalFile());
					list << fileInfo.absoluteFilePath();
				}

				// Every URL imports (the old path took only the first) —
				// but NOT inside the drop handler: the import decision is a
				// modal dialog now (SPECS/IMPORT_DIALOG_SPEC.md §8), and a
				// nested event loop inside a drop leaves the drag source (which
				// may be another application) waiting on the XDND handshake.
				// Acknowledge the drop, then ask.
				QTimer::singleShot(0, this, [this, list]() { importFiles(list); });

				break;
			}

			case QEvent::DragEnter: {
				auto evt = static_cast<QDragEnterEvent*>(event);
				if (evt->mimeData()->hasUrls()) {
					evt->acceptProposedAction();
				}

				break;
			}

			default: break;
		}
	}

	// Image viewer (ASSET_MEDIA_SPEC §2): wheel = zoom (drops fit mode),
	// viewport resize = refit while in fit mode.
	if (imageScroll && watched == imageScroll->viewport()) {
		if (event->type() == QEvent::Wheel) {
			auto *wheel = static_cast<QWheelEvent*>(event);
			if (!imageOriginal.isNull()) {
				const double step = wheel->angleDelta().y() > 0 ? 1.25 : 0.8;
				imageFitMode = false;
				if (imageFitButton) imageFitButton->setChecked(false);
				imageZoom = qBound(0.05, imageZoom * step, 16.0);
				applyImageZoom();
			}
			return true;
		}
		if (event->type() == QEvent::Resize && imageFitMode && !imageOriginal.isNull())
			applyImageZoom();
	}

	return QObject::eventFilter(watched, event);
}

void AssetView::toggleFilterPane(bool toggle) {
    filterPane->setVisible(toggle);
}

void AssetView::spaceSplits()
{
	split->setHandleWidth(1);
	int size = this->height() / 2;
	const QList<int> sizes = { size, size };
	split->setSizes(sizes);
	split->setStretchFactor(0, 1);
	split->setStretchFactor(1, 1);
}

void AssetView::closeViewer()
{

    int size = this->height() / 3;
    const QList<int> sizes = { 1, size * 2 };   // 1px keeps the viewer visible so it's never fully hidden so initializegl gets called
    split->setSizes(sizes);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);

    toggleFilterPane(libraryModel->rowCount() > 0);
}

void AssetView::clearViewer()
{
	viewer->clearScene();
	stopMediaPreviews();
	if (assetNodeTree) assetNodeTree->clear();
	// Back to the explicit empty state — never a stale preview page.
	if (assetEmptyViewer)
		viewers->setCurrentIndex(viewers->indexOf(assetEmptyViewer));
}

void AssetView::populateAssetNodeTree(const QString &guid, int assetType)
{
	if (!assetNodeTree) return;
	assetNodeTree->clear();
	// Only model assets carry a node-tree blob (SceneWriter JSON).
	if (guid.isEmpty() || assetType != static_cast<int>(ModelTypes::Object)) return;
	const QJsonObject root = QJsonDocument::fromJson(db->fetchAssetData(guid)).object();
	if (root.isEmpty()) return;

	std::function<void(const QJsonObject &, QTreeWidgetItem *)> add =
	    [&](const QJsonObject &nodeObj, QTreeWidgetItem *parent) {
		auto *item = new QTreeWidgetItem;
		QString name = nodeObj["name"].toString();
		if (name.isEmpty()) name = nodeObj["type"].toString("node");
		item->setText(0, name);
		item->setToolTip(0, nodeObj["type"].toString());
		if (parent) parent->addChild(item);
		else assetNodeTree->addTopLevelItem(item);
		for (const auto &childVal : nodeObj["children"].toArray())
			add(childVal.toObject(), item);
	};
	add(root, nullptr);
	assetNodeTree->expandAll();
}

QString AssetView::getAssetType(int id)
{
	switch (id) {
		case static_cast<int>(ModelTypes::Material):		return "Material";			break;
		case static_cast<int>(ModelTypes::Texture):			return "Texture";			break;
		case static_cast<int>(ModelTypes::Object):			return "Object";			break;
		case static_cast<int>(ModelTypes::Sky):				return "Sky";				break;
		case static_cast<int>(ModelTypes::Music):			return "Audio";				break;
		case static_cast<int>(ModelTypes::Video):			return "Video";				break;
		case static_cast<int>(ModelTypes::Mesh):			return "Mesh";				break;
		case static_cast<int>(ModelTypes::File):			return "File";				break;
		case static_cast<int>(ModelTypes::ParticleSystem):	return "Particle System";	break;
		case static_cast<int>(ModelTypes::LightProfile):	return "Light Profile";		break;
		case static_cast<int>(ModelTypes::Avatar):			return "Avatar";			break;
		case static_cast<int>(ModelTypes::Animation):		return "Animation";			break;
		default: return "Undefined"; break;
	}
}

void AssetView::setProject(Project *p)
{
	project = p;
	if (viewer) viewer->setProject(p);
}

AssetView::AssetView(Database *handle, QWidget *parent, IAssetViewer *previewViewer) : db(handle), QWidget(parent)
{
	setParent(parent);
	this->parent = parent;
	_assetView = new QListWidget;
	// The page's preview viewer: engine-backed, or the headless document-only
	// stand-in when no engine view can exist.
	viewer = previewViewer ? previewViewer : new HeadlessAssetViewer(this);
    viewer->setDatabase(db);
	// Clears the double-clicked tile's loading overlay once the preview is
	// actually showing (ASSET_DRAWERS_SPEC §1 — big GLBs take a while).
	viewer->setLoadFinishedCallback([this]() { clearLoadingTile(); });

    viewersWidget = new QWidget;
    viewers = new QStackedLayout;

    // Image page (ASSET_MEDIA_SPEC §2): scrollable canvas, Fit / 1:1 toggle,
    // wheel zoom (the wheel drops fit mode and zooms around the current view).
    assetImageViewer = new QWidget;
    assetImageCanvas = new QLabel;
    assetImageCanvas->setAlignment(Qt::AlignCenter);
    imageScroll = new QScrollArea;
    imageScroll->setWidget(assetImageCanvas);
    imageScroll->setWidgetResizable(false);
    imageScroll->setAlignment(Qt::AlignCenter);
    imageScroll->setFrameShape(QFrame::NoFrame);
    imageScroll->viewport()->installEventFilter(this);

    imageFitButton = new QPushButton(tr("Fit"));
    imageFitButton->setCheckable(true);
    imageFitButton->setChecked(true);
    imageFitButton->setFixedWidth(48);
    imageFitButton->setCursor(Qt::PointingHandCursor);
    imageActualButton = new QPushButton(tr("1:1"));
    imageActualButton->setFixedWidth(48);
    imageActualButton->setCursor(Qt::PointingHandCursor);
    imageZoomLabel = new QLabel;
    imageZoomLabel->setStyleSheet(StyleSheet::AssetViewMutedLabel());
    ThemeRoles::setTone(imageZoomLabel, ThemeRoles::Tone::Muted);

    auto imageBar = new QHBoxLayout;
    imageBar->setContentsMargins(12, 6, 12, 6);
    imageBar->addWidget(imageFitButton);
    imageBar->addWidget(imageActualButton);
    imageBar->addWidget(imageZoomLabel);
    imageBar->addStretch();

    auto imgl = new QVBoxLayout;
    imgl->setContentsMargins(0, 0, 0, 0);
    imgl->setSpacing(0);
    imgl->addLayout(imageBar);
    imgl->addWidget(imageScroll, 1);
    assetImageViewer->setLayout(imgl);

    connect(imageFitButton, &QPushButton::toggled, this, [this](bool fit) {
        imageFitMode = fit;
        applyImageZoom();
    });
    connect(imageActualButton, &QPushButton::clicked, this, [this]() {
        imageFitMode = false;
        imageFitButton->setChecked(false);
        imageZoom = 1.0;
        applyImageZoom();
    });

    // Page 2 of the viewers stack: the audio preview (ASSET_DRAWERS_SPEC §3) —
    // filename, play/pause, seek, time. Qt Multimedia was already linked; this
    // is its first real playback consumer.
    assetAudioViewer = new QWidget;
    // mediaPlayer / audioOutput are NOT built here — see ensureAudioPlayer()
    // and the note on the members. The page's widgets are free; the player is
    // an audio-device probe at startup.

    audioNameLabel = new QLabel;
    audioNameLabel->setAlignment(Qt::AlignCenter);
    audioNameLabel->setStyleSheet(StyleSheet::AssetViewPreviewTitle());
    ThemeRoles::setTextSize(audioNameLabel, 14);
    audioPlayButton = new QPushButton(tr("Play"));
    audioPlayButton->setFixedWidth(64);
    audioPlayButton->setCursor(Qt::PointingHandCursor);
    audioSeekSlider = new QSlider(Qt::Horizontal);
    audioSeekSlider->setRange(0, 0);
    audioTimeLabel = new QLabel("0:00 / 0:00");
    audioTimeLabel->setStyleSheet(StyleSheet::AssetViewMutedLabel());
    ThemeRoles::setTone(audioTimeLabel, ThemeRoles::Tone::Muted);

    auto audioControls = new QHBoxLayout;
    audioControls->addWidget(audioPlayButton);
    audioControls->addWidget(audioSeekSlider);
    audioControls->addWidget(audioTimeLabel);

    // The waveform strip (ASSET_MEDIA_SPEC §2): peak envelope, playhead,
    // click-to-seek. Peaks are cached per guid — see loadWaveform().
    waveform = new WaveformWidget;
    waveform->setFixedHeight(96);

    auto audioLayout = new QVBoxLayout;
    audioLayout->addStretch();
    audioLayout->addWidget(audioNameLabel);
    audioLayout->addSpacing(12);
    audioLayout->addWidget(waveform);
    audioLayout->addSpacing(6);
    audioLayout->addLayout(audioControls);
    audioLayout->addStretch();
    audioLayout->setContentsMargins(48, 0, 48, 0);
    assetAudioViewer->setLayout(audioLayout);

    // Controls that only need widgets are wired here. Everything that touches
    // mediaPlayer is wired inside ensureAudioPlayer(), which the two entry
    // points below (this button, showAudioPreview) call first.
    connect(audioPlayButton, &QPushButton::clicked, this, [this]() {
        ensureAudioPlayer();
        if (mediaPlayer->playbackState() == QMediaPlayer::PlayingState) mediaPlayer->pause();
        else mediaPlayer->play();
    });
    connect(audioSeekSlider, &QSlider::sliderMoved, this, [this](int position) {
        if (mediaPlayer) mediaPlayer->setPosition(position);
    });
    connect(waveform, &WaveformWidget::seekRequested, this, [this](qint64 ms) {
        if (mediaPlayer) mediaPlayer->setPosition(ms);
    });

    // Video page (PreviewPage::Video) — ASSET_MEDIA_SPEC §2. The widget itself
    // is cheap; its own QMediaPlayer is deferred the same way (see
    // videopreviewwidget.cpp, ensurePlayer()).
    assetVideoViewer = new VideoPreviewWidget;

    // Placeholder page (PreviewPage::Placeholder): icon + name, so File rows
    // never leave a stale 3D scene or image on screen.
    assetFileViewer = new QWidget;
    fileIconLabel = new QLabel;
    fileIconLabel->setAlignment(Qt::AlignCenter);
    fileIconLabel->setPixmap(QPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-72.png")));
    fileNameLabel = new QLabel;
    fileNameLabel->setAlignment(Qt::AlignCenter);
    fileNameLabel->setStyleSheet(StyleSheet::AssetViewPreviewTitle());
    ThemeRoles::setTextSize(fileNameLabel, 14);
    auto filePageLayout = new QVBoxLayout;
    filePageLayout->addStretch();
    filePageLayout->addWidget(fileIconLabel);
    filePageLayout->addSpacing(8);
    filePageLayout->addWidget(fileNameLabel);
    filePageLayout->addStretch();
    assetFileViewer->setLayout(filePageLayout);

    // Empty state (owner-reported): with nothing selected the preview area
    // used to show a mystery blue "S" — the Placeholder page's file icon with
    // no name. A dedicated page says what to do instead; initial and cleared
    // states land here (both themes are dark — explicit colors, no stray icon).
    assetEmptyViewer = new QWidget;
    assetEmptyViewer->setStyleSheet(StyleSheet::AssetViewEmptyPreview());
    ThemeRoles::setSurface(assetEmptyViewer, ThemeRoles::Surface::Panel);
    {
        auto *emptyPreviewLabel = new QLabel(tr("Select an asset to preview"));
        emptyPreviewLabel->setAlignment(Qt::AlignCenter);
        emptyPreviewLabel->setStyleSheet(StyleSheet::AssetViewEmptyPreviewLabel());
        ThemeRoles::setTextSize(emptyPreviewLabel, 14);
        ThemeRoles::setTone(emptyPreviewLabel, ThemeRoles::Tone::Muted);
        auto *emptyPreviewLayout = new QVBoxLayout;
        emptyPreviewLayout->addWidget(emptyPreviewLabel);
        assetEmptyViewer->setLayout(emptyPreviewLayout);
    }

    settings = SettingsManager::getDefaultManager();

	// Header row (ASSET_DRAWERS_SPEC §1): the Local Assets label plus the [+]
	// drawer button. The Online Assets stub (assetSource was never read, no
	// network code) and the bottom Create Collection button are gone.
	auto headerRow = new QWidget;
	auto headerLayout = new QHBoxLayout;
	headerLayout->setContentsMargins(6, 6, 6, 6);
	auto localAssetsLabel = new QLabel(tr("Local Assets"));
	localAssetsLabel->setStyleSheet(StyleSheet::AssetViewLocalAssetsLabel());
	ThemeRoles::setPadding(localAssetsLabel, 4, 4, 4, 4);
	auto addDrawerButton = new QPushButton("+");
	addDrawerButton->setFixedSize(24, 24);
	addDrawerButton->setCursor(Qt::PointingHandCursor);
	addDrawerButton->setToolTip(tr("New drawer"));
	// Explicit style: the page-wide "QPushButton { padding: 8px 12px; }" rule
	// left a 24px button ZERO content area — the + glyph was clipped away and
	// the button invisible on the dark pane (owner-reported). The theme's
	// accent glyph button, identical in both themes.
	addDrawerButton->setStyleSheet(ThemeManager::accentGlyphButtonSheet());
	headerLayout->addWidget(localAssetsLabel);
	headerLayout->addStretch();
	headerLayout->addWidget(addDrawerButton);
	headerRow->setLayout(headerLayout);

	// THE LIBRARY AS A MODEL (D11-LIBRARY-SCALE): one LibraryModel over the
	// listing (no thumbnail column — the tile cache paints each tile by guid,
	// off the UI thread), one proxy for the drawer + search, and two views of
	// it — the tile grid and the list mode's table. No widget per asset: the
	// page used to build 10,000 of them, three style sheets each, at BOOT.
	libraryModel = new LibraryModel(this);
	libraryModel->setTypeNamer([this](int type) { return getAssetType(type); });
	// Only the tile delegate paints a picture (TileRole): the list mode's rows
	// must not fetch and decode one each.
	libraryModel->setDecorated(false);
	libraryModel->setTileSize(QSize(LibraryTileDelegate::kTileWidth, LibraryTileDelegate::kPictureHeight - 2));
	libraryProxy = new LibraryFilterProxy(this);
	libraryProxy->setSourceModel(libraryModel);

    // gui
    _splitter = new QSplitter(this);
	_splitter->setHandleWidth(1);

    //QWidget *_filterBar;
    _navPane = new QWidget;
    QVBoxLayout *navLayout = new QVBoxLayout;
	// The drawers/contents trees run flush to the pane edges (owner
	// direction, matching the tile area); only the Local Assets header row
	// keeps its own margins.
	navLayout->setContentsMargins(0, 0, 0, 0);
	navLayout->setSpacing(0);
    _navPane->setLayout(navLayout);
    _navPane->setStyleSheet(StyleSheet::AssetViewNavPane());

	// The drawers tree (ASSET_DRAWERS_SPEC §1): nested like a file system,
	// rebuilt from the collections table by rebuildDrawerTree().
	treeWidget = new DrawerTreeWidget;
	treeWidget->setObjectName(QStringLiteral("TreeWidget"));
    treeWidget->setAlternatingRowColors(true);
	treeWidget->setColumnCount(2);
	treeWidget->setHeaderHidden(true);
	treeWidget->header()->setMinimumSectionSize(0);
	treeWidget->header()->setStretchLastSection(false);
	treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	// Renames are deliberate acts (context menu / the [+] flow) — a plain
	// double-click on a drawer must not open an editor.
	treeWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
	treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);

    // Parented: app teardown must close and destroy it (an unparented
    // progress dialog is an orphanable top-level that also blocks
    // quitOnLastWindowClosed).
    progressDialog = new ProgressDialog(this);
    progressDialog->setLabelText("Importing assets...");

	rebuildDrawerTree();

	connect(treeWidget, &QTreeWidget::itemClicked, [this](QTreeWidgetItem *item, int column) {
		Q_UNUSED(column);
		libraryProxy->setCollection(item->data(0, Qt::UserRole).toInt());
	});

	// Inline rename commits straight to the database; a refused or empty name
	// snaps back on the rebuild.
	connect(treeWidget, &QTreeWidget::itemChanged, [this](QTreeWidgetItem *item, int column) {
		if (drawerTreeUpdating || column != 0) return;
		const int id = item->data(0, Qt::UserRole).toInt();
		if (id < 0) return;
		const QString name = item->text(0).trimmed();
		// The pane's Collection row reads the drawer's name live.
		if (!name.isEmpty() && db->renameCollection(id, name) && !selectedGuid.isEmpty())
			fetchMetadata(selectedGuid);
		rebuildDrawerTree();
	});

	// Owner spec change (smoke-test round): the + always creates a ROOT
	// drawer — siblings of Uncategorized. Nesting is New Sub-Drawer's job.
	connect(addDrawerButton, &QPushButton::clicked, [this]() {
		createDrawerUnder(-1);
	});

	connect(treeWidget, &QTreeWidget::customContextMenuRequested, [this](const QPoint &pos) {
		auto item = treeWidget->itemAt(pos);
		if (!item) return;
		const int id = item->data(0, Qt::UserRole).toInt();

		QMenu menu(this);
		menu.setStyleSheet(StyleSheet::QMenuDark());
		if (id >= 0) {   // the virtual root keeps its name
			connect(menu.addAction(tr("Rename")), &QAction::triggered, [this, item]() {
				treeWidget->editItem(item, 0);
			});
		}
		connect(menu.addAction(tr("New Sub-Drawer")), &QAction::triggered, [this, id]() {
			createDrawerUnder(id);
		});
		if (id > 0) {   // Uncategorized is the fallback home
			connect(menu.addAction(tr("Delete")), &QAction::triggered, [this, id]() {
				deleteDrawer(id);
			});
		}
		menu.exec(treeWidget->mapToGlobal(pos));
	});

	// Drops (both kinds) are requests — the database decides (cycle guard
	// included), then the tree rebuilds from what it accepted.
	connect(treeWidget, &DrawerTreeWidget::drawerMoveRequested, [this](int id, int parentId) {
		if (!db->setCollectionParent(id, parentId)) return;
		rebuildDrawerTree();
		// Land the selection on the drawer that moved, so the result is visible.
		if (auto item = findDrawerItem(id)) treeWidget->setCurrentItem(item);
	});

	connect(treeWidget, &DrawerTreeWidget::assetMoveRequested, [this](const QString &guid, int drawerId) {
		if (libraryModel->contains(guid)) moveAssetToDrawer(guid, drawerId);
	});

	// The selected asset's own node tree (the model's scene graph, from its
	// stored node-tree blob). Read-only for now; later: delete parts.
	assetNodeTree = new QTreeWidget;
	assetNodeTree->setObjectName(QStringLiteral("AssetNodeTree"));
	assetNodeTree->setColumnCount(1);
	assetNodeTree->setHeaderLabel("Asset Contents");
	assetNodeTree->setAlternatingRowColors(true);

	// Left column split: top half keeps the collections tree (future asset
	// groups), bottom half shows the selected asset's contents.
	// Frameless like the Materials/Editor left columns: Qlementine draws the
	// default QFrame border around item views that the classic sheets used to
	// suppress — the assets nav column must not grow an inner frame.
	treeWidget->setFrameShape(QFrame::NoFrame);
	assetNodeTree->setFrameShape(QFrame::NoFrame);

	auto leftSplit = new QSplitter(Qt::Vertical);
	leftSplit->setHandleWidth(1);
	leftSplit->addWidget(treeWidget);
	leftSplit->addWidget(assetNodeTree);
	leftSplit->setStretchFactor(0, 1);
	leftSplit->setStretchFactor(1, 1);

    navLayout->addWidget(headerRow);
	navLayout->addWidget(leftSplit);

    //QWidget *_previewPane;  
	split = new QSplitter;
	split->setHandleWidth(1);
	split->setOrientation(Qt::Vertical);

    _viewPane = new QWidget;

	auto testL = new QGridLayout;
	emptyGrid = new QWidget;
	emptyGrid->setFixedHeight(96);
	auto emptyL = new QVBoxLayout;
    testL->setContentsMargins(0, 0, 0, 0);
    testL->setSpacing(0);
	emptyL->setSpacing(0);
	auto emptyLabel = new QLabel("You have no assets in your library.");
	auto emptyIcon = new QLabel;
	emptyIcon->setAlignment(Qt::AlignVCenter | Qt::AlignHCenter);
	emptyIcon->setPixmap(IrisUtils::getAbsoluteAssetPath("/app/icons/icons8-empty-box-50.png"));
	emptyLabel->setAlignment(Qt::AlignVCenter | Qt::AlignHCenter);
	emptyLabel->setStyleSheet(StyleSheet::AssetViewEmptyLibraryLabel());
	ThemeRoles::setTextSize(emptyLabel, 16);
	ThemeRoles::setTone(emptyLabel, ThemeRoles::Tone::Muted);
	emptyL->addWidget(emptyIcon);
	emptyL->addWidget(emptyLabel);
	emptyGrid->setLayout(emptyL);


	searchTimer = new QTimer(this);
	searchTimer->setSingleShot(true);   // timer can only fire once after started

	connect(searchTimer, &QTimer::timeout, this, [this]() {
		// Both views read the one proxy: the list mirrors the grid by construction.
		libraryProxy->setSearch(searchTerm);
	});

	filterPane = new QWidget;
	auto filterLayout = new QHBoxLayout;

	backdropLabel = new QLabel("Backdrop: ");
	backdropColor = new QComboBox();
	backdropColor->setView(new QListView());

	backdropColor->addItem("Plain Dark", 1);
	backdropColor->addItem("Plain Light", 2);
	backdropColor->addItem("Checkered Floor", 3);

	filterLayout->addWidget(backdropLabel);
	filterLayout->addWidget(backdropColor);

    backdropColor->setCurrentText("Checkered Floor");

	connect(backdropColor, &QComboBox::currentTextChanged, [this](const QString &text) {
		if (text == "Plain Dark") {
			viewer->changeBackdrop(1);
		}
		else if (text == "Plain Light") {
			viewer->changeBackdrop(2);
		}
        else if (text == "Checkered Floor") {
            viewer->changeBackdrop(3);
        }
	});

	// Tiles/List switch (owner request 2026-08-31): the same grey "▾"
	// popup-button pattern as the editor panel's Display ▾ — the checked
	// entry is the current mode; persisted per user.
	viewModeButton = new QPushButton(tr("View ▾"));
	viewModeButton->setCursor(Qt::PointingHandCursor);
	viewModeMenu = new QMenu(this);
	viewModeMenu->setStyleSheet(StyleSheet::QMenuDarkDesktop());
	auto viewModeGroup = new QActionGroup(viewModeMenu);
	viewModeGroup->setExclusive(true);
	viewTilesAction = viewModeMenu->addAction(tr("Tiles"));
	viewTilesAction->setCheckable(true);
	viewTilesAction->setChecked(true);
	viewModeGroup->addAction(viewTilesAction);
	viewListAction = viewModeMenu->addAction(tr("List"));
	viewListAction->setCheckable(true);
	viewModeGroup->addAction(viewListAction);
	connect(viewModeButton, &QPushButton::pressed, this, [this]() {
		viewModeMenu->exec(viewModeButton->mapToGlobal(QPoint(0, viewModeButton->height())));
	});
	connect(viewTilesAction, &QAction::triggered, this,
	        [this]() { setAssetViewMode(QStringLiteral("tiles")); });
	connect(viewListAction, &QAction::triggered, this,
	        [this]() { setAssetViewMode(QStringLiteral("list")); });
	if (!ThemeManager::classicActive())
		viewModeButton->setStyleSheet(ThemeManager::chromeCompactButtonSheet());
	filterLayout->addWidget(viewModeButton);

	// LIBRARY ▾ — the page's maintenance menu (THUMBS-1). It exists for one
	// entry today: the repair pass for tiles that are already grey, because a
	// thumbnail that failed when the asset was imported has never had a way to
	// be redrawn except one right-click at a time.
	auto *libraryButton = new QPushButton(tr("Library ▾"));
	libraryButton->setCursor(Qt::PointingHandCursor);
	auto *libraryMenu = new QMenu(this);
	libraryMenu->setStyleSheet(StyleSheet::QMenuDarkDesktop());
	connect(libraryMenu->addAction(tr("Rebuild missing thumbnails")), &QAction::triggered, this,
	        [this]() { rebuildMissingThumbnails(); });
	// "SHOW MEMBER TEXTURES" (MATERIAL_BUNDLE_SPEC V-2 on the Assets page): the
	// editor tray has folded a material's picked pictures into the bundle
	// since phase 2; the page the owner browses most still showed every one
	// of them as a tile of its own (the "5 + 15 tiles" after a preset seed).
	// One rule, one function (assettray::libraryList), one switch here with
	// the tray's wording — a CHECKABLE MENU ENTRY, not a checkbox in the
	// filter bar: a checkbox there added its width to the window's floor and
	// pushed it past the 1366 px laptop budget (ui.window_minimum, push #53).
	showMembersAction = libraryMenu->addAction(tr("Show member textures"));
	showMembersAction->setCheckable(true);
	showMembersAction->setToolTip(tr("List the pictures that came in INSIDE a material as tiles "
	                                 "of their own. Your own imported images are always listed."));
	showMembersAction->setChecked(
	    SettingsManager::getDefaultManager()->getValue("library_show_members", false).toBool());
	showMembers = showMembersAction->isChecked();
	connect(showMembersAction, &QAction::toggled, this, [this](bool on) {
		showMembers = on;
		SettingsManager::getDefaultManager()->setValue("library_show_members", on);
		applyShowMembers(on);
	});
	connect(libraryButton, &QPushButton::pressed, this, [libraryButton, libraryMenu]() {
		libraryMenu->exec(libraryButton->mapToGlobal(QPoint(0, libraryButton->height())));
	});
	if (!ThemeManager::classicActive())
		libraryButton->setStyleSheet(ThemeManager::chromeCompactButtonSheet());
	filterLayout->addWidget(libraryButton);

	filterLayout->addStretch();
	filterLayout->addWidget(new QLabel("Search: "));
	le = new QLineEdit();
	le->setFixedWidth(256);
	le->setStyleSheet(StyleSheet::AssetViewSearchField());
	filterLayout->addWidget(le);

	connect(le, &QLineEdit::textChanged, this, [this](const QString &searchTerm) {
		this->searchTerm = searchTerm;
		searchTimer->start(100);
	});

	filterPane->setObjectName("filterPane");
	filterPane->setLayout(filterLayout);
	filterPane->setFixedHeight(48);
	filterPane->setStyleSheet(StyleSheet::AssetViewFilterPane());

	// THE TILE GRID: a QListView over the proxy, a delegate painting each tile
	// from the tile cache (the placeholder until its decode lands). A plain
	// click makes the tile current (pane, fields, buttons) without loading;
	// double-click loads the preview; Shift+click adds to the open project;
	// the drag is the model's (the house asset payload, ui/controls/assetdrag.h).
	tileDelegate = new LibraryTileDelegate(this);
	tileView = new QListView;
	tileView->setObjectName(QStringLiteral("libraryTiles"));
	tileView->setModel(libraryProxy);
	tileView->setItemDelegate(tileDelegate);
	tileView->setViewMode(QListView::IconMode);
	tileView->setMovement(QListView::Static);
	tileView->setResizeMode(QListView::Adjust);
	tileView->setUniformItemSizes(true);
	tileView->setSpacing(5);
	tileView->setSelectionMode(QAbstractItemView::SingleSelection);
	tileView->setDragEnabled(true);
	tileView->setDragDropMode(QAbstractItemView::DragOnly);
	tileView->setMouseTracking(true);
	tileView->setFrameShape(QFrame::NoFrame);
	tileView->setContextMenuPolicy(Qt::CustomContextMenu);
	tileView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	tileView->setCursor(Qt::PointingHandCursor);
	{
		QPixmap placeholder(LibraryTileDelegate::kTileWidth, LibraryTileDelegate::kPictureHeight - 2);
		placeholder.fill(Qt::transparent);
		libraryModel->setPlaceholder(placeholder);
	}
	connect(tileView, &QListView::clicked, this, [this](const QModelIndex &index) {
		const QString guid = index.data(LibraryModel::GuidRole).toString();
		if (QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier)) {
			// Shift+click: straight into the open project.
			if (services && services->project && services->project->isSceneOpen())
				addAssetItemToProject(guid);
			return;
		}
		lightSelect(guid);
	});
	connect(tileView, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
		openTile(index.data(LibraryModel::GuidRole).toString());
	});
	connect(tileView, &QListView::customContextMenuRequested, this, [this](const QPoint &pos) {
		const QModelIndex index = tileView->indexAt(pos);
		if (!index.isValid()) return;
		showTileMenu(index.data(LibraryModel::GuidRole).toString(),
		             tileView->viewport()->mapToGlobal(pos));
	});
	// "Loading…" pulses over the tile whose preview loads (ASSET_DRAWERS_SPEC §1).
	loadingPulse = new QTimer(this);
	loadingPulse->setInterval(350);
	connect(loadingPulse, &QTimer::timeout, this, [this]() {
		loadingPhase = !loadingPhase;
		tileDelegate->setPulse(loadingPhase);
		const QModelIndex index = libraryProxy->mapFromSource(libraryModel->indexOfGuid(loadingGuid));
		if (index.isValid()) tileView->update(index);
	});

	// The list view (owner request 2026-08-31): name/type/size rows over the
	// SAME proxy — the drawer and the search reach it by construction — and
	// the same selection/preview/context plumbing as the tiles.
	assetListView = new QTreeView;
	assetListView->setModel(libraryProxy);
	assetListView->setRootIsDecorated(false);
	assetListView->setItemsExpandable(false);
	assetListView->setAlternatingRowColors(false);
	assetListView->setUniformRowHeights(true);
	assetListView->setIconSize(QSize(0, 0));
	assetListView->setFrameShape(QFrame::NoFrame);
	assetListView->setSelectionMode(QAbstractItemView::SingleSelection);
	assetListView->setDragEnabled(true);
	assetListView->setDragDropMode(QAbstractItemView::DragOnly);
	assetListView->header()->setStretchLastSection(false);
	assetListView->header()->setSectionResizeMode(LibraryModel::NameColumn, QHeaderView::Stretch);
	assetListView->header()->setSectionResizeMode(LibraryModel::TypeColumn, QHeaderView::ResizeToContents);
	assetListView->header()->setSectionResizeMode(LibraryModel::SizeColumn, QHeaderView::ResizeToContents);
	assetListView->setContextMenuPolicy(Qt::CustomContextMenu);
	assetListView->setVisible(false);
	assetListView->setStyleSheet(StyleSheet::AssetViewPaneBorderless());
	connect(assetListView, &QTreeView::clicked, this, [this](const QModelIndex &index) {
		lightSelect(index.data(LibraryModel::GuidRole).toString());
	});
	connect(assetListView, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
		openTile(index.data(LibraryModel::GuidRole).toString());
	});
	connect(assetListView, &QTreeView::customContextMenuRequested, this, [this](const QPoint &pos) {
		const QModelIndex index = assetListView->indexAt(pos);
		if (!index.isValid()) return;
		showTileMenu(index.data(LibraryModel::GuidRole).toString(),
		             assetListView->viewport()->mapToGlobal(pos));
	});

	// The post-dialog tail pump: one viewer preview/thumbnail per event-loop
	// turn (the app keeps painting and clicking between items), with a
	// subtle status strip under the grid while it works.
	tailStatusLabel = new QLabel;
	tailStatusLabel->setStyleSheet(StyleSheet::AssetViewTailStatus());
	ThemeRoles::setPadding(tailStatusLabel, 10, 4, 10, 4);
	ThemeRoles::setTone(tailStatusLabel, ThemeRoles::Tone::Muted);
	tailStatusLabel->setVisible(false);
	tailQueue = new ImportTailQueue(this);
	connect(tailQueue, &ImportTailQueue::progress, this, [this](int done, int total) {
		tailStatusLabel->setText(tr("Rendering previews… (%1 of %2)")
		                             .arg(done + 1).arg(total));
		tailStatusLabel->setVisible(true);
	});
	connect(tailQueue, &ImportTailQueue::finished, this, [this]() {
		tailStatusLabel->setVisible(false);
	});

	auto views = new QWidget;
	auto viewsL = new QVBoxLayout;
	// Clear the filter/search toolbar (owner direction 2026-08-31 — was
	// deliberately flush); the tile grid adds its own inner margins too.
	viewsL->setContentsMargins(0, 6, 0, 0);
	viewsL->setSpacing(0);
	viewsL->addWidget(emptyGrid);
	viewsL->addWidget(tileView);
	viewsL->addWidget(assetListView);
	viewsL->addWidget(tailStatusLabel);
	views->setLayout(viewsL);
    views->setStyleSheet(StyleSheet::AssetViewPaneBackground());

	testL->addWidget(filterPane, 0, 0);
	testL->addWidget(views, 1, 0);
    _viewPane->setLayout(testL);

	// THE EMPTY STATE follows the LIBRARY's row count (not the filtered one: a
	// drawer with nothing in it still shows the filter bar and the grid).
	tileView->setVisible(false);
	filterPane->setVisible(false);
	const auto syncEmptyState = [this]() {
		const bool any = libraryModel->rowCount() > 0;
		filterPane->setVisible(any);
		emptyGrid->setVisible(!any);
		const bool listMode = assetViewMode == QStringLiteral("list");
		tileView->setVisible(any && !listMode);
		assetListView->setVisible(any && listMode);
	};
	connect(libraryModel, &QAbstractItemModel::modelReset, this, syncEmptyState);
	connect(libraryModel, &QAbstractItemModel::rowsInserted, this, syncEmptyState);
	connect(libraryModel, &QAbstractItemModel::rowsRemoved, this, syncEmptyState);

	// THE ROWS: the library listing, one query without a thumbnail column
	// (services/assettray.h libraryList — the grid rows less a legacy Shader row
	// and, unless "Show member textures" is on, the pictures that arrived inside
	// a material bundle). Nothing is decoded here.
	reloadLibrary();

	// Restore the persisted Tiles/List choice (owner request 2026-08-31).
	setAssetViewMode(settings->getValue(QStringLiteral("assetView/viewMode"),
	                                    QStringLiteral("tiles")).toString(), false);

    _metadataPane = new QWidget; 
	_metadataPane->setObjectName(QStringLiteral("MetadataPane"));
    _metadataPane->setStyleSheet(StyleSheet::AssetViewPaneBackground());
    QVBoxLayout *metaLayout = new QVBoxLayout;
    metaLayout->setContentsMargins(10, 10, 10, 10);
    metaLayout->setSpacing(8);
	assetDropPad = new QWidget;
	assetDropPad->setAcceptDrops(true);
	assetDropPad->installEventFilter(this);
	QSizePolicy policy;
	policy.setHorizontalPolicy(QSizePolicy::Expanding);
	assetDropPad->setSizePolicy(policy);
	assetDropPad->setObjectName(QStringLiteral("assetDropPad"));
	auto assetDropPadLayout = new QVBoxLayout;
	QLabel *assetDropPadLabel = new QLabel("Drop an asset to import...");
	assetDropPadLayout->setSpacing(6);
	assetDropPadLayout->setContentsMargins(6, 6, 6, 6);
	assetDropPadLabel->setObjectName(QStringLiteral("assetDropPadLabel"));
	assetDropPadLabel->setAlignment(Qt::AlignHCenter);

	assetDropPadLayout->addWidget(assetDropPadLabel);
	QPushButton *browseButton = new QPushButton("Import Asset");
	QPushButton *downloadWorld = new QPushButton("Download Assets");

	connect(downloadWorld, &QPushButton::pressed, []() {
		QDesktopServices::openUrl(QUrl("https://www.jahshaka.com/get/models/"));
	});

	QWidget *importButtons = new QWidget;
	auto ipbl = new QHBoxLayout;
    ipbl->setContentsMargins(0, 0, 0, 0);
	ipbl->addWidget(browseButton);
	ipbl->addWidget(downloadWorld);
	importButtons->setLayout(ipbl);

    importButtons->setStyleSheet(StyleSheet::AssetViewImportButtons());

	// The buttons sit BELOW the drag-and-drop box (owner direction — the
	// right-column rework had moved them inside it): importButtons joins
	// metaLayout right after assetDropPad, not assetDropPadLayout.

	updateAsset = new QPushButton("Update");
	updateAsset->setStyleSheet(StyleSheet::AssetViewUpdateButton());
	updateAsset->setVisible(false);

	// THE SIZE ROW's one action (SPECS/IMPORT_DIALOG_SPEC.md §6), created here
	// so the chrome-button styling loop below reaches it; the row itself is
	// assembled with the metadata table.
	importSettingsButton = new QPushButton(tr("Import settings\u2026"));

	addToProject = new QPushButton("Add to Project");
	addToProject->setStyleSheet(StyleSheet::AssetViewAddToProjectButton());
	addToProject->setEnabled(false);

    deleteFromLibrary = new QPushButton("Delete From Library");
	deleteFromLibrary->setStyleSheet(StyleSheet::AssetViewDeleteButton());
    deleteFromLibrary->setEnabled(false);

	if (!ThemeManager::classicActive()) {
		// visible drop-target affordance (the classic dashed box was lost with
		// the sheet kill-switch) + the shared chrome button spec
		assetDropPad->setStyleSheet(ThemeManager::dropZoneSheet(
			QStringLiteral("assetDropPad"), QStringLiteral("assetDropPadLabel")));
		for (QPushButton *chromeBtn : { browseButton, downloadWorld,
		                                importSettingsButton, deleteFromLibrary })
			chromeBtn->setStyleSheet(ThemeManager::chromeButtonSheet());
		updateAsset->setStyleSheet(ThemeManager::chromeAccentButtonSheet());
		addToProject->setStyleSheet(ThemeManager::chromeAccentButtonSheet());
	}

	renameModel = new QLabel("Name:");
	renameModelField = new QLineEdit();

	tagModel = new QLabel("Tags:");
	tagModelField = new QLineEdit();
	tagModelField->setPlaceholderText("(comma separated)");

	renameWidget = new QWidget;
	auto renameLayout = new QHBoxLayout;
    renameLayout->setContentsMargins(0, 0, 0, 0);
	renameLayout->setSpacing(12);
	renameLayout->addWidget(renameModel);
	renameLayout->addWidget(renameModelField);
	renameWidget->setLayout(renameLayout);
	renameWidget->setVisible(false);

	tagWidget = new QWidget;
	auto tagLayout = new QHBoxLayout;
    tagLayout->setContentsMargins(0, 0, 0, 0);
	tagLayout->setSpacing(12);
	tagLayout->addWidget(tagModel);
	tagLayout->addWidget(tagModelField);
	tagWidget->setLayout(tagLayout);
	tagWidget->setVisible(false);

	connect(updateAsset, &QPushButton::pressed, [this]() {
		// ONE path for name + tags (assettags, 2026-09-06 audit F3): this
		// button, assets.rename and assets.setTags all write through the same
		// service, which is also where the "changing one must not wipe the
		// other" rule lives.
		if (selectedGuid.isEmpty()) return;
		const QStringList typedTags = tagModelField->text().split(QLatin1Char(','),
																  Qt::SkipEmptyParts);
		const QString newName = renameModelField->text();
		assettags::write(db, selectedGuid, newName, typedTags);
		const QStringList stored = assettags::tagsOf(db, selectedGuid);
		libraryModel->update(selectedGuid, [&](LibraryRow &row) {
			row.name = newName;
			row.tags = stored;
		});
		fetchMetadata(selectedGuid);
	});

	// THE SIZE ROW's one action (SPECS/IMPORT_DIALOG_SPEC.md §8): reopen the
	// import decision. The shell owns the dialog — it is the one place that has
	// both the widget layer and the ScriptHost, so OK commits through the
	// assets.reimport verb and the open-scene swap happens with it.
	connect(importSettingsButton, &QPushButton::clicked, this, [this]() {
		if (!selectedGuid.isEmpty()) emit reimportAssetRequested(selectedGuid);
	});

	connect(addToProject, &QPushButton::pressed, [this]() {
		if (!selectedGuid.isEmpty()) addAssetItemToProject(selectedGuid);
	});

	connect(deleteFromLibrary, &QPushButton::pressed, [this]() {
		deleteAssetFromLibrary(selectedGuid);
	});

	connect(browseButton, &QPushButton::pressed, [=]() {
		// Built from the type lists so a new library type extends the dialog
		// automatically (§3). The old filter's phantom *.3ds is gone
		// (3ds was never in MODEL_EXTS).
		QStringList patterns;
		for (const auto &ext : Constants::MODEL_EXTS) patterns << "*." + ext;
		for (const auto &ext : Constants::IMAGE_EXTS) patterns << "*." + ext;
		for (const auto &ext : Constants::AUDIO_EXTS) patterns << "*." + ext;
		for (const auto &ext : Constants::VIDEO_EXTS) patterns << "*." + ext;
		// The other four importers the pipeline owns. They were absent here,
		// so the only way to reach ShaderImporter/MaterialImporter/IesImporter/
		// FileImporter from this page was drag-and-drop (deep audit 2026-09).
		// (*.shader is GONE — phase 2's Deletes: the ShaderImporter that
		// ingested one is deleted with the readers it fed.)
		for (const auto &ext : Constants::MATERIAL_EXTS) patterns << "*." + ext;
		for (const auto &ext : Constants::LIGHT_PROFILE_EXTS) patterns << "*." + ext;
		for (const auto &ext : Constants::WHITELIST) patterns << "*." + ext;
		patterns << QStringLiteral("*.%1").arg(QLatin1String(assetshare::extension()));

		const auto files = QFileDialog::getOpenFileNames(this,
		                                                 tr("Import Assets"),
		                                                 QString(),
		                                                 tr("Assets (%1)").arg(patterns.join(' ')));
		importFiles(files);
	});

	assetDropPad->setLayout(assetDropPadLayout);

    metaLayout->addWidget(assetDropPad);
    metaLayout->addWidget(importButtons);

	auto metadata = new QWidget;
	auto l = new QVBoxLayout;
	l->setSpacing(8);
	QSizePolicy policy2;
	policy2.setVerticalPolicy(QSizePolicy::Preferred);
	policy2.setHorizontalPolicy(QSizePolicy::Preferred);
	metadataMissing = new QLabel("Nothing selected...");
	metadataMissing->setAlignment(Qt::AlignCenter);
	metadataMissing->setStyleSheet(StyleSheet::AssetViewNothingSelected());
	ThemeRoles::setPadding(metadataMissing, 12, 12, 12, 12);
	metadataMissing->setSizePolicy(policy2);
	// ONE two-column label/value table (owner 2026-08-31) — replaces the old
	// stack of "Key: value" labels. fetchMetadata() renders every row (type,
	// the rich per-type block, public/author/license/collection) into it.
	metadataDetails = new QLabel;
	metadataDetails->setSizePolicy(policy2);
	metadataDetails->setWordWrap(true);
	metadataDetails->setTextFormat(Qt::RichText);
	metadataDetails->setVisible(false);

	l->addWidget(metadataMissing);

	l->addWidget(renameWidget);
	l->addWidget(tagWidget);

	l->addWidget(metadataDetails);

	// ---- THE SIZE ROW (services/extentmeasure.h) --------------------------
	//
	// "Imported size: 1.75 × 0.5 × 0.3 m" and ONE button that reopens the
	// import decision. The measurement is information; there is no policy
	// behind it any more (SPECS/IMPORT_DIALOG_SPEC.md §6).
	fitRow = new QWidget;
	{
		auto *fitLayout = new QVBoxLayout;
		fitLayout->setContentsMargins(0, 0, 0, 0);
		fitLayout->setSpacing(4);
		fitLabel = new QLabel;
		fitLabel->setWordWrap(true);
		fitLabel->setTextFormat(Qt::RichText);
		auto *fitButtons = new QHBoxLayout;
		fitButtons->setContentsMargins(0, 0, 0, 0);
		fitButtons->addWidget(importSettingsButton);
		fitButtons->addStretch(1);
		fitLayout->addWidget(fitLabel);
		fitLayout->addLayout(fitButtons);
		fitRow->setLayout(fitLayout);
		fitRow->setVisible(false);
	}
	l->addWidget(fitRow);

	l->addWidget(updateAsset);

	metadata->setLayout(l);
	auto header = new QLabel("Asset Metadata");
	header->setAlignment(Qt::AlignCenter);
	header->setStyleSheet(StyleSheet::AssetViewMetadataHeader());
	metaLayout->addWidget(header);
	metaLayout->addWidget(metadata);

	metaLayout->addStretch();

	auto projectSpecific = new QWidget;
	auto ll = new QVBoxLayout;
	ll->addWidget(addToProject);
	ll->addWidget(deleteFromLibrary);
	projectSpecific->setLayout(ll);
	metaLayout->addWidget(projectSpecific);

    _metadataPane->setLayout(metaLayout);

    // Page order IS the PreviewPage enum (ui/pages/previewrouter.h).
    viewers->addWidget(viewer->asWidget());      // PreviewPage::Viewer3D
    viewers->addWidget(assetImageViewer);        // PreviewPage::Image
    viewers->addWidget(assetAudioViewer);        // PreviewPage::Audio
    viewers->addWidget(assetVideoViewer);        // PreviewPage::Video
    viewers->addWidget(assetFileViewer);         // PreviewPage::Placeholder
    viewers->addWidget(assetEmptyViewer);        // empty state (index 5, unrouted)
    viewersWidget->setLayout(viewers);
    // Nothing is selected when the page opens: show the empty state, not a
    // stray page (the router switches to the right page on selection).
    viewers->setCurrentIndex(viewers->indexOf(assetEmptyViewer));

    // Leaving a media page stops its player (§2: viewers stop on page change).
    connect(viewers, &QStackedLayout::currentChanged, this, [this](int index) {
        if (index != static_cast<int>(PreviewPage::Audio) && mediaPlayer)
            mediaPlayer->stop();
        if (index != static_cast<int>(PreviewPage::Video) && assetVideoViewer)
            assetVideoViewer->stop();
    });

	split->addWidget(viewersWidget);
	split->addWidget(_viewPane);

    _splitter->addWidget(_navPane);
    _splitter->addWidget(split);
    _splitter->addWidget(_metadataPane);

    _splitter->setStretchFactor(0, 0);
    _splitter->setStretchFactor(1, 3);
    _splitter->setStretchFactor(2, 0);
    // THE COLUMNS ARE THE EDITOR'S COLUMNS (owner, 2026-09-11, smoke S1). The
    // metadata pane used to live in a 280-380 band of its own — narrower than
    // the editor's right column at one end, and pinned by a MAXIMUM the user
    // could not drag past at the other. Both sides now come from
    // ui/style/panelmetrics.h: the same minimums as the editor's columns, the
    // same opening widths, and no maximum (the stretch factors keep the grid
    // in the middle growing, which is what the old cap was really for).
    _navPane->setMinimumWidth(PanelMetrics::leftColumnMinWidth);
    _metadataPane->setMinimumWidth(PanelMetrics::rightColumnMinWidth);
    _splitter->setSizes({ PanelMetrics::leftColumnWidth, 800, PanelMetrics::rightColumnWidth });
    
    // Offline-store banner (§3.1.2): persistent, non-modal, above the page.
    storeOfflineBanner = new QWidget(this);
    storeOfflineBanner->setObjectName(QStringLiteral("StoreOfflineBanner"));
    storeOfflineBanner->setStyleSheet(StyleSheet::AssetViewStoreOfflineBanner());
    ThemeRoles::setSurface(storeOfflineBanner, ThemeRoles::Surface::Warning);
    {
        auto *bl = new QHBoxLayout(storeOfflineBanner);
        bl->setContentsMargins(12, 6, 12, 6);
        storeOfflineLabel = new QLabel(storeOfflineBanner);
        bl->addWidget(storeOfflineLabel, 1);
        auto *reconnect = new QPushButton(tr("Reconnect"), storeOfflineBanner);
        connect(reconnect, &QPushButton::clicked, this, &AssetView::refreshStoreBanner);
        bl->addWidget(reconnect);
    }
    storeOfflineBanner->hide();

    QGridLayout *layout = new QGridLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(storeOfflineBanner, 0, 0);
    layout->addWidget(_splitter, 1, 0);
    setLayout(layout);

	refreshStoreBanner();
	updateAddToProjectButton();   // initial disabled state carries its tooltip

	setStyleSheet(StyleSheet::AssetViewPanel());
}

// THE TILE GESTURES, by guid (the tile flip, ASSET_DRAWERS_SPEC §1): a plain
// click makes the asset CURRENT — the pane, the rename/tags fields and the
// bottom-right buttons act on it — without loading anything; a double click
// (and assets.select) also loads its preview.
void AssetView::showSelection(const QString &guid)
{
	const LibraryRow *row = libraryModel->rowFor(guid);
	if (!row) return;
	selectedGuid = guid;
	selectedProperties = QJsonObject();
	// Both views select the same row (they share the proxy).
	const QModelIndex index = libraryProxy->mapFromSource(libraryModel->indexOfGuid(guid));
	if (index.isValid()) {
		tileView->setCurrentIndex(index);
		assetListView->setCurrentIndex(index);
	}

	fetchMetadata(guid);
	populateAssetNodeTree(guid, row->type);

	renameModelField->setText(QFileInfo(row->name).baseName());
	tagModelField->setText(row->tags.join(QStringLiteral(", ")));

	renameWidget->setVisible(true);
	tagWidget->setVisible(true);
	updateAsset->setVisible(true);
	deleteFromLibrary->setEnabled(true);
	updateAddToProjectButton();
}

void AssetView::lightSelect(const QString &guid)
{
	showSelection(guid);
}

void AssetView::openTile(const QString &guid)
{
	const LibraryRow *row = libraryModel->rowFor(guid);
	if (!row) return;
	stopMediaPreviews();   // switching tiles/pages stops playback (§2)
	showSelection(guid);   // reads the row's properties into selectedProperties

	QVector3D pos;
	QVector3D rot;
	int distObj = 5;   // was read uninitialized when no camera props were stored
	bool cached = false;
	const QJsonObject camera = selectedProperties.value(QStringLiteral("camera")).toObject();
	if (!camera.isEmpty()) {
		const QJsonObject posObj = camera.value(QStringLiteral("pos")).toObject();
		const QJsonObject rotObj = camera.value(QStringLiteral("rot")).toObject();
		distObj = camera.value(QStringLiteral("distFromPivot")).toDouble(5.0);
		pos = QVector3D(posObj.value(QStringLiteral("x")).toDouble(0), posObj.value(QStringLiteral("y")).toDouble(0),
		                posObj.value(QStringLiteral("z")).toDouble(0));
		rot = QVector3D(rotObj.value(QStringLiteral("x")).toDouble(0), rotObj.value(QStringLiteral("y")).toDouble(0),
		                rotObj.value(QStringLiteral("z")).toDouble(0));
		cached = true;
	}

	// The loading overlay (§1): visible from the double-click until the
	// viewer reports the load finished. The synchronous loads below block the
	// event loop, so paint it before starting.
	setLoadingTile(guid);
	tileView->viewport()->repaint();

	// PreviewRouter (ASSET_MEDIA_SPEC §2): ONE type→page map decides the
	// viewer; the switch also stops whatever the previous page was playing.
	const QString name = row->name;
	const auto type = static_cast<ModelTypes>(row->type);
	const PreviewPage page = PreviewRouter::pageFor(type);
	viewers->setCurrentIndex(static_cast<int>(page));

	// The row's primary file, resolved through the CAS by guid.
	const QString storeFile = AssetCas::resolveSource(
	    QSqlDatabase::database(), AssetStorePaths::root(), guid);

	switch (page) {
	case PreviewPage::Viewer3D: {
		// THE SAVED CAMERA IS RESTORED ONLY IF THERE IS ONE (`cached`): an
		// asset with no stored camera block keeps the framing its load
		// computed (smoke S5: the defaults put the camera inside big models).
		const auto restoreCamera = [&]() {
			viewer->orientCamera(iris::fromQt(pos), iris::fromQt(rot), distObj);
		};
		if (type == ModelTypes::Object || type == ModelTypes::ParticleSystem) {
			// The model file IS the asset's source-role object.
			if (viewer->cachedAsset(guid))
				viewer->addNodeToScene(viewer->cachedAsset(guid), guid, cached, false);
			else
				viewer->loadJafModel(storeFile, guid, false, true, !cached);
			if (cached) restoreCamera();
			else        viewer->frameSubject();   // the editor's F, on the new subject
		}
		else if (type == ModelTypes::Material) {
			viewer->loadJafMaterial(guid);
			if (cached) restoreCamera();
		}
		else if (type == ModelTypes::Sky) {
			viewer->loadJafSky(guid);
		}
		break;
	}
	case PreviewPage::Image:
		showImagePreview(storeFile);
		break;
	case PreviewPage::Audio:
		showAudioPreview(guid, storeFile, name);
		break;
	case PreviewPage::Video:
		showVideoPreview(storeFile, name);
		break;
	case PreviewPage::Placeholder:
		showFilePlaceholder(name);
		break;
	}

	// Types with no viewer load (textures, audio) end up here with the
	// overlay still up — and the viewer callback already fired for the rest.
	// Either way the overlay is done.
	clearLoadingTile();
}

void AssetView::updateAddToProjectButton()
{
	const LibraryRow *row = libraryModel->rowFor(selectedGuid);
	const bool haveTile = row != nullptr;
	const bool sceneOpen = services && services->project && services->project->isSceneOpen();
	const bool storeOnline = AssetStoreService::online();
	addToProject->setEnabled(haveTile && sceneOpen && storeOnline);
	if (!storeOnline)
		addToProject->setToolTip(tr("Asset store offline: %1")
		    .arg(QDir::toNativeSeparators(AssetStorePaths::root())));
	else if (!haveTile)
		addToProject->setToolTip(tr("Click an asset tile to select it first"));
	else if (!sceneOpen)
		addToProject->setToolTip(tr("Open a project to add assets to it"));
	else
		addToProject->setToolTip(tr("Add \"%1\" to the open project")
		    .arg(QFileInfo(row->name).baseName()));
}

void AssetView::refreshStoreBanner()
{
	if (!storeOfflineBanner) return;
	const bool online = AssetStoreService::online();
	if (online) {
		storeOfflineBanner->hide();
	}
	else {
		storeOfflineLabel->setText(tr("Asset store offline: %1 — thumbnails, search and drawers still work; previews and Add to Project need the files.")
		    .arg(QDir::toNativeSeparators(AssetStorePaths::root())));
		storeOfflineBanner->show();
	}
	updateAddToProjectButton();
}

void AssetView::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	// The scene may have opened/closed since the page was last shown — and
	// the store drive may have come or gone (§3.1.2).
	refreshStoreBanner();
	updateAddToProjectButton();
}

// The mesh tail, one item per event-loop turn (see scheduleViewerTails). The
// pipeline half already ran on the batch runner's worker; ImportMeshTail
// previews the COMMITTED asset by guid (the library blob — smoke S6; the
// import-time fragment it used to take is gone, and so is the ImportResult
// field that carried it) and the tile that appeared mid-batch takes the
// stored thumbnail.
void AssetView::finishMeshTailItem(const ImportResult &result, const QString &fileName)
{
    // The grid tile and metadata pane read the `filename` member, which only
    // the browse dialog used to set — a drag-and-dropped model got a nameless
    // tile until restart (ASSETS_AUDIT.md finding 2). Every import path lands
    // here, so set it here.
    filename = fileName;
    renameModelField->setText(QFileInfo(fileName).baseName());

    ImportMeshTail::run(db, viewer, result, fileName);

    // THE ONE THUMBNAIL ROUTINE (smoke S6). This used to persist the viewer's
    // shot of the LIVE import fragment — a white, untextured render, because
    // that fragment's material paths point into the staging directory the
    // commit deleted. assetthumb::storeObject is what `assets.refreshThumbnail`
    // runs: the committed blob, fitted, framed like the editor's F. A page
    // import and a scripted import now store the same image.
    assetthumb::storeObject(db, project, result.assetGuid, EngineHost::instance().engine());

    // The tile's picture follows the stored thumbnail by itself (the write drops
    // the cached tile; the next paint reads the new one). Its loading pulse ends.
    if (loadingGuid == result.assetGuid) clearLoadingTile();
    libraryModel->refreshTile(result.assetGuid);

    renameWidget->setVisible(true);
    tagWidget->setVisible(true);
    updateAsset->setVisible(true);

    // SELECT WHAT WAS JUST IMPORTED (smoke S4: "importing an asset does not
    // select the new tile; a double click is needed"). The last item of a
    // batch wins, which is the one whose preview is on screen anyway.
    selectAsset(result.assetGuid);
}

// The tile gesture as a call — `assets.select`, the import tail's last step,
// and anything else that wants to put an asset in front of the user. The
// scroll matters: a library of hundreds files the new tile off-screen, and a
// selection nobody can see is not a selection.
bool AssetView::selectAsset(const QString &guid)
{
    if (guid.isEmpty()) return false;
    if (!libraryModel->contains(guid) && !db->fetchAsset(guid).guid.isEmpty()) {
        // The page MIRRORS the library: a row that exists but whose tile has
        // not been announced yet (a verb's import, a queued announcement that
        // has not run) still selects — the row is added now. The announcement
        // handler checks the model first, so nothing doubles up.
        addLibraryTileForAsset(guid);
    }
    if (!libraryModel->contains(guid)) return false;
    // A tile the drawer or the search hides is shown: a selection nobody can
    // see is not a selection.
    QModelIndex index = libraryProxy->mapFromSource(libraryModel->indexOfGuid(guid));
    if (!index.isValid()) {
        treeWidget->setCurrentItem(rootItem);
        libraryProxy->setCollection(-1);
        le->clear();
        libraryProxy->setSearch(QString());
        index = libraryProxy->mapFromSource(libraryModel->indexOfGuid(guid));
    }
    openTile(guid);                       // the double-click path: preview + pane
    if (index.isValid()) {
        tileView->scrollTo(index);
        assetListView->scrollTo(index);
    }
    return true;
}

QString AssetView::selectedAssetGuid() const
{
    return selectedGuid;
}

// THE import dispatch (ASSET_DRAWERS_SPEC §3): drop pad and browse dialog both
// land here; one switch keyed on ModelTypes decides each file's request, so a
// new library type (Video, …) is one case. The batch then runs THREADED —
// ImportBatchRunner + the cancellable progress dialog (multi-file drops count
// "N of M" through one dialog).
void AssetView::importFiles(const QStringList &fileNames)
{
	QVector<ImportRequest> requests;
	QStringList shareFiles;
	for (const auto &fileName : fileNames) {
		if (fileName.isEmpty()) continue;
		// A SHARE FILE (.jbundle — every export this app writes, JAF-EXPORTS-1)
		// carries catalog ROWS, not a file to convert: it lands through the
		// import `assets.import` uses (services/assetshare.h), never the
		// model pipeline, which would refuse its extension.
		if (assetshare::looksLikeBundle(fileName)) { shareFiles.append(fileName); continue; }

		ImportRequest request;
		request.sourcePath = fileName;

		const QString suffix = QFileInfo(fileName).suffix().toLower();
		const ModelTypes type = AssetHelper::getAssetTypeFromExtension(suffix);
		switch (type) {
		case ModelTypes::Texture:
		case ModelTypes::Music:
		case ModelTypes::Video:
			request.typeHint = static_cast<int>(type);
			request.drawerId = selectedDrawerId();
			break;
		case ModelTypes::Mesh:
			// Meshes and everything they reference: the viewer-driven path
			// (handleImportedFile keys the mesh tail off this hint).
			request.typeHint = static_cast<int>(ModelTypes::Mesh);
			break;
		default:
			// NO HINT: the pipeline sniffs. This `default` used to say
			// "Mesh", which pinned pickImporter to MeshImporter alone and
			// made FOUR of the nine importers unreachable from this page —
			// a .shader, .material, .ies or whitelisted text file dropped
			// on the Assets page could only ever fail as "not a model"
			// (deep audit 2026-09, area 4).
			break;
		}
		requests.append(request);
	}
	if (!shareFiles.isEmpty()) importShareFiles(shareFiles);
	if (requests.isEmpty()) return;

	// THE IMPORT DECISION (SPECS/IMPORT_DIALOG_SPEC.md §8): one dialog per
	// MODEL file, before anything is read and before any progress dialog is
	// up. Media never prompts. A file the user skipped drops out of the batch —
	// the rest of the drop still imports, and "Skip the rest" drops the tail.
	QStringList modelFiles;
	for (const ImportRequest &request : requests)
		if (isModelImportPath(request.sourcePath)) modelFiles.append(request.sourcePath);
	if (!modelFiles.isEmpty()) {
		// AN OPEN QUESTION IS AN IMPORT IN PROGRESS. Nothing else may start one
		// while a modal dialog of ours is up — see AssetWidget::importAsset for
		// the abort this guards against.
		if (mAsking) return;
		mAsking = true;
		const QHash<QString, QJsonObject> records =
		    ImportSettingsDialog::askForFiles(modelFiles, this);
		mAsking = false;
		QVector<ImportRequest> kept;
		for (ImportRequest request : requests) {
			if (!isModelImportPath(request.sourcePath)) { kept.append(request); continue; }
			if (!records.contains(request.sourcePath)) continue;   // the user skipped it
			request.settings = records.value(request.sourcePath);
			kept.append(request);
		}
		requests = kept;
	}
	if (!requests.isEmpty()) runImportBatch(requests);
}

void AssetView::importShareFiles(const QStringList &files)
{
	QStringList errors;
	QString last;
	for (const QString &file : files) {
		const assetshare::ImportResult landed = assetshare::importBundle(db, project, file);
		if (!landed.ok()) {
			errors << tr("%1: %2").arg(QFileInfo(file).fileName(), landed.error);
			continue;
		}
		last = landed.guid;
	}
	// Every entry a pack carried is a row now; the listing is re-read once
	// (one query, no thumbnails) rather than tile by tile.
	reloadLibrary();
	if (!last.isEmpty()) selectAsset(last);
	if (!errors.isEmpty())
		QMessageBox::warning(this, tr("Import failed"), errors.join(QStringLiteral("\n")),
		                     QMessageBox::Ok);
}

bool AssetView::shutdownImports(int msTimeout)
{
	if (progressDialog) progressDialog->close();
	if (tailQueue) tailQueue->clear();
	pendingViewerTails.clear();
	pendingVideoThumbGuids.clear();
	stopMediaPreviews();
	if (!importRunner) return true;
	// waitForDone pumps events, which can delete the runner (its finished
	// handler deleteLater()s it) — hold it weakly and never touch the raw
	// member afterwards.
	QPointer<ImportBatchRunner> runner(importRunner);
	runner->requestAbort();
	if (runner->waitForDone(msTimeout)) return true;
	qWarning("AssetView: import worker still running after %dms", msTimeout);
	return false;
}

void AssetView::runImportBatch(const QVector<ImportRequest> &requests)
{
	// ONE IMPORTER AT A TIME (SEED-SMALL-1; the rule MaterialPresetSeeder::
	// finishNow documents, which until now only the MATERIAL doors obeyed).
	// The first-run seed imports the presets' maps through this same pipeline,
	// and neither importer dedups ROWS: each asks the library "do you have
	// these bytes" and both hear no. A person importing one of those pictures
	// during the seconds the seed runs therefore got their row AND a second,
	// stamped one that no definition names — a hidden orphan, the worse half
	// of the duplicate the content check was added to prevent. So the seed
	// stands down before a USER's import starts; what it had not reached, the
	// next launch finishes.
	MaterialPresetSeeder::instance().finishNow();

	if (importRunner && importRunner->isRunning()) {
		Toast *t = libraryToast();
		t->showToast(tr("Import in progress"),
		             tr("Wait for the current import to finish (or cancel it) first."));
		return;
	}

	importErrors.clear();
	pendingViewerTails.clear();
	pendingVideoThumbGuids.clear();

	importRunner = new ImportBatchRunner(db, project, this);
	importRunner->setRequests(requests);

	progressDialog->resetCancel();
	progressDialog->setCancelVisible(true);
	progressDialog->setRange(0, 0);
	progressDialog->setValue(0);
	progressDialog->setLabelText(tr("Preparing import…"));
	progressDialog->setStageText(QString());
	progressDialog->show();

	connect(progressDialog, &ProgressDialog::canceled,
	        importRunner, &ImportBatchRunner::cancel);

	connect(importRunner, &ImportBatchRunner::fileStarted, this,
	        [this](int index, int total, const QString &name) {
		const QString counter =
		    total > 1 ? tr(" (%1 of %2)").arg(index + 1).arg(total) : QString();
		progressDialog->setLabelText(tr("Importing %1%2").arg(name, counter));
		progressDialog->setStageText(tr("Reading…"));
		progressDialog->setRange(0, 0);
	});

	connect(importRunner, &ImportBatchRunner::stageProgress, this,
	        [this](int, const QString &stage, int done, int total) {
		QString text;
		if (stage == QStringLiteral("sniff")) text = tr("Reading…");
		else if (stage == QStringLiteral("convert")) text = tr("Converting…");
		else if (stage == QStringLiteral("extract")) text = tr("Extracting archive…");
		else if (stage == QStringLiteral("textures"))
			text = tr("Extracting textures (%1/%2)…").arg(done + 1).arg(total);
		else if (stage == QStringLiteral("hash"))
			text = tr("Hashing content (%1/%2)…").arg(done + 1).arg(total);
		else if (stage == QStringLiteral("store"))
			text = tr("Storing (%1/%2)…").arg(done + 1).arg(total);
		else text = stage;
		progressDialog->setStageText(text);
		progressDialog->setRange(0, total);
		if (total > 0) progressDialog->setValue(done);
	});

	connect(importRunner, &ImportBatchRunner::fileFinished, this,
	        [this](int, const ImportRequest &request, const ImportResult &result) {
		handleImportedFile(request, result);
	});

	connect(importRunner, &ImportBatchRunner::finished, this, [this](bool cancelled) {
		progressDialog->hide();
		auto *runner = importRunner;
		importRunner = nullptr;
		if (runner) runner->deleteLater();
		// R4: an import builds every model's node tree in the staging manager
		// and tears the transient ones down again — shrink the pools to what
		// survived (Engine::reclaimMemory logs the before/after).
		if (auto eng = EngineHost::instance().engine()) eng->reclaimMemory();

		if (cancelled) {
			Toast *t = libraryToast();
			t->showToast(tr("Import cancelled"),
			             tr("The import was cancelled. Files already completed stay in the library."));
		}
		if (!importErrors.isEmpty()) {
			QMessageBox::warning(this, tr("Import failed"),
			                     importErrors.join(QStringLiteral("\n")), QMessageBox::Ok);
			importErrors.clear();
		}

		// Engine-dependent tails AFTER the dialog closed (the dialog never
		// waits on the viewer): mesh previews + rendered thumbnails,
		// video frame grabs — queued ONE PER EVENT-LOOP TURN so the app
		// never freezes; each tile updates live as its render lands.
		scheduleViewerTails();

		// SELECT WHAT WAS IMPORTED (smoke S4). A mesh selects when its tail
		// lands (the preview is part of the selection); everything else —
		// images, audio, video — has no tail, so the batch
		// selects its last asset here.
		if (!tailQueue->isRunning()) selectAsset(lastImportedGuid);
	});

	importRunner->start();
}

void AssetView::handleImportedFile(const ImportRequest &request, const ImportResult &result)
{
	if (!result.ok()) {
		if (result.error != QStringLiteral("cancelled"))
			importErrors.append(QStringLiteral("%1: %2")
			                        .arg(QFileInfo(request.sourcePath).fileName(), result.error));
		return;
	}

	if (request.typeHint == static_cast<int>(ModelTypes::Mesh)) {
		// Viewer-driven types: the preview render happens post-dialog, one
		// item per event-loop turn (scheduleViewerTails). Mesh tiles appear
		// NOW, mid-batch, with the loading overlay up — the render lands on
		// the tile when its turn comes.
		pendingViewerTails.append({ result, request.sourcePath });
		lastImportedGuid = result.assetGuid;
		addLibraryTileForAsset(result.assetGuid);
		if (libraryModel->contains(result.assetGuid)) setLoadingTile(result.assetGuid);
		return;
	}

	// Media (image/audio/video): the tile appears live, mid-batch.
	addLibraryTileForAsset(result.assetGuid);
	lastImportedGuid = result.assetGuid;
	if (request.typeHint == static_cast<int>(ModelTypes::Video))
		pendingVideoThumbGuids.append(result.assetGuid);   // real frame, post-dialog
}

void AssetView::scheduleViewerTails()
{
	const auto tails = pendingViewerTails;
	pendingViewerTails.clear();
	for (const auto &tail : tails)
		tailQueue->enqueue([this, tail]() {
			finishMeshTailItem(tail.result, tail.fileName);
		});

	const auto videoGuids = pendingVideoThumbGuids;
	pendingVideoThumbGuids.clear();
	for (const QString &guid : videoGuids) {
		// The worker could not run QMediaPlayer (GUI-thread-only), so the row
		// committed with the film icon; grab the real first-second frame on
		// the queue — the same path as tile right-click → Rebuild Thumbnail.
		tailQueue->enqueue([this, guid]() {
			if (libraryModel->contains(guid)) rebuildTileThumbnail(guid);
		});
	}

	if (tailQueue->pendingCount() > 0) tailQueue->start();
}

int AssetView::selectedDrawerId() const
{
	const int id = treeWidget->currentItem()
	    ? treeWidget->currentItem()->data(0, Qt::UserRole).toInt() : -1;
	return id > 0 ? id : 0;   // root/none selected -> Uncategorized
}

// The page mirrors the LIBRARY, not just its own import dialog: an import made
// by a verb (script, MCP, the avatar module's Import Avatar/Animation) announces
// itself on the AssetService, and the tile appears here without a relaunch
// (lead, 2026-09-09; the animtype lane found scripted imports invisible until
// the next launch, for every type). Queued to this widget's thread: the
// announcement fires on the caller's thread, and an ImportBatchRunner caller is
// a worker. Idempotent against the dialog's own path (tileByGuid guard).
void AssetView::setServices(StudioServices *s)
{
	services = s;
	if (!services || !services->assets) return;
	QPointer<AssetView> self(this);
	services->assets->onLibraryChanged([self](const QString &guid) {
		if (!self) return;
		QMetaObject::invokeMethod(self, [self, guid]() {
			if (!self || !self->libraryModel) return;
			if (!self->libraryModel->contains(guid)) self->addLibraryTileForAsset(guid);
		}, Qt::QueuedConnection);
	});
}

LibraryRow AssetView::rowFromRecord(const AssetRecord &record)
{
	LibraryRow row;
	row.guid = record.guid;
	row.name = record.name;
	row.type = record.type;
	row.collection = record.collection;
	row.author = record.author;
	row.license = record.license;
	for (const QJsonValue &tag : QJsonDocument::fromJson(record.tags).object().value(QStringLiteral("tags")).toArray())
		row.tags.append(tag.toString());
	return row;
}

void AssetView::reloadLibrary()
{
	// ONE query, no thumbnail column, nothing decoded: the rows only. The view
	// asks the tile cache for the ~50 tiles it paints.
	QVector<LibraryRow> rows;
	const QVector<AssetRecord> records = assettray::libraryList(db, showMembers);
	rows.reserve(records.size());
	for (const AssetRecord &record : records) rows.append(rowFromRecord(record));
	libraryModel->setRows(rows);
	if (!libraryModel->contains(selectedGuid)) selectedGuid.clear();
}

void AssetView::addLibraryTileForAsset(const QString &guid)
{
	// The committed row (the same rows the assets.importFile verb writes) —
	// this is only the tile tail; the pipeline ran on the batch runner. ONE
	// row in the model, at the front, O(1): nothing is relaid out or rebuilt.
	const auto record = db->fetchAsset(guid);
	if (record.guid.isEmpty()) return;
	libraryModel->upsert(rowFromRecord(record));
}

void AssetView::applyShowMembers(bool on)
{
	// THE SWITCH'S OWN SET — the bundle members and nothing else; the listing
	// is re-read (one query, no thumbnails) under the new rule.
	showMembers = on;
	reloadLibrary();
}

// ONE toast for the page, reused. Every message used to `new Toast(this)` and
// never delete it: four leaked top-level windows per import, per delete, per
// add-to-project, for the life of the session.
Toast *AssetView::libraryToast()
{
	if (!mToast) mToast = new Toast(this);
	return mToast;
}

void AssetView::stopMediaPreviews()
{
	if (mediaPlayer) mediaPlayer->stop();
	if (assetVideoViewer) assetVideoViewer->stop();
}

// Build the audio player and everything wired to it, once, on first use.
//
// This used to run in the constructor, and AssetView is constructed
// unconditionally during shell setup, so every launch — including
// `--engine-selftest` and every headless suite — loaded the Qt multimedia
// (ffmpeg) plugin and enumerated audio devices. On this box that is a
// pipewire connect attempt followed by a PulseAudio fallback, both of which
// log and neither of which any startup path needs
// (STABILITY_PROGRAM_SPEC §1.7c / Lane 6a).
//
// Idempotent by the early return; call it from anywhere that is about to
// dereference mediaPlayer.
void AssetView::ensureAudioPlayer()
{
	if (mediaPlayer) return;

	mediaPlayer = new QMediaPlayer(this);
	audioOutput = new QAudioOutput(this);
	mediaPlayer->setAudioOutput(audioOutput);

	const auto formatTime = [](qint64 ms) {
		const qint64 secs = ms / 1000;
		return QStringLiteral("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QChar('0'));
	};

	connect(mediaPlayer, &QMediaPlayer::playbackStateChanged, this,
	        [this](QMediaPlayer::PlaybackState state) {
		audioPlayButton->setText(state == QMediaPlayer::PlayingState ? tr("Pause") : tr("Play"));
	});
	connect(mediaPlayer, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
		audioSeekSlider->setRange(0, static_cast<int>(duration));
	});
	connect(mediaPlayer, &QMediaPlayer::positionChanged, this, [this, formatTime](qint64 position) {
		if (!audioSeekSlider->isSliderDown())
			audioSeekSlider->setValue(static_cast<int>(position));
		audioTimeLabel->setText(formatTime(position) + " / " + formatTime(mediaPlayer->duration()));
	});

	// Waveform <-> player wiring (§2): playhead follows playback, clicks seek
	// (the seekRequested half is wired in the ctor and nullptr-guards).
	connect(mediaPlayer, &QMediaPlayer::durationChanged, waveform, &WaveformWidget::setDuration);
	connect(mediaPlayer, &QMediaPlayer::positionChanged, waveform, &WaveformWidget::setPosition);
}

void AssetView::showAudioPreview(const QString &guid, const QString &filePath,
                                 const QString &displayName)
{
	ensureAudioPlayer();
	viewers->setCurrentIndex(static_cast<int>(PreviewPage::Audio));
	audioNameLabel->setText(displayName);
	audioSeekSlider->setValue(0);
	waveform->setPosition(0);
	loadWaveform(guid, filePath);
	mediaPlayer->setSource(QUrl::fromLocalFile(filePath));
	mediaPlayer->play();   // double-click a Music tile -> audio page + autoplay
}

void AssetView::loadWaveform(const QString &guid, const QString &filePath)
{
	waveformGuid = guid;

	// Cached per guid in the row's properties JSON, beside "metadata"
	// (ASSET_MEDIA_SPEC §2) — reselects render instantly.
	const auto record = db->fetchAsset(guid);
	const QJsonArray cached =
	    QJsonDocument::fromJson(record.properties).object()["waveform"].toArray();
	if (!cached.isEmpty()) {
		waveform->setPeaks(AudioPeaks::fromJson(cached));
		return;
	}

	// First decode: QtConcurrent worker ("…" meanwhile), persist on arrival
	// on the UI thread — it owns the SQLite connection.
	waveform->showComputing();
	auto *watcher = new QFutureWatcher<AudioPeaks::Peaks>(this);
	connect(watcher, &QFutureWatcher<AudioPeaks::Peaks>::finished, this, [this, watcher, guid]() {
		watcher->deleteLater();
		const AudioPeaks::Peaks peaks = watcher->result();
		if (peaks.isEmpty()) {
			if (waveformGuid == guid) waveform->clear();
			return;
		}
		QJsonObject props = QJsonDocument::fromJson(db->fetchAsset(guid).properties).object();
		if (!props.contains("waveform")) {
			props["waveform"] = AudioPeaks::toJson(peaks);
			db->updateAssetProperties(guid, QJsonDocument(props).toJson());
		}
		if (waveformGuid == guid) waveform->setPeaks(peaks);
	});
	watcher->setFuture(QtConcurrent::run(
	    [filePath]() { return AudioPeaks::compute(filePath); }));
}

void AssetView::showVideoPreview(const QString &filePath, const QString &displayName)
{
	viewers->setCurrentIndex(static_cast<int>(PreviewPage::Video));
	assetVideoViewer->showVideo(filePath, displayName);
}

void AssetView::showImagePreview(const QString &filePath)
{
	imageOriginal = QPixmap(filePath);
	imageFitMode = true;
	imageZoom = 1.0;
	if (imageFitButton) imageFitButton->setChecked(true);
	applyImageZoom();
}

void AssetView::showFilePlaceholder(const QString &displayName)
{
	fileNameLabel->setText(displayName);
}

void AssetView::applyImageZoom()
{
	if (imageOriginal.isNull()) {
		assetImageCanvas->setPixmap(QPixmap());
		assetImageCanvas->setText(tr("No preview available"));
		assetImageCanvas->adjustSize();
		if (imageZoomLabel) imageZoomLabel->setText(QString());
		return;
	}

	assetImageCanvas->setText(QString());
	if (imageFitMode) {
		const QSize viewport = imageScroll->viewport()->size();
		QSize target = imageOriginal.size();
		target.scale(viewport, Qt::KeepAspectRatio);
		if (target.width() > imageOriginal.width())   // never upscale in fit
			target = imageOriginal.size();
		imageZoom = imageOriginal.width() > 0
		                ? double(target.width()) / imageOriginal.width() : 1.0;
		assetImageCanvas->setPixmap(imageOriginal.scaled(
		    target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	}
	else {
		const QSize target = imageOriginal.size() * imageZoom;
		assetImageCanvas->setPixmap(
		    imageZoom == 1.0 ? imageOriginal
		                     : imageOriginal.scaled(target, Qt::KeepAspectRatio,
		                                            Qt::SmoothTransformation));
	}
	assetImageCanvas->adjustSize();
	if (imageZoomLabel)
		imageZoomLabel->setText(QStringLiteral("%1%").arg(qRound(imageZoom * 100)));
}

void AssetView::addToJahLibrary(const QString fileName, const QString guid, bool jfx)
{
    Q_UNUSED(fileName);
    Q_UNUSED(jfx);
	db->updateAssetViewFilter(guid, 2);
	const int type = db->fetchAsset(guid).type;
	if (type != static_cast<int>(ModelTypes::Sky))
        db->updateAssetProperties(guid, QJsonDocument(viewer->getSceneProperties()).toJson());

    viewer->cacheCurrentModel(guid);
    addLibraryTileForAsset(guid);
    openTile(guid);

    renameWidget->setVisible(true);
    tagWidget->setVisible(true);
    updateAsset->setVisible(true);
}

// ---- rich metadata formatting (ASSET_DRAWERS_SPEC addendum) ----

static QString formatCount(qint64 n)
{
	return QLocale(QLocale::English).toString(n);   // 12,480
}

static QString formatBytes(qint64 bytes)
{
	if (bytes < 1024) return QString::number(bytes) + " B";
	if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
	if (bytes < qint64(1024) * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
	return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
}

static QString formatDuration(qint64 ms)
{
	const qint64 totalSeconds = (ms + 500) / 1000;
	return QStringLiteral("%1:%2").arg(totalSeconds / 60)
	                              .arg(totalSeconds % 60, 2, 10, QChar('0'));   // 0:32
}

// The labeled list the pane shows under "Type:", per metadata kind.
// The rich per-type block as label/value rows for the metadata table.
using MetadataRows = QList<QPair<QString, QString>>;

static void appendMetadataRows(MetadataRows &rows, const QJsonObject &meta, const QDateTime &imported)
{
	const QString kind = meta["kind"].toString();
	const QString format = meta["format"].toString();

	if (!format.isEmpty())
		rows.append({ kind == "model" ? QStringLiteral("Source") : QStringLiteral("Format"),
		              format.toUpper() });

	if (kind == "model") {
		// The measured size in metres (services/extentmeasure.h) — as IMPORTED,
		// which is the size every placement has. The row below the table
		// repeats it beside the button that reopens the import decision.
		const extent::Extent measured = extent::extentOf(meta);
		if (measured.valid)
			rows.append({ "Imported Size",
			              QStringLiteral("%1 \u00d7 %2 \u00d7 %3 m")
			                  .arg(measured.x, 0, 'g', 3).arg(measured.y, 0, 'g', 3)
			                  .arg(measured.z, 0, 'g', 3) });
		rows.append({ "Vertices", formatCount(meta["vertices"].toInteger()) });
		rows.append({ "Triangles", formatCount(meta["triangles"].toInteger()) });
		if (meta["meshes"].toInt() > 1) rows.append({ "Meshes", formatCount(meta["meshes"].toInt()) });
		rows.append({ "Materials", formatCount(meta["materials"].toInt()) });
		rows.append({ "Textures", formatCount(meta["textures"].toInt()) });
	}
	else if (kind == "image") {
		if (meta.contains("width"))
			rows.append({ "Resolution", QStringLiteral("%1×%2")   // 1920×1080
			                                .arg(meta["width"].toInt()).arg(meta["height"].toInt()) });
	}
	else if (kind == "audio") {
		if (meta.contains("duration")) rows.append({ "Duration", formatDuration(meta["duration"].toInteger()) });
		if (meta.contains("sampleRate")) rows.append({ "Sample Rate", formatCount(meta["sampleRate"].toInteger()) + " Hz" });
		if (meta.contains("channels")) {
			const int channels = meta["channels"].toInt();
			rows.append({ "Channels", channels == 1 ? QStringLiteral("Mono")
			                        : channels == 2 ? QStringLiteral("Stereo")
			                                        : QString::number(channels) });
		}
	}
	else if (kind == "video") {
		if (meta.contains("width"))
			rows.append({ "Resolution", QStringLiteral("%1×%2")
			                                .arg(meta["width"].toInt()).arg(meta["height"].toInt()) });
		if (meta.contains("duration")) rows.append({ "Duration", formatDuration(meta["duration"].toInteger()) });
		if (meta.contains("frameRate"))
			rows.append({ "Frame Rate", QStringLiteral("%1 fps")
			                                .arg(meta["frameRate"].toDouble(), 0, 'g', 4) });
		if (meta.contains("videoCodec")) rows.append({ "Codec", meta["videoCodec"].toString() });
	}

	if (meta.contains("files")) rows.append({ "Files", formatCount(meta["files"].toInt()) });
	if (meta.contains("fileSize")) rows.append({ "Size", formatBytes(meta["fileSize"].toInteger()) });
	if (imported.isValid()) rows.append({ "Imported", imported.toString("yyyy-MM-dd") });
}

static QString metadataTableHtml(const MetadataRows &rows)
{
	QString html = QStringLiteral("<table cellspacing='0' cellpadding='2'>");
	for (const auto &row : rows)
		html += QStringLiteral("<tr><td style='color:#9a9a9a; padding-right:14px;"
		                       " white-space:nowrap;'>%1</td><td>%2</td></tr>")
		            .arg(row.first.toHtmlEscaped(), row.second.toHtmlEscaped());
	html += QStringLiteral("</table>");
	return html;
}

namespace {
// "Kitchen, Showroom and 2 more" — a confirmation has to name the projects it
// is talking about without growing to the size of the library.
QString pinnedProjectNames(const QVector<AssetPinRecord> &pins)
{
	QStringList names;
	for (const AssetPinRecord &pin : pins) names << pin.projectName;
	if (names.size() <= 3) return names.join(QStringLiteral(", "));
	const QStringList head = names.mid(0, 3);
	return QObject::tr("%1 and %n more", "", names.size() - 3).arg(head.join(QStringLiteral(", ")));
}
} // namespace

void AssetView::fetchMetadata(const QString &guid, bool allowBackfill)
{
	const LibraryRow *row = libraryModel->rowFor(guid);
	if (row) {
		metadataMissing->setVisible(false);
		metadataDetails->setVisible(true);

		MetadataRows rows;
		rows.append({ tr("Type"), getAssetType(row->type) });

		// The rich per-type block: import-time for new assets, lazily
		// backfilled (worker thread + update-on-arrival) for old libraries.
		// The row's properties are read HERE, for the one selected asset —
		// the listing never carries them for a pane.
		{
			const auto record = db->fetchAsset(guid);
			const QJsonObject props = QJsonDocument::fromJson(record.properties).object();
			if (guid == selectedGuid) selectedProperties = props;
			const QJsonObject meta = props.value(QStringLiteral("metadata")).toObject();
			if (!meta.isEmpty()) {
				appendMetadataRows(rows, meta, record.dateCreated);
			}
			else if (allowBackfill) {
				rows.append({ tr("Details"), QStringLiteral("…") });
				backfillMetadata(guid, record.type);
			}
			refreshFitRow(guid, record.type, meta);
		}
		// USED BY (library delete keeps project pins): the pin count is what
		// decides whether Delete removes this asset or merely unlists it, so
		// the user gets to see it BEFORE pressing the button.
		{
			// LIVE pins are the ones that decide (code review 2026-09-10 —
			// the row used to count pins from projects that no longer exist,
			// and show their raw guids). Dead ones are still worth saying:
			// they are catalog rows assets.gc can reap.
			const auto all = assetdelete::pins(db, guid);
			const auto live = assetdelete::livePins(db, guid);
			const int dead = all.size() - live.size();
			QString used = live.isEmpty() ? tr("no projects")
			                              : tr("%n project(s): %1", "", live.size())
			                                    .arg(pinnedProjectNames(live));
			if (dead > 0) used += tr(" (+%n pin(s) from deleted projects)", "", dead);
			rows.append({ tr("Used by"), used });
		}
		rows.append({ tr("Author"), row->author });
		rows.append({ tr("License"), row->license });
		rows.append({ tr("Collection"), drawerName(row->collection) });

		metadataDetails->setText(metadataTableHtml(rows));
	}
	else {
		metadataMissing->setVisible(true);

		addToProject->setEnabled(false);
		deleteFromLibrary->setEnabled(false);

		metadataDetails->setVisible(false);
		if (fitRow) fitRow->setVisible(false);
	}
}

// ---- THE SIZE ROW (services/extentmeasure.h) -------------------------------
//
// "Imported size: 1.75 × 0.50 × 0.30 m  [Import settings…]". MODEL rows only:
// everything else has no measured size, so the row is hidden rather than shown
// empty. The number is what the asset MEASURES as imported, which is the size
// every placement of it has — nothing scales it at instantiation any more
// (SPECS/IMPORT_DIALOG_SPEC.md §6).
void AssetView::refreshFitRow(const QString &guid, int assetType, const QJsonObject &meta)
{
	Q_UNUSED(guid);
	if (!fitRow) return;
	if (assetType != static_cast<int>(ModelTypes::Object) || meta.isEmpty()) {
		fitRow->setVisible(false);
		return;
	}

	const extent::Extent measured = extent::extentOf(meta);
	if (!measured.valid) {
		// A file with no geometry, or a row whose block predates the
		// measurement.
		fitLabel->setText(tr("Imported size: not measured"));
	} else {
		fitLabel->setText(tr("Imported size: %1 \u00d7 %2 \u00d7 %3 m")
		                      .arg(measured.x, 0, 'g', 3)
		                      .arg(measured.y, 0, 'g', 3)
		                      .arg(measured.z, 0, 'g', 3));
	}
	fitLabel->setToolTip(tr("Decided once, at import, and baked into the asset: every "
	                        "placement of this model is at scale 1."));
	fitRow->setVisible(true);
}

void AssetView::backfillMetadata(const QString &guid, int assetType)
{
	// The RESOLVED source object, on this thread (the catalog connection is
	// per-thread); the worker only reads the file. This used to hand the worker
	// the retired per-guid folder <root>/<guid>/, which no CAS asset has — so
	// the async half described nothing (FORWARD-ONLY-1).
	const QString source = AssetCas::resolveSource(QSqlDatabase::database(),
	                                               AssetStorePaths::root(), guid);

	// Video is the one kind whose rich fields need the GUI thread
	// (QMediaPlayer probe — ASSET_MEDIA_SPEC §1): compute right here, where
	// ensure() persists the complete block, instead of on the worker where
	// it would come back degraded.
	if (assetType == static_cast<int>(ModelTypes::Video)) {
		const QJsonObject meta = AssetMetadata::ensure(db, guid);
		if (selectedGuid == guid) fetchMetadata(guid, false);
		return;
	}

	auto *watcher = new QFutureWatcher<QJsonObject>(this);
	connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, guid]() {
		watcher->deleteLater();
		const QJsonObject meta = watcher->result();
		if (meta.isEmpty()) {
			// nothing on disk to describe (e.g. a built-in) — re-render the
			// table with the basic rows only (no backfill retry loop)
			if (selectedGuid == guid) fetchMetadata(guid, false);
			return;
		}

		// Persist on the UI thread (it owns the SQLite connection), guarded
		// so a concurrent backfill (verb, second selection) wins only once.
		QJsonObject props = QJsonDocument::fromJson(db->fetchAsset(guid).properties).object();
		if (!props.contains("metadata")) {
			props["metadata"] = meta;
			db->updateAssetProperties(guid, QJsonDocument(props).toJson());
		}
		if (selectedGuid == guid) fetchMetadata(guid, false);   // re-renders with data
	});
	// The row's BAKE and name are resolved HERE (catalog, this thread); the
	// worker only reads files — a bake, an image header, a wav header. No
	// model or clip is ever parsed for its description (SHIPPED-BAKES-1).
	const QString bakePath = AssetMetadata::bakePathFor(assetType, source, guid);
	const QString rowName = db->fetchAsset(guid).name;
	watcher->setFuture(QtConcurrent::run([assetType, source, bakePath, rowName]() {
		return AssetMetadata::computeForSource(assetType, source, bakePath, rowName);
	}));
}

void AssetView::addAssetItemToProject(const QString &guid)
{
	const LibraryRow *row = libraryModel->rowFor(guid);
	if (!row) return;
	const QString fullName = row->name;
	// Are-you-sure first (owner direction): every UI entry point — the
	// button, Shift+click and the tile context menu — funnels through here,
	// so one dialog covers all three. The headless verb
	// (assets.addToProject) never comes this way and stays dialog-free.
	const QString assetName = QFileInfo(fullName).baseName();
	const QString projectName = project ? project->getProjectName() : QString();
	{
		QDialog confirm(this);
		confirm.setWindowTitle(tr("Add to Project"));
		confirm.setWindowFlags(confirm.windowFlags() & ~Qt::WindowContextHelpButtonHint);

		auto *message = new QLabel(
		    tr("Add \"%1\" to project \"%2\"?").arg(assetName, projectName));
		message->setWordWrap(true);

		// The sample-scenes dialog convention: no background band behind the
		// button row, accent confirm, grey cancel (classic keeps its big
		// button sheets).
		auto *cancel = new QPushButton(tr("Cancel"));
		auto *add = new QPushButton(tr("Add"));
		add->setDefault(true);
		if (ThemeManager::classicActive()) {
			cancel->setStyleSheet(StyleSheet::QPushButtonGreyscaleBig());
			add->setStyleSheet(StyleSheet::QPushButtonBlueBig());
		} else {
			cancel->setStyleSheet(ThemeManager::chromeButtonSheet());
			add->setStyleSheet(ThemeManager::chromeAccentButtonSheet());
		}

		auto *buttons = new QHBoxLayout;
		buttons->addStretch();
		buttons->addWidget(cancel);
		buttons->addWidget(add);
		buttons->setContentsMargins(0, 10, 0, 0);

		auto *layout = new QVBoxLayout;
		layout->setContentsMargins(16, 16, 16, 12);
		layout->addWidget(message);
		layout->addLayout(buttons);
		confirm.setLayout(layout);
		confirm.setMinimumWidth(360);

		connect(cancel, &QPushButton::clicked, &confirm, &QDialog::reject);
		connect(add, &QPushButton::clicked, &confirm, &QDialog::accept);
		if (confirm.exec() != QDialog::Accepted) return;
	}

	// Reference-with-pin (phase 4): the twin ~250-line transcription of the
	// verb body (flat project-folder copies + Database::copyAsset clones)
	// died here - ProjectAssets is the one implementation.
	const auto result = ProjectAssets::addToProject(guid, db, project, ProjectAssets::AddKind::Direct);
	if (!result.ok()) {
		QMessageBox::warning(this, tr("Add to project failed"),
		                     result.error, QMessageBox::Ok);
		return;
	}

	// The editor's project panel refreshes off this (the shell connects it):
	// the pin must be visible without switching pages back and forth.
	emit assetAddedToProject(guid);

	// Bottom-centre of the app window, which is where the Toast puts itself
	// (smoke S8) — the old call passed a position nothing read.
	Toast *t = libraryToast();
	t->showToast(
		tr("Asset Added To Project"),
		tr("%1 has been added successfully to the open project.").arg(fullName)
	);
}

void AssetView::moveAssetToDrawer(const QString &guid, int drawerId)
{
	if (guid.isEmpty() || !db->switchAssetCollection(drawerId, guid)) return;

	libraryModel->update(guid, [drawerId](LibraryRow &row) { row.collection = drawerId; });
	if (selectedGuid == guid) fetchMetadata(guid);

	// The view follows the move (owner smoke-test: a successful move must be
	// VISIBLE): select the target drawer and filter the grid to it, so the
	// asset is seen arriving.
	if (auto drawerItem = findDrawerItem(drawerId)) treeWidget->setCurrentItem(drawerItem);
	filterFromSelection();
}

// ---- drawers: the left column (ASSET_DRAWERS_SPEC §1/§2) -------------------

void AssetView::rebuildDrawerTree()
{
	drawerTreeUpdating = true;
	const int selectedId = treeWidget->currentItem()
	    ? treeWidget->currentItem()->data(0, Qt::UserRole).toInt() : -1;
	treeWidget->clear();

	// The virtual root: id -1, shows ALL assets. Not renamable, not
	// deletable, not draggable — and not a database row.
	rootItem = new QTreeWidgetItem;
	rootItem->setText(0, tr("Asset Collections"));
	rootItem->setText(1, QString());
	rootItem->setData(0, Qt::UserRole, -1);
	rootItem->setFlags((rootItem->flags() | Qt::ItemIsDropEnabled)
	                   & ~(Qt::ItemIsDragEnabled | Qt::ItemIsEditable));
	treeWidget->addTopLevelItem(rootItem);

	const auto collections = db->fetchCollections();
	QMap<int, QTreeWidgetItem*> items;
	items.insert(-1, rootItem);
	for (const auto &coll : collections) {
		auto treeItem = new QTreeWidgetItem;
		treeItem->setText(0, coll.name);
		treeItem->setData(0, Qt::UserRole, coll.id);
		Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable
		    | Qt::ItemIsEditable | Qt::ItemIsDropEnabled;
		if (coll.id > 0) flags |= Qt::ItemIsDragEnabled;   // Uncategorized stays put
		treeItem->setFlags(flags);
		items.insert(coll.id, treeItem);
	}

	// Attach: Uncategorized first (always the root's first child), then the
	// rest in fetch order. A row with a vanished parent falls back to the root.
	if (items.contains(0)) rootItem->addChild(items.value(0));
	for (const auto &coll : collections) {
		if (coll.id == 0) continue;
		auto parent = items.value(coll.parent, rootItem);
		if (parent == items.value(coll.id)) parent = rootItem;
		parent->addChild(items.value(coll.id));
	}

	treeWidget->expandAll();
	if (auto restore = findDrawerItem(selectedId)) treeWidget->setCurrentItem(restore);
	drawerTreeUpdating = false;
}

void AssetView::createDrawerUnder(int parentId)
{
	const int id = db->createCollection(tr("New Drawer"), parentId);
	if (id < 0) return;
	rebuildDrawerTree();
	if (auto item = findDrawerItem(id)) {
		treeWidget->setCurrentItem(item);
		treeWidget->editItem(item, 0);   // inline-editable name, straight away
	}
}

void AssetView::deleteDrawer(int drawerId)
{
	const auto subtree = db->fetchCollectionSubtree(drawerId);
	if (subtree.isEmpty()) return;

	// No dialog for an empty drawer; a Yes/No confirm when assets would move.
	const int assetCount = db->countAssetsInCollections(subtree);
	if (assetCount > 0) {
		const auto option = QMessageBox::question(this, tr("Delete Drawer"),
		    tr("%n asset(s) in this drawer will move to Uncategorized. Delete it?",
		       nullptr, assetCount),
		    QMessageBox::Yes | QMessageBox::No);
		if (option != QMessageBox::Yes) return;
	}

	if (!db->deleteCollection(drawerId)) return;
	libraryModel->reassignCollections(subtree, 0);
	rebuildDrawerTree();
	filterFromSelection();
}

QTreeWidgetItem *AssetView::findDrawerItem(int drawerId) const
{
	for (QTreeWidgetItemIterator it(treeWidget); *it; ++it) {
		if ((*it)->data(0, Qt::UserRole).toInt() == drawerId) return *it;
	}
	return nullptr;
}

QString AssetView::drawerName(int drawerId) const
{
	for (const auto &coll : db->fetchCollections()) {
		if (coll.id == drawerId) return coll.name;
	}
	return tr("Uncategorized");
}

QVector<QPair<int, QString>> AssetView::drawerMenuEntries() const
{
	// The drawer tree flattened for a menu, indentation showing the nesting.
	// Uncategorized leads, like the tree itself.
	QVector<QPair<int, QString>> entries;
	const auto collections = db->fetchCollections();

	std::function<void(int, int)> walk = [&](int parent, int depth) {
		for (const auto &coll : collections) {
			if (coll.parent != parent || coll.id == 0) continue;
			entries.append({ coll.id, QString(depth * 3, QChar(' ')) + coll.name });
			walk(coll.id, depth + 1);
		}
	};
	for (const auto &coll : collections)
		if (coll.id == 0) entries.append({ 0, coll.name });
	walk(-1, 0);
	return entries;
}

void AssetView::filterFromSelection()
{
	libraryProxy->setCollection(treeWidget->currentItem()
	    ? treeWidget->currentItem()->data(0, Qt::UserRole).toInt() : -1);
}

void AssetView::setAssetViewMode(const QString &mode, bool persist)
{
	assetViewMode = (mode == QStringLiteral("list")) ? QStringLiteral("list")
	                                                 : QStringLiteral("tiles");
	const bool listMode = assetViewMode == QStringLiteral("list");
	if (viewTilesAction) viewTilesAction->setChecked(!listMode);
	if (viewListAction) viewListAction->setChecked(listMode);

	// Only swap the visible pane when the empty-state isn't showing.
	if (!emptyGrid->isVisible()) {
		tileView->setVisible(!listMode);
		assetListView->setVisible(listMode);
	}
	if (listMode) rebuildAssetList();
	if (persist && settings)
		settings->setValue(QStringLiteral("assetView/viewMode"), assetViewMode);
}

void AssetView::rebuildAssetList()
{
	// The list's Size column: one query for the whole library, read when the
	// list is showing (the rows themselves are the shared model's).
	if (!assetListView || assetViewMode != QStringLiteral("list")) return;
	libraryModel->setSizes(db->fetchAssetFileSizes());
}

// THE TILE'S MENU (the one AssetGridItem carried per widget): the same entries,
// by type, acting on the guid.
void AssetView::showTileMenu(const QString &guid, const QPoint &globalPos)
{
	const LibraryRow *row = libraryModel->rowFor(guid);
	if (!row) return;
	const auto tileType = static_cast<ModelTypes>(row->type);
	const int currentDrawer = row->collection;

	QMenu menu(this);
	menu.setStyleSheet(StyleSheet::QMenuDark());
	connect(menu.addAction(tr("Add to Project")), &QAction::triggered, this,
	        [this, guid]() { addAssetItemToProject(guid); });

	QMenu *moveTo = menu.addMenu(tr("Move to"));
	moveTo->setStyleSheet(StyleSheet::QMenuDark());
	for (const auto &entry : drawerMenuEntries()) {
		QAction *action = moveTo->addAction(entry.second);
		action->setEnabled(entry.first != currentDrawer);
		const int drawerId = entry.first;
		connect(action, &QAction::triggered, this,
		        [this, guid, drawerId]() { moveAssetToDrawer(guid, drawerId); });
	}
	moveTo->setEnabled(!moveTo->isEmpty());

	connect(menu.addAction(tr("Rebuild Thumbnail")), &QAction::triggered, this,
	        [this, guid]() { rebuildTileThumbnail(guid); });
	if (tileType == ModelTypes::Texture)
		connect(menu.addAction(tr("Create Material from Image")), &QAction::triggered, this,
		        [this, guid]() { createMaterialFromImageTile(guid); });
	// AVATARS (§5.5). The rigged test is lazy — one metadata read when the menu
	// is actually opened, never on a listing.
	if (tileType == ModelTypes::Avatar) {
		// The Assets page is the LIBRARY's view of the world, so its Edit opens
		// the library version. The editor drawer's Edit opens the project's.
		connect(menu.addAction(tr("Edit in Avatar Module")), &QAction::triggered, this, [this, guid]() {
			emit editAssetInModule(guid, QStringLiteral("avatar"), QStringLiteral("library"));
		});
	} else if (tileType == ModelTypes::Object
	           && AssetMetadata::ensure(db, guid).value(QStringLiteral("hasSkeleton")).toBool()) {
		connect(menu.addAction(tr("Create Avatar")), &QAction::triggered, this,
		        [this, guid]() { createAvatarFromModelTile(guid); });
	}
	if (tileType == ModelTypes::Object)
		connect(menu.addAction(tr("Reimport\u2026")), &QAction::triggered, this,
		        [this, guid]() { emit reimportAssetRequested(guid); });
	connect(menu.addAction(tr("Delete")), &QAction::triggered, this,
	        [this, guid]() { deleteAssetFromLibrary(guid); });
	menu.exec(globalPos);
}

void AssetView::createAvatarFromModelTile(const QString &objectGuid)
{
	if (objectGuid.isEmpty()) return;

	QString error;
	const QString avatarGuid = AvatarAssets::create(objectGuid, AvatarAssets::Scope::Library, db,
	                                                project, QString(), &error);
	if (avatarGuid.isEmpty()) {
		QMessageBox::warning(this, tr("Create Avatar"),
		                     tr("Could not create the avatar: %1").arg(error));
		return;
	}
	// The library tile for the new avatar, same tail every mint path uses,
	// and then straight into the module: "Create Avatar" is one gesture.
	addLibraryTileForAsset(avatarGuid);
	emit editAssetInModule(avatarGuid, QStringLiteral("avatar"), QStringLiteral("library"));
}

void AssetView::createMaterialFromImageTile(const QString &textureGuid)
{
	// IMAGE_PLANE_SPEC option B1 — the same helper the automatic companion
	// material and materials.createFromImage use.
	if (textureGuid.isEmpty()) return;

	QString error;
	const QString materialGuid =
	    ImageMaterial::createMaterialAsset(textureGuid, db, project, assethome::library(), &error);
	if (materialGuid.isEmpty()) {
		QMessageBox::warning(this, tr("Create Material from Image"),
		                     tr("Could not create the material: %1").arg(error));
		return;
	}
	// With a project open the new material is pinned in too, so it shows up
	// in the bin and drags onto meshes immediately.
	if (project && !project->getProjectGuid().isEmpty()) {
		ProjectAssets::addToProject(materialGuid, db, project, ProjectAssets::AddKind::Direct);
		emit assetAddedToProject(materialGuid);
	}

	// THE TILE IS A RENDER OF THE MATERIAL (THUMBS-1, owner review R9(a)): the
	// mint stores the image as a fallback and one gesture can afford one
	// render. Through the ONE door (services/materialtile.h), so a refused
	// borrow is logged by name instead of discarded.
	materialtile::mint(db, project, materialGuid, "the Assets page's Create Material from Image");

	// The library tile for the new material, same tail the import path uses.
	addLibraryTileForAsset(materialGuid);
}

void AssetView::rebuildMissingThumbnails()
{
	if (!db) return;

	// One render per asset, the event loop turning between them: the window
	// keeps painting and the tiles land one by one, exactly like an import
	// batch's tails. User input is excluded so a second click on the menu
	// cannot start a second sweep over the same rows.
	// The user clicked the menu, so the app is alive and this is a new intent
	// (services/thumbnailstop.h): whatever stopped a previous sweep is cleared.
	thumbrebuild::clearStop();
	thumbrebuild::SweepOptions options;   // missingOnly: the repair, not a redraw of everything
	const auto result = thumbrebuild::rebuildMissing(
	    db, project, EngineHost::instance().engine(), options,
	    [] { QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents); });

	// STOPPED MEANS SILENT (fix round F1). A yield delivers the window's close:
	// ShellLifecycle::stopBackgroundWork has already destroyed the thumbnail renderer by the
	// time we are back here, the page is on its way out, and a toast — let alone
	// the modal box below — would be the "modal swallowed the quit" zombie.
	if (result.cancelled) return;

	// Each rebuilt tile repaints from its new stored picture (the write
	// dropped the cached one; the view reads it again, off this thread).
	for (const QString &guid : result.rebuiltGuids) libraryModel->refreshTile(guid);

	if (result.rebuilt == 0 && result.failed.isEmpty()) {
		libraryToast()->showToast(tr("Thumbnails"),
		                          tr("Every asset that can have a thumbnail already has one."));
		return;
	}
	// THE DENOMINATOR IS WHAT WAS ATTEMPTED, not what was looked at (F10):
	// "Rebuilt 2 of 5,431" reads as a catastrophe on a healthy library.
	const int attempted = result.rebuilt + result.failed.size();
	if (result.failed.isEmpty()) {
		libraryToast()->showToast(tr("Thumbnails"),
		                          tr("Rebuilt %1 of %2 thumbnails.")
		                              .arg(result.rebuilt).arg(attempted));
		return;
	}
	// A FAILURE IS NEVER SILENT: the reasons are what the user needs to act on
	// (no engine, a model whose bytes are gone, a shader with no baked material).
	QStringList lines;
	for (const auto &failure : result.failed)
		lines << QStringLiteral("%1: %2").arg(failure.guid, failure.reason);
	QMessageBox::warning(this, tr("Rebuild missing thumbnails"),
	                     tr("Rebuilt %1 of %2. These could not be rebuilt:\n\n%3")
	                         .arg(result.rebuilt)
	                         .arg(attempted)
	                         .arg(lines.mid(0, 12).join(QStringLiteral("\n"))),
	                     QMessageBox::Ok);
}

void AssetView::rebuildTileThumbnail(const QString &guid)
{
	// ONE ROUTINE, AND THE PREVIEW IS A SIDE EFFECT (THUMBS-1 fix round F7).
	//
	// This used to carry its own switch: a second way to draw a material (the
	// page's viewer screenshot instead of the preview sphere every other
	// surface stores), no branch at all for a LightProfile or an Avatar — so
	// "Rebuild Thumbnail", the one gesture a user has for "redraw this", threw
	// away a photometric lobe or a character and wrote a generic FILE ICON over
	// it — and, when nothing could be drawn, a box saying "Could not rebuild
	// this thumbnail." with the reason dropped on the floor.
	//
	// Now every type goes through thumbrebuild::rebuildOne (which also STORES
	// it, so nothing is written twice), the page still loads the matching
	// preview because that is what the user asked to look at, and a failure
	// shows the reason it already has.
	if (guid.isEmpty()) return;
	const auto record = db->fetchAsset(guid);
	if (record.guid.isEmpty()) return;

	const QString sourceFile = AssetCas::resolveSource(
	    QSqlDatabase::database(), AssetStorePaths::root(), guid);
	const ModelTypes type = static_cast<ModelTypes>(record.type);

	// The preview pane follows the gesture: the user sees what was rebuilt.
	switch (type) {
	case ModelTypes::Object:
	case ModelTypes::ParticleSystem:
		viewers->setCurrentIndex(0);
		viewer->loadJafModel(sourceFile, guid, false, true, false);
		break;
	case ModelTypes::Material:
		viewers->setCurrentIndex(0);
		viewer->loadJafMaterial(guid);
		break;
	case ModelTypes::Sky:
		viewers->setCurrentIndex(0);
		viewer->loadJafSky(guid);
		break;
	default:
		break;
	}

	QPixmap pixmap;
	QString reason;
	if (type == ModelTypes::Sky) {
		// THE ONE TYPE WITH NO ROUTINE OF ITS OWN: a sky asset is a picture
		// only through this page's viewer, so the shot IS the thumbnail and
		// this is the only branch that stores one itself.
		const QImage shot = viewer->takeScreenshot(512, 512);
		if (shot.isNull()) reason = tr("the sky preview produced no image");
		else {
			pixmap = QPixmap::fromImage(shot);
			db->updateAssetThumbnail(guid, AssetHelper::makeBlobFromPixmap(pixmap));
		}
	} else {
		const thumbrebuild::Outcome outcome =
		    thumbrebuild::rebuildOne(db, project, guid, EngineHost::instance().engine());
		if (!outcome.ok) reason = outcome.reason;
	}

	if (!reason.isEmpty()) {
		// THE REASON IS THE POINT: "could not" with no because is what sent the
		// owner looking at grey tiles with nothing to go on.
		QMessageBox::warning(this, tr("Rebuild Thumbnail"),
		                     reason.isEmpty()
		                         ? tr("Could not rebuild this thumbnail.")
		                         : tr("Could not rebuild this thumbnail:\n\n%1").arg(reason),
		                     QMessageBox::Ok);
		return;
	}

	// The tile updates live: the stored picture changed, the cached tile went
	// with it, and the view reads the new one.
	libraryModel->refreshTile(guid);
}

void AssetView::setLoadingTile(const QString &guid)
{
	loadingGuid = guid;
	libraryModel->setLoading(guid);
	if (!loadingPulse->isActive()) loadingPulse->start();
}

void AssetView::clearLoadingTile()
{
	if (loadingGuid.isEmpty()) return;
	loadingGuid.clear();
	loadingPulse->stop();
	libraryModel->setLoading(QString());
}

// LIBRARY DELETE (owner, 2026-09-09): deleting from the library never takes
// an asset out of a project. The decision and the write both live in
// services/assetdelete.h — the same code `assets.remove` runs (API-first) —
// and this function is the two-step confirmation in front of it.
void AssetView::deleteAssetFromLibrary(const QString &guid)
{
	const LibraryRow *row = libraryModel->rowFor(guid);
	if (!row) return;
	const QString name = row->name;
	// LIVE pins only: they are what the delete will actually weigh
	// (Database::countAssetPins ignores pins from deleted projects).
	const QVector<AssetPinRecord> pins = assetdelete::livePins(db, guid);

	bool force = false;
	if (pins.isEmpty()) {
		// Cancel is the DEFAULT (code review 2026-09-10): this branch is a
		// real, permanent delete, and Return on a focused dialog must not be
		// the thing that performs it.
		if (QMessageBox::question(this, tr("Delete Asset"),
		        tr("Delete \u201c%1\u201d from the library? No project uses it.").arg(name),
		        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
			return;
	}
	else {
		// Step one: the honest default — it leaves every project alone.
		QMessageBox box(this);
		box.setWindowTitle(tr("Delete Asset"));
		box.setText(tr("\u201c%1\u201d is used by %n project(s).", "", pins.size()));
		box.setInformativeText(
		    tr("Removing it from the library leaves it in %1 — those projects keep working. "
		       "Deleting it everywhere takes it out of them too, and cannot be undone.")
		        .arg(pinnedProjectNames(pins)));
		QPushButton *unlist = box.addButton(tr("Remove From Library"), QMessageBox::AcceptRole);
		QPushButton *everywhere = box.addButton(tr("Delete Everywhere\u2026"), QMessageBox::DestructiveRole);
		box.addButton(QMessageBox::Cancel);
		box.setDefaultButton(unlist);
		box.exec();
		if (box.clickedButton() == everywhere) {
			// Step two: the destructive one is never one click away.
			if (QMessageBox::warning(this, tr("Delete Everywhere"),
			        tr("Delete \u201c%1\u201d from the library AND from %n project(s) (%2)? "
			           "This cannot be undone.", "", pins.size()).arg(name, pinnedProjectNames(pins)),
			        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
				return;
			force = true;
		}
		else if (box.clickedButton() != unlist) {
			return;
		}
	}

	// keepShared false: the page has always deleted the dependency closure
	// with the asset (each member judged by its OWN pins inside the service).
	const bool wasMaterial = db->fetchAsset(guid).type == static_cast<int>(ModelTypes::Material);
	const auto outcome = assetdelete::remove(db, guid, /*keepShared*/ false, force);
	// A BUNDLE TAKES ITS OWN BORN-INSIDE MEMBERS (spec §4; fix round F6) —
	// the pictures it imported through its own picker, which nothing else
	// uses and no project pins. Only on a real delete: an unlisted row is
	// still live for the projects that pinned it.
	if (outcome.ok && !outcome.unlisted && wasMaterial)
		materialmembers::reapExclusiveMembers(db, guid);
	if (!outcome.ok) {
		QMessageBox::warning(this, tr("Delete Failed!"),
		    tr("The library database refused the delete; the asset is still catalogued. "
		       "See jahshaka.log for the failing query."), QMessageBox::Ok);
		return;
	}

	// The ROW GOES FIRST now: nothing below reads it (the pane is cleared by
	// guid, and a guid the model no longer lists renders the empty pane).
	libraryModel->remove(guid);
	renameWidget->setVisible(false);
	tagWidget->setVisible(false);
	updateAsset->setVisible(false);

	if (selectedGuid == guid) selectedGuid.clear();
	if (loadingGuid == guid) clearLoadingTile();
	updateAddToProjectButton();
	deleteFromLibrary->setEnabled(false);

	fetchMetadata(guid);
	clearViewer();

	if (outcome.unlisted) {
		// The user must know the asset did NOT vanish from their projects.
		Toast *t = libraryToast();
		t->showToast(tr("Removed From Library"),
		             tr("%1 is still used by %n project(s) and stays in them.", "",
		                outcome.pinCount).arg(name));
	}
}

AssetView::~AssetView()
{
    
}
