/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "shaderassetwidget.h"
#include "data/project.h"
#include <QSqlDatabase>
#include "services/assetcas.h"
#include "services/assetdelete.h"
#include "services/assettray.h"
#include "services/projectmembership.h"
#include "services/projectassets.h"
#include "services/assetstorepaths.h"
#include <QMenu>
#include <QEvent>
#include <QMouseEvent>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDebug>
#include <QTimer>


#include "irisgl/core/irisutils.h"
#include "../core/materialhelper.h"
#include "../models/properties.h"
#include <QStandardPaths>
#include <QDirIterator>

#include "../effectspage.h"
#if(EFFECT_BUILD_AS_LIB)
#include "io/assetmanager.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "ui/style/stylesheet.h"
#endif

ShaderAssetWidget::ShaderAssetWidget(Database *handle) : QWidget()
{

	layout = new QVBoxLayout;
	setLayout(layout);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	// (The breadcrumb row is GONE — DRAWERS-1's CRUD. It was an empty QHBoxLayout
	// in a widget of its own: nothing ever put a crumb in it, because nothing
	// here ever navigated into a folder.)

	assetViewWidget = new ShaderListWidget;
    assetViewWidget->shaderContextMenuAllowed = true;
	assetViewWidget->setGridSize({ 95,95 });
	connect(assetViewWidget, &ShaderListWidget::itemDropped, [=](QListWidgetItem *item) {
		addDroppedToProject(item);
	});
	connect(assetViewWidget, &ShaderListWidget::itemDoubleClicked, [=](QListWidgetItem *item) {
		emit loadToGraph(item);
	});

    if(handle) setUpDatabase(handle);

	stackWidget = new QStackedWidget;

	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(stackWidget);

	noWidget = new QWidget;
	auto noWidgetLayout = new QVBoxLayout;
	auto lableIcon = new QLabel;
	auto lableText = new QLabel;
	noWidget->setLayout(noWidgetLayout);
	noWidgetLayout->addWidget(lableIcon);
	noWidgetLayout->addWidget(lableText);
	lableText->setText("No Open projects");
	lableIcon->setPixmap(QPixmap(":/icons/icons8-empty-box-50.png"));
	lableIcon->setAlignment(Qt::AlignCenter);
	lableText->setAlignment(Qt::AlignCenter);
	noWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	closeBtn = new QPushButton("Hide Panel");
	noWidgetLayout->addWidget(closeBtn);

	stackWidget->addWidget(assetViewWidget);
	stackWidget->addWidget(noWidget);
	this->setWidgetToBeShown();
	configureConnections();

	// BOTH DRAWERS REFRESH FROM ONE SIGNAL (DRAWERS-1, the owner's "creating in
	// the project should add it to the project drawer in Materials
	// automatically; likewise adding a material to the project should add it to
	// the project assets"). ProjectMembership is the announcement every pin
	// change and every folder verb makes (services/projectmembership.h,
	// services/projectfolders.h) and the editor's asset tray has repopulated
	// from since lane L13 — this drawer was the one view that did not, so a
	// material minted or added anywhere else sat missing here until the user
	// switched space. Coalesced onto the next event-loop turn, like the tray's:
	// one gesture can pin a dozen rows.
	connect(ProjectMembership::instance(), &ProjectMembership::changed, this,
	        [this](const QString &projectGuid) {
		if (!project || project->getProjectGuid().isEmpty()) return;
		if (!projectGuid.isEmpty() && project->getProjectGuid() != projectGuid) return;
		if (mRefreshPending) return;
		mRefreshPending = true;
		QTimer::singleShot(0, this, [this]() { flushPendingRefresh(); });
	});
}

void ShaderAssetWidget::flushPendingRefresh()
{
	if (!mRefreshPending) return;
	mRefreshPending = false;
	refresh();
}

QVariantList ShaderAssetWidget::shownTiles()
{
	// What the user sees once the event loop turns: a repopulate the one
	// refresh signal queued lands first (a --script run holds the loop for its
	// whole run, exactly as AssetWidget::shownTiles has to handle).
	flushPendingRefresh();
	QVariantList out;
	for (int i = 0; i < assetViewWidget->count(); ++i) {
		const QListWidgetItem *item = assetViewWidget->item(i);
		out.append(QVariantMap{
			{ QStringLiteral("guid"), item->data(MODEL_GUID_ROLE).toString() },
			{ QStringLiteral("name"), item->data(Qt::UserRole).toString() },
		});
	}
	return out;
}


ShaderAssetWidget::~ShaderAssetWidget()
{
}

