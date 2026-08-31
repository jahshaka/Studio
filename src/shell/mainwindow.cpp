/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include <QQuaternion>
#include "shell/mainwindow.h"
#include "ui_mainwindow.h"

#include <QWindow>
#include <QSurface>
#include <QScrollArea>
#include <QTextDocument>
#include <QTemporaryFile>

#include <memory>

#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/viewernode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/assets/shader.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/core/viewport.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/animation/keyframeset.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/materials/postprocessmanager.h"
#include "irisgl/core/logger.h"

#include "data/guidmanager.h"
#include "services/thumbnailmanager.h"
#include "ui/dialogs/ogrepreviewdialog.h"
#include "bridge/enginehost.h"
#include "viewport/enginerenderdriver.h"
#include "bridge/enginematerialpreview.h"
#include "ui/dialogs/donatedialog.h"
#include "ui/dialogs/custompopup.h"
#include "services/assethelper.h"
#include "services/scenenodehelper.h"

#include <QFontDatabase>
#include <qstandarditemmodel.h>
#include <QKeyEvent>
#include <QMessageBox>
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

#include "ui/dialogs/loadmeshdialog.h"
#include "ui/panels/timeline/nodekeyframeanimation.h"
#include "ui/panels/timeline/nodekeyframe.h"

#include "ui/panels/timeline/animationwidget.h"

#include "data/project.h"
#include "ui/controls/accordionbladewidget.h"

#include "viewport/editorcameracontroller.h"
#include "data/settingsmanager.h"
#include "ui/dialogs/preferencesdialog.h"
#include "ui/dialogs/preferences/worldsettingswidget.h"
#include "ui/dialogs/aboutdialog.h"

#include "services/collisionhelper.h"

#include "data/materialpreset.h"

#include "ui/pages/projectmanager.h"

#include "io/scenewriter.h"
#include "io/scenereader.h"

#include "data/constants.h"
#include "io/materialreader.h"
#include "data/database/database.h"

#include "commands/addscenenodecommand.h"
#include "commands/deletescenenodecommand.h"

#include "ui/dialogs/screenshotwidget.h"
#include "viewport/editordata.h"
#include "ui/panels/assetwidget.h"

#include "ui/dialogs/newprojectdialog.h"

#include "ui/panels/scenehierarchywidget.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "ui/panels/propertywidgets/worldpropertywidget.h"

#include "ui/panels/presets/skypresets.h"

#include "ui/panels/presets/assetmodelpanel.h"
#include "ui/panels/presets/assetmaterialpanel.h"

#include "ui/pages/assetview.h"
#include "ui/dialogs/toast.h"

#include "zip.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/physics/charactercontroller.h"
#include "irisgl/thirdparty/bullet3/src/btBulletDynamicsCommon.h"

#include "modules/materials/effectspage.h"
#include "modules/materials/materialsmodule.h"
#include "modules/publish/publishmodule.h"
#include "modules/studiomodule.h"
#include "player/playerwidget.h"
#include "player/engineplayerview.h"
#include "viewport/headlesseditorviewport.h"

#include "scripting/scripthost.h"
#include "scripting/scriptengine.h"
#include "scripting/mcp/mcpserver.h"
#include "ui/panels/scriptconsole.h"
#include "scripting/modules/studiomodules.h"

#include "services/services.h"
#include "services/shortcutregistry.h"
#include "viewport/snapsettings.h"
#include "services/subscriber.h"
#include "services/undoservice.h"
#include "services/selectionservice.h"
#include "services/playbackservice.h"
#include "services/projectservice.h"
#include "services/sceneeditservice.h"
#include "services/thumbnailservice.h"
#include "services/assetservice.h"
#include "ui/style/stylesheet.h"
#include "ui/style/thememanager.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    // The one live Project instance (Phase 4: was Globals::project, a static
    // initialised with the same call). Must exist before any setup*() runs —
    // ProjectManager reads it during construction.
    project = Project::createNew();

    ui->setupUi(this);

	settings = SettingsManager::getDefaultManager();
	SnapSettings::bindSettings(settings->settings);   // snap sizes persist beside the shortcuts



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

    prefsDialog = new PreferencesDialog(nullptr, db, settings);
    aboutDialog = new AboutDialog();

    camControl = Q_NULLPTR;

    setupFileMenu();
	fontIcons = new QtAwesome;
	fontIcons->initFontAwesome();

    setupViewPort();
	setupUndoRedo();
	// Services before pages: pages and modules are constructed against the
	// service layer (audit §6.2 — ModuleHost carries it).
	setupServices();
    setupDesktop();
    setupToolBar();
    setupDockWidgets();
    setupShortcuts();
    prefsDialog->wireShortcuts(shortcutRegistry);

	// scripting (SCRIPTING_SPEC §2): the host sees the live app; the console
	// dock starts hidden — Ctrl+` toggles it in the editor space.
	scriptHost = new ScriptHost;
	scriptHost->mainWindow = this;
	scriptHost->db = db;
	scriptHost->project = project;
	scriptHost->viewport = sceneView;
	scriptHost->projectManager = pmContainer;
	scriptHost->undoStack = undoStack;
	scriptHost->services = services;
	scriptHost->projectOpen = [this]() {
		return projectService->isSceneOpen() && !project->getProjectGuid().isEmpty();
	};
	scriptHost->engineReady = [this]() {
		return EngineHost::instance().isRunning() && sceneView->isInitialized();
	};
	scriptHost->macroOpenChanged = [this](bool open) { undoService->setScriptMacroOpen(open); };
	scriptEngine = new ScriptEngine(*scriptHost, this);
	registerStudioModules(*scriptEngine);
	for (auto *module : modules) module->registerApi(*scriptEngine);

	scriptConsole = new ScriptConsole(scriptEngine);
	scriptConsoleDock = new QDockWidget("Script Console", viewPort);
	scriptConsoleDock->setObjectName(QStringLiteral("scriptConsoleDock"));
	scriptConsoleDock->setWidget(scriptConsole);
	viewPort->addDockWidget(Qt::BottomDockWidgetArea, scriptConsoleDock);
	scriptConsoleDock->hide();

	// MCP endpoint (CLAUDE_EDITOR_SPEC.md phase 1): OFF by default — total
	// lockdown, the scripting engine is the only capability surface. Started
	// here only when the Preferences toggle was saved on; --mcp-port=N starts
	// it from the CLI path instead.
	mcpServer = new McpServer(scriptEngine, this);
	prefsDialog->wireMcp(mcpServer, this);
	if (settings->getValue("mcp_enabled", false).toBool()) {
		QString mcpError;
		if (!startMcpServer(quint16(settings->getValue("mcp_port", McpServer::kDefaultPort).toUInt()), &mcpError))
			qWarning("MCP: %s", qPrintable(mcpError));
	}

	updateTopMenuStates(currentSpace);

	restoreGeometry(settings->getValue("geometry", "").toByteArray());
	restoreState(settings->getValue("windowState", "").toByteArray());

}

void MainWindow::grabOpenGLContextHack()
{
    //switchSpace(WindowSpaces::PLAYER);
}

