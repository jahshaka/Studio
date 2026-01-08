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
#include "irisgl/src/scenegraph/grabnode.h"
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
#include "dialogs/donatedialog.h"
#include "dialogs/custompopup.h"
#include "core/assethelper.h"
#include "core/scenenodehelper.h"
#include "io/newjsonadapter.h"

#include <QFontDatabase>
#include <QOpenGLContext>
#include <qstandarditemmodel.h>
#include <QKeyEvent>
#include <QMessageBox>
#include <QOpenGLDebugLogger>
#include <QUndoStack>

#include <QApplication>
#include <QGuiApplication>
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
#include <QToolButton>

#include "dialogs/loadmeshdialog.h"
#include "core/surfaceview.h"
#include "core/nodekeyframeanimation.h"
#include "core/nodekeyframe.h"
#include "globals.h"

#include "widgets/animationwidget.h"

#include "widgets/layertreewidget.h"
#include "core/project.h"
#include "widgets/accordianbladewidget.h"

#include "editor/editorcameracontroller.h"
#include "core/settingsmanager.h"
#include "dialogs/preferencesdialog.h"
#include "dialogs/preferences/worldsettings.h"
#include "dialogs/preferences/worldsettingswidget.h"
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

#include "../src/widgets/skypresets.h"

#include "widgets/assetmodelpanel.h"
#include "widgets/assetmaterialpanel.h"

#include "../src/widgets/assetview.h"
#include "dialogs/toast.h"

#include "zip.h"

#include "irisgl/src/scenegraph/scene.h"
#include "irisgl/src/physics/environment.h"
#include "irisgl/src/physics/charactercontroller.h"
#include "irisgl/src/bullet3/src/btBulletDynamicsCommon.h"

#include "shadergraph/shadergraphmainwindow.h"
#include "../src/player/playerwidget.h"

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

	currentSpace = WindowSpaces::DESKTOP;
	originalTitle = windowTitle();

	setupProjectDB();
	createPostProcessDockWidget();

    prefsDialog = new PreferencesDialog(nullptr, db, settings);
    aboutDialog = new AboutDialog();

    camControl = Q_NULLPTR;
    vrMode = false;

    setupFileMenu();
	fontIcons = new QtAwesome;
	fontIcons->initFontAwesome();

    setupViewPort();
    setupDesktop();
    setupToolBar();
    setupDockWidgets();
    setupShortcuts();
	setupUndoRedo();
	updateTopMenuStates(currentSpace);

	restoreGeometry(settings->getValue("geometry", "").toByteArray());
	restoreState(settings->getValue("windowState", "").toByteArray());

	undoStackCount = 0;

    QSurfaceFormat format;
    format.setDepthBufferSize(32);
    format.setMajorVersion(3);
    format.setMinorVersion(2);
    format.setProfile(QSurfaceFormat::CoreProfile);

    loadingContext = new QOpenGLContext();
    loadingContext->setFormat(format);
    auto globContext = QOpenGLContext::globalShareContext();
    loadingContext->setShareContext(globContext);
    loadingContext->create();

    loadingSurface = new QOffscreenSurface();
    loadingSurface->setFormat(format);
    loadingSurface->create();
}

void MainWindow::grabOpenGLContextHack()
{
    //switchSpace(WindowSpaces::PLAYER);
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
        QJsonDocument(props).toJson(),
        QByteArray(),
        QByteArray()
    );

    {
        // Make the default plane a static physics object
        iris::PhysicsProperty physicsProperties;
        physicsProperties.objectMass = .0f;
        physicsProperties.isStatic = true;
        physicsProperties.objectCollisionMargin = .1f;
        physicsProperties.objectRestitution = .01f;
        physicsProperties.type = iris::PhysicsType::Static;
        physicsProperties.shape = iris::PhysicsCollisionShape::Plane;

        node->isPhysicsBody = true;
        node->physicsProperty = physicsProperties;
    }

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

#ifndef BUILD_PLAYER_ONLY
    if (closing) {
        if (!getSettingsManager()->getValue("ddialog_seen", "false").toBool()) {
            DonateDialog dialog;
            dialog.updateVersion(Constants::CONTENT_VERSION);
            dialog.exec();
        }
    }
#endif // !BUILD_PLAYER_ONLY

	settings->setValue("geometry", saveGeometry());
	settings->setValue("windowState", saveState());

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

void MainWindow::stopAnimWidget()
{
    animWidget->stopAnimation();
}

void MainWindow::makeLoadingGLContextCurrent()
{
    loadingContext->makeCurrent(loadingSurface);
}

void MainWindow::setupProjectDB()
{
    const QString path = IrisUtils::join(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation), Constants::JAH_DATABASE
    );

    db = new Database();
	if (db->initializeDatabase(path)) {
		db->createAllTables();
	}
	Globals::db = db;
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
	if (currentSpace == space)
		return;
	ListWidget::stopHighlightedNode();

	// properly shutdown previous space
	switch (currentSpace) {
	case WindowSpaces::PLAYER:
		playerView->end();
		break;
	case WindowSpaces::EDITOR:
		sceneView->end();
		break;
    default:
        break;
	}

    previousSpace = currentSpace;
    switch (currentSpace = space) {
        case WindowSpaces::DESKTOP: {
			if (UiManager::isSceneOpen) {
				//if (settings->getValue("auto_save", true).toBool()) saveScene();
				//saveScene();
				if (sceneView->isInitialized())
					updateCurrentSceneThumbnail();
				pmContainer->populateDesktop(true);
			}
			
			ui->stackedWidget->setCurrentIndex(0);

            toggleWidgets(false);
            ui->actionClose->setDisabled(true);
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

			this->sceneView->setWindowSpace(space);
            playSceneBtn->show();
            this->enterEditMode();
            UiManager::sceneMode = SceneMode::EditMode;

            assetWidget->refresh();
			isSceneOpen = true;

			sceneView->begin();
            break;
        }

        case WindowSpaces::PLAYER: {
            ui->stackedWidget->setCurrentIndex(4);
            toggleWidgets(false);
            toolBar->setVisible(false);

			this->sceneView->setWindowSpace(space);
            UiManager::sceneMode = SceneMode::PlayMode;
            playSceneBtn->hide();
            this->enterPlayMode();
			playerView->begin();
            playerView->onPlayScene();

            break;
        }

        case WindowSpaces::ASSETS: {
            ui->stackedWidget->setCurrentIndex(2);
            ui->stackedWidget->currentWidget()->setFocus();
			static_cast<AssetView*>(ui->stackedWidget->currentWidget())->spaceSplits();
    		toggleWidgets(false);
    		toolBar->setVisible(false);
			if (UiManager::isSceneOpen) {
				playSceneBtn->hide();
			}
    		
			break;
    	}

		case WindowSpaces::EFFECT: {
			ui->stackedWidget->setCurrentIndex(3);
			ui->stackedWidget->currentWidget()->setFocus();

			toolBar->setVisible(false);

			shaderGraph->refreshShaderGraph();

			break;
		}

        default: break;
    }

	updateTopMenuStates(space);
}

void MainWindow::updateTopMenuStates(WindowSpaces activeSpace)
{
	const QString disabledMenu = "color: #444; border-color: #111";
	const QString selectedMenu = "border-color: #3498db";
	const QString unselectedMenu = "border-color: #111";

	if (activeSpace == WindowSpaces::EDITOR)
		toolBar->setVisible(true);
	else
		toolBar->setVisible(false);

	worlds_menu->setStyleSheet(activeSpace==WindowSpaces::DESKTOP? selectedMenu:unselectedMenu);
	worlds_menu->setCursor(Qt::PointingHandCursor);

	assets_menu->setStyleSheet(activeSpace == WindowSpaces::ASSETS ? selectedMenu : unselectedMenu);
	assets_menu->setCursor(Qt::PointingHandCursor);

	effect_menu->setStyleSheet(activeSpace == WindowSpaces::EFFECT ? selectedMenu : unselectedMenu);
	effect_menu->setCursor(Qt::PointingHandCursor);

	editor_menu->setStyleSheet(activeSpace == WindowSpaces::EDITOR ? selectedMenu : unselectedMenu);
	player_menu->setStyleSheet(activeSpace == WindowSpaces::PLAYER ? selectedMenu : unselectedMenu);

	if (UiManager::isSceneOpen) {
		editor_menu->setEnabled(true);
		editor_menu->setCursor(Qt::PointingHandCursor);
		player_menu->setEnabled(true);
		player_menu->setCursor(Qt::PointingHandCursor);
	}
	else {
		editor_menu->setEnabled(false);
		editor_menu->setCursor(Qt::ArrowCursor);
		player_menu->setEnabled(false);
		player_menu->setCursor(Qt::ArrowCursor);
		editor_menu->setStyleSheet(disabledMenu);
		player_menu->setStyleSheet(disabledMenu);

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
	// if the sceneView isnt initialized then the scene was never
	// opened in edit mode. This also means no renderer was initialized.
	// There's no need to save (nick)
	if (!sceneView->isInitialized())
		return;

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
	if(!!scene)
        removeScene();

    makeLoadingGLContextCurrent();
    std::unique_ptr<SceneReader> reader(new SceneReader);
	reader->setDatabaseHandle(db);

    EditorData* editorData = Q_NULLPTR;
    UiManager::updateWindowTitle();

    //auto postMan = sceneView->getRenderer()->getPostProcessManager();
    //postMan->clearPostProcesses();

	auto postMan = iris::PostProcessManagerPtr();
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
		// needs to be done so controllers can have the correct
		// camera
		playerView->setScene(scene);
        wireCheckAction->setChecked(editorData->showLightWires);
		physicsCheckAction->setChecked(editorData->showDebugDrawFlags);
    }

    assetWidget->trigger();

	undoStackCount = 0;
	playMode ? switchSpace(WindowSpaces::PLAYER) : switchSpace(WindowSpaces::EDITOR);
	updateTopMenuStates(UiManager::playMode ? WindowSpaces::PLAYER : WindowSpaces::EDITOR);

	// highlight root node
	sceneHierarchyWidget->selectNode(scene->getRootNode()->getGUID());
	sceneNodePropertiesWidget->setSceneNode(scene->getRootNode());

    // autoplay scenes immediately
    if (playMode) {
        playBtn->setToolTip("Pause the scene");
        playBtn->setIcon(QIcon(":/icons/g_pause.svg"));
        UiManager::playScene();
        playerView->onPlayScene();
    }

	// force a refresh
	this->update();
}

void MainWindow::closeProject()
{
    {
		scene->stopPlayingAmbientMusic();
        scene->getPhysicsEnvironment()->stopPhysics();
        scene->getPhysicsEnvironment()->stopSimulation();

        if (!scene->getPhysicsEnvironment()->nodeTransforms.isEmpty()) {
            for (const auto &node : scene->getRootNode()->children) {
                if (node->isPhysicsBody) {
                    node->setGlobalTransform(scene->getPhysicsEnvironment()->nodeTransforms.value(node->getGUID()));
                }
            }
        }

        if (UiManager::isSceneOpen) {
            if (settings->getValue("auto_save", true).toBool()) saveScene();
        }

        scene->getPhysicsEnvironment()->destroyPhysicsWorld();

        //UiManager::stopPhysicsSimulation();
        playSimBtn->setText("Simulate Physics");
        playSimBtn->setToolTip("Simulate physics only");

        QVariantMap options;
        options.insert("color", QColor(52, 152, 219));
        options.insert("color-active", QColor(52, 152, 219));
        playSimBtn->setIcon(fontIcons->icon(fa::play, options));
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

	if (sceneView->isInitialized())
		sceneView->end();
	playerView->end();
}

/// TODO - this needs to be fixed after the objects are added back to the uniforms array/obj
void MainWindow::applyMaterialPreset(QString guid)
{
    auto preset = Constants::Reserved::DefaultMaterials.value(guid);
    auto defaultMats = assetMaterialPanel->getDefaultMaterials();
    for (const auto &material : defaultMats) {
        if (material.name == preset) {
            applyMaterialPreset(material);
            break;
        }
    }
}

void MainWindow::applyMaterialPreset(MaterialPreset preset)
{
    if (!activeSceneNode || activeSceneNode->sceneNodeType != iris::SceneNodeType::Mesh) return;

    auto meshNode = activeSceneNode.staticCast<iris::MeshNode>();

    // TODO - set the TYPE for a preset in the .material file so we can have other preset types
    // only works for the default material at the moment...
    auto m = iris::CustomMaterial::create();
    m->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));

    m->setValue("diffuseTexture", preset.diffuseTexture);
    m->setValue("specularTexture", preset.specularTexture);
    m->setValue("normalTexture", preset.normalTexture);
    m->setValue("reflectionTexture", preset.reflectionTexture);

    m->setValue("ambientColor", preset.ambientColor);
    m->setValue("diffuseColor", preset.diffuseColor);
    m->setValue("specularColor", preset.specularColor);

    m->setValue("shininess", preset.shininess);
    m->setValue("normalIntensity", preset.normalIntensity);
    m->setValue("reflectionInfluence", preset.reflectionInfluence);
    m->setValue("textureScale", preset.textureScale);

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
        preset.name,
        static_cast<int>(ModelTypes::Material),
        fguid,
        QString(),
        QString(),
        QByteArray(),
        QByteArray(),
        QJsonDocument(material).toJson()
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
    this->sceneNodePropertiesWidget->refreshMaterial(preset.type);
}

