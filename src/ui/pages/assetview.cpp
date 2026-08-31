/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/pages/assetview.h"
#include "ui/pages/iassetviewer.h"
#include "ui/pages/headlessassetviewer.h"
#include <QTimer>
#include "ui/dialogs/progressdialog.h"
#include "data/settingsmanager.h"
#include "ui/dialogs/preferencesdialog.h"
#include "ui/dialogs/preferences/worldsettingswidget.h"

#include "irisgl/core/irisutils.h"
#include "irisgl/document/assets/mesh.h"
#include "zip.h"

#include <QStackedLayout>
#include <QDirIterator>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QMessageBox>
#include <QFileDialog>
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
#include <QLocale>
#include <QPointer>
#include <QtConcurrent>

#include "data/constants.h"
#include "data/settingsmanager.h"
#include "data/database/database.h"
#include "data/project.h"
#include "services/services.h"
#include "services/assetstore.h"
#include "services/assetstorepaths.h"
#include "services/projectservice.h"
#include "ui/controls/assetviewgrid.h"
#include "ui/controls/assetgriditem.h"
#include "ui/controls/drawertreewidget.h"
#include "services/assethelper.h"
#include "services/assetimporter.h"
#include "services/assetmetadata.h"
#include "services/audiopeaks.h"
#include "services/videoutils.h"
#include "ui/controls/videopreviewwidget.h"
#include "ui/controls/waveformwidget.h"
#include "ui/pages/previewrouter.h"
#include "io/assetmanager.h"
#include "io/materialreader.h"
#include "services/thumbnailgenerator.h"

#include "data/guidmanager.h"
#include "services/thumbnailmanager.h"
#include "io/assetmanager.h"
#include "io/scenewriter.h"

#include "ui/dialogs/toast.h"
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

