/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
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
#include "zip.h"
#include "core/exporter.h"

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
	// arrangement without an explicit save click. serializeWithBake is
	// hash-cached, so an unchanged graph re-saves cheaply.
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

	if (database) {
		dataBase = database;
		setAssetWidgetDatabase(database);
		TextureManager::getSingleton()->setDatabase(database);
	}

	newNodeGraph();
	generateTileNode();
	configureStyleSheet();
	configureProjectDock();
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
#if(EFFECT_BUILD_AS_LIB)
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

	QJsonDocument doc;
	// saving is a final-bake trigger (MATERIALS_EVALUATOR_SPEC section 2):
	// UV-varying chains land as BakedMaps/<guid>/ PNGs in the project folder
	auto matObj = MaterialHelper::serializeWithBake(graph, currentShaderInformation.GUID);
	doc.setObject(matObj);
	QString data = doc.toJson();

	// Thumbnail: the GL preview died with the legacy viewport; the engine
	// preview has no offscreen capture surface here yet, so an empty thumbnail
	// is stored (matches what engine mode effectively produced before step 14).
	QByteArray arr;

#if(EFFECT_BUILD_AS_LIB)
    dataBase->updateAssetAsset(currentShaderInformation.GUID, doc.toJson());
	dataBase->updateAssetThumbnail(currentShaderInformation.GUID, arr);
#else

	auto filePath = QDir().filePath(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/Materials/MyFx/");
	if (!QDir(filePath).exists()) QDir().mkpath(filePath);
	auto shaderFile = new QFile(filePath + obj["name"].toString());
	if (shaderFile->open(QIODevice::ReadWrite)) {
		shaderFile->write(doc.toJson());
		shaderFile->close();
	}
	else {
		qDebug() << "device not open";
	}
#endif

	int currentTab = selectCorrectTabForItem(currentShaderInformation.GUID);
	auto item = selectCorrectItemFromDrop(currentShaderInformation.GUID);
	if (item) {
		ListWidget::updateThumbnailImage(arr, item);
		tabWidget->setCurrentIndex(currentTab);
		ListWidget::highlightNodeForInterval(2, item);

		if (currentTab == (int)ShaderWorkspace::Projects) updateMaterialFromShader(currentShaderInformation.GUID);
	}
}

void EffectsPage::saveDefaultShader()
{
	bool shouldSaveGraph = createNewGraph(false);
}

void EffectsPage::loadShadersFromDisk()
{
	// create constants for this
    auto filePath = QDir().filePath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/Materials/MyFx/");
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
    QString path = QFileDialog::getOpenFileName(this, "Choose file name","material.json","Material File (*.jaf)");
	if (path == "") return;
	//assetView->importJahModel(path, false); 
	importEffect(path);
	//importGraphFromFilePath(path);
}

void EffectsPage::importEffect(QString fileName)
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

		if (!f.open(QFile::ReadOnly | QFile::Text)) return;
		QTextStream in(&f);
		const QString jafString = in.readLine();
		f.close();

		ModelTypes jafType = ModelTypes::Undefined;

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
		QString guid = dataBase->importAsset(jafType,
			QDir(temporaryDir.path()).filePath("asset.db"),
			QMap<QString, QString>(),
			guidCompareMap,
			records,
			AssetViewFilter::Effects,
			mProject->getProjectGuid());

		const QString assetFolder = QDir(assetPath).filePath(guid);
		QDir().mkpath(assetFolder);

		QString assetsDir = QDir(temporaryDir.path()).filePath("assets");
		QDirIterator projectDirIterator(assetsDir, QDir::NoDotAndDotDot | QDir::Files);

		QStringList fileNames;
		while (projectDirIterator.hasNext()) fileNames << projectDirIterator.next();

		jafType = ModelTypes::Undefined;

		QString placeHolderGuid = GUIDManager::generateGUID();

		// import assets on by one and move them to folders matching their guids
		auto assets = dataBase->fetchAssetAndAllDependencies(guid);
		for (const auto &file : fileNames) {
			QFileInfo fileInfo(file);
			for (const auto &assetGuid : assets) {
				auto record = dataBase->fetchAsset(assetGuid);

				if (fileInfo.fileName() == record.name) {
					// move asset to it's own folder
					const QDir destFolder = QDir(IrisUtils::join(assetPath, record.guid));
					if (destFolder.exists()) {
						if (!destFolder.mkdir("."))
							irisLog("Unable to create folder "+destFolder.absolutePath());
					}

					auto destPath = IrisUtils::join(assetPath, record.guid, fileInfo.fileName());
					bool fileCopied = QFile::copy(fileInfo.absoluteFilePath(), destPath);
					
					if (!fileCopied)
						irisLog("Failed to copy texture " + fileInfo.fileName());
				}
			}
			/*QFileInfo fileInfo(file);
			QString fileToCopyTo = IrisUtils::join(assetFolder, fileInfo.fileName());
			bool copyFile = QFile::copy(fileInfo.absoluteFilePath(), fileToCopyTo);*/
		}
	}

	this->updateAssetDock();
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

