/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/assetwidget.h"

#include "ui/dialogs/importsettingsdialog.h"
#include "ui_assetwidget.h"

#include <iostream>
#include <QDate>
#include <QAbstractItemModel>
#include <QBuffer>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDrag>
#include "ui/panels/singledragowner.h"
#include <QJsonDocument>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QProgressDialog>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include "bridge/enginehost.h"
#include "services/thumbnailrebuild.h"
#include <QComboBox>

#include <algorithm>

#include "irisgl/core/irisutils.h"
#include "irisgl/document/scenegraph/particlesystemnode.h" 
#include "irisgl/document/scenegraph/scene.h" 
#include "zip.h"

#include "ui/pages/assetview.h"
#include "data/constants.h"

#include "shell/mainwindow.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "data/settingsmanager.h"
#include "services/thumbnailmanager.h"
#include "services/thumbnailgenerator.h"
#include "services/assethelper.h"
#include "services/assetstorepaths.h"
#include "services/import/assetimportservice.h"
#include "services/import/importbatchrunner.h"
#include "services/projectassets.h"
#include "services/assetdelete.h"
#include "services/assettray.h"
#include "services/projectfolders.h"
#include "services/materialbundle.h"
#include "services/undoservice.h"
#include "commands/projectfoldercommand.h"
#include <memory>
#include "modules/materials/api/materialsapi.h"
#include "scripting/scriptengine.h"
#include "services/projectmembership.h"
#include "services/avatarassets.h"
#include "services/assetmetadata.h"
#include "services/assetservice.h"
#include "services/services.h"
#include "services/imagematerial.h"
#include "services/assetcas.h"
#include <QSqlDatabase>
#include "io/ziphelper.h"
#include "io/assetmanager.h"
#include "io/builtinmaterials.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/core/properties/property.h"
#include "io/scenewriter.h"
#include "services/subscriber.h"
#include "data/materialpreset.h"
#include "services/loadtimeline.h"
#include "io/materialreader.h"
#include "ui/dialogs/toast.h"
#include "ui/style/panelmetrics.h"
#include "ui/style/stylesheet.h"
#include "ui/style/thememanager.h"
#include <QActionGroup>
#include "ui/controls/assetdrag.h"

namespace {
// Pin-world byte resolution for the .jaf exporters (phase 4): an asset's
// bytes live in the CAS, addressed through the project pin - the flat
// project folder holds nothing.
QString resolvePinnedAssetPath(Project *project, const QString &assetGuid, QString *nameOut)
{
    return AssetCas::resolvePinned(QSqlDatabase::database(), AssetStorePaths::root(),
                                   project ? project->getProjectGuid() : QString(),
                                   assetGuid, nameOut);
}
} // namespace

// The texture/material .jaf exporters' payload: every member that HAS stored
// bytes, copied under its display name. Walks GUIDS (plan item 15c). It used
// to walk the dependency closure as a list of NAMES and turn each back into a
// guid with a by-name catalog lookup scoped to the open project — which only
// ever matched rows stamped with that project (so a pinned library texture,
// stamped with whichever project imported it, was silently left out of the
// archive) and matched the wrong row whenever two assets shared a name. A
// member with no bytes (the material row itself, a DB-only asset) resolves
// to nothing and is skipped, which is what the old "names without an
// extension" filter approximated.
void AssetWidget::copyMemberFilesForExport(const QStringList &members, const QString &writePath)
{
    QStringList seen;
    for (const QString &member : members) {
        if (member.isEmpty() || seen.contains(member)) continue;
        seen << member;
        QString name;
        const QString assetPath = resolvePinnedAssetPath(project, member, &name);
        if (assetPath.isEmpty()) continue;
        if (name.isEmpty()) name = QFileInfo(assetPath).fileName();
        QFile::copy(assetPath, IrisUtils::join(writePath, "assets", QFileInfo(name).fileName()));
    }
}


AssetWidget::AssetWidget(Database *handle, QWidget *parent) : QWidget(parent), ui(new Ui::AssetWidget)
{
	ui->setupUi(this);

	this->db = handle;

	ui->assetView->setAttribute(Qt::WA_MacShowFocusRect, false);
	ui->assetTree->setAttribute(Qt::WA_MacShowFocusRect, false);
	ui->assetView->viewport()->setAttribute(Qt::WA_MacShowFocusRect, false);
	ui->assetTree->viewport()->setAttribute(Qt::WA_MacShowFocusRect, false);

	ui->assetView->viewport()->installEventFilter(this);
	ui->assetTree->viewport()->installEventFilter(this);
	ui->assetTree->setContextMenuPolicy(Qt::CustomContextMenu);

	// THE TRAY MAY BE SHORT (plan item 15: the window on a 1366x768 laptop).
	// The bottom tray is a dock, and a dock area never goes below the sum of
	// its content's minimum heights — which the WINDOW then inherits. Two list
	// views at Qt's default scroll-area minimum put this panel's floor at 169 px
	// and, with the tray around it, the editor window's at 694 px: taller than a
	// 768-line screen leaves once a taskbar and a title bar have taken theirs.
	// A list that can show one row is still a list, and the user who wants a
	// taller tray drags the splitter — this is a floor, not a size.
	ui->assetView->setMinimumHeight(PanelMetrics::trayListMinHeight);
	ui->assetTree->setMinimumHeight(PanelMetrics::trayListMinHeight);
	
	connect(ui->assetTree, SIGNAL(itemClicked(QTreeWidgetItem*, int)),
		this, SLOT(treeItemSelected(QTreeWidgetItem*)));

	connect(ui->assetTree, SIGNAL(itemChanged(QTreeWidgetItem*, int)),
		this, SLOT(treeItemChanged(QTreeWidgetItem*, int)));

	connect(ui->assetTree, SIGNAL(customContextMenuRequested(const QPoint&)),
		this, SLOT(sceneTreeCustomContextMenu(const QPoint&)));

	ui->assetView->setContextMenuPolicy(Qt::CustomContextMenu);
	ui->assetView->setResizeMode(QListWidget::Adjust);
	ui->assetView->setMovement(QListView::Static);
    ui->assetView->setSelectionBehavior(QAbstractItemView::SelectItems);
	ui->assetView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->assetView->setSelectionRectVisible(false);

	ui->assetView->setDragDropMode(QAbstractItemView::DragDrop);
	// ONE DRAG OWNER (ui/panels/singledragowner.h): this widget's eventFilter
	// starts the QDrag, so the view's own machinery stays disarmed. LAST,
	// because setDragDropMode arms dragEnabled.
	singledrag::disarmViewDrag(ui->assetView);
	// AND ONE DROP OWNER (DRAWERS-1): the PANEL takes every drop — the file
	// URLs it always took and the internal drop onto a folder tile
	// (AssetWidget::dropEvent) — so the view is told plainly that it takes
	// none. It was already declining them, but only because its model refuses
	// an unknown payload; the day the model accepted one, the view would
	// swallow the event and the panel's handler would silently stop running.
	// Said, rather than relied upon.
	ui->assetView->viewport()->setAcceptDrops(false);

	activeFilter = SettingsManager::getDefaultManager()->getValue("active_filter", 0).toInt();
	// (The "Show dependencies" checkbox is gone, lane L13: it switched off the
	// dependee filter that hid every asset a scene used, and the tray has one
	// rule now — services/assettray.h — with nothing left to switch.)

    ui->assetView->setItemDelegate(new ListViewDelegate());
    ui->assetView->setTextElideMode(Qt::ElideRight);

	connect(ui->assetView,  SIGNAL(itemClicked(QListWidgetItem*)),
		    this,           SLOT(assetViewClicked(QListWidgetItem*)));

	connect(ui->assetView,  SIGNAL(customContextMenuRequested(const QPoint&)),
		    this,           SLOT(sceneViewCustomContextMenu(const QPoint&)));

	connect(ui->assetView,  SIGNAL(itemDoubleClicked(QListWidgetItem*)),
		    this,           SLOT(assetViewDblClicked(QListWidgetItem*)));

	connect(ui->assetView->itemDelegate(), &QAbstractItemDelegate::commitData, this, &AssetWidget::OnLstItemsCommitData);

	connect(ui->searchBar, SIGNAL(textChanged(QString)), this, SLOT(searchAssets(QString)));

	connect(ui->importBtn, SIGNAL(pressed()), SLOT(importAssetB()));
    ui->importBtn->setVisible(false);

	// The signal will be emitted from another thread (Nick) — or, on the engine
	// viewport, from the main thread. Either way the generator needs the database
	// for asset/material lookups (the legacy viewport also sets it, later).
	ThumbnailGenerator::getSingleton()->setDatabase(db);
	// Function-pointer connect: the payload is a value type now (the old
	// ThumbnailResult* had two receivers and the first one deleted it).
	connect(ThumbnailGenerator::getSingleton(), &ThumbnailGenerator::thumbnailComplete,
	        this, &AssetWidget::onThumbnailResult);

	breadCrumbLayout = new QHBoxLayout;
	breadCrumbLayout->setSpacing(0);
	ui->breadCrumb->setObjectName(QStringLiteral("BreadCrumb"));
	ui->breadCrumb->setLayout(breadCrumbLayout);
	// Display ▾ — same grey popup-button pattern as the desktop footer's
	// Tile Size/Desktops/Layouts buttons (owner direction): static label,
	// the checked popup entry is the current view mode. Replaces the old
	// Icon/List toggle-button pair.
	displayButton = new QPushButton(tr("Display ▾"));
	displayButton->setCursor(Qt::PointingHandCursor);

	displayMenu = new QMenu(this);
	displayMenu->setStyleSheet(StyleSheet::QMenuDarkDesktop());
	auto displayGroup = new QActionGroup(displayMenu);
	displayGroup->setExclusive(true);
	displayGridAction = displayMenu->addAction(tr("Grid"));
	displayGridAction->setCheckable(true);
	// Todo - use preferences
	displayGridAction->setChecked(true);
	displayGroup->addAction(displayGridAction);
	displayListAction = displayMenu->addAction(tr("List"));
	displayListAction->setCheckable(true);
	displayGroup->addAction(displayListAction);

	QHBoxLayout *toggleLayout = new QHBoxLayout;
	toggleLayout->setSpacing(0);
	toggleLayout->setSizeConstraint(QLayout::SetFixedSize);
	toggleLayout->addWidget(displayButton);

	iconSize = QSize(72, 72);
	listSize = QSize(32, 32);
	currentSize = iconSize;

    // "Go Up" is the only directory control. A second "<" (go BACK) button
    // lived here with its handler commented out and its addWidget commented
    // out too — a dead widget that was constructed, never shown, never freed,
    // and connected to an empty lambda. Deleted 2026-09-07 (owner ask); the
    // "Assets" tree root and Go Up are untouched.
    goUpOneControl = new QPushButton(tr("Go Up"));
    goUpOneControl->setEnabled(false);

    QHBoxLayout *dirControlLayout = new QHBoxLayout;
    dirControlLayout->setSpacing(0);
    dirControlLayout->setSizeConstraint(QLayout::SetFixedSize);
    dirControlLayout->addWidget(goUpOneControl);

    ui->dirControls->setLayout(dirControlLayout);
    ui->dirControls->setObjectName("DirControl");

    connect(goUpOneControl, &QPushButton::pressed, [this]() {
        updateAssetView(db->fetchAsset(assetItem.selectedGuid).parent, activeFilter);
    });

	setMouseTracking(true);
	ui->assetView->setMouseTracking(true);

	ui->assetView->setViewMode(QListWidget::IconMode);
	ui->assetView->setSpacing(4);
	ui->assetView->setIconSize(currentSize);

	connect(displayGridAction, &QAction::triggered, this, [this]() {
		ui->assetView->setViewMode(QListWidget::IconMode);
		ui->assetView->setAlternatingRowColors(false);
		ui->assetView->setSpacing(4);
		currentSize = iconSize;
		ui->assetView->setIconSize(currentSize);
		ui->assetView->setItemDelegate(new ListViewDelegate());
		updateAssetView(assetItem.selectedGuid, activeFilter);
	});

	connect(displayListAction, &QAction::triggered, this, [this]() {
		ui->assetView->setViewMode(QListWidget::ListMode);
		ui->assetView->setAlternatingRowColors(true);
		ui->assetView->setSpacing(0);
		currentSize = listSize;
		ui->assetView->setIconSize(currentSize);
		ui->assetView->setItemDelegate(new QStyledItemDelegate());
		updateAssetView(assetItem.selectedGuid, activeFilter);
	});

	connect(displayButton, &QPushButton::pressed, this, [this]() {
		displayMenu->exec(displayButton->mapToGlobal(QPoint(0, displayButton->height())));
	});

	ui->switcher->setLayout(toggleLayout);
	ui->switcher->setObjectName("Switcher");

	if (!ThemeManager::classicActive()) {
		// The shared chrome button spec at panel-header height (owner
		// direction): rounded grey with side gutters, compact. Classic keeps
		// its #Switcher/#DirControl sheets bit-for-bit.
		goUpOneControl->setStyleSheet(ThemeManager::chromeCompactButtonSheet());
		displayButton->setStyleSheet(ThemeManager::chromeCompactButtonSheet());
	}

    filterGroupLayout = new QHBoxLayout;
    filterGroupLayout->setContentsMargins(0, 0, 0, 0);
    filterGroupLayout->setSpacing(0);
    ui->filterWidget->setObjectName(QStringLiteral("FilterWidget"));
    ui->filterWidget->setLayout(filterGroupLayout);

    // THE ASSET CLASSES THE PIPELINE ACTUALLY PRODUCES (audited 2026-09-07
    // against ModelTypes + AssetView::getAssetType + the importers in
    // src/services/import/): Object, Material, Texture, Shader,
    // ParticleSystem, Sky, Music (shown as "Audio" everywhere else), Video,
    // LightProfile (.ies) and File. Video and Light Profiles were missing —
    // both are first-class library types with their own importer, metadata
    // block and thumbnail, and neither could be filtered for. "Music" is
    // relabelled "Audio" to match the type name the rest of the app shows.
    // Mesh is deliberately NOT here: a mesh is the inside of its model and
    // never a tray tile (services/assettray.h, rule 2), so a Meshes filter
    // would always read as empty. Undefined / Variant / SoundEffect are enum
    // values nothing ever writes.
    assetFilterCombo = new QComboBox(this);
    assetFilterCombo->addItem("All Assets", QVariant::fromValue(0));
    assetFilterCombo->addItem("Objects", QVariant::fromValue(static_cast<int>(ModelTypes::Object)));
    assetFilterCombo->addItem("Materials", QVariant::fromValue(static_cast<int>(ModelTypes::Material)));
    assetFilterCombo->addItem("Textures", QVariant::fromValue(static_cast<int>(ModelTypes::Texture)));
    assetFilterCombo->addItem("Particle Systems", QVariant::fromValue(static_cast<int>(ModelTypes::ParticleSystem)));
    assetFilterCombo->addItem("Skies", QVariant::fromValue(static_cast<int>(ModelTypes::Sky)));
    assetFilterCombo->addItem("Audio", QVariant::fromValue(static_cast<int>(ModelTypes::Music)));
    assetFilterCombo->addItem("Video", QVariant::fromValue(static_cast<int>(ModelTypes::Video)));
    assetFilterCombo->addItem("Light Profiles", QVariant::fromValue(static_cast<int>(ModelTypes::LightProfile)));
    assetFilterCombo->addItem("Animations", QVariant::fromValue(static_cast<int>(ModelTypes::Animation)));
    assetFilterCombo->addItem("Avatars", QVariant::fromValue(static_cast<int>(ModelTypes::Avatar)));
    assetFilterCombo->addItem("Files", QVariant::fromValue(static_cast<int>(ModelTypes::File)));

	// A persisted filter naming a class this build no longer offers must fall
	// back to All Assets — findData returns -1, and an index of -1 leaves the
	// combo blank while the view stays filtered by an invisible value.
	int index = assetFilterCombo->findData(activeFilter);
	if (index < 0) { index = 0; activeFilter = 0; }
	assetFilterCombo->setCurrentIndex(index);

    filterGroupLayout->addWidget(new QLabel("Filter Assets:"));
    filterGroupLayout->addWidget(assetFilterCombo);

    // SHOW MEMBER TEXTURES (MATERIAL_BUNDLE_SPEC V-2, the owner's Q4). A
    // picture that came in through a material's picker is part of that
    // bundle and the bundle is its tile — this is the switch that opens the
    // bundle up and lists them beside it. Off by default, remembered, and it
    // turns off exactly ONE rule: everything else the tray collapses stays
    // collapsed.
    showMembersBox = new QCheckBox(tr("Show member textures"));
    showMembersBox->setToolTip(tr("List the pictures that came in INSIDE a material as tiles of "
                                  "their own. Your own imported images are always listed."));
    showMembersBox->setChecked(
        SettingsManager::getDefaultManager()->getValue("tray_show_members", false).toBool());
    showMembers = showMembersBox->isChecked();
    filterGroupLayout->addWidget(showMembersBox);
    connect(showMembersBox, &QCheckBox::toggled, this, [this](bool on) {
        showMembers = on;
        SettingsManager::getDefaultManager()->setValue("tray_show_members", on);
        updateAssetView(assetItem.selectedGuid, activeFilter);
    });

    connect<void(QComboBox::*)(int)>(assetFilterCombo, &QComboBox::currentIndexChanged, this, [&](int index) {
		activeFilter = assetFilterCombo->itemData(index).toInt();
		SettingsManager::getDefaultManager()->setValue("active_filter", activeFilter);
        updateAssetView(assetItem.selectedGuid, activeFilter);
    });

    ui->filterWidget->setStyleSheet(StyleSheet::AssetWidgetFilterPane());

	ui->searchBar->setPlaceholderText(tr("Type to search for assets..."));

	// Parented: app teardown must close and destroy it — an unparented
	// progress dialog is a top-level window that outlives the main window
	// (the owner-reported orphaned "loading" dialog) and, worse, keeps
	// quitOnLastWindowClosed from ever firing.
	progressDialog = new ProgressDialog(this);
	progressDialog->setLabelText("Importing assets...");

	setStyleSheet(StyleSheet::AssetWidgetPanel());

	// THE TRAY FOLLOWS THE PROJECT'S PINS (lane L13). Every pin change for the
	// open project — a verb's add, a binding, a paste, a remove — is announced
	// by the one function that makes it (services/projectmembership.h), and
	// the tray repopulates on the next event-loop turn: coalesced, because a
	// paste or a closure can pin a dozen assets in one gesture. It used to be
	// stale until the user clicked a folder.
	connect(ProjectMembership::instance(), &ProjectMembership::changed, this,
	        [this](const QString &projectGuid) {
		// Empty = "some project" (an edge delete that cannot name one).
		if (!project || project->getProjectGuid().isEmpty()) return;
		if (!projectGuid.isEmpty() && project->getProjectGuid() != projectGuid) return;
		if (membershipRefreshPending) return;
		membershipRefreshPending = true;
		QTimer::singleShot(0, this, [this]() { flushPendingRefresh(); });
	});
}