void MainWindow::goToDesktop()
{
    show();
    switchSpace(WindowSpaces::DESKTOP, true);
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
        project->getProjectGuid(),
        project->getProjectGuid(),
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
		QDir(project->getProjectFolder()).filePath("Tile.png"));

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
												   project->getProjectGuid(),
												   project->getProjectGuid(),
                                                   QString(),
                                                   QString(), 
												   thumbnailBytes);

    db->createDependency(
        static_cast<int>(ModelTypes::Object),
        static_cast<int>(ModelTypes::Texture),
        nodeGuid, assetGuid,
        project->getProjectGuid()
    );

    auto assetTexture = new AssetTexture;
    assetTexture->fileName = "Tile.png";
    assetTexture->assetGuid = assetGuid;
    assetTexture->path = QDir(project->getProjectFolder()).filePath("Tile.png");
    AssetManager::addAsset(assetTexture);

    auto m = iris::CustomMaterial::create();
    m->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));
    m->setValue("diffuseTexture", QDir(project->getProjectFolder()).filePath("Tile.png"));
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

            if (obj == sceneContainer) {
                QCoreApplication::sendEvent(sceneView->asWidget(), event);
            }

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

	if (autoSave && projectService->isSceneOpen()) {
		saveScene();
		closing = true;
		event->accept();
	}
	else {
		if (undoService->isDirty() && !undoService->savedCountMatchesCurrent()) {
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

void MainWindow::sceneTreeCustomContextMenu(const QPoint& pos)
{
}

void MainWindow::stopAnimWidget()
{
    animWidget->stopAnimation();
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
}

void MainWindow::setupServices()
{
    // The service layer (APP_ARCHITECTURE_AUDIT §3.3). The shell constructs
    // the services, wires their signals to its widgets, and hands the
    // aggregate to the scripting host. Phase 4 dissolved the UiManager hub:
    // the services own the state their statics used to hold.
    undoService = new UndoService(undoStack);

    selectionService = new SelectionService(this);
    connect(selectionService, &SelectionService::selectionChanged,
            this, &MainWindow::applySelectionToUi);

    playbackService = new PlaybackService(this);
    playbackService->setViewport(sceneView);
    connect(playbackService, &PlaybackService::editModeEntered,
            this, &MainWindow::applyEditModeUi);
    connect(playbackService, &PlaybackService::playModeEntered,
            this, &MainWindow::applyPlayModeUi);

    projectService = new ProjectService(db, project, settings,
                                        sceneView, undoService,
                                        [this]() { return scene; });

    sceneEditService = new SceneEditService(db, project, undoService,
                                            selectionService, sceneView,
                                            [this]() { return scene; }, this);
    connect(sceneEditService, &SceneEditService::hierarchyChanged, this, [this]() {
        sceneHierarchyWidget->repopulateTree();
    });
    // The undo commands' refresh notifications (Phase 4: was
    // UiManager::sceneHierarchyWidget / ::propertyWidget reach-ins).
    connect(sceneEditService, &SceneEditService::nodeInserted, this,
            [this](const iris::SceneNodePtr &node) {
        if (sceneHierarchyWidget) sceneHierarchyWidget->insertChild(node);
    });
    connect(sceneEditService, &SceneEditService::nodeRemoved, this,
            [this](const iris::SceneNodePtr &node) {
        if (sceneHierarchyWidget) sceneHierarchyWidget->removeChild(node);
    });
    connect(sceneEditService, &SceneEditService::transformRefreshRequested, this, [this]() {
        if (sceneNodePropertiesWidget) sceneNodePropertiesWidget->refreshTransform();
    });
    connect(sceneEditService, &SceneEditService::assetViewRefreshRequested, this, [this]() {
        assetWidget->updateAssetView(assetWidget->assetItem.selectedGuid);
    });
    connect(sceneEditService, &SceneEditService::materialApplied, this, [this](const QString &type) {
        sceneNodePropertiesWidget->refreshMaterial(type);
    });

    thumbnailService = new ThumbnailService(db, project);
    assetService = new AssetService(db, project);

    services = new StudioServices;
    services->eventBus = new Subscriber(this);
    services->undo = undoService;
    services->selection = selectionService;
    services->playback = playbackService;
    services->project = projectService;
    services->sceneEdit = sceneEditService;
    services->thumbnails = thumbnailService;
    services->assets = assetService;

    // Commands raise their refreshes through the aggregate (stamped at push);
    // the viewport's gizmos push through the same aggregate.
    undoService->setServices(services);
    if (sceneView) { sceneView->setServices(services); sceneView->setProject(project); }
    if (prefsDialog) prefsDialog->wireEditor(sceneView, this);
    ThumbnailGenerator::getSingleton()->setProject(project);
    // Pages, panels and modules are constructed AFTER the services and wired
    // at their creation sites (setupDesktop / setupDockWidgets).
    // SceneWriter's two project reads live in static methods (see scenewriter.h),
    // so the pointer rides a class static wired once, like its Database handle.
    SceneWriter::setProject(project);
}

void MainWindow::setupUndoRedo()
{
    undoStack = new QUndoStack(this);


    connect(ui->actionUndo, &QAction::triggered, [this]() {
        undo();
        updateWindowTitle();
    });

    connect(ui->actionEditUndo, &QAction::triggered, [this]() {
        undo();
        updateWindowTitle();
    });

    // (shortcut moved to ShortcutRegistry "edit.undo" — this action is not
    // attached to any widget, so a QKeySequence here never fired anyway)

    connect(ui->actionRedo, &QAction::triggered, [this]() {
        redo();
        updateWindowTitle();
    });

    connect(ui->actionEditRedo, &QAction::triggered, [this]() {
        redo();
        updateWindowTitle();
    });

    // (shortcut moved to ShortcutRegistry "edit.redo")
}

WindowSpaces MainWindow::getWindowSpace()
{
	return currentSpace;
}

void MainWindow::deselectViewports()
{
	editor_menu->setStyleSheet(StyleSheet::TopMenuDisabled());
	editor_menu->setDisabled(true);
	editor_menu->setCursor(Qt::ArrowCursor);
	player_menu->setStyleSheet(StyleSheet::TopMenuDisabled());
	player_menu->setDisabled(true);
	player_menu->setCursor(Qt::ArrowCursor);
}

void MainWindow::switchSpace(WindowSpaces space, bool force)
{
	if (currentSpace == space && !force)
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
			if (projectService->isSceneOpen()) {
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
            playbackService->setSceneMode(SceneMode::EditMode);

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
            playbackService->setSceneMode(SceneMode::PlayMode);
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
			if (projectService->isSceneOpen()) {
				playSceneBtn->hide();
			}
    		
			break;
    	}

		case WindowSpaces::EFFECT: {
			qDebug() << "switchSpace(EFFECT): count" << ui->stackedWidget->count()
			         << "index before" << ui->stackedWidget->currentIndex();
			ui->stackedWidget->setCurrentIndex(3);
			ui->stackedWidget->currentWidget()->setFocus();

			toolBar->setVisible(false);

			shaderGraph->refreshShaderGraph();
			qDebug() << "switchSpace(EFFECT): index now" << ui->stackedWidget->currentIndex()
			         << "current" << ui->stackedWidget->currentWidget()
			         << "shaderGraph visible" << shaderGraph->isVisible()
			         << "size" << shaderGraph->size();

			break;
		}

		case WindowSpaces::PUBLISH: {
			ui->stackedWidget->setCurrentIndex(5);
			toggleWidgets(false);
			toolBar->setVisible(false);
			if (projectService->isSceneOpen()) playSceneBtn->hide();
			break;
		}

        default: break;
    }

	updateTopMenuStates(space);
}

void MainWindow::updateTopMenuStates(WindowSpaces activeSpace)
{
	const QString disabledMenu = StyleSheet::TopMenuDisabled();
	const QString selectedMenu = StyleSheet::TopMenuSelected();
	const QString unselectedMenu = StyleSheet::TopMenuUnselected();

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

	publish_menu->setStyleSheet(activeSpace == WindowSpaces::PUBLISH ? selectedMenu : unselectedMenu);
	publish_menu->setCursor(Qt::PointingHandCursor);

	editor_menu->setStyleSheet(activeSpace == WindowSpaces::EDITOR ? selectedMenu : unselectedMenu);
	player_menu->setStyleSheet(activeSpace == WindowSpaces::PLAYER ? selectedMenu : unselectedMenu);

	// Under Qlementine the classic border-color swap above is neutralized (the
	// getters return "") and a checked flat button renders invisibly on the
	// near-black header, so the active space gets its label painted in the
	// accent color instead — a text-only sheet, the one deliberate stylesheet
	// in Qlementine mode. Runs after the classic swaps so it wins; the
	// scene-closed branch below still overrides editor/player as before.
	if (!ThemeManager::classicActive()) {
		static const QString qlemActive =
			QStringLiteral("QPushButton { color: #3498db; }");
		const QList<QPair<QPushButton *, WindowSpaces>> spaceButtons = {
			{ worlds_menu, WindowSpaces::DESKTOP }, { assets_menu, WindowSpaces::ASSETS },
			{ effect_menu, WindowSpaces::EFFECT }, { publish_menu, WindowSpaces::PUBLISH },
			{ editor_menu, WindowSpaces::EDITOR }, { player_menu, WindowSpaces::PLAYER }
		};
		for (const auto &pair : spaceButtons)
			pair.first->setStyleSheet(activeSpace == pair.second ? qlemActive : QString());
	}

	if (projectService->isSceneOpen()) {
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
	Q_UNUSED(filename);
	projectService->saveInitialScene(projectPath);
}

bool MainWindow::saveProjectBlob()
{
	return projectService->saveProjectBlob();
}

void MainWindow::saveScene()
{
	projectService->saveOpenScene();
}

void MainWindow::openProject(bool playMode)
{
	if(!!scene)
        removeScene();

    EditorData* editorData = Q_NULLPTR;
    updateWindowTitle();

	iris::PostProcessManagerPtr postMan;
    auto scene = projectService->readProjectScene(&editorData, postMan);

    playbackService->setPlayerMode(playMode);
    projectService->setSceneOpen(true);
    ui->actionClose->setDisabled(false);
    setScene(scene);


    if (editorData != Q_NULLPTR) {
        sceneView->setEditorData(editorData);
		// needs to be done so controllers can have the correct
		// camera
		playerView->setScene(scene);
        wireCheckAction->setChecked(editorData->showLightWires);
        gridCheckAction->setChecked(editorData->showGrid);
		physicsCheckAction->setChecked(editorData->showDebugDrawFlags);
    }

    assetWidget->trigger();

	undoService->resetSavedCount();
	playMode ? switchSpace(WindowSpaces::PLAYER) : switchSpace(WindowSpaces::EDITOR);
	updateTopMenuStates(playbackService->isPlayerMode() ? WindowSpaces::PLAYER : WindowSpaces::EDITOR);

	// highlight root node
	sceneHierarchyWidget->selectNode(scene->getRootNode()->getGUID());
	sceneNodePropertiesWidget->setSceneNode(scene->getRootNode());

    // autoplay scenes immediately
    if (playMode) {
        playBtn->setToolTip("Pause the scene");
        playBtn->setIcon(QIcon(":/icons/g_pause.svg"));
        playbackService->playScene();
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

        if (projectService->isSceneOpen()) {
            if (settings->getValue("auto_save", true).toBool()) saveScene();
        }

        scene->getPhysicsEnvironment()->destroyPhysicsWorld();

        //playbackService->stopSimulation();
        playSimBtn->setText("Simulate Physics");
        playSimBtn->setToolTip("Simulate physics only");

        QVariantMap options;
        options.insert("color", QColor(52, 152, 219));
        options.insert("color-active", QColor(52, 152, 219));
        playSimBtn->setIcon(fontIcons->icon(fa::play, options));
    }

    projectService->setSceneOpen(false);
    playbackService->setPlaying(false);
    ui->actionClose->setDisabled(false);

    undoService->clear();
    AssetManager::clearAssetList();

    setWindowTitle(originalTitle);

    scene->cleanup();
    scene.clear();

	undoService->resetSavedCount();

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
            return;
        }
    }

    // Not a built-in preset: a saved material asset (a project .material row,
    // e.g. one registered under Presets/ by an earlier preset apply). This
    // used to fall through silently — dropping a saved material onto an
    // object applied NOTHING persistent while the drag preview made it look
    // applied (the reopen-loses-materials report).
    sceneEditService->applyMaterialAsset(guid, selectionService->selected());
}

