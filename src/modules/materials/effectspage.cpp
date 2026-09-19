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
#include "services/materialbundle.h"
#include "services/materialpresetassets.h"
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
	stack = new QUndoStack;
	scene = nullptr;
	// Debounce for the engine preview: one evaluation per burst of edits
	// (graphInvalidated fires per value change while a slider drags).
	previewUpdateTimer = new QTimer(this);
	previewUpdateTimer->setSingleShot(true);
	previewUpdateTimer->setInterval(300); // MATERIALS_EVALUATOR_SPEC section 2
	connect(previewUpdateTimer, &QTimer::timeout, this, &EffectsPage::updateEnginePreviewMaterial);

	// Moved nodes persist on their own (owner request): a debounced save
	// after the last position change, so re-opening a graph restores the
	// arrangement without an explicit save click. The bake is hash-cached, so
	// an unchanged graph re-saves cheaply.
	// THE GRAPH'S AUTOSAVE. It began as a node-position debounce and is now the
	// one hook every edit reaches (see graphInvalidated in createNewScene): a
	// move, a connection, a deletion, a value. The name is kept because the
	// member is referenced in three places and the behaviour is the same —
	// "write the graph 1.5 s after the last change".
	positionSaveTimer = new QTimer(this);
	positionSaveTimer->setSingleShot(true);
	positionSaveTimer->setInterval(1500);
	connect(positionSaveTimer, &QTimer::timeout, this, [this]() {
		if (!currentShaderInformation.GUID.isEmpty()) saveShader();
	});
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

	newNodeGraph();
	generateTileNode();
	configureStyleSheet();
	configureAssetsDock();
    configureConnections();
	setMinimumSize(300, 400);
    loadShadersFromDisk();

	assetView = nullptr;
}

void EffectsPage::setNodeGraph(NodeGraph *graph)
{
	restoringGraph = true;
	TextureManager::getSingleton()->clearTextures();

    auto newScene = createNewScene();
	graphicsView->setScene(newScene);
	graphicsView->setAcceptDrops(true);
    newScene->setNodeGraph(graph);

    // delete old scene and reassign new scene
    if (scene) {
        scene->deleteLater();
    }
    scene = newScene;


	materialSettingsWidget->setMaterialSettings(graph->settings);

	// §3a: the right dock follows the new scene's selection
	nodePropertiesPanel->setGraph(graph);
	nodePropertiesPanel->setScene(scene);

	stack->clear(); // clears stack, later to add seperate routes for each node addition
	this->graph = graph;
	restoringGraph = false;

	schedulePreviewUpdate();
}

void EffectsPage::newNodeGraph(QString *shaderName, int *templateType, QString *templateName)
{
    auto graph = new NodeGraph;
	graph->setNodeLibrary(new LibraryV1());
    // new graphs author PBR (Option B) - legacy Surface graphs still load
    auto masterNode = new PbrMasterNode();
    graph->addNode(masterNode);
    graph->setMasterNode(masterNode);
    setNodeGraph(graph);
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
	setCurrentShaderItem();
}

EffectsPage::~EffectsPage()
{
    
}