void AssetWidget::flushPendingRefresh()
{
	if (!membershipRefreshPending) return;
	membershipRefreshPending = false;
	if (project && !project->getProjectGuid().isEmpty()) refresh();
}

void AssetWidget::trigger()
{
    // NO clearAssetList() here (IMAGE_PLANE_SPEC §6): trigger() runs AFTER
    // ProjectManager::registerProjectSessionAssets in every open flow
    // (loadProjectAssets[Sync] → MainWindow::openProject → trigger), so a
    // clear at this point silently WIPED the session hydration — the
    // "material drag no-ops after reopen" defect. Every path here already
    // cleared: closeProject() and loadProjectAssets[Sync]() both start with
    // AssetManager::clearAssetList(), so the built-in presets below register
    // exactly once per open.

    // THE BUILT-IN PRESET REGISTRATION IS GONE (MATERIAL-PREVIEW-1 item c).
    // Every shipped preset used to be converted to a material here and parked
    // in the AssetManager behind its reserved guid, on every project open, for
    // ONE reader: the viewport's hover preview — which could not read it, since
    // this loop stored a `QSharedPointer<PbrMaterial>` through `auto` while the
    // reader asked for `iris::MaterialPtr` and Qt has no converter between
    // them. That is the owner's "tray materials do not preview" bug in one
    // line. Nothing resolves a material through the AssetManager any more:
    // SceneEditService::resolveMaterial is the ONE way, and it reads the preset
    // list (SceneEditService::presets) and the database directly.

	// It's important that this gets called after a project has been loaded (iKlsR)
	{
		LoadTimeline::Accumulate tree(QStringLiteral("panel:populateAssetTree"));
		populateAssetTree(true);
	}
	// (The loop that used to follow — turning every Object asset that still
	// held a raw parsed scene into a built fragment — is gone with the parsed-
	// scene asset payload it served: since the asset pipeline every Object
	// entry is registered as a built AssetNodeObject, so the loop skipped
	// every asset it visited. ENGINEERING_DEBT L4 part 3.)
}

void AssetWidget::refresh()
{
	updateAssetView(assetItem.selectedGuid, activeFilter);
	populateAssetTree(false);
}