void AssetView::copyTextures(const QString &folderGuid)
{
    const QString relativePath = "Textures";
    const aiScene *scene = viewer->sceneSource()->importer.GetScene();

    QStringList texturesToCopy;

    for (int i = 0; i < scene->mNumMeshes; i++) {
        auto mesh = scene->mMeshes[i];
        auto material = scene->mMaterials[mesh->mMaterialIndex];

        aiString textureName;

        if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            material->GetTexture(aiTextureType_DIFFUSE, 0, &textureName);
            texturesToCopy.append(textureName.C_Str());
        }

		if (material->GetTextureCount(aiTextureType_SPECULAR) > 0) {
			material->GetTexture(aiTextureType_SPECULAR, 0, &textureName);
			texturesToCopy.append(textureName.C_Str());
		}

		if (material->GetTextureCount(aiTextureType_NORMALS) > 0) {
			material->GetTexture(aiTextureType_NORMALS, 0, &textureName);
			texturesToCopy.append(textureName.C_Str());
		}

		if (material->GetTextureCount(aiTextureType_HEIGHT) > 0) {
			material->GetTexture(aiTextureType_HEIGHT, 0, &textureName);
			texturesToCopy.append(textureName.C_Str());
		}
    }

    if (!texturesToCopy.isEmpty()) {
        QString assetPath = AssetStorePaths::legacyFolder(folderGuid);

        for (auto texture : texturesToCopy) {
			QString tex = QFileInfo(texture).isRelative()
									? QDir::cleanPath(QDir(QFileInfo(filename).absoluteDir()).filePath(texture))
									: QDir::cleanPath(texture);
            QFile::copy(tex, QDir(assetPath).filePath(QFileInfo(texture).fileName()));
        }
    }
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
    mediaPlayer = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);
    mediaPlayer->setAudioOutput(audioOutput);

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

    const auto formatTime = [](qint64 ms) {
        const qint64 secs = ms / 1000;
        return QStringLiteral("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QChar('0'));
    };

    connect(audioPlayButton, &QPushButton::clicked, this, [this]() {
        if (mediaPlayer->playbackState() == QMediaPlayer::PlayingState) mediaPlayer->pause();
        else mediaPlayer->play();
    });
    connect(mediaPlayer, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
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
    connect(audioSeekSlider, &QSlider::sliderMoved, this, [this](int position) {
        mediaPlayer->setPosition(position);
    });

    // Waveform ↔ player wiring (§2): playhead follows playback, clicks seek.
    connect(mediaPlayer, &QMediaPlayer::durationChanged, waveform, &WaveformWidget::setDuration);
    connect(mediaPlayer, &QMediaPlayer::positionChanged, waveform, &WaveformWidget::setPosition);
    connect(waveform, &WaveformWidget::seekRequested, this, [this](qint64 ms) {
        mediaPlayer->setPosition(ms);
    });

    // Video page (PreviewPage::Video) — ASSET_MEDIA_SPEC §2.
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

    progressDialog = new ProgressDialog;
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

	auto views = new QWidget;
	auto viewsL = new QVBoxLayout;
	// The tile area runs flush to its pane edges (owner direction): no
	// inset below the filter/search row, no frame around the scroll area.
	viewsL->setContentsMargins(0, 0, 0, 0);
	viewsL->setSpacing(0);
	viewsL->addWidget(emptyGrid);
	viewsL->addWidget(fastGrid);
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
			fastGrid->setVisible(true);
		}
		else {
			filterPane->setVisible(false);
			emptyGrid->setVisible(true);
			fastGrid->setVisible(false);

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

    normalize = new QPushButton("Normalize");

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
		for (QPushButton *chromeBtn : { browseButton, downloadWorld, normalize,
		                                deleteFromLibrary })
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

            auto assetPath = AssetStorePaths::legacyFolder(gridItem->metadata["guid"].toString());

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

            // Store-file path for the media pages (the row's primary file).
            const QString storeFile =
                AssetStorePaths::legacyFilePath(guid, db->fetchAsset(guid).name);

            switch (page) {
            case PreviewPage::Viewer3D: {
                if (type == ModelTypes::Object || type == ModelTypes::ParticleSystem) {
                    QString path;
                    QDir dir(assetPath);
                    foreach(auto &file, dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files)) {
                        if (Constants::MODEL_EXTS.contains(file.suffix())) {
                            path = file.absoluteFilePath();
                            break;
                        }
                    }
                    if (viewer->cachedAsset(guid))
                        viewer->addNodeToScene(viewer->cachedAsset(guid), guid, true, false);
                    else
                        viewer->loadJafModel(path, guid, false, true, !cached);
                    viewer->orientCamera(pos, rot, distObj);
                }
                else if (type == ModelTypes::Material) {
                    viewer->loadJafMaterial(guid);
                    viewer->orientCamera(pos, rot, distObj);
                }
                else if (type == ModelTypes::Shader) {
                    QMap<QString, QString> map;
                    viewer->loadJafShader(guid, map);
                    viewer->orientCamera(pos, rot, distObj);
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
		QJsonObject tags;
		QJsonArray actualTags;

		// parse tags
		QString stringIn = tagModelField->text();
		if (!stringIn.isEmpty()) {
			std::vector<QString> commaSeparated(1);
			int commaCounter = 0;
			for (int i = 0; i<stringIn.size(); i++) {
                if (stringIn[i] == ',') {
					commaSeparated.push_back("");
					commaCounter++;
				}
				else {
					commaSeparated.at(commaCounter) += stringIn[i];
				}
			}

			for (const QString &tag : commaSeparated) {
				if (!tag.isEmpty()) actualTags.append(tag);
			}

			tags["tags"] = actualTags;
		}

		QJsonDocument tagsDoc(tags);

		auto ext = QFileInfo(selectedGridItem->metadata["full_filename"].toString()).suffix();

		db->updateAssetMetadata(
			selectedGridItem->metadata["guid"].toString(),
			renameModelField->text(),
            tagsDoc.toJson()
		);

		auto metadata = selectedGridItem->metadata;
		metadata["name"] = renameModelField->text();
		metadata["full_filename"] = IrisUtils::buildFileName(renameModelField->text(), QFileInfo(selectedGridItem->metadata["full_filename"].toString()).suffix());

		selectedGridItem->updateMetadata(metadata, tags);
		fetchMetadata(selectedGridItem);
	});

	connect(addToProject, &QPushButton::pressed, [this]() {
		if (selectedGridItem && !selectedGridItem->metadata.isEmpty())
			addAssetItemToProject(selectedGridItem);
	});

	connect(deleteFromLibrary, &QPushButton::pressed, [this]() {
		removeAssetFromProject(selectedGridItem);
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
	// ll->addWidget(normalize);
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
    viewersWidget->setLayout(viewers);

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
    _splitter->setStretchFactor(2, 1);
    // Qlementine's larger control minimums inflated the metadata pane to a
    // huge share of the page; keep it a sensible sidebar band (wide enough
    // for its buttons to breathe, never dominating the grid).
    _metadataPane->setMinimumWidth(280);
    _metadataPane->setMaximumWidth(380);
    
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

void AssetView::importJahModel(const QString &fileName, bool addToLibrary)
{
    QFileInfo entryInfo(fileName);

    auto assetPath = AssetStorePaths::root();

    // create a temporary directory and extract our project into it
    // we need a sure way to get the project name, so we have to extract it first and check the blob
    QTemporaryDir temporaryDir;
    if (temporaryDir.isValid()) {
        zip_extract(entryInfo.absoluteFilePath().toStdString().c_str(),
            temporaryDir.path().toStdString().c_str(),
            Q_NULLPTR, Q_NULLPTR
        );

        bool supported = true;
        QFile f(QDir(temporaryDir.path()).filePath(".manifest"));
        if (!f.exists()) {
            supported = false;
        }

        if (!db->checkIfJafModelVersionSupported(QDir(temporaryDir.path()).filePath("asset.db"))) {
            supported = false;
        }

        if (!supported) {
            QMessageBox::warning(
                this,
                "Incompatible Asset format",
                "This asset was made with a deprecated version of Jahshaka\n"
                "You can extract the contents manually and try importing as regular assets.",
                QMessageBox::Ok
                );

            return;
        }

        if (!f.open(QFile::ReadOnly | QFile::Text)) return;
        QTextStream in(&f);
        const QString jafString = in.readLine();
        f.close();

        ModelTypes jafType = ModelTypes::Undefined;

        if (jafString == "bundle") {
            importJahBundle(fileName);
            return;
        }

        if (jafString == "object") {
            jafType = ModelTypes::Object;
        }
        else if (jafString == "texture") {
            jafType = ModelTypes::Texture;
        }
        else if (jafString == "material") {
            jafType = ModelTypes::Material;
        }
        else if (jafString == "shader") {
            jafType = ModelTypes::Shader;
        }
		else if (jafString == "sky") {
			jafType = ModelTypes::Sky;
		}
        else if (jafString == "particle_system") {
            jafType = ModelTypes::ParticleSystem;
        }

        QVector<AssetRecord> records;

        QMap<QString, QString> guidCompareMap;
        QString guid = db->importAsset(jafType,
                            QDir(temporaryDir.path()).filePath("asset.db"),
                            QMap<QString, QString>(),
                            guidCompareMap,
                            records,
							AssetViewFilter::AssetsView,
							project->getProjectGuid());

        const QString assetFolder = QDir(assetPath).filePath(guid);
        QDir().mkpath(assetFolder);

        QString assetsDir = QDir(temporaryDir.path()).filePath("assets");
        QDirIterator projectDirIterator(assetsDir, QDir::NoDotAndDotDot | QDir::Files);

        QStringList fileNames;
        while (projectDirIterator.hasNext()) fileNames << projectDirIterator.next();

        jafType = ModelTypes::Undefined;

        QString placeHolderGuid = GUIDManager::generateGUID();

        for (const auto &file : fileNames) {
            QFileInfo fileInfo(file);
            QString fileToCopyTo = IrisUtils::join(assetFolder, fileInfo.fileName());
            bool copyFile = QFile::copy(fileInfo.absoluteFilePath(), fileToCopyTo);
        }

		if (addToLibrary) {
			if (jafString == "material") {
				viewers->setCurrentIndex(0);
				renameModelField->setText(QFileInfo(filename).baseName());
				viewer->loadJafMaterial(guid);
				addToJahLibrary(filename, guid, true);
			}

			if (jafString == "shader") {
				viewers->setCurrentIndex(0);
				renameModelField->setText(QFileInfo(filename).baseName());
				viewer->loadJafShader(guid, guidCompareMap);
				addToJahLibrary(filename, guid, true);
			}

			if (jafString == "sky") {
				viewers->setCurrentIndex(0);
				renameModelField->setText(QFileInfo(filename).baseName());
				viewer->loadJafSky(guid);
				addToJahLibrary(filename, guid, true);
			}

			if (jafString == "texture") {
				renameModelField->setText(QFileInfo(filename).baseName());

				{
					viewers->setCurrentIndex(1);
					auto assetPath = AssetStorePaths::legacyFilePath(guid, db->fetchAsset(guid).name);

					QPixmap image(assetPath);
					assetImageCanvas->setPixmap(image.scaledToHeight(480, Qt::SmoothTransformation));
				}

				addToJahLibrary(filename, guid, true);
			}

			if (jafString == "object") {
				viewers->setCurrentIndex(0);
				// Open the asset
				QString path;
				// if model
				QDir dir(assetFolder);
				foreach(auto &file, dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files)) {
					if (Constants::MODEL_EXTS.contains(file.suffix())) {
						path = file.absoluteFilePath();
						break;
					}
				}

				renameModelField->setText(QFileInfo(filename).baseName());
				viewer->loadJafModel(path, guid);
				addToJahLibrary(filename, guid, true);
			}
		}
    }
}

void AssetView::importJahBundle(const QString &fileName)
{
    QFileInfo entryInfo(fileName);

    auto assetPath = AssetStorePaths::root();

    // create a temporary directory and extract our project into it
    // we need a sure way to get the project name, so we have to extract it first and check the blob
    QTemporaryDir temporaryDir;
    temporaryDir.setAutoRemove(false);
    if (temporaryDir.isValid()) {
        zip_extract(entryInfo.absoluteFilePath().toStdString().c_str(),
            temporaryDir.path().toStdString().c_str(),
            Q_NULLPTR, Q_NULLPTR
        );

        QFile f(QDir(temporaryDir.path()).filePath(".manifest"));

        if (!f.exists()) {
            QMessageBox::warning(
                this,
                "Incompatible Asset format",
                "This asset was made with a deprecated version of Jahshaka\n"
                "You can extract the contents manually and try importing as regular assets.",
                QMessageBox::Ok
            );

            return;
        }

        QStringList lines;
        if (!f.open(QFile::ReadOnly | QFile::Text)) return;
        QTextStream in(&f);

        while (!in.atEnd()) {
            QString line = in.readLine();
            lines << line;
        }
        f.close();

        const QString jafString = lines.first();
        lines.pop_front();

        QVector<AssetRecord> records;

        QMap<QString, QString> guidCompareMap;
        QString guid = db->importAssetBundle(
            QDir(temporaryDir.path()).filePath("asset.db"),
            QMap<QString, QString>(),
            guidCompareMap,
            records,
            project->getProjectGuid()
        );

        QMap<QString, QString> guidsToReplace;

        QMap<QString, QString>::const_iterator ptIter;
        for (ptIter = guidCompareMap.constBegin(); ptIter != guidCompareMap.constEnd(); ++ptIter) {
            if (lines.contains(ptIter.key())) {
                guidsToReplace.insert(ptIter.key(), ptIter.value());
            }
        }

        for (ptIter = guidsToReplace.constBegin(); ptIter != guidsToReplace.constEnd(); ++ptIter) {
            QString assetsDir = QDir(QDir(temporaryDir.path()).filePath("assets")).filePath(ptIter.key());
            QDirIterator projectDirIterator(assetsDir, QDir::NoDotAndDotDot | QDir::Files | QDir::Hidden);

            QStringList fileNames;
            while (projectDirIterator.hasNext()) fileNames << projectDirIterator.next();

            const QString assetFolder = QDir(assetPath).filePath(ptIter.value());
            QDir().mkpath(assetFolder);

            for (const auto &file : fileNames) {
                QFileInfo fileInfo(file);
                QString fileToCopyTo = IrisUtils::join(assetFolder, fileInfo.fileName());
                bool copyFile = QFile::copy(fileInfo.absoluteFilePath(), fileToCopyTo);
            }
        }
    }
}

void AssetView::importModel(const QString &fileName, bool jfx)
{
    if (fileName.isEmpty()) {
        return;
    }

    // The grid tile and metadata pane read the `filename` member, which only
    // the browse dialog used to set — a drag-and-dropped model got a nameless
    // tile until restart (ASSETS_AUDIT.md finding 2). Every import path lands
    // here, so set it here.
    filename = fileName;

    QApplication::processEvents();

    QFileInfo entryInfo(fileName);

    auto assetPath = AssetStorePaths::root();

    QString main_guid = GUIDManager::generateGUID();

    QString assetsDir = entryInfo.path();

    const QString assetFolder = QDir(assetPath).filePath(main_guid);
    QDir().mkpath(assetFolder);


    QSet<QString> visitedFiles;
    QSet<QString> visitedDirs;

    std::function<void(const QString&, const QString&, QList<directory_tuple>&)> getImportManifest =
        [&](const QString &filePath, const QString &guid, QList<directory_tuple> &items)
    {
        QFileInfo fileInfo(filePath);
        QString rootDirPath = fileInfo.absoluteDir().absolutePath();

        if (visitedDirs.contains(rootDirPath))
            return;

        visitedDirs.insert(rootDirPath);

        std::function<void(const QString&, const QString&)> recurseDir;
        recurseDir = [&](const QString &dirPath, const QString &parentGuid)
        {
            QDir dir(dirPath);

            QFileInfoList entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);

            for (const QFileInfo &entry : entries) {
                if (entry.isSymLink())
                    continue;

                if (entry.isDir()) {
                    recurseDir(entry.absoluteFilePath(), parentGuid);
                } else {
                    QString fileAbsPath = entry.absoluteFilePath();
                    if (visitedFiles.contains(fileAbsPath))
                        continue;

                    directory_tuple fileItem;
                    fileItem.path = fileAbsPath;
                    fileItem.guid = GUIDManager::generateGUID();
                    fileItem.parent_guid = parentGuid;
                    items.append(fileItem);

                    visitedFiles.insert(fileAbsPath);
                }
            }
        };

        recurseDir(rootDirPath, guid);
    };


    QList<directory_tuple> fileNameList;

    if (fileName.endsWith(".obj")) {
           getImportManifest(fileName, main_guid, fileNameList);
    } else {
        directory_tuple fileItem;
        fileItem.path = fileName;
        fileItem.guid = GUIDManager::generateGUID();
        fileItem.parent_guid = main_guid;
        fileNameList.append(fileItem);
    }


    // Sort filenames by type, we want to import and create db entries in dependent order
    // 0. Folders
    // 1. Textures
    // 2. Materials
    // 3. Shaders
    // 4. Meshes
    // 5. Skies
    // 6. Music
    // 7. Asset files (these contain their own assets)
    // 8. Regular files

    // Path, GUID, Parent
    QList<directory_tuple> finalImportList;
    QList<directory_tuple> finalJafAssetImportList;

    for (const auto &folder : fileNameList) {
        if (QFileInfo(folder.path).isDir()) {
            finalImportList.append(folder);
        }
    }

    for (const auto &image : fileNameList) {
        if (Constants::IMAGE_EXTS.contains(QFileInfo(image.path).suffix().toLower())) {
            finalImportList.append(image);
        }
    }

    for (const auto &material : fileNameList) {
        if (Constants::MATERIAL_EXTS.contains(QFileInfo(material.path).suffix())) {
            finalImportList.append(material);
        }
    }

    for (const auto &file : fileNameList) {
        if (Constants::WHITELIST.contains(QFileInfo(file.path).suffix().toLower())) {
            finalImportList.append(file);
        }
    }

    for (const auto& music : fileNameList) {
        if (Constants::AUDIO_EXTS.contains(QFileInfo(music.path).suffix().toLower())) {
            finalImportList.append(music);
        }
    }

    for (const auto &shader : fileNameList) {
        if (QFileInfo(shader.path).suffix() == Constants::SHADER_EXT) {
            finalImportList.append(shader);
        }
    }

    for (const auto &mesh : fileNameList) {
        if (Constants::MODEL_EXTS.contains(QFileInfo(mesh.path).suffix().toLower())) {
            finalImportList.append(mesh);
        }
    }

    for (const auto &archive : fileNameList) {
        if (QFileInfo(archive.path).suffix() == Constants::ASSET_EXT) {
            finalJafAssetImportList.append(archive);
        }
    }

    int counter = 0;
    // If we're loading a single asset, it's likely a single large file, make the progress indeterminate
    int maxRange = finalImportList.size() == 1 ? 0 : finalImportList.size();

    progressDialog->setRange(0, maxRange);
    progressDialog->setValue(0);
    progressDialog->show();

    QList<directory_tuple> imagesInUse;
    QList<QString> imgaesUsedList;
    QString meshImportError;
    QJsonObject mainModelStats;   // counts from the assimp scene of the main mesh

    foreach(const auto &entry, finalImportList) {
        QFileInfo entryInfo(entry.path);

        if (entryInfo.isDir()) {
            db->createFolder(entryInfo.baseName(), entry.parent_guid, entry.guid, project->getProjectGuid());
        }
        else {
            ModelTypes type;
            QPixmap thumbnail = QPixmap(":/icons/empty_object.png");

            auto asset = new AssetVariant;
            asset->type		 = AssetHelper::getAssetTypeFromExtension(entryInfo.suffix().toLower());
            asset->fileName  = entryInfo.fileName();
            asset->path		 = entry.path;
            asset->thumbnail = thumbnail;

            if (asset->type != ModelTypes::Undefined) {
                //Copy only models, textures and whitelisted files

                QString fileToCopyTo = IrisUtils::join(assetFolder, asset->fileName);
                bool copyFile = QFile::copy(entry.path, fileToCopyTo);
                progressDialog->setLabelText("Copying " + asset->fileName);
                progressDialog->setValue(counter++);
                asset->path = fileToCopyTo;

                if (asset->type == ModelTypes::Texture) {
                    auto thumb = ThumbnailManager::createThumbnail(entryInfo.absoluteFilePath(), 72, 72);
                    thumbnail = QPixmap::fromImage(*thumb->thumb);

                    directory_tuple dt;
                    dt.parent_guid = entry.parent_guid;
                    dt.guid = entry.guid;
                    dt.path = entryInfo.fileName();
                    imgaesUsedList.append(dt.path);
                    imagesInUse.append(dt);
                }


                const QString assetGuid = db->createAssetEntry(entry.guid,
                                                               asset->fileName,
                                                               static_cast<int>(asset->type),
                                                               entry.parent_guid,
                                                               project->getProjectGuid(),
                                                               QString(),
                                                               QString(),
                                                               AssetHelper::makeBlobFromPixmap(thumbnail));

                if (asset->type == ModelTypes::File) {
                    auto assetFile = new AssetFile;
                    assetFile->assetGuid = assetGuid;
                    assetFile->fileName = asset->fileName;
                    assetFile->path = assetGuid;
                    AssetManager::addAsset(assetFile);
                }

                if (asset->type == ModelTypes::Music) {
                    auto assetMusic = new AssetMusic;
                    assetMusic->assetGuid = assetGuid;
                    assetMusic->fileName = asset->fileName;
                    assetMusic->path = assetGuid;
                    AssetManager::addAsset(assetMusic);
                }

                if (asset->type == ModelTypes::Texture) {
                    auto assetTexture = new AssetTexture;
                    assetTexture->assetGuid = assetGuid;
                    assetTexture->fileName = asset->fileName;
                    assetTexture->path = assetGuid;
                    AssetManager::addAsset(assetTexture);
                }

                if (asset->type == ModelTypes::Shader) {
                    QFile *shaderFile = new QFile(asset->path);
                    shaderFile->open(QIODevice::ReadOnly | QIODevice::Text);
                    QJsonObject shaderDefinition = QJsonDocument::fromJson(shaderFile->readAll()).object();
                    shaderFile->close();

                    shaderDefinition["name"] = QFileInfo(asset->fileName).baseName();
                    shaderDefinition["guid"] = assetGuid;

                    db->updateAssetAsset(assetGuid, QJsonDocument(shaderDefinition).toJson());

                    auto assetShader = new AssetShader;
                    assetShader->assetGuid = assetGuid;
                    assetShader->fileName = QFileInfo(asset->fileName).baseName();
                    assetShader->setValue(QVariant::fromValue(shaderDefinition));
                    AssetManager::addAsset(assetShader);
                }

                if (asset->type == ModelTypes::Material) {
                    ThumbnailGenerator::getSingleton()->requestThumbnail(
                        ThumbnailRequestType::Material, asset->path, assetGuid
                        );

                    QJsonObject jsonMaterial;
                    QStringList texturesToCopy;
                    extractTexturesAndMaterialFromMaterial(asset->path, texturesToCopy, jsonMaterial);

                    QString jsonMaterialString = QJsonDocument(jsonMaterial).toJson();

                    // Update the embedded material to point to image asset guids
                    for (const auto &image : imagesInUse) {
                        if (texturesToCopy.contains(QFileInfo(image.path).fileName())) {
                            jsonMaterialString.replace(QFileInfo(image.path).fileName(), image.guid);
                        }
                    }

                    QJsonDocument jsonMaterialGuids = QJsonDocument::fromJson(jsonMaterialString.toUtf8());
                    db->updateAssetAsset(assetGuid, jsonMaterialGuids.toJson());

                    // Create dependencies to the object for the textures used
                    for (const auto &image : imagesInUse) {
                        if (texturesToCopy.contains(QFileInfo(image.path).fileName())) {
                            db->createDependency(
                                static_cast<int>(ModelTypes::Material), static_cast<int>(ModelTypes::Texture),
                                assetGuid, image.guid, main_guid
                                );
                        }
                    }

                    QJsonDocument matDoc = QJsonDocument::fromJson(db->fetchAssetData(assetGuid));
                    QJsonObject matObject = matDoc.object();
                    iris::CustomMaterialPtr material = iris::CustomMaterialPtr::create();
                    material->generate(IrisUtils::join(
                        IrisUtils::getAbsoluteAssetPath(Constants::SHADER_DEFS),
                        IrisUtils::buildFileName(matObject.value("name").toString(), "shader"))
                                       );

                    for (const auto &prop : material->properties) {
                        if (prop->type == iris::PropertyType::Color) {
                            QColor col;
                            col.setNamedColor(matObject.value(prop->name).toString());
                            material->setValue(prop->name, col);
                        }
                        else if (prop->type == iris::PropertyType::Texture) {
                            QString materialName = db->fetchAsset(matObject.value(prop->name).toString()).name;
                            QString textureStr = IrisUtils::join(assetFolder, materialName);
                            material->setValue(prop->name, !materialName.isEmpty() ? textureStr : QString());
                        }
                        else {
                            material->setValue(prop->name, QVariant::fromValue(matObject.value(prop->name)));
                        }
                    }

                    auto assetMat = new AssetMaterial;
                    assetMat->assetGuid = assetGuid;
                    assetMat->setValue(QVariant::fromValue(material));
                    AssetManager::addAsset(assetMat);
                }

                if (asset->type == ModelTypes::Mesh) {
                    QStringList texturesToCopy;

                    bool hasEmbeddedTexture(false);
                    QStringList paths;
                    auto scene = AssetHelper::extractTexturesAndMaterialFromMesh(asset->path,
                                                                                 texturesToCopy,
                                                                                 paths,
                                                                                 hasEmbeddedTexture,
                                                                                 &mainModelStats);


                    if (!scene) {
                        // assimp could not import the model (e.g. a Draco-compressed
                        // glb: ASSIMP_BUILD_DRACO is off) — fail with a message
                        // instead of dereferencing a null hierarchy below.
                        meshImportError = tr("\"%1\" could not be imported.\n"
                                             "The file may be corrupt or use an unsupported "
                                             "feature (for example Draco mesh compression in "
                                             "a .glb/.gltf).").arg(asset->fileName);
                        db->deleteAsset(assetGuid);   // drop the Mesh row created above
                        continue;
                    }

                    if (hasEmbeddedTexture) {
                        for (const auto &image : imgaesUsedList) {
                            int index = texturesToCopy.indexOf(image);
                            if (index >= 0) {
                                texturesToCopy.remove(index);
                                paths.remove(index);
                            }
                        }

                        int index = 0;
                        for (const auto &image : texturesToCopy) {
                            directory_tuple dt;
                            dt.parent_guid = main_guid;
                            dt.guid = GUIDManager::generateGUID();
                            dt.path = image;
                            imagesInUse.append(dt);

                            auto thumb = ThumbnailManager::createThumbnail(paths[index], 72, 72);
                            thumbnail = QPixmap::fromImage(*thumb->thumb);
                            index++;



                            const QString assetGuid = db->createAssetEntry(dt.guid,
                                                                           dt.path,
                                                                           static_cast<int>(ModelTypes::Texture),
                                                                           main_guid,
                                                                           project->getProjectGuid(),
                                                                           QString(),
                                                                           QString(),
                                                                           AssetHelper::makeBlobFromPixmap(thumbnail));
                        }
                    }

                    // Replace all path references with GUIDs before storing in the database
                    std::function<void(iris::SceneNodePtr&)> replacePathsWithGUIDs =
                        [&](iris::SceneNodePtr &node) -> void {
                        if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
                            auto meshNode = node.staticCast<iris::MeshNode>();
                            if (QFileInfo(meshNode->meshPath).fileName() == entryInfo.fileName()) {
                                meshNode->meshPath = assetGuid;
                            }

                            meshNode->setGUID(main_guid);

                            auto material = meshNode->getMaterial().staticCast<iris::CustomMaterial>();
                            for (auto prop : material->properties) {
                                if (prop->type == iris::PropertyType::Texture) {
                                    // Cycle through any textures that were selected in the import and use them
                                    for (const auto &image : imagesInUse) {
                                        auto fileName = QFileInfo(prop->getValue().toString()).fileName();
                                        if (texturesToCopy.contains(fileName) && image.path == fileName) {
                                            material->setValue(prop->name, image.guid);
                                        }
                                    }
                                }
                            }
                        }

                        if (node->hasChildren()) {
                            for (auto &child : node->children) {
                                replacePathsWithGUIDs(child);
                            }
                        }
                    };

                    replacePathsWithGUIDs(scene);

                    QJsonObject nodeWithGUIDs;
                    SceneWriter::writeSceneNode(nodeWithGUIDs, scene, false);


                    QJsonObject object;
                    object["icon_url"] = "";
                    object["name"] = QFileInfo(filename).baseName(); // renameModelField->text();

                    auto assetSnapshot = viewer->takeScreenshot(512, 512);

                    QJsonObject tags;
                    QJsonArray actualTags;
                    QJsonDocument tagsDoc(tags);

                    // Create an actual object from a mesh, materials are embedded into objects by default
                    const QString objectGuid = db->createAssetEntry(main_guid,
                                                                    QFileInfo(asset->fileName).baseName(),
                                                                    static_cast<int>(ModelTypes::Object),
                                                                    QString(),
                                                                    project->getProjectGuid(),
                                                                    QString(),
                                                                    QString(),
                                                                    AssetHelper::makeBlobFromPixmap(QPixmap::fromImage(assetSnapshot)),
                                                                    QJsonDocument(viewer->getSceneProperties()).toJson(),
                                                                    tagsDoc.toJson(),
                                                                    QJsonDocument(nodeWithGUIDs).toJson(),
                                                                    AssetViewFilter::AssetsView);

                    // No async thumbnail request here: the queued render used to land
                    // AFTER the post-load screenshot below and overwrite it (with a
                    // grey, texture-less image at that). The viewer screenshot at the
                    // end of this import IS the thumbnail.

                    QVariant variant = QVariant::fromValue(scene);
                    auto nodeAsset = new AssetNodeObject;
                    nodeAsset->assetGuid = objectGuid;
                    nodeAsset->setValue(variant);
                    AssetManager::addAsset(nodeAsset);


                    // Create dependencies to the object for the textures used
                    for (const auto &image : imagesInUse) {
                        if (texturesToCopy.contains(QFileInfo(image.path).fileName())) {
                            db->createDependency(
                                static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Texture),
                                objectGuid, image.guid
                                );
                        }
                    }

                    // Insert a dependency for the mesh to the object
                    db->createDependency(
                        static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh),
                        objectGuid, assetGuid
                        );

                    // Remove the thumbnail from the object asset
                    db->updateAssetAsset(assetGuid, QByteArray());
                }
            }
        }
    }

    progressDialog->hide();

    if (!meshImportError.isEmpty()) {
        // Nothing was imported for this model: no preview, no tile.
        QMessageBox::warning(this, tr("Import failed"), meshImportError);
        return;
    }

    renameModelField->setText(QFileInfo(fileName).baseName());
    QString new_file = IrisUtils::join(assetFolder, QFileInfo(fileName).fileName());
    viewer->loadModel(new_file, main_guid);

    // The Object row's thumbnail and camera properties were captured before
    // the model finished loading (ASSETS_AUDIT.md finding 5: soulless stored
    // the default camera, lotus stored soulless's) — refresh them now that the
    // viewer actually shows the imported model.
    auto loadedSnapshot = viewer->takeScreenshot(512, 512);
    if (!loadedSnapshot.isNull())
        db->updateAssetThumbnail(main_guid, AssetHelper::makeBlobFromPixmap(QPixmap::fromImage(loadedSnapshot)));
    // Camera props from the viewer + the "metadata" block counted from the
    // assimp scene during this very import (ASSET_DRAWERS_SPEC addendum).
    QJsonObject objectProperties = viewer->getSceneProperties();
    if (!mainModelStats.isEmpty()) objectProperties["metadata"] = mainModelStats;
    db->updateAssetProperties(main_guid, QJsonDocument(objectProperties).toJson());

    addToLibrary(main_guid, jfx);
}