void ShaderAssetWidget::updateAssetView()
{
	assetViewWidget->clear();

	// No library or no project = nothing to list, and the stacked widget's
	// "no scene open" page is what the user sees (setWidgetToBeShown decides
	// which page that is). The truthful empty state, not a skipped refresh.
	//
	// THE PROJECT DRAWER *IS* THE EDITOR'S ASSET TRAY, filtered to materials
	// (the four-drawer rule, OWNER_REVIEW 9: "the project asset tray should
	// mirror the materials project drawer" — ONE LIST, TWO WINDOWS). So it
	// calls the function the tray calls — services/assettray.h — and carries
	// no listing rule of its own.
	//
	// FLAT, ACROSS EVERY FOLDER (DRAWERS-1). The tray can file a material in a
	// folder now; this drawer has no breadcrumb to navigate one with (the
	// folder code it used to carry was never wired to anything), so it shows
	// the project's materials wherever they are filed. A drawer that listed
	// only the root would silently lose a material the moment the user tidied
	// up in the editor.
	if (db && project && !project->getProjectGuid().isEmpty()) {
		for (const auto &asset : assettray::listAll(db, project->getProjectGuid(),
		                                            static_cast<int>(ModelTypes::Material)))
			addItem(asset);
	}

	setWidgetToBeShown();
}

// (addItem(FolderRecord) is DELETED — DRAWERS-1's CRUD. It built a folder tile
// this drawer never listed: nothing called it, because updateAssetView lists
// assets only. The drawer is flat by design now, and says so above.)
void ShaderAssetWidget::addItem(const AssetRecord & assetData)
{
    auto prop = QJsonDocument::fromJson(assetData.properties).object();
	if (!prop["type"].toString().isEmpty()) {
		// No need to check further, this is a builtin asset
		return;
	}

	// dont show item if its not a shader asset

    auto doc = QJsonDocument::fromJson(assetData.asset);
	auto obj = doc.object();
	auto name = obj["name"].toString();

	QListWidgetItem *item = new QListWidgetItem;
	item->setData(Qt::DisplayRole, QFileInfo(assetData.name).baseName());
	item->setData(Qt::UserRole, assetData.name);
	item->setData(MODEL_TYPE_ROLE, assetData.type);
	item->setData(MODEL_ITEM_TYPE, MODEL_ASSET);
	item->setData(MODEL_GUID_ROLE, assetData.guid);
	item->setData(MODEL_PARENT_ROLE, assetData.parent);
	ListWidget::updateThumbnailImage(assetData.thumbnail, item);

	item->setSizeHint(currentSize);
	item->setTextAlignment(Qt::AlignCenter);
	item->setFlags(item->flags() | Qt::ItemIsEditable);

	// Hide meshes for now, we work with objects which are parents for meshes, materials etc
	assetViewWidget->addItem(item);
}

void ShaderAssetWidget::setUpDatabase(Database * db)
{
	// THE HANDLE IS STORED UNCONDITIONALLY (lane DBPTR-1). It used to be kept
	// only when a scene was open at this moment — and the one caller is
	// EffectsPage's CONSTRUCTOR (MaterialsModule builds the page, THEN hands it
	// the scene-open probe), so `sceneOpenProbe` was still empty here every
	// time and `db` was never assigned at all. The member was uninitialised,
	// so every later `db->fetchChildAssets(...)` — one per switch into the
	// Materials space, through refresh() — ran on a wild pointer; it survived
	// only because that query touches no member of Database (the same accident
	// that hid AssetPanel::handle, CLOSE-2).
	this->db = db;
	//remove noWidget if preset and add assetViewWidget
	if (sceneOpenProbe && sceneOpenProbe() && project) {
		updateAssetView();
		assetItemShader.selectedGuid = project->getProjectGuid();
	}
}

void ShaderAssetWidget::refresh()
{
	updateAssetView();
}

void ShaderAssetWidget::setWidgetToBeShown()
{
	if (sceneOpenProbe && sceneOpenProbe()) {
		stackWidget->setCurrentIndex(0);
	}
	else {
		stackWidget->setCurrentIndex(1);

	}
}

void ShaderAssetWidget::configureConnections()
{
	connect(assetViewWidget, &ListWidget::renameShader, [=](QString guid) {
		assetViewWidget->editItem(assetViewWidget->currentItem());
	});
	connect(assetViewWidget, &ListWidget::exportShader, [=](QString guid) {

	});
	connect(assetViewWidget, &ListWidget::editShader, [=](QString guid) {
		emit loadToGraph(assetViewWidget->currentItem());
	});
	connect(assetViewWidget, &ListWidget::deleteShader, [=](QString guid) {
		deleteShader(guid);
	});
	connect(assetViewWidget->itemDelegate(), &QAbstractItemDelegate::commitData, [=]() {
		//item finished editing
		editingFinishedOnListItem(assetViewWidget->currentItem());
	});
}

