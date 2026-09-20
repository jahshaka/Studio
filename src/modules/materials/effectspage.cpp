/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "services/apppaths.h"
#include "ui/style/panelmetrics.h"
#include "irisgl/core/math/qtinterop.h"
#include "irisgl/core/math/vec.h"
#include "io/ziphelper.h"
#include "modules/materials/effectspage.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include <QSqlDatabase>
#include <QActionGroup>
#include "graph/graphnode.h"
#include <QMouseEvent>
#include <QApplication>
#include <QButtonGroup>
#include <QDebug>
#include <QDrag>
#include "bridge/enginehost.h"
#include "core/materialpreviewwidget.h"
#include "irisgl/document/materials/pbrmaterial.h"   // complete type: PbrMaterialPtr -> MaterialPtr upcast
#include <QTimer>
#include <QLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QAbstractItemView>
#include <QTabWidget>
#include <QMenuBar>
#include <QMenu>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QMimeData>
#include <QFile>
#include <QByteArray>
#include <QBuffer>
#include <QPixmap>
#include <QScrollBar>
#include <QShortcut>
#include <QDesktopServices>
#include "nodes/test.h"
#include "nodes/pbrmasternode.h"
#include "core/materialhelper.h"
#include "core/graphbaker.h"
#include "core/graphdefinition.h"
#include "data/materialpreset.h"
#include "io/materialpresets.h"
#include "services/assettags.h"
#include "services/materialbundle.h"
#include "services/materialpresetassets.h"
#include "services/materialpresetseeder.h"
#include "services/sceneissues.h"
#include "ui/controls/assetpickerwidget.h"
#include "services/projectassets.h"
#include <QFutureWatcher>
#include <QtConcurrent>
#include "models/library.h"
#include "models/libraryv1.h"
#include <QPointer>
#include "graph/graphnodescene.h"
#include "propertywidgets/basepropertywidget.h"
#include "dialogs/searchdialog.h"
#include "widgets/listwidget.h"
#include "data/project.h"
#include "core/texturemanager.h"
#include "propertywidgets/texturepropertywidget.h"
#include "ui/pages/assetview.h"
#include "ui/style/stylesheet.h"

#include <QMainWindow>
#include <QStandardPaths>
#include <QDirIterator>
#include <QMessageBox>
#include <QTemporaryDir>

#if(EFFECT_BUILD_AS_LIB)
#include "data/database/database.h"
#include "services/assethelper.h"
#include "services/thumbnailgenerator.h"
#include "data/guidmanager.h"
#include "irisgl/core/irisutils.h"
#include "io/assetmanager.h"
#include "ui/dialogs/progressdialog.h"
#include "ui/dialogs/toast.h"
#else
#include <QUuid>
#endif

#include "io/materialreader.h"
#include "io/scenewriter.h"

#include "core/undoredo.h"
#include "core/texturemanager.h"
#include <QDebug>
#include "services/assetdelete.h"
#include "services/assetshare.h"
#include "services/materialbundle.h"
#include "services/materialmembers.h"
#include "widgets/memberspanel.h"
#include "modules/materials/materialdocument.h"
#include "ui/style/themeroles.h"
#include <QTabBar>

namespace materials
{

