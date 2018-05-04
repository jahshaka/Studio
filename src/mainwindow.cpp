/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QWindow>
#include <QSurface>
#include <QScrollArea>
#include <QTextDocument>
#include <QTemporaryFile>

#include <memory>

#include "irisgl/src/scenegraph/meshnode.h"
#include "irisgl/src/scenegraph/cameranode.h"
#include "irisgl/src/scenegraph/scene.h"
#include "irisgl/src/scenegraph/scenenode.h"
#include "irisgl/src/scenegraph/lightnode.h"
#include "irisgl/src/scenegraph/viewernode.h"
#include "irisgl/src/scenegraph/particlesystemnode.h"
#include "irisgl/src/scenegraph/meshnode.h"
#include "irisgl/src/materials/defaultmaterial.h"
#include "irisgl/src/materials/custommaterial.h"
#include "irisgl/src/graphics/forwardrenderer.h"
#include "irisgl/src/graphics/shader.h"
#include "irisgl/src/graphics/texture2d.h"
#include "irisgl/src/graphics/viewport.h"
#include "irisgl/src/graphics/texture2d.h"
#include "irisgl/src/animation/keyframeset.h"
#include "irisgl/src/animation/keyframeanimation.h"
#include "irisgl/src/animation/animation.h"
#include "irisgl/src/graphics/postprocessmanager.h"
#include "irisgl/src/core/logger.h"
#include "core/guidmanager.h"
#include "core/thumbnailmanager.h"
#include "src/dialogs/donatedialog.h"
#include "src/io/assethelper.h"

#include <QFontDatabase>
#include <QOpenGLContext>
#include <qstandarditemmodel.h>
#include <QKeyEvent>
#include <QMessageBox>
#include <QOpenGLDebugLogger>
#include <QUndoStack>

#include <QApplication>
#include <QDesktopWidget>
#include <QHash>
#include <QHashIterator>
#include <QBuffer>
#include <QDirIterator>
#include <QDockWidget>
#include <QFileDialog>
#include <QTemporaryDir>

#include <QTreeWidgetItem>

#include <QPushButton>
#include <QTimer>
#include <math.h>
#include <QDesktopServices>
#include <QShortcut>

#include "dialogs/loadmeshdialog.h"
#include "core/surfaceview.h"
#include "core/nodekeyframeanimation.h"
#include "core/nodekeyframe.h"
#include "globals.h"

#include "widgets/animationwidget.h"

#include "dialogs/renamelayerdialog.h"
#include "widgets/layertreewidget.h"
#include "core/project.h"
#include "widgets/accordianbladewidget.h"

#include "editor/editorcameracontroller.h"
#include "core/settingsmanager.h"
#include "dialogs/preferencesdialog.h"
#include "dialogs/preferences/worldsettings.h"
#include "dialogs/aboutdialog.h"

#include "helpers/collisionhelper.h"

#include "widgets/sceneviewwidget.h"
#include "core/materialpreset.h"
#include "widgets/postprocesseswidget.h"

#include "widgets/projectmanager.h"

#include "io/scenewriter.h"
#include "io/scenereader.h"

#include "constants.h"
#include <src/io/materialreader.hpp>
#include "uimanager.h"
#include "core/database/database.h"

#include "commands/addscenenodecommand.h"
#include "commands/deletescenenodecommand.h"

#include "widgets/screenshotwidget.h"
#include "editor/editordata.h"
#include "widgets/assetwidget.h"

#include "../src/dialogs/newprojectdialog.h"

#include "../src/widgets/scenehierarchywidget.h"
#include "../src/widgets/scenenodepropertieswidget.h"

#include "../src/widgets/materialsets.h"
#include "../src/widgets/modelpresets.h"
#include "../src/widgets/skypresets.h"

#include "../src/widgets/assetview.h"
#include "dialogs/toast.h"

#include "irisgl/src/zip/zip.h"

enum class VRButtonMode : int
{
    Default = 0,
    Disabled,
    VRMode
};

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

	settings = SettingsManager::getDefaultManager();

    UiManager::mainWindow = this;

    QFont font;
    font.setFamily(font.defaultFamily());
    font.setPointSize(font.pointSize() * devicePixelRatio());
    setFont(font);

#ifdef QT_DEBUG
    iris::Logger::getSingleton()->init(IrisUtils::getAbsoluteAssetPath("jahshaka.log"));
    setWindowTitle(QString("Jahshaka %1 - %2").arg(Constants::CONTENT_VERSION).arg("Developer Build"));
#else
	setWindowTitle(QString("Jahshaka %1").arg(Constants::CONTENT_VERSION));
    iris::Logger::getSingleton()->init(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/jahshaka.log");
#endif

	originalTitle = windowTitle();

	setupProjectDB();
	createPostProcessDockWidget();

    prefsDialog = new PreferencesDialog(nullptr, db, settings);
    aboutDialog = new AboutDialog();

    camControl = Q_NULLPTR;
    vrMode = false;

    setupFileMenu();

	fontIcons.initFontAwesome();

    setupViewPort();
    setupDesktop();
    setupToolBar();
    setupDockWidgets();
    setupShortcuts();
	setupUndoRedo();

	restoreGeometry(settings->getValue("geometry", "").toByteArray());
	//restoreState(settings->getValue("windowState", "").toByteArray());

	undoStackCount = 0;
}

void MainWindow::grabOpenGLContextHack()
{
    switchSpace(WindowSpaces::PLAYER);
}

void MainWindow::goToDesktop()
{
    show();
    switchSpace(WindowSpaces::DESKTOP);
}

void MainWindow::setupVrUi()
{
    /*
    vrButton->setToolTipDuration(0);

    if (sceneView->isVrSupported()) {
        vrButton->setEnabled(true);
        vrButton->setToolTip("Press to view the scene in vr");
        vrButton->setProperty("vrMode", (int) VRButtonMode::Default);
    } else {
        vrButton->setEnabled(false);
        vrButton->setToolTip("No Oculus device detected");
        vrButton->setProperty("vrMode", (int) VRButtonMode::Disabled);
    }

    connect(vrButton, SIGNAL(clicked(bool)), SLOT(vrButtonClicked(bool)));

    // needed to apply changes
    vrButton->style()->unpolish(vrButton);
    vrButton->style()->polish(vrButton);
    */
}

/**
 * uses style property trick
 * http://wiki.qt.io/Dynamic_Properties_and_Stylesheets
 */
void MainWindow::vrButtonClicked(bool)
{
    if (!sceneView->isVrSupported()) {
        // pass
    } else {
        if (sceneView->getViewportMode()==ViewportMode::Editor) {
            sceneView->setViewportMode(ViewportMode::VR);

            // highlight button blue
            vrButton->setProperty("vrMode",(int)VRButtonMode::VRMode);
        } else {
            sceneView->setViewportMode(ViewportMode::Editor);

            // return button back to normal color
            vrButton->setProperty("vrMode",(int)VRButtonMode::Default);
        }
    }

    // needed to apply changes
    vrButton->style()->unpolish(vrButton);
    vrButton->style()->polish(vrButton);
}

iris::ScenePtr MainWindow::getScene()
{
    return scene;
}

iris::ScenePtr MainWindow::createDefaultScene()
{
    auto scene = iris::Scene::create();

    scene->setSkyColor(QColor(72, 72, 72));
    scene->setAmbientColor(QColor(96, 96, 96));

    // second node
    auto node = iris::MeshNode::create();
    node->setMesh(":/models/ground.obj");
    node->setLocalPos(QVector3D(0, 1e-4, 0)); // prevent z-fighting with the default plane reset (iKlsR)
    node->setName("Ground");
    node->setPickable(false);
	node->setFaceCullingMode(iris::FaceCullingMode::None);
    node->setShadowCastingEnabled(false);
    node->isBuiltIn = true;
    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    QJsonObject props;
    props.insert("type", "builtin");
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toBinaryData(),
        QByteArray(),
        QByteArray()
    );

	// if we reached this far, the project dir has already been created
	// we can copy some default assets to each project here
	QFile::copy(IrisUtils::getAbsoluteAssetPath("app/content/textures/tile.png"),
		QDir(Globals::project->getProjectFolder()).filePath("Tile.png"));

	auto thumb = ThumbnailManager::createThumbnail(
		IrisUtils::getAbsoluteAssetPath("app/content/textures/tile.png"), 72, 72);

	QByteArray thumbnailBytes;
	QBuffer buffer(&thumbnailBytes);
	buffer.open(QIODevice::WriteOnly);
	QPixmap::fromImage(*thumb->thumb).save(&buffer, "PNG");

	const QString tileGuid = GUIDManager::generateGUID();
	const QString assetGuid = db->createAssetEntry(tileGuid,
												   "Tile.png",
												   static_cast<int>(ModelTypes::Texture),
												   Globals::project->getProjectGuid(),
                                                   QString(),
                                                   QString(), 
												   thumbnailBytes);

    db->createDependency(
        static_cast<int>(ModelTypes::Object),
        static_cast<int>(ModelTypes::Texture),
        nodeGuid, assetGuid,
        Globals::project->getProjectGuid()
    );

    auto assetTexture = new AssetTexture;
    assetTexture->fileName = "Tile.png";
    assetTexture->assetGuid = assetGuid;
    assetTexture->path = QDir(Globals::project->getProjectFolder()).filePath("Tile.png");
    AssetManager::addAsset(assetTexture);

    auto m = iris::CustomMaterial::create();
    m->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));
    m->setValue("diffuseTexture", QDir(Globals::project->getProjectFolder()).filePath("Tile.png"));
    m->setValue("textureScale", 4.f);
    node->setMaterial(m);

    scene->rootNode->addChild(node);

    auto dlight = iris::LightNode::create();
    dlight->setLightType(iris::LightType::Directional);
    scene->rootNode->addChild(dlight);
    dlight->setName("Directional Light");
    dlight->setLocalPos(QVector3D(4, 4, 0));
    dlight->setLocalRot(QQuaternion::fromEulerAngles(15, 0, 0));
    dlight->intensity = 1;
    dlight->icon = iris::Texture2D::load(":/icons/light.png");

    auto plight = iris::LightNode::create();
    plight->setLightType(iris::LightType::Point);
    scene->rootNode->addChild(plight);
    plight->setName("Point Light");
    plight->setLocalPos(QVector3D(-4, 4, 0));
    plight->intensity = 1;
    plight->icon = iris::Texture2D::load(":/icons/bulb.png");
	plight->setShadowMapType(iris::ShadowMapType::None);

    // fog params
    scene->fogColor = QColor(72, 72, 72);
    scene->shadowEnabled = true;

    sceneNodeSelected(scene->rootNode);

    return scene;
}

void MainWindow::initializeGraphics(SceneViewWidget *widget, QOpenGLFunctions_3_2_Core *gl)
{
    Q_UNUSED(gl);
    postProcessWidget->setPostProcessMgr(widget->getRenderer()->getPostProcessManager());
    setupVrUi();
}