void AssetWidget::extractTexturesAndMaterialFromMaterial(
	const QString &filePath,
	QStringList &textureList,
	QJsonObject &mat)
{
	QFile *file = new QFile(filePath);
	file->open(QIODevice::ReadOnly | QIODevice::Text);
	QJsonDocument doc = QJsonDocument::fromJson(file->readAll());

	const QJsonObject materialDefinition = doc.object();
	// Legacy key names renamed to their PBR equivalents, then driven onto a
	// PbrMaterial's own rows (HLMS_ADOPTION P4b) — no `.shader` file is loaded
	// to borrow a uniform list from any more.
	const QJsonObject normalised = BuiltinMaterials::normaliseLegacyDefinition(materialDefinition);

	auto material = iris::PbrMaterial::create();
	material->setName(materialDefinition["name"].toString());

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

void AssetWidget::extractTexturesAndMaterialFromMaterial(
	const QByteArray &blob,
	QStringList &textureList,
	QJsonObject &mat)
{
    QJsonDocument doc = QJsonDocument::fromJson(blob);
	const QJsonObject materialDefinition = doc.object();
	// Legacy key names renamed to their PBR equivalents, then driven onto a
	// PbrMaterial's own rows (HLMS_ADOPTION P4b) — no `.shader` file is loaded
	// to borrow a uniform list from any more.
	const QJsonObject normalised = BuiltinMaterials::normaliseLegacyDefinition(materialDefinition);

	auto material = iris::PbrMaterial::create();
	material->setName(materialDefinition["name"].toString());

	for (const auto &prop : material->properties) {
		if (normalised.contains(prop->name)) {
			if (prop->type == iris::PropertyType::Texture) {
				auto textureStr = !normalised[prop->name].toString().isEmpty()
					? normalised[prop->name].toString()
					: QString();
				material->setValue(prop->name, textureStr);
				//if (!textureStr.isEmpty()) {
				//	textureList.append(QFileInfo(textureStr).fileName());
				//}
			}
			else {
				material->setValue(prop->name, normalised[prop->name].toVariant());
			}
		}
	}

	SceneWriter::writeSceneNodeMaterial(mat, material, false);
}

void AssetWidget::setEventBus(Subscriber *bus)
{
	// (Phase 4: was a Globals::eventSubscriber connect in the constructor.)
	if (bus) connect(bus,	&Subscriber::updateAssetSkyItemFromSkyPropertyWidget,
	                 this,	&AssetWidget::updateAssetSkyItemFromSkyPropertyWidget);
}

AssetWidget::~AssetWidget()
{
	delete ui;
}

void AssetWidget::populateAssetTree(bool initialRun)
{
	auto rootTreeItem = new QTreeWidgetItem();
	rootTreeItem->setText(0, "Assets");
	rootTreeItem->setIcon(0, QIcon(":/icons/icons8-folder-72.png"));
	rootTreeItem->setData(0, MODEL_GUID_ROLE, project->getProjectGuid());
	updateTree(rootTreeItem, project->getProjectGuid());

	ui->assetTree->clear();
	ui->assetTree->addTopLevelItem(rootTreeItem);
	ui->assetTree->expandItem(rootTreeItem);

	if (initialRun) {
		updateAssetView(project->getProjectGuid(), activeFilter);
		rootTreeItem->setSelected(true);
		assetItem.item = rootTreeItem;
		assetItem.selectedGuid = project->getProjectGuid();
	}
}

void AssetWidget::updateTree(QTreeWidgetItem *parent, QString path)
{
	for (const auto &folder : db->fetchChildFolders(path, project->getProjectGuid())) {
		auto item = new QTreeWidgetItem();
		item->setIcon(0, QIcon(":/icons/icons8-folder-72.png"));
		item->setData(0, Qt::DisplayRole, folder.name);
		item->setData(0, MODEL_GUID_ROLE, folder.guid);
		item->setData(0, MODEL_PARENT_ROLE, folder.parent);
		parent->addChild(item);
		// Add children if any
		updateTree(item, folder.guid);
	}
}

// Use this a force thumbnail generator in the future
void AssetWidget::generateAssetThumbnails()
{
	//foreach (auto asset, AssetManager::assets) {
	//    if (asset->type == AssetType::Object) {
	//        // TODO - fetch a list and check that instead of hitting the db, low cost but better way
	//        if (!db->hasCachedThumbnail(asset->fileName)) {
	//            ThumbnailGenerator::getSingleton()->requestThumbnail(
	//                ThumbnailRequestType::Mesh, asset->path, asset->path
	//            );
	//        }
	//    }
	//}
}

void AssetWidget::addItem(const FolderRecord &folderData)
{
    if (!folderData.visible) return;

	QListWidgetItem *item = new QListWidgetItem;
	item->setData(Qt::DisplayRole, folderData.name);
	item->setData(MODEL_ITEM_TYPE, MODEL_FOLDER);
	item->setData(MODEL_GUID_ROLE, folderData.guid);
	item->setData(MODEL_PARENT_ROLE, folderData.parent);

	item->setSizeHint(currentSize);
	item->setTextAlignment(Qt::AlignCenter);
	item->setFlags(item->flags() | Qt::ItemIsEditable);
	item->setIcon(QIcon(":/icons/icons8-folder-72.png"));

	ui->assetView->addItem(item);
}

void AssetWidget::addItem(const AssetRecord &assetData)
{
    // (The built-in skip that returned here moved INTO the tray rule —
    // services/assettray.h, rule 4 — so the panel and assets.list({tray:true})
    // run the same one. Rows arrive here already decided.)
    const auto prop = QJsonDocument::fromJson(assetData.properties).object();

	QListWidgetItem *item = new QListWidgetItem;
	item->setData(Qt::DisplayRole, QFileInfo(assetData.name).baseName());
    item->setData(Qt::UserRole, assetData.name);
    item->setData(MODEL_TYPE_ROLE, assetData.type);
	item->setData(MODEL_ITEM_TYPE, MODEL_ASSET);
	item->setData(MODEL_GUID_ROLE, assetData.guid);
	item->setData(MODEL_PARENT_ROLE, assetData.parent);

    QPixmap thumbnail;
    if (thumbnail.loadFromData(assetData.thumbnail, "PNG")) {
        item->setIcon(QIcon(thumbnail));
    }
    else {
        item->setIcon(QIcon(":/icons/empty_object.png"));
    }

	if (assetData.type == static_cast<int>(ModelTypes::Texture)) {

	}

	if (assetData.type == static_cast<int>(ModelTypes::Sky)) {
		int skyType = prop.value("sky").toObject().value("type").toInt();
		item->setData(SKY_TYPE_ROLE, skyType);
		item->setData(MODEL_TYPE_ROLE, assetData.type);
		item->setIcon(QIcon(":/icons/icons8-file-sky.png"));
	}

	if (assetData.type == static_cast<int>(ModelTypes::Music)) {
		item->setData(MODEL_TYPE_ROLE, assetData.type);
		item->setIcon(QIcon(":/icons/icons8-file-music.png"));
	}

    if (assetData.type == static_cast<int>(ModelTypes::Shader)) {
        item->setData(MODEL_TYPE_ROLE, assetData.type);
		if(thumbnail.loadFromData(assetData.thumbnail, "PNG"))   item->setIcon(QIcon(thumbnail));
		else item->setIcon(QIcon(":/icons/icons8-file-72.png"));
    }

    if (assetData.type == static_cast<int>(ModelTypes::ParticleSystem)) {
        item->setData(MODEL_TYPE_ROLE, assetData.type);
        item->setIcon(QIcon(":/icons/icons8-file-72-ps.png"));
    }

    if (assetData.type == static_cast<int>(ModelTypes::File)) {
        item->setData(MODEL_TYPE_ROLE, assetData.type);
        // TODO - make this some generic value all assets can use
        //item->setData(MODEL_MESH_ROLE, shaderAssetName.name);
        item->setIcon(QIcon(":/icons/icons8-file-72-file.png"));
    }
	
    if (assetData.type == static_cast<int>(ModelTypes::Material)) {
		item->setData(MODEL_TYPE_ROLE, assetData.type);
	}
	
    if (assetData.type == static_cast<int>(ModelTypes::Object)) {
		const QString meshAssetGuid =
            db->getDependencyByType(static_cast<int>(ModelTypes::Mesh), assetData.guid);
		item->setData(MODEL_TYPE_ROLE, assetData.type);
		item->setData(MODEL_MESH_ROLE, db->fetchAsset(meshAssetGuid).name);
	}

	item->setSizeHint(currentSize);
	item->setTextAlignment(Qt::AlignCenter);
	item->setFlags(item->flags() | Qt::ItemIsEditable);

	ui->assetView->addItem(item);
}

void AssetWidget::addCrumbs(const QVector<FolderRecord> &folderData)
{
	breadCrumbLayout->setAlignment(Qt::AlignLeft);

	while (QLayoutItem* item = breadCrumbLayout->takeAt(0)) {
		Q_ASSERT(!item->layout()); // otherwise the layout will leak
		delete item->widget();
		delete item;
	}

	for (const auto &folder : folderData) {
		QPushButton *crumb = new QPushButton(folder.name);
		crumb->setCheckable(true);
		crumb->setCursor(Qt::PointingHandCursor);
		if (&folder == &folderData.back()) {
			crumb->setChecked(true);
		}
		connect(crumb, &QPushButton::pressed, [folder, crumb, this]() {
			assetItem.selectedGuid = folder.guid;
			updateAssetView(folder.guid, activeFilter);
			syncTreeAndView(folder.guid);
		});
		breadCrumbLayout->addWidget(crumb);
	}
}

void AssetWidget::updateAssetView(const QString &path, int filter)
{
	ui->assetView->clear();

    // THE TRAY LISTING (services/assettray.h): every asset a scene uses, once
    // — the folder's rows and, at the root, the project's pinned members,
    // under the one rule `assets.list({tray: true})` answers with, from the
    // same function, so the panel and the verb cannot drift. The Assets PAGE
    // is untouched: that is where a user browses the members.
    if (filter <= 0) {
        for (const auto &folder : db->fetchChildFolders(path, project->getProjectGuid())) addItem(folder);
        addCrumbs(db->fetchCrumbTrail(path, project->getProjectGuid()));
    }
    for (const auto &asset : assettray::list(db, project->getProjectGuid(), path, filter,
                                            showMembers))
        addItem(asset);

    goUpOneControl->setEnabled(false);
}

QWidget *AssetWidget::dropTarget() const
{
	// The panel itself: the drag events arrive here (see folderItemAt).
	return const_cast<AssetWidget *>(this);
}

QPoint AssetWidget::tileCentre(const QString &guid)
{
	flushPendingRefresh();
	for (int i = 0; i < ui->assetView->count(); ++i) {
		QListWidgetItem *item = ui->assetView->item(i);
		if (item->data(MODEL_GUID_ROLE).toString() != guid) continue;
		const QRect rect = ui->assetView->visualItemRect(item);
		if (rect.isEmpty()) return QPoint();
		// In THIS WIDGET's coordinates, which is where a drop lands.
		return ui->assetView->viewport()->mapTo(this, rect.center());
	}
	return QPoint();
}

QVariantList AssetWidget::shownTiles()
{
    // What the user sees once the event loop turns: a repopulate a pin change
    // queued lands first (a --script run holds the loop for its whole run).
    flushPendingRefresh();
    QVariantList out;
    for (int i = 0; i < ui->assetView->count(); ++i) {
        const QListWidgetItem *item = ui->assetView->item(i);
        const bool folder = item->data(MODEL_ITEM_TYPE).toInt() == MODEL_FOLDER;
        out.append(QVariantMap{
            { QStringLiteral("guid"), item->data(MODEL_GUID_ROLE).toString() },
            { QStringLiteral("name"), folder ? item->data(Qt::DisplayRole).toString()
                                             : item->data(Qt::UserRole).toString() },
            { QStringLiteral("folder"), folder },
        });
    }
    return out;
}

void AssetWidget::updateAssetContentsView(const QString &guid)
{
    ui->assetView->clear();
    for (const auto &asset : db->fetchAssetsFromParent(guid)) addItem(asset);
}

bool AssetWidget::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == ui->assetView->viewport()) {
		switch (event->type()) {
		    case QEvent::MouseButtonPress: {
			    auto evt = static_cast<QMouseEvent*>(event);
                if (evt->button() == Qt::LeftButton) {
                    startPos = evt->pos();
                    QModelIndex index = ui->assetView->indexAt(evt->pos());
                    if (index.isValid()) draggingItem = true;
                }

			    AssetWidget::mousePressEvent(evt);
			    break;
		    }

		    case QEvent::MouseButtonRelease: {
			    auto evt = static_cast<QMouseEvent*>(event);
                draggingItem = false;
                emit assetItemSelected(nullptr);
                AssetWidget::mouseReleaseEvent(evt);
			    break;
		    }

		    case QEvent::MouseMove: {
			    auto evt = static_cast<QMouseEvent*>(event);
			    if (evt->buttons() & Qt::LeftButton) {
                    if (draggingItem) {
                        int distance = (evt->pos() - startPos).manhattanLength();
                        if (distance >= QApplication::startDragDistance()) {
                            // One drag per press: exec() below runs a nested
                            // loop and can swallow the release that would
                            // otherwise clear this (the sticky-drag class of
                            // bug the presets panels had, 2026-09-07).
                            draggingItem = false;
                            auto item = ui->assetView->currentItem();

                            if (item) {
                                auto drag = QPointer<QDrag>(new QDrag(this));
                                // ONE payload builder (ui/controls/assetdrag.h),
                                // carrying the WHOLE selection (DRAWERS-1): the
                                // four slots still describe the tile under the
                                // cursor, so every existing drop handler is
                                // unchanged, and slot 4 carries every guid the
                                // user picked up — which is what a drop onto a
                                // folder tile files. The dragged tile is part of
                                // the gesture even when it is not selected (a
                                // press on an unselected tile starts a drag of
                                // that one thing).
                                QStringList picked;
                                for (QListWidgetItem *chosen : ui->assetView->selectedItems())
                                    picked << chosen->data(MODEL_GUID_ROLE).toString();
                                const QString primary = item->data(MODEL_GUID_ROLE).toString();
                                if (!picked.contains(primary)) picked = QStringList{ primary };
                                drag->setMimeData(AssetDrag::mimeForMany(
                                    item->data(MODEL_TYPE_ROLE).toInt(),
                                    item->data(Qt::UserRole).toString(),
                                    item->data(MODEL_MESH_ROLE).toString(),
                                    primary, picked));

                                drag->setPixmap(item->icon().pixmap(64, 64));
                                drag->exec();
                                // The release exec() ate (singledragowner.h):
                                // without it the view stays in DraggingState
                                // and glues the item to the cursor the next
                                // time the pointer enters the browser.
                                singledrag::clearViewPressState(ui->assetView);
                                // ONE drop per gesture: consume the move, or
                                // QListWidget's own startDrag runs a second
                                // QDrag from this same event and the drop
                                // happens twice (see assetmodelpanel.cpp).
                                return true;
                            }
                        }
                    }
			    }

			    AssetWidget::mouseMoveEvent(evt);
			    break;
		    }

		    default: break;
		}
	}

	return QObject::eventFilter(watched, event);
}

