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
#include "bridge/enginehost.h"
#include "ui/pages/assetview.h"
#include "ui/pages/iassetviewer.h"
#include "ui/pages/headlessassetviewer.h"
#include "ui/pages/importviewertail.h"
#include <QTimer>
#include "ui/dialogs/progressdialog.h"
#include "data/settingsmanager.h"
#include "services/assettags.h"
#include "ui/dialogs/preferencesdialog.h"
#include "ui/dialogs/preferences/worldsettingswidget.h"

#include "irisgl/core/irisutils.h"
#include "irisgl/document/assets/mesh.h"
// extractTexturesAndMaterialFromMaterial still builds a legacy CustomMaterial
// from app/shader_defs/*.shader (the builtin Default/Flat/Glass set, the last
// CustomMaterial users). It used to reach the type through thumbnailgenerator.h;
// the dependency is spelled out here so the scheduled iris::CustomMaterial
// deletion can grep its real call sites.
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
#include "services/assetstore.h"
#include "services/animationfile.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include <QSqlDatabase>
#include "services/projectservice.h"
#include "ui/controls/assetviewgrid.h"
#include "ui/controls/assetgriditem.h"
#include "ui/controls/drawertreewidget.h"
#include "services/assethelper.h"
#include "services/assetimporter.h"
#include "services/import/assetimportservice.h"
#include "services/import/importbatchrunner.h"
#include "ui/dialogs/toast.h"
#include "services/assetdelete.h"
#include "services/projectassets.h"
#include "services/imagematerial.h"
#include "services/fitsize.h"
#include "services/assetmetadata.h"
#include "services/avatarassets.h"
#include "services/audiopeaks.h"
#include "services/videoutils.h"
#include "ui/controls/videopreviewwidget.h"
#include "ui/controls/waveformwidget.h"
#include "ui/pages/previewrouter.h"
#include "io/assetmanager.h"
#include "io/builtinmaterials.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "io/materialreader.h"
#include "services/thumbnailgenerator.h"

#include "data/guidmanager.h"
#include "services/thumbnailmanager.h"
#include "io/assetmanager.h"
#include "io/scenewriter.h"

#include "ui/dialogs/toast.h"
#include "ui/style/panelmetrics.h"
#include "ui/style/stylesheet.h"
#include "ui/style/thememanager.h"