void MainWindow::applyMaterialPreset(MaterialPreset preset)
{
    sceneEditService->applyMaterialPreset(preset);
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
    thumbnailService->refreshObjectThumbnail(guid);
}

void MainWindow::refreshThumbnail(QListWidgetItem *item)
{
    if (item->data(MODEL_TYPE_ROLE).toInt() == static_cast<int>(ModelTypes::Object)) {
        thumbnailService->refreshObjectThumbnail(item->data(MODEL_GUID_ROLE).toString());
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
    selectionService->select(sceneNode);
}

iris::SceneNodePtr MainWindow::selectedSceneNode() const
{
    return selectionService->selected();
}

void MainWindow::applySelectionToUi(iris::SceneNodePtr sceneNode)
{
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
    sceneEditService->addPlane();
}

void MainWindow::addGround()
{
    sceneEditService->addGround();
}

void MainWindow::addCone()
{
    sceneEditService->addCone();
}

void MainWindow::addCapsule()
{
    sceneEditService->addCapsule();
}

void MainWindow::addCube()
{
    sceneEditService->addCube();
}

void MainWindow::addTorus()
{
    sceneEditService->addTorus();
}

void MainWindow::addSphere()
{
    sceneEditService->addSphere();
}

void MainWindow::addCylinder()
{
    sceneEditService->addCylinder();
}

void MainWindow::addPyramid()
{
    sceneEditService->addPyramid();
}

void MainWindow::addSponge()
{
    sceneEditService->addSponge();
}

void MainWindow::addTeapot()
{
    sceneEditService->addTeapot();
}

void MainWindow::addSteps()
{
    sceneEditService->addSteps();
}

void MainWindow::addGear()
{
    sceneEditService->addGear();
}

void MainWindow::addPointLight()
{
    sceneEditService->addPointLight();
}

void MainWindow::addSpotLight()
{
    sceneEditService->addSpotLight();
}


void MainWindow::addDirectionalLight()
{
    sceneEditService->addDirectionalLight();
}

void MainWindow::addAreaLight()
{
    sceneEditService->addAreaLight();
}

void MainWindow::addEmpty()
{
    sceneEditService->addEmpty();
}

void MainWindow::addViewer()
{
    sceneEditService->addViewer();
}

void MainWindow::addParticleSystem()
{
    sceneEditService->addParticleSystem();
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

    sceneEditService->addMesh(filename, ignore, position);
}

void MainWindow::addPrimitiveObject(const QString &text)
{
    sceneEditService->addPrimitive(text);
}

void MainWindow::addMaterialMesh(const QString &path, bool ignore, QVector3D position, const QString &guid, const QString &assetName)
{
    sceneEditService->addMaterialMesh(path, ignore, position, guid, assetName);
}

void MainWindow::addAssetParticleSystem(bool ignore, QVector3D position, QString guid, QString assetName)
{
    sceneEditService->addAssetParticleSystem(ignore, position, guid, assetName);
}

void MainWindow::addDragPlaceholder()
{
    /*
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
    sceneEditService->addNodeToActiveNode(sceneNode);
}

/**
 * adds sceneNode directly to the scene's rootNode
 * applied default material to mesh if one isnt present
 * ignore set to false means we only add it visually, usually to discard it afterw
 */
void MainWindow::addNodeToScene(QSharedPointer<iris::SceneNode> sceneNode, bool ignore)
{
    sceneEditService->addNodeToScene(sceneNode, ignore);
}

void MainWindow::repopulateSceneTree()
{
    this->sceneHierarchyWidget->repopulateTree();
}

void MainWindow::duplicateNode()
{
    duplicateSceneNode(selectionService->selected());
}

iris::SceneNodePtr MainWindow::duplicateSceneNode(iris::SceneNodePtr source)
{
    return sceneEditService->duplicateNode(source);
}

void MainWindow::createMaterial()
{
    sceneEditService->createMaterialFromNode(selectionService->selected(),
                                             assetWidget->assetItem.selectedGuid);
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

    sceneEditService->exportNodeTo(node, modelType, filePath);
}

void MainWindow::deleteNode()
{
    deleteSceneNode(selectionService->selected());
}

bool MainWindow::deleteSceneNode(iris::SceneNodePtr node)
{
    return sceneEditService->deleteNode(node);
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
    projectService->updateCurrentSceneThumbnail();
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
                        QString("%1_export").arg(project->getProjectName()),
                        "Supported Export Formats (*.zip)"
                    );

    if (filePath.isEmpty() || filePath.isNull()) return;
    if (!!scene) saveScene();

    // Maybe in the future one could add a way to using an in memory database
    // and saving that as a blob which can be put into the zip as bytes (iKlsR)
    // prepare our export database with the current scene, use the os temp location and remove after
    db->createExportScene(QStandardPaths::writableLocation(QStandardPaths::TempLocation),
                          project->getProjectGuid());

    // get the current project working directory
    auto pFldr = IrisUtils::join(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                 Constants::PROJECT_FOLDER);
    auto defaultProjectDirectory = settings->getValue("default_directory", pFldr).toString();
    auto pDir = IrisUtils::join(defaultProjectDirectory, "Projects", project->getProjectGuid());

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
    zip_entry_open(zip, QString(project->getProjectGuid() + ".db").toStdString().c_str());
    zip_entry_fwrite(
        zip,
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(project->getProjectGuid() + ".db").toStdString().c_str()
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
            .filePath(project->getProjectGuid() + ".db")
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
    if (sceneView) sceneView->setHierarchyDragSource(sceneHierarchyWidget->getWidget());

    connect(sceneHierarchyWidget,   SIGNAL(sceneNodeSelected(iris::SceneNodePtr)),
            this,                   SLOT(sceneNodeSelected(iris::SceneNodePtr)));

    // Scene Node Properties Dock
    // Since this widget can be longer than there is screen space, we need to add a QScrollArea
    // For this to also work, we need a "holder widget" that will have a layout and the scroll area
    sceneNodePropertiesDock = new QDockWidget("Properties", viewPort);
    sceneNodePropertiesDock->setObjectName(QStringLiteral("sceneNodePropertiesDock"));
    sceneNodePropertiesWidget = new SceneNodePropertiesWidget;
    sceneNodePropertiesWidget->setSceneView(sceneView);
    // World blade's "Show Grid" row is a second face of the View Options
    // Ground Grid action (created in setupViewPort, which runs before this)
    sceneNodePropertiesWidget->getWorldPropertyWidget()->setGridAction(gridCheckAction);
    sceneNodePropertiesWidget->setDatabase(db);
    sceneNodePropertiesWidget->setServices(services);
    sceneNodePropertiesWidget->setProject(project);
    sceneNodePropertiesWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sceneNodePropertiesWidget->setObjectName(QStringLiteral("SceneNodePropertiesWidget"));
    if (ThemeManager::classicActive())
        sceneNodePropertiesDock->setStyleSheet("QWidget { background-color: #202020; }");

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
    if (ThemeManager::classicActive())
        presetDockContents->setStyleSheet( "QWidget { background-color: #151515; }");
    SkyPresets *skyPresets = new SkyPresets;
    skyPresets->setMainWindow(this);
	skyPresets->setDatabase(db);
	skyPresets->setProject(project);

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
    assetWidget->setEventBus(services->eventBus);
    assetWidget->setProject(project);
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

	viewPort->setStyleSheet(StyleSheet::QMenuFlat());
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
	publish_menu = new QPushButton("Publish");
	publish_menu->setObjectName("publish_menu");
	publish_menu->setCursor(Qt::PointingHandCursor);

	assets_panel = new QWidget;

	auto hl = new QHBoxLayout;
    hl->setContentsMargins(0,0,0,0);
	hl->setSpacing(12);
    hl->addWidget(worlds_menu);
    hl->addWidget(player_menu);
	hl->addWidget(editor_menu);
	hl->addWidget(effect_menu);
	hl->addWidget(assets_menu);
	hl->addWidget(publish_menu);

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
    // Classic paints the logo via a stylesheet image; under Qlementine that
    // getter is neutralized, so set a real pixmap instead (sheet-free).
    if (ThemeManager::classicActive()) {
        jlogo->setStyleSheet(StyleSheet::MainWindowHeaderLogo(header_image_path));
    } else {
        jlogo->setPixmap(QPixmap(header_image_path)
                             .scaledToHeight(40, Qt::SmoothTransformation));
        jlogo->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

	help = new QPushButton;
	help->setObjectName("helpButton");
    // for adapting Qt6.9.0
    help->setText(QChar(static_cast<ushort>(fa::questioncircle)));
    //help->setText(QChar(fa::questioncircle));
	help->setFont(fontIcons->font(28));
	help->setCursor(Qt::PointingHandCursor);

	help->setStyleSheet(StyleSheet::HelpButton());

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

	prefs->setStyleSheet(StyleSheet::PrefsButton());

	connect(prefs, &QPushButton::pressed, [this]() { showPreferences(); });

	QWidget *buttons = new QWidget;
	QHBoxLayout *bl = new QHBoxLayout;
	buttons->setLayout(bl);
	bl->setSpacing(20);
	bl->addWidget(help);
	bl->addWidget(prefs);

	// The header buttons are mouse-driven chrome: keep them out of the focus
	// chain, or the theme's focus indicator rings the focused space button
	// whenever the window is active (Qlementine only hijacks the policy of
	// Strong/ClickFocus buttons, so NoFocus sticks).
	for (auto *chrome : { worlds_menu, player_menu, editor_menu, effect_menu,
	                      assets_menu, publish_menu, help, prefs })
		chrome->setFocusPolicy(Qt::NoFocus);

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
	connect(publish_menu, &QPushButton::pressed, [this]() { switchSpace(WindowSpaces::PUBLISH); });

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
    screenShotBtn->setStyleSheet(StyleSheet::BackgroundTransparent());
    screenShotBtn->setIcon(QIcon(":/icons/icons8-camera-48.png"));
	screenShotBtn->setIconSize(QSize(16,17));

    wireFramesButton = new QToolButton;
    wireFramesButton->setStyleSheet(
        "padding: 0 8px 0 0; margin: 0"
    );
    wireFramesMenu = new QMenu;
	wireFramesMenu->setStyleSheet(StyleSheet::QMenuFlat());

    wireCheckAction = new QAction(QIcon(), "Light Bounds");
    wireCheckAction->setCheckable(true);
    connect(wireCheckAction, SIGNAL(toggled(bool)), this, SLOT(toggleLightWires(bool)));
    wireFramesMenu->addAction(wireCheckAction);

    // Ground grid (EDITOR_SHORTCUTS_SPEC §3): default ON, per-scene persisted
    // beside the light-wires flag; hidden in Game View (G) and while playing.
    gridCheckAction = new QAction(QIcon(), "Ground Grid");
    gridCheckAction->setCheckable(true);
    connect(gridCheckAction, SIGNAL(toggled(bool)), this, SLOT(toggleGrid(bool)));
    wireFramesMenu->addAction(gridCheckAction);

    physicsCheckAction = new QAction(QIcon(), "Physics Debug Overlay");
    physicsCheckAction->setCheckable(true);
    connect(physicsCheckAction, SIGNAL(toggled(bool)), this, SLOT(toggleDebugDrawer(bool)));
    wireFramesMenu->addAction(physicsCheckAction);

    // Selection highlight: silhouette outline by default; this shows the polygon
    // wireframe instead (engine viewport only — legacy keeps its single style).
    auto selectionWireAction = new QAction(QIcon(), "Selection Wireframe");
    selectionWireAction->setCheckable(true);
    connect(selectionWireAction, &QAction::toggled, this, [this](bool on) {
        if (sceneView) sceneView->setSelectionWireframe(on);
    });
    wireFramesMenu->addAction(selectionWireAction);

    // --- Engine preview (Ogre-Next) -------------------------------------
    // Scaffolding for the engine migration: opens a window driven entirely
    // through the engine abstraction. Removed once the editor viewport moves over.
    {
        QAction *enginePreviewAction = new QAction(QIcon(), "Engine Preview (Ogre-Next)", this);
        enginePreviewAction->setShortcut(QKeySequence("Ctrl+Shift+O"));
        // Register on the window itself, application-wide: an action living only in a
        // toolbar-button menu does not reliably deliver its shortcut.
        enginePreviewAction->setShortcutContext(Qt::ApplicationShortcut);
        this->addAction(enginePreviewAction);
        wireFramesMenu->addSeparator();
        wireFramesMenu->addAction(enginePreviewAction);
        connect(enginePreviewAction, &QAction::triggered, this, [this]() {
            // ONE dialog for the life of the process: it owns the Engine, which is
            // one-per-process and (with the current Ogre build) cannot be re-created
            // after destruction. Closing merely hides it; a second trigger raises it.
            static OgrePreviewDialog *dlg = nullptr;
            if (!dlg) {
                dlg = new OgrePreviewDialog(this);
                connect(dlg, &QObject::destroyed, this, [] { dlg = nullptr; });
            }
            dlg->show();
            dlg->raise();
            dlg->activateWindow();
        });
    }
    // --------------------------------------------------------------------

    // Qlementine: the checkable actions become Switch rows (and stay in sync
    // with their QActions); a bonus is the menu no longer closes per toggle.
    ThemeManager::switchifyMenuToggles(wireFramesMenu);

    wireFramesButton->setMenu(wireFramesMenu);
    wireFramesButton->setText("View Options ");
    wireFramesButton->setPopupMode(QToolButton::InstantPopup);

    // Views ▾ — canonical camera views (owner request): Perspective plus the
    // six orthographic axis views. Same path as the view.* shortcuts and the
    // editor.setView verb (applyCameraView).
    viewsButton = new QToolButton;
    viewsButton->setStyleSheet("padding: 0 8px 0 0; margin: 0");
    viewsMenu = new QMenu;
    viewsMenu->setStyleSheet(StyleSheet::QMenuFlat());
    auto viewsGroup = new QActionGroup(viewsMenu);
    viewsGroup->setExclusive(true);
    const QVector<QPair<QString, QString>> canonicalViews = {
        { QStringLiteral("perspective"), QStringLiteral("Perspective") },
        { QStringLiteral("top"), QStringLiteral("Top") },
        { QStringLiteral("bottom"), QStringLiteral("Bottom") },
        { QStringLiteral("left"), QStringLiteral("Left") },
        { QStringLiteral("right"), QStringLiteral("Right") },
        { QStringLiteral("front"), QStringLiteral("Front") },
        { QStringLiteral("back"), QStringLiteral("Back") },
    };
    for (const auto &entry : canonicalViews) {
        QAction *action = viewsMenu->addAction(entry.second);
        action->setCheckable(true);
        action->setChecked(entry.first == QLatin1String("perspective"));
        action->setData(entry.first);
        viewsGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, entry]() { applyCameraView(entry.first); });
        viewsActions.push_back(action);
    }
    viewsButton->setMenu(viewsMenu);
    viewsButton->setText("Views ");
    viewsButton->setPopupMode(QToolButton::InstantPopup);

    connect(screenShotBtn, SIGNAL(pressed()), this, SLOT(takeScreenshot()));

    QVariantMap options;
    
    auto controlBarLayout = new QHBoxLayout;
    playSceneBtn = new QPushButton(fontIcons->icon(fa::play), "Play scene");
    playSceneBtn->setToolTip("Play all animations in the scene");
    playSceneBtn->setStyleSheet(StyleSheet::BackgroundTransparent());

    options.insert("color", QColor(52, 152, 219));
    options.insert("color-active", QColor(52, 152, 219));
	playSimBtn = new QPushButton(fontIcons->icon(fa::play, options), "Simulate physics");
	playSimBtn->setToolTip("Simulate physics only");
	playSimBtn->setStyleSheet(StyleSheet::BackgroundTransparent());

	cameraView = new QPushButton;
	cameraView->setStyleSheet("QPushButton{background:rgba(0,0,0,0);}");
	// The icon used to appear only after the first changeProjection() call —
	// invisible on a transparent background, but an empty grey pill under the
	// chrome button spec. The editor camera starts perspective; say so.
	cameraView->setIcon(QIcon(":/icons/perspective-view-80.png"));
	cameraView->setToolTip(tr("Perspective view | Toggle to switch to orthogonal view"));

    controlBarLayout->setSpacing(8);
    controlBarLayout->addWidget(screenShotBtn);
	controlBarLayout->addWidget(cameraView);
    controlBarLayout->addWidget(wireFramesButton);
    controlBarLayout->addWidget(viewsButton);
    controlBarLayout->addStretch();
    controlBarLayout->addWidget(playSceneBtn);
    controlBarLayout->addSpacing(2);
#ifdef QT_DEBUG
	controlBarLayout->addWidget(playSimBtn);
#endif // QT_DEBUG

    controlBar->setLayout(controlBarLayout);
    controlBar->setStyleSheet(StyleSheet::ControlBar());

    if (!ThemeManager::classicActive()) {
        // ONE chrome button spec across the app (shared with the desktop
        // footer, owner direction): rounded grey, consistent height,
        // horizontal text gutters — replaces the square edge-tight look.
        for (QWidget *chromeBtn :
             std::initializer_list<QWidget *>{ screenShotBtn, cameraView,
                                               wireFramesButton, viewsButton,
                                               playSceneBtn, playSimBtn })
            chromeBtn->setStyleSheet(ThemeManager::chromeButtonSheet());
    }

    playerControls = new QWidget;
    if (ThemeManager::classicActive())
        playerControls->setStyleSheet("background: #1A1A1A");

    auto playerControlsLayout = new QHBoxLayout;

    restartBtn = new QPushButton;
    restartBtn->setCursor(Qt::PointingHandCursor);
    restartBtn->setToolTip("Restart playback");
    restartBtn->setToolTipDuration(-1);
    restartBtn->setStyleSheet(StyleSheet::BackgroundTransparent());
    restartBtn->setIcon(QIcon(":/icons/rotate-to-right.svg"));
    restartBtn->setIconSize(QSize(16, 16));

    playBtn = new QPushButton;
    playBtn->setCursor(Qt::PointingHandCursor);
    playBtn->setToolTip("Play the scene");
    playBtn->setToolTipDuration(-1);
    playBtn->setStyleSheet(StyleSheet::BackgroundTransparent());
    playBtn->setIcon(QIcon(":/icons/g_play.svg"));
    playBtn->setIconSize(QSize(24, 24));

    stopBtn = new QPushButton;
    stopBtn->setCursor(Qt::PointingHandCursor);
    stopBtn->setToolTip("Stop playback");
    stopBtn->setToolTipDuration(-1);
    stopBtn->setStyleSheet(StyleSheet::BackgroundTransparent());
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
        playbackService->restartScene();
    });

    connect(playBtn, &QPushButton::pressed, [this]() {
        if (playbackService->isPlaying()) {
            playBtn->setToolTip("Play the scene");
            playBtn->setIcon(QIcon(":/icons/g_play.svg"));
            playbackService->pauseScene();
        } else {
            playBtn->setToolTip("Pause the scene");
            playBtn->setIcon(QIcon(":/icons/g_pause.svg"));
            playbackService->playScene();
        }
    });

    connect(stopBtn, &QPushButton::pressed, [this]() {
        playBtn->setToolTip("Play the scene");
        playBtn->setIcon(QIcon(":/icons/g_play.svg"));
        playbackService->stopScene();
    });

	connect(playSimBtn, &QPushButton::pressed, [this]() {
		playbackService->setSimulationRunning(!playbackService->isSimulationRunning());

        QVariantMap options;

		if (playbackService->isSimulationRunning()) {
			playbackService->startSimulation();

            playSimBtn->setText("Stop Simulation");
			playSimBtn->setToolTip("Pause physics simulation");

            options.insert("color", QColor(241, 196, 15));
            options.insert("color-active", QColor(241, 196, 15));
            playSimBtn->setIcon(fontIcons->icon(fa::stop, options));
		}
		else {
            playbackService->restartSimulation();

            playSimBtn->setText("Simulate Physics");
			playSimBtn->setToolTip("Simulate physics only");

            options.insert("color", QColor(52, 152, 219));
            options.insert("color-active", QColor(52, 152, 219));
            playSimBtn->setIcon(fontIcons->icon(fa::play, options));
		}

        if (auto sel = selectedSceneNode()) sceneNodeSelected(sel);
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

    // The engine viewport is the only renderer. When the engine cannot start
    // (offscreen platform: --headless scripts, --dump-api-docs) a document-only
    // stand-in serves the document verbs; nothing renders.
    sceneView = nullptr;
    {
        auto &host = EngineHost::instance();
        QString error;
        if (host.start(error)) {
            sceneView = createEngineSceneViewport(host.engine(), host.driver(), viewPort);
            host.driver()->start(16);
        } else {
            qCritical("Engine unavailable (%s): using the headless document-only viewport.",
                      qPrintable(error));
        }
    }
    if (!sceneView) sceneView = new HeadlessEditorViewport(viewPort);
    sceneView->asWidget()->setParent(viewPort);
    sceneView->asWidget()->setFocusPolicy(Qt::ClickFocus);
    sceneView->asWidget()->setFocus();
    sceneView->setMainWindow(this);
    sceneView->setDatabase(db);

	// The player page: PlayerWidget gets an EnginePlayerView (a second engine
	// Scene mirroring the same document), or none in headless runs.
	EnginePlayerView *playerBackend = nullptr;
	if (EngineHost::instance().isRunning()) {
		auto &host = EngineHost::instance();
		playerBackend = createEnginePlayerView(host.engine(), host.driver(), viewPort);
		playerBackend->setEditorViewport(sceneView);
	}
	playerView = new PlayerWidget(viewPort, playerBackend);

    wireCheckAction->setChecked(sceneView->getShowLightWires());
    gridCheckAction->setChecked(sceneView->getShowGrid());
	physicsCheckAction->setChecked(sceneView->getShowDebugDrawFlags());

    QGridLayout* layout = new QGridLayout;
    layout->addWidget(sceneView->asWidget());
    layout->setContentsMargins(0, 0, 0, 0);
    sceneContainer->setLayout(layout);

    auto events = sceneView->events();
    connect(events, &EditorViewportEvents::addDroppedMesh, this, [this](QString path, bool v, QVector3D pos, QString guid, QString name) {
        addMaterialMesh(path, v, pos, guid, name);
    });

    connect(events, &EditorViewportEvents::addPrimitive, this, [this](QString guid) {
        addPrimitiveObject(guid);
    });

    connect(events, &EditorViewportEvents::addDroppedParticleSystem, this, [this](bool v, QVector3D pos, QString guid, QString name) {
        addAssetParticleSystem(v, pos, guid, name);
    });

    connect(events, &EditorViewportEvents::sceneNodeSelected,
            this,   qOverload<iris::SceneNodePtr>(&MainWindow::sceneNodeSelected));

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
	pmContainer = new ProjectManager(db, project, this);
	pmContainer->mainWindow = this;
	projectService->setProjectManager(pmContainer);
	// The Assets page: AssetView gets an EngineAssetViewer (a third engine
	// Scene with its own preview document), or none in headless runs.
	IAssetViewer *assetBackend = nullptr;
	if (EngineHost::instance().isRunning()) {
		auto &host = EngineHost::instance();
		assetBackend = createEngineAssetViewer(host.engine(), host.driver(), this);
	}
	_assetView = new AssetView(db, this, assetBackend);
	_assetView->installEventFilter(this);
	_assetView->setServices(services);
	_assetView->setProject(project);

	ui->stackedWidget->addWidget(pmContainer);
	
	ui->stackedWidget->addWidget(viewPort);
	ui->stackedWidget->addWidget(_assetView);
	//ui->stackedWidget->addWidget(new QWidget(this));
	// The modules (audit §6.2): the shell constructs them against the full
	// host context and drives pages through the one interface. Stack order is
	// load-bearing (WindowSpaces indexes): EFFECT = 3, PLAYER = 4, PUBLISH = 5.
	ModuleHost moduleHost;
	moduleHost.db = db;
	moduleHost.settings = settings;
	moduleHost.viewport = sceneView;
	moduleHost.engine = &EngineHost::instance();
	moduleHost.services = services;
	moduleHost.project = project;
	moduleHost.shellWidget = this;
	materialsModule = new MaterialsModule;
	publishModule = new PublishModule;
	modules = { materialsModule, publishModule };
	for (auto *module : modules) module->initialize(moduleHost);
	materialsModule->setAssetView(_assetView);

	shaderGraph = materialsModule->effectsPage();
	ui->stackedWidget->addWidget(materialsModule->createPage());
	ui->stackedWidget->addWidget(playerView);
	publishView = publishModule->createPage();
	ui->stackedWidget->addWidget(publishView);

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

	connect(cameraView, &QPushButton::clicked, [=](){ emit projectionChangeRequested(!sceneView->editorCamera()->isPerspective); });

	connect(this, SIGNAL(projectionChangeRequested(bool)), this, SLOT(changeProjection(bool)));	

	connect(sceneView->events(), &EditorViewportEvents::updateToolbarButton, this, [=]() {
		if (sceneView->editorCamera()->isPerspective) projectionChangeRequested(true);
		else projectionChangeRequested(false);
	});
	
	connect(actionExport,		SIGNAL(triggered(bool)), SLOT(exportSceneAsZip()));
	connect(viewDocks,			SIGNAL(triggered(bool)), SLOT(toggleDockWidgets()));
	connect(actionSaveScene,	SIGNAL(triggered(bool)), SLOT(saveScene()));

    viewPort->addToolBar(toolBar);
}

void MainWindow::setupShortcuts()
{
    // EDITOR_SHORTCUTS_SPEC §1: every binding lives in the ShortcutRegistry —
    // persisted overrides (jahsettings.ini "shortcut/<id>"), conflict-checked
    // rebinding, and the generated Preferences → Shortcuts page. Inputs the
    // shortcut system cannot express (RMB-held fly keys, held modifiers,
    // Alt+drag) are registered as fixed rows for discoverability; their
    // handling lives in the viewport's event code.
    shortcutRegistry = new ShortcutRegistry(settings->settings, this);
    ShortcutRegistry &reg = *shortcutRegistry;

    // ---- tools (Unreal keys: W/E/R; T kept as the historical translate key.
    // While RMB is held these keys fly the camera — the viewport withholds
    // them from the shortcut system, see EngineSceneViewport::event) ----
    reg.add("tool.translate", "Translate Tool", "Tools", QKeySequence(Qt::Key_W), this,
            [this]() { if (currentSpace == WindowSpaces::EDITOR) translateGizmo(); });
    reg.add("tool.translate.alt", "Translate Tool (alias)", "Tools", QKeySequence(Qt::Key_T), this,
            [this]() { if (currentSpace == WindowSpaces::EDITOR) translateGizmo(); });
    reg.add("tool.rotate", "Rotate Tool", "Tools", QKeySequence(Qt::Key_E), this,
            [this]() { if (currentSpace == WindowSpaces::EDITOR) rotateGizmo(); });
    reg.add("tool.scale", "Scale Tool", "Tools", QKeySequence(Qt::Key_R), this,
            [this]() { if (currentSpace == WindowSpaces::EDITOR) scaleGizmo(); });
    reg.add("tool.cycle", "Cycle Gizmo Mode", "Tools", QKeySequence(Qt::Key_Space), this,
            [this]() { if (currentSpace == WindowSpaces::EDITOR) cycleGizmoMode(); });

    // ---- camera ----
    reg.add("camera.focus", "Focus Selection", "Camera", QKeySequence(Qt::Key_F), this,
            [this]() { if (currentSpace == WindowSpaces::EDITOR) sceneView->focusOnSelection(); });
    reg.add("view.orthographic", "Orthographic Projection", "Camera", QKeySequence(Qt::Key_O), this,
            [this]() { emit projectionChangeRequested(false); });
    reg.add("view.perspective", "Perspective Projection", "Camera", QKeySequence(Qt::Key_P), this,
            [this]() { emit projectionChangeRequested(true); });
    // Canonical axis views (historical X/Y/Z keys, moved out of the arcball
    // controller's raw key handling so they are remappable, listed in
    // Preferences -> Shortcuts, and work in the free camera too). Ctrl+Z
    // stays undo — "back" gets Shift+Z instead.
    reg.add("view.top", "Top View", "Camera", QKeySequence(Qt::Key_Y), this,
            [this]() { if (currentSpace == WindowSpaces::EDITOR) applyCameraView("top"); });
    reg.add("view.bottom", "Bottom View", "Camera", QKeySequence(Qt::CTRL | Qt::Key_Y), this,
            [this]() { if (currentSpace == WindowSpaces::EDITOR) applyCameraView("bottom"); });
    reg.add("view.left", "Left View", "Camera", QKeySequence(Qt::Key_X), this,
            [this]() { if (currentSpace == WindowSpaces::EDITOR) applyCameraView("left"); });
    reg.add("view.right", "Right View", "Camera", QKeySequence(Qt::CTRL | Qt::Key_X), this,
            [this]() { if (currentSpace == WindowSpaces::EDITOR) applyCameraView("right"); });
    reg.add("view.front", "Front View", "Camera", QKeySequence(Qt::Key_Z), this,
            [this]() { if (currentSpace == WindowSpaces::EDITOR) applyCameraView("front"); });
    reg.add("view.back", "Back View", "Camera", QKeySequence(Qt::SHIFT | Qt::Key_Z), this,
            [this]() { if (currentSpace == WindowSpaces::EDITOR) applyCameraView("back"); });
    reg.addFixed("camera.fly", "Fly Camera (free camera)", "Camera",
                 "RMB (hold) + W/A/S/D + Q/E \xc2\xb7 Shift: 3x");
    reg.addFixed("camera.wheel", "Zoom / Dolly", "Camera", "Mouse Wheel");

    // ---- view ----
    reg.add("view.gameView", "Game View (hide editor helpers)", "View", QKeySequence(Qt::Key_G), this,
            [this]() {
                if (currentSpace == WindowSpaces::EDITOR)
                    sceneView->setGameView(!sceneView->isGameView());
            });
    reg.add("view.grid", "Toggle Ground Grid", "View", QKeySequence(), this,
            [this]() { if (gridCheckAction) gridCheckAction->toggle(); });
    reg.add("window.fullscreen", "Immersive Fullscreen", "View", QKeySequence(Qt::Key_F11), this,
            [this]() { toggleImmersiveFullscreen(); });

    // ---- playback (Space is the gizmo cycle now — Unreal PIE puts play on
    // Alt+P; the toolbar Play button is unchanged) ----
    reg.add("play.toggle", "Play / Stop Scene", "Playback",
            QKeySequence(Qt::ALT | Qt::Key_P), this, [this]() {
                if (currentSpace == WindowSpaces::EDITOR)
                    onPlaySceneButton();
                else if (currentSpace == WindowSpaces::PLAYER)
                    playerView->onPlayScene();
            });

    // ---- snapping (SnapSettings, EDITOR_SHORTCUTS_SPEC §4) ----
    reg.add("snap.decrease", "Decrease Snap / Grid Size", "Snapping", QKeySequence(Qt::Key_BracketLeft),
            this, [this]() { stepSnapSize(-1); });
    reg.add("snap.increase", "Increase Snap / Grid Size", "Snapping", QKeySequence(Qt::Key_BracketRight),
            this, [this]() { stepSnapSize(+1); });
    reg.add("snap.floor", "Snap Selection To Floor", "Snapping", QKeySequence(Qt::Key_End), this,
            [this]() { if (currentSpace == WindowSpaces::EDITOR) sceneView->snapSelectionToFloor(); });
    reg.addFixed("snap.relative", "Snap While Dragging", "Snapping", "Ctrl (hold)");
    reg.addFixed("snap.altdrag", "Duplicate While Dragging", "Snapping", "Alt + drag gizmo");
    reg.addFixed("snap.vertex", "Snap To Vertex", "Snapping", "V (hold) while moving");

    // ---- editing ----
    // Ctrl+Z/Ctrl+Shift+Z had been DEAD since the menubar went away: the .ui's
    // actionEditUndo/actionEditRedo carried the QKeySequence but were attached
    // to no widget, so the shortcut never fired (the toolbar buttons were the
    // only working trigger). Registered here like every other binding.
    // Redo is explicit Ctrl+Shift+Z — QKeySequence::Redo's Ctrl+Y alternate
    // would collide with view.bottom.
    reg.add("edit.undo", "Undo", "Editing", QKeySequence(Qt::CTRL | Qt::Key_Z), this,
            [this]() { undo(); updateWindowTitle(); });
    reg.add("edit.redo", "Redo", "Editing", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), this,
            [this]() { redo(); updateWindowTitle(); });

    // ---- file / windows ----
    reg.add("file.save", "Save Scene", "File", QKeySequence(Qt::CTRL | Qt::Key_S), this,
            [this]() { saveScene(); });
    reg.add("console.toggle", "Script Console", "Windows",
            QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft), this, [this]() {
                if (scriptConsoleDock) scriptConsoleDock->setVisible(!scriptConsoleDock->isVisible());
            });
    reg.add("space.desktop", "Desktop Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_1), this,
            [this]() { this->switchSpace(WindowSpaces::DESKTOP); });
    reg.add("space.player", "Player Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_2), this,
            [this]() { if (projectService->isSceneOpen()) this->switchSpace(WindowSpaces::PLAYER); });
    reg.add("space.editor", "Editor Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_3), this,
            [this]() { if (projectService->isSceneOpen()) this->switchSpace(WindowSpaces::EDITOR); });
    reg.add("space.effects", "Effects Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_4), this,
            [this]() { this->switchSpace(WindowSpaces::EFFECT); });
    reg.add("space.assets", "Assets Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_5), this,
            [this]() { this->switchSpace(WindowSpaces::ASSETS); });
    reg.add("space.previous", "Previous Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_Tab), this,
            [this]() {
                if ((previousSpace == WindowSpaces::PLAYER || previousSpace == WindowSpaces::EDITOR) &&
                    !projectService->isSceneOpen())
                    return;
                this->switchSpace(previousSpace);
            });
}