void MainWindow::favoriteItem(QListWidgetItem *item)
{
    if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Material)) {
        assetMaterialPanel->addNewItem(item);
        presetsTabWidget->setCurrentIndex(1);
    }
    else if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Object)) {
        assetModelPanel->addNewItem(item);
        presetsTabWidget->setCurrentIndex(0);
    }
}

void MainWindow::refreshThumbnail(const QString &guid)
{
    QString meshGuid = db->fetchObjectMesh(guid, static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh));
    auto assetName = db->fetchAsset(meshGuid).name;

    ThumbnailGenerator::getSingleton()->requestThumbnail(
        ThumbnailRequestType::ImportedMesh,
        QDir(Globals::project->getProjectFolder()).filePath(assetName),
        guid
    );
}

void MainWindow::refreshThumbnail(QListWidgetItem *item)
{
    if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Object)) {
        QString itemGuid = item->data(MODEL_GUID_ROLE).toString();
        QString meshGuid = db->fetchObjectMesh(itemGuid, static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh));

        auto assetName = db->fetchAsset(meshGuid).name;

        ThumbnailGenerator::getSingleton()->requestThumbnail(
            ThumbnailRequestType::ImportedMesh,
            QDir(Globals::project->getProjectFolder()).filePath(assetName),
            item->data(MODEL_GUID_ROLE).toString()
        );
    }
}

void MainWindow::setScene(QSharedPointer<iris::Scene> scene)
{
    this->scene = scene;
    //this->sceneView->context()->setShareContext(loadingContext);
    this->sceneView->setScene(scene);
	this->playerView->setScene(scene);
    this->sceneHierarchyWidget->setScene(scene);
    this->sceneNodePropertiesWidget->setScene(scene);

    // interim...
    updateSceneSettings();
}