// THE import dispatch (ASSET_DRAWERS_SPEC §3): drop pad and browse dialog both
// land here; one switch keyed on ModelTypes decides each file's path, so a new
// library type (Video, …) is one case — here and in AssetImporter::importFile.
void AssetView::importFiles(const QStringList &fileNames)
{
	for (const auto &fileName : fileNames) {
		if (fileName.isEmpty()) continue;

		if (QFileInfo(fileName).suffix().toLower() == Constants::ASSET_EXT) {
			importJahModel(fileName);
			continue;
		}

		const ModelTypes type =
		    AssetHelper::getAssetTypeFromExtension(QFileInfo(fileName).suffix().toLower());
		switch (type) {
		case ModelTypes::Texture:
		case ModelTypes::Music:
		case ModelTypes::Video:
			importImageOrAudio(fileName);
			break;
		default:
			// Meshes and everything they reference: the viewer-driven path.
			importModel(fileName);
			break;
		}
	}
}

int AssetView::selectedDrawerId() const
{
	const int id = treeWidget->currentItem()
	    ? treeWidget->currentItem()->data(0, Qt::UserRole).toInt() : -1;
	return id > 0 ? id : 0;   // root/none selected -> Uncategorized
}

void AssetView::importImageOrAudio(const QString &fileName)
{
	// The same service the assets.importFile verb runs (§4: UI calls the
	// verb's path) — the row, the store copy and the thumbnail happen there.
	const auto result = AssetImporter::importFile(fileName, db, project, selectedDrawerId());
	if (!result.ok()) {
		QMessageBox::warning(this, tr("Import failed"), result.error);
		return;
	}

	const auto record = db->fetchAsset(result.objectGuid);

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
	QApplication::processEvents();
	fastGrid->updateGridColumns(fastGrid->lastWidth);
	filterFromSelection();
}