// THE DROP TARGET UNDER THE CURSOR, from a point in THIS WIDGET's coordinates
// (DRAWERS-1). The drag events arrive HERE and not on the list's viewport, and
// the reason is worth writing down, because the first version of this handler
// was installed on the viewport and never ran once.
//
// The viewport DOES accept drops: `setDragDropMode(DragDrop)` sets it on the
// view and QAbstractScrollArea syncs the viewport on AcceptDropsChange, so
// both read true (measured with this exact construction — a QListWidget at
// DragDrop with dragEnabled cleared by singledragowner.h). What happens is one
// step later: QAbstractItemView asks its MODEL whether it can take the payload,
// the list's model declines a four/five-slot map that is not its own internal
// move, the view ignores the event — and Qt then propagates the drag up to the
// first ancestor that accepts it, which is this panel (MainWindow sets
// `assetWidget->setAcceptDrops(true)`). That is also why the file-URL drop has
// always landed in AssetWidget::dropEvent.
//
// So the panel is where the handler belongs, and the list is explicitly told it
// takes no drops of its own (DropNone in the constructor) rather than left to
// decline them by accident: the day the model accepts an item-view payload,
// the view would swallow the event and this handler would go silent with no
// error anywhere.
QListWidgetItem *AssetWidget::folderItemAt(const QPoint &pos) const
{
	QListWidgetItem *item =
		ui->assetView->itemAt(ui->assetView->viewport()->mapFrom(
			const_cast<AssetWidget *>(this), pos));
	if (!item || item->data(MODEL_ITEM_TYPE).toInt() != MODEL_FOLDER) return nullptr;
	return item;
}

// THE PANEL CALLS THE MODEL, AND THE MODEL ANSWERS IN WORDS (DRAWERS-1). Every
// folder gesture here is the body of the verb it mirrors — one rule set, one
// wording — and it goes on the editor's undo stack exactly as the verb does on
// a script's, so one Ctrl+Z takes back a drag of a dozen tiles.
void AssetWidget::moveToFolder(const QStringList &guids, const QString &folderGuid)
{
	if (!db || !project || guids.isEmpty()) return;
	const QString projectGuid = project->getProjectGuid();
	if (projectGuid.isEmpty()) return;

	// JUDGED BEFORE ANYTHING IS PUSHED (the same order the verb takes): a
	// refused move must not leave a step on the user's undo stack, and neither
	// must a drop onto the folder the rows are already in.
	const projectfolders::Result judged =
		projectfolders::judgeMove(db, projectGuid, guids, folderGuid);
	if (!judged.ok) {
		// A TOAST, NOT A MODAL. This runs one event-loop turn after a drop —
		// and, through editor.dragAssetToTray, inside a SCRIPT's run: a modal
		// box there waits for a click nobody is going to make.
		toast()->showToast(tr("Move to folder"), judged.error, 3000);
		return;
	}
	if (judged.moved == 0) return;

	auto outcome = std::make_shared<FolderCommandOutcome>();
	auto *command = new MoveToProjectFolderCommand(db, projectGuid, guids, folderGuid, outcome);
	if (services && services->undo) {
		services->undo->push(command);            // redo() runs the move; may self-delete
	} else {
		command->redo();
		delete command;
	}
	if (!outcome->error.isEmpty()) {
		toast()->showToast(tr("Move to folder"), outcome->error, 3000);
		return;
	}
	// NO refresh() HERE (fix round item 10): the model announced the change and
	// this panel repopulates from that announcement on the next turn. Calling
	// both repopulated the tray twice for every gesture.
}

// ONE TOAST FOR THE PANEL, reused — the Assets page's own pattern
// (ui/pages/assetview.cpp): a new one per message leaks a widget per refusal.
Toast *AssetWidget::toast()
{
	if (!mToast) mToast = new Toast(this);
	return mToast;
}

void AssetWidget::dragEnterEvent(QDragEnterEvent *evt)
{
	if (evt->mimeData()->hasUrls()) {
		evt->acceptProposedAction();
		return;
	}
	// THE INTERNAL DROP (DRAWERS-1): a tile, or a whole selection, dropped ON A
	// FOLDER TILE is a move into that folder. The gesture carries the ONE asset
	// payload (ui/controls/assetdrag.h) — the same one this panel's own drag
	// builds — so the enter is accepted for any asset drag and dragMoveEvent
	// decides, tile by tile, whether this particular pixel takes one.
	if (AssetDrag::isAssetDrag(evt->mimeData())) evt->acceptProposedAction();
}

void AssetWidget::dragMoveEvent(QDragMoveEvent *evt)
{
	if (evt->mimeData()->hasUrls()) { evt->acceptProposedAction(); return; }
	if (!AssetDrag::isAssetDrag(evt->mimeData())) { evt->ignore(); return; }
	// ONLY a folder tile takes a move — over anything else the gesture reads
	// as refused, which is what the cursor tells the user.
	if (!folderItemAt(evt->position().toPoint())) { evt->ignore(); return; }
	evt->setDropAction(Qt::MoveAction);
	evt->accept();
}

void AssetWidget::dropEvent(QDropEvent *evt)
{
	// An internal drop first: it is the only one that is not a file import.
	if (!evt->mimeData()->hasUrls() && AssetDrag::isAssetDrag(evt->mimeData())) {
		QListWidgetItem *folder = folderItemAt(evt->position().toPoint());
		if (!folder) { evt->ignore(); return; }
		const QStringList guids = AssetDrag::guidsOf(evt->mimeData());
		evt->setDropAction(Qt::MoveAction);
		evt->accept();
		// DEFERRED out of the handler, like the import below: the move puts a
		// command on the undo stack and repopulates the list the drag source
		// still belongs to, and a drop handler is inside the source's nested
		// event loop.
		const QString target = folder->data(MODEL_GUID_ROLE).toString();
		QTimer::singleShot(0, this, [this, guids, target]() { moveToFolder(guids, target); });
		return;
	}

	QList<QUrl> droppedUrls = evt->mimeData()->urls();
	QStringList list;
	for (auto url : droppedUrls) {
		auto fileInfo = QFileInfo(url.toLocalFile());
		list << fileInfo.absoluteFilePath();
	}

	evt->acceptProposedAction();

	// Deferred out of the drop handler on purpose: the import decision is a
	// modal dialog (SPECS/IMPORT_DIALOG_SPEC.md §8) and a nested event loop
	// inside a drop leaves the drag source waiting on the XDND handshake.
	if (!list.isEmpty())
		QTimer::singleShot(0, this, [this, list]() { importAsset(list); });
}

void AssetWidget::treeItemSelected(QTreeWidgetItem *item)
{
	assetItem.item = item;
	assetItem.selectedGuid = item->data(0, MODEL_GUID_ROLE).toString();
	updateAssetView(item->data(0, MODEL_GUID_ROLE).toString(), activeFilter);
}

void AssetWidget::treeItemChanged(QTreeWidgetItem *item, int column)
{

}

void AssetWidget::updateAssetSkyItemFromSkyPropertyWidget(const QString &guid, iris::SkyType skyType)
{
	for (int i = 0; i < ui->assetView->count(); i++) {
		QListWidgetItem* item = ui->assetView->item(i);
		if (item->data(MODEL_GUID_ROLE).toString() == guid) {
			item->setData(SKY_TYPE_ROLE, static_cast<int>(skyType));
			return;
		}
	}
}

void AssetWidget::sceneTreeCustomContextMenu(const QPoint& pos)
{
	QModelIndex index = ui->assetTree->indexAt(pos);

	if (!index.isValid()) return;

	assetItem.item = ui->assetTree->itemAt(pos);
	assetItem.selectedPath = assetItem.item->data(0, Qt::UserRole).toString();

	QMenu menu;
	menu.setStyleSheet(StyleSheet::QMenuDark());

	QAction *action;

	// (CREATE > SHADER IS GONE — MATERIAL_BUNDLE_SPEC phase 2's Deletes, owner
	// Q3 "only materials". It minted a ModelTypes::Shader row from
	// app/templates/ShaderTemplate.shader, a format that predates the node
	// graph and that the graph loader cannot reopen: the tile it made could
	// not be edited, previewed or applied. A material is made in the Materials
	// module — or by `materials.create` — as ONE bundle.)
	QMenu *createMenu = menu.addMenu("Create");

    action = new QAction(QIcon(), "Sky", this);
    connect(action, SIGNAL(triggered()), this, SLOT(createSky()));
    createMenu->addAction(action);

    action = new QAction(QIcon(), "New Folder", this);
	connect(action, SIGNAL(triggered()), this, SLOT(createFolder()));
	createMenu->addAction(action);

	action = new QAction(QIcon(), "Import Asset", this);
	connect(action, SIGNAL(triggered()), this, SLOT(importAsset()));
	menu.addAction(action);

	action = new QAction(QIcon(), "Delete", this);
	connect(action, SIGNAL(triggered()), this, SLOT(deleteTreeFolder()));
	menu.addAction(action);

	menu.exec(ui->assetTree->mapToGlobal(pos));
}

void AssetWidget::exportSky()
{
    // get the export file path from a save dialog
    auto filePath = QFileDialog::getSaveFileName(
        this,
        "Choose export path",
        assetItem.wItem->data(Qt::DisplayRole).toString(),
        "Supported Export Formats (*.jaf)");

    if (filePath.isEmpty() || filePath.isNull())
        return;

    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid())
        return;

    const QString writePath = temporaryDir.path();

    const QString guid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();

    db->createBlobFromAsset(guid, QDir(writePath).filePath("asset.db"));

    QDir tempDir(writePath);
    tempDir.mkpath("assets");

    QFile manifest(QDir(writePath).filePath(".manifest"));
    if (manifest.open(QIODevice::ReadWrite))
    {
        QTextStream stream(&manifest);
        stream << "sky";
    }
    manifest.close();

    for (const auto &assetGuid : AssetHelper::fetchAssetAndAllDependencies(guid, db))
    {
        QString name;
        const QString assetPath = resolvePinnedAssetPath(project, assetGuid, &name);
        if (assetPath.isEmpty()) continue;
        if (name.isEmpty()) name = db->fetchAsset(assetGuid).name;
        if (name.isEmpty()) name = QFileInfo(assetPath).fileName();
        QFile::copy(assetPath, IrisUtils::join(writePath, "assets", name));
    }

    // ONE zip loop (amendment 7): the shared helper replaces the
    // hand-rolled zip_entry sweep this site duplicated.
    ZipHelper::zipDirectory(writePath, filePath);
}