	enum class ShaderWorkspace {
		Presets = 0,
		MyEffects = 1,
		Projects = 2
	};

EffectsPage::EffectsPage( QWidget *parent, Database *database) :
    QMainWindow(parent)
{
	// Debounce for the engine preview: one evaluation per burst of edits
	// (graphInvalidated fires per value change while a slider drags).
	previewUpdateTimer = new QTimer(this);
	previewUpdateTimer->setSingleShot(true);
	previewUpdateTimer->setInterval(300); // MATERIALS_EVALUATOR_SPEC section 2
	connect(previewUpdateTimer, &QTimer::timeout, this, &EffectsPage::updateEnginePreviewMaterial);

	// (The page's single 1.5 s autosave timer is gone: there is one PER
	// DOCUMENT now — MaterialDocument's, wired in newDocument. It was never
	// stopped on a switch, so an edit made less than 1.5 s before opening
	// another material fired against the NEW material's guid.)
	fontIcons = new QtAwesome;
	fontIcons->initFontAwesome();
	configureUI();
	configureToolbar();
	addMenuToSceneWidget();

	installEventFilter(this);

	// The handle is stored WHATEVER it is (lane DBPTR-1): the member used to be
	// assigned only inside this `if`, so a page built without a library kept an
	// uninitialised pointer that renameShader/saveShader dereference. A null
	// library is a real state (headless hosts, the module suites) and the
	// member must say so.
	dataBase = database;
	if (database) {
		setAssetWidgetDatabase(database);
		TextureManager::getSingleton()->setDatabase(database);
	}

	// ONE library for every graph this page opens (MATERIALS_TABS_SPEC §2.8).
	mNodeLibrary = MaterialHelper::sharedNodeLibrary();
	newNodeGraph();
	generateTileNode();
	configureStyleSheet();
	configureAssetsDock();
    configureConnections();
	setMinimumSize(300, 400);
    loadShadersFromDisk();

	assetView = nullptr;
}

// ---- the open materials (MATERIALS_TABS_SPEC §2) -------------------------

MaterialDocument *EffectsPage::activeDoc() const
{
	return (mActive >= 0 && mActive < mDocs.size()) ? mDocs[mActive] : nullptr;
}

NodeGraph *EffectsPage::activeGraph() const
{
	auto *doc = activeDoc();
	return doc ? doc->graph : nullptr;
}

GraphNodeScene *EffectsPage::activeScene() const
{
	auto *doc = activeDoc();
	return doc ? doc->scene : nullptr;
}

QUndoStack *EffectsPage::activeStack() const
{
	auto *doc = activeDoc();
	return doc ? doc->stack : nullptr;
}

shaderInfo EffectsPage::currentInfo() const
{
	auto *doc = activeDoc();
	return doc ? doc->info : shaderInfo();
}

MaterialDocument *EffectsPage::newDocument()
{
	auto *doc = new MaterialDocument(this);
	// THE AUTOSAVE IS THIS DOCUMENT'S. It fires against the material the
	// document names, whether or not that document is the one on screen — which
	// is the whole point: an edit made a moment before switching tabs is still
	// written to the material it was made in.
	connect(doc->saveTimer, &QTimer::timeout, this, [this, doc]() {
		if (!doc->info.GUID.isEmpty()) saveShader(doc);
	});
	return doc;
}

void EffectsPage::bindGraph(MaterialDocument *doc, NodeGraph *graph)
{
	if (!doc || !graph) return;
	const bool active = (doc == activeDoc());
	restoringGraph = true;

	auto *newScene = createNewScene(doc);
	newScene->setNodeGraph(graph);
	// The previous graph AND the scene drawing it go here, in that order
	// (MaterialDocument::adopt) — until this lane nothing ever freed either.
	doc->adopt(graph, newScene);
	doc->stack->clear();
	// THE READ-ONLY STATE IS THE DOCUMENT'S, AND THE SCENE IS REBUILT PER GRAPH:
	// re-assert it here or a preset's canvas would be locked only until the
	// scene it was set on was replaced — which is every open.
	newScene->setReadOnly(doc->readOnly);

	if (active) showDocument(doc);
	restoringGraph = false;
}

void EffectsPage::showDocument(MaterialDocument *doc)
{
	if (!doc || !doc->scene || !doc->graph) return;
	// A REBIND IS NOT AN EDIT: `setMaterialSettings` emits `settingsChanged`,
	// and that signal pushes an undoable MaterialSettingsChangeCommand — so
	// every activation would leave a command on the tab it activated.
	mShowingDocument = true;
	graphicsView->setScene(doc->scene);
	graphicsView->setAcceptDrops(true);
	materialSettingsWidget->setMaterialSettings(doc->graph->settings);
	// §3a: the right dock follows this document's scene selection
	nodePropertiesPanel->setGraph(doc->graph);
	nodePropertiesPanel->setScene(doc->scene);
	applyReadOnlyUi();
	if (membersPanel) membersPanel->setMaterial(doc->info.GUID);
	refreshCurrentTile();
	schedulePreviewUpdate();
	mShowingDocument = false;
}

bool EffectsPage::activateTab(int index)
{
	if (index < 0 || index >= mDocs.size()) return false;
	mActive = index;
	// EVERYTHING FOLLOWS THE ACTIVE TAB (§2.2): the canvas, the material
	// settings, the properties panel, the Members panel, the read-only banner,
	// the preview and, through graphUndo, Ctrl+Z.
	showDocument(mDocs[index]);
	syncTabBar();
	return true;
}

bool EffectsPage::closeDocumentAt(int index)
{
	if (index < 0 || index >= mDocs.size()) return false;
	MaterialDocument *doc = mDocs[index];

	// A PENDING AUTOSAVE IS WRITTEN FIRST (§2.3). There is no "save before
	// closing?" question on this page because the module autosaves — so a
	// close is a FLUSH, not a decision. A read-only document writes nothing,
	// and neither does the anonymous one (only an explicit Save can name it).
	if (doc->saveTimer->isActive()) {
		doc->saveTimer->stop();
		if (!doc->readOnly && !doc->info.GUID.isEmpty()) saveShader(doc);
	}
	doc->saveTimer->stop();

	mDocs.removeAt(index);
	if (mActive > index) --mActive;
	else if (mActive == index) mActive = qMin(index, mDocs.size() - 1);

	if (mDocs.isEmpty()) {
		// AN EMPTY PAGE IS NOT A STATE THIS EDITOR HAS: closing the last tab
		// leaves the untitled canvas the page boots on.
		auto *fresh = newDocument();
		mDocs.append(fresh);
		mActive = 0;
		bindGraph(fresh, newMasterGraph());
	} else {
		showDocument(mDocs[mActive]);
	}
	// Only now — the view is showing another document's scene, and the panels
	// point at another document's graph.
	delete doc;
	syncTabBar();
	return true;
}

void EffectsPage::dropUntouchedAnonymous(MaterialDocument *keep)
{
	for (int i = mDocs.size() - 1; i >= 0; --i) {
		MaterialDocument *doc = mDocs[i];
		if (doc == keep || !doc->isAnonymous()) continue;
		// UNTOUCHED means nothing was drawn on it: an untitled canvas somebody
		// HAS worked on stays open (nothing else can save it), an empty one is
		// just the page's boot state and gets out of the way.
		if (doc->stack && doc->stack->count() > 0) continue;
		closeDocumentAt(i);
	}
}

void EffectsPage::syncTabBar()
{
	if (!mTabBar) return;
	mSyncingTabs = true;
	while (mTabBar->count() > mDocs.size()) mTabBar->removeTab(mTabBar->count() - 1);
	while (mTabBar->count() < mDocs.size()) mTabBar->addTab(QString());
	for (int i = 0; i < mDocs.size(); ++i) {
		MaterialDocument *doc = mDocs[i];
		mTabBar->setTabText(i, doc->label());
		mTabBar->setTabToolTip(
		    i, doc->readOnly
		           ? tr("'%1' is a material the app ships — read-only.").arg(doc->label())
		           : doc->info.origin == shaderInfo::Origin::Project
		                 ? tr("'%1' as THIS PROJECT holds it — editing it never touches the "
		                      "library original.").arg(doc->info.name)
		                 : doc->label());
	}
	if (mActive >= 0 && mActive < mTabBar->count()) mTabBar->setCurrentIndex(mActive);
	// TODAY'S LOOK, BYTE FOR BYTE, while the only document is the boot canvas:
	// a page with nothing open has nothing to show a bar of.
	mTabBar->setVisible(!(mDocs.size() == 1 && mDocs[0]->isAnonymous()));
	mSyncingTabs = false;
}

QVariantMap EffectsPage::tabInfo(MaterialDocument *doc) const
{
	QVariantMap out;
	if (!doc) return out;
	out["tab"] = mDocs.indexOf(doc);
	out["guid"] = doc->info.GUID;
	out["name"] = doc->info.name;
	out["scope"] = doc->info.origin == shaderInfo::Origin::Project
	                   ? QStringLiteral("project") : QStringLiteral("library");
	out["readOnly"] = doc->readOnly;
	// DIRTY = an autosave is pending on this document (it writes 1.5 s after
	// the last edit, or on close).
	out["dirty"] = doc->saveTimer && doc->saveTimer->isActive();
	out["active"] = doc == activeDoc();
	return out;
}

int EffectsPage::indexForRef(const QVariant &tabOrGuid) const
{
	// A NUMBER IS AN INDEX, a string is a guid (the first tab in bar order).
	const int type = tabOrGuid.typeId();
	if (type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong
	    || type == QMetaType::ULongLong || type == QMetaType::Double
	    || type == QMetaType::Float) {
		return tabOrGuid.toInt();
	}
	const QString guid = tabOrGuid.toString();
	for (int i = 0; i < mDocs.size(); ++i)
		if (mDocs[i]->info.GUID == guid) return i;
	return -1;
}

QVariantMap EffectsPage::openMaterialTab(const QString &guid, const QString &scope)
{
	shaderInfo::Origin origin;
	if (scope == QLatin1String("project")) origin = shaderInfo::Origin::Project;
	else if (scope == QLatin1String("library")) origin = shaderInfo::Origin::Library;
	else {
		// THE DEFAULT IS THE COPY THE USER WOULD REACH FOR: the project's, when
		// the open project pins this material (that is the drawer it is in),
		// the library's otherwise.
		const bool pinned = dataBase && mProject && !mProject->getProjectGuid().isEmpty()
		                    && dataBase->isAssetPinnedBy(mProject->getProjectGuid(), guid);
		origin = pinned ? shaderInfo::Origin::Project : shaderInfo::Origin::Library;
	}
	return tabInfo(openDocument(guid, origin));
}

QVariantList EffectsPage::materialTabs() const
{
	QVariantList out;
	for (MaterialDocument *doc : mDocs) out.append(tabInfo(doc));
	return out;
}

QVariantMap EffectsPage::activeMaterialTab() const
{
	return tabInfo(activeDoc());
}

bool EffectsPage::activateMaterialTab(const QVariant &tabOrGuid)
{
	return activateTab(indexForRef(tabOrGuid));
}

bool EffectsPage::closeMaterialTab(const QVariant &tabOrGuid)
{
	return closeDocumentAt(indexForRef(tabOrGuid));
}

void EffectsPage::documentChanged(MaterialDocument *doc)
{
	if (!doc) return;
	if (doc == activeDoc()) {
		if (membersPanel) membersPanel->setMaterial(doc->info.GUID);
		refreshCurrentTile();
	}
	syncTabBar();   // the label is the document's name
}

void EffectsPage::forgetMaterial(const QString &guid)
{
	// THE MATERIAL IS GONE FROM THE LIBRARY, so the tabs that are it CLOSE —
	// without a save, because the row they would write to no longer exists
	// (MATERIALS_TABS_SPEC §2.6). Nothing else on the page changes: deleting one
	// material never touched the others, and the unconditional reset this
	// replaced made every delete reach for whatever was on screen.
	if (guid.isEmpty()) return;
	for (int i = mDocs.size() - 1; i >= 0; --i) {
		if (mDocs[i]->info.GUID != guid) continue;
		mDocs[i]->saveTimer->stop();
		mDocs[i]->info = shaderInfo();   // no save on the way out
		closeDocumentAt(i);
	}
}

void EffectsPage::renameOpenDocuments(const QString &guid, const QString &newName)
{
	if (guid.isEmpty()) return;
	for (MaterialDocument *doc : mDocs) {
		if (doc->info.GUID != guid) continue;
		doc->info.name = newName;
		if (doc->graph) doc->graph->settings.name = newName;
		if (doc == activeDoc() && materialSettingsWidget) materialSettingsWidget->setName(newName);
		documentChanged(doc);
		saveShader(doc);
	}
}

void EffectsPage::setNodeGraph(NodeGraph *graph)
{
	// The page-level entry point kept for the callers that mean "put this graph
	// on the canvas I am looking at" — a new material, an imported file, the
	// boot canvas. Opening a STORED material makes its own document.
	MaterialDocument *doc = activeDoc();
	if (!doc) {
		doc = newDocument();
		mDocs.append(doc);
		mActive = mDocs.size() - 1;
	}
	bindGraph(doc, graph);
}

NodeGraph *EffectsPage::newMasterGraph()
{
    auto graph = new NodeGraph;
	graph->setNodeLibrary(mNodeLibrary ? mNodeLibrary
	                                   : MaterialHelper::sharedNodeLibrary());
    // new graphs author PBR (Option B) - legacy Surface graphs still load
    auto masterNode = new PbrMasterNode();
    graph->addNode(masterNode);
    graph->setMasterNode(masterNode);
    return graph;
}

void EffectsPage::newNodeGraph(QString *shaderName, int *templateType, QString *templateName)
{
    setNodeGraph(newMasterGraph());
}

void EffectsPage::refreshShaderGraph()
{
	// BOTH DRAWERS, because both can be out of date (fix round F1/F2). This
	// used to refresh only the PROJECT drawer, so every gesture that changes
	// what the library holds — Duplicate, Import material…, Add to project —
	// left the Custom list showing yesterday's rows. That is not a cosmetic
	// staleness: `selectCorrectItemFromDrop` looks the new material up IN THE
	// WIDGETS, so a bundle with no tile could not be opened at all, and a
	// material just added to a project kept a Custom tile that then claimed
	// the LIBRARY scope for an edit belonging to the project.
	//
	// `updateAssetDock` is one library query plus a small parse per row, and
	// it is what makes the drawer agree with the catalog after anything —
	// including an import made on the Assets page while this page was hidden.
#if(EFFECT_BUILD_AS_LIB)
	updateAssetDock();
	assetWidget->refresh();
#endif
	refreshCurrentTile();
}

EffectsPage::~EffectsPage()
{
    
}

void EffectsPage::saveShader()
{
	saveShader(activeDoc());
}

void EffectsPage::saveShader(MaterialDocument *doc)
{
	if (!doc || !doc->graph) return;

	// A SHIPPED PRESET IS ON SCREEN TO BE READ (PRESET-UNIFY-1). It is open
	// because the user selected it, the definition writer refuses its
	// reserved guid, and the 1.5 s autosave runs on every node the user drags
	// — so without this the act of LOOKING at a preset raised a refusal in
	// the scene-issue bar every second and a half. Nothing is written, and
	// nothing needs to be: a preset cannot change.
	if (doc->readOnly) return;

	if (doc->info.GUID.isEmpty()) {
		// The anonymous canvas: there is no material to write to yet, so this
		// is the "name it first" route (CreateNewDialog), and the document
		// acquires its guid in place.
		saveDefaultShader();
		return;
	}

	// SAVING IS THE DEFINITION WRITE (MATERIAL_BUNDLE_SPEC phase 1). It is
	// still a final-bake trigger, but the maps land as MEMBER TEXTURE ROWS in
	// the store instead of loose PNGs under `<projectFolder>/BakedMaps/`, and
	// what is stored is the bundle definition — guids only — as the Material
	// row's own file. `MaterialBundle::write` derives the membership edges
	// from it and refuses any path that slipped through.
#if(EFFECT_BUILD_AS_LIB)
	{
		const auto build = materials::buildDefinition(doc->graph, doc->info.GUID,
		                                              dataBase, mProject);
		if (!build.ok()) {
			irisLog("saveShader: " + build.error);
			reportSaveRefused(doc, build.error);
		} else {
			// WHOSE VERSION (the owner's model, spec 12 Q2): the DRAWER the
			// material was opened from decides. A Projects tile is edited as
			// the project's own — its pin moves, the library original does
			// not; a Custom tile publishes to the library. This used to ask
			// "does the project pin it?", so a material the project held
			// could never be edited as the library's, and the same guid
			// meant two things in one window.
			const bool projectOwns =
			    doc->info.origin == shaderInfo::Origin::Project
			    && mProject && !mProject->getProjectGuid().isEmpty()
			    && dataBase->isAssetPinnedBy(mProject->getProjectGuid(), doc->info.GUID);
			const auto written = MaterialBundle::write(
			    dataBase, mProject, doc->info.GUID, build.definition,
			    projectOwns ? MaterialBundle::Scope::Project
			                : MaterialBundle::Scope::Library);
			if (!written.ok) {
				irisLog("saveShader: " + written.error);
				// A REFUSED SAVE IS TOLD, not logged. This runs on the 1.5 s
				// autosave, so a refusal the user cannot see means they keep
				// working on a graph nothing is writing down — an hour of
				// work lost silently, which is exactly the shape of the
				// defect the path guard exists to prevent.
				reportSaveRefused(doc, written.error);
			} else if (doc->saveRefused) {
				SceneIssues::instance().clear(QStringLiteral("material.save:")
				                              + doc->info.GUID);
				doc->saveRefused = false;
			}
			// AND THE EDIT REACHES THE SCENE (R19 D2). Every mesh wearing this
			// material is re-dressed from the definition just written, through
			// the ONE apply the drop and the verb use. Until this lane a graph
			// edit reached the scene only by accident — through a material
			// SWITCH, and only while the Projects tab happened to be current.
			else if (mMaterialChanged) mMaterialChanged(doc->info.GUID);
		}
	}
	// Thumbnail: queued, never inline. The graph's baked material renders on
	// the preview sphere through the shell's thumbnail queue (one request per
	// tick, main thread) and lands in onShaderThumbnail — saving must not block
	// on a render, and the stored asset data must already be written when the
	// request is served (the renderer re-reads it from the database).
	requestShaderThumbnail(doc->info.GUID);
	// A SAVE CAN CHANGE THE MEMBERS: the final bake mints its maps as member
	// textures, and a picture picked in a texture node becomes one. Only the
	// panel's own document, though — it shows the ACTIVE material.
	if (membersPanel && doc == activeDoc()) membersPanel->refresh();
#else
	// The STANDALONE build (no library): the graph goes to a file, unchanged.
	{
		auto filePath = QDir().filePath(AppPaths::dataRoot() + "/Materials/MyFx/");
		if (!QDir(filePath).exists()) QDir().mkpath(filePath);
		const QJsonObject matObj = MaterialHelper::serialize(doc->graph);
		auto shaderFile = new QFile(filePath + matObj["name"].toString());
		if (shaderFile->open(QIODevice::ReadWrite)) {
			shaderFile->write(QJsonDocument(matObj).toJson());
			shaderFile->close();
		}
		else {
			qDebug() << "device not open";
		}
	}
#endif

	int currentTab = selectCorrectTabForItem(doc->info.GUID);
	auto item = selectCorrectItemFromDrop(doc->info.GUID);
	if (item) {
		// The tile keeps whatever it has until the render arrives (blanking it
		// here is what made every saved graph show the generic file icon).
		tabWidget->setCurrentIndex(currentTab);
		ListWidget::highlightNodeForInterval(2, item);
	}
}

void EffectsPage::reportSaveRefused(MaterialDocument *doc, const QString &why)
{
	if (!doc) return;
	// The scene-issue bar, not a toast: a toast leaves, and this condition
	// stays true until the material is fixed (services/sceneissues.h). The id
	// is per material, so a second refused autosave of the same graph is a
	// no-op rather than a second line.
	doc->saveRefused = true;
	SceneIssue issue;
	issue.id = QStringLiteral("material.save:") + doc->info.GUID;
	issue.kind = QStringLiteral("material.save");
	issue.nodeName = doc->info.name;
	issue.message = tr("'%1' could not be saved: %2").arg(doc->info.name, why);
	issue.action = tr("Your edits are still on screen but are NOT being written down. "
	                  "Re-pick the image on the node the message names, then save again.");
	SceneIssues::instance().raise(issue);
}

void EffectsPage::reportGraphRefused(const QString &guid, const QString &why)
{
	// A REFUSED OPEN IS A SCENE ISSUE, not a silent empty canvas
	// (LEGACY-MASTER-CRUD). The page has no graph to show, and the one thing
	// that must not happen is the user being given a blank canvas with the
	// material's name on it and saving over their file with it. The id is per
	// material, so re-opening the same one does not stack lines.
	//
	// THE NAME IS RESOLVED FROM THE GUID (fix round): `currentShaderInformation`
	// is the material still on the canvas — the one that opened fine — so
	// reading the name off it labelled the refusal with the WRONG material on
	// every route.
	const QString shipped = MaterialBundle::shippedPresetName(guid);
	QString name = shipped;
	if (name.isEmpty() && dataBase) name = dataBase->fetchAsset(guid).name;
	if (name.isEmpty()) name = guid;

	SceneIssue issue;
	issue.id = QStringLiteral("material.legacy:") + guid;
	issue.kind = QStringLiteral("material.legacy");
	issue.nodeName = name;
	issue.message = tr("'%1' could not be opened: %2").arg(name, why);
	issue.action = tr("Make a new material from a preset and re-pick its images; "
	                  "the old file is left exactly as it is.");
	SceneIssues::instance().raise(issue);

	// AND IT IS SAID WHERE THE GESTURE HAPPENED (fix round). The scene-issue
	// bar lives in the EDITOR space and is hidden everywhere else, so a user
	// who double-clicks a tile on the Materials page would be told nothing at
	// all: the canvas simply would not change. The issue stays as the
	// persistent record; this is the answer to the click.
	if (!mRefusalToast) {
		mRefusalToast = new Toast(this);
		mRefusalToast->setAnchor(Toast::Anchor::WindowBottom);
	}
	mRefusalToast->showToast(tr("Cannot open '%1'").arg(name), why, 8000);
}

void EffectsPage::requestShaderThumbnail(const QString &shaderGuid)
{
	if (shaderGuid.isEmpty()) return;
	auto generator = ThumbnailGenerator::getSingleton();
	generator->setDatabase(dataBase);
	generator->setProject(mProject);   // the definition resolves pin-first against it
	if (!mThumbnailConnected) {
		connect(generator, &ThumbnailGenerator::thumbnailComplete,
		        this, &EffectsPage::onShaderThumbnail);
		mThumbnailConnected = true;
	}
	// A MATERIAL RENDER OF A MATERIAL ROW. This asked for a SHADER render, and
	// that branch reads the row blob through `parseShaderAsPbr`, which refuses
	// any definition with no `pbrMaterial` key — a key a bundle definition does
	// not have. So every autosave of every graph material in this module
	// logged "nothing was rendered", burned a queue tick and left a BLANK
	// TILE, while the library's own sweep (thumbnailrebuild, which reads the
	// bundle) rendered the same material perfectly. Two readers for one
	// question; there is one now, and it is the bundle's.
	mPendingThumbnails.insert(shaderGuid);
	generator->requestThumbnail(ThumbnailRequestType::Material, QString(), shaderGuid);
}

void EffectsPage::onShaderThumbnail(const ThumbnailResult &result)
{
	// THE QUEUE IS SHARED, and a material render is no longer ours by TYPE
	// alone — the editor's material panel and the tray's sweep ask for the
	// same kind. Ours are the guids we asked about.
	// (The payload used to arrive as a ThumbnailResult* that AssetWidget's
	// slot had already deleted by the time this one ran — a read-after-free
	// decided by connection order, and the likely root of shader thumbnails
	// that "sometimes" failed to save. It is a value now.)
	if (result.type != ThumbnailRequestType::Material) return;
	if (result.preview || result.thumbnail.isNull() || result.id.isEmpty()) return;
	if (mPendingThumbnails.remove(result.id) == 0) return;

	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	QPixmap::fromImage(result.thumbnail).save(&buffer, "PNG");
	if (bytes.isEmpty()) return;

	dataBase->updateAssetThumbnail(result.id, bytes);
	if (auto item = selectCorrectItemFromDrop(result.id))
		ListWidget::updateThumbnailImage(bytes, item);

	// (There is no second row to copy the picture onto any more: ONE material,
	// ONE thumbnail — spec 9 item 5.)
}

void EffectsPage::saveDefaultShader()
{
	bool shouldSaveGraph = createNewGraph(false);
}

void EffectsPage::loadShadersFromDisk()
{
	// create constants for this
    auto filePath = QDir().filePath(AppPaths::dataRoot() + "/Materials/MyFx/");
	QDirIterator it(filePath);

	while (it.hasNext()) {

		QFile file(it.next());
		file.open(QIODevice::ReadOnly);
		auto doc = QJsonDocument::fromJson(file.readAll());
		file.close();

		auto obj = doc.object();
        if (obj["guid"].toString() == "") continue;

		QListWidgetItem *item = new QListWidgetItem;
		item->setFlags(item->flags() | Qt::ItemIsEditable);
		item->setSizeHint(defaultItemSize);
		item->setTextAlignment(Qt::AlignCenter);
		item->setIcon(QIcon(":/icons/icons8-file-72.png"));

		item->setData(Qt::DisplayRole, obj["name"].toString());
		item->setData(MODEL_GUID_ROLE, obj["guid"].toString());
		item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Shader));
		item->icon().addPixmap(QPixmap(":/icons.shader_overlay.png"));
		effects->addItem(item);
    }
	
}