// [ / ]: steps the ACTIVE gizmo's snap size through its step list — the
// translate size is also the ground grid's spacing, which re-spaces live.
// A toast over the viewport shows the new value (EDITOR_SHORTCUTS_SPEC §4).
void MainWindow::stepSnapSize(int direction)
{
    if (currentSpace != WindowSpaces::EDITOR) return;
    const QString mode = sceneView->gizmoMode();
    QString text;
    if (mode == "rotate") {
        SnapSettings::setRotateSize(SnapSettings::stepped(SnapSettings::rotateSteps(),
                                                          SnapSettings::rotateSize(), direction));
        text = QString("Rotate snap: %1\xc2\xb0").arg(double(SnapSettings::rotateSize()));
    } else if (mode == "scale") {
        SnapSettings::setScaleSize(SnapSettings::stepped(SnapSettings::scaleSteps(),
                                                         SnapSettings::scaleSize(), direction));
        text = QString("Scale snap: %1").arg(double(SnapSettings::scaleSize()));
    } else {
        SnapSettings::setTranslateSize(SnapSettings::stepped(SnapSettings::translateSteps(),
                                                             SnapSettings::translateSize(), direction));
        text = QString("Move / grid snap: %1").arg(double(SnapSettings::translateSize()));
    }
    if (!snapToast) snapToast = new Toast(this);
    snapToast->showToast("Snap Size", text, 0, QPoint(), QRect());   // auto-hides
    snapToast->adjustSize();
    QWidget *vp = sceneView->asWidget();
    const QPoint top = vp->mapToGlobal(QPoint(vp->width() / 2, 24));
    snapToast->move(top - QPoint(snapToast->width() / 2, 0));
}