void AssetWidget::sceneViewCustomContextMenu(const QPoint& pos)
{
	QModelIndex index = ui->assetView->indexAt(pos);

	QMenu menu;
	menu.setStyleSheet(StyleSheet::QMenuDark());
	QAction *action;

	if (index.isValid()) {
		auto item = ui->assetView->itemAt(pos);
		assetItem.wItem = item;

		// A FOLDER HAS TWO ROWS AND THEY ARE ITS OWN (DRAWERS-1). Rename goes
		// through the folder model's rule (a system folder, a duplicate name
		// under the same parent), and Delete is the PROJECT-side delete: the
		// folder goes and everything in it moves up to its parent. It used to
		// fall through to the asset rows below, where Delete ran
		// `deleteFolderAndDependencies` — a LIBRARY delete of every row filed
		// in the folder, from a view of one project.
		if (item->data(MODEL_ITEM_TYPE).toInt() == MODEL_FOLDER) {
			action = new QAction(QIcon(), tr("Rename"), this);
			connect(action, SIGNAL(triggered()), this, SLOT(renameViewItem()));
			menu.addAction(action);

			action = new QAction(QIcon(), tr("Delete Folder"), this);
			connect(action, SIGNAL(triggered()), this, SLOT(deleteFolderItem()));
			menu.addAction(action);

			menu.exec(ui->assetView->mapToGlobal(pos));
			return;
		}

		action = new QAction(QIcon(), "Rename", this);
		connect(action, SIGNAL(triggered()), this, SLOT(renameViewItem()));
		menu.addAction(action);

        if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Object) ||
            item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Material))
        {
            action = new QAction(QIcon(), "Favorite Asset", this);
            connect(action, SIGNAL(triggered()), this, SLOT(favoriteItem()));
            menu.addAction(action);

            action = new QAction(QIcon(), "Refresh Thumbnail", this);
            connect(action, SIGNAL(triggered()), this, SLOT(refreshThumbnail()));
            menu.addAction(action);
        }

        if (ui->assetView->selectedItems().count() > 1) {
            action = new QAction(QIcon(), "Export Asset Pack", this);
            connect(action, SIGNAL(triggered()), this, SLOT(exportAssetPack()));
            menu.addAction(action);
        }

        if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Texture)) {
            action = new QAction(QIcon(), "Export Texture", this);
            connect(action, SIGNAL(triggered()), this, SLOT(exportTexture()));
            menu.addAction(action);

            // IMAGE_PLANE_SPEC option B1 — the same helper the automatic
            // companion material and materials.createFromImage use.
            action = new QAction(QIcon(), "Create Material from Image", this);
            connect(action, SIGNAL(triggered()), this, SLOT(createMaterialFromImage()));
            menu.addAction(action);
        }

        if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Sky)) {
            action = new QAction(QIcon(), "Export Sky", this);
            connect(action, SIGNAL(triggered()), this, SLOT(exportSky()));
            menu.addAction(action);
        }

		if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Material)) {
			action = new QAction(QIcon(), "Export Material", this);
			connect(action, SIGNAL(triggered()), this, SLOT(exportMaterial()));
			menu.addAction(action);

            if (QGuiApplication::queryKeyboardModifiers() == Qt::ShiftModifier) {
                action = new QAction(QIcon(), "Save Material Preview", this);
                connect(action, SIGNAL(triggered()), this, SLOT(exportMaterialPreview()));
                menu.addAction(action);
            }
		}

        if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Shader) ||
            item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::File))
        {
            action = new QAction(QIcon(), "Edit", this);
            connect(action, SIGNAL(triggered()), this, SLOT(editFileExternally()));
            menu.addAction(action);
        }

		if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Shader)) {
            action = new QAction(QIcon(), "Export Shader", this);
            connect(action, SIGNAL(triggered()), this, SLOT(exportShader()));
            menu.addAction(action);
		}

		// AVATARS (AVATAR_ASSET_SPEC §5.5). The drawer is the PROJECT's view of
		// the world, so everything here works on the project's version: Edit
		// opens it in project scope, Update from Library re-pins to the
		// library's current one (discarding this project's edits), and Save to
		// Library publishes this project's version back. That is the owner's
		// model spelled out in four menu rows.
		if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Avatar)) {
			const QString avatarGuid = item->data(MODEL_GUID_ROLE).toString();
			action = new QAction(QIcon(), "Edit in Avatar Module", this);
			connect(action, SIGNAL(triggered()), this, SLOT(editAvatarInModule()));
			menu.addAction(action);

			action = new QAction(QIcon(), "Add to Scene", this);
			connect(action, SIGNAL(triggered()), this, SLOT(addAvatarToScene()));
			menu.addAction(action);

			action = new QAction(QIcon(), "Update from Library", this);
			// Enabled only when there is something to take: the project's pin
			// differs from the library's current version. A row that would do
			// nothing is worse than one that is not there.
			action->setEnabled(AvatarAssets::isEdited(avatarGuid, project));
			connect(action, SIGNAL(triggered()), this, SLOT(updateAvatarFromLibrary()));
			menu.addAction(action);

			action = new QAction(QIcon(), "Save to Library", this);
			connect(action, SIGNAL(triggered()), this, SLOT(saveAvatarToLibrary()));
			menu.addAction(action);
		}
		else if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Object)
		         && AssetMetadata::ensure(db, item->data(MODEL_GUID_ROLE).toString())
		                .value(QStringLiteral("hasSkeleton")).toBool()) {
			action = new QAction(QIcon(), "Create Avatar", this);
			connect(action, SIGNAL(triggered()), this, SLOT(createAvatarFromModel()));
			menu.addAction(action);
		}

		// THE IMPORT DECISION, REOPENED (SPECS/IMPORT_DIALOG_SPEC.md §8) —
		// model rows only. It is the only way to change how big an asset is:
		// the size is baked in and every placement is at scale 1.
		if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Object)) {
			const QString objectGuid = item->data(MODEL_GUID_ROLE).toString();
			action = new QAction(QIcon(), "Reimport\u2026", this);
			connect(action, &QAction::triggered, this, [this, objectGuid]() {
				emit reimportAssetRequested(objectGuid);
			});
			menu.addAction(action);
		}

		action = new QAction(QIcon(), "Delete", this);
		connect(action, SIGNAL(triggered()), this, SLOT(deleteItem()));
		menu.addAction(action);
	}
	else {
		QMenu *createMenu = menu.addMenu("Create");

		// CREATE MATERIAL, ON THE BACKGROUND (DRAWERS-1; the owner: "right
		// click > Create Material — I think we also need this in the asset
		// drawer of the editor"). The image row's "Create Material from Image"
		// is the other half and is on the image itself, where it belongs.
		action = new QAction(QIcon(), tr("Material"), this);
		connect(action, SIGNAL(triggered()), this, SLOT(createMaterial()));
		createMenu->addAction(action);

        action = new QAction(QIcon(), "Sky", this);
        connect(action, SIGNAL(triggered()), this, SLOT(createSky()));
        createMenu->addAction(action);

        action = new QAction(QIcon(), "New Folder", this);
		connect(action, SIGNAL(triggered()), this, SLOT(createFolder()));
		createMenu->addAction(action);

		action = new QAction(QIcon(), "Import Asset", this);
		connect(action, SIGNAL(triggered()), this, SLOT(importAssetB()));
		menu.addAction(action);

	}

	menu.exec(ui->assetView->mapToGlobal(pos));
}

void AssetWidget::assetViewClicked(QListWidgetItem *item)
{
    assetItem.wItem = item;
    emit assetItemSelected(item);
}

void AssetWidget::syncTreeAndView(const QString &path)
{
	QTreeWidgetItemIterator it(ui->assetTree);
	while (*it) {
		if ((*it)->data(0, MODEL_GUID_ROLE).toString() == path) {
			ui->assetTree->clearSelection();
			(*it)->setSelected(true);
			ui->assetTree->expandItem((*it));
			ui->assetTree->scrollToItem((*it));
			break;
		}

		++it;
	}
}

void AssetWidget::assetViewDblClicked(QListWidgetItem *item)
{
    if (item->data(MODEL_ITEM_TYPE) == MODEL_ASSET) {
        //if (item->data(MODEL_TYPE_ROLE) == static_cast<int>(ModelTypes::Shader)) {
        //    editFileExternally();
        //}

        //if (item->data(MODEL_TYPE_ROLE) == static_cast<int>(ModelTypes::File)) {
        //    editFileExternally();
        //}

        //// Maybe  have an internal viewer?
        //if (item->data(MODEL_TYPE_ROLE) == static_cast<int>(ModelTypes::Texture)) {
        //    QDesktopServices::openUrl(QUrl(
        //        IrisUtils::join(
        //            project->getProjectFolder(), "Textures",
        //            db->fetchAsset(item->data(MODEL_GUID_ROLE).toString()).name
        //        )
        //    ));
        //}

        // If item has dependencies
        const QString guid = item->data(MODEL_GUID_ROLE).toString();
        if (!db->hasDependencies(guid)) return;
        assetItem.selectedGuid = guid;
        updateAssetContentsView(guid);
        goUpOneControl->setEnabled(true);
        //syncTreeAndView(guid);
    } else if (item->data(MODEL_ITEM_TYPE) == MODEL_FOLDER) {
        const QString guid = item->data(MODEL_GUID_ROLE).toString();
        assetItem.selectedGuid = guid;
        updateAssetView(guid, activeFilter);
        syncTreeAndView(guid);
    }
}

void AssetWidget::renameViewItem()
{
	ui->assetView->editItem(assetItem.wItem);
}

void AssetWidget::favoriteItem()
{
    mainWindow->favoriteItem(assetItem.wItem);
}

void AssetWidget::refreshThumbnail()
{
    mainWindow->refreshThumbnail(assetItem.wItem);
}

void AssetWidget::editFileExternally()
{
	//for (auto asset : AssetManager::getAssets()) {
 //       if (asset->type == ModelTypes::File) {
 //           if (asset->fileName == assetItem.wItem->text()) {
 //               auto editor = SettingsManager::getDefaultManager()->getValue("editor_path", "");
 //               if (!editor.toString().isEmpty()) {
 //                   QProcess *process = new QProcess(this);
 //                   QStringList argument;
 //                   argument << asset->path;
 //                   process->start(editor.toString(), argument);
 //               }
 //               else {
 //                   QDesktopServices::openUrl(QUrl(asset->path));
 //               }
 //           }
 //       }
	//	else if (asset->type == ModelTypes::Shader) {
			//if (asset->fileName == assetItem.wItem->text()) {
   //             auto editor = SettingsManager::getDefaultManager()->getValue("editor_path", "");
   //             if (!editor.toString().isEmpty()) {
   //                 QProcess *process = new QProcess(this);
   //                 QStringList argument;
   //                 argument << asset->path;
   //                 process->start(editor.toString(), argument);
   //             }
   //             else {
   //                 QDesktopServices::openUrl(QUrl(asset->path));
   //             }
			//}
	//	}
	//}
}

void AssetWidget::createMaterialFromImage()
{
    if (!assetItem.wItem) return;
    const QString textureGuid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();

    QString error;
    const QString materialGuid =
        ImageMaterial::createMaterialAsset(textureGuid, db, project, &error);
    if (materialGuid.isEmpty()) {
        QMessageBox::warning(this, tr("Create Material from Image"),
                             tr("Could not create the material: %1").arg(error));
        return;
    }
    // Project context: pin it in so it lands in the bin, session-registered
    // and immediately droppable onto meshes.
    if (project && !project->getProjectGuid().isEmpty())
        ProjectAssets::addToProject(materialGuid, db, project, ProjectAssets::AddKind::Direct);

    // THE TILE IS A RENDER OF THE MATERIAL (THUMBS-1): the mint stores the
    // image as a fallback and one gesture can afford one render.
    thumbrebuild::rebuildOne(db, project, materialGuid, EngineHost::instance().engine());

    updateAssetView(assetItem.selectedGuid);
}

// --- AVATARS (AVATAR_ASSET_SPEC §5.5) --------------------------------------
//
// Every one of these is a thin view over a VERB or over the AvatarAssets
// service the verb calls — the drawer never grows a second implementation of
// the model (which is exactly what the materials module's "Add to project"
// did, and why this spec says mirror the UX, not the implementation).

void AssetWidget::editAvatarInModule()
{
    if (!assetItem.wItem) return;
    emit editAssetInModule(assetItem.wItem->data(MODEL_GUID_ROLE).toString(),
                           QStringLiteral("avatar"), QStringLiteral("project"));
}

void AssetWidget::addAvatarToScene()
{
    if (!assetItem.wItem) return;
    emit spawnAvatarInScene(assetItem.wItem->data(MODEL_GUID_ROLE).toString());
}

void AssetWidget::createAvatarFromModel()
{
    if (!assetItem.wItem) return;
    const QString objectGuid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();

    QString error;
    // PROJECT scope: a drawer row is a project member, so the avatar minted
    // from it belongs to this project until the user publishes it (D6).
    const QString avatarGuid = AvatarAssets::create(objectGuid, AvatarAssets::Scope::Project, db,
                                                    project, QString(), &error);
    if (avatarGuid.isEmpty()) {
        QMessageBox::warning(this, tr("Create Avatar"),
                             tr("Could not create the avatar: %1").arg(error));
        return;
    }
    updateAssetView(assetItem.selectedGuid);
    emit editAssetInModule(avatarGuid, QStringLiteral("avatar"), QStringLiteral("project"));
}

void AssetWidget::updateAvatarFromLibrary()
{
    if (!assetItem.wItem) return;
    const QString guid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();
    // CONFIRMED, because it throws away work: the project's own version of
    // this avatar is replaced by the library's, and the pin is the only thing
    // that named it.
    if (QMessageBox::question(
            this, tr("Update from Library"),
            tr("Replace this project's version of the avatar with the library's current one?\n\n"
               "Any edits made to it inside this project will no longer be used."))
        != QMessageBox::Yes)
        return;
    if (!ProjectAssets::updatePinToLatest(guid, db, project)) {
        QMessageBox::warning(this, tr("Update from Library"),
                             tr("The project's pin could not be updated."));
        return;
    }
    if (services && services->assets) services->assets->announcePinChanged(guid);
    updateAssetView(assetItem.selectedGuid);
}