// (`deleteMaterialFile` is DELETED — MATERIALS_TABS_SPEC §7. It had no
// callers and no body worth the name: it serialised the open graph into a
// local QJsonDocument and dropped it on the floor.)

QString EffectsPage::genGUID()
{
	auto id = QUuid::createUuid();
	auto guid = id.toString().remove(0, 1);
	guid.chop(1);
	return guid;
}

void EffectsPage::importGraph()
{
	// IMPORT A MATERIAL = THE SHARE FILE (MATERIAL_BUNDLE_SPEC 5, owner Q5).
	//
	// What was here was `importEffect`: extract the zip by hand, read a
	// one-word `.manifest`, call the legacy .jaf row importer and then
	// QFile::copy every image in `assets/` into the RETIRED per-guid folder
	// under the store root — outside the content-addressed store, with no
	// hash, no sidecar and no pin. It is deleted with its exporter twin: a
	// material arrives through `assetshare::importBundle` now, which is the
	// import that lands a closure payload (rows, bytes, edges, pins) and the
	// same one `assets.import` uses.
	if (!dataBase) return;
	const QString path = QFileDialog::getOpenFileName(this, tr("Import material"), QString(),
	                                                  assetshare::fileFilter());
	if (path.isEmpty()) return;

	const auto landed = assetshare::importBundle(dataBase, mProject, path);
	if (!landed.ok()) {
		QMessageBox::warning(this, tr("Import material"),
		                     tr("That file could not be imported: %1").arg(landed.error));
		return;
	}
	// A MATERIAL THIS LIBRARY ALREADY HOLDS IS NOT REPLACED, and the user is
	// told so rather than left thinking their newer file landed: the import
	// pinned the version that is already here. Updating an asset from a share
	// file is a decision nobody has taken (there is no merge rule, and
	// overwriting a row other projects pin is the opposite of the pin law).
	if (landed.alreadyHad)
		QMessageBox::information(
		    this, tr("Import material"),
		    tr("'%1' is already in your library, so it was added to the project as it is — "
		       "nothing was overwritten. Duplicate it first if you want both versions.")
		        .arg(dataBase->fetchAsset(landed.guid).name));

	refreshShaderGraph();
	tabWidget->setCurrentIndex(static_cast<int>(
	    mProject && !mProject->getProjectGuid().isEmpty() ? ShaderWorkspace::Projects
	                                                      : ShaderWorkspace::MyEffects));
	if (auto *item = selectCorrectItemFromDrop(landed.guid))
		ListWidget::highlightNodeForInterval(2, item);
}

void EffectsPage::loadGraph(QString guid, shaderInfo::Origin origin)
{
	openDocument(guid, origin);
}

MaterialDocument *EffectsPage::openDocument(const QString &guid, shaderInfo::Origin origin)
{
	if (guid.isEmpty()) return nullptr;

	// A SHIPPED PRESET OPENS — READ-ONLY (PRESET-UNIFY-1; the owner
	// 2026-09-20: "if i select a preset i should see the graph, i dont see
	// it"). What stays true is that it cannot be CHANGED: the definition
	// writer refuses its reserved guid, so it opens with the save stood down
	// and says so above the canvas, with Customise one button away.
	//
	// NOTHING MOVES UNTIL THE GRAPH IS IN HAND (LEGACY-CONVERT-CRUD's fix
	// round, the data-loss defect). `shipped` is a pure lookup, the scope is an
	// ARGUMENT to `fetchAsset`, and the document's identity, its read-only
	// state and its canvas are written together, once, in `adoptGraph` — after
	// the file has proved it can be opened. WITH TABS THE GUARANTEE IS
	// STRONGER: a refused open creates no document at all, so there is not even
	// an empty tab to save from.
	const QString shipped = MaterialBundle::shippedPresetName(guid);
	// A stale refusal from an earlier build's behaviour, or from a save that
	// was refused before this material was opened, must not outlive it.
	SceneIssues::instance().clear(QStringLiteral("material.readonly:") + guid);
	// The same for a refusal: a material that opens now must not still carry
	// the line saying it cannot be opened.
	SceneIssues::instance().clear(QStringLiteral("material.legacy:") + guid);

	// ALREADY OPEN AT THIS SCOPE? Then this is an ACTIVATION, not a second
	// copy: identity is (guid, origin) — the same guid at both origins is two
	// documents (a library original and a project's pinned copy are two
	// things by the four-drawer rule), the same guid at ONE origin is one.
	for (int i = 0; i < mDocs.size(); ++i) {
		if (mDocs[i]->is(guid, origin)) { activateTab(i); return mDocs[i]; }
	}

	// THE DRAWERS ARE A VIEW of the catalog, refilled on a space switch, so a
	// material minted a moment ago — by a verb, by an import made on another
	// page — may have no tile yet. Ask the catalog once; the open does not
	// DEPEND on the tile (adoptGraph takes the name from the row when there is
	// none), but the tile work that follows a save does.
	if (!selectCorrectItemFromDrop(guid)) refreshShaderGraph();

	restoringGraph = true;
	// Parented + deleted below: this used to leak one orphanable top-level
	// window per loadGraph call.
	auto progressDialog = new ProgressDialog(this);
	progressDialog->setPumpsEventLoop(true);   // synchronous graph load

	progressDialog->setRange(0, 10);
	progressDialog->setValueAndText(1, "Preparing graph");
	progressDialog->show();

	NodeGraph *graph = nullptr;
	QJsonObject obj;
	QString refused;

#if(EFFECT_BUILD_AS_LIB)
	obj = QJsonDocument::fromJson(fetchAsset(guid, origin)).object();
	// A PRESET NOBODY HAS USED YET HAS NO ROW (seeding is on first USE, not on
	// listing — services/materialpresetassets.h). Looking at one must not be
	// what seeds it: the shipped graph is on disk, so the page reads THAT and
	// writes nothing at all. Once the preset has been applied or customised,
	// its definition is the better source — the same graph with its images
	// named by the guids the library gave them.
	if (!shipped.isEmpty() && !obj.contains(QStringLiteral("shadergraph"))) {
		bool found = false;
		const MaterialPreset preset = MaterialPresets::find(guid, &found);
		if (found) obj[QStringLiteral("shadergraph")] = preset.graph;
	}
	progressDialog->setValueAndText(2, "Fetch graph");

	graph = MaterialHelper::extractNodeGraphFromMaterialDefinition(obj, &refused);
#else
	{
		auto filePath = QDir().filePath(AppPaths::dataRoot() + "/Materials/MyFx/");
		QDirIterator it(filePath);
		while (it.hasNext()) {
			QFile file(it.next());
			file.open(QIODevice::ReadOnly);
			auto doc1 = QJsonDocument::fromJson(file.readAll());
			file.close();
			auto obj1 = doc1.object();
			if (obj1["guid"].toString() == guid) { obj = obj1; break; }
		}
		graph = NodeGraph::deserialize(obj["graph"].toObject(), mNodeLibrary, &refused);
	}
#endif

	// THE FILE CAN BE REFUSED (LEGACY-MASTER-CRUD): a material written on the
	// deleted "Surface Material" master has no graph this build can draw. The
	// user is told — a toast here, a scene issue that stays — and NOTHING on
	// this page changes: no tab is opened, the tab that was active is still
	// active, still showing its own graph under its own name.
	if (graph == nullptr) {
		reportGraphRefused(guid, refused);
		restoringGraph = false;
		progressDialog->close();
		progressDialog->deleteLater();
		return nullptr;
	}

	// A READ-ONLY OPEN BINDS THE SHIPPED FILE AND WRITES NOTHING (fix round).
	// Opening a preset to LOOK at it must not import its pictures into the
	// library and pin them into the open project — a device wait each, on the
	// thread that draws, for a gesture that reads.
	if (!shipped.isEmpty())
		MaterialHelper::resolveAppRelativeTextures(
		    graph, MaterialHelper::TextureBinding::PathOnly);
	progressDialog->setValueAndText(6, "Deserialize Graph");

	// A TAB OF ITS OWN (MATERIALS_TABS_SPEC §2.2). This is the line the whole
	// lane is: opening a material used to REPLACE the one on the canvas.
	MaterialDocument *doc = newDocument();
	mDocs.append(doc);
	mActive = mDocs.size() - 1;
	// THE IDENTITY AND THE CANVAS MOVE TOGETHER, and only now: whatever this
	// document saves next belongs to the graph that is on it.
	adoptGraph(doc, guid, origin, shipped, graph);

	progressDialog->setValueAndText(8, "Tidying up");

#if(EFFECT_BUILD_AS_LIB)
	restoreGraphPositions(doc, obj["shadergraph"].toObject());
#else
	restoreGraphPositions(doc, obj["graph"].toObject());
#endif
	restoringGraph = false;
	// The Members panel follows the open bundle.
	if (membersPanel && doc == activeDoc()) membersPanel->setMaterial(doc->info.GUID);
	refreshCurrentTile();
	// The boot canvas stands aside for the first material that opens (C8).
	dropUntouchedAnonymous(doc);
	syncTabBar();
	progressDialog->close();
	progressDialog->deleteLater();
	return doc;
}

void EffectsPage::adoptGraph(const QString &guid, shaderInfo::Origin origin,
                             const QString &shippedName, NodeGraph *graph)
{
	adoptGraph(activeDoc(), guid, origin, shippedName, graph);
}

void EffectsPage::adoptGraph(MaterialDocument *doc, const QString &guid,
                             shaderInfo::Origin origin, const QString &shippedName,
                             NodeGraph *graph)
{
	// ONE PLACE WHERE A DOCUMENT BECOMES A MATERIAL (LEGACY-CONVERT-CRUD's fix
	// round, kept whole by this lane and moved onto the document). Identity,
	// read-only state and canvas in one step, in this order, so there is no
	// window in which a save would write one material's graph into another
	// material's row — which is what a refused open used to leave behind.
	if (!doc) return;
	doc->info.GUID = guid;
	doc->info.origin = origin;
	// A PRESET TILE'S LABEL IS ELIDED to fit its 90 px tile, so the NAME comes
	// from the shipped list rather than from what the tile could draw; with no
	// tile at all (a guid whose drawer has not been refilled yet) the library
	// row is the source, because a document must never carry the PREVIOUS
	// material's name over a new one.
	QListWidgetItem *tile = selectCorrectItemFromDrop(guid);
	doc->info.name =
	    !shippedName.isEmpty()
	        ? shippedName
	        : (tile ? tile->data(Qt::DisplayRole).toString()
	                : (dataBase ? dataBase->fetchAsset(guid).name : QString()));
	if (doc == activeDoc()) oldName = doc->info.name;
	// AFTER the graph is in hand, never before: this is what stands the save
	// down for a shipped preset, and dropping it for a material that turned
	// out not to open is how an edit could reach a read-only row.
	doc->readOnly = !shippedName.isEmpty();
	doc->presetName = shippedName;
	bindGraph(doc, graph);
	documentChanged(doc);
}

void EffectsPage::setReadOnly(bool readOnly, const QString &presetName)
{
	if (auto *doc = activeDoc()) {
		doc->readOnly = readOnly;
		doc->presetName = presetName;
	}
	applyReadOnlyUi();
}

bool EffectsPage::isReadOnly() const
{
	auto *doc = activeDoc();
	return doc && doc->readOnly;
}

void EffectsPage::applyReadOnlyUi()
{
	auto *doc = activeDoc();
	const bool readOnly = doc && doc->readOnly;
	// THE CANVAS REFUSES EDITS, it does not merely fail to save them (fix
	// round). Flipping a flag and a banner left the scene taking nodes,
	// wires, drags and typed values while `saveShader` quietly returned and
	// Customise built the copy from the SHIPPED definition — so the work went
	// into a window that showed it and into nothing else.
	if (doc && doc->scene) doc->scene->setReadOnly(readOnly);
	// THE DOCKS TOO. `GraphNode::setInteractive` reaches only the widgets
	// EMBEDDED IN A NODE; the right-hand properties panel and the settings
	// dock are separate windows onto the same model and were writing to it
	// through `NodePropertiesPanel::writeValue`. Disabling them is both the
	// enforcement and the thing the user can see. They are SHARED widgets, so
	// this runs on every activation too, or a preset's lock leaks onto the
	// next tab.
	if (nodePropertiesPanel) nodePropertiesPanel->setReadOnly(readOnly);
	if (materialSettingsWidget) materialSettingsWidget->setEnabled(!readOnly);
	if (!mReadOnlyBanner || !mReadOnlyLabel) return;
	if (readOnly) {
		mReadOnlyLabel->setText(
		    tr("'%1' is a material the app ships — read-only, so the canvas takes no edits. "
		       "This is its graph; Customise makes '%1-1', your own copy, and every edit "
		       "works on that.")
		        .arg(doc->presetName));
	}
	mReadOnlyBanner->setVisible(readOnly);
}

void EffectsPage::exportEffect(QString guid)
{
	// EXPORT A MATERIAL = THE SHARE FILE (owner Q5). The old body minted a
	// SECOND Material row at export time, wrote a flat `.material` file,
	// copied every texture by DISPLAY NAME into a temp tree and zipped it
	// with a one-word manifest — `Exporter::exportShaderAsMaterial`, deleted
	// with this lane. One asset, its closure, its bytes, one file.
	if (!dataBase || guid.isEmpty()) return;
	const QString assetName = dataBase->fetchAsset(guid).name;
	QString path = QFileDialog::getSaveFileName(
	    this, tr("Export material"),
	    QStringLiteral("%1.%2").arg(QFileInfo(assetName).completeBaseName(),
	                                QLatin1String(assetshare::extension())),
	    assetshare::fileFilter());
	if (path.isEmpty()) return;
	if (QFileInfo(path).suffix().isEmpty())
		path += QStringLiteral(".") + QLatin1String(assetshare::extension());

	const auto written = assetshare::exportBundle(dataBase, mProject, guid, path);
	if (!written.ok())
		QMessageBox::warning(this, tr("Export material"),
		                     tr("That material could not be exported: %1").arg(written.error));
}

void EffectsPage::duplicateShader(QString guid)
{
	// THE VERB'S OWN IMPLEMENTATION (API-first, SCRIPTING_SPEC §2.3):
	// `materials.duplicate` and this menu item call the one function, so the
	// copy a script makes and the copy a click makes are the same copy —
	// pictures shared, bake not inherited, name numbered against the library.
	if (!dataBase || guid.isEmpty()) return;
	QString error;
	const QString copy = materialmembers::duplicate(dataBase, mProject, guid, QString(), &error);
	if (copy.isEmpty()) {
		QMessageBox::warning(this, tr("Duplicate material"),
		                     tr("That material could not be duplicated: %1").arg(error));
		return;
	}
	// THE DRAWERS FIRST, THEN THE OPEN (fix round F1): `loadGraph` finds its
	// tile in the widgets, so opening the copy before the Custom list is
	// refilled used to dereference a tile that did not exist.
	refreshShaderGraph();
	tabWidget->setCurrentIndex(static_cast<int>(ShaderWorkspace::MyEffects));
	if (auto *item = selectCorrectItemFromDrop(copy))
		ListWidget::highlightNodeForInterval(2, item);
	loadGraph(copy, shaderInfo::Origin::Library);
}