void ShaderAssetWidget::deleteShader(QString guid)
{
	// DELETE FROM THE PROJECT DRAWER MEANS "TAKE IT OUT OF THIS PROJECT"
	// (MATERIAL_BUNDLE_SPEC phase 2's Deletes column — the module's private
	// asset code). What was here was 130 lines of the module's own delete: a
	// hand-rolled dependency dialog over `deleteAssetAndDependencies`, plus a
	// sweep that removed FILES from the project folder by display name. Every
	// premise of it is gone — a project holds no asset files (they are pins on
	// store objects), and the library-delete law says a row a project pins is
	// unlisted, never deleted. It also answered the wrong question: this is the
	// PROJECT drawer, and removing something from a project must not touch the
	// library at all.
	//
	// `assetdelete::removeFromProject` is the one implementation the editor's
	// tray Delete uses: it drops this project's pin and the pins of the members
	// only this bundle uses (a texture two materials share keeps its pin), and
	// reaps a row that was already unlisted and has just lost its last pin.
	if (!db || !project || project->getProjectGuid().isEmpty()) return;
	auto *item = assetViewWidget->currentItem();
	if (!item) return;

	// (THE FOLDER BRANCH IS GONE WITH THE FOLDER TILES — DRAWERS-1. This
	// drawer is FLAT: it lists the project's materials wherever they are
	// filed, so it shows no folder tile and this branch was unreachable. A
	// folder is deleted in the editor's asset tray, through
	// assets.deleteFolder / services/projectfolders.h, which does exactly what
	// this branch did by hand — and moves the contents up to the parent rather
	// than taking them out of the project, which is what a Delete on a FOLDER
	// should do. MATERIALS-TABS-1's `assetRemoved` is emitted for every row
	// that really leaves the project, below and in the tray's own delete.)
	const QString target = guid.isEmpty() ? item->data(MODEL_GUID_ROLE).toString() : guid;
	if (target.isEmpty()) return;
	assetdelete::removeFromProject(db, target, project->getProjectGuid());
	// AND THE PAGE IS TOLD (fix round F1): a '(project)' tab is editing this
	// project's copy, and the pin it reads and writes through has just gone.
	emit assetRemoved(target);
	refresh();
}

void ShaderAssetWidget::editingFinishedOnListItem(QListWidgetItem *item)
{
	// THE DRAWER DOES NOT RENAME ANYTHING (MATERIALS_TABS_SPEC §7). It used
	// to write the catalog row itself — `db->renameAsset` — which is neither
	// of the two things a rename is: it bypasses the ONE name writer (so it
	// would rename a shipped preset, or take a preset's name) and it never
	// touched the DEFINITION, so the stored name stayed behind and the next
	// save of that material put the old one straight back. The page owns the
	// rename, for every drawer; this says which row the user typed in.
	if (!db || !project) return;   // nothing to rename without a library
	const QString guid = item->data(MODEL_GUID_ROLE).toString();
	const QString newName = item->data(Qt::DisplayRole).toString();
	if (guid.isEmpty() || newName.isEmpty()) return;
	if (newName == db->fetchAsset(guid).name) return;
	emit assetRenamed(guid, newName);
}


// (createFolder() is DELETED — DRAWERS-1's CRUD. It was a SECOND
// implementation of the tray's own New Folder (the same duplicate-name loop,
// the same db->createFolder call) with NO caller: nothing in the Materials
// module ever offered the gesture, so every folder it could have made would
// have been invisible in a drawer that lists no folders. A project folder is
// made in one place now — services/projectfolders.h, behind
// assets.createFolder.)

void ShaderAssetWidget::addDroppedToProject(QListWidgetItem *item)
{
	// A LIBRARY TILE DROPPED ON THE PROJECT DRAWER IS AN ADD, NOT A MINT
	// (MATERIAL_BUNDLE_SPEC 5). This used to create a fresh ModelTypes::Shader
	// row in the project from `app/templates/ShaderTemplate.shader` — a format
	// that predates the graph and cannot be reopened by the graph loader, so
	// the tile it produced was unopenable. The gesture is the same pin every
	// other asset gets: one row, its closure pinned with it.
	if (!db || !project || project->getProjectGuid().isEmpty() || !item) return;
	const QString guid = item->data(MODEL_GUID_ROLE).toString();
	if (guid.isEmpty()) return;
	ProjectAssets::addToProject(guid, db, project, ProjectAssets::AddKind::Direct);
	refresh();
}


QByteArray ShaderAssetWidget::fetchAsset(QString string)
{
	if (!db) return QByteArray();
	return db->fetchAssetData(string);
}