void MainWindow::setSettingsManager(SettingsManager* settings)
{
    this->settings = settings;
}

SettingsManager* MainWindow::getSettingsManager()
{
    return settings;
}

bool MainWindow::handleMousePress(QMouseEvent *event)
{
    mouseButton = event->button();
    mousePressPos = event->pos();

    return true;
}

bool MainWindow::handleMouseRelease(QMouseEvent *event)
{
    return true;
}

bool MainWindow::handleMouseMove(QMouseEvent *event)
{
    mousePos = event->pos();
    return false;
}

// TODO - disable scrolling while doing gizmo transform ?
bool MainWindow::handleMouseWheel(QWheelEvent *event)
{
    return false;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    switch (event->type()) {
        case QEvent::MouseButtonPress: {
            dragging = true;

            if (obj == surface) return handleMousePress(static_cast<QMouseEvent*>(event));

            if (obj == sceneContainer) {
                sceneView->mousePressEvent(static_cast<QMouseEvent*>(event));
            }

            break;
        }

        case QEvent::MouseButtonRelease: {
            if (obj == surface) return handleMouseRelease(static_cast<QMouseEvent*>(event));
            break;
        }

        case QEvent::MouseMove: {
            if (obj == surface) return handleMouseMove(static_cast<QMouseEvent*>(event));
            break;
        }

        case QEvent::Wheel: {
            if (obj == surface) return handleMouseWheel(static_cast<QWheelEvent*>(event));
            break;
        }

        default:
            break;
    }

    return false;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    bool closing = false;
	bool autoSave = settings->getValue("auto_save", true).toBool();

	if (autoSave && UiManager::isSceneOpen) {
		saveScene();
		closing = true;
		event->accept();
	}
	else {
		if (UiManager::isUndoStackDirty() && (undoStackCount != UiManager::getUndoStackCount())) {
			QMessageBox::StandardButton reply;
			reply = QMessageBox::question(this,
				"Unsaved Changes",
				"There are unsaved changes, save before closing?",
				QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
			if (reply == QMessageBox::Yes) {
				saveScene();
				event->accept();
				closing = true;
			}
			else if (reply == QMessageBox::No) {
				event->accept();
				closing = true;
			}
			else {
				event->ignore();
				return;
			}
		}
		else {
			event->accept();
			closing = true;
		}
	}

//#ifndef QT_DEBUG
    if (closing) {
        if (!getSettingsManager()->getValue("ddialog_seen", "false").toBool()) {
            DonateDialog dialog;
            dialog.exec();
        }
    }
//#endif

	settings->setValue("geometry", saveGeometry());

    ThumbnailGenerator::getSingleton()->shutdown();
}

void MainWindow::setupFileMenu()
{
    connect(prefsDialog,            SIGNAL(PreferencesDialogClosed()), SLOT(updateSceneSettings()));
}

void MainWindow::createPostProcessDockWidget()
{
    postProcessDockWidget = new QDockWidget(this);
    postProcessWidget = new PostProcessesWidget();
    // postProcessWidget->setWindowTitle("Post Processes");
    postProcessDockWidget->setWidget(postProcessWidget);
    postProcessDockWidget->setWindowTitle("PostProcesses");
    // postProcessDockWidget->setFloating(true);
    postProcessDockWidget->setHidden(true);
    this->addDockWidget(Qt::RightDockWidgetArea, postProcessDockWidget);

}

void MainWindow::sceneTreeCustomContextMenu(const QPoint& pos)
{
}

void MainWindow::renameNode()
{
    RenameLayerDialog dialog(this);
    dialog.setName(activeSceneNode->getName());
    dialog.exec();

    activeSceneNode->setName(dialog.getName());
    this->sceneHierarchyWidget->repopulateTree();
}

void MainWindow::stopAnimWidget()
{
    animWidget->stopAnimation();
}

void MainWindow::setupProjectDB()
{
    const QString path = IrisUtils::join(
        QStandardPaths::writableLocation(QStandardPaths::DataLocation), Constants::JAH_DATABASE
    );

    db = new Database();
	if (db->initializeDatabase(path)) {
		db->createAllTables();
	}
}

void MainWindow::setupUndoRedo()
{
    undoStack = new QUndoStack(this);
    UiManager::setUndoStack(undoStack);
    UiManager::mainWindow = this;

    connect(ui->actionUndo, &QAction::triggered, [this]() {
        undo();
        UiManager::updateWindowTitle();
    });

    connect(ui->actionEditUndo, &QAction::triggered, [this]() {
        undo();
        UiManager::updateWindowTitle();
    });

    ui->actionEditUndo->setShortcuts(QKeySequence::Undo);

    connect(ui->actionRedo, &QAction::triggered, [this]() {
        redo();
        UiManager::updateWindowTitle();
    });

    connect(ui->actionEditRedo, &QAction::triggered, [this]() {
        redo();
        UiManager::updateWindowTitle();
    });

    ui->actionEditRedo->setShortcuts(QKeySequence::Redo);
}

WindowSpaces MainWindow::getWindowSpace()
{
	return currentSpace;
}

void MainWindow::deselectViewports()
{
	editor_menu->setStyleSheet("color: #444; border-color: #111");
	editor_menu->setDisabled(true);
	editor_menu->setCursor(Qt::ArrowCursor);
	player_menu->setStyleSheet("color: #444; border-color: #111");
	player_menu->setDisabled(true);
	player_menu->setCursor(Qt::ArrowCursor);
}

void MainWindow::switchSpace(WindowSpaces space)
{
	const QString disabledMenu   = "color: #444; border-color: #111";
	const QString selectedMenu   = "border-color: #3498db";
	const QString unselectedMenu = "border-color: #111";

	assets_menu->setStyleSheet(unselectedMenu);

    switch (currentSpace = space) {
        case WindowSpaces::DESKTOP: {
			if (UiManager::isSceneOpen) {
				//if (settings->getValue("auto_save", true).toBool()) saveScene();
				saveScene();
				pmContainer->populateDesktop(true);
			}
			
			ui->stackedWidget->setCurrentIndex(0);

            toggleWidgets(false);

            worlds_menu->setStyleSheet(selectedMenu);
            ui->actionClose->setDisabled(true);

            if (UiManager::isSceneOpen) {
                editor_menu->setStyleSheet(unselectedMenu);
                editor_menu->setDisabled(false);
                player_menu->setStyleSheet(unselectedMenu);
                player_menu->setDisabled(false);
                //ui->assets_menu->setStyleSheet(unselectedMenu);
                //ui->assets_menu->setDisabled(false);
            } else {
                editor_menu->setStyleSheet(disabledMenu);
                editor_menu->setDisabled(true);
                editor_menu->setCursor(Qt::ArrowCursor);
                player_menu->setStyleSheet(disabledMenu);
                player_menu->setDisabled(true);
                player_menu->setCursor(Qt::ArrowCursor);
                //ui->assets_menu->setStyleSheet(disabledMenu);
                //ui->assets_menu->setDisabled(true);
                //ui->assets_menu->setCursor(Qt::ArrowCursor);
            }
            break;
        }

        case WindowSpaces::EDITOR: {
            ui->stackedWidget->setCurrentIndex(1);
            
			sceneHierarchyDock->setVisible(widgetStates[(int) Widget::HIERARCHY]);
			sceneNodePropertiesDock->setVisible(widgetStates[(int)Widget::PROPERTIES]);
			presetsDock->setVisible(widgetStates[(int)Widget::PRESETS]);
			assetDock->setVisible(widgetStates[(int)Widget::ASSETS]);
			animationDock->setVisible(widgetStates[(int)Widget::TIMELINE]);
			playerControls->setVisible(false);

            toolBar->setVisible(true);
            worlds_menu->setStyleSheet(unselectedMenu);
            editor_menu->setStyleSheet(selectedMenu);
            editor_menu->setDisabled(false);
            editor_menu->setCursor(Qt::PointingHandCursor);
            player_menu->setStyleSheet(unselectedMenu);
            player_menu->setDisabled(false);
            player_menu->setCursor(Qt::PointingHandCursor);
            //ui->assets_menu->setStyleSheet(unselectedMenu);
            //ui->assets_menu->setDisabled(false);
            //ui->assets_menu->setCursor(Qt::PointingHandCursor);

			this->sceneView->setWindowSpace(space);
            playSceneBtn->show();
            this->enterEditMode();
            UiManager::sceneMode = SceneMode::EditMode;

            assetWidget->refresh();

            break;
        }

        case WindowSpaces::PLAYER: {
            ui->stackedWidget->setCurrentIndex(1);
            toggleWidgets(false);
            toolBar->setVisible(false);
            worlds_menu->setStyleSheet(unselectedMenu);
            editor_menu->setStyleSheet(unselectedMenu);
            editor_menu->setDisabled(false);
            editor_menu->setCursor(Qt::PointingHandCursor);
            player_menu->setStyleSheet(selectedMenu);
            player_menu->setDisabled(false);
            player_menu->setCursor(Qt::PointingHandCursor);
            //ui->assets_menu->setStyleSheet(unselectedMenu);
            //ui->assets_menu->setDisabled(false);
            //ui->assets_menu->setCursor(Qt::PointingHandCursor);

			this->sceneView->setWindowSpace(space);
            UiManager::sceneMode = SceneMode::PlayMode;
            playSceneBtn->hide();
            this->enterPlayMode();
            break;
        }

        case WindowSpaces::ASSETS: {
            ui->stackedWidget->setCurrentIndex(2);
            ui->stackedWidget->currentWidget()->setFocus();
			static_cast<AssetView*>(ui->stackedWidget->currentWidget())->spaceSplits();
    		toggleWidgets(false);
    		toolBar->setVisible(false);
			if (UiManager::isSceneOpen) {
				worlds_menu->setStyleSheet(unselectedMenu);
				editor_menu->setStyleSheet(unselectedMenu);
				player_menu->setStyleSheet(unselectedMenu);
				//ui->assets_menu->setDisabled(false);
				//ui->assets_menu->setCursor(Qt::PointingHandCursor);
				playSceneBtn->hide();
			}

			worlds_menu->setStyleSheet(unselectedMenu);
			assets_menu->setStyleSheet(selectedMenu);
    		
			break;
    	}

        default: break;
    }
}

void MainWindow::saveScene(const QString &filename, const QString &projectPath)
{
	SceneWriter writer;
	auto sceneObject = writer.getSceneObject(projectPath,
											 this->scene,
											 sceneView->getRenderer()->getPostProcessManager(),
											 sceneView->getEditorData());

	auto img = sceneView->takeScreenshot(Constants::TILE_SIZE * 2);
	QByteArray thumb;
	QBuffer buffer(&thumb);
	buffer.open(QIODevice::WriteOnly);
	img.save(&buffer, "PNG");

	db->updateProject(sceneObject, thumb);

	undoStackCount = UiManager::getUndoStackCount();
}

void MainWindow::saveScene()
{
	SceneWriter writer;
    auto blob = writer.getSceneObject(Globals::project->getProjectFolder(),
                                      scene,
                                      sceneView->getRenderer()->getPostProcessManager(),
                                      sceneView->getEditorData());

    auto img = sceneView->takeScreenshot(Constants::TILE_SIZE * 2);
    QByteArray thumb;
    QBuffer buffer(&thumb);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");

    db->updateProject(blob, thumb);
	pmContainer->updateTile(Globals::project->getProjectGuid(), thumb);

	undoStackCount = UiManager::getUndoStackCount();
}

void MainWindow::openProject(bool playMode)
{
    removeScene();
    sceneView->makeCurrent();

    std::unique_ptr<SceneReader> reader(new SceneReader);
	reader->setDatabaseHandle(db);

    EditorData* editorData = Q_NULLPTR;
    UiManager::updateWindowTitle();

    auto postMan = sceneView->getRenderer()->getPostProcessManager();
    postMan->clearPostProcesses();

    auto scene = reader->readScene(Globals::project->getProjectFolder(),
                                   db->getSceneBlobGlobal(),
                                   postMan,
                                   &editorData);

    UiManager::playMode = playMode;
    UiManager::isSceneOpen = true;
    ui->actionClose->setDisabled(false);
    setScene(scene);

    // use new post process that has fxaa by default
    // TODO: remember to find a better replacement (Nick)
    postProcessWidget->setPostProcessMgr(postMan);
    this->sceneView->doneCurrent();

    if (editorData != Q_NULLPTR) {
        sceneView->setEditorData(editorData);
        wireCheckBtn->setChecked(editorData->showLightWires);
    }

    assetWidget->trigger();

    // autoplay scenes immediately
    if (playMode) {
        playBtn->setToolTip("Pause the scene");
        playBtn->setIcon(QIcon(":/icons/g_pause.svg"));
        onPlaySceneButton();
    }

    UiManager::playMode ? switchSpace(WindowSpaces::PLAYER) : switchSpace(WindowSpaces::EDITOR);

	undoStackCount = 0;
}

void MainWindow::closeProject()
{
	if (UiManager::isSceneOpen) {
		//if (settings->getValue("auto_save", true).toBool()) saveScene();
        saveScene();
	}

    UiManager::isSceneOpen = false;
    UiManager::isScenePlaying = false;
    ui->actionClose->setDisabled(false);

    UiManager::clearUndoStack();
    AssetManager::clearAssetList();

    UiManager::mainWindow->setWindowTitle(originalTitle);

    scene->cleanup();
    scene.clear();

	undoStackCount = 0;

	if (currentSpace == WindowSpaces::DESKTOP) {
		deselectViewports();
		return;
	}

    switchSpace(WindowSpaces::DESKTOP);
}

/// TODO - this needs to be fixed after the objects are added back to the uniforms array/obj
void MainWindow::applyMaterialPreset(MaterialPreset *preset)
{
    if (!activeSceneNode || activeSceneNode->sceneNodeType != iris::SceneNodeType::Mesh) return;

    auto meshNode = activeSceneNode.staticCast<iris::MeshNode>();

    // TODO - set the TYPE for a preset in the .material file so we can have other preset types
    // only works for the default material at the moment...
    auto m = iris::CustomMaterial::create();
    m->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));

    m->setValue("diffuseTexture", preset->diffuseTexture);
    m->setValue("specularTexture", preset->specularTexture);
    m->setValue("normalTexture", preset->normalTexture);
    m->setValue("reflectionTexture", preset->reflectionTexture);

    m->setValue("ambientColor", preset->ambientColor);
    m->setValue("diffuseColor", preset->diffuseColor);
    m->setValue("specularColor", preset->specularColor);

    m->setValue("shininess", preset->shininess);
    m->setValue("normalIntensity", preset->normalIntensity);
    m->setValue("reflectionInfluence", preset->reflectionInfluence);
    m->setValue("textureScale", preset->textureScale);

    meshNode->setMaterial(m);

    QJsonObject material;
    SceneWriter::writeSceneNodeMaterial(material, m);

    // Remove previous material dependencies
    //auto objectGuid = db->fetchMeshObject(
    //    meshNode->getGUID(),
    //    static_cast<int>(ModelTypes::Object),
    //    static_cast<int>(ModelTypes::Mesh)
    //);

    //db->removeDependenciesByType(objectGuid, ModelTypes::Texture);

    QFile jsonFile(QDir(Globals::project->getProjectFolder()).filePath("matgen.material"));
    jsonFile.open(QFile::WriteOnly);
    jsonFile.write(QJsonDocument(material).toJson());

    auto fguid = GUIDManager::generateGUID();
    if (!db->checkIfRecordExists("name", "Presets", "folders")) {
        if (!db->createFolder("Presets", Globals::project->getProjectGuid(), fguid, false)) return;
    }

    QString guid = db->createAssetEntry(
        GUIDManager::generateGUID(),
        preset->name,
        static_cast<int>(ModelTypes::Material),
        fguid,
        QString(),
        QString(),
        QByteArray(),
        QByteArray(),
        QJsonDocument(material).toBinaryData()
    );

    ThumbnailGenerator::getSingleton()->requestThumbnail(
        ThumbnailRequestType::Material, QDir(Globals::project->getProjectFolder()).filePath("matgen.material"), guid
    );

    assetWidget->updateAssetView(assetWidget->assetItem.selectedGuid);

    for (const auto &prop : m->properties) {
        if (prop->type == iris::PropertyType::Texture) {
            auto file = prop->getValue().toString();
            if (file.isEmpty()) continue;
            QFile::copy(
                file,
                QDir(Globals::project->getProjectFolder()).filePath(QFileInfo(file).fileName())
            );

            QString fileGuid = db->createAssetEntry(
                GUIDManager::generateGUID(),
                QFileInfo(file).fileName(),
                static_cast<int>(ModelTypes::Texture),
                fguid,
                QString(),
                QString(),
                QByteArray(),
                QByteArray(),
                QByteArray()
            );

            db->createDependency(
                static_cast<int>(ModelTypes::Material),
                static_cast<int>(ModelTypes::Texture),
                guid,
                fileGuid,
                Globals::project->getProjectGuid()
            );
        }
    }

    db->createDependency(
        static_cast<int>(ModelTypes::Object),
        static_cast<int>(ModelTypes::Material),
        meshNode->getGUID(),
        guid,
        Globals::project->getProjectGuid()
    );

    // TODO: update node's material without updating the whole ui
    this->sceneNodePropertiesWidget->refreshMaterial(preset->type);
}