void AssetWidget::saveAvatarToLibrary()
{
    if (!assetItem.wItem) return;
    const QString guid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();
    QString error;
    if (AvatarAssets::saveToLibrary(guid, db, project, &error).isEmpty()) {
        QMessageBox::warning(this, tr("Save to Library"),
                             tr("Could not publish the avatar: %1").arg(error));
        return;
    }
    updateAssetView(assetItem.selectedGuid);
}

void AssetWidget::exportTexture()
{
    // get the export file path from a save dialog
    auto filePath = QFileDialog::getSaveFileName(
        this,
        "Choose export path",
        assetItem.wItem->data(Qt::DisplayRole).toString() + "_texture",
        "Supported Export Formats (*.jaf)"
    );

    if (filePath.isEmpty() || filePath.isNull()) return;

    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid()) return;

    const QString writePath = temporaryDir.path();

    const QString guid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();

    db->createBlobFromAsset(guid, QDir(writePath).filePath("asset.db"));

    QDir tempDir(writePath);
    tempDir.mkpath("assets");

    QFile manifest(QDir(writePath).filePath(".manifest"));
    if (manifest.open(QIODevice::ReadWrite)) {
        QTextStream stream(&manifest);
        stream << "texture";
    }
    manifest.close();

    QStringList members = db->fetchAssetGUIDAndDependencies(guid);
    auto shaderGuid = QJsonDocument::fromJson(db->fetchAssetData(guid)).object()["guid"].toString();
    bool exportCustomShader = false;
    QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
    while (it.hasNext()) {
        it.next();
        if (it.key() != shaderGuid) {
            exportCustomShader = true;
            break;
        }
    }
    if (exportCustomShader) members.append(db->fetchAssetGUIDAndDependencies(shaderGuid));
    copyMemberFilesForExport(members, writePath);

    // ONE zip loop (amendment 7): the shared helper replaces the
    // hand-rolled zip_entry sweep this site duplicated.
    ZipHelper::zipDirectory(writePath, filePath);
}

void AssetWidget::exportMaterial()
{
	// get the export file path from a save dialog
	auto filePath = QFileDialog::getSaveFileName(
		this,
		"Choose export path",
        assetItem.wItem->data(Qt::DisplayRole).toString() + "_material",
		"Supported Export Formats (*.jaf)"
	);

	if (filePath.isEmpty() || filePath.isNull()) return;

	QTemporaryDir temporaryDir;
	if (!temporaryDir.isValid()) return;

	const QString writePath = temporaryDir.path();
	const QString guid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();

	db->createBlobFromAsset(guid, QDir(writePath).filePath("asset.db"));

	QDir tempDir(writePath);
	tempDir.mkpath("assets");

	QFile manifest(QDir(writePath).filePath(".manifest"));
	if (manifest.open(QIODevice::ReadWrite)) {
		QTextStream stream(&manifest);
		stream << "material";
	}
	manifest.close();

    QStringList members = db->fetchAssetGUIDAndDependencies(guid);
    auto shaderGuid = QJsonDocument::fromJson(db->fetchAssetData(guid)).object()["guid"].toString();
    bool exportCustomShader = false;
    QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
    while (it.hasNext()) {
        it.next();
        if (it.key() != shaderGuid) {
            exportCustomShader = true;
            break;
        }
    }
    if (exportCustomShader) members.append(db->fetchAssetGUIDAndDependencies(shaderGuid));
    copyMemberFilesForExport(members, writePath);

    // ONE zip loop (amendment 7): the shared helper replaces the
    // hand-rolled zip_entry sweep this site duplicated.
    ZipHelper::zipDirectory(writePath, filePath);
}

void AssetWidget::exportMaterialPreview()
{
    auto assetGuid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();
    auto materialDef = QJsonDocument::fromJson(db->fetchAssetData(assetGuid)).object();

    QString jsonMaterialString = QJsonDocument(materialDef).toJson();

    for (const auto &value : materialDef) {
        if (value.isString() &&
            !db->fetchAsset(value.toString()).name.isEmpty() &&
            value.toString() != materialDef["guid"].toString())
        {
            jsonMaterialString.replace(value.toString(), QString(db->fetchAsset(value.toString()).name));
        }
    }

    QJsonDocument saveDoc = QJsonDocument::fromJson(jsonMaterialString.toUtf8());

    QString fileName = IrisUtils::join(
        project->getProjectFolder(),
        IrisUtils::buildFileName(db->fetchAsset(assetGuid).name, "material")
    );

    QFile file(fileName);
    file.open(QFile::WriteOnly);
    file.write(saveDoc.toJson());
    file.close();

    ThumbnailGenerator::getSingleton()->requestThumbnail(
        ThumbnailRequestType::Material, fileName, assetGuid, true
    );

    //QFile::remove(fileName);
}

void AssetWidget::exportShader()
{
    // get the export file path from a save dialog
    auto filePath = QFileDialog::getSaveFileName(
        this,
        "Choose export path",
        assetItem.wItem->data(Qt::DisplayRole).toString(),
        "Supported Export Formats (*.jaf)"
    );

    if (filePath.isEmpty() || filePath.isNull()) return;

    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid()) return;

    const QString writePath = temporaryDir.path();

    const QString guid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();

    db->createBlobFromAsset(guid, QDir(writePath).filePath("asset.db"));

    QDir tempDir(writePath);
    tempDir.mkpath("assets");

    QFile manifest(QDir(writePath).filePath(".manifest"));
    if (manifest.open(QIODevice::ReadWrite)) {
        QTextStream stream(&manifest);
        stream << "shader";
    }
    manifest.close();

    for (const auto &assetGuid : AssetHelper::fetchAssetAndAllDependencies(guid, db)) {
        QString name;
        const QString assetPath = resolvePinnedAssetPath(project, assetGuid, &name);
        if (assetPath.isEmpty()) continue;
        if (name.isEmpty()) name = db->fetchAsset(assetGuid).name;
        if (name.isEmpty()) name = QFileInfo(assetPath).fileName();
        QFile::copy(assetPath, IrisUtils::join(writePath, "assets", name));
    }

    // ONE zip loop (amendment 7): the shared helper replaces the
    // hand-rolled zip_entry sweep this site duplicated.
    ZipHelper::zipDirectory(writePath, filePath);
}

void AssetWidget::exportAssetPack()
{
    QDateTime currentDateTime = QDateTime::currentDateTimeUtc();
     // get the export file path from a save dialog
    auto filePath = QFileDialog::getSaveFileName(
        this,
        "Choose export path",
        QString("AssetBundle_%1").arg(QString::number(currentDateTime.toSecsSinceEpoch())),
        "Supported Export Formats (*.jaf)"
    );

    if (filePath.isEmpty() || filePath.isNull()) return;

    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid()) return;

    const QString writePath = temporaryDir.path();

    QStringList assetGuids;
    for (const auto &item : ui->assetView->selectedItems()) {
        assetGuids << item->data(MODEL_GUID_ROLE).toString();
    }

    db->createExportBundle(assetGuids, QDir(writePath).filePath("asset.db"));

    QDir tempDir(writePath);
    tempDir.mkpath("assets");

    QFile manifest(QDir(writePath).filePath(".manifest"));
    if (manifest.open(QIODevice::ReadWrite)) {
        QTextStream stream(&manifest);
        stream << "bundle\n";
        for (const auto &item : assetGuids) stream << item << "\n";
    }
    manifest.close();

    for (const auto &guid : assetGuids) {
        QDir assetDir(QDir(writePath).filePath("assets"));
        assetDir.mkpath(guid);

        for (const auto &assetGuid : AssetHelper::fetchAssetAndAllDependencies(guid, db)) {
            QString name;
            const QString assetPath = resolvePinnedAssetPath(project, assetGuid, &name);
            if (name.isEmpty()) name = db->fetchAsset(assetGuid).name;
            if (name.isEmpty()) name = QFileInfo(assetPath).fileName();

            if (!assetPath.isEmpty()) {
                QFile::copy(
                    IrisUtils::join(assetPath),
                    IrisUtils::join(assetDir.absolutePath(), guid, name)
                );
            }
        }
    }

    // ONE zip loop (amendment 7): the shared helper replaces the
    // hand-rolled zip_entry sweep this site duplicated.
    ZipHelper::zipDirectory(writePath, filePath);
}

void AssetWidget::searchAssets(QString searchString)
{
	// Type-to-search filter (was an empty stub — any query showed NOTHING):
	// shows every matching asset in the currently selected folder and its
	// subfolders, honoring the type filter combo. Clearing the box restores
	// the plain folder view.
	const QString needle = searchString.trimmed();
	if (needle.isEmpty()) {
		updateAssetView(assetItem.selectedGuid, activeFilter);
		return;
	}

	ui->assetView->clear();
	std::function<void(const QString &)> addMatches = [&](const QString &folderGuid) {
		for (const auto &folder : db->fetchChildFolders(folderGuid, project->getProjectGuid())) {
			if (folder.name.contains(needle, Qt::CaseInsensitive)) addItem(folder);
			addMatches(folder.guid);
		}
		// The TRAY listing of each folder (services/assettray.h), so a search
		// finds exactly the tiles the folders show — the root's pinned members
		// included, which a raw folder query never returned.
		for (const auto &asset : assettray::list(db, project->getProjectGuid(), folderGuid,
		                                         activeFilter)) {
			if (asset.name.contains(needle, Qt::CaseInsensitive)) addItem(asset);
		}
	};
	addMatches(assetItem.selectedGuid);
}

void AssetWidget::OnLstItemsCommitData(QWidget *listItem)
{
	QString newName = qobject_cast<QLineEdit*>(listItem)->text();
	const QString guid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();
	const QString oldName = db->fetchAsset(guid).name;

    if (!newName.isEmpty()) {
        if (assetItem.wItem->data(MODEL_ITEM_TYPE) == MODEL_ASSET) {
			QString newFileName = IrisUtils::buildFileName(newName, QFileInfo(oldName).suffix());
            db->renameAsset(guid, newFileName);
			QFile assetToRename(QDir(project->getProjectFolder()).filePath(oldName));
			//if (!assetToRename.exists()) return;
			if (!assetToRename.rename(QDir(project->getProjectFolder()).filePath(newFileName))) {
				if (rename(
					QDir(project->getProjectFolder()).filePath(oldName).toStdString().c_str(),
					QDir(project->getProjectFolder()).filePath(newFileName).toStdString().c_str()
				)) {
					for (auto &asset : AssetManager::getAssets()) {
                        if (asset->assetGuid == guid) {
                            asset->fileName = newFileName;
                            if (!asset->path.isEmpty()) {
                                asset->path = QDir(project->getProjectFolder()).filePath(newFileName);
                            }
                        }
					}
				}
            }
            else {
                for (auto &asset : AssetManager::getAssets()) {
                    if (asset->assetGuid == guid) {
                        asset->fileName = newFileName;
                        if (!asset->path.isEmpty()) {
                            asset->path = QDir(project->getProjectFolder()).filePath(newFileName);
                        }
                    }
                }
            }
        }
        else {
            // THE FOLDER MODEL'S RULE (DRAWERS-1): a system folder and a
            // duplicate under the same parent are refused, with the same
            // sentence a script gets. A bare `db->renameFolder` was here.
            const projectfolders::Result result =
                projectfolders::rename(db, project->getProjectGuid(), guid, newName);
            if (!result.ok) {
                toast()->showToast(tr("Rename"), result.error, 3000);
                refresh();   // the tile is showing the name the user typed
            }
            // (A rename that took repopulates from the model's announcement.)
        }
    }
}

void AssetWidget::deleteTreeFolder()
{
	// THE SAME DELETE THE FOLDER TILE'S IS (DRAWERS-1): the folder row goes and
	// everything in it moves up to its parent, through the one folder model.
	// What was here removed a DIRECTORY RECURSIVELY from disk — `QDir(assetItem
	// .selectedPath).removeRecursively()` — on a path the tree never sets (no
	// tree item carries Qt::UserRole), for a project layout that has not
	// existed since reference-with-pin: a project holds no asset files. It
	// deleted nothing because the path was always empty, and it would have
	// deleted a user's directory the day anything filled it in.
	if (!db || !project || !assetItem.item) return;
	const QString guid = assetItem.item->data(0, MODEL_GUID_ROLE).toString();
	// WHERE THE USER ENDS UP. The deleted folder may be the one the view is
	// listing, and `selectedGuid` left pointing at a row that no longer exists
	// lists nothing at all until the user clicks somewhere — an empty tray that
	// looks like a data loss. The parent is where the contents went, so it is
	// where the user goes.
	const QString parent = db->fetchFolder(guid).parent;
	const projectfolders::Result result =
		projectfolders::remove(db, project->getProjectGuid(), guid, true);
	if (!result.ok) {
		toast()->showToast(tr("Delete Folder"), result.error, 3000);
		return;
	}
	const QString landing = parent.isEmpty() ? project->getProjectGuid() : parent;
	assetItem.selectedGuid = landing;
	refresh();
	syncTreeAndView(landing);
}