void EffectsPage::restoreGraphPositions(MaterialDocument *doc, const QJsonObject &data)
{
    if (!doc || !doc->scene) return;
    auto sceneObj = data["scene"].toObject();
    auto nodeList = sceneObj["nodes"].toArray();

    for(auto nodeVal : nodeList) {
        auto nodeObj = nodeVal.toObject();
        auto nodeId = nodeObj["id"].toString();
        auto node = doc->scene->getNodeById(nodeId);
        // A SAVED POSITION FOR A NODE THIS GRAPH NO LONGER HAS is not a crash
        // (getNodeById answers null and this dereferenced it): a migration
        // that dropped a node, or a hand-edited file, is enough to produce one.
        if (!node) continue;
        node->setX(nodeObj["x"].toDouble());
        node->setY(nodeObj["y"].toDouble());
    }
}

bool EffectsPage::deleteShader(QString guid)
{

    auto item = selectCorrectItemFromDrop(guid);
    auto holder = item->listWidget();

#if(EFFECT_BUILD_AS_LIB)

    // THE LIBRARY-DELETE LAW, not a bare row delete (services/assetdelete.h;
    // owner, 2026-09-09: "deleting an asset from the LIBRARY should not delete
    // it from a project"). A material a project pins is UNLISTED — it leaves
    // the drawer and every project that uses it keeps opening, rendering and
    // exporting exactly as before. `deleteAsset` skipped that law entirely and
    // took the row out from under them.
    //
    // From the PROJECT drawer the gesture means the other thing: take it out
    // of THIS project. That is `removeFromProject`, which drops the pin and
    // the pins of the members only this bundle uses, and leaves the library
    // alone.
    const bool fromProject = originForItem(guid) == shaderInfo::Origin::Project
                             && mProject && !mProject->getProjectGuid().isEmpty();
    const bool wasMaterial =
        dataBase->fetchAsset(guid).type == static_cast<int>(ModelTypes::Material);
    const auto outcome = fromProject
                             ? assetdelete::removeFromProject(dataBase, guid,
                                                              mProject->getProjectGuid())
                             : assetdelete::remove(dataBase, guid);
    // AND THE BUNDLE'S OWN MEMBERS GO WITH IT (spec §4; fix round F6). Only
    // on a real library delete — an UNLIST keeps the bundle alive for the
    // projects that pin it, and `removeFromProject` is the project's
    // business. Without this the pictures a material imported through its own
    // picker stayed behind for ever: nothing references them, they are hidden
    // by the V-2 fold, and with the material gone no Clean unused can reach
    // them.
    if (outcome.ok && !fromProject && !outcome.unlisted && wasMaterial)
        materialmembers::reapExclusiveMembers(dataBase, guid);
    if (outcome.ok) {
        holder->takeItem(holder->row(item));
        // ONLY THE MATERIAL THAT WENT (MATERIALS_TABS_SPEC C7). This used to
        // clear the open material's record UNCONDITIONALLY, so deleting any
        // row in the drawer left the graph on screen with no guid — and its
        // next save went down the 'name this new material' route, modal
        // dialog and all.
        forgetMaterial(guid);
        return true;
    }
#else

    auto filePath = QDir().filePath(AppPaths::dataRoot() + "/Materials/MyFx/");
    QDirIterator it(filePath);

    while (it.hasNext()) {

        QFile file(it.next());
        file.open(QIODevice::ReadOnly);
        auto doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        auto obj = doc.object();
        if (obj["guid"].toString() == "") continue;
        if(obj["guid"].toString() == guid){
            if(file.remove()){
                holder->takeItem(holder->row(item));
                forgetMaterial(guid);
                return true;
            }
        }

    }

#endif
    return false;

}


void EffectsPage::configureStyleSheet()
{
	setStyleSheet(StyleSheet::EffectsPageRoot());

	nodePropertiesPanel->setStyleSheet(StyleSheet::EffectsNodePropertiesPanel());

	nodeContainer->setStyleSheet(StyleSheet::EffectsNodeTiles());

	nodeContainer->verticalScrollBar()->setStyleSheet(StyleSheet::EffectsNodeTilesScrollBar());

	nodeTray->setStyleSheet(StyleSheet::EffectsDock());

	displayWidget->setStyleSheet(StyleSheet::EffectsDock());
	propertyWidget->setStyleSheet(StyleSheet::EffectsDock());
	materialSettingsWidget->setStyleSheet(StyleSheet::EffectsDock());
	materialSettingsDock->setStyleSheet(StyleSheet::EffectsDock());
	tabbedWidget->setStyleSheet(StyleSheet::EffectsTabbedWidget());
	for (int i = 0; i < tabbedWidget->count(); i++) {
		tabbedWidget->widget(i)->setStyleSheet(StyleSheet::EffectsNodeTiles());
	}
}


void EffectsPage::configureAssetsDock()
{
	auto holder = new QWidget;
	auto layout = new QVBoxLayout;
	holder->setLayout(layout);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	tabWidget = new QTabWidget;
	presets = new ListWidget;
	effects = new ListWidget;
	presets->sceneOpenProbe = mSceneOpenProbe;
	effects->sceneOpenProbe = mSceneOpenProbe;
	effects->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    effects->shaderContextMenuAllowed = true;

	effects->addToProjectMenuAllowed = true;

	auto scrollViewPreset = new QScrollArea;
	auto scrollViewFx = new QScrollArea;
	auto scrollViewAsset = new QScrollArea;

	// READ-ONLY, and the absence of the menu is the statement: a preset has
	// no Rename, no Delete and no Export, because it is not the user's row —
	// `shaderContextMenuAllowed` stays false here (phase 3 makes presets real
	// read-only library bundles with a Customise gesture; until then the
	// drawer must not offer edits it cannot honour).
	presets->shaderContextMenuAllowed = false;
	// …but it has ONE gesture (R18): Customise, which is how a read-only
	// preset becomes a material the user owns.
	presets->presetContextMenuAllowed = true;
	presets->setToolTip(tr("Shipped materials — read-only. Double-click one to SEE its graph; "
	                       "right-click › Customise for your own copy to edit."));
	presets->setStyleSheet(StyleSheet::EffectsPresetsList());

	// THE SHIPPED PRESETS — ONE LIST, TWO WINDOWS (PRESET-UNIFY-1, the owner
	// 2026-09-20: "the old presets should be gone and we should only have the
	// new ones, we dont need duplicates").
	//
	// There used to be TWO preset families in this one drawer: the graph
	// TEMPLATES under `app/shadergraph/` (listed by three
	// `CreateNewDialog::get*List` functions, now deleted) and the shipped
	// material presets — so "Brick" sat beside "Brick PBR", "Gold" beside
	// "Gold PBR", fifteen times over, one of each pair openable and the other
	// not. They are one set now: every shipped preset carries its own authored
	// graph (io/materialpresets.h reads both halves of one file), and this
	// drawer and the editor's materials tray read the SAME list.
	//
	// LISTING DOES NOT SEED: the guid is reserved and known before any row
	// exists, so the drawer costs nothing until somebody uses a preset.
	// ONE LINE PER TILE (PRESET-UNIFY-1). A 90 px tile cannot hold "Checker
	// Board PBR", and a WRAPPED bottom-aligned label answers that by showing
	// its SECOND line — "Board PBR" — directly above the wood preset, which
	// is also called "Board PBR". Two tiles reading the same word is the very
	// thing the owner called a duplicate. Wrapping off, the view elides the
	// one line it draws; the full name is on the tooltip.
	presets->setWordWrap(false);
	for (const MaterialPreset &preset : MaterialPresets::all()) {
		const QString guid = MaterialPresetAssets::guidFor(preset.name);
		if (guid.isEmpty()) continue;
		auto item = new QListWidgetItem;
		item->setText(preset.name);
		item->setToolTip(preset.name);
		item->setData(Qt::UserRole, preset.name);
		item->setSizeHint(defaultItemSize);
		item->setTextAlignment(Qt::AlignBottom);
		item->setIcon(QIcon(preset.icon));
		item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Material));
		item->setData(MODEL_GUID_ROLE, guid);
		presets->addToListWidget(item);
	}

	presets->isResizable = true;
	effects->isResizable = true;
	
	scrollViewFx->setWidget(effects);
	scrollViewPreset->setWidget(presets);
	scrollViewAsset->setWidget(assetWidget);
	scrollViewPreset->setWidgetResizable(true);
	scrollViewFx->setWidgetResizable(true);
	scrollViewAsset->setWidgetResizable(true);


	// THE DRAWERS SAY WHICH IS WHICH (the four-drawer rule, OWNER_REVIEW 9 —
	// the owner's own words: "presets (users can't edit), custom (users can
	// edit), adding a custom to a project, and the project asset tray"). The
	// three are not three views of one list: they are three SCOPES, and which
	// one a tile came from decides whose version an edit writes (shaderInfo::
	// Origin, EffectsPage::fetchAsset / saveShader). A user who cannot tell
	// them apart cannot tell what their edit will change.
	tabWidget->addTab(scrollViewPreset, tr("Presets"));
	tabWidget->setTabToolTip(static_cast<int>(ShaderWorkspace::Presets),
	                         tr("The materials the app ships. READ-ONLY — duplicate one into "
	                            "Custom to change it."));
	tabWidget->addTab(scrollViewFx, tr("Custom"));
	tabWidget->setTabToolTip(static_cast<int>(ShaderWorkspace::MyEffects),
	                         tr("Your own materials, in the LIBRARY. Editing one here changes the "
	                            "library's version; projects keep the version they took until they "
	                            "ask for the newer one."));
	tabWidget->addTab(scrollViewAsset, tr("Project"));
	tabWidget->setTabToolTip(static_cast<int>(ShaderWorkspace::Projects),
	                         tr("The ACTIVE project's materials — the same list as the editor's "
	                            "asset tray. Editing one here makes it this project's own and "
	                            "never touches the library original."));

	scrollViewFx->adjustSize();
	scrollViewPreset->adjustSize();

	scrollViewFx->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	scrollViewPreset->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);


	//layout->addWidget(scrollView);
	//layout->addWidget(buttonBar);
	assetsDock->setWidget(tabWidget);
	assetsDock->setStyleSheet(StyleSheet::EffectsDock());

	updateAssetDock();
}

void EffectsPage::createShader(NodeGraphPreset preset, bool loadNewGraph, const QString &wanted)
{
	// A NEW MATERIAL IS THE USER'S (PRESET-UNIFY-1): whatever was on the
	// canvas, this one is editable, so the read-only banner goes.
	setReadOnly(false);

	// AND IT IS NOT CALLED WHAT THE PRESET IS CALLED (fix round; corrected in
	// round 2). It took `preset.title` — a shipped preset's own name — so
	// "New material, based on Gold PBR" made a SECOND "Gold PBR", and a
	// preset is reached BY NAME (`material.apply(n, "Gold PBR")`,
	// `materials.loadGraph("Gold PBR")`, the drag payload), so the user's
	// material was unreachable by name for ever while the name went on
	// resolving to the preset.
	//
	// EVERY name goes through the ONE namer, including one the user TYPED.
	// Round 1 let a typed name through verbatim and leaned on the definition
	// writer to refuse a preset's name — but this door does not go through
	// `MaterialBundle::create`, it mints the row itself below, so nothing
	// refused it and typing "Gold PBR" reproduced the very defect. The namer
	// bumps a taken name to `<name>-1`, and "taken" includes every shipped
	// preset's, case-insensitively.
	QString newShader = MaterialPresetAssets::customiseName(
	    dataBase, wanted.trimmed().isEmpty() ? preset.name : wanted.trimmed());

	QListWidgetItem *item = new QListWidgetItem;
	item->setFlags(item->flags() | Qt::ItemIsEditable);
	item->setSizeHint(defaultItemSize);
	item->setTextAlignment(Qt::AlignCenter);
	item->setIcon(QIcon(":/icons/icons8-file-72.png"));

	auto assetGuid = genGUID();

	item->setData(MODEL_GUID_ROLE, assetGuid);
	item->setData(MODEL_ITEM_TYPE, MODEL_ASSET);
	// ONE ROW, AND IT IS A MATERIAL (MATERIAL_BUNDLE_SPEC 2.3, owner Q3: "only
	// materials"). The graph rides its definition as a payload; no
	// ModelTypes::Shader row is minted here any more.
	item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Material));
	item->setData(Qt::DisplayRole, newShader);

	currentProjectShader = item;
	oldName = newShader;

	//QStringList assetsInProject = dataBase->fetchAssetNameByParent(assetItemShader.selectedGuid);

	//// If we encounter the same file, make a duplicate...
	int increment = 1;
	//while (assetsInProject.contains(IrisUtils::buildFileName(shaderName, "shader"))) {
	//	shaderName = QString(newShader + " %1").arg(QString::number(increment++));
	//}

	item->setText(newShader);
	effects->addItem(item);
	effects->displayAllContents();

	if (auto *stack = activeStack()) stack->clear();

	if (loadNewGraph)	loadGraphFromTemplate(preset, newShader);
	else				setNodeGraph(activeGraph());   // the canvas as it stands, named at last

	if (auto *doc = activeDoc()) {
		doc->info.GUID = assetGuid;
		doc->info.name = newShader;
		doc->info.origin = shaderInfo::Origin::Library;   // created in the library
		documentChanged(doc);
	}


#if(EFFECT_BUILD_AS_LIB)
	// A LIBRARY BUNDLE. It is created in the library, not in a project — the
	// user adds it to a project when they want it there (the four-drawer rule,
	// OWNER_REVIEW 9) — and the row is an ordinary AssetsView Material, so the
	// Assets page and the Materials module browse ONE world.
	dataBase->createAssetEntry(assetGuid, newShader, static_cast<int>(ModelTypes::Material),
	                           QString(), QString(), QString(), QString(),
	                           QByteArray(), QByteArray(), QByteArray(), QByteArray(),
	                           AssetViewFilter::AssetsView);
	auto assetShader = new AssetMaterial;
	assetShader->fileName = newShader;
	assetShader->assetGuid = assetGuid;
	AssetManager::addAsset(assetShader);