void EffectsPage::loadGraph(QString guid)
{
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
	auto filePath = QDir().filePath(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/Materials/MyFx/");
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

	currentProjectShader = selectCorrectItemFromDrop(guid);
	currentShaderInformation.GUID = currentProjectShader->data(MODEL_GUID_ROLE).toString();
	oldName = currentShaderInformation.name = currentProjectShader->data(Qt::DisplayRole).toString(); 
	restoreGraphPositions(obj["shadergraph"].toObject());
	restoringGraph = false;
	progressDialog->close();
	progressDialog->deleteLater();
}

void EffectsPage::exportEffect(QString guid)
{
	const QString assetName = dataBase->fetchAsset(guid).name;

	// get the export file path from a save dialog
	auto filePath = QFileDialog::getSaveFileName(
		this,
		"Choose export path",
		assetName,
		"Supported Export Formats (*.jaf)"
	);

	if (filePath.isEmpty() || filePath.isNull()) return;

	QTemporaryDir temporaryDir;
	if (!temporaryDir.isValid()) return;

	const QString writePath = temporaryDir.path();

	Exporter::exportShaderAsMaterial(dataBase, mProject, guid, filePath);
	return;

	//const QString guid = assetItem.wItem->data(MODEL_GUID_ROLE).toString();

	dataBase->createBlobFromAsset(guid, QDir(writePath).filePath("asset.db"));

	QDir tempDir(writePath);
	tempDir.mkpath("assets");

	QFile manifest(QDir(writePath).filePath(".manifest"));
	if (manifest.open(QIODevice::ReadWrite)) {
		QTextStream stream(&manifest);
		stream << "shader";
	}
	manifest.close();

	for (const auto &assetGuid : AssetHelper::fetchAssetAndAllDependencies(guid, dataBase)) {
		// Pin world (phase 4): bytes resolve through the project pin /
		// library source - the flat project folder holds no assets.
		QString name;
		const QString assetPath = AssetCas::resolvePinned(
			QSqlDatabase::database(), AssetStorePaths::root(),
			mProject ? mProject->getProjectGuid() : QString(), assetGuid, &name);
		if (assetPath.isEmpty()) continue;
		if (name.isEmpty()) name = dataBase->fetchAsset(assetGuid).name;
		if (name.isEmpty()) name = QFileInfo(assetPath).fileName();
		QFile::copy(assetPath, IrisUtils::join(writePath, "assets", name));
	}

	// ONE zip loop (amendment 7): shared helper.
	ZipHelper::zipDirectory(writePath, filePath);
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

    if(dataBase->deleteAsset(guid)){
        holder->takeItem(holder->row(item));
        currentShaderInformation = shaderInfo();
        return true;
    }
#else

    auto filePath = QDir().filePath(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/Materials/MyFx/");
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
	setStyleSheet(
		"QMainWindow::separator {width: 10px;h eight: 0px; margin: -3.5px; padding: 0px; border: 0px solid black; background: rgba(19, 19, 19, 1);}"
		"QWidget{background:rgba(32,32,32,1); color:rgba(240,240,240,1); border: 0px solid rgba(0,0,0,0);}"
		"QMenu{	background: rgba(26,26,26,.9); color: rgba(250,250, 250,.9); border-radius : 2px; }"
		"QMenu::item{padding: 4px 5px 4px 10px;	}"
		"QMenu::item:hover{	background: rgba(40,128, 185,.9);}"
		"QMenu::item:selected{	background: rgba(40,128, 185,.9);}"

		"QTabWidget::pane{border: 1px solid rgba(0,0,0,.1);	border - top: 0px solid rgba(0,0,0,0);	}"
		"QTabWidget::tab - bar{	left: 1px; background: rgba(26,26,26,.9);	}"
		"QDockWidget::tab{	background:rgba(32,32,32,1);}"

		"QScrollBar:vertical {border : 0px solid black;	background: rgba(132, 132, 132, 0);width: 24px; padding: 4px;}"
		"QScrollBar::handle{ background: rgba(72, 72, 72, 1);	border-radius: 8px; width: 14px; }"
		"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {	background: rgba(200, 200, 200, 0);}"
		"QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {	background: rgba(0, 0, 0, 0);border: 0px solid white;}"
		"QScrollBar::sub-line, QScrollBar::add-line {	background: rgba(10, 0, 0, .0);}"
	);

	nodePropertiesPanel->setStyleSheet(
		"QWidget{background:rgba(32,32,32,1);}"
	);

	nodeContainer->setStyleSheet(
		"QListView::item{ border-radius: 2px; border: 1px solid rgba(0,0,0,.31); background: rgba(51,51,51,1); margin: 3px;  }"
		"QListView::item:selected{ background: rgba(155,155,155,1); border: 1px solid rgba(50,150,250,.1); }"
		"QListView::item:hover{ background: rgba(95,95,95,1); border: .1px solid rgba(50,150,250,.1); }"
		"QListView::text{ top : -6; }"

	);

	nodeContainer->verticalScrollBar()->setStyleSheet(
		"QScrollBar:vertical {border : 0px solid black;	background: rgba(132, 132, 132, 0);width: 10px; }"
		"QScrollBar::handle{ background: rgba(72, 72, 72, 1);	border-radius: 5px;  left: 8px; }"
		"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {	background: rgba(200, 200, 200, 0);}"
		"QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {	background: rgba(0, 0, 0, 0);border: 0px solid white;}"
		"QScrollBar::sub-line, QScrollBar::add-line {	background: rgba(10, 0, 0, .0);}"
	);

	nodeTray->setStyleSheet(
		"QDockWidget{color: rgba(250,250,250,.9); background: rgba(32,32,32,1);}"
		"QDockWidget::title{ padding: 8px; background: rgba(22,22,22,1);	border: 1px solid rgba(20,20,20, .8);	text-align: center;}"
		"QDockWidget::close-button{ background: rgba(0,0,0,0); color: rgba(200,200,200,0); icon-size: 0px; padding: 23px; }"
		"QDockWidget::float-button{ background: rgba(0,0,0,0); color: rgba(200,200,200,0); icon-size: 0px; padding: 22px; }"
		//"QDockWidget::close-button, QDockWidget::float-button{	background: rgba(10,10,10,1); color: white;padding: 0px;}"
		//"QDockWidget::close-button:hover, QDockWidget::float-button:hover{background: rgba(0,220,0,0);padding: 0px;}"
		"QComboBox::drop-down {	width: 15px;  border: none; subcontrol-position: center right;}"
		"QComboBox::down-arrow{image : url(:/images/drop-down-24.png); }"
	);

	displayWidget->setStyleSheet(nodeTray->styleSheet());
	propertyWidget->setStyleSheet(nodeTray->styleSheet());
	materialSettingsWidget->setStyleSheet(nodeTray->styleSheet());
	materialSettingsDock->setStyleSheet(nodeTray->styleSheet());
	tabbedWidget->setStyleSheet(nodeTray->styleSheet() + 
	"QTabWidget::pane{	border: 1px solid rgba(0, 0, 0, .5); border - top: 0px solid rgba(0, 0, 0, 0);}"
	"QTabBar::tab{	background: rgba(21, 21, 21, .7); color: rgba(250, 250, 250, .9); font - weight: 400; font - size: 13em; padding: 5px 22px 5px 22px; }"
		"QTabBar::tab:selected{ color: rgba(255, 255, 255, .99); border-top: 2px solid rgba(50,150,250,.8); }"
		"QTabBar::tab:!selected{ background: rgba(55, 55, 55, .99); border : 1px solid rgba(21,21,21,.4); color: rgba(200,200,200,.5); }"
	);
	for (int i = 0; i < tabbedWidget->count(); i++) {
		tabbedWidget->widget(i)->setStyleSheet(nodeContainer->styleSheet());
	}
}


void EffectsPage::configureProjectDock()
{
#if(EFFECT_BUILD_AS_LIB)
	auto widget = new QWidget;
	auto layout = new QVBoxLayout;
	widget->setLayout(layout);
	layout->setContentsMargins(0, 0, 0, 0);
	//projectDock->setWidget(widget);
	//projectDock->setStyleSheet(nodeTray->styleSheet());

	auto searchContainer = new QWidget;
	auto searchLayout = new QHBoxLayout;
	auto searchBar = new QLineEdit;

	searchContainer->setLayout(searchLayout);
	searchLayout->addWidget(searchBar);
	searchLayout->addSpacing(12);

	searchBar->setPlaceholderText("search");
	searchBar->setAlignment(Qt::AlignLeft);
	searchBar->setFont(font);
	searchBar->setTextMargins(8, 0, 0, 0);
	searchBar->setStyleSheet("QLineEdit{ background:rgba(41,41,41,1); border: 1px solid rgba(150,150,150,.2); border-radius: 1px; color: rgba(250,250,250,.95); }");

	//layout->addWidget(assetWidget);
#endif
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
	auto contentHolder = new QWidget;
	auto contentLayout = new QVBoxLayout;
	/*contentHolder->setLayout(contentLayout);
	scrollView->setWidget(contentHolder);
	scrollView->setWidgetResizable(true);
	scrollView->setContentsMargins(0, 0, 0, 0);
	scrollView->setStyleSheet(
		"QScrollBar:vertical {border : 0px solid black;	background: rgba(132, 132, 132, 0);width: 10px; }"
		"QScrollBar::handle{ background: rgba(72, 72, 72, 1);	border-radius: 3px;  left: 8px; }"
		"QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {	background: rgba(200, 200, 200, 0);}"
		"QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {	background: rgba(0, 0, 0, 0);border: 0px solid white;}"
		"QScrollBar::sub-line, QScrollBar::add-line {	background: rgba(10, 0, 0, .0);}"
	);*/

	//auto presetsLabel = new QLabel("Presets");
	//auto effectsLabel = new QLabel("My Fx");

	//presetsLabel->setStyleSheet("QLabel{ background: rgba(20,20,20,1); padding: 3px; padding-left: 8px; color: rgba(200,200,200,1); }");
	//effectsLabel->setStyleSheet(presetsLabel->styleSheet());

	//contentLayout->addWidget(presetsLabel);
	//contentLayout->addWidget(presets);
	//contentLayout->addWidget(effectsLabel);
	//contentLayout->addWidget(effects);
	//contentLayout->setContentsMargins(0, 0, 0, 0);

	presets->setStyleSheet(presets->styleSheet() +
		"border: 1px solid black;"
	);

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

	presets->isResizable = true;
	effects->isResizable = true;
	
	scrollViewFx->setWidget(effects);
	scrollViewPreset->setWidget(presets);
	scrollViewAsset->setWidget(assetWidget);
	scrollViewPreset->setWidgetResizable(true);
	scrollViewFx->setWidgetResizable(true);
	scrollViewAsset->setWidgetResizable(true);


	tabWidget->addTab(scrollViewPreset, "Presets");
    tabWidget->addTab(scrollViewFx, "Custom");
    tabWidget->addTab(scrollViewAsset, "Projects");

	scrollViewFx->adjustSize();
	scrollViewPreset->adjustSize();

	scrollViewFx->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	scrollViewPreset->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	/*scrollView->adjustSize();
	
	auto buttonBar = new QWidget;
	auto buttonLayout = new QHBoxLayout;
	auto exportBtn = new QPushButton("help");
	auto importBtn = new QPushButton("help");
	auto addBtn = new QPushButton("+");
	{
		int fontSize = 12;

		buttonBar->setLayout(buttonLayout);
		buttonLayout->addWidget(exportBtn);
		buttonLayout->addWidget(importBtn);
		buttonLayout->addWidget(addBtn);
		buttonLayout->setContentsMargins(2, 2, 2, 2);
		buttonLayout->setSpacing(1);


		exportBtn->setText(QChar(fa::upload));
		exportBtn->setFont(fontIcons->font(fontSize));
		exportBtn->setToolTip("Export shader");
		importBtn->setText(QChar(fa::download));
		importBtn->setFont(fontIcons->font(fontSize));
		importBtn->setToolTip("Import shader");
		addBtn->setText(QChar(fa::plus));
		addBtn->setFont(fontIcons->font(fontSize));
		addBtn->setToolTip("Create new shader");

		exportBtn->setCursor(Qt::PointingHandCursor);
		importBtn->setCursor(Qt::PointingHandCursor);
		addBtn->setCursor(Qt::PointingHandCursor);

		exportBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		importBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		addBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		buttonBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		presetsLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		effectsLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

		exportBtn->setStyleSheet(
			"QPushButton{background: rgba(51,51,51,1); color:rgba(230,230,230,1); border: 1px solid rgba(50,50,50,.1); padding: 5px 10px; }"
			"QPushButton:hover{background: rgba(100,100,100,.3); color:rgba(230,230,230,1);}"
		);
		importBtn->setStyleSheet(exportBtn->styleSheet());
		addBtn->setStyleSheet(exportBtn->styleSheet());

		buttonBar->setStyleSheet(
			"background: rgba(21,21,21,1); padding :0px;"
		);
		buttonBar->setContentsMargins(0, 0, 0, 0);

		connect(exportBtn, &QPushButton::clicked, [=]() {
			exportGraph();
		});
		connect(importBtn, &QPushButton::clicked, [=]() {
			importGraph();
		});
		connect(addBtn, &QPushButton::clicked, [=]() {
			createNewGraph();
		});
	}*/

	//layout->addWidget(scrollView);
	//layout->addWidget(buttonBar);
	assetsDock->setWidget(tabWidget);
	assetsDock->setStyleSheet(nodeTray->styleSheet());

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
	item->setData(MODEL_TYPE_ROLE, static_cast<int>(ModelTypes::Shader));
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


#if(EFFECT_BUILD_AS_LIB)

	auto shaderDefinition = MaterialHelper::serialize(graph);
    dataBase->createAssetEntry(QString(), assetGuid,newShader,static_cast<int>(ModelTypes::Shader), QJsonDocument(shaderDefinition).toJson(), QByteArray(), AssetViewFilter::Effects);
	auto assetShader = new AssetMaterial;
	assetShader->fileName = newShader;
	assetShader->assetGuid = assetGuid;
	assetShader->path = IrisUtils::join(mProject->getProjectFolder(), IrisUtils::buildFileName(newShader, "shader"));
	// the stored value is the definition itself (materialsapi.createGraph
	// precedent) — the GLSL CustomMaterial route died in phase 5
	assetShader->setValue(QVariant::fromValue(shaderDefinition));
    dataBase->updateAssetAsset(assetGuid, QJsonDocument(shaderDefinition).toJson());
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
	// against the shadergraph asset folder and import on first use.
	for (auto node : graph->nodes.values()) {
		if (node->typeName != "texture") continue;
		auto texNode = static_cast<TextureNode*>(node);
		if (!texNode->getTexturePath().isEmpty()) continue; // already resolved
		auto rel = texNode->getTextureGuid();
		if (rel.isEmpty()) continue;
		auto abs = MaterialHelper::assetPath(rel);
		if (QFileInfo::exists(abs)) {
			GraphTexture* graphTexture = TextureManager::getSingleton()->importTexture(abs);
			texNode->setTextureGuid(graphTexture->guid);
		}
	}

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
	addDockWidget(Qt::RightDockWidgetArea, propertyWidget, Qt::Vertical);

	// The right column starts and bottoms out at the left column's width
	// (assetsDock is 330): the Display preview scales to any width, and the
	// properties panel scrolls (phase-5 owner fix - the dock used to open at
	// ~750px and refuse to shrink).
	displayWidget->setMinimumSize(330, 230);
	resizeDocks({ displayWidget, propertyWidget }, { 330, 330 }, Qt::Horizontal);
	// The dock stays hidden until Studio hands in the engine-rendered preview
	// (setEnginePreview).
	displayWidget->hide();
	displayWidget->toggleViewAction()->setEnabled(false);
	assetsDock->setMinimumWidth(330);

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
	projectName->setStyleSheet(
		"QLineEdit{background: rgba(0,0,0,0); border-radius: 3px; padding-left: 5px; color: rgba(255,255,255,.8); }"
		"QLineEdit:hover{ background : rgba(21,21,21,1); color: rgba(255,255,255,1);}"
	);

	connect(projectName, &QLineEdit::textEdited, [=](const QString text) {
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

	connect(actionUndo, &QAction::triggered, [=]() {
		scene->stack->undo();
	});
	connect(actionRedo, &QAction::triggered, [=]() {
		scene->stack->redo();
	});

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
	downloadBtn->setStyleSheet( QString(
		"QPushButton{ background-color: rgba(33,33,33, 1); color: #DEDEDE; border : 0; padding: 10px 16px; margin-right:6px; margin-left:6px; border-radius: 2px; }"
		"QPushButton:hover{ background-color: #555; }"
		"QPushButton:pressed{ background-color: #444; }"
	));
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

	toolBar->setStyleSheet(""
		//"QToolBar{background: rgba(48,48,48, 1); border: .5px solid rgba(20,20,20, .8); border-bottom: 1px solid rgba(20,20,20, .8); padding: 0px;}"
		"QToolBar{ background: rgba(48,48,48,1); border-bottom: 1px solid rgba(20,20,20, .8);}"
		"QToolBar::handle:horizontal { image: url(:/icons/thandleh.png); width: 24px; }"
		//"QToolBar::handle:vertical { image: url(:/icons/thandlev.png); height: 22px;}"
		"QToolBar::separator { background: rgba(0,0,0,.2); width: 1px; height : 20px;}"
		"QToolBar::separator:horizontal { background: #272727; width: 1px; margin-left: 6px; margin-right: 6px;} "
		"QToolButton { border-radius: 2px; background: rgba(33,33,33, 1); color: rgba(250,250,250, 1); border : 1px solid rgba(10,10,10, .4); font: 18px; padding: 8px; } "
		"QToolButton:hover{ background: rgba(48,48,48, 1); } "
		"QToolButton#actionDownload{width:40px;}"
	);

	empty->setStyleSheet(
		"background : rgba(0,0,0,0);"
	);
}

void EffectsPage::generateTileNode()
{
	QSize currentSize(90, 90);

	for (NodeLibraryItem *tile : graph->library->items) {
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
	effects->clear();
#if(EFFECT_BUILD_AS_LIB)
	//auto assets = dataBase->fetchAssets();
	auto assets = dataBase->fetchAssetsByViewFilter(AssetViewFilter::Effects);
		for (const auto &asset : assets)  //dp something{
		{
			if (asset.projectGuid == "" && asset.type == static_cast<int>(ModelTypes::Shader)) {
				 
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
#endif
}


void EffectsPage::setProject(Project *project)
{
	mProject = project;
	if (assetWidget) assetWidget->project = project;
	// the baked-map cache (BakedMaps/...) resolves against the open project
	MaterialHelper::setProjectRoot(project ? project->getProjectFolder() : QString());
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
#endif
}

void EffectsPage::renameShader()
{
#if(EFFECT_BUILD_AS_LIB)
	dataBase->renameAsset(currentProjectShader->data(MODEL_GUID_ROLE).toString(), currentProjectShader->data(Qt::DisplayRole).toString());
#else
	auto filePath = QDir().filePath(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/Materials/MyFx/");
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
	});

	connect(scene, &GraphNodeScene::nodeMoved, this, [this]() {
		// drags fire per step; setPos during graph builds must not count
		if (!restoringGraph && positionSaveTimer) positionSaveTimer->start();
	});

	connect(scene, &GraphNodeScene::loadGraph, [=](QListWidgetItem *item) {
		currentShaderInformation.name = item->data(Qt::DisplayRole).toString();
		currentShaderInformation.GUID = item->data(MODEL_GUID_ROLE).toString();
		loadGraph(currentShaderInformation.GUID);
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

void EffectsPage::updateMaterialThumbnail(QString shaderGuid, QString materialGuid)
{
	auto assetThumbnails = dataBase->fetchAssetThumbnails({ shaderGuid });
	auto assetThumbnail = assetThumbnails[0].thumbnail;
	dataBase->updateAssetThumbnail(materialGuid, assetThumbnail);
}

void EffectsPage::generateMaterialInProjectFromShader(QString guid)
{
	QJsonObject matDef; 
	writeMaterial(matDef, guid);

    QJsonObject obj = QJsonDocument::fromJson(fetchAsset(guid)).object();
	auto graphObj = MaterialHelper::extractNodeGraphFromMaterialDefinition(obj);

	QJsonDocument saveDoc;
	//saveDoc.setObject(materialDef);
	saveDoc.setObject(matDef);

	QString fileName = IrisUtils::join(
		mProject->getProjectFolder(),
		IrisUtils::buildFileName(matDef["name"].toString(), "material")
	);

	QFile file(fileName);
	file.open(QFile::WriteOnly);
	file.write(saveDoc.toJson());
	file.close();

	// WRITE TO DATABASE
	const QString assetGuid = GUIDManager::generateGUID();
    QByteArray binaryMat = QJsonDocument(matDef).toJson();
	dataBase->createAssetEntry(
		assetGuid,
		QFileInfo(fileName).fileName(),
		static_cast<int>(ModelTypes::Material),
		mProject->getProjectGuid(),
		mProject->getProjectGuid(),
		QString(),
		QString(),
		QByteArray(),
		QByteArray(),
		QByteArray(),
		binaryMat,
		AssetViewFilter::Editor
	);

	updateMaterialThumbnail(guid, assetGuid);

	MaterialReader reader;
	reader.setProject(mProject);
	auto material = reader.parseMaterial(matDef, dataBase);

	// Actually create the material and add shader as it's dependency
	dataBase->createDependency(
		static_cast<int>(ModelTypes::Material),
		static_cast<int>(ModelTypes::Shader),
		assetGuid, guid,
		mProject->getProjectGuid());

	// Add all its textures as dependencies too
	auto values = matDef["values"].toObject();
	for (const auto& prop : graphObj->properties) {
		if (prop->type == PropertyType::Texture) {
			if (!values.value(prop->name).toString().isEmpty()) {
				dataBase->createDependency(
					static_cast<int>(ModelTypes::Material),
					static_cast<int>(ModelTypes::Texture),
					assetGuid, values.value(prop->name).toString(),
					mProject->getProjectGuid()
				);
			}
		}
	}

	auto assetMat = new AssetMaterial;
	assetMat->assetGuid = assetGuid;
	assetMat->setValue(QVariant::fromValue(material));
	AssetManager::addAsset(assetMat);


	// write material guid to graph and save graph (a final-bake trigger:
	// applying a graph as a project material must land its baked maps)
	graphObj->materialGuid = assetGuid;
	graph->materialGuid = assetGuid;
	QJsonDocument doc;
	auto graphObject = MaterialHelper::serializeWithBake(graphObj, guid);
	doc.setObject(graphObject);
    dataBase->updateAssetAsset(guid, doc.toJson());
}

void EffectsPage::updateMaterialFromShader(QString guid)
{
	bool tryas = true;
    QJsonObject obj = QJsonDocument::fromJson(fetchAsset(guid)).object();
	auto graphObj = MaterialHelper::extractNodeGraphFromMaterialDefinition(obj);
    auto materialDef = QJsonDocument::fromJson(dataBase->fetchAssetData(graphObj->materialGuid)).object();

	materialDef["values"] = writeMaterialValuesFromShader(guid);
	
	MaterialReader reader;
	reader.setProject(mProject);
	auto material = reader.parseMaterial(materialDef, dataBase);

	if (!dataBase->checkIfDependencyExists(graphObj->materialGuid, guid)) {
		dataBase->createDependency(
			static_cast<int>(ModelTypes::Material),
			static_cast<int>(ModelTypes::Shader),
			graphObj->materialGuid, guid,
			mProject->getProjectGuid());
	}

	//create dependency for textures if they dont exists
	auto values = materialDef["values"].toObject();
	for (const auto& prop : graphObj->properties) {
		if (prop->type == PropertyType::Texture) {
			if (!values.value(prop->name).toString().isEmpty()) {
				if (!dataBase->checkIfDependencyExists(graphObj->materialGuid, values.value(prop->name).toString()))
				{
					dataBase->createDependency(
						static_cast<int>(ModelTypes::Material),
						static_cast<int>(ModelTypes::Texture),
						graphObj->materialGuid, values.value(prop->name).toString(),
						mProject->getProjectGuid()
					);
				}
			}
		}
	}
	updateMaterialThumbnail(guid, graphObj->materialGuid);


	auto assetMat = new AssetMaterial;
	assetMat->assetGuid = graphObj->materialGuid;
	assetMat->setValue(QVariant::fromValue(material));
	AssetManager::replaceAssets(graphObj->materialGuid, assetMat);

}

void EffectsPage::writeMaterial(QJsonObject& matObj, QString guid)
{
	auto name = dataBase->fetchAsset(guid).name;
	matObj["name"] = name;
	matObj["version"] = 2.0;
	matObj["shaderGuid"] = guid;
	matObj["values"] = writeMaterialValuesFromShader(guid);
}

QJsonObject EffectsPage::writeMaterialValuesFromShader(QString guid)
{
    QJsonObject obj = QJsonDocument::fromJson(fetchAsset(guid)).object();
	auto graphObj = MaterialHelper::extractNodeGraphFromMaterialDefinition(obj);
	QJsonObject valuesObj;
	for (auto prop : graphObj->properties) {
		if (prop->type == PropertyType::Bool) {
			valuesObj[prop->name] = prop->getValue().toBool();
		}

		if (prop->type == PropertyType::Float) {
			valuesObj[prop->name] = prop->getValue().toFloat();
		}

		if (prop->type == PropertyType::Color) {
			valuesObj[prop->name] = prop->getValue().value<QColor>().name();
		}

		if (prop->type == PropertyType::Texture) {
			auto id = prop->getValue().toString();
			valuesObj[prop->name] = id;
		}

		if (prop->type == PropertyType::Vec2) {
			valuesObj[prop->name] = SceneWriter::jsonVector2(prop->getValue().value<QVector2D>());
		}

		if (prop->type == PropertyType::Vec3) {
			valuesObj[prop->name] = SceneWriter::jsonVector3(prop->getValue().value<QVector3D>());
		}

		if (prop->type == PropertyType::Vec4) {
			valuesObj[prop->name] = SceneWriter::jsonVector4(prop->getValue().value<QVector4D>());
		}
	}

	return valuesObj;
}

void EffectsPage::configureConnections()
{
#if(EFFECT_BUILD_AS_LIB)
	connect(assetWidget, &ShaderAssetWidget::loadToGraph, [=](QListWidgetItem * item) {
		currentShaderInformation.name = item->data(Qt::DisplayRole).toString();
		currentShaderInformation.GUID = item->data(MODEL_GUID_ROLE).toString();
		loadGraph(currentShaderInformation.GUID);
	});
#endif

    connect(effects, &QListWidget::itemDoubleClicked, [=](QListWidgetItem *item) {
        currentShaderInformation.name = item->data(Qt::DisplayRole).toString();
        currentShaderInformation.GUID = item->data(MODEL_GUID_ROLE).toString();
        loadGraph(currentShaderInformation.GUID);
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

	

	QShortcut *shortcut = new QShortcut(QKeySequence("space"), this);
	QShortcut *undo = new QShortcut(QKeySequence("crtl+z"), this);
	QShortcut *redo = new QShortcut(QKeySequence("crtl+shift+z"), this);
	connect(shortcut, &QShortcut::activated, [=]() {
		auto dialog = new SearchDialog(this->graph, scene, { 0,0 });
		dialog->exec();
	});
	connect(undo, &QShortcut::activated, [=]() {
		stack->undo();
	});
	connect(redo, &QShortcut::activated, [=]() {
		stack->redo();
	});

    //connections for MyFx sections

    connect(effects, &ListWidget::renameShader, [=](QString guid){
        auto item = selectCorrectItemFromDrop(guid);
        effects->editItem(item);
    });
    connect(effects, &ListWidget::exportShader, [=](QString guid){
        exportEffect(guid);
    });
    connect(effects, &ListWidget::editShader, [=](QString guid){
        loadGraph(guid);
    });
    connect(effects, &ListWidget::deleteShader, [=](QString guid){
        deleteShader(guid);
    });
    connect(effects, &ListWidget::createShader, [=](QString guid){
        createNewGraph();
    });
	connect(effects, &ListWidget::importShader, [=](QString guid) {

	});
	connect(effects, &ListWidget::addToProject, [=](QListWidgetItem *item) {
		auto guid = assetWidget->createShader(item);
		tabWidget->setCurrentIndex((int)ShaderWorkspace::Projects);
		ListWidget::highlightNodeForInterval(2, selectCorrectItemFromDrop(guid));
		loadGraph(guid);
		generateMaterialInProjectFromShader(guid);
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
    auto oldName = pressedShaderInfo.name;
    auto newName = item->data(Qt::DisplayRole).toString();

	if (oldName == newName) return;

#if(EFFECT_BUILD_AS_LIB)
    QJsonDocument doc;
    QJsonObject obj = QJsonDocument::fromJson(fetchAsset(pressedShaderInfo.GUID)).object();
    auto graph = MaterialHelper::extractNodeGraphFromMaterialDefinition(obj);
    graph->settings.name = newName;
    auto go = graph->serialize();

    auto shadergraph = obj["shadergraph"].toObject();
    auto graphObj = shadergraph["graph"].toObject();
    auto settings = graphObj["settings"].toObject();
    settings["name"] = newName;

    graphObj["settings"] = settings;
    shadergraph["graph"] = graphObj;
    obj["shadergraph"] = shadergraph;

    doc.setObject(obj);
    dataBase->updateAssetAsset(pressedShaderInfo.GUID,doc.toJson());
    dataBase->renameAsset(pressedShaderInfo.GUID, newName);
#else
    // get json obj from file and edit graph like above

    auto filePath = QDir().filePath(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + "/Materials/MyFx/");
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
	modelMenu->setStyleSheet(
		"QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
		"QMenu:hover { background-color: #3498db; }"
		"QMenu::item { background-color: #1A1A1A; padding: 6px 16px; margin: 0; }"
		"QMenu::item:selected { background-color: #3498db; color: #EEE; }"
		"QMenu::item : disabled { color: #555; }"
	);
	backgroundMenu->setStyleSheet(modelMenu->styleSheet());

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