void MainWindow::removeScene()
{
    sceneView->cleanup();
    sceneNodePropertiesWidget->setScene(iris::ScenePtr());
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
    const QString nodeGuid = GUIDManager::generateGUID();
    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
        ":/content/primitives/plane.obj",
        "Plane",
        nodeGuid
    );
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toJson(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addGround()
{
    this->sceneView->makeCurrent();
    const QString nodeGuid = GUIDManager::generateGUID();
    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
        ":/models/ground.obj",
        "Ground",
        nodeGuid
    );
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toJson(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addCone()
{
    this->sceneView->makeCurrent();
    const QString nodeGuid = GUIDManager::generateGUID();
    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
        ":/content/primitives/cone.obj",
        "Cone",
        nodeGuid
    );
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toJson(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addCapsule()
{
    this->sceneView->makeCurrent();
    const QString nodeGuid = GUIDManager::generateGUID();
    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
        ":/content/primitives/capsule.obj",
        "Plane",
        nodeGuid
    );
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toJson(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addCube()
{
    this->sceneView->makeCurrent();
    const QString nodeGuid = GUIDManager::generateGUID();
    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
        ":/content/primitives/cube.obj",
        "Cube",
        nodeGuid
    );
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toJson(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addTorus()
{
    this->sceneView->makeCurrent();
    const QString nodeGuid = GUIDManager::generateGUID();
    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
        ":/content/primitives/torus.obj",
        "Torus",
        nodeGuid
    );
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toJson(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addSphere()
{
    this->sceneView->makeCurrent();
    const QString nodeGuid = GUIDManager::generateGUID();
    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
        ":/content/primitives/sphere.obj",
        "Sphere",
        nodeGuid
    );
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toJson(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addCylinder()
{
    this->sceneView->makeCurrent();
    const QString nodeGuid = GUIDManager::generateGUID();
    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
        ":/content/primitives/cylinder.obj",
        "Cylinder",
        nodeGuid
    );
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toJson(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addPyramid()
{
    this->sceneView->makeCurrent();
    const QString nodeGuid = GUIDManager::generateGUID();
    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
        ":/content/primitives/pyramid.obj",
        "Pyramid",
        nodeGuid
    );
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toJson(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addSponge()
{
    this->sceneView->makeCurrent();
    const QString nodeGuid = GUIDManager::generateGUID();
    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
        ":/content/primitives/sponge.obj",
        "Sponge",
        nodeGuid
    );
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toJson(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addTeapot()
{
    this->sceneView->makeCurrent();
    const QString nodeGuid = GUIDManager::generateGUID();
    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
        ":/content/primitives/teapot.obj",
        "Teapot",
        nodeGuid
    );
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toJson(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addSteps()
{
    this->sceneView->makeCurrent();
    const QString nodeGuid = GUIDManager::generateGUID();
    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
        ":/content/primitives/steps.obj",
        "Steps",
        nodeGuid
    );
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toJson(),
        QByteArray(),
        QByteArray()
    );
    addNodeToScene(node);
}

void MainWindow::addGear()
{
    this->sceneView->makeCurrent();
    const QString nodeGuid = GUIDManager::generateGUID();
    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
        ":/content/primitives/gear.obj",
        "Gear",
        nodeGuid
    );
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toJson(),
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
    node->setName("Avatar");
    addNodeToScene(node);

	// Set all other controllers to false
	for (auto node : scene->getRootNode()->children) {
		if (node->getSceneNodeType() == iris::SceneNodeType::Viewer) {
			node.staticCast<iris::ViewerNode>()->setActiveCharacterController(false);
		}
	}

	node->setActiveCharacterController(true);
	scene->getPhysicsEnvironment()->addCharacterControllerToWorldUsingNode(node);
}

void MainWindow::addGrabHand()
{
	this->sceneView->makeCurrent();
	auto node = iris::GrabNode::create();
	node->setName("Hand");
	addNodeToScene(node);
}

void MainWindow::addParticleSystem()
{
    this->sceneView->makeCurrent();
    auto node = iris::ParticleSystemNode::create();
    node->setName("Particle System");

    auto fguid = GUIDManager::generateGUID();
    if (!db->checkIfRecordExists("name", "Systems", "folders")) {
        if (!db->createFolder("Systems", Globals::project->getProjectGuid(), fguid, false)) return;
    }

    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    QJsonObject props;
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::ParticleSystem),
        fguid,
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toJson(),
        QByteArray(),
        QByteArray()
    );

    // if we reached this far, the project dir has already been created
    // we can copy some default assets to each project here
    QFile::copy(IrisUtils::getAbsoluteAssetPath("app/images/default_particle.jpg"),
        QDir(Globals::project->getProjectFolder()).filePath("Glowing Particle.jpg"));

    auto thumb = ThumbnailManager::createThumbnail(
        IrisUtils::getAbsoluteAssetPath("app/images/default_particle.jpg"), 72, 72);

    QByteArray thumbnailBytes;
    QBuffer buffer(&thumbnailBytes);
    buffer.open(QIODevice::WriteOnly);
    QPixmap::fromImage(*thumb->thumb).save(&buffer, "PNG");

    const QString tileGuid = GUIDManager::generateGUID();
    const QString assetGuid = db->createAssetEntry(tileGuid,
        "Glowing Particle.jpg",
        static_cast<int>(ModelTypes::Texture),
        Globals::project->getProjectGuid(),
        QString(),
        QString(),
        thumbnailBytes);

    db->createDependency(
        static_cast<int>(ModelTypes::ParticleSystem),
        static_cast<int>(ModelTypes::Texture),
        nodeGuid, assetGuid,
        Globals::project->getProjectGuid()
    );

    {
        QString texPath = QDir(Globals::project->getProjectFolder()).filePath("Glowing Particle.jpg");
        node->setTexture(iris::Texture2D::load(texPath));
    }

    auto assetTexture = new AssetTexture;
    assetTexture->fileName = "Glowing Particle.jpg";
    assetTexture->assetGuid = assetGuid;
    assetTexture->path = QDir(Globals::project->getProjectFolder()).filePath("Glowing Particle.jpg");
    AssetManager::addAsset(assetTexture);

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
        mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));

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

void MainWindow::addPrimitiveObject(const QString &text)
{
    if (text == "Plane")    addPlane();
    if (text == "Cone")     addCone();
    if (text == "Cube")     addCube();
    if (text == "Cylinder") addCylinder();
    if (text == "Sphere")   addSphere();
    if (text == "Torus")    addTorus();
    if (text == "Capsule")  addCapsule();
    if (text == "Gear")     addGear();
    if (text == "Pyramid")  addPyramid();
    if (text == "Teapot")   addTeapot();
    if (text == "Sponge")   addSponge();
    if (text == "Steps")    addSteps();
}



// void MainWindow::addMaterialMesh(const QString &path, bool ignore, QVector3D position, const QString &guid, const QString &assetName)
// {
//     auto document = QJsonDocument::fromJson(db->fetchAssetData(guid)).object();

//     auto reader = new SceneReader;
//     reader->setBaseDirectory(Globals::project->getProjectFolder());
//     this->sceneView->makeCurrent();
//     iris::SceneNodePtr node = reader->readSceneNode(document);
//     this->sceneView->doneCurrent();
//     delete reader;

// 	// rename animation sources to relative paths
// 	QString meshGuid = db->fetchObjectMesh(guid, static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh));
// 	auto relPath = QDir(Globals::project->folderPath).relativeFilePath(db->fetchAsset(meshGuid).name);
// 	for (auto anim : node->getAnimations()) if (!!anim->skeletalAnimation) anim->skeletalAnimation->source = relPath;

// 	addNodeToScene(node, ignore);
// }


// 替换或合并此函数到 src/mainwindow.cpp，替换现有的 MainWindow::addMaterialMesh 实现。
// 依赖：NewJsonAdapter.h, SceneReader, Globals, IrisUtils, Constants 等（按项目实际包含头文件调整）
#include "io/NewJsonAdapter.h"
#include "io/scenereader.h"
#include "constants.h"
#include "irisgl/src/core/irisutils.h"

static bool propMatchesAny(const iris::Property* prop, const QStringList &tokens) {
    if (!prop) return false;
    QString name = prop->name.toLower();
    QString uniform = prop->uniform.toLower();
    for (const QString &t : tokens) {
        QString tl = t.toLower();
        if (name.contains(tl) || uniform.contains(tl)) return true;
    }
    return false;
}

iris::CustomMaterialPtr createAndBindMaterialSafe(
    QSharedPointer<iris::MeshNode> meshNode,
    const QJsonObject &nodeObj,
    const QJsonObject &materialJson,
    const QMap<QString, QString> &texturesMap,
    Database* db,
    bool useAlternativeLocation,
    const QString &assetDirectory)
{
    Q_UNUSED(meshNode);

    // 1) Guard: only parse if explicit material id or material_override_id exists.
    QString matId = materialJson.value("id").toString().trimmed();
    QString overrideId = nodeObj.value("material_override_id").toString().trimmed();
    if (matId.isEmpty() && overrideId.isEmpty()) {
        qDebug() << "createAndBindMaterialSafe: no explicit material id or override for node"
                 << nodeObj.value("id").toString() << "- attaching default material";

        iris::CustomMaterialPtr defaultMat = iris::CustomMaterial::create();
        defaultMat->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));
        if (meshNode && !!defaultMat) {
            meshNode->setMaterial(defaultMat);
        }
        return defaultMat;
    }

    // 2) Copy material JSON and try to replace texture GUIDs with resolved paths
    QJsonObject matObj = materialJson;

    for (const QString &key : matObj.keys()) {
        if (!key.endsWith("_texture", Qt::CaseInsensitive)) continue;

        QJsonValue v = matObj.value(key);
        if (!v.isString()) continue;
        QString texVal = v.toString().trimmed();
        if (texVal.isEmpty()) continue;

        // If value already looks like a path and exists, keep it
        if (QFileInfo(texVal).exists()) {
            // already a usable path
            continue;
        }

        // texturesMap priority
        if (texturesMap.contains(texVal)) {
            QString p = texturesMap.value(texVal);
            if (!p.isEmpty() && QFileInfo(p).exists()) {
                matObj[key] = p;
                qDebug() << "createAndBindMaterialSafe: replaced GUID" << texVal << "->" << p << "for key" << key;
                continue;
            } else {
                qDebug() << "createAndBindMaterialSafe: texturesMap path missing for GUID" << texVal << "->" << p;
            }
        }

        // fallback: try DB lookup (like MaterialReader does)
        if (db != nullptr) {
            try {
                auto asset = db->fetchAsset(texVal);
                QString assetName = asset.name;
                if (!assetName.isEmpty() && Globals::project != nullptr) {
                    QString candidate;
                    candidate = IrisUtils::join(Globals::project->getProjectFolder(), assetName);
                    if (QFileInfo(candidate).exists()) {
                        matObj[key] = candidate;
                        qDebug() << "createAndBindMaterialSafe: db resolved GUID" << texVal << "->" << candidate;
                        continue;
                    }
                    // try global asset folder fallback
                    QString globalCandidate = IrisUtils::join(IrisUtils::getAbsoluteAssetPath(Constants::ASSET_FOLDER), assetName);
                    if (QFileInfo(globalCandidate).exists()) {
                        matObj[key] = globalCandidate;
                        qDebug() << "createAndBindMaterialSafe: db resolved GUID (global) " << texVal << "->" << globalCandidate;
                        continue;
                    }
                }
            } catch(...) {
                // ignore DB lookup errors
            }
        }

        // unable to resolve; let MaterialReader handle GUID (may convert to empty string).
        qDebug() << "createAndBindMaterialSafe: unable to resolve texture GUID" << texVal << "for key" << key;
    }

    // 2.5) QUICK FIX A: Force default shader to avoid broken/missing custom shaders.
    // This ensures MaterialReader compiles a known-good shader and prevents "must write to gl_Position" errors.
    QString defaultShaderPath = IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER);
    if (!defaultShaderPath.isEmpty()) {
        // Overwrite any custom shader references in the material JSON.
        // Field names may vary; set common ones so MaterialReader picks up the default shader.
        matObj.insert(QStringLiteral("vertex_shader"), defaultShaderPath);
        matObj.insert(QStringLiteral("fragment_shader"), defaultShaderPath);
        matObj.insert(QStringLiteral("shader"), defaultShaderPath);
        // Also provide inline source keys if consumer uses them (best-effort; empty if file read fails)
        QFile f(defaultShaderPath);
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray shaderSrc = f.readAll();
            f.close();
            // Some readers accept inline source fields; this is a defensive attempt.
            matObj.insert(QStringLiteral("vertexShaderSource"), QString::fromUtf8(shaderSrc));
            matObj.insert(QStringLiteral("fragmentShaderSource"), QString::fromUtf8(shaderSrc));
        } else {
            qDebug() << "createAndBindMaterialSafe: could not open default shader file to inline source:" << defaultShaderPath;
        }
        qDebug() << "createAndBindMaterialSafe: forcing default shader for material parse to avoid broken custom shaders";
    }

    // 3) Create material via MaterialReader
    MaterialReader reader;
    if (useAlternativeLocation) reader.setSource(TextureSource::GlobalAssets, assetDirectory);

    iris::CustomMaterialPtr material;
    try {
        material = reader.parseMaterial(matObj, db, true);
    } catch (...) {
        qWarning() << "createAndBindMaterialSafe: exception while parsing material for node"
                   << nodeObj.value("id").toString();
        material = iris::CustomMaterial::create();
    }

    if (!material) {
        qWarning() << "createAndBindMaterialSafe: parseMaterial returned null, using default";
        material = iris::CustomMaterial::create();
    }

    // 4) Attach material to meshNode (if present)
    if (meshNode && !!material) {
        meshNode->setMaterial(material);
    }

    // 5) If adapter provided __resolved_textures metadata on nodeObj, apply them first.
    if (nodeObj.contains(QStringLiteral("__resolved_textures")) && nodeObj.value(QStringLiteral("__resolved_textures")).isObject()) {
        QJsonObject resolved = nodeObj.value(QStringLiteral("__resolved_textures")).toObject();
        if (!!material) {
            auto applyResolved = [&](const QString &chan, const QString &propName) {
                if (!resolved.contains(chan)) return;
                QString p = resolved.value(chan).toString().trimmed();
                if (p.isEmpty()) return;
                QString norm = QDir::cleanPath(QDir::fromNativeSeparators(p));
                if (!QFileInfo::exists(norm)) {
                    qDebug() << "createAndBindMaterialSafe: resolved texture file missing:" << norm << "for channel" << chan;
                    return;
                }
                // setValue will trigger setTextureWithUniform for texture properties
                material->setValue(propName, norm);
                qDebug() << "createAndBindMaterialSafe: applied resolved texture for channel" << chan << "->" << propName << ":" << norm;
            };

            applyResolved("base_color", "diffuseTexture");
            applyResolved("normal", "normalTexture");
            applyResolved("metallic", "specularTexture");
            if (resolved.contains("roughness") && !resolved.contains("metallic")) applyResolved("roughness", "specularTexture");
            applyResolved("emissive", "emissionTexture");
        }
    }

    // 6) Ensure texture properties are bound (fallbacks & safety)
    if (!!material) {
        for (auto prop : material->properties) {
            if (!prop) continue;
            if (prop->type != iris::PropertyType::Texture) continue;

            QString curVal = prop->getValue().toString().trimmed();
            if (!curVal.isEmpty()) {
                // If path exists, bind it. If looks like GUID, allow material to resolve it.
                if (QFileInfo(curVal).exists()) {
                    material->setTextureWithUniform(prop->uniform, curVal);
                    qDebug() << "createAndBindMaterialSafe: bound existing texture value for prop" << prop->name << "->" << curVal;
                } else {
                    bool looksLikeGuid = QRegularExpression("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$").match(curVal).hasMatch();
                    if (looksLikeGuid) {
                        material->setTextureWithUniform(prop->uniform, curVal);
                        qDebug() << "createAndBindMaterialSafe: attempting to bind GUID value for prop" << prop->name << "->" << curVal;
                    } else {
                        qWarning() << "createAndBindMaterialSafe: texture path does not exist for prop" << prop->name << ":" << curVal;
                    }
                }
                continue;
            }

            // Try common candidate keys in matObj
            QStringList candidates = { QString("base_color_texture"), QString("diffuse_texture"), QString("diffuseTexture"),
                                      QString("diffuse"), QString(prop->name) };

            bool bound = false;
            for (const QString &cand : candidates) {
                if (!matObj.contains(cand)) continue;
                QString candVal = matObj.value(cand).toString().trimmed();
                if (candVal.isEmpty()) continue;

                // If value is GUID and texturesMap has mapping
                if (texturesMap.contains(candVal) && QFileInfo(texturesMap.value(candVal)).exists()) {
                    material->setTextureWithUniform(prop->uniform, texturesMap.value(candVal));
                    qDebug() << "createAndBindMaterialSafe: bound texture from texturesMap for prop" << prop->name << "->" << texturesMap.value(candVal);
                    bound = true;
                    break;
                }

                // If candidate looks like a path that exists
                if (QFileInfo(candVal).exists()) {
                    material->setTextureWithUniform(prop->uniform, candVal);
                    qDebug() << "createAndBindMaterialSafe: bound texture path for prop" << prop->name << "->" << candVal;
                    bound = true;
                    break;
                }

                // DB fallback
                if (db != nullptr) {
                    try {
                        auto asset = db->fetchAsset(candVal);
                        if (!asset.name.isEmpty() && Globals::project != nullptr) {
                            QString p = IrisUtils::join(Globals::project->getProjectFolder(), asset.name);
                            if (QFileInfo(p).exists()) {
                                material->setTextureWithUniform(prop->uniform, p);
                                qDebug() << "createAndBindMaterialSafe: bound texture via DB for prop" << prop->name << "->" << p;
                                bound = true;
                                break;
                            }
                        }
                    } catch(...) {}
                }
            } // end candidates loop

            if (!bound) {
                qDebug() << "createAndBindMaterialSafe: no texture bound for prop" << prop->name << "on material" << material->getName();
            }
        }
    }

    return material;
}

void MainWindow::addMaterialMesh(const QString &path, bool ignore, QVector3D position, const QString &guid, const QString &assetName)
{
    // 1) 读取 asset JSON（可能为空）
    QByteArray raw;
    raw.clear();
    try {
        raw = db->fetchAssetData(guid);
    } catch(...) {
        qDebug() << "addMaterialMesh: db->fetchAssetData threw for guid" << guid;
    }

    QJsonObject rootObj;
    if (!raw.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (!doc.isNull() && doc.isObject()) rootObj = doc.object();
        else qDebug() << "addMaterialMesh: fetched assetData is not top-level JSON for guid" << guid;
    } else {
        qDebug() << "addMaterialMesh: no raw assetData returned for guid" << guid;
    }

    // 2) resolver: asset GUID -> filesystem path（保持现有 heuristics）
    NewJsonAdapter::AssetResolverFunc resolver = [this](const QString &assetGuid) -> QString {
        if (assetGuid.isEmpty()) return QString();

        auto normalize = [](const QString &p)->QString {
            if (p.isEmpty()) return QString();
            return QDir::cleanPath(QDir::fromNativeSeparators(p));
        };

        try {
            auto rec = db->fetchAsset(assetGuid);
            if (!rec.asset.isEmpty()) {
                QJsonDocument d = QJsonDocument::fromJson(rec.asset);
                if (!d.isNull() && d.isObject()) {
                    QJsonObject r = d.object();
                    if (r.contains("source_file") && r["source_file"].isString()) {
                        QString sf = r["source_file"].toString();
                        if (!sf.isEmpty()) {
                            QString p = normalize(sf);
                            if (QFileInfo(p).exists()) {
                                qDebug() << "resolver: resolved via rec.asset.source_file:" << p;
                                return p;
                            }
                        }
                    }
                    if (r.contains("textures") && r["textures"].isArray()) {
                        for (const QJsonValue &tv : r["textures"].toArray()) {
                            if (!tv.isObject()) continue;
                            QString tp = tv.toObject().value("path").toString();
                            if (tp.isEmpty()) continue;
                            QString p = normalize(tp);
                            if (QFileInfo(p).exists()) {
                                qDebug() << "resolver: resolved via rec.asset.textures[].path:" << p;
                                return p;
                            }
                        }
                    }
                    const QStringList otherKeys = { "model_file", "modelPath", "model", "file", "source" };
                    for (const QString &k : otherKeys) {
                        if (r.contains(k) && r[k].isString()) {
                            QString p = normalize(r[k].toString());
                            if (!p.isEmpty() && QFileInfo(p).exists()) {
                                qDebug() << "resolver: resolved via rec.asset[" << k << "]:" << p;
                                return p;
                            }
                        }
                    }
                }
            }

            QString name = rec.name;
            if (!name.isEmpty()) {
                if (Globals::project) {
                    QString p = normalize(QDir(Globals::project->folderPath).filePath(name));
                    if (QFileInfo(p).exists()) {
                        qDebug() << "resolver: resolved via project folder:" << p;
                        return p;
                    }
                }
                QString assetsRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                QString candidate = normalize(IrisUtils::join(assetsRoot, Constants::ASSET_FOLDER, assetGuid, name));
                if (QFileInfo(candidate).exists()) {
                    qDebug() << "resolver: resolved via AppData asset folder (by name):" << candidate;
                    return candidate;
                }
                if (QFileInfo(name).isAbsolute() && QFileInfo(name).exists()) {
                    qDebug() << "resolver: resolved via absolute name:" << normalize(name);
                    return normalize(name);
                }
            }
        } catch(...) {
            // ignore and continue
        }

        // folder heuristics
        QString assetsRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QString assetFolder = IrisUtils::join(assetsRoot, Constants::ASSET_FOLDER, assetGuid);
        QDir d(assetFolder);
        if (d.exists()) {
            const QStringList modelExts = { "gltf","glb","fbx","obj","stl","ply","vtp" };
            for (const QString &ext : modelExts) {
                QStringList files = d.entryList(QStringList() << ("*." + ext), QDir::Files | QDir::NoDotAndDotDot);
                if (!files.isEmpty()) {
                    QString p = normalize(d.filePath(files.first()));
                    qDebug() << "resolver: found model in asset folder:" << p;
                    return p;
                }
            }
            const QStringList imageExts = { "png","jpg","jpeg","tga","bmp","dds","webp" };
            for (const QString &ext : imageExts) {
                QStringList files = d.entryList(QStringList() << ("*." + ext), QDir::Files | QDir::NoDotAndDotDot);
                if (!files.isEmpty()) {
                    QString p = normalize(d.filePath(files.first()));
                    qDebug() << "resolver: found image in asset folder root:" << p;
                    return p;
                }
            }
            const QStringList subdirs = { "data", "models", "meshes", "geometry", "Textures", "textures", "images", "Images" };
            for (const QString &sub : subdirs) {
                QString subdirPath = IrisUtils::join(assetFolder, sub);
                QDir sd(subdirPath);
                if (!sd.exists()) continue;
                QStringList exts = modelExts + imageExts;
                for (const QString &ext : exts) {
                    QStringList files = sd.entryList(QStringList() << ("*." + ext), QDir::Files | QDir::NoDotAndDotDot);
                    if (!files.isEmpty()) {
                        QString found = normalize(sd.filePath(files.first()));
                        qDebug() << "resolver: found file in subfolder:" << found;
                        return found;
                    }
                }
            }
        }

        qDebug() << "resolver: could not resolve assetGuid to path:" << assetGuid;
        return QString();
    };

    // 3) 用 NewJsonAdapter 解析（若可处理）
    NewJsonAdapter::AdapterResult result;
    bool usedAdapter = false;
    if (!rootObj.isEmpty() && NewJsonAdapter::canHandle(rootObj)) {
        qDebug() << "addMaterialMesh: using NewJsonAdapter for guid" << guid;
        result = NewJsonAdapter::convertJsonToScene(rootObj, resolver);
        usedAdapter = true;
    }

    // 4) 如果 adapter 提供了 scene，按 metadata 创建可渲染节点（优先生成 MeshNode）
    if (usedAdapter && result.scene) {
        qDebug() << "addMaterialMesh: creating nodes from adapter.scene (metadata-driven)";

        this->sceneView->makeCurrent();

        SceneReader meshLoader;
        meshLoader.setBaseDirectory(Globals::project ? Globals::project->getProjectFolder() : QString());

        // 构建 adapter node id -> manifest guid 的反向映射（若可用）
        QHash<qint64, QString> adapterIdToManifestGuid;
        for (auto it = result.guidToNodeId.constBegin(); it != result.guidToNodeId.constEnd(); ++it) {
            adapterIdToManifestGuid.insert(it.value(), it.key());
        }

        // Build texturesMap from model JSON (rootObj) for use by helper
        QMap<QString, QString> texturesMap;
        if (!rootObj.isEmpty() && rootObj.contains("textures") && rootObj["textures"].isArray()) {
            for (const QJsonValue &tv : rootObj["textures"].toArray()) {
                if (!tv.isObject()) continue;
                QJsonObject to = tv.toObject();
                QString tid = to.value("id").toString();
                QString p = to.value("path").toString();
                if (!tid.isEmpty() && !p.isEmpty()) texturesMap.insert(tid, QDir::cleanPath(QDir::fromNativeSeparators(p)));
            }
        }

        // helper to find material JSON by id in rootObj
        auto findMaterialById = [&](const QString &mid)->QJsonObject {
            if (mid.isEmpty()) return QJsonObject();
            if (!rootObj.contains("materials") || !rootObj["materials"].isArray()) return QJsonObject();
            for (const QJsonValue &mv : rootObj["materials"].toArray()) {
                if (!mv.isObject()) continue;
                QJsonObject mo = mv.toObject();
                if (mo.value("id").toString() == mid) return mo;
            }
            return QJsonObject();
        };

        // Helper: resolve model path for an adapter node (tries metadata, resolver(manifestGuid), project-relative names)
        auto resolveModelPathForAdapterNode = [&](qint64 adapterNodeId, iris::SceneNodePtr src)->QString {
            QString modelPath;

            if (result.nodeMetadata.contains(adapterNodeId)) {
                QJsonObject meta = result.nodeMetadata.value(adapterNodeId);
                const QStringList keys = { "source_file", "mesh", "meshPath", "model", "model_file", "source", "file", "path" };
                for (const QString &k : keys) {
                    if (meta.contains(k) && meta[k].isString()) {
                        QString v = meta[k].toString();
                        if (v.isEmpty()) continue;
                        if (v.length() == 36 && v.contains('-')) {
                            QString r = resolver(v);
                            if (!r.isEmpty()) return r;
                        }
                        QString abs = QDir(Globals::project->getProjectFolder()).filePath(v);
                        if (QFileInfo(abs).exists()) return QDir::cleanPath(abs);
                        if (QFileInfo(v).exists()) return QDir::cleanPath(v);
                        modelPath = v;
                    }
                }
                if (meta.contains("original_index") && meta.contains("source_file") && meta["source_file"].isString()) {
                    QString sf = meta["source_file"].toString();
                    QString abs = QDir(Globals::project->getProjectFolder()).filePath(sf);
                    if (QFileInfo(abs).exists()) return QDir::cleanPath(abs);
                    if (QFileInfo(sf).exists()) return QDir::cleanPath(sf);
                }
            }

            QString manifestGuid;
            if (adapterIdToManifestGuid.contains(adapterNodeId)) manifestGuid = adapterIdToManifestGuid.value(adapterNodeId);
            if (!manifestGuid.isEmpty()) {
                QString r = resolver(manifestGuid);
                if (!r.isEmpty()) return r;
                try {
                    auto rec = db->fetchAsset(manifestGuid);
                    if (!rec.name.isEmpty()) {
                        QString cand = QDir(Globals::project->getProjectFolder()).filePath(rec.name);
                        if (QFileInfo(cand).exists()) return QDir::cleanPath(cand);
                    }
                } catch(...) {}
            }

            if (!!src) {
                QString nm = src->getName();
                if (!nm.isEmpty()) {
                    QString cand = QDir(Globals::project->getProjectFolder()).filePath(nm);
                    if (QFileInfo(cand).exists()) return QDir::cleanPath(cand);
                    QString cand2 = QDir(Globals::project->getProjectFolder()).filePath("Models/" + nm);
                    if (QFileInfo(cand2).exists()) return QDir::cleanPath(cand2);
                }
            }

            if (!modelPath.isEmpty()) {
                if (QFileInfo(modelPath).isAbsolute() && QFileInfo(modelPath).exists()) return QDir::cleanPath(modelPath);
                QString cand = QDir(Globals::project->getProjectFolder()).filePath(modelPath);
                if (QFileInfo(cand).exists()) return QDir::cleanPath(cand);
            }

            return QString();
        };

        // We'll keep track of manifest GUIDs we've assigned to created nodes (avoid duplicates)
        QSet<QString> assignedManifestGuids;

        // --- Recursive traversal: handle submeshes by inheriting parent modelPath and index ---
        QList<iris::SceneNodePtr> added;
        std::function<void(iris::SceneNodePtr, const QString&, int, const QString&)> processAdapterNode;
        processAdapterNode = [&](iris::SceneNodePtr child, const QString &parentModelPath, int parentMeshIndex, const QString &parentManifestGuid) {
            if (!child) return;

            qint64 adapterNodeId = child->getNodeId();
            qDebug() << "DEBUG: processing adapter nodeId=" << adapterNodeId << "name=" << child->getName()
                     << "nodeMetadata contains?" << result.nodeMetadata.contains(adapterNodeId);

            QString manifestGuidForAdapter;
            if (adapterIdToManifestGuid.contains(adapterNodeId)) manifestGuidForAdapter = adapterIdToManifestGuid.value(adapterNodeId);
            QString effectiveManifestGuid = !manifestGuidForAdapter.isEmpty() ? manifestGuidForAdapter : parentManifestGuid;

            // Resolve modelPath for this adapter node; if empty, inherit parent's modelPath
            QString modelPath = resolveModelPathForAdapterNode(adapterNodeId, child);
            if (modelPath.isEmpty()) modelPath = parentModelPath;

            // Determine meshIndex from metadata or inherit
            int meshIndex = -1;
            if (result.nodeMetadata.contains(adapterNodeId)) {
                QJsonObject meta = result.nodeMetadata.value(adapterNodeId);
                const QStringList idxKeys = { "original_index", "originalIndex", "index", "mesh_index", "primitive_index", "submesh" };
                for (const QString &k : idxKeys) {
                    if (meta.contains(k)) {
                        if (meta[k].isDouble()) { meshIndex = meta[k].toInt(); break; }
                        if (meta[k].isString()) {
                            bool ok = false;
                            int v = meta[k].toString().toInt(&ok);
                            if (ok) { meshIndex = v; break; }
                        }
                    }
                }
            }
            if (meshIndex < 0) meshIndex = (parentMeshIndex >= 0 ? parentMeshIndex : 0);

            iris::SceneNodePtr created;

            if (!modelPath.isEmpty()) {
                // Create MeshNode for this adapter node, using modelPath and meshIndex
                auto mn = iris::MeshNode::create();
                mn->setName(child->getName());
                if (!effectiveManifestGuid.isEmpty()) mn->setGUID(effectiveManifestGuid);
                else mn->setGUID(GUIDManager::generateGUID());

                iris::MeshPtr meshPtr = meshLoader.getMesh(modelPath, meshIndex);
                if (!!meshPtr) {
                    mn->setMesh(meshPtr);
                    mn->meshPath = modelPath;
                    mn->meshIndex = meshIndex;
                    qDebug() << "addMaterialMesh: loaded MeshPtr for modelPath" << modelPath << " index=" << meshIndex;
                } else {
                    mn->setMesh(modelPath);
                    mn->meshPath = modelPath;
                    mn->meshIndex = meshIndex;
                    qDebug() << "addMaterialMesh: setMesh with path for modelPath" << modelPath << " index=" << meshIndex;
                }

                mn->setLocalPos(child->getLocalPos());
                mn->setLocalRot(child->getLocalRot());
                mn->setLocalScale(child->getLocalScale().isNull() ? QVector3D(1,1,1) : child->getLocalScale());
                mn->setVisible(true);

                created = mn.staticCast<iris::SceneNode>();

                // Material binding: prefer adapter node metadata for this adapterNodeId
                QJsonObject adapterMeta;
                if (result.nodeMetadata.contains(adapterNodeId)) adapterMeta = result.nodeMetadata.value(adapterNodeId);

                QJsonObject matJson;
                QString adapterMatId = adapterMeta.value("material_override_id").toString().trimmed();
                if (!adapterMatId.isEmpty()) {
                    matJson = findMaterialById(adapterMatId);
                } else {
                    QString meshId = adapterMeta.value("mesh_id").toString().trimmed();
                    if (!meshId.isEmpty() && rootObj.contains("meshes") && rootObj["meshes"].isArray()) {
                        for (const QJsonValue &mv : rootObj["meshes"].toArray()) {
                            if (!mv.isObject()) continue;
                            QJsonObject mo = mv.toObject();
                            if (mo.value("id").toString() == meshId) {
                                QString mid = mo.value("material_id").toString();
                                if (!mid.isEmpty()) { matJson = findMaterialById(mid); break; }
                            }
                        }
                    }
                }

                // create and bind material
                createAndBindMaterialSafe(created.staticCast<iris::MeshNode>(),
                                          adapterMeta,
                                          matJson,
                                          texturesMap,
                                          db,
                                          /*useAlternativeLocation=*/ false,
                                          /*assetDirectory=*/ QString());
            } else {
                // no modelPath -> generic node
                created = iris::SceneNode::create();
                created->setName(child->getName());
                QString guidToSet = effectiveManifestGuid.isEmpty() ? (child->getGUID().isEmpty() ? GUIDManager::generateGUID() : child->getGUID()) : effectiveManifestGuid;
                created->setGUID(guidToSet);
                created->setLocalPos(child->getLocalPos());
                created->setLocalRot(child->getLocalRot());
                created->setLocalScale(child->getLocalScale());
                created->setVisible(child->isVisible());
            }

            if (!!created) {
                addNodeToScene(created, true);
                added.append(created);
            }

            // Recurse children with inherited modelPath/meshIndex/manifestGuid
            for (auto &grandChild : child->children) {
                processAdapterNode(grandChild, modelPath, meshIndex, effectiveManifestGuid);
            }
        };

        // Start recursion from each root-level child
        for (auto &rootChild : result.scene->rootNode->children) {
            processAdapterNode(rootChild, QString(), -1, QString());
        }

        // Diagnostics
        qDebug() << "addMaterialMesh: added" << added.size() << "nodes from adapter.scene. Dumping info:";
        for (auto &n : added) {
            qDebug() << " ADDED node name=" << n->getName() << " guid=" << n->getGUID() << " type=" << static_cast<int>(n->sceneNodeType)
            << " pos=" << n->getLocalPos() << " scale=" << n->getLocalScale() << " visible=" << n->isVisible();
            if (n->sceneNodeType == iris::SceneNodeType::Mesh) {
                auto mn = n.staticCast<iris::MeshNode>();
                qDebug() << "   meshPath=" << mn->meshPath << " meshIndex=" << mn->meshIndex;
                iris::MeshPtr meshPtr = meshLoader.getMesh(mn->meshPath, mn->meshIndex);
                qDebug() << "   meshLoader.getMesh valid?" << (!!meshPtr);
                auto mat = mn->getMaterial();
                qDebug() << "   material valid?" << (!!mat);
                if (!!mat) qDebug() << "   material textures keys:" << mat->textures.keys();
            }
        }

        this->sceneView->doneCurrent();

        if (!added.isEmpty()) {
            sceneHierarchyWidget->repopulateTree();
            sceneNodeSelected(added.first());
        }

        return;
    }

    // 5) Fallback: legacy SceneReader whole-document handling
    SceneReader reader;
    reader.setBaseDirectory(Globals::project ? Globals::project->getProjectFolder() : QString());
    this->sceneView->makeCurrent();
    iris::SceneNodePtr node = nullptr;
    if (!rootObj.isEmpty()) {
        node = reader.readSceneNode(rootObj);
    }
    this->sceneView->doneCurrent();
    if (!node) {
        qWarning() << "addMaterialMesh: SceneReader returned null for guid" << guid;
        return;
    }

    // rename animation sources to relative paths (preserve existing behavior)
    QString meshGuidFromDb;
    try {
        meshGuidFromDb = db->fetchObjectMesh(guid, static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh));
    } catch(...) { meshGuidFromDb.clear(); }

    if (!meshGuidFromDb.isEmpty()) {
        QString relPath;
        try { relPath = QDir(Globals::project->folderPath).relativeFilePath(db->fetchAsset(meshGuidFromDb).name); } catch(...) { relPath.clear(); }
        if (!relPath.isEmpty()) {
            for (auto anim : node->getAnimations()) if (!!anim->skeletalAnimation) anim->skeletalAnimation->source = relPath;
        }
    }

    node->setLocalPos(position);
    addNodeToScene(node, ignore);
}