void AssetView::stopMediaPreviews()
{
	if (mediaPlayer) mediaPlayer->stop();
	if (assetVideoViewer) assetVideoViewer->stop();
}

void AssetView::showAudioPreview(const QString &guid, const QString &filePath,
                                 const QString &displayName)
{
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
    auto shaderName = Constants::SHADER_DEFS + material_name + ".shader";
    if (material_name.isEmpty()) {
        shaderName ="app/shader_defs/Default.shader";
        material_name = "Default";
    }

    auto material = iris::CustomMaterial::create();
    material->generate(IrisUtils::getAbsoluteAssetPath(shaderName));
    material->setName(material_name);

    for (const auto &prop : material->properties) {
        if (materialDefinition.contains(prop->name)) {
            if (prop->type == iris::PropertyType::Texture) {
                auto textureStr = !materialDefinition[prop->name].toString().isEmpty()
                ? materialDefinition[prop->name].toString()
                : QString();
                material->setValue(prop->name, textureStr);
                if (!textureStr.isEmpty()) {
                    textureList.append(QFileInfo(textureStr).fileName());
                }
            }
            else {
                material->setValue(prop->name, materialDefinition[prop->name].toVariant());
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
	//auto rx = _navPane->rect().x() + viewer->rect().x();
	//auto ry = _navPane->rect().y() + viewer->rect().y();
	//auto rw = _navPane->rect().width() + viewer->rect().width();
	//auto rh = viewer->rect().height() + 32;

	//auto endRect = QRect(parent->pos().x(), parent->pos().y(), rw, rh);
	Toast *t = new Toast(this);
	t->showToast(
		"Asset Added To Project",
		QString("%1 has been added successfully to the open project.").arg(item->metadata["name"].toString()),
		0, parent->pos(), QRect()
	);

	// get the current project working directory
	auto pFldr = IrisUtils::join(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), Constants::PROJECT_FOLDER);
	auto defaultProjectDirectory = settings->getValue("default_directory", pFldr).toString();
	auto pDir = IrisUtils::join(defaultProjectDirectory, project->getProjectGuid());

	QString guid = item->metadata["guid"].toString();
	int assetType = item->metadata["type"].toInt();

    auto assetsDir = AssetStorePaths::legacyFolder(guid);

    const int aType = db->fetchAsset(guid).type;

    QDirIterator projectDirIterator(assetsDir, QDir::NoDotAndDotDot | QDir::Files);

    QStringList fileNames;
    while (projectDirIterator.hasNext()) fileNames << projectDirIterator.next();

    // Create a pair that holds the original name and the new name (if any)
    QVector<QPair<QString, QString>> files;	/* original x new */

    ModelTypes jafType = ModelTypes::Undefined;

    QString placeHolderGuid = GUIDManager::generateGUID();

    for (const auto &file : fileNames) {
        QFileInfo fileInfo(file);
		ModelTypes jafType = AssetHelper::getAssetTypeFromExtension(fileInfo.suffix().toLower());

        QString pathToCopyTo = project->getProjectFolder();
        QString fileToCopyTo = IrisUtils::join(pathToCopyTo, fileInfo.fileName());

        int increment = 1;
        QFileInfo checkFile(fileToCopyTo);
        // If we encounter the same file, make a duplicate...
        QString newFileName = fileInfo.fileName();

        while (checkFile.exists()) {
            QString newName = fileInfo.baseName() + " " + QString::number(increment++);
            checkFile = QFileInfo(IrisUtils::buildFileName(
                IrisUtils::join(pathToCopyTo, newName), fileInfo.suffix())
            );
            newFileName = checkFile.fileName();
			fileToCopyTo = checkFile.absoluteFilePath();
        }

        files.push_back(QPair<QString, QString>(file, fileToCopyTo));
        
		bool copyFile = QFile::copy(file, fileToCopyTo);

		QFileInfo newFileInfo(fileToCopyTo);

        if (jafType == ModelTypes::File) {
            auto assetFile = new AssetFile;
            assetFile->fileName = newFileInfo.fileName();
            assetFile->assetGuid = placeHolderGuid;
            assetFile->path = fileToCopyTo;
            AssetManager::addAsset(assetFile);
        }

		if (jafType == ModelTypes::Texture) {
			auto assetTexture = new AssetTexture;
			assetTexture->fileName = newFileInfo.fileName();
			assetTexture->assetGuid = placeHolderGuid;
			assetTexture->path = fileToCopyTo;
			AssetManager::addAsset(assetTexture);
		}

		if (jafType == ModelTypes::Music) {
			// §3: imported audio becomes selectable in the World panel's
			// Background Ambience combo (it lists the project's Music assets).
			auto assetMusic = new AssetMusic;
			assetMusic->fileName = newFileInfo.fileName();
			assetMusic->assetGuid = placeHolderGuid;
			assetMusic->path = fileToCopyTo;
			AssetManager::addAsset(assetMusic);
		}

        if (jafType == ModelTypes::Mesh) {
            auto ssource = new iris::SceneSource();
            // load mesh as scene
            auto node = iris::MeshNode::loadAsSceneFragment(
				fileToCopyTo,
                [&](iris::MeshPtr mesh, iris::MeshMaterialData& data)
            {
                auto mat = iris::CustomMaterial::create();
                mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));

                return mat;
            }, ssource);

            QVariant variant = QVariant::fromValue(node);
            auto nodeAsset = new AssetNodeObject;
			nodeAsset->fileName = newFileInfo.fileName();
			nodeAsset->path = fileToCopyTo;
            nodeAsset->assetGuid = placeHolderGuid;	/* temp guid */
            nodeAsset->setValue(variant);
            AssetManager::addAsset(nodeAsset);
        }
    }

    QMap<QString, QString> newNames;	/* original x new */
    for (const auto &file : files) {
        newNames.insert(
            QFileInfo(file.first).fileName(),
            QFileInfo(file.second).fileName()
        );
    }

    // We can discern most types from their extension, we don't store material files so we use the manifest
    if (aType == static_cast<int>(ModelTypes::Material)) {
        jafType = ModelTypes::Material;
    }
    else if (aType == static_cast<int>(ModelTypes::Object)) {
        jafType = ModelTypes::Object;
    }
	else if (aType == static_cast<int>(ModelTypes::Texture)) {
		jafType = ModelTypes::Texture;
	}
	else if (aType == static_cast<int>(ModelTypes::Music)) {
		jafType = ModelTypes::Music;
	}
	else if (aType == static_cast<int>(ModelTypes::Video)) {
		// Copies like audio/textures (ASSET_MEDIA_SPEC §1, Music precedent).
		// No runtime AssetManager entry yet — scene playback is §3.
		jafType = ModelTypes::Video;
	}
    else if (aType == static_cast<int>(ModelTypes::Shader)) {
        jafType = ModelTypes::Shader;
    }
	else if (aType == static_cast<int>(ModelTypes::Sky)) {
		jafType = ModelTypes::Sky;
	}
    else if (aType == static_cast<int>(ModelTypes::ParticleSystem)) {
        jafType = ModelTypes::ParticleSystem;
    }
    else {
        // Default to files since we know what archives can contain
        jafType = ModelTypes::File;
    }

    QVector<AssetRecord> oldAssetRecords;

    QString guidReturned = db->copyAsset(
        jafType, guid, newNames,
        oldAssetRecords, project->getProjectGuid(),
		AssetViewFilter::Editor,
		project->getProjectGuid()
    );

    for (auto &asset : AssetManager::getAssets()) {
        if (asset->type == ModelTypes::File) {
            for (const auto &record : oldAssetRecords) {
                if (record.name == asset->fileName) {
                    asset->assetGuid = record.guid;
                }
            }
        }
    }

    if (jafType == ModelTypes::Texture || jafType == ModelTypes::Music) {
        for (auto &asset : AssetManager::getAssets()) {
            if (asset->assetGuid == placeHolderGuid && asset->type == jafType) {
                asset->assetGuid = guidReturned;
            }
        }
    }

    if (jafType == ModelTypes::Shader) {
        QJsonDocument matDoc = QJsonDocument::fromJson(db->fetchAssetData(guidReturned));
        QJsonObject shaderDefinition = matDoc.object();

        auto assetShader = new AssetShader;
        assetShader->assetGuid = guidReturned;
        assetShader->fileName = db->fetchAsset(guidReturned).name;
        assetShader->setValue(QVariant::fromValue(shaderDefinition));
        AssetManager::addAsset(assetShader);
    }
    else {
        for (const auto &asset : oldAssetRecords) {
            if (asset.type == static_cast<int>(ModelTypes::Shader)) {
                QJsonDocument matDoc = QJsonDocument::fromJson(asset.asset);
                QJsonObject shaderDefinition = matDoc.object();

                auto assetShader = new AssetShader;
                assetShader->assetGuid = asset.guid;
                assetShader->fileName = asset.name;
                assetShader->setValue(QVariant::fromValue(shaderDefinition));
                AssetManager::addAsset(assetShader);
            }
        }
    }

    if (jafType == ModelTypes::ParticleSystem) {
        QJsonDocument matDoc = QJsonDocument::fromJson(db->fetchAssetData(guidReturned));
        QJsonObject shaderDefinition = matDoc.object();

        auto assetShader = new AssetParticleSystem;
        assetShader->assetGuid = guidReturned;
        assetShader->fileName = db->fetchAsset(guidReturned).name;
        assetShader->setValue(QVariant::fromValue(shaderDefinition));
        AssetManager::addAsset(assetShader);
    }

    if (jafType == ModelTypes::Object) {
        for (auto &asset : AssetManager::getAssets()) {
            if (asset->assetGuid == placeHolderGuid && asset->type == ModelTypes::Object) {
                asset->assetGuid = guidReturned;
                auto node = asset->getValue().value<iris::SceneNodePtr>();
                auto material = db->fetchAssetData(guidReturned);
                auto materialObj = QJsonDocument::fromJson(material);
                AssetHelper::updateNodeMaterial(node, materialObj.object());
            }
        }
    }

    if (jafType == ModelTypes::Material) {
        QJsonDocument matDoc = QJsonDocument::fromJson(db->fetchAssetData(guidReturned));
        QJsonObject matObject = matDoc.object();

		MaterialReader reader;
		reader.setProject(project);
		iris::CustomMaterialPtr material = reader.parseMaterial(matObject, db);

        auto assetMat = new AssetMaterial;
        assetMat->assetGuid = guidReturned;
        assetMat->setValue(QVariant::fromValue(material));
        AssetManager::addAsset(assetMat);
    }
}