void AssetView::focusInEvent(QFocusEvent *event)
{
	Q_UNUSED(event);
	//emit fetch();
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

				// Every URL imports (the old path took only the first).
				importFiles(list);

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

void AssetView::checkForEmptyState()
{
    //if (fastGrid->containsTiles()) {
    //    ui->stackedWidget->setCurrentIndex(0);
    //    return false;
    //}

    //ui->stackedWidget->setCurrentIndex(1);
    //return true;
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
    // viewer->clearScene();

    int size = this->height() / 3;
    const QList<int> sizes = { 1, size * 2 };   // 1px keeps the viewer visible so it's never fully hidden so initializegl gets called
    split->setSizes(sizes);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);

    toggleFilterPane(fastGrid->containsTiles());
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
		case static_cast<int>(ModelTypes::Shader):			return "Shader";			break;
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
	// Was never initialized: the bottom-right buttons read it before any
	// selection existed (UB on a garbage pointer once they got enabled).
	selectedGridItem = nullptr;
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
    imageZoomLabel->setStyleSheet("color: #BABABA;");

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
    audioNameLabel->setStyleSheet("font-size: 14px; color: #EEEEEE;");
    audioPlayButton = new QPushButton(tr("Play"));
    audioPlayButton->setFixedWidth(64);
    audioPlayButton->setCursor(Qt::PointingHandCursor);
    audioSeekSlider = new QSlider(Qt::Horizontal);
    audioSeekSlider->setRange(0, 0);
    audioTimeLabel = new QLabel("0:00 / 0:00");
    audioTimeLabel->setStyleSheet("color: #BABABA;");

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
    fileNameLabel->setStyleSheet("font-size: 14px; color: #EEEEEE;");
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
    assetEmptyViewer->setStyleSheet("background: #1e1e1e;");
    {
        auto *emptyPreviewLabel = new QLabel(tr("Select an asset to preview"));
        emptyPreviewLabel->setAlignment(Qt::AlignCenter);
        emptyPreviewLabel->setStyleSheet(
            "font-size: 14px; color: #8f8f8f; background: transparent;");
        auto *emptyPreviewLayout = new QVBoxLayout;
        emptyPreviewLayout->addWidget(emptyPreviewLabel);
        assetEmptyViewer->setLayout(emptyPreviewLayout);
    }

    settings = SettingsManager::getDefaultManager();
	//prefsDialog = new PreferencesDialog(this, db, settings);

	// Header row (ASSET_DRAWERS_SPEC §1): the Local Assets label plus the [+]
	// drawer button. The Online Assets stub (assetSource was never read, no
	// network code) and the bottom Create Collection button are gone.
	auto headerRow = new QWidget;
	auto headerLayout = new QHBoxLayout;
	headerLayout->setContentsMargins(6, 6, 6, 6);
	auto localAssetsLabel = new QLabel(tr("Local Assets"));
	localAssetsLabel->setStyleSheet("font-size: 12px; padding: 4px;");
	auto addDrawerButton = new QPushButton("+");
	addDrawerButton->setFixedSize(24, 24);
	addDrawerButton->setCursor(Qt::PointingHandCursor);
	addDrawerButton->setToolTip(tr("New drawer"));
	// Explicit style: the page-wide "QPushButton { padding: 8px 12px; }" rule
	// left a 24px button ZERO content area — the + glyph was clipped away and
	// the button invisible on the dark pane (owner-reported).
	addDrawerButton->setStyleSheet(
	    "QPushButton { background: #3498db; color: #FFFFFF; border-radius: 2px;"
	    "              padding: 0; margin: 0; font-size: 16px; font-weight: bold; }"
	    "QPushButton:hover { background: #4EA8E5; }");
	headerLayout->addWidget(localAssetsLabel);
	headerLayout->addStretch();
	headerLayout->addWidget(addDrawerButton);
	headerRow->setLayout(headerLayout);

	fastGrid = new AssetViewGrid(this);
	//fastGrid->installEventFilter(this);

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
    if (ThemeManager::classicActive())
        _navPane->setStyleSheet("background: #202020;");

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
		fastGrid->filterAssets(item->data(0, Qt::UserRole).toInt());
	});

	// Inline rename commits straight to the database; a refused or empty name
	// snaps back on the rebuild.
	connect(treeWidget, &QTreeWidget::itemChanged, [this](QTreeWidgetItem *item, int column) {
		if (drawerTreeUpdating || column != 0) return;
		const int id = item->data(0, Qt::UserRole).toInt();
		if (id < 0) return;
		const QString name = item->text(0).trimmed();
		if (!name.isEmpty() && db->renameCollection(id, name))
			fastGrid->reassignCollections({ id }, id, name);   // tiles' collection_name
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
		if (auto tile = fastGrid->tileByGuid(guid)) moveAssetToDrawer(tile, drawerId);
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
	emptyLabel->setStyleSheet("font-size: 16px; color: #BABABA;");
	emptyL->addWidget(emptyIcon);
	emptyL->addWidget(emptyLabel);
	emptyGrid->setLayout(emptyL);

    auto meshObject = new QPushButton();
    meshObject->setAccessibleName("filterObj");
    meshObject->setIcon(QPixmap(IrisUtils::getAbsoluteAssetPath("/app/icons/icons8-cube-filled-50.png")));
    meshObject->setIconSize(QSize(16, 16));
    meshObject->setStyleSheet("border-top-left-radius: 2px; border-bottom-left-radius: 2px;");

	auto typeObject = new QPushButton();
	typeObject->setAccessibleName("filterObj");
	typeObject->setIcon(QPixmap(IrisUtils::getAbsoluteAssetPath("/app/icons/icons8-purchase-order-50.png")));
	typeObject->setIconSize(QSize(16, 16));

	//auto scriptObject = new QPushButton();
	//scriptObject->setAccessibleName("filterObj");
	//scriptObject->setIcon(QPixmap(IrisUtils::getAbsoluteAssetPath("/app/icons/icons8-music-50.png")));
	//scriptObject->setIconSize(QSize(16, 16));
	//scriptObject->setStyleSheet("border-top-right-radius: 2px; border-bottom-right-radius: 2px;");

	auto imageObject = new QPushButton();
	imageObject->setAccessibleName("filterObj");
	imageObject->setIcon(QPixmap(IrisUtils::getAbsoluteAssetPath("/app/icons/icons8-picture-50.png")));
	imageObject->setIconSize(QSize(16, 16));
	imageObject->setStyleSheet("border-top-right-radius: 2px; border-bottom-right-radius: 2px;");

	QWidget *filterGroup = new QWidget;
	auto fgL = new QHBoxLayout;
	fgL->addWidget(meshObject);
	//fgL->addWidget(typeObject);
	//fgL->addWidget(imageObject);
	//fgL->addWidget(scriptObject);
	filterGroup->setLayout(fgL);
    fgL->setContentsMargins(0, 0, 0, 0);
	fgL->setSpacing(0);

	searchTimer = new QTimer(this);
	searchTimer->setSingleShot(true);   // timer can only fire once after started

	connect(searchTimer, &QTimer::timeout, this, [this]() {
		fastGrid->searchTiles(searchTerm.toLower());
		rebuildAssetList();   // the list mirrors the grid's filtered set
	});

	filterPane = new QWidget;
	auto filterLayout = new QHBoxLayout;

	backdropLabel = new QLabel("Backdrop: ");
	backdropColor = new QComboBox();
	backdropColor->setView(new QListView());

	backdropColor->addItem("Plain Dark", 1);
	backdropColor->addItem("Plain Light", 2);
	backdropColor->addItem("Checkered Floor", 3);
	//backdropColor->addItem("Custom Color", 4);

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
		//else if ("Custom Color") {
		//	viewer->changeBackdrop(3);
		//}
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

	//filterLayout->addWidget(new QLabel("Filter: "));
	//filterLayout->addWidget(filterGroup);
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

	// The list view (owner request 2026-08-31): mirrors the editor panel's
	// list mode — name/type/size rows from the catalog, driving the very
	// same tile selection/preview/context plumbing.
	assetListView = new QTreeWidget;
	assetListView->setColumnCount(3);
	assetListView->setHeaderLabels({ tr("Name"), tr("Type"), tr("Size") });
	assetListView->setRootIsDecorated(false);
	assetListView->setAlternatingRowColors(false);
	assetListView->setUniformRowHeights(true);
	assetListView->setFrameShape(QFrame::NoFrame);
	assetListView->header()->setStretchLastSection(false);
	assetListView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	assetListView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	assetListView->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	assetListView->setContextMenuPolicy(Qt::CustomContextMenu);
	assetListView->setVisible(false);
	if (ThemeManager::classicActive())
		assetListView->setStyleSheet("background: #202020; border: 0");
	connect(assetListView, &QTreeWidget::itemClicked, this,
	        [this](QTreeWidgetItem *item, int) {
		if (auto *tile = fastGrid->tileByGuid(item->data(0, Qt::UserRole).toString()))
			fastGrid->lightSelectTile(tile);
	});
	connect(assetListView, &QTreeWidget::itemDoubleClicked, this,
	        [this](QTreeWidgetItem *item, int) {
		if (auto *tile = fastGrid->tileByGuid(item->data(0, Qt::UserRole).toString()))
			fastGrid->selectTile(tile);
	});
	connect(assetListView, &QTreeWidget::customContextMenuRequested, this,
	        [this](const QPoint &pos) {
		QTreeWidgetItem *item = assetListView->itemAt(pos);
		if (!item) return;
		if (auto *tile = fastGrid->tileByGuid(item->data(0, Qt::UserRole).toString())) {
			const QPoint global = assetListView->viewport()->mapToGlobal(pos);
			tile->projectContextMenu(tile->mapFromGlobal(global));
		}
	});

	// The post-dialog tail pump: one viewer preview/thumbnail per event-loop
	// turn (the app keeps painting and clicking between items), with a
	// subtle status strip under the grid while it works.
	tailStatusLabel = new QLabel;
	tailStatusLabel->setStyleSheet(
	    "padding: 4px 10px; color: #9a9a9a; font-size: 12px;");
	tailStatusLabel->setVisible(false);
	tailQueue = new ImportTailQueue(this);
	connect(tailQueue, &ImportTailQueue::progress, this, [this](int done, int total) {
		tailStatusLabel->setText(tr("Rendering previews… (%1 of %2)")
		                             .arg(done + 1).arg(total));
		tailStatusLabel->setVisible(true);
	});
	connect(tailQueue, &ImportTailQueue::finished, this, [this]() {
		tailStatusLabel->setVisible(false);
		fastGrid->updateGridColumns(fastGrid->lastWidth);
		filterFromSelection();
	});

	auto views = new QWidget;
	auto viewsL = new QVBoxLayout;
	// Clear the filter/search toolbar (owner direction 2026-08-31 — was
	// deliberately flush); the tile grid adds its own inner margins too.
	viewsL->setContentsMargins(0, 6, 0, 0);
	viewsL->setSpacing(0);
	viewsL->addWidget(emptyGrid);
	viewsL->addWidget(fastGrid);
	viewsL->addWidget(assetListView);
	viewsL->addWidget(tailStatusLabel);
	views->setLayout(viewsL);
    if (ThemeManager::classicActive())
        views->setStyleSheet("background: #202020");

	testL->addWidget(filterPane, 0, 0);
	testL->addWidget(views, 1, 0);
    _viewPane->setLayout(testL);

	// temp this should be checked before by emitting a signal
	fastGrid->setVisible(false);
	filterPane->setVisible(false);

	connect(fastGrid, &AssetViewGrid::gridCount, [this](int count) {
		if (count > 0) {
			filterPane->setVisible(true);
			emptyGrid->setVisible(false);
			const bool listMode = assetViewMode == QStringLiteral("list");
			fastGrid->setVisible(!listMode);
			assetListView->setVisible(listMode);
			rebuildAssetList();
		}
		else {
			filterPane->setVisible(false);
			emptyGrid->setVisible(true);
			fastGrid->setVisible(false);
			assetListView->setVisible(false);

            // closeViewer();
		}
	});

	// show assets
	int i = 0;
	// The tile's collection_name used to store the collection's int id — the
	// metadata pane showed a number. Store the actual name (§2 defect list).
	QMap<int, QString> drawerNames;
	for (const auto &coll : db->fetchCollections()) drawerNames.insert(coll.id, coll.name);
	foreach(const AssetRecord &record, db->fetchAssetsForAssetView()) {
		QJsonObject object;
		object["icon_url"] = "";
		object["guid"] = record.guid;
		object["name"] = record.name;
		object["type"] = record.type;
		object["collection"] = record.collection;
		object["collection_name"] = drawerNames.value(record.collection, tr("Uncategorized"));
		object["author"] = record.author;
		object["license"] = record.license;

        auto tags = QJsonDocument::fromJson(record.tags);

		QImage image;
		image.loadFromData(record.thumbnail, "PNG");

        if (image.isNull() && record.type == static_cast<int>(ModelTypes::Shader)) {
            image = QImage(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-72.png"));
        }

        auto sceneProperties = QJsonDocument::fromJson(record.properties);

		auto gridItem = new AssetGridItem(object, image, sceneProperties.object(), tags.object());
		wireTile(gridItem);

		fastGrid->addTo(gridItem, i);
		i++;
	}

	//QApplication::processEvents();
	fastGrid->updateGridColumns(fastGrid->lastWidth);

	// Restore the persisted Tiles/List choice (owner request 2026-08-31).
	setAssetViewMode(settings->getValue(QStringLiteral("assetView/viewMode"),
	                                    QStringLiteral("tiles")).toString(), false);

    _metadataPane = new QWidget; 
	_metadataPane->setObjectName(QStringLiteral("MetadataPane"));
    if (ThemeManager::classicActive())
        _metadataPane->setStyleSheet("background: #202020");
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
	updateAsset->setStyleSheet("background: #3498db");
	updateAsset->setVisible(false);

	// FIT TO SIZE (services/fitsize.h) \u2014 the three actions of the "Imported
	// size" row, created here so the chrome-button styling loop below reaches
	// them; the row itself is assembled with the metadata table.
	fitRemeasure = new QPushButton(tr("Re-measure"));
	fitReset = new QPushButton(tr("Reset"));
	fitSet = new QPushButton(tr("Set\u2026"));

	addToProject = new QPushButton("Add to Project");
	addToProject->setStyleSheet(StyleSheet::AssetViewAddToProjectButton());
	addToProject->setEnabled(false);

    deleteFromLibrary = new QPushButton("Delete From Library");
	deleteFromLibrary->setStyleSheet(StyleSheet::AssetViewDeleteButton());
    deleteFromLibrary->setEnabled(false);

	if (!ThemeManager::classicActive()) {
		// visible drop-target affordance (the classic dashed box was lost with
		// the sheet kill-switch) + the shared chrome button spec
		assetDropPad->setStyleSheet(
			"#assetDropPad { border: 2px dashed #4a4a4a; border-radius: 6px; }"
			// With the buttons below the box the label alone gives the drop
			// target its height — keep it a real target, not a thin strip.
			"#assetDropPadLabel { padding: 24px 8px; }");
		for (QPushButton *chromeBtn : { browseButton, downloadWorld, fitRemeasure,
		                                fitReset, fitSet, deleteFromLibrary })
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

	connect(fastGrid, &AssetViewGrid::selectedTileToAdd, [=](AssetGridItem *gridItem) {
		if (!gridItem->metadata.isEmpty()) {
			if (services && services->project && services->project->isSceneOpen()) {
				selectedGridItem = gridItem;
				addAssetItemToProject(gridItem);
				selectedGridItem = Q_NULLPTR;
			}
		}
	});

	// Plain click (the tile flip keeps preview loading on double-click):
	// the tile still becomes CURRENT — pane, rename/tags fields and the
	// bottom-right buttons act on what the user just clicked. This is what
	// made "Add to Project" look dead: the button read a selection plain
	// clicks no longer set.
	connect(fastGrid, &AssetViewGrid::lightSelectedTile, [this](AssetGridItem *gridItem) {
		if (gridItem->metadata.isEmpty()) return;
		selectedGridItem = gridItem;

		fetchMetadata(gridItem);
		populateAssetNodeTree(gridItem->metadata["guid"].toString(),
		                      gridItem->metadata["type"].toInt());

		renameModelField->setText(QFileInfo(gridItem->metadata["name"].toString()).baseName());
		QString tags;
		for (const auto childObj : gridItem->tags["tags"].toArray())
			tags.append(childObj.toString() + ", ");
		tags.chop(2);
		tagModelField->setText(tags);

		renameWidget->setVisible(true);
		tagWidget->setVisible(true);
		updateAsset->setVisible(true);
		deleteFromLibrary->setEnabled(true);
		updateAddToProjectButton();
	});

    connect(fastGrid, &AssetViewGrid::selectedTile, [&](AssetGridItem *gridItem) {
		fastGrid->deselectAll();
		stopMediaPreviews();   // switching tiles/pages stops playback (§2)

		renameWidget->setVisible(false);
		tagWidget->setVisible(false);
		updateAsset->setVisible(false);

		fetchMetadata(gridItem);

		populateAssetNodeTree(gridItem->metadata["guid"].toString(),
		                      gridItem->metadata["type"].toInt());

		if (!gridItem->metadata.isEmpty()) {

			selectedGridItem = gridItem;
			updateAddToProjectButton();
			deleteFromLibrary->setEnabled(true);

			renameWidget->setVisible(true);
			tagWidget->setVisible(true);
			updateAsset->setVisible(true);

			renameModelField->setText(QFileInfo(gridItem->metadata["name"].toString()).baseName());

			QString tags;
			QJsonArray children = gridItem->tags["tags"].toArray();
			for (auto childObj : children) {
				auto tag = childObj.toString();
				tags.append(tag + ", ");
			}

			tags.chop(2);

			tagModelField->setText(tags);

			// get material 
			//auto material_guid = db->getDependencyByType((int)AssetMetaType::Material, gridItem->metadata["guid"].toString());
			//auto material = db->getAssetMaterialGlobal(gridItem->metadata["guid"].toString());
			//auto materialObj = QJsonDocument::fromBinaryData(material);


			// viewer->setMaterial(materialObj.object());

			QVector3D pos;
			QVector3D rot;
			int distObj = 5;   // was read uninitialized when no camera props were stored

			bool cached = false;

			if (!gridItem->sceneProperties["camera"].toObject().isEmpty()) {
				auto props = gridItem->sceneProperties["camera"].toObject();
				auto posObj = props["pos"].toObject();
				distObj = props["distFromPivot"].toDouble(5.0);
				auto rotObj = props["rot"].toObject();

				pos.setX(posObj["x"].toDouble(0));
				pos.setY(posObj["y"].toDouble(0));
				pos.setZ(posObj["z"].toDouble(0));

				rot.setX(rotObj["x"].toDouble(0));
				rot.setY(rotObj["y"].toDouble(0));
				rot.setZ(rotObj["z"].toDouble(0));

				cached = true;
			}

			// The loading overlay (§1): visible from the double-click until the
			// viewer reports the load finished. The synchronous loads below
			// block the event loop, so paint it before starting.
			loadingTile = gridItem;
			gridItem->showLoadingOverlay();
			QApplication::processEvents();

            // PreviewRouter (ASSET_MEDIA_SPEC §2): ONE type→page map decides
            // the viewer; the old flat if-chain per type is gone.
            const QString guid = gridItem->metadata["guid"].toString();
            const QString name = gridItem->metadata["name"].toString();
            const auto type = static_cast<ModelTypes>(gridItem->metadata["type"].toInt());
            const PreviewPage page = PreviewRouter::pageFor(type);
            // The switch also stops whatever the previous page was playing
            // (the currentChanged hook), before the new page starts.
            viewers->setCurrentIndex(static_cast<int>(page));

            // The row's primary file, resolved through the CAS by guid — the
            // retired <root>/<guid>/ view is gone (deep audit 2026-09, area 6).
            const QString storeFile = AssetCas::resolveSource(
                QSqlDatabase::database(), AssetStorePaths::root(), guid);

            switch (page) {
            case PreviewPage::Viewer3D: {
                // THE SAVED CAMERA IS RESTORED ONLY IF THERE IS ONE (`cached`).
                // orientCamera ran unconditionally, so an asset with no stored
                // camera block — everything imported by a verb, by the avatar
                // module, by a drop that never reached the page's tail — had
                // the framing the load had just computed overwritten with the
                // DEFAULTS: the origin, distance 5, looking down -Z. On any
                // model bigger than about five metres that is a camera inside
                // the model, which is what the owner saw (smoke S5).
                const auto restoreCamera = [&]() {
                    viewer->orientCamera(iris::fromQt(pos), iris::fromQt(rot), distObj);
                };
                if (type == ModelTypes::Object || type == ModelTypes::ParticleSystem) {
                    // The model file IS the asset's source-role object; the
                    // old "scan the per-guid folder for a MODEL_EXTS suffix"
                    // was the retired legacy view's only remaining reader here.
                    const QString path = storeFile;
                    if (viewer->cachedAsset(guid))
                        viewer->addNodeToScene(viewer->cachedAsset(guid), guid, cached, false);
                    else
                        viewer->loadJafModel(path, guid, false, true, !cached);
                    if (cached) restoreCamera();
                    else        viewer->frameSubject();   // the editor's F, on the new subject
                }
                else if (type == ModelTypes::Material) {
                    viewer->loadJafMaterial(guid);
                    if (cached) restoreCamera();
                }
                else if (type == ModelTypes::Shader) {
                    QMap<QString, QString> map;
                    viewer->loadJafShader(guid, map);
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

			selectedGridItem = gridItem;
			selectedGridItem->highlight(true);

			// Types with no viewer load (textures, audio) end up here with the
			// overlay still up — and the viewer callback already fired for the
			// rest. Either way the overlay is done.
			clearLoadingTile();
		}
    });

	connect(updateAsset, &QPushButton::pressed, [this]() {
		// ONE path for name + tags (assettags, 2026-09-06 audit F3): this
		// button, assets.rename and assets.setTags all write through the same
		// service, which is also where the "changing one must not wipe the
		// other" rule lives. The inline JSON-blob builder this replaced was
		// the only tag writer in the product.
		const QStringList typedTags = tagModelField->text().split(QLatin1Char(','),
																  Qt::SkipEmptyParts);
		const QString newName = renameModelField->text();
		assettags::write(db, selectedGridItem->metadata["guid"].toString(), newName,
						 typedTags);

		QJsonObject tags;
		const QStringList stored = assettags::tagsOf(db, selectedGridItem->metadata["guid"].toString());
		if (!stored.isEmpty()) {
			QJsonArray actualTags;
			for (const QString &tag : stored) actualTags.append(tag);
			tags["tags"] = actualTags;
		}

		auto metadata = selectedGridItem->metadata;
		metadata["name"] = newName;
		metadata["full_filename"] = IrisUtils::buildFileName(newName, QFileInfo(selectedGridItem->metadata["full_filename"].toString()).suffix());

		selectedGridItem->updateMetadata(metadata, tags);
		fetchMetadata(selectedGridItem);
	});

	// FIT TO SIZE: the three actions of the "Imported size" row. Each is one
	// AssetMetadata::writeFit call — the same write assets.setFit makes.
	connect(fitRemeasure, &QPushButton::clicked, this, [this]() {
		if (!selectedGridItem || selectedGridItem->metadata.isEmpty()) return;
		applyFitChange(selectedGridItem->metadata["guid"].toString(),
		               QVariantMap{ { "remeasure", true } });
	});
	connect(fitReset, &QPushButton::clicked, this, [this]() {
		if (!selectedGridItem || selectedGridItem->metadata.isEmpty()) return;
		applyFitChange(selectedGridItem->metadata["guid"].toString(),
		               QVariantMap{ { "reset", true } });
	});
	connect(fitSet, &QPushButton::clicked, this, [this]() {
		if (!selectedGridItem || selectedGridItem->metadata.isEmpty()) return;
		const QString guid = selectedGridItem->metadata["guid"].toString();
		const QJsonObject meta = selectedGridItem->sceneProperties["metadata"].toObject();
		const fitsize::Extent extent = fitsize::extentOf(meta);
		const fitsize::Kind kind = fitsize::kindFromName(meta.value("fitKind").toString());
		const double measured = extent.valid ? fitsize::measureFor(extent, kind) : 0.0;
		if (!(measured > 0.0)) {
			qWarning() << "assets: this asset has no measured size to fit";
			return;
		}
		// The user types the SIZE they want, not a multiplier — a multiplier is
		// the implementation, a size in metres is the thing they can see.
		bool ok = false;
		const double wanted = QInputDialog::getDouble(
		    this, tr("Fit to Size"),
		    kind == fitsize::Kind::Character
		        ? tr("Height in metres (imported %1 m):").arg(measured, 0, 'g', 3)
		        : tr("Largest side in metres (imported %1 m):").arg(measured, 0, 'g', 3),
		    measured * fitsize::fitScaleOf(meta), 0.0001, 10000.0, 4, &ok);
		if (!ok || !(wanted > 0.0)) return;
		applyFitChange(guid, QVariantMap{ { "scale", wanted / measured } });
	});

	connect(addToProject, &QPushButton::pressed, [this]() {
		if (selectedGridItem && !selectedGridItem->metadata.isEmpty())
			addAssetItemToProject(selectedGridItem);
	});

	connect(deleteFromLibrary, &QPushButton::pressed, [this]() {
		deleteAssetFromLibrary(selectedGridItem);
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
		patterns << "*." + Constants::SHADER_EXT;
		for (const auto &ext : Constants::MATERIAL_EXTS) patterns << "*." + ext;
		for (const auto &ext : Constants::LIGHT_PROFILE_EXTS) patterns << "*." + ext;
		for (const auto &ext : Constants::WHITELIST) patterns << "*." + ext;
		patterns << "*." + Constants::ASSET_EXT;

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
	metadataMissing->setStyleSheet("padding: 12px; text-align: center");
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

	// ---- FIT TO SIZE (services/fitsize.h) ---------------------------------
	//
	// "Imported size: 17.3 m -> fitted to 1.75 m" with the three actions the
	// verb exposes. The row is the UI half of `assets.setFit`, and it calls the
	// SAME writes the verb does (API-first: the verb is the capability, this is
	// a caller of it) so a scripted change and a clicked one cannot diverge.
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
		fitButtons->addWidget(fitRemeasure);
		fitButtons->addWidget(fitReset);
		fitButtons->addWidget(fitSet);
		fitLayout->addWidget(fitLabel);
		fitLayout->addLayout(fitButtons);
		fitRow->setLayout(fitLayout);
		fitRow->setVisible(false);
	}
	l->addWidget(fitRow);

	l->addWidget(updateAsset);

	metadata->setLayout(l);
	//metadata->setStyleSheet("QLabel { font-size: 12px; }");
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

	//split->addWidget(viewer);
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
    storeOfflineBanner->setStyleSheet(
        "#StoreOfflineBanner { background: #7a4a12; }"
        "#StoreOfflineBanner QLabel { color: #ffe0b3; background: transparent; }");
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

void AssetView::updateAddToProjectButton()
{
	const bool haveTile = selectedGridItem && !selectedGridItem->metadata.isEmpty();
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
		    .arg(QFileInfo(selectedGridItem->metadata["name"].toString()).baseName()));
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

QString importProjectNameAV;
int on_extract_entry_av(const char *filename, void *arg) {
	QFileInfo fInfo(filename);
	if (fInfo.suffix() == "db") importProjectNameAV = fInfo.baseName();
	return 0;
}

// The old importJahModel viewer/tile tail — the archive is already imported
// (ImportBatchRunner committed it); this runs post-dialog on the UI thread.
void AssetView::finishJafImport(const ImportResult &result, const QString &fileName)
{
    if (result.jafKind == QStringLiteral("bundle")) return;

    const QString guid = result.assetGuid;
    filename = fileName;

    if (result.jafKind == QStringLiteral("material")) {
        viewers->setCurrentIndex(0);
        renameModelField->setText(QFileInfo(filename).baseName());
        viewer->loadJafMaterial(guid);
        addToJahLibrary(filename, guid, true);
    }
    else if (result.jafKind == QStringLiteral("shader")) {
        viewers->setCurrentIndex(0);
        renameModelField->setText(QFileInfo(filename).baseName());
        QMap<QString, QString> guidMap = result.guidMap;
        viewer->loadJafShader(guid, guidMap);
        addToJahLibrary(filename, guid, true);
    }
    else if (result.jafKind == QStringLiteral("sky")) {
        viewers->setCurrentIndex(0);
        renameModelField->setText(QFileInfo(filename).baseName());
        viewer->loadJafSky(guid);
        addToJahLibrary(filename, guid, true);
    }
    else if (result.jafKind == QStringLiteral("texture")) {
        renameModelField->setText(QFileInfo(filename).baseName());
        viewers->setCurrentIndex(1);
        const QString imagePath = AssetCas::resolveSource(
            QSqlDatabase::database(), AssetStorePaths::root(), guid);
        QPixmap image(imagePath);
        assetImageCanvas->setPixmap(image.scaledToHeight(480, Qt::SmoothTransformation));
        addToJahLibrary(filename, guid, true);
    }
    else if (result.jafKind == QStringLiteral("object")) {
        viewers->setCurrentIndex(0);
        // The archive's model file: the asset's source-role object.
        const QString path = AssetCas::resolveSource(
            QSqlDatabase::database(), AssetStorePaths::root(), guid);
        renameModelField->setText(QFileInfo(filename).baseName());
        viewer->loadJafModel(path, guid);
        addToJahLibrary(filename, guid, true);
    }
}

// The mesh tail, one item per event-loop turn (see scheduleViewerTails). The
// pipeline half already ran on the batch runner's worker; ImportMeshTail
// consumes ITS parsed fragment (ImportResult::node) — engine upload +
// offscreen render only, no second assimp parse — and the tile that appeared
// mid-batch takes the rendered thumbnail.
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

    if (auto *tile = fastGrid->tileByGuid(result.assetGuid)) {
        tile->hideLoadingOverlay();
        const auto record = db->fetchAsset(result.assetGuid);
        QImage thumbnail;
        if (thumbnail.loadFromData(record.thumbnail, "PNG"))
            tile->setTile(QPixmap::fromImage(thumbnail));
        // The freshly rendered camera properties feed the pane on selection.
        tile->sceneProperties = QJsonDocument::fromJson(record.properties).object();
    }

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
    AssetGridItem *tile = fastGrid->tileByGuid(guid);
    if (!tile && !db->fetchAsset(guid).guid.isEmpty()) {
        // The page MIRRORS the library: a row that exists but whose tile has
        // not been announced yet (a verb's import, a queued announcement that
        // has not run) still selects — the tile is built now. The announcement
        // handler checks for an existing tile, so nothing doubles up.
        addLibraryTileForAsset(guid);
        tile = fastGrid->tileByGuid(guid);
    }
    if (!tile) return false;
    fastGrid->selectTile(tile);          // the double-click path: preview + pane
    fastGrid->ensureWidgetVisible(tile, 32, 32);
    return true;
}

QString AssetView::selectedAssetGuid() const
{
    return selectedGridItem ? selectedGridItem->metadata["guid"].toString() : QString();
}

// THE import dispatch (ASSET_DRAWERS_SPEC §3): drop pad and browse dialog both
// land here; one switch keyed on ModelTypes decides each file's request, so a
// new library type (Video, …) is one case. The batch then runs THREADED —
// ImportBatchRunner + the cancellable progress dialog (multi-file drops count
// "N of M" through one dialog).
void AssetView::importFiles(const QStringList &fileNames)
{
	QVector<ImportRequest> requests;
	for (const auto &fileName : fileNames) {
		if (fileName.isEmpty()) continue;

		ImportRequest request;
		request.sourcePath = fileName;

		const QString suffix = QFileInfo(fileName).suffix().toLower();
		if (suffix != Constants::ASSET_EXT) {   // .jaf sniffs importer-side
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
		}
		requests.append(request);
	}
	if (!requests.isEmpty()) runImportBatch(requests);
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
		// waits on the viewer): mesh/.jaf previews + rendered thumbnails,
		// video frame grabs — queued ONE PER EVENT-LOOP TURN so the app
		// never freezes; each tile updates live as its render lands.
		scheduleViewerTails();

		fastGrid->updateGridColumns(fastGrid->lastWidth);
		filterFromSelection();

		// SELECT WHAT WAS IMPORTED (smoke S4). A mesh selects when its tail
		// lands (the preview is part of the selection); everything else —
		// images, audio, video, .jaf archives — has no tail, so the batch
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

	const bool isJaf =
	    QFileInfo(request.sourcePath).suffix().toLower() == Constants::ASSET_EXT;
	if (isJaf || request.typeHint == static_cast<int>(ModelTypes::Mesh)) {
		// Viewer-driven types: the preview render happens post-dialog, one
		// item per event-loop turn (scheduleViewerTails). Mesh tiles appear
		// NOW, mid-batch, with the loading overlay up — the render lands on
		// the tile when its turn comes.
		pendingViewerTails.append({ result, request.sourcePath });
		lastImportedGuid = result.assetGuid;
		if (!isJaf) {
			addLibraryTileForAsset(result.assetGuid);
			if (auto *tile = fastGrid->tileByGuid(result.assetGuid))
				tile->showLoadingOverlay();
		}
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
	for (const auto &tail : tails) {
		if (QFileInfo(tail.fileName).suffix().toLower() == Constants::ASSET_EXT)
			tailQueue->enqueue([this, tail]() {
				finishJafImport(tail.result, tail.fileName);
			});
		else
			tailQueue->enqueue([this, tail]() {
				finishMeshTailItem(tail.result, tail.fileName);
			});
	}

	const auto videoGuids = pendingVideoThumbGuids;
	pendingVideoThumbGuids.clear();
	for (const QString &guid : videoGuids) {
		// The worker could not run QMediaPlayer (GUI-thread-only), so the row
		// committed with the film icon; grab the real first-second frame on
		// the queue — the same path as tile right-click → Rebuild Thumbnail.
		tailQueue->enqueue([this, guid]() {
			if (AssetGridItem *tile = fastGrid->tileByGuid(guid))
				rebuildTileThumbnail(tile);
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
			if (!self || !self->fastGrid) return;
			if (!self->fastGrid->tileByGuid(guid)) self->addLibraryTileForAsset(guid);
		}, Qt::QueuedConnection);
	});
}

void AssetView::addLibraryTileForAsset(const QString &guid)
{
	// The committed row (the same rows the assets.importFile verb writes) —
	// this is only the tile tail; the pipeline ran on the batch runner.
	const auto record = db->fetchAsset(guid);
	if (record.guid.isEmpty()) return;

	QJsonObject object;
	object["icon_url"] = "";
	object["guid"] = record.guid;
	object["name"] = record.name;
	object["type"] = record.type;
	object["collection"] = record.collection;
	object["collection_name"] = drawerName(record.collection);
	object["author"] = record.author;
	object["license"] = record.license;

	QImage thumbnail;
	thumbnail.loadFromData(record.thumbnail, "PNG");

	// The row's properties carry the freshly computed "metadata" block —
	// hand it to the tile so the pane shows it without a backfill round-trip.
	auto gridItem = new AssetGridItem(object, thumbnail,
	                                  QJsonDocument::fromJson(record.properties).object(),
	                                  QJsonObject());
	wireTile(gridItem);
	fastGrid->addTo(gridItem, 0);
	fastGrid->updateGridColumns(fastGrid->lastWidth);
	filterFromSelection();
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

void AssetView::extractTexturesAndMaterialFromMaterial(const QString &filePath,
                                                       QStringList &textureList,
                                                       QJsonObject &mat)
{
    QFile *file = new QFile(filePath);
    file->open(QIODevice::ReadOnly | QIODevice::Text);
    QJsonDocument doc = QJsonDocument::fromJson(file->readAll());

    const QJsonObject materialDefinition = doc.object();

    auto material_name = materialDefinition["name"].toString();
    if (material_name.isEmpty()) material_name = "Default";

    // The definition's legacy Default-shader key names are renamed to their PBR
    // equivalents and then drive a PbrMaterial's own rows (HLMS_ADOPTION P4b).
    // This used to load the matching `.shader` file just to borrow its uniform
    // list, which meant a definition naming a shader that no longer existed
    // silently produced a material with no properties and no values.
    const QJsonObject normalised = BuiltinMaterials::normaliseLegacyDefinition(materialDefinition);
    auto material = iris::PbrMaterial::create();
    material->setName(material_name);

    for (const auto &prop : material->properties) {
        if (normalised.contains(prop->name)) {
            if (prop->type == iris::PropertyType::Texture) {
                auto textureStr = !normalised[prop->name].toString().isEmpty()
                ? normalised[prop->name].toString()
                : QString();
                material->setValue(prop->name, textureStr);
                if (!textureStr.isEmpty()) {
                    textureList.append(QFileInfo(textureStr).fileName());
                }
            }
            else {
                material->setValue(prop->name, normalised[prop->name].toVariant());
            }
        }
    }

    SceneWriter::writeSceneNodeMaterial(mat, material, false);
}

void AssetView::addToJahLibrary(const QString fileName, const QString guid, bool jfx)
{
    QJsonObject tags;
    QJsonArray actualTags;

    QFileInfo fInfo(filename);
    QJsonObject object;
    object["icon_url"] = "";
    object["name"] = QFileInfo(fileName).baseName(); // renameModelField->text();

    //auto thumbnail = viewer->takeScreenshot(512, 512);

    auto bytes = db->fetchAsset(guid).thumbnail;
    QImage thumbnail;
    if (!thumbnail.loadFromData(bytes, "PNG")) {
        //thumbnail = viewer->takeScreenshot(512, 512);
        //db->updateAssetThumbnail(guid, bytes);
    }


    //db->updateAssetThumbnail(guid, bytes);
	db->updateAssetViewFilter(guid, 2);

    object["type"] = db->fetchAsset(guid).type;

	if (object["type"].toInt() != static_cast<int>(ModelTypes::Sky)) {
        db->updateAssetProperties(guid, QJsonDocument(viewer->getSceneProperties()).toJson());
	}

    if (object["type"].toInt() == static_cast<int>(ModelTypes::Shader)) {
        thumbnail = QImage(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-72.png"));
    }

	if (object["type"].toInt() == static_cast<int>(ModelTypes::Sky)) {
		thumbnail = QImage(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-sky.png"));
	}

    if (object["type"].toInt() == static_cast<int>(ModelTypes::ParticleSystem)) {
        thumbnail = QImage(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-ps.png"));
    }

    object["guid"] = guid;

    auto gridItem = new AssetGridItem(object, thumbnail, viewer->getSceneProperties(), tags);
    wireTile(gridItem);

    viewer->cacheCurrentModel(guid);

    fastGrid->addTo(gridItem, 0, true);
    QApplication::processEvents();
    fastGrid->updateGridColumns(fastGrid->lastWidth);

    renameWidget->setVisible(true);
    tagWidget->setVisible(true);
    updateAsset->setVisible(true);
}

void AssetView::addToLibrary(const QString& main_guid, bool jfx)
{
	//bool canAdd = db->isAuthorInfoPresent();
	QJsonObject tags;
	QJsonArray actualTags;

	// parse tags
	//QString stringIn = tagModelField->text();
	//if (!stringIn.isEmpty()) {
	//	std::vector<QString> commaSeparated(1);
	//	int commaCounter = 0;
	//	for (int i = 0; i<stringIn.size(); i++) {
	//		if (stringIn[i] == ",") {
	//			commaSeparated.push_back("");
	//			commaCounter++;
	//		}
	//		else {
	//			commaSeparated.at(commaCounter) += stringIn[i];
	//		}
	//	}

	//	for (const QString &tag : commaSeparated) {
	//		if (!tag.isEmpty()) actualTags.append(tag);
	//	}

	//	tags["tags"] = actualTags;
	//}

	//if (canAdd) {
		QFileInfo fInfo(filename);
		QJsonObject object;
		object["icon_url"] = "";
		object["name"] = QFileInfo(filename).baseName(); // renameModelField->text();

    auto assetSnapshot = viewer->takeScreenshot(512, 512);

    QJsonDocument tagsDoc(tags);

    // // maybe actually check if Object?
    // QString guid;
    // if (jfx) {
    //     guid = db->createAssetEntry(
    //         main_guid,
    //         QFileInfo(filename).fileName(),
    //         static_cast<int>(ModelTypes::Object),
    //         QString(),
    //         QString(),
    //         "JahFX",
    //         AssetHelper::makeBlobFromPixmap(QPixmap::fromImage(assetSnapshot)),
    //         QJsonDocument(viewer->getSceneProperties()).toJson(),
    //         tagsDoc.toJson(),
    //         QJsonDocument(viewer->getMaterial()).toJson(),
    //         AssetViewFilter::AssetsView
    //         );
    // }
    // else {
    //     guid = db->createAssetEntry(
    //         main_guid,
    //         QFileInfo(filename).fileName(),
    //         static_cast<int>(ModelTypes::Object),
    //         QString(),
    //         QString(),
    //         QString(),
    //         AssetHelper::makeBlobFromPixmap(QPixmap::fromImage(assetSnapshot)),
    //         QJsonDocument(viewer->getSceneProperties()).toJson(),
    //         tagsDoc.toJson(),
    //         QJsonDocument(viewer->getMaterial()).toJson(),
    //         AssetViewFilter::AssetsView
    //         );
    // }

    object["guid"] = main_guid;
    object["type"] = db->fetchAsset(main_guid).type; // model?
    object["full_filename"] = IrisUtils::buildFileName(main_guid, fInfo.suffix());
    if (jfx) {
        object["author"] = "JahFX";// db->getAuthorName();
    }
    else {
        object["author"] = "";// db->getAuthorName();
    }
    object["license"] = "CCBY";



   //  if (!QDir(QDir(assetPath).filePath(main_guid)).exists()) {
   //      QDir().mkdir(QDir(assetPath).filePath(main_guid));
   //      bool copyFile = QFile::copy(filename,
   //                                  QDir(QDir(assetPath).filePath(main_guid)).filePath(
   //                                      IrisUtils::buildFileName(main_guid, fInfo.suffix().toLower()))
   //                                  );
   //  }

   // copyTextures(main_guid);

		//auto material_guid = db->insertMaterialGlobal(QFileInfo(filename).baseName() + "_material", guid, QJsonDocument(viewer->getMaterial()).toBinaryData());
		//db->insertGlobalDependency(static_cast<int>(ModelTypes::Material), guid, material_guid);

		auto gridItem = new AssetGridItem(object, assetSnapshot, viewer->getSceneProperties(), tags);
		wireTile(gridItem);

    viewer->cacheCurrentModel(main_guid);

		fastGrid->addTo(gridItem, 0, true);
		QApplication::processEvents();
		fastGrid->updateGridColumns(fastGrid->lastWidth);

		renameWidget->setVisible(true);
		tagWidget->setVisible(true);
		updateAsset->setVisible(true);
		//addToLibrary->setVisible(false);
	//}
	//else {
	//	auto option = QMessageBox::question(this,
	//		"No Author!", "There is no author set, would you like to set a name now?\n"
	//		"Without it you will not be able to import assets.\n\n"
	//		"Enter a valid name in the Author field and save.",
	//		QMessageBox::Yes | QMessageBox::No);

	//	if (option == QMessageBox::Yes) {
	//		prefsDialog->exec();
	//	}
	//	else {
	//		QMessageBox::warning(this, "Failed to add asset!", "Nothing was done.", QMessageBox::Ok);
	//	}
	//}
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
		// FIT TO SIZE (services/fitsize.h): the measured size in metres. The
		// fit itself and its actions live in the fit row below the table.
		const fitsize::Extent extent = fitsize::extentOf(meta);
		if (extent.valid)
			rows.append({ "Imported Size",
			              QStringLiteral("%1 \u00d7 %2 \u00d7 %3 m")
			                  .arg(extent.x, 0, 'g', 3).arg(extent.y, 0, 'g', 3)
			                  .arg(extent.z, 0, 'g', 3) });
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

void AssetView::fetchMetadata(AssetGridItem *widget, bool allowBackfill)
{
	if (!widget->metadata.isEmpty()) {
		metadataMissing->setVisible(false);
		metadataDetails->setVisible(true);

		MetadataRows rows;
		rows.append({ tr("Type"), getAssetType(widget->metadata["type"].toInt()) });

		// The rich per-type block: import-time for new assets, lazily
		// backfilled (worker thread + update-on-arrival) for old libraries.
		{
			const QString guid = widget->metadata["guid"].toString();
			const auto record = db->fetchAsset(guid);
			QJsonObject meta = widget->sceneProperties["metadata"].toObject();
			if (meta.isEmpty()) {
				// another session (or the verb) may have persisted it already
				meta = QJsonDocument::fromJson(record.properties).object()["metadata"].toObject();
				if (!meta.isEmpty()) widget->sceneProperties["metadata"] = meta;
			}
			if (!meta.isEmpty()) {
				appendMetadataRows(rows, meta, record.dateCreated);
			}
			else if (allowBackfill) {
				rows.append({ tr("Details"), QStringLiteral("…") });
				backfillMetadata(widget, guid, record.type);
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
			const QString assetGuid = widget->metadata["guid"].toString();
			const auto all = assetdelete::pins(db, assetGuid);
			const auto live = assetdelete::livePins(db, assetGuid);
			const int dead = all.size() - live.size();
			QString used = live.isEmpty() ? tr("no projects")
			                              : tr("%n project(s): %1", "", live.size())
			                                    .arg(pinnedProjectNames(live));
			if (dead > 0) used += tr(" (+%n pin(s) from deleted projects)", "", dead);
			rows.append({ tr("Used by"), used });
		}
		rows.append({ tr("Public"), widget->metadata["is_public"].toBool() ? tr("true") : tr("false") });
		rows.append({ tr("Author"), widget->metadata["author"].toString() });
		rows.append({ tr("License"), widget->metadata["license"].toString() });
		
		const QString collection = widget->metadata["collection_name"].toString();
		if (!collection.isEmpty()) rows.append({ tr("Collection"), collection });

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

// ---- FIT TO SIZE (services/fitsize.h) --------------------------------------
//
// The "Imported size: 17.3 m -> fitted to 1.75 m [Re-measure] [Reset] [Set...]"
// row. MODEL rows only: everything else has no measured size, so the row is
// hidden rather than shown empty. Every button goes through
// AssetMetadata::writeFit — the one write `assets.setFit` makes.
void AssetView::refreshFitRow(const QString &guid, int assetType, const QJsonObject &meta)
{
	if (!fitRow) return;
	if (assetType != static_cast<int>(ModelTypes::Object) || meta.isEmpty()) {
		fitRow->setVisible(false);
		return;
	}

	const fitsize::Extent extent = fitsize::extentOf(meta);
	const double scale = fitsize::fitScaleOf(meta);
	const fitsize::Kind kind = fitsize::kindFromName(meta.value("fitKind").toString());
	const bool manual = meta.value("fitSource").toString() == QLatin1String("manual");
	const double measured = extent.valid ? fitsize::measureFor(extent, kind) : 0.0;

	QString text;
	if (!extent.valid) {
		// A row imported before the size policy, or a file with no geometry.
		text = tr("Imported size: not measured");
	} else if (!fitsize::isFitted(scale)) {
		text = tr("Imported size: %1 m \u2014 placed as authored")
		           .arg(measured, 0, 'g', 3);
	} else {
		text = tr("Imported size: %1 m \u2192 fitted to %2 m (\u00d7%3%4)")
		           .arg(measured, 0, 'g', 3)
		           .arg(measured * scale, 0, 'g', 3)
		           .arg(scale, 0, 'g', 4)
		           .arg(manual ? tr(", by hand") : QString());
	}
	fitLabel->setText(text);
	fitLabel->setToolTip(meta.value("fitReason").toString());

	// Reset is only meaningful when there is something to go back FROM.
	fitReset->setEnabled(manual || fitsize::isFitted(scale));
	fitRow->setVisible(true);
}

void AssetView::applyFitChange(const QString &guid, const QVariantMap &options)
{
	QString error;
	const auto change = options.contains("scale") ? AssetMetadata::FitChange::Manual
	                  : options.value("remeasure").toBool() ? AssetMetadata::FitChange::Remeasure
	                                                        : AssetMetadata::FitChange::Reset;
	const QJsonObject meta = AssetMetadata::writeFit(db, guid, change,
	                                                 options.value("scale").toDouble(), &error);
	if (meta.isEmpty()) {
		qWarning() << "assets: fit change failed:" << error;
		return;
	}
	if (selectedGridItem) {
		selectedGridItem->sceneProperties["metadata"] = meta;
		fetchMetadata(selectedGridItem);
	}
}

void AssetView::backfillMetadata(AssetGridItem *widget, const QString &guid, int assetType)
{
	const QString folder = IrisUtils::join(AssetMetadata::storeRootPath(), guid);
	QPointer<AssetGridItem> tile(widget);

	// Video is the one kind whose rich fields need the GUI thread
	// (QMediaPlayer probe — ASSET_MEDIA_SPEC §1): compute right here, where
	// ensure() persists the complete block, instead of on the worker where
	// it would come back degraded.
	if (assetType == static_cast<int>(ModelTypes::Video)) {
		const QJsonObject meta = AssetMetadata::ensure(db, guid);
		if (tile) {
			if (!meta.isEmpty()) tile->sceneProperties["metadata"] = meta;
			if (selectedGridItem == tile) fetchMetadata(tile);
		}
		return;
	}

	auto *watcher = new QFutureWatcher<QJsonObject>(this);
	connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher, tile, guid]() {
		watcher->deleteLater();
		const QJsonObject meta = watcher->result();
		if (meta.isEmpty()) {
			// nothing on disk to describe (e.g. a built-in) — re-render the
			// table with the basic rows only (no backfill retry loop)
			if (tile && selectedGridItem == tile) fetchMetadata(tile, false);
			return;
		}

		// Persist on the UI thread (it owns the SQLite connection), guarded
		// so a concurrent backfill (verb, second selection) wins only once.
		QJsonObject props = QJsonDocument::fromJson(db->fetchAsset(guid).properties).object();
		if (!props.contains("metadata")) {
			props["metadata"] = meta;
			db->updateAssetProperties(guid, QJsonDocument(props).toJson());
		}

		if (tile) {
			tile->sceneProperties["metadata"] = props.contains("metadata")
			                                        ? props["metadata"].toObject() : meta;
			if (selectedGridItem == tile) fetchMetadata(tile);   // re-renders with data
		}
	});
	// Pure file inspection (assimp / image header / wav header) — thread-safe.
	watcher->setFuture(QtConcurrent::run(
	    [assetType, folder]() { return AssetMetadata::computeForStore(assetType, folder); }));
}

void AssetView::addAssetItemToProject(AssetGridItem *item)
{
	// Are-you-sure first (owner direction): every UI entry point — the
	// button, Shift+click and the tile context menu — funnels through here,
	// so one dialog covers all three. The headless verb
	// (assets.addToProject) never comes this way and stays dialog-free.
	const QString assetName =
	    QFileInfo(item->metadata["name"].toString()).baseName();
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
	const QString guid = item->metadata["guid"].toString();
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
		tr("%1 has been added successfully to the open project.").arg(item->metadata["name"].toString())
	);
}

void AssetView::moveAssetToDrawer(AssetGridItem *item, int drawerId)
{
	const auto guid = item->metadata["guid"].toString();
	if (guid.isEmpty() || !db->switchAssetCollection(drawerId, guid)) return;

	item->metadata["collection"] = drawerId;
	item->metadata["collection_name"] = drawerName(drawerId);
	if (selectedGridItem == item) fetchMetadata(item);

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
	fastGrid->reassignCollections(subtree, 0, drawerName(0));
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
	fastGrid->filterAssets(treeWidget->currentItem()
	    ? treeWidget->currentItem()->data(0, Qt::UserRole).toInt() : -1);
	rebuildAssetList();   // the list mirrors the grid's filtered set
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
		fastGrid->setVisible(!listMode);
		assetListView->setVisible(listMode);
	}
	if (listMode) rebuildAssetList();
	if (persist && settings)
		settings->setValue(QStringLiteral("assetView/viewMode"), assetViewMode);
}

void AssetView::rebuildAssetList()
{
	if (!assetListView || assetViewMode != QStringLiteral("list")) return;

	const QMap<QString, qint64> sizes = db->fetchAssetFileSizes();
	const QLocale locale;

	assetListView->clear();
	for (AssetGridItem *tile : fastGrid->tiles()) {
		// Mirror the grid's search/drawer filtering: a tile hidden by
		// searchTiles/filterAssets stays out of the list too (isVisibleTo
		// ignores whether the grid pane itself is currently shown).
		if (!tile->isVisibleTo(tile->parentWidget())) continue;

		const QString guid = tile->metadata["guid"].toString();
		auto *row = new QTreeWidgetItem(assetListView);
		row->setText(0, tile->metadata["name"].toString());
		row->setText(1, getAssetType(tile->metadata["type"].toInt()));
		row->setText(2, sizes.contains(guid)
		                    ? locale.formattedDataSize(sizes.value(guid))
		                    : QStringLiteral("—"));
		row->setData(0, Qt::UserRole, guid);
	}
}

void AssetView::wireTile(AssetGridItem *gridItem)
{
	gridItem->setDrawerProvider([this]() { return drawerMenuEntries(); });

	connect(gridItem, &AssetGridItem::addAssetItemToProject, [this](AssetGridItem *item) {
		addAssetItemToProject(item);
	});

	connect(gridItem, &AssetGridItem::moveAssetToDrawer, [this](AssetGridItem *item, int drawerId) {
		moveAssetToDrawer(item, drawerId);
	});

	connect(gridItem, &AssetGridItem::deleteAssetFromLibrary, [this](AssetGridItem *item) {
		deleteAssetFromLibrary(item);
	});

	connect(gridItem, &AssetGridItem::rebuildThumbnail, [this](AssetGridItem *item) {
		rebuildTileThumbnail(item);
	});

	connect(gridItem, &AssetGridItem::createMaterialFromImage, [this](AssetGridItem *item) {
		createMaterialFromImageTile(item);
	});

	// AVATARS (§5.5). The rigged test is lazy — one metadata read when a menu
	// is actually opened, never on the grid build.
	gridItem->setRiggedProvider([this](const QString &guid) {
		return AssetMetadata::ensure(db, guid).value(QStringLiteral("hasSkeleton")).toBool();
	});
	connect(gridItem, &AssetGridItem::editAvatarAsset, [this](AssetGridItem *item) {
		if (!item || item->metadata.isEmpty()) return;
		// The Assets page is the LIBRARY's view of the world, so its Edit opens
		// the library version. The editor drawer's Edit opens the project's.
		emit editAssetInModule(item->metadata["guid"].toString(), QStringLiteral("avatar"),
		                       QStringLiteral("library"));
	});
	connect(gridItem, &AssetGridItem::createAvatarFromModel, [this](AssetGridItem *item) {
		createAvatarFromModelTile(item);
	});
}

void AssetView::createAvatarFromModelTile(AssetGridItem *item)
{
	if (!item || item->metadata.isEmpty()) return;
	const QString objectGuid = item->metadata["guid"].toString();

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

void AssetView::createMaterialFromImageTile(AssetGridItem *item)
{
	// IMAGE_PLANE_SPEC option B1 — the same helper the automatic companion
	// material and materials.createFromImage use.
	if (!item || item->metadata.isEmpty()) return;
	const QString textureGuid = item->metadata["guid"].toString();

	QString error;
	const QString materialGuid =
	    ImageMaterial::createMaterialAsset(textureGuid, db, project, &error);
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

	// The library tile for the new material, same tail the import path uses.
	addLibraryTileForAsset(materialGuid);
}

void AssetView::rebuildTileThumbnail(AssetGridItem *item)
{
	if (!item || item->metadata.isEmpty()) return;
	const QString guid = item->metadata["guid"].toString();
	const auto record = db->fetchAsset(guid);
	if (record.guid.isEmpty()) return;

	const QString sourceFile = AssetCas::resolveSource(
	    QSqlDatabase::database(), AssetStorePaths::root(), guid);

	QPixmap pixmap;
	switch (static_cast<ModelTypes>(record.type)) {
	case ModelTypes::Texture: {
		// straight from the source image, like the import path
		auto thumb = ThumbnailManager::createThumbnail(sourceFile, 256, 256);
		if (thumb && !thumb->thumb.isNull()) pixmap = QPixmap::fromImage(thumb->thumb);
		break;
	}
	case ModelTypes::Music:
		pixmap = QPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-music.png"));
		break;
	case ModelTypes::Video:
		// Re-grab the first-second frame (film icon when decode fails).
		pixmap = VideoUtils::thumbnailFor(sourceFile);
		break;
	case ModelTypes::Object:
	case ModelTypes::ParticleSystem: {
		// THE one thumbnail routine — `assets.refreshThumbnail`'s body: the
		// stored blob, fitted, framed. (It used to be a screenshot of this
		// page's viewer, which is a second render of a second node.) The
		// preview follows so the user sees what was rebuilt.
		viewers->setCurrentIndex(0);
		const QImage shot = assetthumb::renderObject(db, project, guid,
		                                             EngineHost::instance().engine());
		if (!shot.isNull()) pixmap = QPixmap::fromImage(shot);
		viewer->loadJafModel(sourceFile, guid, false, true, false);
		break;
	}
	case ModelTypes::Material: {
		viewers->setCurrentIndex(0);
		viewer->loadJafMaterial(guid);
		const QImage shot = viewer->takeScreenshot(512, 512);
		if (!shot.isNull()) pixmap = QPixmap::fromImage(shot);
		break;
	}
	case ModelTypes::Shader: {
		viewers->setCurrentIndex(0);
		QMap<QString, QString> map;
		viewer->loadJafShader(guid, map);
		const QImage shot = viewer->takeScreenshot(512, 512);
		if (!shot.isNull()) pixmap = QPixmap::fromImage(shot);
		break;
	}
	case ModelTypes::Sky: {
		viewers->setCurrentIndex(0);
		viewer->loadJafSky(guid);
		const QImage shot = viewer->takeScreenshot(512, 512);
		if (!shot.isNull()) pixmap = QPixmap::fromImage(shot);
		break;
	}
	case ModelTypes::Animation: {
		// The POSE STRIP the import drew, redrawn from the stored bytes. There
		// is nothing to render in a viewer — a clip has no geometry of its own
		// and playing it needs a rig to play it on — so the default branch
		// below would replace a readable thumbnail with a file icon.
		QImage strip;
		animfile::read(sourceFile, &strip, 256, 256);
		if (!strip.isNull()) pixmap = QPixmap::fromImage(strip);
		break;
	}
	default:
		pixmap = QPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-72.png"));
		break;
	}

	if (pixmap.isNull()) {
		QMessageBox::warning(this, tr("Rebuild Thumbnail"),
		                     tr("Could not rebuild this thumbnail."), QMessageBox::Ok);
		return;
	}

	db->updateAssetThumbnail(guid, AssetHelper::makeBlobFromPixmap(pixmap));
	item->setTile(pixmap);   // the tile updates live
}

void AssetView::clearLoadingTile()
{
	if (!loadingTile) return;
	loadingTile->hideLoadingOverlay();
	loadingTile = nullptr;
}

// LIBRARY DELETE (owner, 2026-09-09): deleting from the library never takes
// an asset out of a project. The decision and the write both live in
// services/assetdelete.h — the same code `assets.remove` runs (API-first) —
// and this function is the two-step confirmation in front of it.
void AssetView::deleteAssetFromLibrary(AssetGridItem *item)
{
	if (!item || item->metadata.isEmpty()) return;
	const QString guid = item->metadata["guid"].toString();
	const QString name = item->metadata["name"].toString();
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
	const auto outcome = assetdelete::remove(db, guid, /*keepShared*/ false, force);
	if (!outcome.ok) {
		QMessageBox::warning(this, tr("Delete Failed!"),
		    tr("The library database refused the delete; the asset is still catalogued. "
		       "See jahshaka.log for the failing query."), QMessageBox::Ok);
		return;
	}

	// The TILE GOES LAST (code review 2026-09-10): everything below still
	// reads `item` — fetchMetadata dereferences it — and deleteTile hands the
	// widget to deleteLater. That is safe only because the delete is deferred
	// to the event loop; ordering it here makes it safe by construction.
	item->metadata = QJsonObject();
	renameWidget->setVisible(false);
	tagWidget->setVisible(false);
	updateAsset->setVisible(false);

	if (selectedGridItem == item) selectedGridItem = nullptr;
	updateAddToProjectButton();
	deleteFromLibrary->setEnabled(false);

	fetchMetadata(item);
	clearViewer();
	fastGrid->deleteTile(item);

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