// void MainWindow::addMaterialMesh(const QString &path, bool ignore, QVector3D position, const QString &guid, const QString &assetName)
// {
//     // Try to obtain raw asset JSON from DB first (may be empty or packed)
//     QByteArray raw = QByteArray();
//     try {
//         raw = db->fetchAssetData(guid);
//     } catch(...) {
//         qDebug() << "addMaterialMesh: db->fetchAssetData threw for guid" << guid;
//     }

//     QJsonObject rootObj;
//     if (!raw.isEmpty()) {
//         QJsonDocument doc = QJsonDocument::fromJson(raw);
//         if (!doc.isNull() && doc.isObject()) rootObj = doc.object();
//         else qDebug() << "addMaterialMesh: fetched assetData is not top-level JSON for guid" << guid;
//     } else {
//         qDebug() << "addMaterialMesh: no raw assetData returned for guid" << guid;
//     }

//     // Resolver: attempts to turn an asset GUID (mesh_id / material_id) into a filesystem path
//     // 替换你现有的 resolver 实现为下面这段（保持函数签名不变）
//     NewJsonAdapter::AssetResolverFunc resolver = [this](const QString &assetGuid) -> QString {
//         if (assetGuid.isEmpty()) return QString();

//         // Helper to normalize path
//         auto normalize = [](const QString &p)->QString {
//             if (p.isEmpty()) return QString();
//             return QDir::cleanPath(QDir::fromNativeSeparators(p));
//         };