// Files an asset in a drawer — the tile drag-drop and the context menu's
// Move to ▸ both land here (ASSET_DRAWERS_SPEC §1; replaced the old Change
// Collections dialog).
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

	connect(gridItem, &AssetGridItem::removeAssetFromProject, [this](AssetGridItem *item) {
		removeAssetFromProject(item);
	});

	connect(gridItem, &AssetGridItem::rebuildThumbnail, [this](AssetGridItem *item) {
		rebuildTileThumbnail(item);
	});
}

void AssetView::rebuildTileThumbnail(AssetGridItem *item)
{
	if (!item || item->metadata.isEmpty()) return;
	const QString guid = item->metadata["guid"].toString();
	const auto record = db->fetchAsset(guid);
	if (record.guid.isEmpty()) return;

	const auto assetFolder = AssetStorePaths::legacyFolder(guid);

	QPixmap pixmap;
	switch (static_cast<ModelTypes>(record.type)) {
	case ModelTypes::Texture: {
		// straight from the source image, like the import path
		auto thumb = ThumbnailManager::createThumbnail(IrisUtils::join(assetFolder, record.name), 256, 256);
		if (thumb && thumb->thumb) pixmap = QPixmap::fromImage(*thumb->thumb);
		break;
	}
	case ModelTypes::Music:
		pixmap = QPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-music.png"));
		break;
	case ModelTypes::Video:
		// Re-grab the first-second frame (film icon when decode fails).
		pixmap = VideoUtils::thumbnailFor(IrisUtils::join(assetFolder, record.name));
		break;
	case ModelTypes::Object:
	case ModelTypes::ParticleSystem: {
		// the import-time flow: load into the asset viewer, screenshot it —
		// the same lit, textured render a fresh import stores today.
		QString path;
		for (const auto &file : QDir(assetFolder).entryInfoList(QDir::NoDotAndDotDot | QDir::Files)) {
			if (Constants::MODEL_EXTS.contains(file.suffix().toLower())) {
				path = file.absoluteFilePath();
				break;
			}
		}
		viewers->setCurrentIndex(0);
		viewer->loadJafModel(path, guid, false, true, false);
		const QImage shot = viewer->takeScreenshot(512, 512);
		if (!shot.isNull()) pixmap = QPixmap::fromImage(shot);
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

void AssetView::removeAssetFromProject(AssetGridItem *item)
{
    auto assetPath = AssetStorePaths::root();

	auto option = QMessageBox::question(this,
	    "Deleting Asset", "Are you sure you want to delete this asset?",
	    QMessageBox::Yes | QMessageBox::Cancel);

	if (option == QMessageBox::Yes) {
	    if (IrisUtils::removeDir(QDir(assetPath).filePath(item->metadata["guid"].toString()))) {
	        fastGrid->deleteTile(item);
			// if the item is being used soft delete it
			//db->deleteAsset(item->metadata["guid"].toString());
            db->deleteAssetAndDependencies(item->metadata["guid"].toString());

			item->metadata = QJsonObject();
			renameWidget->setVisible(false);
			tagWidget->setVisible(false);
			updateAsset->setVisible(false);

			if (selectedGridItem == item) selectedGridItem = nullptr;
			updateAddToProjectButton();
			deleteFromLibrary->setEnabled(false);

			fetchMetadata(item);
	        clearViewer();
	    }
	    else {
	        QMessageBox::warning(this, "Delete Failed!", "Failed to remove asset, please try again!", QMessageBox::Ok);
	    }
	}
}

AssetView::~AssetView()
{
    
}