#endif
	saveShader();
}

void EffectsPage::loadGraphFromTemplate(NodeGraphPreset preset, const QString &name)
{
	// A NEW MATERIAL IS BASED ON A PRESET (PRESET-UNIFY-1). This used to read
	// a `.effect` TEMPLATE file from app/shadergraph/ and then import an
	// ordered list of images into the texture PROPERTIES the pre-evaluator
	// format carried; the templates were the second preset family and they
	// are deleted. The graph comes from the preset itself now — the same
	// graph the Presets drawer shows and Customise copies.
	if (auto *doc = activeDoc()) doc->info = shaderInfo();
	setReadOnly(false);

	bool found = false;
	const MaterialPreset shipped = MaterialPresets::find(
	    preset.guid.isEmpty() ? preset.name : preset.guid, &found);
	NodeGraph *graph = nullptr;
	QString refused;
	if (found && !shipped.graph.isEmpty())
		graph = NodeGraph::deserialize(shipped.graph, mNodeLibrary, &refused);
	// A SHIPPED PRESET THAT REFUSES IS A SHIPPING DEFECT, not a user's old
	// file (fix round), and substituting a blank canvas for it silently is how
	// "New from Gold" would quietly make an empty material. Say it, then fall
	// back so the gesture still produces something editable.
	if (found && !refused.isEmpty()) {
		irisLog("loadGraphFromTemplate: the shipped preset '" + shipped.name
		        + "' was refused: " + refused);
		reportGraphRefused(preset.guid.isEmpty() ? preset.name : preset.guid, refused);
	}
	if (!graph) {
		// No preset (a blank new material): a master node on an empty canvas,
		// which is exactly what `materials.create({graph:true})` builds.
		graph = new NodeGraph;
		graph->setNodeLibrary(mNodeLibrary);
		auto *master = new PbrMasterNode();
		graph->addNode(master);
		graph->setMasterNode(master);
	}

	// The preset's images are shipped FILES; this material's are library
	// assets. One import by content each, so the new material's slots name
	// guids (a definition may never name a path — F3) and the pictures are
	// the same objects the preset's own bundle uses.
	MaterialHelper::resolveAppRelativeTextures(graph);

	// THE GRAPH CARRIES THE NEW MATERIAL'S NAME, not the preset's (fix round
	// 2, found on the rig). `buildDefinition` writes the graph's settings name
	// into the definition on every save, so naming it after the PRESET meant a
	// new material called "Gold PBR-1" whose stored definition said "Basic PBR"
	// the first time it was saved — the same drift Customise had.
	graph->settings.name = name.isEmpty() ? preset.name : name;
	setNodeGraph(graph);
}

void EffectsPage::refreshCurrentTile()
{
	// THE GUID IS THE IDENTITY, the QListWidgetItem is a view (fix round F1's
	// family): a drawer refill deletes every item, so the active document's
	// tile pointer is re-resolved from its guid after anything that refills a
	// drawer. This used to ask the SCENE which tile it had last been handed —
	// a pointer the scene kept for no other reason (`currentlyEditing`, now
	// deleted).
	const QString guid = currentInfo().GUID;
	currentProjectShader = guid.isEmpty() ? nullptr : selectCorrectItemFromDrop(guid);
}

QByteArray EffectsPage::fetchAsset(const QString &guid, shaderInfo::Origin origin)
{
#if(EFFECT_BUILD_AS_LIB)
	// THE DEFINITION AT THE SCOPE THIS MATERIAL WAS OPENED AT (D-2 + the
	// four-drawer rule): a Projects tile reads the project's pinned version,
	// a Custom tile reads the library original. Passing the project
	// unconditionally made the library copy unreachable the moment any
	// project pinned it.
	const bool projectScope = origin == shaderInfo::Origin::Project;
	const QJsonObject definition =
	    MaterialBundle::read(dataBase, guid, projectScope ? mProject : nullptr);
	if (!definition.isEmpty()) return QJsonDocument(definition).toJson();
	return dataBase->fetchAssetData(guid);
#else
	Q_UNUSED(guid); Q_UNUSED(origin);
	return QByteArray();
#endif
}

void EffectsPage::configureUI()
{
	nodeTray = new QDockWidget("Library");
	centralWidget = new QWidget();
	displayWidget = new QDockWidget("Display");
    assetsDock = new QDockWidget("");
    projectDock = new QDockWidget("Project");

	propertyWidget = new QDockWidget("Properties");
	materialSettingsDock = new QDockWidget("Material Settings");
	materialSettingsWidget = new MaterialSettingsWidget;
	tabbedWidget = new QTabWidget;
	graphicsView = new GraphicsView;
	nodePropertiesPanel = new NodePropertiesPanel;
	// THE ONE PICKER (MATERIAL_BUNDLE_SPEC P-2): the shell's asset picker, with
	// "Import from disk…" on the same dialog, answering with a GUID. The panel
	// asks through this so the graph layer never includes the shell's UI.
	nodePropertiesPanel->setTexturePicker([this](std::function<void(const QString &)> chosen) {
		auto *picker = new AssetPickerWidget(ModelTypes::Texture);
		picker->setImportFromDisk([](const QString &path) -> QString {
			auto *tex = TextureManager::getSingleton()->importTexture(path);
			return tex ? tex->guid : QString();
		});
		QObject::connect(picker, &AssetPickerWidget::itemDoubleClicked, this,
		                 [chosen](QListWidgetItem *item) {
			chosen(item->data(MODEL_GUID_ROLE).toString());
		});
	});
	nodeContainer = new QListWidget;
	splitView = new QSplitter;
	projectName = new QLineEdit;

	nodeTray->setAllowedAreas(Qt::AllDockWidgetAreas);
	displayWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	propertyWidget->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	materialSettingsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	//projectDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	assetsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

	setDockNestingEnabled(true);
	this->setCentralWidget(splitView);
	splitView->setOrientation(Qt::Vertical);

	// THE READ-ONLY BANNER (PRESET-UNIFY-1). A shipped preset opens here so
	// the user can SEE its graph; it is read-only in fact, so the page says
	// so above the canvas and offers the one gesture that does work rather
	// than letting them edit into a save that will be refused. No stylesheet:
	// a framed row of ordinary widgets reads correctly in both themes.
	{
		mReadOnlyBanner = new QWidget;
		auto *bannerRow = new QHBoxLayout(mReadOnlyBanner);
		bannerRow->setContentsMargins(8, 4, 8, 4);
		bannerRow->setSpacing(8);
		mReadOnlyLabel = new QLabel;
		mReadOnlyLabel->setWordWrap(true);
		auto *customiseButton = new QPushButton(tr("Customise"));
		customiseButton->setToolTip(tr("Make your own editable copy of this material."));
		connect(customiseButton, &QPushButton::clicked, this, [this]() {
			const shaderInfo open = currentInfo();
			if (!isReadOnly() || open.GUID.isEmpty()) return;
			emit presets->customisePreset(open.GUID);
		});
		bannerRow->addWidget(mReadOnlyLabel, 1);
		bannerRow->addWidget(customiseButton, 0);
		mReadOnlyBanner->hide();

		auto *canvas = new QWidget;
		auto *canvasColumn = new QVBoxLayout(canvas);
		canvasColumn->setContentsMargins(0, 0, 0, 0);
		canvasColumn->setSpacing(0);
		// THE OPEN MATERIALS, AS TABS (MATERIALS_TABS_SPEC §4). It lives INSIDE
		// the central splitter pane — above the read-only banner and the canvas
		// — so it adds nothing to the window's minimum width (the pane's
		// minimum is the view's; ui.window_minimum measures 1366). Configured
		// exactly as the house strip is (ui/controls/propertiestabstrip.cpp):
		// not expanding, eliding, scroll buttons as the last resort, no base
		// line, and the one STYLE GETTER — never a sheet of our own.
		mTabBar = new QTabBar;
		mTabBar->setExpanding(false);
		mTabBar->setElideMode(Qt::ElideRight);
		mTabBar->setUsesScrollButtons(true);
		mTabBar->setDrawBase(false);
		mTabBar->setTabsClosable(true);
		mTabBar->setMovable(true);
		mTabBar->setStyleSheet(StyleSheet::PreferencesTabs());
		ThemeRoles::setSurface(mTabBar, ThemeRoles::Surface::Panel);
		mTabBar->hide();   // today's look while the boot canvas is all there is
		connect(mTabBar, &QTabBar::currentChanged, this, [this](int index) {
			if (mSyncingTabs) return;
			activateTab(index);
		});
		connect(mTabBar, &QTabBar::tabCloseRequested, this, [this](int index) {
			closeDocumentAt(index);
		});
		connect(mTabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
			if (mSyncingTabs) return;
			if (from < 0 || from >= mDocs.size() || to < 0 || to >= mDocs.size()) return;
			// THE BAR IS THE ORDER (§4): mDocs is kept in tab order, so a drag
			// re-orders the documents and the saved tab set with them.
			MaterialDocument *act = activeDoc();
			mDocs.move(from, to);
			mActive = mDocs.indexOf(act);
		});
		canvasColumn->addWidget(mTabBar);
		canvasColumn->addWidget(mReadOnlyBanner);
		canvasColumn->addWidget(graphicsView, 1);
		splitView->addWidget(canvas);
	}
	splitView->addWidget(tabbedWidget);
	splitView->setStretchFactor(0, 90);

#if(EFFECT_BUILD_AS_LIB)
	assetWidget = new ShaderAssetWidget;
	assetWidget->sceneOpenProbe = mSceneOpenProbe;
	//addDockWidget(Qt::LeftDockWidgetArea, projectDock, Qt::Vertical);
#endif
	addDockWidget(Qt::LeftDockWidgetArea, assetsDock, Qt::Vertical);
	addDockWidget(Qt::RightDockWidgetArea, displayWidget, Qt::Vertical);
	addDockWidget(Qt::LeftDockWidgetArea, materialSettingsDock, Qt::Vertical);

	// THE MEMBERS PANEL (MATERIAL_BUNDLE_SPEC 6): name, slot or node, size,
	// used by, baked or picture — plus Clean unused (which lists first) and
	// Make unique. It reads through the same functions the verbs call
	// (services/materialmembers.h), so the window and `materials.members`
	// cannot describe one bundle two ways.
	membersDock = new QDockWidget(tr("Members"));
	membersDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	membersPanel = new MembersPanel;
	membersPanel->setDatabase(dataBase);
	membersPanel->setProject(mProject);
	membersDock->setWidget(membersPanel);
	membersDock->setMinimumWidth(PanelMetrics::leftColumnMinWidth);
	addDockWidget(Qt::LeftDockWidgetArea, membersDock, Qt::Vertical);
	connect(membersPanel, &MembersPanel::membersChanged, this, [this](const QString &guid) {
		// A member changed identity or went: the definition on disk moved, so
		// the graph in front of the user is re-read and every mesh wearing
		// the material re-dressed through the ONE apply.
		const shaderInfo open = currentInfo();
		if (!guid.isEmpty() && guid == open.GUID) loadGraph(guid, open.origin);
		if (mMaterialChanged) mMaterialChanged(guid);
	});
	addDockWidget(Qt::RightDockWidgetArea, propertyWidget, Qt::Vertical);

	// THE COLUMNS ARE THE EDITOR'S COLUMNS (owner, 2026-09-11, smoke S1): this
	// page used to carry its own 330 for both sides, so walking from the editor
	// to Materials moved both edges of the work area. Left = the assets/
	// settings column, right = Display + Properties, both from
	// ui/style/panelmetrics.h. The Display preview scales to any width and the
	// properties panel scrolls (phase-5 owner fix - the dock used to open at
	// ~750px and refuse to shrink).
	displayWidget->setMinimumSize(PanelMetrics::rightColumnMinWidth, 230);
	propertyWidget->setMinimumWidth(PanelMetrics::rightColumnMinWidth);
	// The dock stays hidden until Studio hands in the engine-rendered preview
	// (setEnginePreview).
	displayWidget->hide();
	displayWidget->toggleViewAction()->setEnabled(false);
	assetsDock->setMinimumWidth(PanelMetrics::leftColumnMinWidth);
	materialSettingsDock->setMinimumWidth(PanelMetrics::leftColumnMinWidth);
	// The opening widths are applied on the first SHOW (showEvent): a dock is
	// re-laid-out from its widget's sizeHint when it becomes visible, so a
	// resizeDocks from here is undone before anyone sees it.

	propertyWidget->setWidget(nodePropertiesPanel);
	nodePropertiesPanel->setMinimumHeight(400);
	
	QSize currentSize(100, 100);

	auto assetViewToggleButtonGroup = new QButtonGroup;
	auto toggleIconView = new QPushButton(tr("Icon"));
	toggleIconView->setCheckable(true);
	toggleIconView->setCursor(Qt::PointingHandCursor);
	toggleIconView->setChecked(true);
	toggleIconView->setFont(font);

	auto toggleListView = new QPushButton(tr("List"));
	toggleListView->setCheckable(true);
	toggleListView->setCursor(Qt::PointingHandCursor);
	toggleListView->setFont(font);

	auto label = new QLabel("Display:");
	label->setFont(font);

	assetViewToggleButtonGroup->addButton(toggleIconView);
	assetViewToggleButtonGroup->addButton(toggleListView);

	QHBoxLayout *toggleLayout = new QHBoxLayout;
	toggleLayout->setSpacing(0);
	toggleLayout->addWidget(label);
	toggleLayout->addStretch();
	toggleLayout->addWidget(toggleIconView);
	toggleLayout->addWidget(toggleListView);

	connect(toggleIconView, &QPushButton::pressed, [this]() {
		nodeContainer->setViewMode(QListWidget::IconMode);
	});

	connect(toggleListView, &QPushButton::pressed, [this]() {
		nodeContainer->setViewMode(QListWidget::ListMode);
	});

	//connect(materialSettingsWidget, SIGNAL(settingsChanged(MaterialSettings)), sceneWidget, SLOT(setMaterialSettings(MaterialSettings)));
	connect(materialSettingsWidget, &MaterialSettingsWidget::settingsChanged, [=](MaterialSettings value) {
	});
	materialSettingsDock->setWidget(materialSettingsWidget);

	addTabs();
	
}