//         // 1) Try DB record: prefer parsing the asset blob/JSON to extract real file paths
//         try {
//             auto rec = db->fetchAsset(assetGuid); // may throw if no record
//             // If rec.asset contains JSON, parse it to find "source_file" or textures[].path
//             if (!rec.asset.isEmpty()) {
//                 QJsonDocument doc = QJsonDocument::fromJson(rec.asset);
//                 if (!doc.isNull() && doc.isObject()) {
//                     QJsonObject root = doc.object();
//                     // Try source_file first (model file)
//                     if (root.contains("source_file") && root["source_file"].isString()) {
//                         QString sf = root["source_file"].toString();
//                         if (!sf.isEmpty()) {
//                             QString p = normalize(sf);
//                             if (QFileInfo(p).exists()) {
//                                 qDebug() << "resolver: resolved via rec.asset.source_file:" << p;
//                                 return p;
//                             }
//                             // maybe relative to AppData/ASSET_FOLDER/<guid> or project; we'll try below
//                         }
//                     }
//                     // Try textures array: prefer first matching texture path
//                     if (root.contains("textures") && root["textures"].isArray()) {
//                         for (const QJsonValue &tv : root["textures"].toArray()) {
//                             if (!tv.isObject()) continue;
//                             QString tp = tv.toObject().value("path").toString();
//                             if (tp.isEmpty()) continue;
//                             QString p = normalize(tp);
//                             if (QFileInfo(p).exists()) {
//                                 qDebug() << "resolver: resolved via rec.asset.textures[].path:" << p;
//                                 return p;
//                             }
//                             // try relative to AppData or project later
//                         }
//                     }
//                     // Some assets may encode their model path at root["model_file"] etc. Try common keys:
//                     const QStringList otherKeys = { "model_file", "modelPath", "model", "file" };
//                     for (const QString &k : otherKeys) {
//                         if (root.contains(k) && root[k].isString()) {
//                             QString p = normalize(root[k].toString());
//                             if (!p.isEmpty() && QFileInfo(p).exists()) {
//                                 qDebug() << "resolver: resolved via rec.asset[" << k << "]:" << p;
//                                 return p;
//                             }
//                         }
//                     }
//                 }
//             }

//             // If rec.name looks like a path or filename, try some likely locations using the name
//             QString name = rec.name;
//             if (!name.isEmpty()) {
//                 // Try project-local assets first
//                 if (Globals::project) {
//                     QString p = normalize(QDir(Globals::project->folderPath).filePath(name));
//                     if (QFileInfo(p).exists()) {
//                         qDebug() << "resolver: resolved via project folder:" << p;
//                         return p;
//                     }
//                 }
//                 // Try AppData asset folder layout: <AppData>/ASSET_FOLDER/<guid>/<name>
//                 QString assetsRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
//                 QString candidate = normalize(IrisUtils::join(assetsRoot, Constants::ASSET_FOLDER, assetGuid, name));
//                 if (QFileInfo(candidate).exists()) {
//                     qDebug() << "resolver: resolved via AppData asset folder (by name):" << candidate;
//                     return candidate;
//                 }
//                 // If name is an absolute path
//                 if (QFileInfo(name).isAbsolute() && QFileInfo(name).exists()) {
//                     qDebug() << "resolver: resolved via absolute name:" << normalize(name);
//                     return normalize(name);
//                 }
//                 // else we'll fall back to folder scanning below
//             }
//         } catch (...) {
//             // no DB record (or fetch failed) -- continue with folder heuristics
//         }

//         // 2) Folder heuristics: search <AppData>/ASSET_FOLDER/<guid> and common subfolders
//         QString assetsRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
//         QString assetFolder = IrisUtils::join(assetsRoot, Constants::ASSET_FOLDER, assetGuid);
//         QDir d(assetFolder);
//         if (d.exists()) {
//             // First try common model extensions
//             const QStringList modelExts = { "gltf","glb","fbx","obj","stl","ply","vtp" };
//             for (const QString &ext : modelExts) {
//                 QStringList files = d.entryList(QStringList() << ("*." + ext), QDir::Files | QDir::NoDotAndDotDot);
//                 if (!files.isEmpty()) {
//                     QString p = normalize(d.filePath(files.first()));
//                     qDebug() << "resolver: found model in asset folder:" << p;
//                     return p;
//                 }
//             }

//             // try image/texture extensions (textures are often stored under Textures subfolder)
//             const QStringList imageExts = { "png","jpg","jpeg","tga","bmp","dds","webp" };
//             // check root for images
//             for (const QString &ext : imageExts) {
//                 QStringList files = d.entryList(QStringList() << ("*." + ext), QDir::Files | QDir::NoDotAndDotDot);
//                 if (!files.isEmpty()) {
//                     QString p = normalize(d.filePath(files.first()));
//                     qDebug() << "resolver: found image in asset folder root:" << p;
//                     return p;
//                 }
//             }

//             // check likely subdirectories for models and textures
//             const QStringList subdirs = { "data", "models", "meshes", "geometry", "Textures", "textures", "images", "Images" };
//             for (const QString &sub : subdirs) {
//                 QString subdirPath = IrisUtils::join(assetFolder, sub);
//                 QDir sd(subdirPath);
//                 if (!sd.exists()) continue;
//                 // check both model and image exts
//                 QStringList exts = modelExts + imageExts;
//                 for (const QString &ext : exts) {
//                     QStringList files = sd.entryList(QStringList() << ("*." + ext), QDir::Files | QDir::NoDotAndDotDot);
//                     if (!files.isEmpty()) {
//                         QString found = normalize(sd.filePath(files.first()));
//                         qDebug() << "resolver: found file in subfolder:" << found;
//                         return found;
//                     }
//                 }
//             }
//         }

//         // 3) Last-ditch: try project folder / absolute name scanning (if fetchAsset returned a name earlier we'd have tried)
//         // (Nothing else to try)
//         qDebug() << "resolver: could not resolve assetGuid to path:" << assetGuid;
//         return QString();
//     };

//     iris::SceneNodePtr node; // final node to add to scene

//     // If JSON is new flat format, use adapter
//     if (!rootObj.isEmpty() && NewJsonAdapter::canHandle(rootObj)) {
//         qDebug() << "addMaterialMesh: using NewJsonAdapter for guid" << guid;
//         auto result = NewJsonAdapter::convertJsonToScene(rootObj, resolver);