// Space: translate -> rotate -> scale -> translate (Unreal's mode cycle).
// Routed through the same slots the toolbar uses so the checked states follow.
void MainWindow::cycleGizmoMode()
{
    const QString mode = sceneView->gizmoMode();
    if (mode == "translate")   rotateGizmo();
    else if (mode == "rotate") scaleGizmo();
    else                       translateGizmo();
}

void MainWindow::toggleDockWidgets()
{
	QDialog *d = new QDialog(this);
	d->setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::Popup);

	d->setStyleSheet(StyleSheet::DockToggleDialog());

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
	if (projectService->isSceneOpen() || !!scene) {
		scene->setOutlineWidth(prefsDialog->worldSettings->outlineWidth);
		scene->setOutlineColor(prefsDialog->worldSettings->outlineColor);
	}

	actionSaveScene->setVisible(!prefsDialog->worldSettings->autoSave);
}

void MainWindow::undo()
{
    undoService->undo();
}

void MainWindow::updateWindowTitle()
{
    // (was UiManager::updateWindowTitle — window chrome belongs to the shell)
    setWindowTitle(QString("%1 - %2").arg(originalTitle).arg(project->getProjectName()));
}

void MainWindow::redo()
{
    undoService->redo();
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

void MainWindow::toggleGrid(bool state)
{
    if (sceneView) sceneView->setShowGrid(state);
}

// F11: immersive fullscreen — the window goes fullscreen and (in the editor
// space) the docks and toolbar hide; a second F11 restores exactly what was
// visible before (EDITOR_SHORTCUTS_SPEC §3).
void MainWindow::toggleImmersiveFullscreen()
{
    QWidget *editorDocks[] = { sceneHierarchyDock, sceneNodePropertiesDock, presetsDock,
                               assetDock, animationDock, scriptConsoleDock, toolBar };
    if (!immersiveFullscreen) {
        immersiveFullscreen = true;
        preFullscreenMaximized = isMaximized();
        preFullscreenWidgets.clear();
        if (currentSpace == WindowSpaces::EDITOR) {
            for (QWidget *w : editorDocks) {
                preFullscreenWidgets.append(w && w->isVisible());
                if (w) w->hide();
            }
        }
        showFullScreen();
    } else {
        immersiveFullscreen = false;
        if (preFullscreenWidgets.size() == int(sizeof(editorDocks) / sizeof(editorDocks[0]))) {
            for (int i = 0; i < preFullscreenWidgets.size(); ++i)
                if (editorDocks[i]) editorDocks[i]->setVisible(preFullscreenWidgets[i]);
        }
        preFullscreenWidgets.clear();
        preFullscreenMaximized ? showMaximized() : showNormal();
    }
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
    if (undoService->isDirty()) {
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

    if (playbackService->isPlaying()) enterEditMode();
    hide();
    pmContainer->populateDesktop(true);
    pmContainer->cleanupOnClose();
}

void MainWindow::newScene()
{
    auto scene = this->createDefaultScene();
    this->setScene(scene);
    this->sceneView->resetEditorCam();
}

bool MainWindow::beginEngineSelftest(QString &why)
{
    if (!EngineHost::instance().isRunning()) {
        why = "the engine is not running (engine failed to start?)";
        return false;
    }
    // The editor page of the stacked widget; showing it gives the viewport its
    // native window, and with it the engine View and Scene.
    ui->stackedWidget->setCurrentIndex(1);
    sceneView->setWindowSpace(WindowSpaces::EDITOR);
    QCoreApplication::processEvents();
    if (!sceneView->isInitialized()) {
        why = "the engine viewport has no view after being shown";
        return false;
    }
    newScene();
    sceneView->begin();
    return true;
}

void MainWindow::endEngineSelftest()
{
    sceneView->end();
}

bool MainWindow::startMcpServer(quint16 port, QString *errorOut)
{
    if (!mcpServer) {
        if (errorOut) *errorOut = QStringLiteral("the MCP server was not created");
        return false;
    }
    QString error;
    if (!mcpServer->start(port, &error)) {
        if (errorOut) *errorOut = error;
        return false;
    }
    // The console dock shows the copyable connect line (the token lives only
    // in this session — it is never persisted).
    if (scriptConsole) {
        scriptConsole->announce(QStringLiteral("MCP server listening on http://127.0.0.1:%1/mcp")
                                    .arg(mcpServer->port()));
        scriptConsole->announce(mcpServer->connectCommand());
    }
    return true;
}

void MainWindow::newProject(const QString &filename, const QString &projectPath)
{
    if (projectService->isSceneOpen()) closeProject();

	// this is to ensure the editor's context is created
	switchSpace(WindowSpaces::EDITOR);

    newScene();
    projectService->setSceneOpen(true);
    ui->actionClose->setDisabled(false);

    saveScene(filename, projectPath);

    assetWidget->trigger();

    undoService->clear();
    updateWindowTitle();
	updateTopMenuStates(WindowSpaces::EDITOR);
}

MainWindow::~MainWindow()
{
    this->db->closeDatabase();
    // The QObject services (selection/playback/sceneEdit) are parented to the
    // window; the plain ones are deleted here.
    delete services;
    delete projectService;
    delete thumbnailService;
    delete assetService;
    delete undoService;
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
	playbackService->setSimulationRunning(!playbackService->isSimulationRunning());

    if (playbackService->isPlaying()) {
        enterEditMode();
		//playbackService->restartSimulation();
		sceneView->stopPlayingScene();
    }
    else {
        enterPlayMode();
		//playbackService->startSimulation();
		sceneView->startPlayingScene();
    }

	if (auto sel = selectedSceneNode()) sceneNodeSelected(sel);
}

void MainWindow::enterEditMode()
{
    playbackService->enterEditMode();   // chrome follows via applyEditModeUi()
}

void MainWindow::enterPlayMode()
{
    playbackService->enterPlayMode();   // chrome follows via applyPlayModeUi()
}

void MainWindow::applyEditModeUi()
{
    playSceneBtn->setText("Play Scene");
    playSceneBtn->setToolTip("Play scene");
	shaderGraph->setAssetWidgetDatabase(db);
    QVariantMap options;
    options.insert("color", QColor(46, 204, 113));
    options.insert("color-active", QColor(46, 204, 113));
    playSceneBtn->setIcon(fontIcons->icon(fa::play, options));
}

void MainWindow::applyPlayModeUi()
{
    playSceneBtn->setEnabled(true);
    playSceneBtn->setText("Stop playing");
    playSceneBtn->setToolTip("Stop playing");

    QVariantMap options;
    options.insert("color", QColor(231, 76, 60));
    options.insert("color-active", QColor(231, 76, 60));
    playSceneBtn->setIcon(fontIcons->icon(fa::stop, options));
}

bool MainWindow::applyCameraView(const QString &name)
{
    if (!sceneView || !sceneView->setCameraView(name)) return false;

    // projection icon + tooltip stay in sync (changeProjection re-applies the
    // projection the viewport already set — idempotent)
    changeProjection(name == QLatin1String("perspective"));

    for (QAction *action : viewsActions)
        action->setChecked(action->data().toString() == name);
    return true;
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