void EffectsPage::configureToolbar()
{
	QVariantMap options;
	options.insert("color", QColor(255, 255, 255));
	options.insert("color-active", QColor(255, 255, 255));

	toolBar = new QToolBar("Tool Bar");
	toolBar->setIconSize(QSize(15, 15));	
	
	projectName->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
	projectName->setMinimumWidth(250);
	projectName->setText("Untitled Shader");
	projectName->setStyleSheet(StyleSheet::EffectsProjectName());

	connect(projectName, &QLineEdit::textEdited, [=](const QString text) {
		if (!currentProjectShader) return;   // no material open, nothing to rename
		currentProjectShader->setData(Qt::DisplayRole, text);
		currentProjectShader->setData(Qt::UserRole, text);
		newName = text;
	});

	connect(projectName, &QLineEdit::editingFinished, [=]() {
		saveShader();
		renameShader();
	});

	QAction *actionUndo = new QAction;
	actionUndo->setToolTip("Undo | Undo last action");
	actionUndo->setObjectName(QStringLiteral("actionUndo"));
	actionUndo->setIcon(fontIcons->icon(fa::reply, options));
	toolBar->addAction(actionUndo);

	QAction *actionRedo = new QAction;
	actionRedo->setToolTip("Redo | Redo last action");
	actionRedo->setObjectName(QStringLiteral("actionRedo"));
	actionRedo->setIcon(fontIcons->icon(fa::share, options));
	toolBar->addAction(actionRedo);

	// Through the page's own entry point, not scene->stack directly: one place
	// decides what "undo on this page" means, and the toolbar buttons, the
	// graph.undo verb and the shell's Ctrl+Z all go through it.
	connect(actionUndo, &QAction::triggered, [this]() { graphUndo(); });
	connect(actionRedo, &QAction::triggered, [this]() { graphRedo(); });

	toolBar->addSeparator();

	auto importBtn = new QAction;
	auto addBtn = new QAction;

	importBtn->setIcon(fontIcons->icon(fa::download, options));
	importBtn->setToolTip("Import shader");

	addBtn->setIcon(fontIcons->icon(fa::plus, options));
	addBtn->setToolTip("Create new shader");

	toolBar->addActions({ importBtn, addBtn });

	// this acts as a spacer
	QWidget* empty = new QWidget();
	empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	toolBar->addWidget(empty);

	QAction *actionSave = new QAction;
	actionSave->setObjectName(QStringLiteral("actionSave"));
	actionSave->setCheckable(false);
	actionSave->setToolTip("Export | Export the current scene");
	actionSave->setIcon(fontIcons->icon(fa::floppyo, options));
	toolBar->addAction(actionSave);

	QPushButton* downloadBtn = new QPushButton("Download Materials");
	//downloadBtn->setStyleSheet(StyleSheet::QPushButtonGreyscale());
	downloadBtn->setStyleSheet(StyleSheet::EffectsDownloadButton());
	connect(downloadBtn, &QPushButton::pressed, []() {
		QDesktopServices::openUrl(QUrl("https://www.jahshaka.com/get/materials/"));
	});
	toolBar->addWidget(downloadBtn);

	this->addToolBar(toolBar);

	// (a lambda, because saveShader is overloaded on the document now)
	connect(actionSave, &QAction::triggered, this, [this]() { saveShader(); });
	connect(importBtn, &QAction::triggered, this, &EffectsPage::importGraph);
	connect(addBtn, &QAction::triggered, this, [=]() {
		createNewGraph(true);
	});

	toolBar->setStyleSheet(StyleSheet::EffectsToolBar());

	empty->setStyleSheet(StyleSheet::EffectsEmptySpacer());
}

void EffectsPage::generateTileNode()
{
	QSize currentSize(90, 90);

	for (NodeLibraryItem *tile : mNodeLibrary->items) {
		if (tile->hidden) continue; // load aliases are not palette entries
		auto item = new QListWidgetItem;
		item->setText(tile->displayName);
		item->setData(Qt::DisplayRole, tile->displayName);
		item->setData(Qt::UserRole, tile->name);
		item->setSizeHint(defaultItemSize);
		item->setTextAlignment(Qt::AlignBottom | Qt::AlignHCenter);
		item->setFlags(item->flags() | Qt::ItemIsEditable);
		item->setIcon(tile->icon);
        item->setBackground(QColor(60, 60, 60));
		item->setData(MODEL_TYPE_ROLE, QString("node"));
		item->icon().addPixmap(QPixmap(":/icons/shader_overlay.png"));
		setNodeLibraryItem(item, tile);

	}
}

void EffectsPage::addTabs()
{
	for (int i = 0; i < (int)NodeCategory::PlaceHolder; i++) {
		auto wid = new ListWidget;
		wid->sceneOpenProbe = mSceneOpenProbe;
		wid ->setIconSize({ 40,40 });
		tabbedWidget->addTab(wid, NodeModel::getEnumString(static_cast<NodeCategory>(i)));
	}
}

void EffectsPage::setNodeLibraryItem(QListWidgetItem *item, NodeLibraryItem *tile)
{
	auto wid = static_cast<QListWidget*>(tabbedWidget->widget(static_cast<int>(tile->nodeCategory)));
	wid->addItem(item);
}

bool EffectsPage::createNewGraph(bool loadNewGraph)
{
	CreateNewDialog node(loadNewGraph);
	node.exec();

	if (node.result() == QDialog::Accepted) {
		auto preset = node.getPreset();
		createShader(preset, loadNewGraph, node.getName());
		return true;
	}
	return false;
}

void EffectsPage::updateAssetDock()
{
	// THE PAGE HOLDS A POINTER INTO THIS LIST (`currentProjectShader`), and
	// `clear()` DELETES the items. Refilling the drawer while that pointer
	// still names a freed item is the same class of crash as opening a
	// material with no tile (fix round F1) — so it is dropped here and
	// re-resolved from the GUID once the list is rebuilt, which is the only
	// identity that survives a refill.
	const QString openGuid = currentInfo().GUID;
	if (currentProjectShader && currentProjectShader->listWidget() == effects)
		currentProjectShader = nullptr;
	effects->clear();
#if(EFFECT_BUILD_AS_LIB)
	// THE TWO LIBRARY WORLDS MERGED (spec 2.4): the module lists the same
	// library MATERIAL bundles the Assets page does — there is no private
	// "Effects" world any more, and no ModelTypes::Shader tile.
	auto assets = dataBase->fetchAssetsByViewFilter(AssetViewFilter::AssetsView);
		for (const auto &asset : assets)  //dp something{
		{
			if (asset.type != static_cast<int>(ModelTypes::Material)) continue;
			// THE CUSTOM DRAWER IS THE USER'S OWN MATERIALS, once (the
			// four-drawer rule, OWNER_REVIEW 9).
			//
			// NOT an image's COMPANION: adding a picture to a project mints a
			// one-slot PBR material for it (ImageMaterial's `companionOf`
			// stamp) so the picture has a tile — that material is the
			// picture, it has no graph, and listing it here filled the
			// drawer with rows the user never authored. The stamp is the
			// identity, never the shape: a material the user built on the
			// same image is theirs and stays.
			//
			// NOT a material the open project already holds, either: that one
			// is in the PROJECT drawer, and a row in both drawers is the same
			// guid meaning two things in one window.
			{
				const QJsonObject props = QJsonDocument::fromJson(asset.properties).object();
				const QJsonObject blob = QJsonDocument::fromJson(asset.asset).object();
				const bool companion = !blob.value(QStringLiteral("companionOf")).toString().isEmpty()
				                       && !blob.contains(QStringLiteral("shadergraph"));
				if (companion) continue;
				// NOT A SHIPPED PRESET EITHER (phase 3): a seeded preset
				// bundle is an ordinary library Material row, and Custom is
				// the drawer of materials the user may EDIT. A preset lives
				// in Presets, read-only, and its Customise copy — an
				// ordinary guid — is what lands here.
				if (!MaterialBundle::shippedPresetName(asset.guid).isEmpty()) continue;
				if (mProject && !mProject->getProjectGuid().isEmpty()
				    && dataBase->isAssetPinnedBy(mProject->getProjectGuid(), asset.guid))
					continue;
			}
			{
				 
				auto item = new QListWidgetItem;
				item->setText(asset.name);
				item->setFlags(item->flags() | Qt::ItemIsEditable);
				item->setSizeHint(defaultItemSize);
				item->setTextAlignment( Qt::AlignHCenter | Qt::AlignBottom);

				item->setData(Qt::UserRole, asset.name);
				item->setData(Qt::DisplayRole, asset.name);
				item->setData(MODEL_GUID_ROLE, asset.guid);
				item->setData(MODEL_TYPE_ROLE, asset.type);
				ListWidget::updateThumbnailImage(asset.thumbnail, item);
				effects->addToListWidget(item);
			}
		}
	if (!openGuid.isEmpty() && !currentProjectShader)
		currentProjectShader = selectCorrectItemFromDrop(openGuid);
#endif
}


void EffectsPage::setProject(Project *project)
{
	mProject = project;
	if (assetWidget) assetWidget->project = project;
	if (membersPanel) { membersPanel->setProject(project); membersPanel->refresh(); }
	// An image picked inside a texture node is pinned into THIS project (and
	// only when there is one) — MATERIAL_BUNDLE_SPEC Q1.
	TextureManager::getSingleton()->setProject(project);
}

// ---- §3a selection bridge (graph.selectNode / selectedNode / deselect) ----

bool EffectsPage::selectGraphNode(const QString& nodeId)
{
	auto *scene = activeScene();
	return scene != nullptr && scene->selectNodeById(nodeId);
}

QString EffectsPage::selectedGraphNodeId()
{
	auto *scene = activeScene();
	return scene != nullptr ? scene->selectedNodeId() : QString();
}

void EffectsPage::deselectGraphNodes()
{
	if (auto *scene = activeScene()) scene->deselectAll();
}

QVariantMap EffectsPage::paletteTileRect(const QString &name)
{
	QVariantMap out;
	if (!tabbedWidget || !graphicsView) return out;

	const QString wanted = name.trimmed().toLower();
	for (int tab = 0; tab < tabbedWidget->count(); ++tab) {
		auto *list = qobject_cast<QListWidget *>(tabbedWidget->widget(tab));
		if (!list) continue;
		for (int row = 0; row < list->count(); ++row) {
			QListWidgetItem *item = list->item(row);
			if (item->text().trimmed().toLower() != wanted) continue;

			// SELECT THE TAB FIRST, and then make Qt lay it out NOW. A tab page
			// that has never been current has never been given a geometry —
			// QStackedLayout only positions the current widget — so its item
			// rects would be measured against a default-sized viewport. The
			// two activate() calls are what a show() would have done.
			tabbedWidget->setCurrentIndex(tab);
			if (QWidget *stack = list->parentWidget())
				if (QLayout *l = stack->layout()) l->activate();
			if (QLayout *l = list->layout()) l->activate();
			list->scrollToItem(item, QAbstractItemView::PositionAtCenter);

			QWidget *window = list->window();
			const QRect inViewport = list->visualItemRect(item);
			const QPoint topLeft = list->viewport()->mapTo(window, inViewport.topLeft());
			out["tab"] = tabbedWidget->tabText(tab);
			out["tabIndex"] = tab;
			out["x"] = topLeft.x();
			out["y"] = topLeft.y();
			out["w"] = inViewport.width();
			out["h"] = inViewport.height();
			// True only when the tile is actually inside the window the caller
			// is about to click in — a tile scrolled out of a clipped viewport,
			// or a window hanging off the screen, is not clickable and the
			// caller must be able to say so instead of clicking blind.
			const QRect visible(list->viewport()->mapTo(window, QPoint(0, 0)),
			                    list->viewport()->size());
			out["clickable"] = visible.contains(QRect(topLeft, inViewport.size()).center())
			                   && window->rect().contains(QRect(topLeft, inViewport.size()));

			// The DROP TARGET travels with the tile: a palette drag is only
			// meaningful onto this page's canvas, and a caller that had to
			// compute the canvas itself would be back to guessing window
			// fractions — which is the defect this verb exists to remove.
			const QRect canvas(graphicsView->mapTo(window, QPoint(0, 0)), graphicsView->size());
			QVariantMap canvasOut;
			canvasOut["x"] = canvas.x();
			canvasOut["y"] = canvas.y();
			canvasOut["w"] = canvas.width();
			canvasOut["h"] = canvas.height();
			out["canvas"] = canvasOut;
			// WHICH window these coordinates are in. This page is a QMainWindow
			// in its own right (it hosts the graph's docks), so window() is the
			// app's main window only while the page is parented into it — the
			// caller must be able to check that its idea of the window and this
			// one are the same rectangle instead of clicking into thin air.
			QVariantMap windowOut;
			windowOut["w"] = window->width();
			windowOut["h"] = window->height();
			out["window"] = windowOut;
			return out;
		}
	}
	return out;
}

bool EffectsPage::removeGraphNode(const QString& nodeId)
{
	auto *scene = activeScene();
	if (scene == nullptr) return false;
	if (!scene->deleteNodeById(nodeId)) return false;
	scene->update();
	return true;
}

bool EffectsPage::removeGraphConnection(const QString& connectionId)
{
	auto *scene = activeScene();
	if (scene == nullptr) return false;
	if (!scene->deleteConnectionById(connectionId)) return false;
	scene->update();
	return true;
}

// ---- the graph's edit stack (graph.undo / graph.redo, and the shell's
//      Ctrl+Z while the Materials space is active) --------------------------

bool EffectsPage::graphUndo()
{
	auto *stack = activeStack();
	if (!stack || !stack->canUndo()) return false;
	stack->undo();
	// The scene has to be told to repaint: the commands mutate node/connection
	// state directly and QGraphicsScene has no way to know (this is what the
	// page's own toolbar undo button did, and the graph view's deleted
	// shortcut before it).
	if (auto *scene = activeScene()) scene->update();
	return true;
}

bool EffectsPage::graphRedo()
{
	auto *stack = activeStack();
	if (!stack || !stack->canRedo()) return false;
	stack->redo();
	if (auto *scene = activeScene()) scene->update();
	return true;
}

// The node-search palette, reachable from the keyboard for the first time:
// the dialog existed and only Tab-over-the-view opened it.
bool EffectsPage::openNodeSearch()
{
	if (!graphicsView) return false;
	return graphicsView->openNodeSearch();
}

// The four edit chords, routed here by MainWindow when the Materials space is
// active (EDITOR_MULTISELECT_SPEC §2.6). Each one is exactly what the graph
// view's deleted QShortcut did, minus the ambiguity that made the chord
// unreliable the moment a second WindowShortcut claimed it.
bool EffectsPage::graphDeleteSelected()
{
	auto *scene = activeScene();
	if (!scene) return false;
	scene->deleteSelectedNodes();
	scene->update();
	return true;
}