//         // // Store metadata to AssetViewer so material/texture can be resolved later.
//         // if (assetViewer) {
//         //     assetViewer->nodeMetadataMap = result.nodeMetadata;
//         //     assetViewer->guidToNodeIdMap = result.guidToNodeId;
//         // }

//         // IMPORTANT: Do NOT directly reuse result.scene->rootNode (it already belongs to result.scene)
//         // Instead, iterate the original JSON top-level nodes and recreate them in the main scene
//         // using SceneReader so the created nodes belong to the active scene and avoid the "!this->scene" assert.

//         if (rootObj.contains("nodes") && rootObj["nodes"].isArray()) {
//             QJsonArray topNodes = rootObj["nodes"].toArray();

//             SceneReader reader;
//             reader.setBaseDirectory(Globals::project ? Globals::project->getProjectFolder() : QString());
//             this->sceneView->makeCurrent();

//             for (const QJsonValue &nv : topNodes) {
//                 if (!nv.isObject()) continue;
//                 QJsonObject nodeObj = nv.toObject();

//                 iris::SceneNodePtr created = reader.readSceneNode(nodeObj);
//                 if (created) {
//                     // attach original node JSON to AssetViewer metadata keyed by the newly created nodeId
//                     //if (assetViewer) assetViewer->nodeMetadataMap.insert(created->getNodeId(), nodeObj);

//                     // optionally set requested placement position (only for top-level placement)
//                     created->setLocalPos(position);

//                     // add into the live scene (reuse your existing helper)
//                     addNodeToScene(created, true);
//                 } else {
//                     qWarning() << "addMaterialMesh: SceneReader failed to create node from adapter JSON node for guid" << guid;
//                 }
//             }

//             this->sceneView->doneCurrent();
//             return;
//         } else {
//             // fallback: try to create a single root node via SceneReader
//             SceneReader reader;
//             reader.setBaseDirectory(Globals::project ? Globals::project->getProjectFolder() : QString());
//             this->sceneView->makeCurrent();
//             iris::SceneNodePtr created = reader.readSceneNode(rootObj);
//             this->sceneView->doneCurrent();
//             if (created) {
//                 //if (assetViewer) assetViewer->nodeMetadataMap.insert(created->getNodeId(), rootObj);
//                 created->setLocalPos(position);
//                 addNodeToScene(created, true);
//                 return;
//             } else {
//                 qWarning() << "addMaterialMesh: SceneReader fallback failed to create node for guid" << guid;
//             }
//         }
//     }

//     // Fallback: legacy SceneReader (if adapter didn't produce nodes or JSON not present)
//     if (rootObj.isEmpty() == false) {
//         qDebug() << "addMaterialMesh: falling back to SceneReader for guid" << guid;
//         SceneReader reader;
//         reader.setBaseDirectory(Globals::project ? Globals::project->getProjectFolder() : QString());
//         this->sceneView->makeCurrent();
//         node = reader.readSceneNode(rootObj);
//         this->sceneView->doneCurrent();
//         if (!node) {
//             qWarning() << "addMaterialMesh: SceneReader returned null for guid" << guid;
//             return;
//         }
//     }

//     // If still no node built, try to resolve a model file and create a minimal MeshNode
//     if (!node) {
//         QString modelPath = resolver(guid);
//         if (!modelPath.isEmpty()) {
//             node = iris::MeshNode::create();
//             node->setName(assetName.isEmpty() ? guid : assetName);
//             auto mn = node.staticCast<iris::MeshNode>();
//             mn->meshPath = modelPath;
//         } else {
//             qWarning() << "addMaterialMesh: cannot find model or JSON for guid" << guid;
//             return;
//         }
//     }

//     // rename animation sources to relative paths (preserve existing behavior)
//     QString meshGuid;
//     try {
//         meshGuid = db->fetchObjectMesh(guid, static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh));
//     } catch(...) { meshGuid.clear(); }

//     if (!meshGuid.isEmpty()) {
//         auto assetRec = db->fetchAsset(meshGuid);
//         QString name;
//         try { name = assetRec.name; } catch(...) { name.clear(); }
//         if (!name.isEmpty() && Globals::project) {
//             auto relPath = QDir(Globals::project->folderPath).relativeFilePath(name);
//             for (auto anim : node->getAnimations()) {
//                 if (!!anim->skeletalAnimation) anim->skeletalAnimation->source = relPath;
//             }
//         }
//     }

//     // Place node at requested position and add
//     node->setLocalPos(position);
//     addNodeToScene(node, ignore);
// }