void EffectsPage::saveShader()
{
	if (currentShaderInformation.GUID == "") {
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
		const auto build = materials::buildDefinition(graph, currentShaderInformation.GUID,
		                                              dataBase, mProject);
		if (!build.ok()) {
			irisLog("saveShader: " + build.error);
			reportSaveRefused(build.error);
		} else {
			// WHOSE VERSION (the owner's model, spec 12 Q2): the DRAWER the
			// material was opened from decides. A Projects tile is edited as
			// the project's own — its pin moves, the library original does
			// not; a Custom tile publishes to the library. This used to ask
			// "does the project pin it?", so a material the project held
			// could never be edited as the library's, and the same guid
			// meant two things in one window.
			const bool projectOwns =
			    currentShaderInformation.origin == shaderInfo::Origin::Project
			    && mProject && !mProject->getProjectGuid().isEmpty()
			    && dataBase->isAssetPinnedBy(mProject->getProjectGuid(),
			                                 currentShaderInformation.GUID);
			const auto written = MaterialBundle::write(
			    dataBase, mProject, currentShaderInformation.GUID, build.definition,
			    projectOwns ? MaterialBundle::Scope::Project
			                : MaterialBundle::Scope::Library);
			if (!written.ok) {
				irisLog("saveShader: " + written.error);
				// A REFUSED SAVE IS TOLD, not logged. This runs on the 1.5 s
				// autosave, so a refusal the user cannot see means they keep
				// working on a graph nothing is writing down — an hour of
				// work lost silently, which is exactly the shape of the
				// defect the path guard exists to prevent.
				reportSaveRefused(written.error);
			} else if (mSaveRefused) {
				SceneIssues::instance().clear(QStringLiteral("material.save:")
				                              + currentShaderInformation.GUID);
				mSaveRefused = false;
			}
			// AND THE EDIT REACHES THE SCENE (R19 D2). Every mesh wearing this
			// material is re-dressed from the definition just written, through
			// the ONE apply the drop and the verb use. Until this lane a graph
			// edit reached the scene only by accident — through a material
			// SWITCH, and only while the Projects tab happened to be current.
			else if (mMaterialChanged) mMaterialChanged(currentShaderInformation.GUID);
		}
	}
	// Thumbnail: queued, never inline. The graph's baked material renders on
	// the preview sphere through the shell's thumbnail queue (one request per
	// tick, main thread) and lands in onShaderThumbnail — saving must not block
	// on a render, and the stored asset data must already be written when the
	// request is served (the renderer re-reads it from the database).
	requestShaderThumbnail(currentShaderInformation.GUID);
	// A SAVE CAN CHANGE THE MEMBERS: the final bake mints its maps as member
	// textures, and a picture picked in a texture node becomes one.
	if (membersPanel) membersPanel->refresh();
#else
	// The STANDALONE build (no library): the graph goes to a file, unchanged.
	{
		auto filePath = QDir().filePath(AppPaths::dataRoot() + "/Materials/MyFx/");
		if (!QDir(filePath).exists()) QDir().mkpath(filePath);
		const QJsonObject matObj = MaterialHelper::serialize(graph);
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

	int currentTab = selectCorrectTabForItem(currentShaderInformation.GUID);
	auto item = selectCorrectItemFromDrop(currentShaderInformation.GUID);
	if (item) {
		// The tile keeps whatever it has until the render arrives (blanking it
		// here is what made every saved graph show the generic file icon).
		tabWidget->setCurrentIndex(currentTab);
		ListWidget::highlightNodeForInterval(2, item);
	}
}

void EffectsPage::reportSaveRefused(const QString &why)
{
	// The scene-issue bar, not a toast: a toast leaves, and this condition
	// stays true until the material is fixed (services/sceneissues.h). The id
	// is per material, so a second refused autosave of the same graph is a
	// no-op rather than a second line.
	mSaveRefused = true;
	SceneIssue issue;
	issue.id = QStringLiteral("material.save:") + currentShaderInformation.GUID;
	issue.kind = QStringLiteral("material.save");
	issue.nodeName = currentShaderInformation.name;
	issue.message = tr("'%1' could not be saved: %2")
	                    .arg(currentShaderInformation.name, why);
	issue.action = tr("Your edits are still on screen but are NOT being written down. "
	                  "Re-pick the image on the node the message names, then save again.");
	SceneIssues::instance().raise(issue);
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

void EffectsPage::deleteMaterialFile(QString filename)
{
#if(EFFECT_BUILD_AS_LIB)

    QJsonDocument doc;
    doc.setObject(graph->serialize());

#endif
}

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

NodeGraph* EffectsPage::importGraphFromFilePath(QString filePath, bool assign)
{
	QFile file(filePath);
	file.open(QIODevice::ReadOnly | QIODevice::Text);
	auto val = file.readAll();
	file.close();
	QJsonDocument d = QJsonDocument::fromJson(val);

	auto obj = d.object();
	auto graph = MaterialHelper::extractNodeGraphFromMaterialDefinition(obj);

	if (assign) {
		this->setNodeGraph(graph);
		schedulePreviewUpdate();
	}
	
	return graph;
}

void EffectsPage::loadGraph(QString guid, shaderInfo::Origin origin)
{
	// A SHIPPED PRESET DOES NOT OPEN (phase 3). It is read-only — the
	// definition writer refuses it by name — and it has no graph to show, so
	// opening one would put an EMPTY editor in front of the user and refuse
	// their first save. A preset is reachable here only from the PROJECT
	// drawer, where a pinned one is an ordinary tile; the way to change it is
	// Customise, which is what this says.
	const QString shipped = MaterialBundle::shippedPresetName(guid);
	if (!shipped.isEmpty()) {
		irisLog("loadGraph: '" + shipped + "' is a material the app ships and is read-only "
		        "- Customise it (Presets drawer, right-click) to edit your own copy");
		return;
	}

	// The origin is set BEFORE the read, because `fetchAsset` reads the
	// definition at this scope.
	currentShaderInformation.origin = origin;
	restoringGraph = true;
	// Parented + deleted below: this used to leak one orphanable top-level
	// window per loadGraph call.
	auto progressDialog = new ProgressDialog(this);
	progressDialog->setPumpsEventLoop(true);   // synchronous graph load

	progressDialog->setRange(0, 10);
	progressDialog->setValueAndText(1, "Preparing graph");
	progressDialog->show();

	NodeGraph *graph;

#if(EFFECT_BUILD_AS_LIB)
    QJsonObject obj = QJsonDocument::fromJson(fetchAsset(guid)).object();
	progressDialog->setValueAndText(2, "Fetch graph");

	graph = MaterialHelper::extractNodeGraphFromMaterialDefinition(obj);
	progressDialog->setValueAndText(6, "Deserialize Graph");

	this->setNodeGraph(graph);
#else
	auto filePath = QDir().filePath(AppPaths::dataRoot() + "/Materials/MyFx/");
	QDirIterator it(filePath);
	QJsonObject obj;

	while (it.hasNext()) {

		QFile file(it.next());
		file.open(QIODevice::ReadOnly);
		auto doc = QJsonDocument::fromJson(file.readAll());
		file.close();

		auto obj1 = doc.object();
        if (obj1["guid"].toString() == guid) {
			obj = obj1;
			break;
		}
	}
	graph = NodeGraph::deserialize(obj["graph"].toObject(), new LibraryV1());
	this->setNodeGraph(graph);
	this->restoreGraphPositions(obj["graph"].toObject());
#endif

	progressDialog->setValueAndText(8, "Tidying up");

	// NO TILE, NO OPEN (fix round F1). Every line below reads the list item,
	// and `selectCorrectItemFromDrop` answers null for a guid no drawer holds
	// — which a caller can produce simply by asking before the drawers were
	// refilled. It used to dereference it on the next line: a segfault, in the
	// first thing a user clicks after making a material.
	currentProjectShader = selectCorrectItemFromDrop(guid);
	if (!currentProjectShader) {
		irisLog("loadGraph: no drawer holds '" + guid + "' — nothing to open");
		restoringGraph = false;
		progressDialog->close();
		progressDialog->deleteLater();
		return;
	}
	currentShaderInformation.GUID = currentProjectShader->data(MODEL_GUID_ROLE).toString();
	currentShaderInformation.origin = origin;
	oldName = currentShaderInformation.name = currentProjectShader->data(Qt::DisplayRole).toString(); 
	restoreGraphPositions(obj["shadergraph"].toObject());
	restoringGraph = false;
	// The Members panel follows the open bundle.
	if (membersPanel) membersPanel->setMaterial(currentShaderInformation.GUID);
	progressDialog->close();
	progressDialog->deleteLater();
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

void EffectsPage::restoreGraphPositions(const QJsonObject &data)
{
    auto scene = data["scene"].toObject();
    auto nodeList = scene["nodes"].toArray();

    for(auto nodeVal : nodeList) {
        auto nodeObj = nodeVal.toObject();
        auto nodeId = nodeObj["id"].toString();
        auto node = this->scene->getNodeById(nodeId);
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
        currentShaderInformation = shaderInfo();
        if (membersPanel) membersPanel->setMaterial(QString());
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
                currentShaderInformation = shaderInfo();
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
	presets->setToolTip(tr("Shipped materials — read-only. Right-click › Customise for your own copy."));
	presets->setStyleSheet(StyleSheet::EffectsPresetsList());

	CreateNewDialog::getAdditionalPresetList();

	// get list of presets
	for (auto tile : CreateNewDialog::getPresetList()) {
		auto item = new QListWidgetItem;
		item->setText(tile.name);
		item->setSizeHint(defaultItemSize);
		item->setTextAlignment(Qt::AlignBottom);
		item->setIcon(QIcon(MaterialHelper::assetPath(tile.iconPath)));
		item->setData(MODEL_TYPE_ROLE, "presets");
		item->icon().addPixmap(QPixmap(":/icons.shader_overlay.png"));
		presets->addToListWidget(item);
	}

	for (auto tile : CreateNewDialog::getAdditionalPresetList()) {
		auto item = new QListWidgetItem;
		item->setText(tile.name);
		item->setSizeHint(defaultItemSize);
		item->setTextAlignment(Qt::AlignBottom);
		item->setIcon(QIcon(MaterialHelper::assetPath(tile.iconPath)));
		item->setData(MODEL_TYPE_ROLE, "presets2");
		item->icon().addPixmap(QPixmap(":/icons.shader_overlay.png"));
		presets->addToListWidget(item);
	}

	// The starters too: the editor's materials drawer ships Default/Basic/
	// Texture PBR presets, so the Presets tab offers the same set (preset sync).
	for (auto tile : CreateNewDialog::getStarterList()) {
		auto item = new QListWidgetItem;
		item->setText(tile.name);
		item->setSizeHint(defaultItemSize);
		item->setTextAlignment(Qt::AlignBottom);
		item->setIcon(QIcon(MaterialHelper::assetPath(tile.iconPath)));
		item->setData(MODEL_TYPE_ROLE, "presets");
		item->icon().addPixmap(QPixmap(":/icons.shader_overlay.png"));
		presets->addToListWidget(item);
	}

	// THE SHIPPED MATERIAL PRESETS (phase 3). They are library bundles with
	// reserved guids now — the same tiles the editor's materials drawer
	// shows, from the same one list (io/materialpresets.h) — so the module's
	// Presets drawer is what its name and its tooltip always claimed: the
	// materials the app ships, read-only, with Customise as the way out.
	// LISTING DOES NOT SEED: the guid is reserved and known before any row
	// exists, so the drawer costs nothing until somebody uses a preset.
	for (const MaterialPreset &preset : MaterialPresets::all()) {
		const QString guid = MaterialPresetAssets::guidFor(preset.name);
		if (guid.isEmpty()) continue;
		auto item = new QListWidgetItem;
		item->setText(preset.name);
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

void EffectsPage::createShader(NodeGraphPreset preset, bool loadNewGraph)
{
	QString newShader;
	newShader = preset.title;

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

	stack->clear();

	if (loadNewGraph)	loadGraphFromTemplate(preset);
	else				setNodeGraph(graph);
	
	currentShaderInformation.GUID = assetGuid;
	currentShaderInformation.name = newShader;
	currentShaderInformation.origin = shaderInfo::Origin::Library;   // created in the library


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

void EffectsPage::loadGraphFromTemplate(NodeGraphPreset preset)
{
    currentShaderInformation.GUID = "";
	NodeGraph *graph;
	graph = importGraphFromFilePath(MaterialHelper::assetPath(preset.templatePath), false);

	// Texture assignment at template instantiation (§3b, post-migration):
	//
	// OLD-format templates still carry graph["properties"]; the migration
	// turned each texture property's PropertyNode into a texture node
	// (graph->migratedPropertyNodes). Import the preset's image per texture
	// property, in property order — exactly the pairing the old loop used —
	// and hand the imported guid to BOTH the readable property and its
	// migrated node.
	int i = 0;
	for (auto prop : graph->properties) {
		if (prop->type != PropertyType::Texture) continue;
		if (i >= preset.list.size()) break;
		GraphTexture* graphTexture = TextureManager::getSingleton()->importTexture(MaterialHelper::assetPath(preset.list.at(i)));
		prop->setValue(graphTexture->guid);
		for (auto it = graph->migratedPropertyNodes.constBegin(); it != graph->migratedPropertyNodes.constEnd(); ++it) {
			if (it.value() != prop->id) continue;
			if (auto texNode = dynamic_cast<TextureNode*>(graph->getNode(it.key())))
				texNode->setTextureGuid(graphTexture->guid);
		}
		i++;
	}

	// NEW-format templates (re-saved through the migration) have no
	// properties: their texture nodes carry app-relative image names
	// ("wood.jpg", "materials_to_graph/brick diff.jpg") that resolve
	// against the shadergraph asset folder and import on first use. Shared
	// with materials.loadGraph since 2026-09-04 — the scripted route used to
	// see those textures unconnected.
	MaterialHelper::resolveAppRelativeTextures(graph);

	graph->settings.name = preset.name;
	setNodeGraph(graph);

}

void EffectsPage::setCurrentShaderItem()
{
 	if (scene->currentlyEditing)
		currentProjectShader = selectCorrectItemFromDrop(scene->currentlyEditing->data(MODEL_GUID_ROLE).toString());
}

QByteArray EffectsPage::fetchAsset(QString string)
{
#if(EFFECT_BUILD_AS_LIB)
	// THE DEFINITION AT THE SCOPE THIS MATERIAL WAS OPENED AT (D-2 + the
	// four-drawer rule): a Projects tile reads the project's pinned version,
	// a Custom tile reads the library original. Passing the project
	// unconditionally made the library copy unreachable the moment any
	// project pinned it.
	const bool projectScope = currentShaderInformation.origin == shaderInfo::Origin::Project;
	const QJsonObject definition =
	    MaterialBundle::read(dataBase, string, projectScope ? mProject : nullptr);
	if (!definition.isEmpty()) return QJsonDocument(definition).toJson();
	return dataBase->fetchAssetData(string);
#else
	// fetch file locally

#endif


	return QByteArray();
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
	splitView->addWidget(graphicsView);
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
		if (!guid.isEmpty() && guid == currentShaderInformation.GUID)
			loadGraph(guid, currentShaderInformation.origin);
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

	connect(actionSave, &QAction::triggered, this, &EffectsPage::saveShader);
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

	for (NodeLibraryItem *tile : graph->library->items) {
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
		createShader(preset, loadNewGraph);
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
	const QString openGuid = currentShaderInformation.GUID;
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
	return scene != nullptr && scene->selectNodeById(nodeId);
}

QString EffectsPage::selectedGraphNodeId()
{
	return scene != nullptr ? scene->selectedNodeId() : QString();
}

void EffectsPage::deselectGraphNodes()
{
	if (scene != nullptr) scene->deselectAll();
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
	if (scene == nullptr) return false;
	if (!scene->deleteNodeById(nodeId)) return false;
	scene->update();
	return true;
}

bool EffectsPage::removeGraphConnection(const QString& connectionId)
{
	if (scene == nullptr) return false;
	if (!scene->deleteConnectionById(connectionId)) return false;
	scene->update();
	return true;
}

// ---- the graph's edit stack (graph.undo / graph.redo, and the shell's
//      Ctrl+Z while the Materials space is active) --------------------------

bool EffectsPage::graphUndo()
{
	if (!stack || !stack->canUndo()) return false;
	stack->undo();
	// The scene has to be told to repaint: the commands mutate node/connection
	// state directly and QGraphicsScene has no way to know (this is what the
	// page's own toolbar undo button did, and the graph view's deleted
	// shortcut before it).
	if (scene) scene->update();
	return true;
}

bool EffectsPage::graphRedo()
{
	if (!stack || !stack->canRedo()) return false;
	stack->redo();
	if (scene) scene->update();
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
	if (!scene) return false;
	scene->deleteSelectedNodes();
	scene->update();
	return true;
}

bool EffectsPage::graphDuplicateSelected()
{
	if (!scene) return false;
	scene->duplicateSelected();
	scene->update();
	return true;
}

bool EffectsPage::graphCopySelected()
{
	if (!scene) return false;
	scene->copySelectedToClipboard();
	return true;
}

bool EffectsPage::graphPaste()
{
	if (!scene) return false;
	scene->pasteFromClipboard();
	scene->update();
	return true;
}

int EffectsPage::graphUndoCount() const { return stack ? stack->index() : 0; }

int EffectsPage::graphRedoCount() const
{
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
	dataBase->renameAsset(currentProjectShader->data(MODEL_GUID_ROLE).toString(), currentProjectShader->data(Qt::DisplayRole).toString());
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

GraphNodeScene *EffectsPage::createNewScene()
{
    auto scene = new GraphNodeScene(this);
	scene->setUndoRedoStack(stack);
    scene->setBackgroundBrush(QBrush(QColor(60, 60, 60)));

	connect(scene, &GraphNodeScene::graphInvalidated, [this, scene]()
	{
		// Engine preview: every path that re-evaluates the graph ends here
		// (graphInvalidated covers connections, deletions and value edits
		// alike), so this one debounced hook keeps the Display dock live.
		schedulePreviewUpdate();
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
		if (!restoringGraph && positionSaveTimer) positionSaveTimer->start();
	});

	connect(scene, &GraphNodeScene::nodeMoved, this, [this]() {
		// drags fire per step; setPos during graph builds must not count
		if (!restoringGraph && positionSaveTimer) positionSaveTimer->start();
	});

	connect(scene, &GraphNodeScene::loadGraph, [=](QListWidgetItem *item) {
		currentShaderInformation.name = item->data(Qt::DisplayRole).toString();
		currentShaderInformation.GUID = item->data(MODEL_GUID_ROLE).toString();
		// A tile dropped on the canvas opens at the scope of the drawer it
		// was dragged out of (the four-drawer rule).
		loadGraph(currentShaderInformation.GUID, originForItem(currentShaderInformation.GUID));
	});

	connect(scene, &GraphNodeScene::loadGraphFromPreset, [=](QString name) {
		for (auto preset : CreateNewDialog::getPresetList() + CreateNewDialog::getStarterList()) {
			if (name == preset.name) {
				loadGraphFromTemplate(preset);
			}
		}
	});


	connect(scene, &GraphNodeScene::loadGraphFromPreset2, [=](QString name) {
		for (auto preset : CreateNewDialog::getAdditionalPresetList()) {
			if (name == preset.name) {
				loadGraphFromTemplate(preset);
			}
		}
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
	if (!enginePreview || !graph) return;

	// MATERIALS_EVALUATOR_SPEC section 2: preview bakes run off-thread over
	// the compiled BakeProgram (a pure value object - the graph's QWidgets
	// are only touched here, on the GUI thread), latest-wins by generation.
	const quint64 generation = ++previewGeneration;
	auto compiled = materials::GraphBaker::compile(graph, MaterialHelper::textureResolver());

	materials::GraphBaker::Options opts;
	opts.resolution = 256; // preview quality, fixed
	const QString guid = currentShaderInformation.GUID.isEmpty()
	                         ? QStringLiteral("preview") : currentShaderInformation.GUID;
	opts.outputDir = QDir::temp().absoluteFilePath("jahshaka-preview-bakes/" + guid);
	opts.relativePrefix = opts.outputDir + "/"; // absolute: no resolver round-trip

	auto watcher = new QFutureWatcher<materials::GraphBaker::Result>(this);
	connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation]() {
		watcher->deleteLater();
		if (generation != previewGeneration) return; // a newer bake is in flight
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
		currentShaderInformation.name = item->data(Qt::DisplayRole).toString();
		currentShaderInformation.GUID = item->data(MODEL_GUID_ROLE).toString();
		currentShaderInformation.origin = shaderInfo::Origin::Project;
		loadGraph(currentShaderInformation.GUID, shaderInfo::Origin::Project);
	});
#endif

    connect(effects, &QListWidget::itemDoubleClicked, [=](QListWidgetItem *item) {
        currentShaderInformation.name = item->data(Qt::DisplayRole).toString();
        currentShaderInformation.GUID = item->data(MODEL_GUID_ROLE).toString();
        currentShaderInformation.origin = shaderInfo::Origin::Library;
        loadGraph(currentShaderInformation.GUID, shaderInfo::Origin::Library);
    });

    connect(effects, &QListWidget::itemPressed, [=](QListWidgetItem *item){
        pressedShaderInfo.name = item->data(Qt::DisplayRole).toString();
        pressedShaderInfo.GUID = item->data(MODEL_GUID_ROLE).toString();
    });

	connect(presets, &QListWidget::itemDoubleClicked, [=](QListWidgetItem *item) {
		for (auto preset : CreateNewDialog::getPresetList() + CreateNewDialog::getStarterList()) {
			if (item->data(Qt::DisplayRole).toString() == preset.name) {
				loadGraphFromTemplate(preset);
			}
		}
	});
	connect(presets, &QListWidget::itemDoubleClicked, [=](QListWidgetItem *item) {
		for (auto preset : CreateNewDialog::getAdditionalPresetList()) {
			if (item->data(Qt::DisplayRole).toString() == preset.name) {
				loadGraphFromTemplate(preset);
			}
		}
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
	});

    // change: any settings changed
    connect(materialSettingsWidget, &MaterialSettingsWidget::settingsChanged,[=](MaterialSettings settings){
		auto command = new MaterialSettingsChangeCommand(graph, settings, materialSettingsWidget);
		stack->push(command);
		nodePropertiesPanel->refreshSettings();
    });

	// §3a: the panel's master/graph settings views push through the SAME
	// undo command the left settings dock uses — one edit stack
	connect(nodePropertiesPanel, &NodePropertiesPanel::settingsEdited, [=](MaterialSettings settings) {
		auto command = new MaterialSettingsChangeCommand(graph, settings, materialSettingsWidget);
		stack->push(command);
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
    dataBase->renameAsset(pressedShaderInfo.GUID, newName);
#else
    // get json obj from file and edit graph like above

    auto filePath = QDir().filePath(AppPaths::dataRoot() + "/Materials/MyFx/");
    if (!QDir(filePath).exists()) return;
    auto shaderFileOld = new QFile(filePath + oldName);
    auto shaderFileNew = new QFile(filePath + newName);
    QDir().rename(shaderFileOld->fileName() , shaderFileNew->fileName());
#endif

	item->setData(Qt::DisplayRole, newName);

    // update current settings if the same
    if(pressedShaderInfo.GUID == currentShaderInformation.GUID){
        currentShaderInformation.name = newName;
        this->graph->settings.name = newName;
        materialSettingsWidget->setName(newName);
        saveShader();
    }

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