void AssetWidget::deleteItem()
{
	auto item = assetItem.wItem;

	// (THE FOLDER BRANCH IS GONE — DRAWERS-1. A folder tile has its own two
	// menu rows now and never reaches this function; what stood here ran
	// `deleteFolderAndDependencies`, a LIBRARY delete of every row filed in the
	// folder, from a view of ONE project — the opposite of the click, and the
	// same defect the module's project drawer had fixed in its own copy.)

	// Remove a PINNED library asset from THIS project (code review 2026-09-10):
	// the panel's Delete used to run the library delete, which under the pin
	// law unlisted the library tile and left the project untouched — the
	// opposite of the click. The project-side remove drops the project's pins
	// (assets.removeFromProject — the same service); the library keeps the row.
	if (item->data(MODEL_ITEM_TYPE).toInt() == MODEL_ASSET && project &&
	    db->countAssetPins(item->data(MODEL_GUID_ROLE).toString()) > 0) {
		const auto outcome = assetdelete::removeFromProject(
			db, item->data(MODEL_GUID_ROLE).toString(), project->getProjectGuid());
		if (!outcome.ok) qWarning("project panel: %s", qPrintable(outcome.error));
		updateAssetView(assetItem.selectedGuid, activeFilter);
		populateAssetTree(false);
		return;
	}

	// Delete asset and dependencies (a legacy project-scoped row, never pinned)
	if (item->data(MODEL_ITEM_TYPE).toInt() == MODEL_ASSET) {
		QStringList dependentAssets;
		for (const auto &files :
             db->fetchAssetGUIDAndDependencies(item->data(MODEL_GUID_ROLE).toString()))
        {
			dependentAssets.append(files);
		}

        // If a asset is single, remove it
        // If an asset has multiple dependers, warn
        // If an asset has dependencies that have multiple dependers, warn

        QStringList otherDependers;
        QStringList assetWithDeps;

        for (const auto &asset : dependentAssets) {
            auto dependers = db->hasMultipleDependers(asset);
            if (dependers.count() > 1) {
                otherDependers.append(dependers);
                assetWithDeps.append(asset);
            }
        }

        // Don't warn if it's a single asset, just break stuff
        if (assetWithDeps.isEmpty()) {
            // do a normal delete and return
            for (const auto &files : db->deleteAssetAndDependencies(item->data(MODEL_GUID_ROLE).toString())) {
                auto file = QFileInfo(QDir(project->getProjectFolder()).filePath(files));
                if (file.isFile() && file.exists()) QFile(file.absoluteFilePath()).remove();
            }

            updateAssetView(assetItem.selectedGuid, activeFilter);
            populateAssetTree(false);
            return;
        }

		QListWidget *assetsToRemove = new QListWidget;

        bool assetHasDependencies = db->hasDependencies(item->data(MODEL_GUID_ROLE).toString());
		
        if (assetHasDependencies) {
            QStringListIterator it(dependentAssets);
            int iter = 0;
            while (it.hasNext()) {
                auto guid = it.next();
                QListWidgetItem *listItem = new QListWidgetItem(db->fetchAsset(guid).name, assetsToRemove);
                listItem->setData(Qt::UserRole, guid);
                if (!iter || db->fetchAsset(guid).type == static_cast<int>(ModelTypes::Mesh)) {
                    listItem->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                }
                if (assetWithDeps.contains(guid)) {
                    listItem->setCheckState(Qt::Unchecked);
                }
                else {
                    listItem->setCheckState(Qt::Checked);
                }
                assetsToRemove->addItem(listItem);
                iter++;
            }
        }
        else {
            QStringListIterator it(otherDependers);
            int iter = 0;
            while (it.hasNext()) {
                auto guid = it.next();
                QListWidgetItem *listItem = new QListWidgetItem(db->fetchAsset(guid).name, assetsToRemove);
                listItem->setFlags(item->flags() & ~Qt::ItemIsSelectable);
                listItem->setData(Qt::UserRole, guid);
                assetsToRemove->addItem(listItem);
                iter++;
            }
        }

		QDialog dialog;
		dialog.setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
		dialog.setWindowTitle("Dependent Assets");

        QLabel *textLabel = new QLabel;

        if (assetHasDependencies) {
            textLabel->setText(
                "The assets below will be deleted as dependencies.\n"
                "Unticked items are being used with other assets, select them to remove them as well."
            );
        }
        else {
            textLabel->setText(
                "The assets below are dependent on this asset.\n"
                "If you choose to continue removing this asset, those assets will be affected."
            );
        }

		auto layout = new QVBoxLayout;
		dialog.setLayout(layout);

		layout->addWidget(textLabel);
		layout->addSpacing(8);
		layout->addWidget(assetsToRemove);

		auto blayout = new QHBoxLayout;
		auto bwidget = new QWidget;
		bwidget->setLayout(blayout);
		QPushButton *deleteSelected = new QPushButton("Delete Selected");
		QPushButton *cancel = new QPushButton("Cancel");
		blayout->addStretch(1);
		blayout->addWidget(deleteSelected);
		blayout->addWidget(cancel);
		layout->addWidget(bwidget);

        if (!assetHasDependencies) {
            connect(deleteSelected, &QPushButton::pressed, this, [&]() {
                dialog.close();

                for (const auto &files : db->deleteAssetAndDependencies(item->data(MODEL_GUID_ROLE).toString())) {
                    auto file = QFileInfo(QDir(project->getProjectFolder()).filePath(files));
                    if (file.isFile() && file.exists()) QFile(file.absoluteFilePath()).remove();
                }

                //delete ui->assetView->takeItem(ui->assetView->row(item));
                updateAssetView(assetItem.selectedGuid, activeFilter);
                populateAssetTree(false);
            });
        }
        else {
            connect(deleteSelected, &QPushButton::pressed, this, [&]() {
                dialog.close();

                for (int i = 0; i < assetsToRemove->count(); ++i) {
                    QListWidgetItem *item = assetsToRemove->item(i);
                    auto itemGuid = item->data(Qt::UserRole).toString();

                    if (item->checkState() == Qt::Checked) {
                        db->deleteAsset(itemGuid);
                        db->deleteDependency(item->data(MODEL_GUID_ROLE).toString(), itemGuid);

                        auto file = QFileInfo(QDir(project->getProjectFolder()).filePath(db->fetchAsset(itemGuid).name));
                        if (file.isFile() && file.exists()) QFile(file.absoluteFilePath()).remove();
                    }
                }

                //delete ui->assetView->takeItem(ui->assetView->row(item));
                updateAssetView(assetItem.selectedGuid, activeFilter);
                populateAssetTree(false);
            });
        }

		connect(cancel, &QPushButton::pressed, this, [&dialog]() {
			dialog.close();
		});

		dialog.setStyleSheet(StyleSheet::AssetWidgetTagDialog());

		dialog.exec();
	}
}

void AssetWidget::createSky()
{
    QListWidgetItem *item = new QListWidgetItem;
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    item->setSizeHint(currentSize);
    item->setTextAlignment(Qt::AlignCenter);
    item->setIcon(QIcon(":/icons/icons8-file-sky.png"));

    const QString assetGuid = GUIDManager::generateGUID();

    item->setData(MODEL_GUID_ROLE, assetGuid);
    item->setData(MODEL_PARENT_ROLE, assetItem.selectedGuid);
    item->setData(MODEL_ITEM_TYPE, MODEL_ASSET);
    item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Sky));
    item->setData(SKY_TYPE_ROLE, static_cast<int>(iris::SkyType::SINGLE_COLOR));

	QJsonObject properties;
	QJsonObject skyProps;
	skyProps.insert("type", item->data(SKY_TYPE_ROLE).toInt());
	properties.insert("sky", skyProps);

	QJsonObject skyDescription;
	// Need to leave the defaut sky properties empty, the widget will set it
	//skyDescription.insert("guid", assetGuid);
	//skyDescription.insert("skyColor", SceneWriter::jsonColor(QColor(255, 255, 255, 255)));

	db->createAssetEntry(
		assetGuid,
		"Sky",
		static_cast<int>(ModelTypes::Sky),
		project->getProjectGuid(),
		project->getProjectGuid(),
		QString(),
		QString(),
		AssetHelper::makeBlobFromPixmap(QPixmap(":/icons/icons8-file-sky.png")),
        QJsonDocument(properties).toJson(),
		QByteArray(),
        QJsonDocument(skyDescription).toJson()
	);

    item->setText("Sky");
    ui->assetView->addItem(item);
}

void AssetWidget::createFolder()
{
	// ONE FOLDER MODEL (DRAWERS-1, services/projectfolders.h). What was here
	// was the model's second implementation: a fresh guid, a "New Folder N"
	// loop against `fetchFolderNameByParent`, a bare `db->createFolder` and a
	// hand-built tree branch. The name loop survives — it is the panel's job
	// to propose a name nothing else has — and the rule, the write and the
	// announcement are the verb's.
	if (!db || !project || project->getProjectGuid().isEmpty()) return;
	const QString parent = assetItem.selectedGuid.isEmpty() ? project->getProjectGuid()
	                                                        : assetItem.selectedGuid;

	// THE PROPOSED NAME, judged the way the model judges it: CASE-INSENSITIVELY
	// (projectfolders' nameTaken is, and this loop was not — "new folder"
	// beside "New Folder" proposed a name the create then refused).
	const QString base = tr("New Folder");
	QString folderName = base;
	const QStringList taken = db->fetchFolderNameByParent(parent);
	auto isTaken = [&taken](const QString &name) {
		for (const QString &other : taken)
			if (other.compare(name, Qt::CaseInsensitive) == 0) return true;
		return false;
	};
	int increment = 1;
	while (isTaken(folderName)) folderName = base + " " + QString::number(increment++);

	const projectfolders::Result judged =
		projectfolders::judgeCreate(db, project->getProjectGuid(), folderName, parent);
	if (!judged.ok) {
		toast()->showToast(tr("New Folder"), judged.error, 3000);
		return;
	}

	auto outcome = std::make_shared<FolderCommandOutcome>();
	auto *command = new CreateProjectFolderCommand(db, project->getProjectGuid(),
	                                               folderName, parent, outcome);
	if (services && services->undo) {
		services->undo->push(command);            // redo() runs the create
	} else {
		command->redo();
		delete command;
	}
	if (!outcome->error.isEmpty()) {
		toast()->showToast(tr("New Folder"), outcome->error, 3000);
		return;
	}

	// The repopulate comes from the model's announcement (item 10); the tree
	// selection is this panel's own business.
	syncTreeAndView(parent);
}

void AssetWidget::deleteFolderItem()
{
	// THE PROJECT-SIDE DELETE: the folder goes, everything in it moves up to
	// its parent (services/projectfolders.h). Nothing leaves the project and
	// nothing leaves the library, so there is no question to ask first.
	if (!assetItem.wItem || !db || !project) return;
	const projectfolders::Result result = projectfolders::remove(
		db, project->getProjectGuid(), assetItem.wItem->data(MODEL_GUID_ROLE).toString(), true);
	// (The tile's delete never lists the folder it deletes — the tile is IN the
	// folder's parent — so there is no selection to move, unlike the tree's.)
	if (!result.ok) {
		toast()->showToast(tr("Delete Folder"), result.error, 3000);
		return;
	}
	// The repopulate comes from the model's announcement (item 10).
}