void MainWindow::setScene(QSharedPointer<iris::Scene> scene)
{
    this->scene = scene;
    this->sceneView->setScene(scene);
    this->sceneHierarchyWidget->setScene(scene);

    // interim...
    updateSceneSettings();
}

void MainWindow::removeScene()
{
    sceneView->cleanup();
    sceneNodePropertiesWidget->setSceneNode(iris::SceneNodePtr());
}

void MainWindow::setupPropertyUi()
{
    animWidget = new AnimationWidget();
}

void MainWindow::assetItemSelected(QListWidgetItem *item)
{
	emit sceneNodeSelected(iris::SceneNodePtr());
	this->sceneNodePropertiesWidget->setAssetItem(item);
}

void MainWindow::sceneNodeSelected(QTreeWidgetItem* item)
{

}

void MainWindow::sceneTreeItemChanged(QTreeWidgetItem* item,int column)
{

}

void MainWindow::sceneNodeSelected(iris::SceneNodePtr sceneNode)
{
    activeSceneNode = sceneNode;

    sceneView->setSelectedNode(sceneNode);
    this->sceneNodePropertiesWidget->setSceneNode(sceneNode);
    this->sceneHierarchyWidget->setSelectedNode(sceneNode);
    animationWidget->setSceneNode(sceneNode);
}

void MainWindow::updateAnim()
{
}

void MainWindow::setSceneAnimTime(float time)
{
}