bool EffectsPage::graphDuplicateSelected()
{
	auto *scene = activeScene();
	if (!scene) return false;
	scene->duplicateSelected();
	scene->update();
	return true;
}

bool EffectsPage::graphCopySelected()
{
	auto *scene = activeScene();
	if (!scene) return false;
	scene->copySelectedToClipboard();
	return true;
}

bool EffectsPage::graphPaste()
{
	auto *scene = activeScene();
	if (!scene) return false;
	scene->pasteFromClipboard();
	scene->update();
	return true;
}

int EffectsPage::graphUndoCount() const
{
	auto *stack = activeStack();
	return stack ? stack->index() : 0;
}

int EffectsPage::graphRedoCount() const
{
	auto *stack = activeStack();
	return stack ? stack->count() - stack->index() : 0;
}

void EffectsPage::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (mColumnsSized) return;
    mColumnsSized = true;
    applyColumnWidths();
    // AND AGAIN once this show's layout pass has run: a dock that has just
    // become visible is re-laid-out from its widget's sizeHint, which undoes a
    // resize done inside the show itself.
    QTimer::singleShot(0, this, [this]() { applyColumnWidths(); });
}

void EffectsPage::applyColumnWidths()
{
    // THE ONE WIDTH LAW (ui/style/panelmetrics.h), one resizeDocks per area:
    // a single call naming docks from two areas resolves against one of them
    // and leaves the other at its minimum.
    resizeDocks({ displayWidget, propertyWidget },
                { PanelMetrics::rightColumnWidth, PanelMetrics::rightColumnWidth },
                Qt::Horizontal);
    resizeDocks({ assetsDock, materialSettingsDock },
                { PanelMetrics::leftColumnWidth, PanelMetrics::leftColumnWidth },
                Qt::Horizontal);
}

QWidget *EffectsPage::leftColumn() const
{
    return assetsDock;
}

QWidget *EffectsPage::rightColumn() const
{
    // Display sits ABOVE Properties in the right column and is hidden until the
    // shell hands in the engine preview, so the column's width is Properties'
    // width — the widget that is always there.
    return propertyWidget;
}

void EffectsPage::setSceneOpenProbe(std::function<bool()> probe)
{
	mSceneOpenProbe = probe;
	if (presets) presets->sceneOpenProbe = probe;
	if (effects) effects->sceneOpenProbe = probe;
	if (assetWidget) assetWidget->sceneOpenProbe = probe;
}

void EffectsPage::setAssetWidgetDatabase(Database * db)
{
#if(EFFECT_BUILD_AS_LIB)
	TextureManager::getSingleton()->setDatabase(db);
    assetWidget->setUpDatabase(db);
	// The page's own handle is assigned in the constructor AFTER configureUI
	// built the panels, so the Members panel is given the library here — the
	// one place every caller passes through.
	if (membersPanel) membersPanel->setDatabase(db);
#endif
}

void EffectsPage::renameShader()
{
	// No open material, no tile, nothing to rename (fix round F1's family:
	// the page's item pointer is null whenever no material is open, and a
	// drawer refill can clear it).
	if (!currentProjectShader) return;
#if(EFFECT_BUILD_AS_LIB)
	// THROUGH THE ONE NAME WRITER (PRESET-UNIFY-1 fix round 2), which is where
	// both name laws live: a shipped preset cannot be renamed, and nothing
	// else may take a shipped preset's name. `Database::renameAsset` knows
	// neither, and this door went straight to it.
	assettags::rename(dataBase, currentProjectShader->data(MODEL_GUID_ROLE).toString(),
	                  currentProjectShader->data(Qt::DisplayRole).toString());
#else
	auto filePath = QDir().filePath(AppPaths::dataRoot() + "/Materials/MyFx/");
	if (!QDir(filePath).exists()) return;
	auto shaderFileOld = new QFile(filePath + oldName);
	auto shaderFileNew = new QFile(filePath + newName);
	QDir().rename(shaderFileOld->fileName() , shaderFileNew->fileName());
	
#endif
	oldName = currentProjectShader->data(Qt::DisplayRole).toString();
}

bool EffectsPage::eventFilter(QObject * watched, QEvent * event)
{
	// (the graph-global property list and its drag-to-canvas flow died with
	// §3b — nothing page-level to intercept any more)
	return QObject::eventFilter(watched, event);
}

GraphNodeScene *EffectsPage::createNewScene(MaterialDocument *doc)
{
    auto scene = new GraphNodeScene(this);
	scene->setUndoRedoStack(doc->stack);
    scene->setBackgroundBrush(QBrush(QColor(60, 60, 60)));

	connect(scene, &GraphNodeScene::graphInvalidated, this, [this, doc]()
	{
		// Engine preview: every path that re-evaluates the graph ends here
		// (graphInvalidated covers connections, deletions and value edits
		// alike), so this one debounced hook keeps the Display dock live. One
		// preview, so only the document on screen bakes.
		if (doc == activeDoc()) schedulePreviewUpdate();
		// ...AND THE EDIT IS WRITTEN DOWN (the owner, 2026-09-19: "I added a UV
		// node and connected it to the texture, went to the editor and back and
		// the node connection is gone; I had to toggle between materials for it
		// to stay"). THIS SIGNAL IS EVERY REAL EDIT — a connection, a deletion,
		// a value typed into a node — and until now the only thing that ever
		// reached `saveShader` on its own was `nodeMoved`, the timer below. So
		// a graph you EDITED was kept only if you also happened to DRAG a node
		// (or rename the material, or switch to another one, which saves on the
		// way out): the work was lost on any other exit, silently. The same
		// debounce carries it — the bake is hash-cached, so an edit
		// that changes nothing re-saves cheaply — and `restoringGraph` still
		// guards the rebuild, so loading a graph writes nothing.
		//
		// THE TIMER IS THIS DOCUMENT'S: the edit lands on the material it was
		// made in even if the user has moved to another tab meanwhile.
		if (!restoringGraph) doc->saveTimer->start();
	});

	connect(scene, &GraphNodeScene::nodeMoved, this, [this, doc]() {
		// drags fire per step; setPos during graph builds must not count
		if (!restoringGraph) doc->saveTimer->start();
	});

	connect(scene, &GraphNodeScene::loadGraph, this, [this](const QString &guid) {
		// THE HANDLER NAMES THE MATERIAL, IT DOES NOT BECOME IT (fix round):
		// writing the page's identity here meant a refused open left the page
		// editing the previous graph under this guid. `loadGraph` adopts the
		// material once it has one — onto a document of its own.
		// A tile dropped on the canvas opens at the scope of the drawer it
		// was dragged out of (the four-drawer rule).
		loadGraph(guid, originForItem(guid));
	});

    return scene;
}

void EffectsPage::setEnginePreview(IMaterialPreviewWidget *preview)
{
	enginePreview = preview;
	if (!preview) return;

	// The dock's central slot was left empty in engine mode (the GL SceneWidget
	// must never be realized on xcb), so nothing is deleted here.
	if (displayWindow) displayWindow->setCentralWidget(preview->previewWidget());
	displayWidget->show();
	displayWidget->toggleViewAction()->setEnabled(true);

	schedulePreviewUpdate();
}

void EffectsPage::schedulePreviewUpdate()
{
	if (enginePreview && previewUpdateTimer) previewUpdateTimer->start();
}

void EffectsPage::updateEnginePreviewMaterial()
{
	auto *doc = activeDoc();
	if (!enginePreview || !doc || !doc->graph) return;

	// MATERIALS_EVALUATOR_SPEC section 2: preview bakes run off-thread over
	// the compiled BakeProgram (a pure value object - the graph's QWidgets
	// are only touched here, on the GUI thread), latest-wins by generation.
	//
	// THE GENERATION IS THE DOCUMENT'S (MATERIALS_TABS_SPEC §2.5): a bake
	// started for one tab that lands after another tab is active must not
	// paint the wrong material, and the watcher is a CHILD of the document, so
	// closing a tab takes its in-flight bakes with it.
	const quint64 generation = ++doc->previewGeneration;
	auto compiled = materials::GraphBaker::compile(doc->graph, MaterialHelper::textureResolver());

	materials::GraphBaker::Options opts;
	opts.resolution = 256; // preview quality, fixed
	const QString guid = doc->info.GUID.isEmpty()
	                         ? QStringLiteral("preview") : doc->info.GUID;
	opts.outputDir = QDir::temp().absoluteFilePath("jahshaka-preview-bakes/" + guid);
	opts.relativePrefix = opts.outputDir + "/"; // absolute: no resolver round-trip

	auto watcher = new QFutureWatcher<materials::GraphBaker::Result>(doc);
	connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, doc, generation]() {
		watcher->deleteLater();
		if (generation != doc->previewGeneration) return; // a newer bake is in flight
		if (doc != activeDoc()) return;                   // another tab is on screen
		if (!enginePreview) return;
		auto material = PbrGraphEvaluator::materialFromValues(
		    watcher->result().eval.values, MaterialHelper::textureResolver());
		if (material) enginePreview->setPreviewMaterial(material);
	});
	watcher->setFuture(QtConcurrent::run([compiled, opts]() {
		return materials::GraphBaker::runCompiled(compiled, opts);
	}));
}

QListWidgetItem * EffectsPage::selectCorrectItemFromDrop(QString guid)
{
	// THE PRESETS DRAWER IS A DRAWER TOO (PRESET-UNIFY-1). A shipped preset
	// opens read-only now, and every line after the lookup in `loadGraph`
	// reads the tile — so leaving this blind to the Presets list meant a
	// preset's graph appeared with no name, no restored node positions and
	// no guid for the banner's Customise button.
	for (int i = 0; i < presets->count(); i++)
	{
		if (guid == presets->item(i)->data(MODEL_GUID_ROLE)) {
			return presets->item(i);
		}
	}

	for (int i = 0; i < effects->count(); i++)
	{
		if (guid == effects->item(i)->data(MODEL_GUID_ROLE)) {
			return effects->item(i);
		}
	}

#if(EFFECT_BUILD_AS_LIB)
	for (int i = 0; i < assetWidget->assetViewWidget->count(); i++)
	{
		if (guid == assetWidget->assetViewWidget->item(i)->data(MODEL_GUID_ROLE)) {
			return assetWidget->assetViewWidget->item(i);
		}
	}
#endif


    return nullptr;
}

shaderInfo::Origin EffectsPage::originForItem(QString guid)
{
	// WHICH DRAWER holds this tile — the one question that decides whose copy
	// of a material an edit belongs to (the four-drawer rule). It is asked of
	// the WIDGETS, not of the catalog, because the catalog cannot answer it:
	// a pinned material is one row and the drawers are two views of it.
	return selectCorrectTabForItem(guid) == static_cast<int>(ShaderWorkspace::Projects)
	           ? shaderInfo::Origin::Project
	           : shaderInfo::Origin::Library;
}

int EffectsPage::selectCorrectTabForItem(QString guid)
{
	for (int i = 0; i < presets->count(); i++)
	{
		if (guid == presets->item(i)->data(MODEL_GUID_ROLE))	return (int) ShaderWorkspace::Presets;
	}

	for (int i = 0; i < effects->count(); i++)
	{
		if (guid == effects->item(i)->data(MODEL_GUID_ROLE))	return (int) ShaderWorkspace::MyEffects;
	}

#if(EFFECT_BUILD_AS_LIB)
	for (int i = 0; i < assetWidget->assetViewWidget->count(); i++)
	{
		if (guid == assetWidget->assetViewWidget->item(i)->data(MODEL_GUID_ROLE))	return (int)ShaderWorkspace::Projects;
	}
#endif
	return 0;
}