void MainWindow::addAssetParticleSystem(bool ignore, QVector3D position, QString guid, QString assetName)
{
    this->sceneView->makeCurrent();

    QJsonObject pDefs;
    QVector<Asset*>::const_iterator iterator = AssetManager::getAssets().constBegin();
    while (iterator != AssetManager::getAssets().constEnd()) {
        if ((*iterator)->assetGuid == guid) pDefs = (*iterator)->getValue().toJsonObject();
        ++iterator;
    }

    //if (!node) return; 

    auto particleNode = iris::ParticleSystemNode::create();

    particleNode->setGUID(pDefs["guid"].toString());
    particleNode->setPPS((float) pDefs["particlesPerSecond"].toDouble(1.0f));
    particleNode->setParticleScale((float) pDefs["particleScale"].toDouble(1.0f));
    particleNode->setDissipation(pDefs["dissipate"].toBool());
    particleNode->setDissipationInv(pDefs["dissipateInv"].toBool());
    particleNode->setRandomRotation(pDefs["randomRotation"].toBool());
    particleNode->setGravity((float) pDefs["gravityComplement"].toDouble(1.0f));
    particleNode->setBlendMode(pDefs["blendMode"].toBool());
    particleNode->setLife((float) pDefs["lifeLength"].toDouble(1.0f));
    particleNode->setName(pDefs["name"].toString());
    particleNode->setSpeed((float) pDefs["speed"].toDouble(1.0f));
    {
        auto textureGuid = pDefs["texture"].toString();
        auto texPath = IrisUtils::join(
            Globals::project->getProjectFolder(),
            db->fetchAsset(textureGuid).name
        );
        particleNode->setTexture(iris::Texture2D::load(texPath));
    }
    particleNode->setVisible(pDefs["visible"].toBool(true));

    //return particleNode; 

    particleNode->setPickable(true);
    particleNode->setGUID(guid);
    particleNode->setName(assetName);
    particleNode->setLocalPos(position);

    addNodeToScene(particleNode, ignore);
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

    // // apply default material to mesh nodes if there is none
    // if (sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
    //     auto meshNode = sceneNode.staticCast<iris::MeshNode>();
    //     if (!meshNode->getMaterial()) {
    //         auto mat = iris::CustomMaterial::create();
    //         mat->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));
    //         meshNode->setMaterial(mat);
    //     }
    // }


    /* 在 addNodeToScene 中 meshNode 已经获取到 material 后调用 */
    if (sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
        auto meshNode = sceneNode.staticCast<iris::MeshNode>();

        auto matPtr = meshNode->getMaterial();
        if (matPtr) {
            // 如果是 CustomMaterial，可以安全地 staticCast
            auto customMat = matPtr.staticCast<iris::CustomMaterial>();
            if (customMat) {
                // 遍历 material 的 properties，把 texture 类型的值再驱动一次加载
                for (auto prop : customMat->properties) {
                    if (prop->type == iris::PropertyType::Texture) {
                        QString texVal = prop->getValue().toString();
                        if (!texVal.isEmpty()) {
                            // 触发材质内部的纹理加载/绑定逻辑（与 UI 改变时一样）
                            customMat->setTextureWithUniform(prop->uniform, texVal);
                            qDebug() << "Rebound texture for material prop" << prop->name << "value:" << texVal;
                        }
                    }
                }
            }
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
		// (nick) the material version gets updated during writing so
		// it's safe to assume we're working the v2 material structure
		SceneWriter::writeSceneNodeMaterial(
			materialDef,
			activeSceneNode.staticCast<iris::MeshNode>()->getMaterial().staticCast<iris::CustomMaterial>()
		);

		// materialDef will be mutated
		// it's only used to generate a file for the thumbnail
		auto materialDefOriginal = materialDef;

		// replace material guid with texture name
		auto materialValues = materialDef["values"].toObject();
        for (const auto &key : materialValues.keys()) {
			if (materialValues[key].isString())
            {
				auto texName = db->fetchAsset(materialValues[key].toString()).name;
				if (texName.isEmpty())
					continue;
				materialValues[key] = texName;
			}
		}
		materialDef["values"] = materialValues;

		QJsonDocument saveDoc;
		//saveDoc.setObject(materialDef);
		saveDoc.setObject(materialDefOriginal);

        QString fileName = IrisUtils::join(
            Globals::project->getProjectFolder(),
            IrisUtils::buildFileName(activeSceneNode.staticCast<iris::MeshNode>()->getName(), "material")
        );

        QFile file(fileName);
        file.open(QFile::WriteOnly);
        file.write(saveDoc.toJson());
        file.close();

		// WRITE TO DATABASE
		const QString assetGuid = GUIDManager::generateGUID();
        QByteArray binaryMat = QJsonDocument(materialDefOriginal).toJson();
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


		MaterialReader reader;
		auto material = reader.parseMaterial(materialDefOriginal, db);

		// Actually create the material and add shader as it's dependency
		db->createDependency(
			static_cast<int>(ModelTypes::Material),
			static_cast<int>(ModelTypes::Shader),
			assetGuid, material->getGuid(),
			Globals::project->getProjectGuid());

		// Add all its textures as dependencies too
		auto values = materialDefOriginal["values"].toObject();
		for (const auto &prop : material->properties) {
			if (prop->type == iris::PropertyType::Texture) {
				if (!values.value(prop->name).toString().isEmpty()) {
					db->createDependency(
						static_cast<int>(ModelTypes::Material),
						static_cast<int>(ModelTypes::Texture),
						assetGuid, values.value(prop->name).toString(),
						Globals::project->getProjectGuid()
					);
				}
			}
		}

		auto assetMat = new AssetMaterial;
		assetMat->assetGuid = assetGuid;
		assetMat->setValue(QVariant::fromValue(material));
		AssetManager::addAsset(assetMat);

		// it's assumed that the thumbnail rendering will
		// be finished by the time this is executed
		QFile::remove(fileName);
    }
    else {
        qDebug() << "Need an active scenenode!";
        return;
    }
}

void MainWindow::exportNode(const iris::SceneNodePtr &node, ModelTypes modelType)
{
    if (!node) return;

    // Dispatch a thumbnail request regardless of what happens,
    // This should finish in the time it takes to spawn a dialog and save
    // Since the object is already loaded in memory
    refreshThumbnail(node->getGUID());

    QDateTime currentDateTime = QDateTime::currentDateTimeUtc();

    // The export is titled the name of the node + the current date time in UTC
    auto filePath = QFileDialog::getSaveFileName(
        this,
        "Choose export path",
        QString("%1_%2").arg(node->getName(), QString::number(static_cast<time_t>(currentDateTime.toSecsSinceEpoch()))),
        "Supported Export Formats (*.jaf)"
    );

    if (filePath.isEmpty() || filePath.isNull()) return;

    // Construct a temporary dir to place all the files that will be packaged
    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid()) return;

    const QString writePath = temporaryDir.path();

    // Create a blob containing the necessary tables and rows that are needed to recreate the asset
    // Assets are exported AS IS with their guids, these are changed when being reimported 
    db->createBlobFromNode(node, QDir(writePath).filePath("asset.db"));

    QDir tempDir(writePath);
    tempDir.mkpath("assets");

    // The manifest contains a single string telling the asset type
    // This helps with some preliminary checks to avoid reading the db and encountering blobs etc
    QFile manifest(QDir(writePath).filePath(".manifest"));
    if (manifest.open(QIODevice::ReadWrite)) {
        QTextStream stream(&manifest);
        stream << Project::ModelTypesAsString[static_cast<int>(modelType)];
    }
    manifest.close();

    // Collect all assets that will be exported and copy these to the temporary directory
    QStringList assetGuids = AssetHelper::getChildGuids(node);

    for (const auto &guid : assetGuids) {
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
    }

    // Get all the files and directories in the temporary directory
    QDir workingProjectDirectory(writePath);
    QDirIterator projectDirIterator(
        writePath,
        QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs | QDir::Hidden,
        QDirIterator::Subdirectories
    );

    // Create a zipped archive containing
    // - A manifest (might be hidden when extracted on some platforms)
    // - A sqlite blob
    // - An assets folder containing textures, models, files etc
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
        // TODO - gray/disable delete button if a node isn't removable
        if (activeSceneNode->isRootNode() || !activeSceneNode->isRemovable()) return;
        if (activeSceneNode->isBuiltIn) db->deleteAsset(activeSceneNode->getGUID());

		if (activeSceneNode->sceneNodeType == iris::SceneNodeType::Viewer) {
			scene->getPhysicsEnvironment()->removeCharacterControllerFromWorld(activeSceneNode->getGUID());
		}

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

void MainWindow::updateCurrentSceneThumbnail()
{
    auto img = sceneView->takeScreenshot(Constants::TILE_SIZE * 2);
    QByteArray thumb;
    QBuffer buffer(&thumb);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");

    db->updateSceneThumbnail(Globals::project->getProjectGuid(), thumb);
    pmContainer->updateTile(Globals::project->getProjectGuid(), thumb);
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
    sceneNodePropertiesWidget->setSceneView(sceneView);
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
    SkyPresets *skyPresets = new SkyPresets;
    skyPresets->setMainWindow(this);
	skyPresets->setDatabase(db);

	connect(skyPresets, &SkyPresets::changeSceneCubemap,
			sceneNodePropertiesWidget, &SceneNodePropertiesWidget::acceptCubemapTexturesFromSkyPresets);

    assetModelPanel = new AssetModelPanel;
    assetModelPanel->setMainWindow(this);
    assetModelPanel->setDatabaseHandle(db);

    assetMaterialPanel = new AssetMaterialPanel;
    assetMaterialPanel->setMainWindow(this);
    assetMaterialPanel->setDatabaseHandle(db);

    presetsTabWidget = new QTabWidget;
    presetsTabWidget->setObjectName("PresetsTabWidget");
    presetsTabWidget->setMinimumWidth(396);
    presetsTabWidget->addTab(assetModelPanel, "Models");
    presetsTabWidget->addTab(assetMaterialPanel, "Materials");
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
    assetWidget->setMainWindow(this);
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

	viewPort->setStyleSheet(
		"QMenu{	background: rgba(26,26,26,.9); color: rgba(250,250, 250,.9);}"
		"QMenu::item{padding: 2px 5px 2px 20px;	}"
		"QMenu::item:hover{	background: rgba(40,128, 185,.9);}"
		"QMenu::item:selected{	background: rgba(40,128, 185,.9);}"
	//	"QMenu::indicator{ width : 13; height : 10; border-radius: 3px; background: rgba(53,53,53,.9);}"
		//"QMenu::indicator:checked{background: rgba(40,128, 185,.9);}"
	);
}

void MainWindow::setupViewPort()
{
	// ui->MenuBar->setVisible(false);

	worlds_menu = new QPushButton("Desktop");
	worlds_menu->setObjectName("worlds_menu");
	worlds_menu->setCursor(Qt::PointingHandCursor);
	player_menu = new QPushButton("Player");
	player_menu->setObjectName("player_menu");
	player_menu->setCursor(Qt::PointingHandCursor);
	editor_menu = new QPushButton("Editor");
	editor_menu->setObjectName("editor_menu");
	editor_menu->setCursor(Qt::PointingHandCursor);
	effect_menu = new QPushButton("Materials");
	effect_menu->setObjectName("effects_menu");
	effect_menu->setCursor(Qt::PointingHandCursor);
	assets_menu = new QPushButton("Assets");
	assets_menu->setObjectName("assets_menu");
	assets_menu->setCursor(Qt::PointingHandCursor);

	assets_panel = new QWidget;

	auto hl = new QHBoxLayout;
    hl->setContentsMargins(0,0,0,0);
	hl->setSpacing(12);
    hl->addWidget(worlds_menu);
    hl->addWidget(player_menu);
	hl->addWidget(editor_menu);
	hl->addWidget(effect_menu);
	hl->addWidget(assets_menu);

	assets_panel->setLayout(hl);

	jlogo = new QLabel;
    jlogo->setMinimumSize(QSize(244, 48));
    jlogo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    QString header_image_path;
#ifdef QT_DEBUG
    header_image_path = IrisUtils::getAbsoluteAssetPath("app/images/jahshakastudiodevheader.png");
#else
    header_image_path = IrisUtils::getAbsoluteAssetPath("app/images/jahshakastudioheader.svg");
#endif
    QString style("image: url(%1);");
    jlogo->setStyleSheet(style.arg(header_image_path));

	help = new QPushButton;
	help->setObjectName("helpButton");
    // for adapting Qt6.9.0
    help->setText(QChar(static_cast<ushort>(fa::questioncircle)));
    //help->setText(QChar(fa::questioncircle));
	help->setFont(fontIcons->font(28));
	help->setCursor(Qt::PointingHandCursor);

	help->setStyleSheet(
		"#helpButton { qproperty-icon: url(\"\");"
		"qproperty-iconSize: 48px 48px;"
		"background: transparent;"
		"color: rgba(255,255,255,.9);"
		"background-repeat: no-repeat; }"
		"#helpButton::hover {color: rgba(255, 255, 255, 1); }"
		
	);

    connect(help, &QPushButton::pressed, []() {
        QDesktopServices::openUrl(QUrl("https://www.jahshaka.com/learn"));
	});

	prefs = new QPushButton;
	prefs->setObjectName("prefsButton");

    //prefs->setText(QChar(fa::cog));
    // for adapting Qt6.9.0
    prefs->setText(QChar(static_cast<ushort>(fa::cog)));
	prefs->setFont(fontIcons->font(28));
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
	connect(effect_menu, &QPushButton::pressed, [this]() { switchSpace(WindowSpaces::EFFECT); });

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
    screenShotBtn->setIcon(QIcon(":/icons/icons8-camera-48.png"));
	screenShotBtn->setIconSize(QSize(16,17));

    wireFramesButton = new QToolButton;
    wireFramesButton->setStyleSheet(
        "padding: 0 8px 0 0; margin: 0"
    );
    wireFramesMenu = new QMenu;
	wireFramesMenu->setStyleSheet("QMenu{	background: rgba(26,26,26,.9); color: rgba(250,250, 250,.9);}"
		"QMenu::item{padding: 2px 5px 2px 20px;	}"
		"QMenu::item:hover{	background: rgba(40,128, 185,.9);}"
		"QMenu::item:selected{	background: rgba(40,128, 185,.9);}");

    wireCheckAction = new QAction(QIcon(), "Light Bounds");
    wireCheckAction->setCheckable(true);
    connect(wireCheckAction, SIGNAL(toggled(bool)), this, SLOT(toggleLightWires(bool)));
    wireFramesMenu->addAction(wireCheckAction);

    physicsCheckAction = new QAction(QIcon(), "Physics Debug Overlay");
    physicsCheckAction->setCheckable(true);
    connect(physicsCheckAction, SIGNAL(toggled(bool)), this, SLOT(toggleDebugDrawer(bool)));
    wireFramesMenu->addAction(physicsCheckAction);

    wireFramesButton->setMenu(wireFramesMenu);
    wireFramesButton->setText("View Options ");
    wireFramesButton->setPopupMode(QToolButton::InstantPopup);

    connect(screenShotBtn, SIGNAL(pressed()), this, SLOT(takeScreenshot()));

    QVariantMap options;
    
    auto controlBarLayout = new QHBoxLayout;
    playSceneBtn = new QPushButton(fontIcons->icon(fa::play), "Play scene");
    playSceneBtn->setToolTip("Play all animations in the scene");
    playSceneBtn->setStyleSheet("background: transparent");

    options.insert("color", QColor(52, 152, 219));
    options.insert("color-active", QColor(52, 152, 219));
	playSimBtn = new QPushButton(fontIcons->icon(fa::play, options), "Simulate physics");
	playSimBtn->setToolTip("Simulate physics only");
	playSimBtn->setStyleSheet("background: transparent");

	cameraView = new QPushButton;
	cameraView->setStyleSheet("QPushButton{background:rgba(0,0,0,0);}");	

    controlBarLayout->setSpacing(8);
    controlBarLayout->addWidget(screenShotBtn);
	controlBarLayout->addWidget(cameraView);
    controlBarLayout->addWidget(wireFramesButton);
    controlBarLayout->addStretch();
    controlBarLayout->addWidget(playSceneBtn);
    controlBarLayout->addSpacing(2);
#ifdef QT_DEBUG
	controlBarLayout->addWidget(playSimBtn);
#endif // QT_DEBUG

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
    playerControlsLayout->setContentsMargins(6, 6, 6, 6);
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

	connect(playSimBtn, &QPushButton::pressed, [this]() {
		UiManager::isSimulationRunning = !UiManager::isSimulationRunning;
		
        QVariantMap options;

		if (UiManager::isSimulationRunning) {
			UiManager::startPhysicsSimulation();

            playSimBtn->setText("Stop Simulation");
			playSimBtn->setToolTip("Pause physics simulation");

            options.insert("color", QColor(241, 196, 15));
            options.insert("color-active", QColor(241, 196, 15));
            playSimBtn->setIcon(fontIcons->icon(fa::stop, options));
		}
		else {
            UiManager::restartPhysicsSimulation();

            playSimBtn->setText("Simulate Physics");
			playSimBtn->setToolTip("Simulate physics only");

            options.insert("color", QColor(52, 152, 219));
            options.insert("color-active", QColor(52, 152, 219));
            playSimBtn->setIcon(fontIcons->icon(fa::play, options));
		}

        if (!!activeSceneNode) sceneNodeSelected(activeSceneNode);
	});

    playerControls->setLayout(playerControlsLayout);

    containerLayout->setSpacing(0);
    containerLayout->setContentsMargins(0, 0, 0, 0);
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
    sceneView->setMainWindow(this);
    sceneView->setDatabase(db);
    Globals::sceneViewWidget = sceneView;
    UiManager::setSceneViewWidget(sceneView);

	playerView = new PlayerWidget(viewPort);

    wireCheckAction->setChecked(sceneView->getShowLightWires());
	physicsCheckAction->setChecked(sceneView->getShowDebugDrawFlags());

    QGridLayout* layout = new QGridLayout;
    layout->addWidget(sceneView);
    layout->setContentsMargins(0, 0, 0, 0);
    sceneContainer->setLayout(layout);

    connect(sceneView, &SceneViewWidget::addDroppedMesh, [this](QString path, bool v, QVector3D pos, QString guid, QString name) {
        addMaterialMesh(path, v, pos, guid, name);
    });

    connect(sceneView, &SceneViewWidget::addPrimitive, [this](QString guid) {
        addPrimitiveObject(guid);
    });

    connect(sceneView, &SceneViewWidget::addDroppedParticleSystem, [this](bool v, QVector3D pos, QString guid, QString name) {
        addAssetParticleSystem(v, pos, guid, name);
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
	//ui->stackedWidget->addWidget(new QWidget(this));
	shaderGraph = new shadergraph::MainWindow(this,db);
	shaderGraph->setAssetView(_assetView);
	ui->stackedWidget->addWidget(shaderGraph);
	ui->stackedWidget->addWidget(playerView);

	connect(pmContainer, SIGNAL(fileToOpen(bool)), SLOT(openProject(bool)));
	connect(pmContainer, SIGNAL(closeProject()), SLOT(closeProject()));
	connect(pmContainer, SIGNAL(fileToCreate(QString, QString)), SLOT(newProject(QString, QString)));
	connect(pmContainer, SIGNAL(exportProject()), SLOT(exportSceneAsZip()));
}

void MainWindow::setupToolBar()
{

	QVariantMap options;
	options.insert("color", QColor(255, 255, 255));
	options.insert("color-active", QColor(255, 255, 255));
  
    toolBar = new QToolBar("Tool Bar");
	toolBar->setIconSize(QSize(16, 16));

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

	toolBar->addSeparator();

	connect(actionUndo, SIGNAL(triggered(bool)), SLOT(undo()));
	connect(actionRedo, SIGNAL(triggered(bool)), SLOT(redo()));

    actionTranslate = new QAction;
    actionTranslate->setObjectName(QStringLiteral("actionTranslate"));
    actionTranslate->setCheckable(true);
	actionTranslate->setToolTip("Translate | Manipulator for translating objects | Translates the object along a given axis");
	actionTranslate->setIcon(fontIcons->icon(fa::arrows, options));
	toolBar->addAction(actionTranslate);

    actionRotate = new QAction;
    actionRotate->setObjectName(QStringLiteral("actionRotate"));
    actionRotate->setCheckable(true);
	actionRotate->setToolTip("Rptate | Manipulator for rotating objects | Rotates the object along a given axis");
	actionRotate->setIcon(fontIcons->icon(fa::rotateright, options));
	toolBar->addAction(actionRotate);

    actionScale = new QAction;
    actionScale->setObjectName(QStringLiteral("actionScale"));
    actionScale->setCheckable(true);
	actionScale->setToolTip("Scale | Manipulator for scaling objects | Scales the object along a given axis");
	actionScale->setIcon(fontIcons->icon(fa::expand, options));
	toolBar->addAction(actionScale);

    toolBar->addSeparator();

    QAction *actionGlobalSpace = new QAction;
    actionGlobalSpace->setObjectName(QStringLiteral("actionGlobalSpace"));
    actionGlobalSpace->setCheckable(true);
	actionGlobalSpace->setToolTip("Global Space | Move objects relative to the global world");
	actionGlobalSpace->setIcon(fontIcons->icon(fa::globe, options));
	toolBar->addAction(actionGlobalSpace);

    QAction *actionLocalSpace = new QAction;
    actionLocalSpace->setObjectName(QStringLiteral("actionLocalSpace"));
    actionLocalSpace->setCheckable(true);
	actionLocalSpace->setToolTip("Local Space | Move objects relative to their transform");
	actionLocalSpace->setIcon(fontIcons->icon(fa::cube, options));
	toolBar->addAction(actionLocalSpace);

    toolBar->addSeparator();

    QAction *actionFreeCamera = new QAction;
    actionFreeCamera->setObjectName(QStringLiteral("actionFreeCamera"));
    actionFreeCamera->setCheckable(true);
	actionFreeCamera->setToolTip("Free Camera | Freely move and orient the camera");
	actionFreeCamera->setIcon(fontIcons->icon(fa::eye, options));
	toolBar->addAction(actionFreeCamera);

	QAction *actionArcballCam = new QAction;
	actionArcballCam->setObjectName(QStringLiteral("actionArcballCam"));
	actionArcballCam->setCheckable(true);
	actionArcballCam->setToolTip("Arc Ball Camera | Move and orient the camera around a fixed point | With this button selected, you are now able to move around a fixed point.");
	actionArcballCam->setIcon(fontIcons->icon(fa::dotcircleo, options));
	toolBar->addAction(actionArcballCam);

	toolBar->addSeparator();

    connect(actionTranslate,    SIGNAL(triggered(bool)), SLOT(translateGizmo()));
    connect(actionRotate,       SIGNAL(triggered(bool)), SLOT(rotateGizmo()));
    connect(actionScale,        SIGNAL(triggered(bool)), SLOT(scaleGizmo()));

    transformGroup = new QActionGroup(viewPort);
    transformGroup->addAction(actionTranslate);
    transformGroup->addAction(actionRotate);
    transformGroup->addAction(actionScale);
    actionTranslate->setChecked(true);

    connect(actionGlobalSpace,  SIGNAL(triggered(bool)), SLOT(useGlobalTransform()));
    connect(actionLocalSpace,   SIGNAL(triggered(bool)), SLOT(useLocalTransform()));

    transformSpaceGroup = new QActionGroup(viewPort);
    transformSpaceGroup->addAction(actionGlobalSpace);
    transformSpaceGroup->addAction(actionLocalSpace);
    actionGlobalSpace->setChecked(true);

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
	actionExport->setToolTip("Export | Export the current scene");
	actionExport->setIcon(fontIcons->icon(fa::upload, options));
	toolBar->addAction(actionExport);

	actionSaveScene = new QAction;
	actionSaveScene->setObjectName(QStringLiteral("actionSaveScene"));
	actionSaveScene->setVisible(!settings->getValue("auto_save", true).toBool());
	actionSaveScene->setCheckable(false);
	actionSaveScene->setToolTip("Save | Save the current scene");
	actionSaveScene->setIcon(fontIcons->icon(fa::floppyo, options));
	toolBar->addAction(actionSaveScene);

	QAction *viewDocks = new QAction;
	viewDocks->setObjectName(QStringLiteral("viewDocks"));
	viewDocks->setCheckable(false);
	viewDocks->setToolTip("Toggle Widgets | Toggle the dock widgets");
	viewDocks->setIcon(fontIcons->icon(fa::listalt, options));
	toolBar->addAction(viewDocks);

	cameraView->setIconSize(QSize(17, 17));

	connect(cameraView, &QPushButton::clicked, [=](){ emit projectionChangeRequested(!sceneView->editorCam->isPerspective); });

	connect(this, SIGNAL(projectionChangeRequested(bool)), this, SLOT(changeProjection(bool)));	

	connect(sceneView, &SceneViewWidget::updateToolbarButton, [=]() {
		if (sceneView->editorCam->isPerspective) projectionChangeRequested(true);
		else projectionChangeRequested(false);
	});
	
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

    shortcut = new QShortcut(QKeySequence("alt+s"),sceneView);
    connect(shortcut, SIGNAL(activated()), this, SLOT(scaleGizmo()));

    // Save
	shortcut = new QShortcut(QKeySequence("ctrl+s"), sceneView);
	connect(shortcut, SIGNAL(activated()), this, SLOT(saveScene()));

	shortcut = new QShortcut(QKeySequence("o"), sceneView);
	connect(shortcut, &QShortcut::activated, [=]() {
		emit projectionChangeRequested(false);
	});

	shortcut = new QShortcut(QKeySequence("p"), sceneView);
	connect(shortcut, &QShortcut::activated, [=]() {
		emit projectionChangeRequested(true);
	});


    // TAB SHORTCUTS
    shortcut = new QShortcut(QKeySequence("ctrl+1"), this);
    connect(shortcut, &QShortcut::activated, [=]() {
        this->switchSpace(WindowSpaces::DESKTOP);
    });

    shortcut = new QShortcut(QKeySequence("ctrl+2"), this);
    connect(shortcut, &QShortcut::activated, [=]() {
        if (UiManager::isSceneOpen)
            this->switchSpace(WindowSpaces::PLAYER);
    });

    shortcut = new QShortcut(QKeySequence("ctrl+3"), this);
    connect(shortcut, &QShortcut::activated, [=]() {
        if (UiManager::isSceneOpen)
            this->switchSpace(WindowSpaces::EDITOR);
    });

    shortcut = new QShortcut(QKeySequence("ctrl+4"), this);
    connect(shortcut, &QShortcut::activated, [=]() {
        this->switchSpace(WindowSpaces::EFFECT);
    });

    shortcut = new QShortcut(QKeySequence("ctrl+5"), this);
    connect(shortcut, &QShortcut::activated, [=]() {
        this->switchSpace(WindowSpaces::ASSETS);
    });

    shortcut = new QShortcut(QKeySequence("ctrl+tab"), this);
    connect(shortcut, &QShortcut::activated, [=]() {
        if ((previousSpace == WindowSpaces::PLAYER || previousSpace == WindowSpaces::EDITOR) && !UiManager::isSceneOpen)
            return;

        this->switchSpace(previousSpace);
    });

    shortcut = new QShortcut(QKeySequence("space"), this);
    connect(shortcut, &QShortcut::activated, [=]() {
        if (currentSpace == WindowSpaces::EDITOR)
            onPlaySceneButton();
        else if (currentSpace == WindowSpaces::PLAYER)
            playerView->onPlayScene();
    });

    // left
    shortcut = new QShortcut(QKeySequence("A"), this);
    connect(shortcut, &QShortcut::activated, [=]() {
        qDebug() << "left;";
    });

    // right
    shortcut = new QShortcut(QKeySequence("D"), this);
    connect(shortcut, &QShortcut::activated, [=]() {
        qDebug() << "right;";
    });


    // back
    shortcut = new QShortcut(QKeySequence("S"), this);
    connect(shortcut, &QShortcut::activated, [=]() {
        qDebug() << "back;";
    });


    // forward
    shortcut = new QShortcut(QKeySequence("W"), this);
    connect(shortcut, &QShortcut::activated, [=]() {
        qDebug() << "forward;";
    });
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
    cl->setContentsMargins(0, 0, 0, 0);
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

void MainWindow::toggleDebugDrawer(bool state)
{
	sceneView->setShowDebugDrawFlags(state);
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

	// this is to ensure the editor's context is created
	switchSpace(WindowSpaces::EDITOR);

    newScene();
    UiManager::isSceneOpen = true;
    ui->actionClose->setDisabled(false);

    saveScene(filename, projectPath);

    assetWidget->trigger();

    UiManager::clearUndoStack();
    UiManager::updateWindowTitle();
	updateTopMenuStates(WindowSpaces::EDITOR);
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
    actionTranslate->setChecked(true);
}

void MainWindow::rotateGizmo()
{
    sceneView->setGizmoRot();
    actionRotate->setChecked(true);
}

void MainWindow::scaleGizmo()
{
    sceneView->setGizmoScale();
    actionScale->setChecked(true);
}

void MainWindow::onPlaySceneButton()
{
	UiManager::isSimulationRunning = !UiManager::isSimulationRunning;

    if (UiManager::isScenePlaying) {
        enterEditMode();
		//UiManager::restartPhysicsSimulation();
		sceneView->stopPlayingScene();
    }
    else {
        enterPlayMode();
		//UiManager::startPhysicsSimulation();
		sceneView->startPlayingScene();
    }

	if (!!activeSceneNode) sceneNodeSelected(activeSceneNode);
}

void MainWindow::enterEditMode()
{
    UiManager::isScenePlaying = false;
    UiManager::enterEditMode();
    
    playSceneBtn->setText("Play Scene");
    playSceneBtn->setToolTip("Play scene");
	shaderGraph->setAssetWidgetDatabase(db);
    QVariantMap options;
    options.insert("color", QColor(46, 204, 113));
    options.insert("color-active", QColor(46, 204, 113));
    playSceneBtn->setIcon(fontIcons->icon(fa::play, options));
}

void MainWindow::enterPlayMode()
{
    UiManager::isScenePlaying = true;
    UiManager::enterPlayMode();
    
    playSceneBtn->setEnabled(true);
    playSceneBtn->setText("Stop playing");
    playSceneBtn->setToolTip("Stop playing");

    QVariantMap options;
    options.insert("color", QColor(231, 76, 60));
    options.insert("color-active", QColor(231, 76, 60));
    playSceneBtn->setIcon(fontIcons->icon(fa::stop, options));
}

void MainWindow::changeProjection(bool val)
{
	if (!val) {
		sceneView->getScene()->camera->setProjection(iris::CameraProjection::Orthogonal);
		cameraView->setIcon(QIcon(":/icons/orthogonal-view-80.png"));
		cameraView->setToolTip(tr("Orthogonal view | Toggle to switch to perspective view"));		
	}
	else {
		sceneView->getScene()->camera->setProjection(iris::CameraProjection::Perspective);
		cameraView->setIcon(QIcon(":/icons/perspective-view-80.png"));
		cameraView->setToolTip(tr("Perspective view | Toggle to switch to orthogonal view"));
	}
}