void MainWindow::addPlane()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/plane.obj");
    node->setFaceCullingMode(iris::FaceCullingMode::None);
    node->setName("Plane");
    node->isBuiltIn = true;
    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toBinaryData(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addGround()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/models/ground.obj");
    node->setFaceCullingMode(iris::FaceCullingMode::None);
    node->setName("Ground");
    node->isBuiltIn = true;
    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toBinaryData(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addCone()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/cone.obj");
    node->setName("Cone");
    node->isBuiltIn = true;
    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toBinaryData(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addCapsule()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/capsule.obj");
    node->setName("Capsule");
    node->isBuiltIn = true;
    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toBinaryData(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addCube()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/cube.obj");
    node->setName("Cube");
    node->isBuiltIn = true;
    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toBinaryData(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addTorus()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/torus.obj");
    node->setName("Torus");
    node->isBuiltIn = true;
    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toBinaryData(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addSphere()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/sphere.obj");
    node->setName("Sphere");
    node->isBuiltIn = true;
    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toBinaryData(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addCylinder()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/cylinder.obj");
    node->setName("Cylinder");
    node->isBuiltIn = true;
    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toBinaryData(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addPyramid()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/pyramid.obj");
    node->setName("Pyramid");
    node->isBuiltIn = true;
    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toBinaryData(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addSponge()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/sponge.obj");
    node->setName("Sponge");
    node->isBuiltIn = true;
    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toBinaryData(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addTeapot()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/teapot.obj");
    node->setName("Teapot");
    node->isBuiltIn = true;
    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toBinaryData(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addSteps()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/steps.obj");
    node->setName("Steps");
    node->isBuiltIn = true;
    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toBinaryData(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addGear()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/gear.obj");
    node->setName("Gear");
    node->isBuiltIn = true;
    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toBinaryData(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addPointLight()
{
    this->sceneView->makeCurrent();
    auto node = iris::LightNode::create();
    node->setLightType(iris::LightType::Point);
    node->icon = iris::Texture2D::load(":/icons/bulb.png");
    node->setName("Point Light");
    node->intensity = 1.0f;
    node->distance = 40.0f;
    addNodeToScene(node);
}

void MainWindow::addSpotLight()
{
    this->sceneView->makeCurrent();
    auto node = iris::LightNode::create();
    node->setLightType(iris::LightType::Spot);
    node->icon = iris::Texture2D::load(":/icons/bulb.png");
    node->setName("Spot Light");
    addNodeToScene(node);
}


void MainWindow::addDirectionalLight()
{
    this->sceneView->makeCurrent();
    auto node = iris::LightNode::create();
    node->shadowMap->shadowType = iris::ShadowMapType::Soft;
    node->setLightType(iris::LightType::Directional);
    node->icon = iris::Texture2D::load(":/icons/bulb.png");
    node->setName("Directional Light");
    addNodeToScene(node);
}

void MainWindow::addEmpty()
{
    this->sceneView->makeCurrent();
    auto node = iris::SceneNode::create();
    node->setName("Empty");
    addNodeToScene(node);
}

void MainWindow::addViewer()
{
    this->sceneView->makeCurrent();
    auto node = iris::ViewerNode::create();
    node->setName("Viewer");
    addNodeToScene(node);
}

void MainWindow::addParticleSystem()
{
    this->sceneView->makeCurrent();
    auto node = iris::ParticleSystemNode::create();
    node->setName("Particle System");
    addNodeToScene(node);
}

void MainWindow::addMesh(const QString &path, bool ignore, QVector3D position)
{
    QString filename;
    if (path.isEmpty()) {
        filename = QFileDialog::getOpenFileName(this, "Load Mesh", "Mesh Files (*.obj *.fbx *.3ds *.dae *.c4d *.blend)");
    } else {
        filename = path;
    }

    if (filename.isEmpty()) return;

    iris::SceneSource *ssource = new iris::SceneSource();

    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::loadAsSceneFragment(filename, [](iris::MeshPtr mesh, iris::MeshMaterialData& data)
    {
        auto mat = iris::CustomMaterial::create();
        //MaterialReader *materialReader = new MaterialReader();
        if (mesh->hasSkeleton()) mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/DefaultAnimated.shader"));
        else mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));

        mat->setValue("diffuseColor", data.diffuseColor);
        mat->setValue("specularColor", data.specularColor);
        mat->setValue("ambientColor", data.ambientColor);
        mat->setValue("emissionColor", data.emissionColor);

        mat->setValue("shininess", data.shininess);

        if (QFile(data.diffuseTexture).exists() && QFileInfo(data.diffuseTexture).isFile())
            mat->setValue("diffuseTexture", data.diffuseTexture);

        if (QFile(data.specularTexture).exists() && QFileInfo(data.specularTexture).isFile())
            mat->setValue("specularTexture", data.specularTexture);

        if (QFile(data.normalTexture).exists() && QFileInfo(data.normalTexture).isFile())
            mat->setValue("normalTexture", data.normalTexture);

        return mat;
    }, ssource);

    // model file may be invalid so null gets returned
    if (!node) return;

    // rename animation sources to relative paths
    auto relPath = QDir(Globals::project->folderPath).relativeFilePath(filename);
    for (auto anim : node->getAnimations()) {
        if (!!anim->skeletalAnimation)
            anim->skeletalAnimation->source = relPath;
    }

    node->setLocalPos(position);

    // todo: load material data
    addNodeToScene(node, ignore);
}

void MainWindow::addMaterialMesh(const QString &path, bool ignore, QVector3D position, const QString &guid, const QString &assetName)
{
	this->sceneView->makeCurrent();

	iris::SceneNodePtr node;

	QString meshGuid = db->fetchObjectMesh(guid, static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh));

	QVector<Asset*>::const_iterator iterator = AssetManager::getAssets().constBegin();
	while (iterator != AssetManager::getAssets().constEnd()) {
		if ((*iterator)->assetGuid == guid) node = (*iterator)->getValue().value<iris::SceneNodePtr>()->duplicate();
		++iterator;
	}

    if (!node) return;

	std::function<void(iris::SceneNodePtr&)> updateNodeValues = [&](iris::SceneNodePtr &node) -> void {
		if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
			auto n = node.staticCast<iris::MeshNode>();
			n->meshPath = meshGuid;
			n->setGUID(guid);
			auto mat = n->getMaterial().staticCast<iris::CustomMaterial>();
			for (auto prop : mat->properties) {
				if (prop->type == iris::PropertyType::Texture) {
					if (!prop->getValue().toString().isEmpty()) {
						mat->setValue(prop->name,
							IrisUtils::join(
                                Globals::project->getProjectFolder(),
								db->fetchAsset(prop->getValue().toString()).name
                            )
                        );
					}
				}
			}
		}

		if (node->hasChildren()) {
			for (auto &child : node->children) {
				updateNodeValues(child);
			}
		}
	};

	updateNodeValues(node);

	// rename animation sources to relative paths
	auto relPath = QDir(Globals::project->folderPath).relativeFilePath(db->fetchAsset(meshGuid).name);
	for (auto anim : node->getAnimations()) if (!!anim->skeletalAnimation) anim->skeletalAnimation->source = relPath;

	node->setGUID(guid);
    node->setName(assetName);
	node->setLocalPos(position);

	addNodeToScene(node, ignore);
}

void MainWindow::addDragPlaceholder()
{
    /*
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->scale = QVector3D(.5f, .5f, .5f);
    node->setMesh(":app/content/primitives/arrow.obj");
    node->setName("Arrow");
    addNodeToScene(node, true);
    */
}

/**
 * Adds sceneNode to selected scene node. If there is no selected scene node,
 * sceneNode is added to the root node
 * @param sceneNode
 */
void MainWindow::addNodeToActiveNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!scene) {
        //todo: set alert that a scene needs to be set before this can be done
    }

    // apply default material
    if (sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
        auto meshNode = sceneNode.staticCast<iris::MeshNode>();

        if (!meshNode->getMaterial()) {
            auto mat = iris::DefaultMaterial::create();
            meshNode->setMaterial(mat);
        }
    }

    if (!!activeSceneNode) {
        activeSceneNode->addChild(sceneNode);
    } else {
        scene->getRootNode()->addChild(sceneNode);
    }

    this->sceneHierarchyWidget->repopulateTree();
}

/**
 * adds sceneNode directly to the scene's rootNode
 * applied default material to mesh if one isnt present
 * ignore set to false means we only add it visually, usually to discard it afterw
 */
void MainWindow::addNodeToScene(QSharedPointer<iris::SceneNode> sceneNode, bool ignore)
{
    if (!scene) {
        // @TODO: set alert that a scene needs to be set before this can be done
        return;
    }

    // @TODO: add this to a constants file
    if (!ignore) {
        const float spawnDist = 10.0f;
        auto offset = sceneView->editorCam->getLocalRot().rotatedVector(QVector3D(0, -1.0f, -spawnDist));
        offset += sceneView->editorCam->getLocalPos();
        sceneNode->setLocalPos(offset);
    }

    // apply default material to mesh nodes if there is none
    if (sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
        auto meshNode = sceneNode.staticCast<iris::MeshNode>();
        if (!meshNode->getMaterial()) {
            auto mat = iris::CustomMaterial::create();
            mat->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));
            meshNode->setMaterial(mat);
        }
    }

    auto cmd = new AddSceneNodeCommand(scene->getRootNode(), sceneNode);
    UiManager::pushUndoStack(cmd);
}

void MainWindow::repopulateSceneTree()
{
    this->sceneHierarchyWidget->repopulateTree();
}

void MainWindow::duplicateNode()
{
    if (!scene) return;
    if (!activeSceneNode || !activeSceneNode->isDuplicable()) return;

	sceneView->makeCurrent();
    auto node = activeSceneNode->duplicate();
    activeSceneNode->parent->addChild(node, false);

    this->sceneHierarchyWidget->repopulateTree();
    sceneNodeSelected(node);
	sceneView->doneCurrent();
}

void MainWindow::createMaterial()
{
	if (!!activeSceneNode) {
        QJsonObject materialDef;
		SceneWriter::writeSceneNodeMaterial(
			materialDef,
			activeSceneNode.staticCast<iris::MeshNode>()->getMaterial().staticCast<iris::CustomMaterial>()
		);

		QByteArray binaryMat = QJsonDocument(materialDef).toBinaryData();

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
            Globals::project->getProjectFolder(),
            IrisUtils::buildFileName(activeSceneNode.staticCast<iris::MeshNode>()->getName(), "material")
        );

        QFile file(fileName);
        file.open(QFile::WriteOnly);
        file.write(saveDoc.toJson());
        file.close();

		const QString assetGuid = GUIDManager::generateGUID();

		db->createAssetEntry(
            assetGuid,
			QFileInfo(fileName).fileName(),
			static_cast<int>(ModelTypes::Material),
			assetWidget->assetItem.selectedGuid,
            QString(),
            QString(),
			QByteArray(),
			QByteArray(),
			QByteArray(),
			binaryMat
		);

		ThumbnailGenerator::getSingleton()->requestThumbnail(
			ThumbnailRequestType::Material, fileName, assetGuid
		);

		assetWidget->updateAssetView(assetWidget->assetItem.selectedGuid);

		QJsonObject matObject = QJsonDocument::fromBinaryData(binaryMat).object();

		auto material = iris::CustomMaterial::create();
		const QJsonObject materialDefinition = matObject;
		auto shaderGuid = materialDefinition["guid"].toString();
		material->setName(materialDefinition["name"].toString());
		material->setGuid(shaderGuid);

		QFileInfo shaderFile;

		QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
		while (it.hasNext()) {
			it.next();
			if (it.key() == shaderGuid) {
				shaderFile = QFileInfo(IrisUtils::getAbsoluteAssetPath(it.value()));
				break;
			}
		}

		if (shaderFile.exists()) {
			material->generate(shaderFile.absoluteFilePath());
		}
		else {
			// Reading is thread safe...
			for (auto asset : AssetManager::getAssets()) {
				if (asset->type == ModelTypes::Shader) {
					if (asset->assetGuid == material->getGuid()) {
						auto def = asset->getValue().toJsonObject();
						auto vertexShader = def["vertex_shader"].toString();
						auto fragmentShader = def["fragment_shader"].toString();
						for (auto asset : AssetManager::getAssets()) {
							if (asset->type == ModelTypes::File) {
								if (vertexShader == asset->assetGuid) vertexShader = asset->path;
								if (fragmentShader == asset->assetGuid) fragmentShader = asset->path;
							}
						}
						def["vertex_shader"] = vertexShader;
						def["fragment_shader"] = fragmentShader;
						material->setMaterialDefinition(def);
						material->generate(def);

						db->createDependency(
							static_cast<int>(ModelTypes::Material),
							static_cast<int>(ModelTypes::Shader),
							assetGuid, material->getGuid(),
							Globals::project->getProjectGuid()
						);
					}
				}
			}
		}

		for (const auto &prop : material->properties) {
			if (prop->type == iris::PropertyType::Color) {
				QColor col;
				col.setNamedColor(matObject.value(prop->name).toString());
				material->setValue(prop->name, col);
			}
			else if (prop->type == iris::PropertyType::Texture) {
				if (!matObject.value(prop->name).toString().isEmpty()) {
					db->createDependency(
						static_cast<int>(ModelTypes::Material),
						static_cast<int>(ModelTypes::Texture),
						assetGuid, matObject.value(prop->name).toString(),
						Globals::project->getProjectGuid()
					);
				}

				QString materialName = db->fetchAsset(matObject.value(prop->name).toString()).name;
				QString textureStr = IrisUtils::join(Globals::project->getProjectFolder(), materialName);
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

		QFile::remove(fileName);
    }
    else {
        qDebug() << "Need an active scenenode!";
        return;
    }
}

void MainWindow::exportNode(const iris::SceneNodePtr &node)
{
	// get the export file path from a save dialog
	auto filePath = QFileDialog::getSaveFileName(
		this,
		"Choose export path",
		QString("%1_export").arg(node->getName()),
		"Supported Export Formats (*.jaf)"
	);

	if (filePath.isEmpty() || filePath.isNull()) return;

	QTemporaryDir temporaryDir;
	if (!temporaryDir.isValid()) return;

    const QString writePath = temporaryDir.path();
    const QString guid = node->getGUID();

    db->createExportNode(ModelTypes::Object, guid, QDir(writePath).filePath("asset.db"));

    QDir tempDir(writePath);
    tempDir.mkpath("assets");

    QFile manifest(QDir(writePath).filePath(".manifest"));
    if (manifest.open(QIODevice::ReadWrite)) {
        QTextStream stream(&manifest);
        stream << "object";
    }
    manifest.close();

	for (const auto &assetGuid : AssetHelper::fetchAssetAndAllDependencies(guid, db)) {
        auto asset = db->fetchAsset(assetGuid);
        auto assetPath = QDir(Globals::project->getProjectFolder()).filePath(asset.name);
        QFileInfo assetInfo(assetPath);
        if (assetInfo.exists()) {
            QFile::copy(
                IrisUtils::join(assetPath),
                IrisUtils::join(writePath, "assets", assetInfo.fileName())
            );
        }
	}

    // get all the files and directories in the project working directory
    QDir workingProjectDirectory(writePath);
    QDirIterator projectDirIterator(writePath,
                                    QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs,
                                    QDirIterator::Subdirectories);

    QVector<QString> fileNames;
    while (projectDirIterator.hasNext()) fileNames.push_back(projectDirIterator.next());

    // open a basic zip file for writing, maybe change compression level later (iKlsR)
    struct zip_t *zip = zip_open(filePath.toStdString().c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');

    for (int i = 0; i < fileNames.count(); i++) {
		QFileInfo fInfo(fileNames[i]);

		// we need to pay special attention to directories since we want to write empty ones as well
		if (fInfo.isDir()) {
			zip_entry_open(
				zip,
				/* will only create directory if / is appended */
				QString(workingProjectDirectory.relativeFilePath(fileNames[i]) + "/").toStdString().c_str()
			);
			zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
		}
		else {
			zip_entry_open(
				zip,
				workingProjectDirectory.relativeFilePath(fileNames[i]).toStdString().c_str()
			);
			zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
		}

		// we close each entry after a successful write
		zip_entry_close(zip);
	}

    // close our now exported file
    zip_close(zip);
}

void MainWindow::exportNodes(const QStringList &assetGuids)
{
	// get the export file path from a save dialog
	auto filePath = QFileDialog::getSaveFileName(
		this,
		"Choose export path",
		"export",
		"Supported Export Formats (*.jaf)"
	);

	if (filePath.isEmpty() || filePath.isNull()) return;

	QTemporaryDir temporaryDir;
	if (!temporaryDir.isValid()) return;

	const QString writePath = temporaryDir.path();

	db->createExportNodes(ModelTypes::Object, assetGuids, QDir(writePath).filePath("asset.db"));

	QDir tempDir(writePath);
	tempDir.mkpath("assets");

	QFile manifest(QDir(writePath).filePath(".manifest"));
	if (manifest.open(QIODevice::ReadWrite)) {
		QTextStream stream(&manifest);
		stream << "object";
	}
	manifest.close();

	for (const auto &guid : assetGuids) {
		for (const auto &asset : db->fetchAssetAndDependencies(guid)) {
			QFile::copy(
				IrisUtils::join(Globals::project->getProjectFolder(), asset),
				IrisUtils::join(writePath, "assets", QFileInfo(asset).fileName())
			);
		}
	}

	// get all the files and directories in the project working directory
	QDir workingProjectDirectory(writePath);
	QDirIterator projectDirIterator(writePath,
		QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs,
		QDirIterator::Subdirectories);

	QVector<QString> fileNames;
	while (projectDirIterator.hasNext()) fileNames.push_back(projectDirIterator.next());

	// open a basic zip file for writing, maybe change compression level later (iKlsR)
	struct zip_t *zip = zip_open(filePath.toStdString().c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');

	for (int i = 0; i < fileNames.count(); i++) {
		QFileInfo fInfo(fileNames[i]);

		// we need to pay special attention to directories since we want to write empty ones as well
		if (fInfo.isDir()) {
			zip_entry_open(
				zip,
				/* will only create directory if / is appended */
				QString(workingProjectDirectory.relativeFilePath(fileNames[i]) + "/").toStdString().c_str()
			);
			zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
		}
		else {
			zip_entry_open(
				zip,
				workingProjectDirectory.relativeFilePath(fileNames[i]).toStdString().c_str()
			);
			zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
		}

		// we close each entry after a successful write
		zip_entry_close(zip);
	}

	// close our now exported file
	zip_close(zip);
}

void MainWindow::deleteNode()
{
    if (!!activeSceneNode) {
        // TODO - do a deps check here as well
        if (activeSceneNode->isBuiltIn) db->deleteAsset(activeSceneNode->getGUID());
        auto cmd = new DeleteSceneNodeCommand(activeSceneNode->parent, activeSceneNode);
        UiManager::pushUndoStack(cmd);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    event->acceptProposedAction();
}

/**
 * @brief accepts model files dropped into scene
 * currently only .obj files are supported
 */
void MainWindow::dropEvent(QDropEvent* event)
{

}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    event->accept();
}

/*
bool MainWindow::isModelExtension(QString extension)
{
    if(extension == "obj"   ||
       extension == "3ds"   ||
       extension == "fbx"   ||
       extension == "dae"   ||
       extension == "blend" ||
       extension == "c4d"   )
        return true;
    return false;
}
*/
void MainWindow::exportSceneAsZip()
{
    // get the export file path from a save dialog
    auto filePath = QFileDialog::getSaveFileName(
                        this,
                        "Choose export path",
                        QString("%1_export").arg(Globals::project->getProjectName()),
                        "Supported Export Formats (*.zip)"
                    );

    if (filePath.isEmpty() || filePath.isNull()) return;
    if (!!scene) saveScene();

    // Maybe in the future one could add a way to using an in memory database
    // and saving that as a blob which can be put into the zip as bytes (iKlsR)
    // prepare our export database with the current scene, use the os temp location and remove after
    db->createExportScene(QStandardPaths::writableLocation(QStandardPaths::TempLocation));

    // get the current project working directory
    auto pFldr = IrisUtils::join(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                 Constants::PROJECT_FOLDER);
    auto defaultProjectDirectory = settings->getValue("default_directory", pFldr).toString();
    auto pDir = IrisUtils::join(defaultProjectDirectory, "Projects", Globals::project->getProjectGuid());

    // get all the files and directories in the project working directory
    QDir workingProjectDirectory(pDir);
    QDirIterator projectDirIterator(pDir,
                                    QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs,
                                    QDirIterator::Subdirectories);

    QVector<QString> fileNames;
    while (projectDirIterator.hasNext()) fileNames.push_back(projectDirIterator.next());

    // open a basic zip file for writing, maybe change compression level later (iKlsR)
    struct zip_t *zip = zip_open(filePath.toStdString().c_str(), ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');

    for (int i = 0; i < fileNames.count(); i++) {
        QFileInfo fInfo(fileNames[i]);

        // we need to pay special attention to directories since we want to write empty ones as well
        if (fInfo.isDir()) {
            zip_entry_open(
                zip,
                /* will only create directory if / is appended */
                QString(workingProjectDirectory.relativeFilePath(fileNames[i]) + "/").toStdString().c_str()
            );
            zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
        }
        else {
            zip_entry_open(
                zip,
                workingProjectDirectory.relativeFilePath(fileNames[i]).toStdString().c_str()
            );
            zip_entry_fwrite(zip, fileNames[i].toStdString().c_str());
        }

        // we close each entry after a successful write
        zip_entry_close(zip);
    }

    // finally add our exported scene
    zip_entry_open(zip, QString(Globals::project->getProjectGuid() + ".db").toStdString().c_str());
    zip_entry_fwrite(
        zip,
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(Globals::project->getProjectGuid() + ".db").toStdString().c_str()
    );
    zip_entry_close(zip);

    // empty manifest
    QTemporaryFile tempManifestFile;
    tempManifestFile.open();
    zip_entry_open(zip, ".manifest");
    zip_entry_fwrite(
        zip,
        QFileInfo(tempManifestFile.fileName()).absoluteFilePath().toStdString().c_str()
    );
    zip_entry_close(zip);

    // close our now exported file
    zip_close(zip);

    // remove the temporary db created
    QDir tempFile;
    tempFile.remove(
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(Globals::project->getProjectGuid() + ".db")
                );
}

void MainWindow::setupDockWidgets()
{
    // Hierarchy Dock
    sceneHierarchyDock = new QDockWidget("Hierarchy", viewPort);
    sceneHierarchyDock->setObjectName(QStringLiteral("sceneHierarchyDock"));
    sceneHierarchyWidget = new SceneHierarchyWidget;
    sceneHierarchyDock->setObjectName(QStringLiteral("sceneHierarchyWidget"));
    sceneHierarchyDock->setWidget(sceneHierarchyWidget);
    sceneHierarchyWidget->setMainWindow(this);

    UiManager::sceneHierarchyWidget = sceneHierarchyWidget;

    connect(sceneHierarchyWidget,   SIGNAL(sceneNodeSelected(iris::SceneNodePtr)),
            this,                   SLOT(sceneNodeSelected(iris::SceneNodePtr)));

    // Scene Node Properties Dock
    // Since this widget can be longer than there is screen space, we need to add a QScrollArea
    // For this to also work, we need a "holder widget" that will have a layout and the scroll area
    sceneNodePropertiesDock = new QDockWidget("Properties", viewPort);
    sceneNodePropertiesDock->setObjectName(QStringLiteral("sceneNodePropertiesDock"));
    sceneNodePropertiesWidget = new SceneNodePropertiesWidget;
    sceneNodePropertiesWidget->setDatabase(db);
    sceneNodePropertiesWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sceneNodePropertiesWidget->setObjectName(QStringLiteral("SceneNodePropertiesWidget"));
    sceneNodePropertiesDock->setStyleSheet("QWidget { background-color: #202020; }");
	UiManager::propertyWidget = sceneNodePropertiesWidget;

    QWidget *sceneNodeDockWidgetContents = new QWidget(viewPort);
    QScrollArea *sceneNodeScrollArea = new QScrollArea(sceneNodeDockWidgetContents);
    sceneNodeScrollArea->setMinimumWidth(326);
    sceneNodeScrollArea->setStyleSheet("border: 0");
    sceneNodeScrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    sceneNodeScrollArea->setWidget(sceneNodePropertiesWidget);
    sceneNodeScrollArea->setWidgetResizable(true);
    sceneNodeScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QVBoxLayout *sceneNodeLayout = new QVBoxLayout(sceneNodeDockWidgetContents);
    sceneNodeLayout->setContentsMargins(0, 0, 0, 0);
    sceneNodeLayout->addWidget(sceneNodeScrollArea);
    sceneNodeDockWidgetContents->setLayout(sceneNodeLayout);
    sceneNodePropertiesDock->setWidget(sceneNodeDockWidgetContents);

    // Presets Dock
    presetsDock = new QDockWidget("Presets", viewPort);
    presetsDock->setObjectName(QStringLiteral("presetsDock"));

    QWidget *presetDockContents = new QWidget;
    presetDockContents->setStyleSheet( "QWidget { background-color: #151515; }");
    MaterialSets *materialPresets = new MaterialSets;
    materialPresets->setMainWindow(this);
    SkyPresets *skyPresets = new SkyPresets;
    skyPresets->setMainWindow(this);
    ModelPresets *modelPresets = new ModelPresets;
    modelPresets->setMainWindow(this);

    presetsTabWidget = new QTabWidget;
    presetsTabWidget->setObjectName("PresetsTabWidget");
    presetsTabWidget->setMinimumWidth(396);
    presetsTabWidget->addTab(modelPresets, "Primitives");
    presetsTabWidget->addTab(materialPresets, "Materials");
    presetsTabWidget->addTab(skyPresets, "Skyboxes");
    presetDockContents->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    QGridLayout *presetsLayout = new QGridLayout(presetDockContents);
    presetsLayout->setContentsMargins(0, 0, 0, 0);
    presetsLayout->addWidget(presetsTabWidget);
    presetsDock->setWidget(presetDockContents);

    // Asset Dock
    assetDock = new QDockWidget("Asset Browser", viewPort);
    assetDock->setObjectName(QStringLiteral("assetDock"));
    assetWidget = new AssetWidget(db, viewPort);
    assetWidget->setAcceptDrops(true);
    assetWidget->installEventFilter(this);

	connect(assetWidget, SIGNAL(assetItemSelected(QListWidgetItem*)), this, SLOT(assetItemSelected(QListWidgetItem*)));

	assetWidget->sceneView = sceneView;

    QWidget *assetDockContents = new QWidget(viewPort);
    QGridLayout *assetsLayout = new QGridLayout(assetDockContents);
    assetsLayout->addWidget(assetWidget);
    assetsLayout->setContentsMargins(0, 0, 0, 0);
    assetDock->setWidget(assetDockContents);

    // Animation Dock
    animationDock = new QDockWidget("Timeline", viewPort);
    animationDock->setObjectName(QStringLiteral("animationDock"));
    animationWidget = new AnimationWidget;
    UiManager::setAnimationWidget(animationWidget);

    QWidget *animationDockContents = new QWidget;
    QGridLayout *animationLayout = new QGridLayout(animationDockContents);
    animationLayout->setContentsMargins(0, 0, 0, 0);
    animationLayout->addWidget(animationWidget);

    animationDock->setWidget(animationDockContents);

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateAnim()));

    viewPort->addDockWidget(Qt::LeftDockWidgetArea, sceneHierarchyDock);
    viewPort->addDockWidget(Qt::RightDockWidgetArea, sceneNodePropertiesDock);
    viewPort->addDockWidget(Qt::BottomDockWidgetArea, assetDock);
    viewPort->addDockWidget(Qt::BottomDockWidgetArea, animationDock);
    viewPort->addDockWidget(Qt::BottomDockWidgetArea, presetsDock);
    viewPort->tabifyDockWidget(animationDock, assetDock);
}

void MainWindow::setupViewPort()
{
	// ui->MenuBar->setVisible(false);

	worlds_menu = new QPushButton("Worlds");
	worlds_menu->setObjectName("worlds_menu");
	worlds_menu->setCursor(Qt::PointingHandCursor);
	player_menu = new QPushButton("Player");
	player_menu->setObjectName("player_menu");
	player_menu->setCursor(Qt::PointingHandCursor);
	editor_menu = new QPushButton("Editor");
	editor_menu->setObjectName("editor_menu");
	editor_menu->setCursor(Qt::PointingHandCursor);
	assets_menu = new QPushButton("Assets");
	assets_menu->setObjectName("assets_menu");
	assets_menu->setCursor(Qt::PointingHandCursor);

	assets_panel = new QWidget;

	auto hl = new QHBoxLayout;
	hl->setMargin(0);
	hl->setSpacing(12);
	hl->addWidget(worlds_menu);
	hl->addWidget(player_menu);
	hl->addWidget(editor_menu);
	hl->addWidget(assets_menu);

	assets_panel->setLayout(hl);

	jlogo = new QLabel;
#ifdef QT_DEBUG
    jlogo->setPixmap(IrisUtils::getAbsoluteAssetPath("app/images/header_dev.png"));
#else
    jlogo->setPixmap(IrisUtils::getAbsoluteAssetPath("app/images/header.png"));
#endif
	help = new QPushButton;
	help->setObjectName("helpButton");
	help->setText(QChar(fa::questioncircle));
	help->setFont(fontIcons.font(28));
	help->setCursor(Qt::PointingHandCursor);

	help->setStyleSheet(
		"#helpButton { qproperty-icon: url(\"\");"
		"qproperty-iconSize: 48px 48px;"
		"background: transparent;"
		"color: rgba(255,255,255,.9);"
		"background-repeat: no-repeat; }"
		"#helpButton::hover {color: rgba(255, 255, 255, 1); }"
		
	);

	connect(help, &QPushButton::pressed, [this]() {
		QDesktopServices::openUrl(QUrl("http://www.jahshaka.com/tutorials/"));
	});

	prefs = new QPushButton;
	prefs->setObjectName("prefsButton");

	prefs->setText(QChar(fa::cog));
	prefs->setFont(fontIcons.font(28));
	prefs->setCursor(Qt::PointingHandCursor);

	prefs->setStyleSheet(
		"#prefsButton { qproperty-icon: url(\"\");"
		"qproperty-iconSize: 48px 48px;"
		"background: transparent;"
		"color: rgba(255,255,255,.9);"
		"background-repeat: no-repeat; }"
		"#prefsButton::hover { color: rgba(255, 255, 255, 1); }"
	);

	connect(prefs, &QPushButton::pressed, [this]() { showPreferences(); });

	QWidget *buttons = new QWidget;
	QHBoxLayout *bl = new QHBoxLayout;
	buttons->setLayout(bl);
	bl->setSpacing(20);
	bl->addWidget(help);
	bl->addWidget(prefs);

	ui->ohlayout->addWidget(jlogo, 0, 0, Qt::AlignLeft);
	ui->ohlayout->addWidget(assets_panel, 0, 1, Qt::AlignCenter);
	ui->ohlayout->addWidget(buttons, 0, 2, Qt::AlignRight);

    connect(worlds_menu, &QPushButton::pressed, [this]() {
		if (!currentSpace == WindowSpaces::DESKTOP) switchSpace(WindowSpaces::DESKTOP);
	});
    connect(player_menu, &QPushButton::pressed, [this]() { switchSpace(WindowSpaces::PLAYER); });
    connect(editor_menu, &QPushButton::pressed, [this]() { switchSpace(WindowSpaces::EDITOR); });
    connect(assets_menu, &QPushButton::pressed, [this]() { switchSpace(WindowSpaces::ASSETS); });

    sceneContainer = new QWidget;
    QSizePolicy sceneContainerPolicy;
    sceneContainerPolicy.setHorizontalPolicy(QSizePolicy::Preferred);
    sceneContainerPolicy.setVerticalPolicy(QSizePolicy::Preferred);
    sceneContainerPolicy.setVerticalStretch(1);
    sceneContainer->setSizePolicy(sceneContainerPolicy);
    sceneContainer->setAcceptDrops(true);
    sceneContainer->installEventFilter(this);

    controlBar = new QWidget;
    controlBar->setObjectName(QStringLiteral("controlBar"));

    auto container = new QWidget;
    auto containerLayout = new QVBoxLayout;

    auto screenShotBtn = new QPushButton;
    screenShotBtn->setToolTip("Take a screenshot of the scene");
    screenShotBtn->setToolTipDuration(-1);
    screenShotBtn->setStyleSheet("background: transparent");
    screenShotBtn->setIcon(QIcon(":/icons/camera.svg"));

    wireCheckBtn = new QCheckBox("Viewport Wireframes");
    wireCheckBtn->setCheckable(true);

    connect(screenShotBtn, SIGNAL(pressed()), this, SLOT(takeScreenshot()));
    connect(wireCheckBtn, SIGNAL(toggled(bool)), this, SLOT(toggleLightWires(bool)));

    auto controlBarLayout = new QHBoxLayout;
    playSceneBtn = new QPushButton;
    playSceneBtn->setToolTip("Play scene");
    playSceneBtn->setToolTipDuration(-1);
    playSceneBtn->setStyleSheet("background: transparent");
    playSceneBtn->setIcon(QIcon(":/icons/g_play.svg"));

    controlBarLayout->setSpacing(8);
    controlBarLayout->addWidget(screenShotBtn);
    controlBarLayout->addWidget(wireCheckBtn);
    controlBarLayout->addStretch();
    controlBarLayout->addWidget(playSceneBtn);

    controlBar->setLayout(controlBarLayout);
    controlBar->setStyleSheet("#controlBar {  background: #1E1E1E; border-bottom: 1px solid black; }");

    playerControls = new QWidget;
    playerControls->setStyleSheet("background: #1A1A1A");

    auto playerControlsLayout = new QHBoxLayout;

    restartBtn = new QPushButton;
    restartBtn->setCursor(Qt::PointingHandCursor);
    restartBtn->setToolTip("Restart playback");
    restartBtn->setToolTipDuration(-1);
    restartBtn->setStyleSheet("background: transparent");
    restartBtn->setIcon(QIcon(":/icons/rotate-to-right.svg"));
    restartBtn->setIconSize(QSize(16, 16));

    playBtn = new QPushButton;
    playBtn->setCursor(Qt::PointingHandCursor);
    playBtn->setToolTip("Play the scene");
    playBtn->setToolTipDuration(-1);
    playBtn->setStyleSheet("background: transparent");
    playBtn->setIcon(QIcon(":/icons/g_play.svg"));
    playBtn->setIconSize(QSize(24, 24));

    stopBtn = new QPushButton;
    stopBtn->setCursor(Qt::PointingHandCursor);
    stopBtn->setToolTip("Stop playback");
    stopBtn->setToolTipDuration(-1);
    stopBtn->setStyleSheet("background: transparent");
    stopBtn->setIcon(QIcon(":/icons/g_stop.svg"));
    stopBtn->setIconSize(QSize(16, 16));

    playerControlsLayout->setSpacing(12);
    playerControlsLayout->setMargin(6);
    playerControlsLayout->addStretch();
    playerControlsLayout->addWidget(restartBtn);
    playerControlsLayout->addWidget(playBtn);
    playerControlsLayout->addWidget(stopBtn);
    playerControlsLayout->addStretch();

    connect(restartBtn, &QPushButton::pressed, [this]() {
        playBtn->setToolTip("Pause the scene");
        playBtn->setIcon(QIcon(":/icons/g_pause.svg"));
        UiManager::restartScene();
    });

    connect(playBtn, &QPushButton::pressed, [this]() {
        if (UiManager::isScenePlaying) {
            playBtn->setToolTip("Play the scene");
            playBtn->setIcon(QIcon(":/icons/g_play.svg"));
            UiManager::pauseScene();
        } else {
            playBtn->setToolTip("Pause the scene");
            playBtn->setIcon(QIcon(":/icons/g_pause.svg"));
            UiManager::playScene();
        }
    });
    connect(stopBtn, &QPushButton::pressed, [this]() {
        playBtn->setToolTip("Play the scene");
        playBtn->setIcon(QIcon(":/icons/g_play.svg"));
        UiManager::stopScene();
    });

    playerControls->setLayout(playerControlsLayout);

    containerLayout->setSpacing(0);
    containerLayout->setMargin(0);
    containerLayout->addWidget(controlBar);
    containerLayout->addWidget(sceneContainer);
    containerLayout->addWidget(playerControls);

    container->setLayout(containerLayout);

    viewPort = new QMainWindow;
    viewPort->setWindowFlags(Qt::Widget);
    viewPort->setCentralWidget(container);

    sceneView = new SceneViewWidget(viewPort);
    sceneView->setParent(viewPort);
    sceneView->setFocusPolicy(Qt::ClickFocus);
    sceneView->setFocus();
    Globals::sceneViewWidget = sceneView;
    UiManager::setSceneViewWidget(sceneView);

    wireCheckBtn->setChecked(sceneView->getShowLightWires());

    QGridLayout* layout = new QGridLayout;
    layout->addWidget(sceneView);
    layout->setMargin(0);
    sceneContainer->setLayout(layout);

    connect(sceneView, &SceneViewWidget::addDroppedMesh, [this](QString path, bool v, QVector3D pos, QString guid, QString name) {
        addMaterialMesh(path, v, pos, guid, name);
    });

    connect(sceneView,  SIGNAL(initializeGraphics(SceneViewWidget*, QOpenGLFunctions_3_2_Core*)),
            this,       SLOT(initializeGraphics(SceneViewWidget*,   QOpenGLFunctions_3_2_Core*)));

    connect(sceneView,  SIGNAL(sceneNodeSelected(iris::SceneNodePtr)),
            this,       SLOT(sceneNodeSelected(iris::SceneNodePtr)));

    connect(playSceneBtn, SIGNAL(clicked(bool)), SLOT(onPlaySceneButton()));

	widgetStates = QVector<bool>(5);

	//auto values = settings->getValue("widgets", { /* empty */ }).value<QVector<bool>>();

	//if (!values.isEmpty()) {
		widgetStates[static_cast<int>(Widget::HIERARCHY)]	= true;
		widgetStates[static_cast<int>(Widget::PROPERTIES)]	= true;
		widgetStates[static_cast<int>(Widget::ASSETS)]		= true;
		widgetStates[static_cast<int>(Widget::TIMELINE)]	= true;
		widgetStates[static_cast<int>(Widget::PRESETS)]		= true;
	//}
}

void MainWindow::setupDesktop()
{
	pmContainer = new ProjectManager(db, this);
	pmContainer->mainWindow = this;
	_assetView = new AssetView(db, this);
	_assetView->installEventFilter(this);

	ui->stackedWidget->addWidget(pmContainer);
	ui->stackedWidget->addWidget(viewPort);
	ui->stackedWidget->addWidget(_assetView);

	connect(pmContainer, SIGNAL(fileToOpen(bool)), SLOT(openProject(bool)));
	connect(pmContainer, SIGNAL(closeProject()), SLOT(closeProject()));
	connect(pmContainer, SIGNAL(fileToCreate(QString, QString)), SLOT(newProject(QString, QString)));
	connect(pmContainer, SIGNAL(exportProject()), SLOT(exportSceneAsZip()));
}

void MainWindow::setupToolBar()
{
    toolBar = new QToolBar;
	toolBar->setIconSize(QSize(14, 14));
	QAction *actionUndo = new QAction;
	actionUndo->setToolTip("Undo last action");
	actionUndo->setObjectName(QStringLiteral("actionUndo"));
	actionUndo->setText(QChar(fa::reply));
	actionUndo->setFont(fontIcons.font(16)); 
	toolBar->addAction(actionUndo);

	QAction *actionRedo = new QAction;
	actionRedo->setToolTip("Redo last action");
	actionRedo->setObjectName(QStringLiteral("actionRedo"));
	actionRedo->setText(QChar(fa::share));
	actionRedo->setFont(fontIcons.font(16));
	toolBar->addAction(actionRedo);

	toolBar->addSeparator();

	connect(actionUndo, SIGNAL(triggered(bool)), SLOT(undo()));
	connect(actionRedo, SIGNAL(triggered(bool)), SLOT(redo()));

    QAction *actionTranslate = new QAction;
    actionTranslate->setObjectName(QStringLiteral("actionTranslate"));
    actionTranslate->setCheckable(true);
	actionTranslate->setToolTip("Manipulator for translating objects");
	actionTranslate->setText(QChar(fa::arrows));
	actionTranslate->setFont(fontIcons.font(16));
	toolBar->addAction(actionTranslate);

    QAction *actionRotate = new QAction;
    actionRotate->setObjectName(QStringLiteral("actionRotate"));
    actionRotate->setCheckable(true);
	actionRotate->setToolTip("Manipulator for rotating objects");
	actionRotate->setText(QChar(fa::rotateright));
	actionRotate->setFont(fontIcons.font(16));
	toolBar->addAction(actionRotate);

    QAction *actionScale = new QAction;
    actionScale->setObjectName(QStringLiteral("actionScale"));
    actionScale->setCheckable(true);
	actionScale->setToolTip("Manipulator for scaling objects");
	actionScale->setText(QChar(fa::expand));
	actionScale->setFont(fontIcons.font(16));
	toolBar->addAction(actionScale);

    toolBar->addSeparator();

    QAction *actionGlobalSpace = new QAction;
    actionGlobalSpace->setObjectName(QStringLiteral("actionGlobalSpace"));
    actionGlobalSpace->setCheckable(true);
	actionGlobalSpace->setToolTip("Move objects relative to the global world");
	actionGlobalSpace->setText(QChar(fa::globe));
	actionGlobalSpace->setFont(fontIcons.font(16));
	toolBar->addAction(actionGlobalSpace);

    QAction *actionLocalSpace = new QAction;
    actionLocalSpace->setObjectName(QStringLiteral("actionLocalSpace"));
    actionLocalSpace->setCheckable(true);
	actionLocalSpace->setToolTip("Move objects relative to their transform");
	actionLocalSpace->setText(QChar(fa::cube));
	actionLocalSpace->setFont(fontIcons.font(16));
	toolBar->addAction(actionLocalSpace);

    toolBar->addSeparator();

    QAction *actionFreeCamera = new QAction;
    actionFreeCamera->setObjectName(QStringLiteral("actionFreeCamera"));
    actionFreeCamera->setCheckable(true);
	actionFreeCamera->setToolTip("Freely move and orient the camera");
	actionFreeCamera->setText(QChar(fa::eye));
	actionFreeCamera->setFont(fontIcons.font(16));
	toolBar->addAction(actionFreeCamera);

    QAction *actionArcballCam = new QAction;
    actionArcballCam->setObjectName(QStringLiteral("actionArcballCam"));
    actionArcballCam->setCheckable(true);
	actionArcballCam->setToolTip("Move and orient the camera around a fixed point");
	actionArcballCam->setText(QChar(fa::dotcircleo));
	actionArcballCam->setFont(fontIcons.font(16));
	toolBar->addAction(actionArcballCam);

    connect(actionTranslate,    SIGNAL(triggered(bool)), SLOT(translateGizmo()));
    connect(actionRotate,       SIGNAL(triggered(bool)), SLOT(rotateGizmo()));
    connect(actionScale,        SIGNAL(triggered(bool)), SLOT(scaleGizmo()));

    transformGroup = new QActionGroup(viewPort);
    transformGroup->addAction(actionTranslate);
    transformGroup->addAction(actionRotate);
    transformGroup->addAction(actionScale);

    connect(actionGlobalSpace,  SIGNAL(triggered(bool)), SLOT(useGlobalTransform()));
    connect(actionLocalSpace,   SIGNAL(triggered(bool)), SLOT(useLocalTransform()));

    transformSpaceGroup = new QActionGroup(viewPort);
    transformSpaceGroup->addAction(actionGlobalSpace);
    transformSpaceGroup->addAction(actionLocalSpace);
    ui->actionGlobalSpace->setChecked(true);

    connect(actionFreeCamera,   SIGNAL(triggered(bool)), SLOT(useFreeCamera()));
    connect(actionArcballCam,   SIGNAL(triggered(bool)), SLOT(useArcballCam()));

    cameraGroup = new QActionGroup(viewPort);
    cameraGroup->addAction(actionFreeCamera);
    cameraGroup->addAction(actionArcballCam);
    actionFreeCamera->setChecked(true);

    // this acts as a spacer
    QWidget* empty = new QWidget();
    empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    toolBar->addWidget(empty);

	QAction *actionExport = new QAction;
	actionExport->setObjectName(QStringLiteral("actionExport"));
	actionExport->setCheckable(false);
	actionExport->setToolTip("Export the current scene");
	actionExport->setText(QChar(fa::upload));
	actionExport->setFont(fontIcons.font(16)); 
	toolBar->addAction(actionExport);

	actionSaveScene = new QAction;
	actionSaveScene->setObjectName(QStringLiteral("actionSaveScene"));
	actionSaveScene->setVisible(!settings->getValue("auto_save", true).toBool());
	actionSaveScene->setCheckable(false);
	actionSaveScene->setToolTip("Save the current scene");
	actionSaveScene->setText(QChar(fa::floppyo));
	actionSaveScene->setFont(fontIcons.font(16));
	toolBar->addAction(actionSaveScene);

	QAction *viewDocks = new QAction;
	viewDocks->setObjectName(QStringLiteral("viewDocks"));
	viewDocks->setCheckable(false);
	viewDocks->setToolTip("Toggle Widgets");
	viewDocks->setText(QChar(fa::listalt));
	viewDocks->setFont(fontIcons.font(16));
	toolBar->addAction(viewDocks);

	connect(actionExport,		SIGNAL(triggered(bool)), SLOT(exportSceneAsZip()));
	connect(viewDocks,			SIGNAL(triggered(bool)), SLOT(toggleDockWidgets()));
	connect(actionSaveScene,	SIGNAL(triggered(bool)), SLOT(saveScene()));

    /*
    vrButton = new QPushButton();
    QIcon icovr(":/icons/virtual-reality.svg");
    vrButton->setIcon(icovr);
    vrButton->setObjectName("vrButton");
    toolBar->addWidget(vrButton);
    */

    viewPort->addToolBar(toolBar);
}

void MainWindow::setupShortcuts()
{
    // Translation, Rotation and Scaling gizmo shortcuts for
    QShortcut *shortcut = new QShortcut(QKeySequence("t"),sceneView);
    connect(shortcut, SIGNAL(activated()), this, SLOT(translateGizmo()));

    shortcut = new QShortcut(QKeySequence("r"),sceneView);
    connect(shortcut, SIGNAL(activated()), this, SLOT(rotateGizmo()));

    shortcut = new QShortcut(QKeySequence("s"),sceneView);
    connect(shortcut, SIGNAL(activated()), this, SLOT(scaleGizmo()));

    // Save
    shortcut = new QShortcut(QKeySequence("ctrl+s"),sceneView);
    connect(shortcut, SIGNAL(activated()), this, SLOT(saveScene()));
}

void MainWindow::toggleDockWidgets()
{
	QDialog *d = new QDialog(this);
	d->setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::Popup);

	d->setStyleSheet(
		"QDialog { border: 1px solid black; background: #1E1E1E; }"
		"QPushButton { padding: 8px 24px; border-radius: 1px; }"
		"QPushButton[accessibleName=\"toggleAbles\"]:checked { background: #1E1E1E; }"
		"QPushButton[accessibleName=\"toggleAbles\"] { background: #3E3E3E; }"
	);

	QVBoxLayout *dl = new QVBoxLayout;
	dl->setContentsMargins(20, 10, 20, 16);
	d->setLayout(dl);

	QPushButton *hierarchy = new QPushButton("Hierarchy");
	hierarchy->setAccessibleName(QStringLiteral("toggleAbles"));
	hierarchy->setCheckable(true);
	hierarchy->setChecked(widgetStates[(int) Widget::HIERARCHY]);

	QPushButton *properties = new QPushButton("Properties");
	properties->setAccessibleName(QStringLiteral("toggleAbles"));
	properties->setCheckable(true);
	properties->setChecked(widgetStates[(int) Widget::PROPERTIES]);

	QPushButton *presets = new QPushButton("Presets");
	presets->setAccessibleName(QStringLiteral("toggleAbles"));
	presets->setCheckable(true);
	presets->setChecked(widgetStates[(int) Widget::PRESETS]);

	QPushButton *timeline = new QPushButton("Timeline");
	timeline->setAccessibleName(QStringLiteral("toggleAbles"));
	timeline->setCheckable(true);
	timeline->setChecked(widgetStates[(int) Widget::TIMELINE]);

	QPushButton *assets = new QPushButton("Assets Browser");
	assets->setAccessibleName(QStringLiteral("toggleAbles"));
	assets->setCheckable(true);
	assets->setChecked(widgetStates[(int) Widget::ASSETS]);

	QPushButton *closeAll = new QPushButton("Close All");
	closeAll->setCheckable(true);
	//closeAll->setChecked(true);

	QPushButton *restoreAll = new QPushButton("Restore All");
	restoreAll->setCheckable(true);
	//restoreAll->setChecked(true);

	QLabel *label = new QLabel("Toggle Widgets");
	label->setAlignment(Qt::AlignCenter);
	label->setContentsMargins(0, 0, 0, 6);
	dl->addWidget(label);

	dl->addWidget(hierarchy);
	dl->addWidget(properties);
	dl->addWidget(presets);
	dl->addWidget(timeline);
	dl->addWidget(assets);

	QPushButton *saveLayout = new QPushButton("Save");
	
	connect(saveLayout, &QPushButton::pressed, [=]() {
		//widgetStates[(int) Widget::HIERARCHY]	= hierarchy->isChecked() || !sceneHierarchyDock->isVisible();
		//widgetStates[(int) Widget::PROPERTIES]	= properties->isChecked() || !sceneNodePropertiesDock->isVisible();
		//widgetStates[(int) Widget::ASSETS]		= assets->isChecked() || !assetDock->isVisible();
		//widgetStates[(int) Widget::TIMELINE]	= timeline->isChecked() || !animationDock->isVisible();
		//widgetStates[(int) Widget::PRESETS]		= presets->isChecked() || !presetsDock->isVisible();

		//// saveState and saveGeometry don't seem to work if visibility is altered so do this instead
		//settings->setValue("widgets", QVariant::fromValue(widgetStates));
	});

	QWidget *cw = new QWidget;
	QHBoxLayout *cl = new QHBoxLayout;
	cl->setMargin(0);
	cw->setLayout(cl);
	cl->addWidget(closeAll);
	cl->addWidget(restoreAll);
	//cl->addWidget(saveLayout);
	dl->addWidget(cw);

	connect(hierarchy, &QPushButton::toggled, [&](bool set) {
		sceneHierarchyDock->setVisible(set);
		widgetStates[(int)Widget::HIERARCHY] = set;
	});

	connect(properties, &QPushButton::toggled, [this](bool set) {
		sceneNodePropertiesDock->setVisible(set);
		widgetStates[(int)Widget::PROPERTIES] = set;
	});

	connect(presets, &QPushButton::toggled, [this](bool set) {
		presetsDock->setVisible(set);
		widgetStates[(int)Widget::PRESETS] = set;
	});

	connect(timeline, &QPushButton::toggled, [this](bool set) {
		animationDock->setVisible(set);
		widgetStates[(int)Widget::TIMELINE] = set;
	});

	connect(assets, &QPushButton::toggled, [this](bool set) {
		assetDock->setVisible(set);
		widgetStates[(int)Widget::ASSETS] = set;
	});

	connect(closeAll,	&QPushButton::pressed,	[&]() {
		sceneHierarchyDock->close();
		sceneNodePropertiesDock->close();
		presetsDock->close();
		assetDock->close();
		animationDock->close();

		hierarchy->setChecked(false);
		properties->setChecked(false);
		assets->setChecked(false);
		timeline->setChecked(false);
		presets->setChecked(false);
	});

	connect(restoreAll, &QPushButton::pressed,	[&]() {
		sceneHierarchyDock->show();
		sceneNodePropertiesDock->show();
		presetsDock->show();
		assetDock->show();
		animationDock->show();

		hierarchy->setChecked(true);
		properties->setChecked(true);
		assets->setChecked(true);
		timeline->setChecked(true);
		presets->setChecked(true);
	});

	d->exec();
}

void MainWindow::showPreferences()
{
    prefsDialog->exec();
}

void MainWindow::exitApp()
{
    QApplication::exit();
}

void MainWindow::updateSceneSettings()
{
	if (UiManager::isSceneOpen || !!scene) {
		scene->setOutlineWidth(prefsDialog->worldSettings->outlineWidth);
		scene->setOutlineColor(prefsDialog->worldSettings->outlineColor);
	}

	actionSaveScene->setVisible(!prefsDialog->worldSettings->autoSave);
}

void MainWindow::undo()
{
    if (undoStack->canUndo()) undoStack->undo();
}

void MainWindow::redo()
{
    if (undoStack->canRedo()) undoStack->redo();
}

void MainWindow::takeScreenshot()
{
    auto img = sceneView->takeScreenshot();
    ScreenshotWidget screenshotWidget;
    screenshotWidget.setMaximumWidth(1280);
    screenshotWidget.setMaximumHeight(720);
    screenshotWidget.layout()->setSizeConstraint(QLayout::SetNoConstraint);
    screenshotWidget.setImage(img);
    screenshotWidget.exec();
}

void MainWindow::toggleLightWires(bool state)
{
    sceneView->setShowLightWires(state);
}

void MainWindow::toggleWidgets(bool state)
{
    sceneHierarchyDock->setVisible(state);
    sceneNodePropertiesDock->setVisible(state);
    presetsDock->setVisible(state);
    assetDock->setVisible(state);
    animationDock->setVisible(state);
    playerControls->setVisible(!state);
}

void MainWindow::showProjectManagerInternal()
{
    if (UiManager::isUndoStackDirty()) {
        QMessageBox::StandardButton option;
        option = QMessageBox::question(this,
                                       "Unsaved Changes",
                                       "There are unsaved changes, save before closing?",
                                       QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

        if (option == QMessageBox::Yes) {
            saveScene();
        } else if (option == QMessageBox::Cancel) {
            return;
        }
    }

    if (UiManager::isScenePlaying) enterEditMode();
    hide();
    pmContainer->populateDesktop(true);
    //pmContainer->showMaximized();
    pmContainer->cleanupOnClose();
}

void MainWindow::newScene()
{
    this->sceneView->makeCurrent();
    auto scene = this->createDefaultScene();
    this->setScene(scene);
    this->sceneView->resetEditorCam();
    this->sceneView->doneCurrent();
}

void MainWindow::newProject(const QString &filename, const QString &projectPath)
{
    if (UiManager::isSceneOpen) closeProject();

    newScene();
    UiManager::isSceneOpen = true;
    ui->actionClose->setDisabled(false);

    saveScene(filename, projectPath);

    assetWidget->trigger();

    UiManager::clearUndoStack();
    UiManager::updateWindowTitle();

    switchSpace(WindowSpaces::EDITOR);
}

MainWindow::~MainWindow()
{
    this->db->closeDatabase();
    delete ui;
}

void MainWindow::useFreeCamera()
{
    sceneView->setFreeCameraMode();
}

void MainWindow::useArcballCam()
{
    sceneView->setArcBallCameraMode();
}

void MainWindow::useLocalTransform()
{
    sceneView->setGizmoTransformToLocal();
}

void MainWindow::useGlobalTransform()
{
    sceneView->setGizmoTransformToGlobal();
}

void MainWindow::translateGizmo()
{
    sceneView->setGizmoLoc();
}

void MainWindow::rotateGizmo()
{
    sceneView->setGizmoRot();
}

void MainWindow::scaleGizmo()
{
    sceneView->setGizmoScale();
}

void MainWindow::onPlaySceneButton()
{
    if (UiManager::isScenePlaying) {
        enterEditMode();
    }
    else {
        enterPlayMode();
    }
}

void MainWindow::enterEditMode()
{
    UiManager::isScenePlaying = false;
    UiManager::enterEditMode();
    playSceneBtn->setToolTip("Play scene");
    playSceneBtn->setIcon(QIcon(":/icons/g_play.svg"));
}

void MainWindow::enterPlayMode()
{
    UiManager::isScenePlaying = true;
    UiManager::enterPlayMode();
    playSceneBtn->setToolTip("Stop playing");
    playSceneBtn->setIcon(QIcon(":/icons/g_stop.svg"));
}