void EffectsPage::configureConnections()
{
#if(EFFECT_BUILD_AS_LIB)
	// THE DRAWER IS THE SCOPE (the four-drawer rule). A PROJECTS tile opens
	// and saves the project's own copy; a CUSTOM tile opens and saves the
	// library original — even while a project holds it, which the old
	// "is it pinned?" inference made impossible.
	connect(assetWidget, &ShaderAssetWidget::loadToGraph, [=](QListWidgetItem * item) {
		// The handler names the material; loadGraph adopts it (fix round).
		loadGraph(item->data(MODEL_GUID_ROLE).toString(), shaderInfo::Origin::Project);
	});
#endif

    connect(effects, &QListWidget::itemDoubleClicked, [=](QListWidgetItem *item) {
        // The handler names the material; loadGraph adopts it (fix round).
        loadGraph(item->data(MODEL_GUID_ROLE).toString(), shaderInfo::Origin::Library);
    });

    connect(effects, &QListWidget::itemPressed, [=](QListWidgetItem *item){
        pressedShaderInfo.name = item->data(Qt::DisplayRole).toString();
        pressedShaderInfo.GUID = item->data(MODEL_GUID_ROLE).toString();
    });

	// SELECTING A PRESET SHOWS ITS GRAPH (PRESET-UNIFY-1, the owner
	// 2026-09-20: "if i select a preset i should see the graph, i dont see
	// it… we said right click a preset - customise creates a custom preset
	// out of it, not just selecting a preset, its messy that way").
	//
	// There were THREE handlers on this one signal before: two matched a
	// GRAPH TEMPLATE by name and opened it as a brand-new unsaved material,
	// and the third turned a double-click on a shipped preset into Customise
	// — a gesture that WROTE a row for a gesture that reads. One handler now,
	// and it opens the preset's own graph READ-ONLY: the canvas shows exactly
	// the material the tray applies, and nothing is written down until the
	// user asks for their own copy.
	connect(presets, &QListWidget::itemDoubleClicked, [=](QListWidgetItem *item) {
		const QString guid = item->data(MODEL_GUID_ROLE).toString();
		if (guid.isEmpty()) return;
		// The tile's LABEL is elided to fit 90 px; the name is the preset's —
		// and `adoptGraph` takes it from the shipped list itself, once the
		// graph has loaded (fix round). Writing it here named the page after a
		// material that might not open.
		loadGraph(guid, shaderInfo::Origin::Library);
	});

	

	// (No page-level Space here either, since 2026-09-04. There WAS one — a
	// Qt::WindowShortcut on this page opening SearchDialog — and it was the
	// SECOND claimant for the chord: MainWindow's ShortcutRegistry "tool.cycle"
	// owns Space too, both Qt::WindowShortcut, so with this page visible Qt
	// dispatched it AMBIGUOUSLY and QShortcut answers an ambiguous event by
	// doing nothing. Measured on Xvfb with qt.gui.shortcutmap.debug: two
	// ExactMatch entries for QKeySequence("Space") and no dialog. That is why
	// the node-search palette had never once opened from the keyboard.
	//
	// The registry entry stays the single claimant and MainWindow routes by
	// space (spaceKeyActiveSpace -> EffectsPage::openNodeSearch), exactly the
	// pattern undoActiveSpace established for Ctrl+Z. Tab over the graph view
	// still opens it too. This one also used `this->graph` and a fixed {0,0}
	// position, where the live graph is scene->getNodeGraph().
	//
	// (No page-level Ctrl+Z / Ctrl+Shift+Z here. There WERE two, spelled
	// "crtl+z" and "crtl+shift+z" — QKeySequence parses the typo'd modifier
	// as nothing, so neither had ever fired since the day they were written
	// (deep audit 2026-09, area 1).
	//
	// Spelling them correctly would not have made them work, and would have
	// made the page's shortcut story worse. Two live claimants already own
	// Ctrl+Z whenever this page is visible — MainWindow's ShortcutRegistry
	// "edit.undo" and GraphicsView::addShortcuts' own graph undo, both
	// Qt::WindowShortcut — so Qt dispatches the chord AMBIGUOUSLY here and
	// QShortcut answers an ambiguous event by doing nothing. Measured on
	// Xvfb with qt.gui.shortcutmap.debug: on this page the log says "The
	// following shortcuts are about to be activated ambiguously" and sends
	// QShortcutEvent("Ctrl+Z", -4, TRUE); on the editor page, where the
	// graph view is not visible, the same key sends (..., -32, false) and
	// the editor's undo runs. A third claimant would not have changed that.
	//
	// The duplicates are deleted rather than fixed: this page's `stack` IS
	// the graph scene's stack (scene->setUndoRedoStack(stack) below), so
	// they were exact duplicates of GraphicsView's pair anyway. Making the
	// chord work on this page is a decision about WHICH undo owns it —
	// reported to the lead, not made here.)

    //connections for MyFx sections

    connect(effects, &ListWidget::renameShader, [=](QString guid){
        auto item = selectCorrectItemFromDrop(guid);
        effects->editItem(item);
    });
    connect(effects, &ListWidget::exportShader, [=](QString guid){
        exportEffect(guid);
    });
    connect(effects, &ListWidget::editShader, [=](QString guid){
        loadGraph(guid, shaderInfo::Origin::Library);   // the Custom drawer is the library's
    });
    connect(effects, &ListWidget::deleteShader, [=](QString guid){
        deleteShader(guid);
    });
    connect(effects, &ListWidget::duplicateShader, [=](QString guid){
        duplicateShader(guid);
    });
    connect(effects, &ListWidget::createShader, [=](QString guid){
        createNewGraph();
    });
	connect(effects, &ListWidget::importShader, [=](QString guid) {
		importGraph();
	});
	connect(effects, &ListWidget::addToProject, [=](QListWidgetItem *item) {
		// ADD TO PROJECT IS A PIN, NOT A CLONE (MATERIAL_BUNDLE_SPEC 5). It
		// used to make TWO assets out of one gesture — a cloned Shader row
		// (copying every texture into the project folder under a guid-shaped
		// name, outside the store) PLUS a generated Material stub with a flat
		// `.material` file beside it — which is exactly the owner's "adding a
		// custom material to a project lands as two parts, not a bundle". It
		// is now the same call every other asset uses: the bundle and its
		// closure are pinned at the version the project took.
		const QString guid = item->data(MODEL_GUID_ROLE).toString();
		if (guid.isEmpty() || !mProject || mProject->getProjectGuid().isEmpty()) return;
		const auto added = ProjectAssets::addToProject(guid, dataBase, mProject,
		                                              ProjectAssets::AddKind::Direct);
		if (!added.ok()) { irisLog("add to project: " + added.error); return; }
		refreshShaderGraph();
		tabWidget->setCurrentIndex((int)ShaderWorkspace::Projects);
		if (auto *pinned = selectCorrectItemFromDrop(guid))
			ListWidget::highlightNodeForInterval(2, pinned);
		// It is the PROJECT's copy the user is now looking at.
		loadGraph(guid, shaderInfo::Origin::Project);
	});


	// R18 — CUSTOMISE A SHIPPED PRESET. The drawer's one gesture on a
	// read-only tile, and it calls the SAME implementation
	// `materials.createFromPreset` calls (the suffix rule lives there, once):
	// an ordinary editable bundle named "<Preset>-1", pinned into the open
	// project so the project drawer and the editor's tray show it too.
	connect(presets, &ListWidget::customisePreset, [=](QString presetGuid) {
		MaterialPresetSeeder::instance().finishNow();   // one importer at a time
		QString error;
		const QString copy = MaterialPresetAssets::customise(presetGuid, QString(),
		                                                     dataBase, mProject, &error);
		if (copy.isEmpty()) { irisLog("Customise: " + error); return; }
		refreshShaderGraph();
		// It is the user's material now: show them where it landed. In a
		// project it is the Project drawer (Customise pins it), otherwise
		// Custom.
		const bool pinned = mProject && !mProject->getProjectGuid().isEmpty()
		                    && dataBase->isAssetPinnedBy(mProject->getProjectGuid(), copy);
		tabWidget->setCurrentIndex(static_cast<int>(pinned ? ShaderWorkspace::Projects
		                                                   : ShaderWorkspace::MyEffects));
		if (auto *tile = selectCorrectItemFromDrop(copy))
			ListWidget::highlightNodeForInterval(2, tile);
		// AND IT OPENS (PRESET-UNIFY-1, the owner: "a custom preset is a new
		// material based on the preset it was customised from"). The copy
		// carries the preset's GRAPH now, so there is something to open —
		// which is the point of the gesture: the user asked to edit this
		// material, and the node editor is where they do it.
		// (The name comes from the row inside `adoptGraph`, after the graph
		// has loaded — fix round.)
		loadGraph(copy, pinned ? shaderInfo::Origin::Project : shaderInfo::Origin::Library);
	});

    // change: any settings changed
    //
    // A LOCKED GRAPH TAKES NO SETTINGS EDIT EITHER (PRESET-UNIFY-1 fix round
    // 2). The canvas was locked and these two were not, so Blend Mode on a
    // shipped preset still flipped — and became an undoable command on a
    // graph nothing will ever save. The docks are DISABLED in `setReadOnly`,
    // which is what the user sees; these guards are what makes it true for a
    // signal that arrives any other way.
    connect(materialSettingsWidget, &MaterialSettingsWidget::settingsChanged,[=](MaterialSettings settings){
		if (mShowingDocument || restoringGraph) return;   // a rebind, not an edit
		if (isReadOnly() || !activeGraph()) return;
		auto command = new MaterialSettingsChangeCommand(activeGraph(), settings, materialSettingsWidget);
		activeStack()->push(command);
		nodePropertiesPanel->refreshSettings();
    });

	// §3a: the panel's master/graph settings views push through the SAME
	// undo command the left settings dock uses — one edit stack
	connect(nodePropertiesPanel, &NodePropertiesPanel::settingsEdited, [=](MaterialSettings settings) {
		if (mShowingDocument || restoringGraph) return;   // a rebind, not an edit
		if (isReadOnly() || !activeGraph()) return;
		auto command = new MaterialSettingsChangeCommand(activeGraph(), settings, materialSettingsWidget);
		activeStack()->push(command);
		nodePropertiesPanel->refreshSettings();
	});

    //connection for renaming item
    connect(effects->itemDelegate(), &QAbstractItemDelegate::commitData,[=](){
        //item finished editing
        editingFinishedOnListItem();
    });

}

void EffectsPage::editingFinishedOnListItem()
{
    QListWidgetItem *item = selectCorrectItemFromDrop(pressedShaderInfo.GUID);
    if (!item) return;   // the row this edit belonged to is no longer in a drawer
    auto oldName = pressedShaderInfo.name;
    auto newName = item->data(Qt::DisplayRole).toString();

	if (oldName == newName) return;

#if(EFFECT_BUILD_AS_LIB)
    // A RENAME IS A DEFINITION WRITE (F11). It used to write the row's BLOB
    // and the row's name and stop — so the stored DEFINITION kept the old
    // name and the next save, which builds the definition from the graph,
    // put the old name straight back. It also reached for
    // `shadergraph.graph.settings`, a nesting the serializer does not write
    // (the settings are at `shadergraph.settings`), so even the blob's copy
    // never moved.
    //
    // Read at the scope this material is open at, set the name in BOTH
    // places it lives — the definition's own `name` and the graph payload's
    // settings — and write through the ONE writer.
    {
        const shaderInfo::Origin origin = originForItem(pressedShaderInfo.GUID);
        const bool projectScope = origin == shaderInfo::Origin::Project;
        QJsonObject definition = MaterialBundle::read(dataBase, pressedShaderInfo.GUID,
                                                      projectScope ? mProject : nullptr);
        if (!definition.isEmpty()) {
            definition[QStringLiteral("name")] = newName;
            QJsonObject shadergraph = definition[QStringLiteral("shadergraph")].toObject();
            if (!shadergraph.isEmpty()) {
                QJsonObject settings = shadergraph[QStringLiteral("settings")].toObject();
                settings[QStringLiteral("name")] = newName;
                shadergraph[QStringLiteral("settings")] = settings;
                definition[QStringLiteral("shadergraph")] = shadergraph;
            }
            const auto written = MaterialBundle::write(
                dataBase, mProject, pressedShaderInfo.GUID, definition,
                projectScope ? MaterialBundle::Scope::Project
                             : MaterialBundle::Scope::Library);
            if (!written.ok) irisLog("rename: " + written.error);
        }
    }
    // THROUGH THE ONE NAME WRITER (fix round 2) — see renameShader.
    if (!assettags::rename(dataBase, pressedShaderInfo.GUID, newName)) {
        // Refused: put the tile's label back, or the drawer would show a name
        // the catalog does not have.
        irisLog("rename: '" + newName + "' was refused");
        item->setData(Qt::DisplayRole, oldName);
        return;
    }
#else
    // get json obj from file and edit graph like above

    auto filePath = QDir().filePath(AppPaths::dataRoot() + "/Materials/MyFx/");
    if (!QDir(filePath).exists()) return;
    auto shaderFileOld = new QFile(filePath + oldName);
    auto shaderFileNew = new QFile(filePath + newName);
    QDir().rename(shaderFileOld->fileName() , shaderFileNew->fileName());
#endif

	item->setData(Qt::DisplayRole, newName);

    // THE DOCUMENT THAT IS THIS MATERIAL takes the new name — not "the
    // current one" (MATERIALS_TABS_SPEC §2.6).
    renameOpenDocuments(pressedShaderInfo.GUID, newName);

	pressedShaderInfo = shaderInfo();
}

void EffectsPage::addMenuToSceneWidget()
{
	QMenu *modelMenu = new QMenu("Model");
	QMenu *backgroundMenu = new QMenu("Background");
	modelMenu->setStyleSheet(StyleSheet::EffectsPreviewMenu());
	backgroundMenu->setStyleSheet(StyleSheet::EffectsPreviewMenu());

	QMainWindow *window = new QMainWindow;
	QToolBar *bar = new QToolBar;

	window->menuBar()->addMenu(modelMenu);
	window->menuBar()->addMenu(backgroundMenu);
	displayWidget->setWidget(window);
	displayWindow = window;
	// The central slot stays empty until Studio hands in the engine-rendered
	// preview (setEnginePreview).

	auto cubeAction = new QAction("Cube");
	connect(cubeAction, &QAction::triggered, [=]() {
		if (enginePreview) enginePreview->setPreviewModel(IMaterialPreviewWidget::Model::Cube);
	});
	auto planeAction = new QAction("Plane");
	connect(planeAction, &QAction::triggered, [=]() {
		if (enginePreview) enginePreview->setPreviewModel(IMaterialPreviewWidget::Model::Plane);
	});
	auto sphereAction = new QAction("Sphere");
	connect(sphereAction, &QAction::triggered, [=]() {
		if (enginePreview) enginePreview->setPreviewModel(IMaterialPreviewWidget::Model::Sphere);
	});
	auto cylinderAction = new QAction("Cylinder");
	connect(cylinderAction, &QAction::triggered, [=]() {
		if (enginePreview) enginePreview->setPreviewModel(IMaterialPreviewWidget::Model::Cylinder);
	});
	auto capsuleAction = new QAction("Capsule");
	connect(capsuleAction, &QAction::triggered, [=]() {
		if (enginePreview) enginePreview->setPreviewModel(IMaterialPreviewWidget::Model::Capsule);
	});
	auto torusAction = new QAction("Torus");
	connect(torusAction, &QAction::triggered, [=]() {
		if (enginePreview) enginePreview->setPreviewModel(IMaterialPreviewWidget::Model::Torus);
	});

	modelMenu->addActions({cubeAction,
						   planeAction,
						   sphereAction,
						   cylinderAction,
						   capsuleAction,
						   torusAction,
		});

	auto whiteAction = new QAction("White");
	connect(whiteAction, &QAction::triggered, [=]() {
		if (enginePreview) enginePreview->setPreviewBackground(QColor(255, 255, 255));
	});

	auto grayAction = new QAction("Gray");
	connect(grayAction, &QAction::triggered, [=]() {
		if (enginePreview) enginePreview->setPreviewBackground(QColor(125, 125, 125));
	});


	auto blackAction = new QAction("Black");
	connect(blackAction, &QAction::triggered, [=]() {
		if (enginePreview) enginePreview->setPreviewBackground(QColor(0, 0, 0));
	});
	backgroundMenu->addActions({ whiteAction, grayAction, blackAction});

	cubeAction->setCheckable(true);
	planeAction->setCheckable(true);
	sphereAction->setCheckable(true);
	cylinderAction->setCheckable(true);
	capsuleAction->setCheckable(true);
	torusAction->setCheckable(true);
	whiteAction->setCheckable(true);
	blackAction->setCheckable(true);

	sphereAction->setChecked(true);
	whiteAction->setChecked(true);

	auto screenShotBtn = new QPushButton("screenshot");
	bar->addWidget(screenShotBtn);
	connect(screenShotBtn, &QPushButton::clicked, [=]() {
		
	});

	// model group
	auto modelGroup = new QActionGroup(this);
	modelGroup->addAction(sphereAction);
	modelGroup->addAction(planeAction);
	modelGroup->addAction(cubeAction);
	modelGroup->addAction(cylinderAction);
	modelGroup->addAction(capsuleAction);
	modelGroup->addAction(torusAction);
	modelGroup->setExclusive(true);

	// background group
	modelGroup = new QActionGroup(this);
	modelGroup->addAction(whiteAction);
	modelGroup->addAction(blackAction);
	modelGroup->setExclusive(true);
}

}