void AssetWidget::createMaterial()
{
	// THE VERB, WITH THE FOLDER THE USER IS LOOKING AT (DRAWERS-1):
	// `materials.create(name, {folder})` mints the library bundle, pins it into
	// this project and files it here, and its one announcement repopulates this
	// panel AND the Materials module's project drawer — which is the owner's
	// "creating in the project should add it to the project drawer in Materials
	// automatically", made true by construction rather than by a second call.
	if (!db || !project || project->getProjectGuid().isEmpty()) return;
	if (!mainWindow || !mainWindow->scripting()) return;

	const QString folder = assetItem.selectedGuid.isEmpty() ? project->getProjectGuid()
	                                                        : assetItem.selectedGuid;
	MaterialsApi api(mainWindow->scripting()->scriptHost());
	const QString name = MaterialBundle::uniqueName(db, tr("New Material"));
	const QString guid = api.quietly([&] {
		return api.create(name, QVariantMap{ { QStringLiteral("folder"), folder } });
	});
	if (guid.isEmpty()) {
		QMessageBox::warning(this, tr("Create Material"),
		                     tr("The material could not be created: %1").arg(api.lastError()));
		return;
	}
	// (NO rebuildOne HERE: `materials.create` renders the tile itself on the
	// studio sphere since MATPREVIEW-ENV-1 — one gesture, one render, and the
	// panel asking for a second would draw the same sphere twice.)
	refresh();
	// Created AND SELECTED (the brief): opening it in the Materials module is
	// the user's double-click, not ours.
	for (int i = 0; i < ui->assetView->count(); ++i) {
		QListWidgetItem *item = ui->assetView->item(i);
		if (item->data(MODEL_GUID_ROLE).toString() != guid) continue;
		ui->assetView->setCurrentItem(item);
		assetItem.wItem = item;
		emit assetItemSelected(item);
		break;
	}
}

void AssetWidget::importAssetB()
{
	auto fileNames = QFileDialog::getOpenFileNames(this, "Import Asset");
	if (!fileNames.isEmpty()) importAsset(fileNames);
}

bool AssetWidget::importFiles(const QStringList &files)
{
	// A QUESTION IS AN IMPORT (see importAsset): the verb answers "already
	// importing" rather than starting a second batch behind the user's dialog.
	if (mAsking || (importRunner && importRunner->isRunning())) return false;
	importAsset(files, false);   // no modal question on a scripted import
	return true;
}

bool AssetWidget::shutdownImports(int msTimeout)
{
	if (progressDialog) progressDialog->close();
	// THE THUMBNAIL BACKLOG GOES WITH THEM (fix round F9). A pending
	// single-shot would run after MainWindow::shutdownBackgroundWork has
	// destroyed the thumbnail renderer and RE-CREATE it, on a window that is
	// already leaving — the drain re-arms itself, so one survivor is all of
	// them. Nothing is lost: a model with no thumbnail is what "Rebuild
	// missing thumbnails" is for.
	thumbnailBacklog.clear();
	if (!importRunner) return true;
	// waitForDone pumps events, which can delete the runner (its finished
	// handler deleteLater()s it) — hold it weakly and never touch the raw
	// member afterwards.
	QPointer<ImportBatchRunner> runner(importRunner);
	runner->requestAbort();
	if (runner->waitForDone(msTimeout)) return true;
	qWarning("AssetWidget: import worker still running after %dms", msTimeout);
	return false;
}

void AssetWidget::importAsset(const QStringList &fileNames, bool askImportSettings)
{
	// ONE pipeline + reference-with-pin (phases 3+4): a project-panel drop
	// is a library import through AssetImportService followed by a pin into
	// the open project. The old ~700 lines (recursive directory copies into
	// the store, Editor-filter ghost rows, commented-out project copies,
	// a second .jaf importer) died here.
	// Dropped directories expand to their files; each file imports on its
	// own merits (the pipeline stages exactly what each one references).
	QStringList expanded;
	for (const QString &fileName : fileNames) {
		if (QFileInfo(fileName).isDir()) {
			QDirIterator it(fileName, QDir::NoDotAndDotDot | QDir::Files,
			                QDirIterator::Subdirectories);
			while (it.hasNext()) expanded.append(it.next());
		} else {
			expanded.append(fileName);
		}
	}

	if (expanded.isEmpty()) return;

	// ONE IMPORT AT A TIME, AND AN OPEN QUESTION COUNTS AS ONE. `isRunning()` is
	// false while the modal dialog below is up, so without mAsking a scripted
	// editor.importAssets arriving during a user's dialog would replace the
	// runner and start it — and the outer call would then setRequests on a
	// RUNNING runner, connect every signal twice and start() again, which is a
	// Q_ASSERT(!mRunning) abort in a Debug build.
	if (mAsking || (importRunner && importRunner->isRunning())) return;

	QVector<ImportRequest> requests;
	for (const QString &fileName : expanded) {
		ImportRequest request;
		request.sourcePath = fileName;
		requests.append(request);
	}

	// THE IMPORT DECISION (SPECS/IMPORT_DIALOG_SPEC.md §8): one dialog per
	// MODEL file BEFORE anything is read and before any progress dialog is up —
	// a busy bar spinning under a question is a lie about what the app is
	// doing. Media never prompts; a skipped file drops out and the rest of the
	// drop still imports; "Skip the rest" drops the tail.
	QStringList modelFiles;
	if (askImportSettings)
		for (const ImportRequest &request : requests)
			if (isModelImportPath(request.sourcePath)) modelFiles.append(request.sourcePath);
	if (!modelFiles.isEmpty()) {
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
	if (requests.isEmpty()) return;          // every file was skipped

	// THREADED (UI-freeze fix): the pipeline's heavy half runs on
	// ImportBatchRunner's worker with one cancellable dialog for the whole
	// drop; the pin + panel refresh land back here per file / at the end.
	progressDialog->resetCancel();
	progressDialog->setCancelVisible(true);
	progressDialog->setRange(0, 0);
	progressDialog->setValue(0);
	progressDialog->setLabelText(tr("Preparing import…"));
	progressDialog->setStageText(QString());
	progressDialog->show();

	importRunner = new ImportBatchRunner(db, project, this);
	importRunner->setRequests(requests);

	connect(progressDialog, &ProgressDialog::canceled,
	        importRunner, &ImportBatchRunner::cancel);

	connect(importRunner, &ImportBatchRunner::fileStarted, this,
	        [this](int index, int total, const QString &name) {
		const QString counter =
		    total > 1 ? tr(" (%1 of %2)").arg(index + 1).arg(total) : QString();
		progressDialog->setLabelText(tr("Importing %1%2").arg(name, counter));
		progressDialog->setRange(0, 0);
	});
	connect(importRunner, &ImportBatchRunner::stageProgress, this,
	        [this](int, const QString &stage, int done, int total) {
		progressDialog->setStageText(
		    total > 0 ? QStringLiteral("%1 (%2/%3)…").arg(stage).arg(done + 1).arg(total)
		              : stage + QStringLiteral("…"));
		progressDialog->setRange(0, total);
		if (total > 0) progressDialog->setValue(done);
	});
	connect(importRunner, &ImportBatchRunner::fileFinished, this,
	        [this](int, const ImportRequest &request, const ImportResult &result) {
		if (!result.ok()) {
			if (result.error != QStringLiteral("cancelled"))
				importErrors.append(QStringLiteral("%1: %2").arg(
				    QFileInfo(request.sourcePath).fileName(), result.error));
			return;
		}
		const auto pinned = ProjectAssets::addToProject(result.assetGuid, db, project, ProjectAssets::AddKind::Direct);
		if (!pinned.ok()) importErrors.append(pinned.error);
		// THE SAME TAIL THE ASSETS PAGE RUNS (THUMBS-1 item 3): a model's
		// thumbnail is a render, and this path never asked for one.
		const int type = db ? db->fetchAsset(result.assetGuid).type : 0;
		if (type == static_cast<int>(ModelTypes::Object)
		    || type == static_cast<int>(ModelTypes::ParticleSystem))
			thumbnailBacklog.append(result.assetGuid);
		// …AND SO IS A COMPANION MATERIAL'S (fix round F9). Adding an image to
		// a project mints one, and its tile is a render of the material — but
		// a drop of a hundred images must not be a hundred synchronous renders
		// inside the import's commit hops, so they queue here like the models.
		for (const QString &companion : pinned.pinnedGuids) {
			if (companion == result.assetGuid || thumbnailBacklog.contains(companion)) continue;
			if (db && db->fetchAsset(companion).type == static_cast<int>(ModelTypes::Material))
				thumbnailBacklog.append(companion);
		}
	});
	connect(importRunner, &ImportBatchRunner::finished, this, [this](bool cancelled) {
		progressDialog->hide();
		auto *runner = importRunner;
		importRunner = nullptr;
		if (runner) runner->deleteLater();

		if (cancelled)
			progressDialog->setStageText(QString());
		if (!importErrors.isEmpty()) {
			QMessageBox::warning(this, tr("Import"),
			                     importErrors.join(QStringLiteral("\n")), QMessageBox::Ok);
			importErrors.clear();
		}
		populateAssetTree(false);
		updateAssetView(assetItem.selectedGuid, activeFilter);
		// After the dialog is gone and the tree is rebuilt: the renders.
		if (!thumbnailBacklog.isEmpty()) QTimer::singleShot(0, this, [this] { drainThumbnailBacklog(); });
	});

	importRunner->start();
}

void AssetWidget::drainThumbnailBacklog()
{
	if (thumbnailBacklog.isEmpty()) return;
	const QString guid = thumbnailBacklog.takeFirst();
	// THE ONE ROUTINE (services/thumbnailrebuild.h): a model from its stored
	// blob, fitted and framed; a companion material on the preview sphere —
	// whichever this row is. Its failures are logged by it; the tray has no
	// surface to put a message on, and an import that succeeded must not become
	// an error dialog because a tile is grey.
	const thumbrebuild::Outcome outcome =
	    thumbrebuild::rebuildOne(db, project, guid, EngineHost::instance().engine());

	// ONE TILE, NOT THE WHOLE TRAY (fix round F9). This used to call
	// updateAssetView per render — a full repopulate of the panel (the tray
	// query, every row's blob, every icon rebuilt) for one changed icon, once
	// per imported model.
	if (outcome.ok) {
		QPixmap thumbnail;
		if (thumbnail.loadFromData(db->fetchAsset(guid).thumbnail, "PNG")) {
			for (int i = 0; i < ui->assetView->count(); ++i) {
				QListWidgetItem *item = ui->assetView->item(i);
				if (item->data(MODEL_GUID_ROLE).toString() == guid) {
					item->setIcon(QIcon(thumbnail));
					break;
				}
			}
		}
	}
	// ONE PER EVENT-LOOP TURN: the window keeps painting between renders.
	if (!thumbnailBacklog.isEmpty()) QTimer::singleShot(0, this, [this] { drainThumbnailBacklog(); });
}

void AssetWidget::onThumbnailResult(const ThumbnailResult &result)
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);

    if (!result.preview) {
        // Store the render at full size (requests are 512x512): scaling it to
        // the icon height here permanently degraded every stored thumbnail to
        // 72px (ASSETS_AUDIT.md finding 5). Views scale at display time.
        auto thumbnail = QPixmap::fromImage(result.thumbnail);
        thumbnail.save(&buffer, "PNG");

        db->updateAssetThumbnail(result.id, bytes);

        // Refresh the view if we're still there
        for (int i = 0; i < ui->assetView->count(); i++) {
            QListWidgetItem* item = ui->assetView->item(i);
            if (item->data(MODEL_GUID_ROLE).toString() == result.id) {
                updateAssetView(assetItem.selectedGuid, activeFilter);
            }
        }
    }
    else {
        const QPixmap rendered = QPixmap::fromImage(result.thumbnail);
        if (rendered.isNull()) {
            // The render failed and said why in the log (THUMBS-1): the export
            // has nothing to write, and asking Qt to scale nothing only adds a
            // second, less useful warning.
            QMessageBox::warning(this, tr("Export Preview"),
                                 tr("Nothing was rendered for this preview."), QMessageBox::Ok);
            return;
        }
        auto thumbnail = rendered.scaledToHeight(512, Qt::SmoothTransformation);
        thumbnail.save(&buffer, "PNG");

        auto filePath = QFileDialog::getSaveFileName(
            this,
            "Choose image path",
            QString("%1_preview.png").arg(QFileInfo(result.path).baseName()),
            "Supported Image Formats (*.jpg, *.png)"
        );

        if (filePath.isEmpty() || filePath.isNull()) return;
        thumbnail.save(filePath);
    }
    // No delete: the payload is a value and the OTHER receiver
    // (EffectsPage::onShaderThumbnail) reads it after this slot returns.
}
