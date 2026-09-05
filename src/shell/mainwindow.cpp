/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
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
#include "services/assetstore.h"
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

#include <QThreadPool>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

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
#include "modules/avatar/avatarmodule.h"
#include "modules/studiomodule.h"
#include "player/playerwidget.h"
#include "player/engineplayerview.h"
#include "viewport/headlesseditorviewport.h"

#include "scripting/scripthost.h"
#include "scripting/scriptengine.h"
#include "scripting/mcp/mcpserver.h"
#include "scripting/claude/claudechathost.h"
#include "scripting/claude/claudecliprobe.h"
#include "scripting/claude/claudelaunchconfig.h"
#include "ui/windows/claudechatwindow.h"
#include "ui/panels/scriptconsole.h"
#include "scripting/modules/studiomodules.h"

#include "services/services.h"
#include "services/shortcutregistry.h"
#include "services/worldmodes.h"
#include "viewport/snapsettings.h"
#include "services/subscriber.h"
#include "services/undoservice.h"
#include "services/selectionservice.h"
#include "services/playbackservice.h"
#include "services/projectservice.h"
#include "services/loadtimeline.h"
#include "services/meshbakestore.h"
#include "services/sceneopenrunner.h"
#include "services/mainthreadwatchdog.h"
#include "shell/shutdownorder.h"
#include "services/projectarchiver.h"
#include "ui/dialogs/progressdialog.h"
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

	// Every exit path funnels through aboutToQuit (window close,
	// exitApp()'s QApplication::exit, quitOnLastWindowClosed) — teardown of
	// background workers must not depend on closeEvent alone.
	connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::shutdownBackgroundWork);

	// Step 7 of the shutdown order has no code of its own: it IS ~QWidget
	// destroying this window's children. A plain QObject child records it on
	// the way out (shell/shutdownorder.h).
	new ShutdownOrder::WidgetTreeMarker(this);

	// The main-thread watchdog (STABILITY_PROGRAM_SPEC Lane 5). Started HERE,
	// from the UI thread, because the thread that starts it is the thread its
	// backtraces will be of. Dev builds only, and it stops itself in
	// shutdownBackgroundWork so a normal teardown is never photographed.
	MainThreadWatchdog::start();
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

void MainWindow::setShowFrameStats(bool on)
{
    // ONE code path for the F3 key, the View Options row, the Preferences
    // checkbox and editor.setOverlays({stats}) — and one stored value, so the
    // readout is still there after a restart (STATS_OVERLAY_SPEC §5.3).
    if (sceneView) sceneView->setShowFps(on);
    SettingsManager::getDefaultManager()->setValue("show_fps", on);
    if (statsCheckAction && statsCheckAction->isChecked() != on) {
        QSignalBlocker block(statsCheckAction);   // no toggled() round trip
        statsCheckAction->setChecked(on);
    }
}

bool MainWindow::bounceIfViewportIsDead()
{
    // THE FAILED STATE, respecced (STATS_OVERLAY_SPEC.md §6.4).
    //
    // EngineViewWidget::createView can fail — a bad handle, a Vulkan surface
    // the driver refuses, an Hlms media directory that never resolved — and it
    // then falls back to an OFFSCREEN view so the editor, the selftest and
    // scripting all keep working. What the user sees is a blank region that
    // will never draw, and viewCreationError() is the only thing anywhere that
    // knows why.
    //
    // Until D2 that reason reached them through ViewportCover's Failed state.
    // An engine-drawn cover cannot carry it, by definition: nothing will ever
    // present into that widget. So it becomes a toast plus a return to a page
    // that works. WHAT IS LOST, plainly: there is no longer a permanent
    // explanation sitting in the viewport region. The mitigation is that the
    // user cannot get BACK to a blank editor — every switchSpace(EDITOR)
    // bounces them, so the message reappears instead of a dead page.
    if (!sceneView || sceneView->viewCreationError().isEmpty()) return false;
    if (!viewErrorToast) viewErrorToast = new Toast(this);
    viewErrorToast->showToast(tr("3D view unavailable"),
                              tr("The 3D view could not be created: %1")
                                  .arg(sceneView->viewCreationError()),
                              0, QPoint(), QRect());
    viewErrorToast->adjustSize();
    viewErrorToast->move(rect().center() - QPoint(viewErrorToast->width() / 2, 0) +
                         mapToGlobal(QPoint(0, 0)) - QPoint(0, height() / 4));
    goToDesktop();
    return true;
}

iris::ScenePtr MainWindow::getScene()
{
    return scene;
}

iris::ScenePtr MainWindow::createDefaultScene()
{
    auto scene = iris::Scene::create();
    // New scenes start on EPIC (POST_CHAIN_SPEC.md §12 decision 8, owner call).
    // Applied through the registry rather than by hardcoding the values here, so
    // the tier table stays the single place any of them is written.
    worldmodes::setMode(scene, worldmodes::Mode::Epic);

    // second node
    auto node = iris::MeshNode::create();
    node->setMesh(":/models/ground.obj");
    node->setLocalPos(iris::Vec3(0, 1e-4, 0)); // prevent z-fighting with the default plane reset (iKlsR)
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
	QPixmap::fromImage(thumb->thumb).save(&buffer, "PNG");

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
    dlight->setLocalPos(iris::Vec3(4, 4, 0));
    dlight->setLocalRot(iris::Quat::fromEulerAngles(15, 0, 0));
    dlight->intensity = 1;
    dlight->icon = iris::Texture2D::load(":/icons/light.png");

    auto plight = iris::LightNode::create();
    plight->setLightType(iris::LightType::Point);
    scene->rootNode->addChild(plight);
    plight->setName("Point Light");
    plight->setLocalPos(iris::Vec3(-4, 4, 0));
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
	// An open IN FLIGHT is finished first (services/sceneopenrunner.h). Its
	// slices are short and waitForDone pumps the loop that runs them, so this
	// costs at most the rest of one open — and it is what makes the decision
	// below coherent: closing halfway through an install found sceneOpen still
	// false and a dirty undo stack, and asked the user to save a document that
	// was not built yet (a modal QMessageBox that then swallowed the quit and
	// left the process alive — the import.shutdown zombie, wearing a different
	// hat). Re-entrancy is guarded: the pump can deliver another close.
	static bool sSettlingOpen = false;
	if (!sSettlingOpen && isOpeningProject()) {
		sSettlingOpen = true;
		openRunner->waitForDone(5000);
		if (openRunner->isRunning()) openRunner->requestAbort();
		sSettlingOpen = false;
	}

    bool closing = false;
	bool autoSave = settings->getValue("auto_save", true).toBool();

	if (autoSave && projectService->isSceneOpen()) {
		saveScene();
		closing = true;
		event->accept();
	}
	else {
		// `isSceneOpen()` is part of the CONDITION, not just the branch above
		// it (2026-09-04, found by app.watchdog_stall): with no project open
		// the undo stack is still dirty — the editor's default scene put
		// entries there — so this asked the user to save a document that does
		// not exist, with a modal QMessageBox that swallowed the quit and left
		// the process alive — the same zombie the in-flight-open settle at the
		// top of this function was written for, in a second guise. Nothing to
		// save means nothing to ask.
		if (undoService->isDirty() && !undoService->savedCountMatchesCurrent()
		    && projectService->isSceneOpen()) {
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

	// STEP 1 of the shutdown order (the whole sequence is documented in one
	// place, at ~MainWindow, and enumerated in shell/shutdownorder.h). Recorded
	// HERE, past the Cancel branch above: a close the user backed out of is not
	// a shutdown.
	JAH_SHUTDOWN_STEP(ShutdownOrder::CloseEvent, "closeEvent: autosave + settings");

	settings->setValue("geometry", saveGeometry());
	settings->setValue("windowState", saveState());

    // Orderly teardown BEFORE the window disappears: dialogs close with a
    // window still on screen, and a mid-flight import batch is aborted and
    // joined while the event loop can still service its commit hop. (Also
    // wired to aboutToQuit for the exitApp()/QApplication::exit path.)
    shutdownBackgroundWork();
}

void MainWindow::shutdownBackgroundWork()
{
    // Idempotent: closeEvent AND aboutToQuit both land here.
    static bool sDone = false;
    if (sDone) return;
    sDone = true;

    // STEP 2 of the shutdown order (see ~MainWindow / shell/shutdownorder.h).
    JAH_SHUTDOWN_STEP(ShutdownOrder::BackgroundWork, "shutdownBackgroundWork: workers joined");

    // The main-thread watchdog goes FIRST. A teardown that takes two seconds
    // is normal — the joins below are bounded at 3 s each on purpose — and a
    // watchdog left running would photograph a perfectly healthy shutdown and
    // deliver a signal into the middle of it. (Not to be confused with the 20 s
    // force-exit thread started a few lines down: that one IS the shutdown
    // watchdog. STABILITY_PROGRAM_SPEC §3 item 10.)
    MainThreadWatchdog::stop();

    // A worker that will not die must never zombify the process: from here
    // the whole teardown is bounded. If anything below (or Qt's/Ogre's own
    // destruction) wedges, log and force the exit — better a logged forced
    // exit than a headless process orphaning a "loading" dialog.
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::seconds(20));
        qWarning("shutdown watchdog: teardown exceeded 20s — forcing process exit");
        std::fflush(nullptr);
        std::_Exit(0);
    }).detach();

    // The lazy mesh-bake queue: nothing left to schedule. A bake already in
    // flight writes into its own QTemporaryDir and is discarded on arrival
    // (its completion hop is a no-op once cancelled).
    MeshBakeStore::cancelPendingBakes();

    // Import pipeline: abort batches, join workers (bounded), close the
    // progress dialogs, drop viewer-tail queues.
    bool workersStopped = true;
    // The open runner: abandon whatever is left and join its parse worker.
    if (openRunner) {
        openRunner->requestAbort();
        workersStopped &= openRunner->waitForDone(3000);
    }
    if (assetWidget) workersStopped &= assetWidget->shutdownImports(3000);
    if (_assetView) workersStopped &= _assetView->shutdownImports(3000);

    // Archive export/import (STABILITY_PROGRAM_SPEC Lane 4): cancelled and
    // joined, bounded, exactly like the import batches. Every live archiver —
    // this window's exporter and the project page's importer — is covered by
    // the one static call.
    workersStopped &= ProjectArchiver::shutdownArchives(3000);

    // The MCP endpoint must not accept requests into a half-torn-down app.
    if (mcpServer) mcpServer->stop();

    // The Claude chat subprocess: closes stdin, waits briefly, kills.
    if (claudeChatHost) claudeChatHost->shutdown();

    ThumbnailGenerator::getSingleton()->shutdown();

    // Reap the remaining pool workers (metadata/peaks/bake futures) so
    // QThreadPool's exit-time wait finds an empty pool.
    workersStopped &= QThreadPool::globalInstance()->waitForDone(3000);

    if (!workersStopped) {
        // A worker outlived its abort window. Continuing would run the rest
        // of Qt teardown (window + services destroyed, DB closed, engine
        // released) UNDER a thread still using those objects — an exit-time
        // crash, and the settings are already saved by now. Stop here, on
        // purpose and on the record: a logged forced exit beats both a
        // zombie and a crash.
        qWarning("shutdown: background workers did not stop in time — forcing a clean "
                 "process exit now (settings are saved; no teardown race)");
        std::fflush(nullptr);
        std::_Exit(0);
    }

    shutdownModules();
}

void MainWindow::shutdownModules()
{
    // STEP 3 of the shutdown order (see ~MainWindow / shell/shutdownorder.h).
    //
    // StudioModule::shutdown() is part of the module contract and had ZERO
    // call sites (deep audit 2026-09, area 1): the avatar module's documented
    // guarantee — "only the document model is ours, and it must go before the
    // engine does" — simply did not hold. Here is the place where it does:
    // after the workers are joined and BEFORE EngineHost::shutdown() (step 4),
    // so a module still sees a live engine while it lets go of it.
    //
    // The module OBJECTS are deleted in ~MainWindow, not here: a module's page
    // is still in the stacked widget at this point and the destructor order of
    // the two must stay the Qt one.
    JAH_SHUTDOWN_STEP(ShutdownOrder::Modules, "modules shut down");
    for (auto *module : modules)
        if (module) module->shutdown();
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

    // Library lock (ASSET_PIPELINE preflight §6.2): held for the app's
    // lifetime so store migration tools can refuse while any instance runs.
    // Non-fatal — a second instance simply runs without the lock, as before.
    LibraryLock::acquire(path);

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


    // All four go through the space-routing entry points, like the registry
    // shortcut — one rule, one place (undoActiveSpace).
    connect(ui->actionUndo, &QAction::triggered, [this]() { undoActiveSpace(); });
    connect(ui->actionEditUndo, &QAction::triggered, [this]() { undoActiveSpace(); });

    // (shortcut moved to ShortcutRegistry "edit.undo" — this action is not
    // attached to any widget, so a QKeySequence here never fired anyway)

    connect(ui->actionRedo, &QAction::triggered, [this]() { redoActiveSpace(); });
    connect(ui->actionEditRedo, &QAction::triggered, [this]() { redoActiveSpace(); });

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
			// The on-screen View could not be created at all: nothing will
			// ever present into this page, so say why and go back to one that
			// works, rather than leaving the user on a permanent blank
			// (STATS_OVERLAY_SPEC.md §6.4 — this is where ViewportCover's
			// Failed state went). `return`, not `break`: goToDesktop has
			// already run a whole switchSpace(DESKTOP) inside that call, so
			// falling through to this one's trailing
			// updateTopMenuStates(EDITOR) would dress the menus for a page
			// nobody is looking at.
			if (bounceIfViewportIsDead()) return;
			// The viewport's native window has just been mapped. Until the
			// engine presents into it the X server shows whatever was on that
			// part of the screen before — the page we just left. Present the
			// cover NOW (synchronously; a queued driver tick would arrive
			// after the rest of this open) unless the engine already owns
			// those pixels.
			sceneView->coverIfNotPresenting();
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

		case WindowSpaces::AVATAR: {
			ui->stackedWidget->setCurrentIndex(6);
			ui->stackedWidget->currentWidget()->setFocus();
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

	// publish_menu is an ICON in the right cluster — the text-menu sheets set a
	// 14px font that halves the glyph. It keeps the help-button styling always;
	// active-space feedback comes from the page itself.
	publish_menu->setStyleSheet(StyleSheet::HelpButton());
	publish_menu->setCursor(Qt::PointingHandCursor);

	avatar_menu->setStyleSheet(activeSpace == WindowSpaces::AVATAR ? selectedMenu : unselectedMenu);
	avatar_menu->setCursor(Qt::PointingHandCursor);

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
			{ effect_menu, WindowSpaces::EFFECT },
			// publish_menu is NOT here: it is an icon with its own sheet above.
			{ avatar_menu, WindowSpaces::AVATAR },
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
		if (ThemeManager::classicActive()) {
			editor_menu->setStyleSheet(disabledMenu);
			player_menu->setStyleSheet(disabledMenu);
		} else {
			// TopMenuDisabled() is neutralized under Qlementine and its
			// disabled rendering on the near-black header still reads white —
			// grey the labels explicitly (owner, 2026-09-03).
			static const QString qlemDisabled =
				QStringLiteral("QPushButton { color: #63676d; }");
			editor_menu->setStyleSheet(qlemDisabled);
			player_menu->setStyleSheet(qlemDisabled);
		}
	}
}

void MainWindow::saveScene(const QString &filename, const QString &projectPath)
{
	Q_UNUSED(filename);
	projectService->saveInitialScene(projectPath);
}

bool MainWindow::startInteractiveImport(const QStringList &files)
{
    if (!assetWidget) return false;
    return assetWidget->importFiles(files);
}

bool MainWindow::saveProjectBlob()
{
	return projectService->saveProjectBlob();
}

void MainWindow::saveScene()
{
	projectService->saveOpenScene();
}

// ---- the open, in stages ---------------------------------------------------
//
// ORDER MATTERS HERE (the viewport desktop-bleed defect, 2026-09-03).
// Everything that can be done before the page switch IS done before it: the
// document read, the session registrations (the project panel did those
// already) and the viewport's scene binding all happen while the desktop page
// — with its progress dialog — is still what the user sees. The page switch is
// the LAST step, and even then the engine has not put a frame of this world on
// screen yet, so the viewport wears its loading cover until it has — an
// overlay the ENGINE draws into the frame it was going to present anyway
// (irisgl/engine/src/OgreOverlayHud.cpp; owner decision D2). Without a cover,
// the viewport's native window shows whatever pixels were on that part of the
// screen before it was mapped: a copy of the desktop page.
//
// The stages are separate functions because the THREADED open
// (openProjectAsync / services/sceneopenrunner.h) runs each of them on its own
// event-loop turn. The synchronous open below calls exactly the same four, in
// exactly the same order, back to back — that is what keeps `project.open()`
// and every headless script behaving as they always did.

void MainWindow::openStageBegin()
{
	// The bake cache window (MESH_BAKE_SPEC phase 1): while it is open, the
	// scene reader and the session registrations share ONE deserialized model
	// per source file. openStageReveal closes it, so nothing is retained
	// between opens.
	MeshBakeStore::endScope();      // idempotent: an abandoned open's scope
	MeshBakeStore::beginScope();

	// The cover goes up FIRST, before any teardown: opening a world from
	// inside the editor (load in place) must not leave the previous world on
	// screen while this one loads.
	LoadTimeline::mark(QStringLiteral("cover+teardown"));
	sceneView->beginSceneLoad(project ? project->getProjectName() : QString());

	if (!!scene)
		removeScene();

	updateWindowTitle();
}

void MainWindow::openStageRead(const iris::MeshPrewarmPtr &prewarm)
{
	LoadTimeline::mark(QStringLiteral("readProjectScene"));
	iris::PostProcessManagerPtr postMan;
	openPendingEditorData = Q_NULLPTR;
	openPendingScene = projectService->readProjectScene(&openPendingEditorData, postMan, prewarm);
}

void MainWindow::openStageBind(bool playMode)
{
	LoadTimeline::mark(QStringLiteral("setScene"));
	auto scene = openPendingScene;
	EditorData *editorData = openPendingEditorData;
	openPendingScene.clear();
	openPendingEditorData = Q_NULLPTR;

	playbackService->setPlayerMode(playMode);
	projectService->setSceneOpen(true);
	ui->actionClose->setDisabled(false);
	setScene(scene);
	refreshClaudeChatContext();   // D1: rebind an open chat to the new project

	if (editorData != Q_NULLPTR) {
		sceneView->setEditorData(editorData);
		// needs to be done so controllers can have the correct
		// camera
		playerView->setScene(scene);
		wireCheckAction->setChecked(editorData->showLightWires);
		gridCheckAction->setChecked(editorData->showGrid);
		physicsCheckAction->setChecked(editorData->showDebugDrawFlags);
	}
}

void MainWindow::openStageReadDocument(bool playMode, const iris::MeshPrewarmPtr &prewarm)
{
	openStageRead(prewarm);
	openStageBind(playMode);
}

void MainWindow::openStagePanels()
{
	LoadTimeline::mark(QStringLiteral("assetWidget.trigger"));
	assetWidget->trigger();
	undoService->resetSavedCount();
}

void MainWindow::openStageReveal(bool playMode)
{
	LoadTimeline::mark(QStringLiteral("switchSpace"));
	playMode ? switchSpace(WindowSpaces::PLAYER) : switchSpace(WindowSpaces::EDITOR);
	// A SCENE OPEN into a broken view must bounce too, not just a manual space
	// switch (STATS_OVERLAY_SPEC.md §6.4). switchSpace(EDITOR) has already run
	// the same check and taken us to the Desktop; this stops the rest of the
	// reveal from re-selecting nodes and re-enabling toolbars for a page nobody
	// is on. Harmless in the PLAYER case: the check reads the editor viewport,
	// which is equally dead either way.
	if (!sceneView->viewCreationError().isEmpty()) return;
	updateTopMenuStates(playbackService->isPlayerMode() ? WindowSpaces::PLAYER : WindowSpaces::EDITOR);

	LoadTimeline::mark(QStringLiteral("selectRoot"));
	// highlight root node
	if (!!scene) {
		sceneHierarchyWidget->selectNode(scene->getRootNode()->getGUID());
		sceneNodePropertiesWidget->setSceneNode(scene->getRootNode());
	}

	// autoplay scenes immediately
	if (playMode) {
		playBtn->setToolTip("Pause the scene");
		playBtn->setIcon(QIcon(":/icons/g_pause.svg"));
		playbackService->playScene();
		playerView->onPlayScene();
	}

	// force a refresh
	this->update();
	MeshBakeStore::endScope();
	LoadTimeline::end();

	// LAZY RE-BAKE (MESH_BAKE_SPEC phase 1, "existing libraries"). Every world
	// that arrived as an ARCHIVE — which is all five samples, and every
	// project imported before this build — has no mesh bake, so this open
	// parsed. Queue the bake now that the world is on screen: the parse and
	// the serialize run on a worker, the catalog write is one small step per
	// model, and the NEXT open of this world is a load. Nothing here can fail
	// the open; a bake that cannot be built simply never appears.
	if (projectService && pmContainer) {
		QStringList models = pmContainer->plannedSessionModelPaths();
		for (const QString &path : projectService->plannedModelPaths())
			if (!models.contains(path)) models.append(path);
		const int queued = MeshBakeStore::scheduleBakes(models);
		if (queued > 0)
			irisLog(QString("mesh bake: %1 model(s) queued for a lazy bake").arg(queued));
	}
}

void MainWindow::openProject(bool playMode)
{
	// The ledger (services/loadtimeline.h). Both open paths mark the same
	// stage names, so the synchronous open and the threaded one are directly
	// comparable in the log and in app.openTimings().
	if (!LoadTimeline::isRunning())
		LoadTimeline::begin(QStringLiteral("open(sync) %1")
		                        .arg(project ? project->getProjectName() : QString()));
	openStageBegin();
	openStageReadDocument(playMode, iris::MeshPrewarmPtr());
	openStagePanels();
	// The LAST thing before the page switch: push the whole document into the
	// renderer (meshes, materials, textures) while the desktop page is still
	// the page on screen. Whatever this costs is spent under the progress
	// dialog instead of under a viewport that has nothing to show. Skipped
	// silently on the very first open, when no render view exists yet.
	LoadTimeline::mark(QStringLiteral("primeSceneSync"));
	sceneView->primeSceneSync();
	openStageReveal(playMode);
}

bool MainWindow::isOpeningProject() const
{
	return openRunner && openRunner->isRunning();
}

void MainWindow::openProjectAsync(bool playMode)
{
	// One open at a time. A second request while one is in flight falls back
	// to the synchronous path rather than interleaving two worlds through the
	// same slices (which would tear the document apart mid-install).
	if (isOpeningProject() || !projectService || !pmContainer) {
		openProject(playMode);
		return;
	}

	if (!LoadTimeline::isRunning())
		LoadTimeline::begin(QStringLiteral("open(async) %1")
		                        .arg(project ? project->getProjectName() : QString()));

	// The cover goes up NOW, not in the first slice: the parse phase runs on
	// a worker for up to a second, and opening a world from inside the editor
	// must not leave the previous one on screen while it does. openStageBegin
	// raises it again (idempotent — it only rebases the present counter, and
	// nothing has presented in between).
	sceneView->beginSceneLoad(project ? project->getProjectName() : QString());

	// ---- plan: the DB half, here, on the thread that owns the connection ----
	LoadTimeline::mark(QStringLiteral("plan"));
	QStringList modelPaths = pmContainer->plannedSessionModelPaths();
	for (const QString &path : projectService->plannedModelPaths())
		if (!modelPaths.contains(path)) modelPaths.append(path);

	if (!openRunner) {
		openRunner = new SceneOpenRunner(db, project, this);
		connect(openRunner, &SceneOpenRunner::progress, this,
		        [this](int percent, const QString &text) {
			        if (pmContainer) pmContainer->showOpenProgress(percent, text);
		        });
		connect(openRunner, &SceneOpenRunner::finished, this, [this](bool) {
			if (pmContainer) pmContainer->hideOpenProgress();
		});
	}

	// ---- install: UI-thread slices, one per event-loop turn ----------------
	//
	// The session hydration is spread over SEVERAL turns: it is the one
	// install step whose cost grows with the project (49 assets in the
	// Showroom sample), and a single 200 ms slice plus an engine frame is
	// most of the responsiveness budget on its own.
	const QStringList sessionGuids = pmContainer->sessionAssetGuids();
	const int kAssetsPerSlice = 8;

	QVector<SceneOpenRunner::Slice> slices;
	slices.append({ QStringLiteral("Preparing assets…"), 45, [this]() {
		openStageBegin();
		LoadTimeline::mark(QStringLiteral("sessionRegistrations"));
		AssetManager::clearAssetList();
	} });
	for (int at = 0; at < sessionGuids.size(); at += kAssetsPerSlice) {
		const QStringList batch = sessionGuids.mid(at, kAssetsPerSlice);
		const int pct = 45 + (10 * (at + batch.size())) / qMax(1, sessionGuids.size());
		slices.append({ QStringLiteral("Preparing assets (%1 of %2)…")
		                    .arg(at + batch.size()).arg(sessionGuids.size()),
		                pct, [this, batch]() {
			pmContainer->registerSessionAssetGuids(batch, openRunner->prewarm());
		} });
	}
	slices.append({ QStringLiteral("Reading the scene…"), 60,
	                [this]() { openStageRead(openRunner->prewarm()); } });
	slices.append({ QStringLiteral("Binding the scene…"), 65,
	                [this, playMode]() { openStageBind(playMode); } });
	slices.append({ QStringLiteral("Building the asset panel…"), 70,
	                [this]() { openStagePanels(); } });
	slices.append({ QStringLiteral("Uploading geometry…"), 80, [this]() {
		LoadTimeline::mark(QStringLiteral("primeSceneSync"));
		sceneView->primeSceneGeometry();
	} });
	slices.append({ QStringLiteral("Lighting the world…"), 90, [this]() {
		LoadTimeline::mark(QStringLiteral("primeSceneEnvironment"));
		sceneView->primeSceneEnvironment();
	} });
	// SHADER_CACHE_SPEC §5: build the world's shaders while the cover is still
	// up, so the frames right after the reveal do not hitch through dozens of
	// compiles. Its own slice and its own event-loop turn, so the window keeps
	// answering while it runs.
	//
	// OFF BY DEFAULT, and the reason is a measurement, not caution. On this box,
	// open.responsive against the Showroom (worst UI-thread gap, ms):
	//
	//                        cold open        second open
	//   without this slice   1691 - 1789      439 - 476
	//   with it              1723 - 1761      646 - 736
	//
	// The cold open — the one this exists for — is UNCHANGED: those compiles
	// happened either way, and the gap there is dominated by other stages. But
	// the second open pays ~250 ms more, and the responsiveness budget the lane
	// contract pins is 500 ms with about 25 ms of headroom. That 250 ms is a
	// single renderOneFrame, which is atomic — it cannot be split across event
	// loop turns, and rendering the warm-up through a smaller target would build
	// a DIFFERENT chain's shaders, i.e. the wrong ones.
	//
	// So the capability ships and the policy does not: Preferences -> Cache
	// turns it on, editor.warmUpShaders() runs it on demand, and whether it
	// becomes the default is a decision about that 500 ms budget rather than
	// something this code should assume.
	if (settings->getValue("shader_warmup_on_open", false).toBool()) {
		slices.append({ QStringLiteral("Precompiling shaders…"), 95, [this]() {
			LoadTimeline::mark(QStringLiteral("warmUpShaders"));
			const unsigned built = sceneView->warmUpShaders();
			if (built) qInfo("scene open: precompiled %u shader(s) behind the cover", built);
		} });
	}
	slices.append({ QStringLiteral("Opening…"), 100,
	                [this, playMode]() { openStageReveal(playMode); } });

	openRunner->setPlan(modelPaths, slices,
	                    project ? project->getProjectName() : QStringLiteral("scene"));
	openRunner->start();
}

void MainWindow::closeProject()
{
    // A tile's close control can fire with no scene open (double-fired close,
    // or closing while an open never completed): every line below dereferences
    // `scene`, so the first one crashed on null (crash-1788555267.log,
    // stopPlayingAmbientMusic at offset 0x320 of a null Scene). Nothing open
    // means nothing to close.
    if (!scene) return;
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
    refreshClaudeChatContext();   // D1: an open chat loses its project

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
    { LoadTimeline::Accumulate a(QStringLiteral("setScene:viewport")); this->sceneView->setScene(scene); }
    { LoadTimeline::Accumulate a(QStringLiteral("setScene:player"));   this->playerView->setScene(scene); }
    { LoadTimeline::Accumulate a(QStringLiteral("setScene:hierarchy")); this->sceneHierarchyWidget->setScene(scene); }
    { LoadTimeline::Accumulate a(QStringLiteral("setScene:properties")); this->sceneNodePropertiesWidget->setScene(scene); }

    // interim...
    { LoadTimeline::Accumulate a(QStringLiteral("setScene:updateSettings")); updateSceneSettings(); }
}

void MainWindow::removeScene()
{
    // Scene-scoped teardown only — the engine view must survive a project
    // swap (script sessions never re-trigger the showEvent that recreates it).
    sceneView->clearScene();
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

void MainWindow::addDecal()
{
    sceneEditService->addDecal(QString());
}

void MainWindow::addEmpty()
{
    sceneEditService->addEmpty();
}

void MainWindow::addCamera()
{
    sceneEditService->addCamera();
}

void MainWindow::addViewer()
{
    sceneEditService->addViewer();
}

void MainWindow::addParticleSystem()
{
    sceneEditService->addParticleSystem(iris::ParticlePreset::Custom);
}

void MainWindow::addMesh(const QString &path, bool ignore, iris::Vec3 position)
{
    QString filename;
    if (path.isEmpty()) {
        // Built from MODEL_EXTS, like every other model dialog. The old literal
        // was wrong twice over: it listed *.3ds and *.c4d, whose assimp
        // importers are not compiled in (irisgl/CMakeLists.txt's allowlist) and
        // never were, while omitting the glb/gltf everything else accepts — and
        // it was passed in getOpenFileName's DIRECTORY parameter, so the dialog
        // had no filter at all and opened on a nonexistent path.
        QStringList patterns;
        for (const auto &ext : Constants::MODEL_EXTS) patterns << "*." + ext;
        filename = QFileDialog::getOpenFileName(this, tr("Load Mesh"), QString(),
                                                tr("Mesh Files (%1)").arg(patterns.join(' ')));
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

void MainWindow::addMaterialMesh(const QString &path, bool ignore, iris::Vec3 position, const QString &guid, const QString &assetName)
{
    sceneEditService->addMaterialMesh(path, ignore, position, guid, assetName);
}

void MainWindow::addAssetParticleSystem(bool ignore, iris::Vec3 position, QString guid, QString assetName)
{
    sceneEditService->addAssetParticleSystem(ignore, position, guid, assetName);
}

void MainWindow::addDragPlaceholder()
{
    /*
    auto node = iris::MeshNode::create();
    node->scale = iris::Vec3(.5f, .5f, .5f);
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
    if (!filePath.endsWith(".zip")) filePath += ".zip";
    if (!!scene) saveScene();

    if (archiver && archiver->isRunning()) {
        QMessageBox::information(this, tr("Export"),
                                 tr("An archive operation is already running."), QMessageBox::Ok);
        return;
    }
    if (!archiver) {
        // Parented: it dies with this window (step 5 of the shutdown order),
        // and shutdownBackgroundWork cancels + joins it before that.
        archiver = new ProjectArchiver(db, project, this);
        archiveProgress = new ProgressDialog(this);
        // SIGNAL-driven, never pumping: a pump from inside a slice re-enters
        // the loop and can destroy objects the slice is still using
        // (ProgressDialog::setPumpsEventLoop documents the scar).
        archiveProgress->setPumpsEventLoop(false);
        connect(archiveProgress, &ProgressDialog::canceled, this,
                [this]() { if (archiver) archiver->requestCancel(); });
        connect(archiver, &ProjectArchiver::progress, this,
                [this](int percent, const QString &text) {
                    if (archiveProgress) archiveProgress->setValueAndText(percent, text);
                });
        connect(archiver, &ProjectArchiver::finished, this, [this](bool canceled) {
            if (archiveProgress) archiveProgress->close();
            if (!canceled && !archiver->result().ok())
                QMessageBox::warning(this, tr("Export failed"),
                                     archiver->result().error, QMessageBox::Ok);
        });
    }

    // Pin-world archives (phase 4): catalog snapshot + manifest v2 + the
    // pinned CAS objects, through the one archive implementation the
    // project.exportArchive verb also calls — THREADED here (Lane 4), so the
    // window keeps painting while a multi-hundred-megabyte world compresses.
    if (archiveProgress) {
        archiveProgress->setLabelText(tr("Exporting scene…"));
        archiveProgress->resetCancel();
        archiveProgress->setCancelVisible(true);
        archiveProgress->setValue(0);
        archiveProgress->show();
    }
    archiver->startExport(filePath);
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
	// Publish is an icon (circle + up arrow) in the right-hand cluster, owner
	// direction 2026-09-03 — the end of the pipeline lives beside Help/Prefs,
	// not among the space tabs. Same glyph mechanism as the help button.
	publish_menu = new QPushButton;
	publish_menu->setObjectName("publish_menu");
	publish_menu->setText(QChar(static_cast<ushort>(fa::arrowcircleup)));
	publish_menu->setToolTip("Publish");
	publish_menu->setStyleSheet(StyleSheet::HelpButton());
	publish_menu->setCursor(Qt::PointingHandCursor);
	avatar_menu = new QPushButton("Avatar");
	avatar_menu->setObjectName("avatar_menu");
	avatar_menu->setCursor(Qt::PointingHandCursor);

	assets_panel = new QWidget;

	auto hl = new QHBoxLayout;
    hl->setContentsMargins(0,0,0,0);
	hl->setSpacing(12);
    hl->addWidget(worlds_menu);
    hl->addWidget(player_menu);
	hl->addWidget(editor_menu);
	hl->addWidget(effect_menu);
	hl->addWidget(assets_menu);
	// Avatar sits before Publish: Publish is the end of the pipeline and stays
	// last in the menu. This is BUTTON ORDER only — the stacked-widget indices
	// switchSpace hard-codes are unchanged (AVATAR is still appended last).
	hl->addWidget(avatar_menu);

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
        QDesktopServices::openUrl(QUrl("https://www.jahshaka.com/learn/resources/"));
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
	publish_menu->setFont(fontIcons->font(28));
	bl->addWidget(publish_menu);
	bl->addWidget(help);
	bl->addWidget(prefs);

	// The header buttons are mouse-driven chrome: keep them out of the focus
	// chain, or the theme's focus indicator rings the focused space button
	// whenever the window is active (Qlementine only hijacks the policy of
	// Strong/ClickFocus buttons, so NoFocus sticks).
	for (auto *chrome : { worlds_menu, player_menu, editor_menu, effect_menu,
	                      assets_menu, publish_menu, avatar_menu, help, prefs })
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
	connect(avatar_menu, &QPushButton::pressed, [this]() { switchSpace(WindowSpaces::AVATAR); });

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

    // The engine-drawn frame-stats readout (F3). It is in this menu because
    // this is where a user looks for viewport toggles — but it is NOT one of
    // the helpers Game View hides, and it is the only row here that persists
    // (as the `show_fps` preference, shared with the Preferences checkbox).
    statsCheckAction = new QAction(QIcon(), "Frame Stats (F3)");
    statsCheckAction->setCheckable(true);
    statsCheckAction->setChecked(
        SettingsManager::getDefaultManager()->getValue("show_fps", false).toBool());
    connect(statsCheckAction, &QAction::toggled, this,
            [this](bool on) { setShowFrameStats(on); });
    wireFramesMenu->addAction(statsCheckAction);

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

    // Camera ▾ — the switcher (CAMERAS_SPEC D4): the Viewport (explorer) plus
    // every scene camera by name. Choosing a camera PILOTS it; choosing
    // Viewport ejects. It is rebuilt on every open rather than kept in sync,
    // because the list is the document's and the document changes underneath it
    // (a camera added, renamed, deleted, a whole world closed) — and a stale
    // entry would hand the viewport a dangling node.
    camerasButton = new QToolButton;
    camerasButton->setStyleSheet("padding: 0 8px 0 0; margin: 0");
    camerasMenu = new QMenu;
    camerasMenu->setStyleSheet(StyleSheet::QMenuFlat());
    connect(camerasMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildCamerasMenu);
    camerasButton->setMenu(camerasMenu);
    camerasButton->setText("Camera ");
    camerasButton->setPopupMode(QToolButton::InstantPopup);
    camerasButton->setToolTip(tr("Render the viewport through the free explorer or a scene camera "
                                 "(choosing a camera pilots it)"));

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
    controlBarLayout->addWidget(camerasButton);
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
                                               camerasButton,
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
            // Non-owning: step 5 of the shutdown order checks it (see
            // destroyEngineViews / shell/shutdownorder.h).
            mEngineWatch = host.engine();
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
    // The persisted readout state reaches the viewport HERE, not when the menu
    // action was built: the View Options menu is constructed before sceneView
    // exists, so its initial setChecked found nothing to switch on.
    setShowFrameStats(SettingsManager::getDefaultManager()->getValue("show_fps", false).toBool());

    QGridLayout* layout = new QGridLayout;
    layout->addWidget(sceneView->asWidget(), 0, 0);
    // NO COVER WIDGET (owner decision D2). This grid cell used to hold a
    // second widget stacked over the viewport — ViewportCover — which, to be
    // visible over a native render window on X11, had to own a native window of
    // its own and raise() itself above the viewport's. The cover is now drawn
    // by the engine, inside the frame it was already presenting, so there is
    // nothing to add here and no stacking order to get wrong.
    layout->setContentsMargins(0, 0, 0, 0);
    sceneContainer->setLayout(layout);

    auto events = sceneView->events();
    connect(events, &EditorViewportEvents::addDroppedMesh, this, [this](QString path, bool v, iris::Vec3 pos, QString guid, QString name) {
        addMaterialMesh(path, v, pos, guid, name);
    });

    connect(events, &EditorViewportEvents::addPrimitive, this, [this](QString guid) {
        addPrimitiveObject(guid);
    });

    connect(events, &EditorViewportEvents::addDroppedParticleSystem, this, [this](bool v, iris::Vec3 pos, QString guid, QString name) {
        addAssetParticleSystem(v, pos, guid, name);
    });

    connect(events, &EditorViewportEvents::addDroppedImagePlane, this, [this](iris::Vec3 pos, QString guid) {
        sceneEditService->addImagePlane(guid, pos);
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
	// A pin made on the Assets page must show up in the editor's project
	// panel live (both can be open in one session) — the panel repopulates
	// from the pinned membership on every add.
	connect(_assetView, &AssetView::assetAddedToProject, this,
	        [this](const QString &) { assetWidget->refresh(); });

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
	avatarModule = new AvatarModule;
	modules = { materialsModule, publishModule, avatarModule };
	for (auto *module : modules) module->initialize(moduleHost);
	materialsModule->setAssetView(_assetView);

	shaderGraph = materialsModule->effectsPage();
	ui->stackedWidget->addWidget(materialsModule->createPage());
	ui->stackedWidget->addWidget(playerView);
	publishView = publishModule->createPage();
	ui->stackedWidget->addWidget(publishView);
	// AVATAR = stack index 6, APPENDED (R0.14: switchSpace's indices are hard-coded).
	avatarView = avatarModule->createPage();
	ui->stackedWidget->addWidget(avatarView);

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

	QAction *actionClaude = new QAction;
	actionClaude->setObjectName(QStringLiteral("actionClaudeChat"));
	actionClaude->setCheckable(false);
	actionClaude->setToolTip("Claude | Chat with Claude inside the editor (Ctrl+Shift+C)");
	actionClaude->setIcon(fontIcons->icon(fa::magic, options));
	toolBar->addAction(actionClaude);
	connect(actionClaude, &QAction::triggered, this, &MainWindow::toggleClaudeChat);

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
    // Space is page-scoped, exactly like Ctrl+Z: ONE registry claimant, routed
    // by the active space (see spaceKeyActiveSpace).
    reg.add("tool.cycle", "Cycle Gizmo Mode / Node Search", "Tools", QKeySequence(Qt::Key_Space), this,
            [this]() { spaceKeyActiveSpace(); });

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
    // Held-modifier input, like the fly keys: listed read-only, never a
    // QShortcut. Alt ON the gizmo keeps its duplicate-while-dragging meaning
    // (snap.altdrag below) — the gizmo hit-test runs first.
    reg.addFixed("camera.orbit", "Orbit Around Selection", "Camera",
                 "Alt + LMB drag (off the gizmo)");

    // ---- view ----
    reg.add("view.gameView", "Game View (hide editor helpers)", "View", QKeySequence(Qt::Key_G), this,
            [this]() {
                if (currentSpace == WindowSpaces::EDITOR)
                    sceneView->setGameView(!sceneView->isGameView());
            });
    reg.add("view.grid", "Toggle Ground Grid", "View", QKeySequence(), this,
            [this]() { if (gridCheckAction) gridCheckAction->toggle(); });
    // F3 — the games convention (Minecraft, idTech-adjacent), and the only free
    // F-key in this registry besides F11 (STATS_OVERLAY_SPEC D3). Category
    // "View" so it lands beside gameView/grid/fullscreen in the generated
    // Preferences page. Goes through the same verb path as the checkbox and
    // never a separate one — and persists, because a diagnostic you have to
    // switch on again after every restart is a diagnostic nobody uses.
    reg.add("view.stats", "Show Frame Stats", "View", QKeySequence(Qt::Key_F3), this,
            [this]() { setShowFrameStats(!sceneView->getShowFps()); });
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
    // The ONE claimant for each chord — see undoActiveSpace() for why that
    // matters and which stack each space owns.
    reg.add("edit.undo", "Undo", "Editing", QKeySequence(Qt::CTRL | Qt::Key_Z), this,
            [this]() { undoActiveSpace(); });
    reg.add("edit.redo", "Redo", "Editing", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), this,
            [this]() { redoActiveSpace(); });

    // ---- file / windows ----
    reg.add("file.save", "Save Scene", "File", QKeySequence(Qt::CTRL | Qt::Key_S), this,
            [this]() { saveScene(); });
    reg.add("console.toggle", "Script Console", "Windows",
            QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft), this, [this]() {
                if (scriptConsoleDock) scriptConsoleDock->setVisible(!scriptConsoleDock->isVisible());
            });
    reg.add("claude.toggle", "Claude Assistant", "Windows",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), this,
            [this]() { toggleClaudeChat(); });
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

// ---- Ctrl+Z / Ctrl+Shift+Z routing (deep audit 2026-09, area 1) ------------
//
// Ctrl+Z had TWO claimants whenever the Materials page was visible —
// "edit.undo" here and GraphicsView's own QShortcut, both Qt::WindowShortcut —
// so Qt dispatched the chord ambiguously and NEITHER ran: on that page undo did
// nothing at all. The graph view's pair is deleted (materials/widgets/
// graphicsview.cpp says why), leaving this the single claimant, and the owner's
// decision is that on the Materials page the GRAPH stack is the one it drives.
//
// Deliberately not a fallback: with the Materials space active, Ctrl+Z with an
// empty graph stack does NOTHING rather than quietly undoing a scene edit the
// user cannot see. Everywhere else it is exactly the editor undo it always was.

void MainWindow::undoActiveSpace()
{
    if (currentSpace == WindowSpaces::EFFECT && shaderGraph) { shaderGraph->graphUndo(); return; }
    undo();
    updateWindowTitle();
}

void MainWindow::redoActiveSpace()
{
    if (currentSpace == WindowSpaces::EFFECT && shaderGraph) { shaderGraph->graphRedo(); return; }
    redo();
    updateWindowTitle();
}

// ---- Space routing (owner decision 2026-09-05) -----------------------------
//
// Same shape as undoActiveSpace, and for the same reason: the chord keeps ONE
// registry claimant (so it stays listed and remappable in Preferences, and Qt
// never sees an ambiguous WindowShortcut), and the active space decides what it
// means. On the Materials space Space opens the node-SEARCH palette — the graph
// is the thing being edited there and there is no gizmo to cycle; everywhere
// else it is the tool cycle it has always been.
void MainWindow::spaceKeyActiveSpace()
{
    if (currentSpace == WindowSpaces::EFFECT) {
        if (shaderGraph) shaderGraph->openNodeSearch();
        return;
    }
    if (currentSpace == WindowSpaces::EDITOR) cycleGizmoMode();
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

void MainWindow::toggleClaudeChat()
{
    if (claudeChatWindow && claudeChatWindow->isVisible()) {
        claudeChatWindow->close();
        return;
    }
    if (!claudeChatHost) claudeChatHost = new ClaudeChatHost(this);
    // The model seam (AI_SURFACE_PROGRAM_SPEC owner decision): the dock pins a
    // model instead of silently inheriting the user's terminal default. The
    // setting is what a header picker will write; absent, the shipped default
    // applies, and an explicit empty string restores "inherit".
    claudeChatHost->setModel(settings->getValue("claude_model",
                                                ClaudeLaunchConfig::defaultModel()).toString());
    if (!claudeChatWindow) {
        claudeChatWindow = new ClaudeChatWindow(settings->settings, claudeChatHost, this);
        connect(claudeChatWindow, &ClaudeChatWindow::enableMcpRequested, this, [this]() {
            const quint16 port =
                quint16(settings->getValue("mcp_port", McpServer::kDefaultPort).toUInt());
            QString error;
            if (startMcpServer(port, &error)) {
                settings->setValue("mcp_enabled", true);
            } else if (scriptConsole) {
                scriptConsole->announce(QStringLiteral("MCP enable failed: %1").arg(error));
            }
            refreshClaudeChatContext();
        });
        // The one-time CLI probe (~ms when installed; renders the friendly
        // install state when not).
        claudeChatWindow->setCliState(ClaudeCliProbe::probe());
    }
    refreshClaudeChatContext();
    claudeChatWindow->show();
    claudeChatWindow->raise();
    claudeChatWindow->activateWindow();
}

// Called on every project OPEN and CLOSE as well as on toggle/enable-MCP
// (CLAUDE_EDITOR_SPEC D1): ClaudeChatHost::configure is written to rebind on a
// folder change, but nothing used to call it when the project changed, so a
// chat left open across a switch kept the previous project's cwd, MCP config
// file and session. Cheap when the chat was never opened — it returns at the
// first line.
void MainWindow::refreshClaudeChatContext()
{
    if (!claudeChatWindow || !claudeChatHost) return;
    const bool sceneOpen = projectService->isSceneOpen();
    const bool mcpRunning = mcpServer && mcpServer->isRunning();
    claudeChatWindow->setProjectOpen(sceneOpen);
    claudeChatWindow->setMcpRunning(mcpRunning);
    const QString folder = (sceneOpen && project) ? project->getProjectFolder() : QString();
    QString error;
    if (!claudeChatHost->configure(folder, mcpRunning,
                                   mcpRunning ? mcpServer->port() : 0,
                                   mcpRunning ? mcpServer->token() : QString(), &error)
        && scriptConsole && !error.isEmpty()) {
        scriptConsole->announce(QStringLiteral("Claude chat config: %1").arg(error));
    }
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
    refreshClaudeChatContext();   // D1: rebind an open chat to the new project
}

// ===========================================================================
//  THE SHUTDOWN ORDER  (STABILITY_PROGRAM_SPEC.md §1.5 / Lane 3)
//  Written down ONCE, here. shell/shutdownorder.h carries the enumeration and
//  the two incidents that paid for it; this is the code half.
//
//   1 CloseEvent        MainWindow::closeEvent — settle an in-flight open,
//                       autosave / unsaved-changes prompt, donate dialog,
//                       geometry + state to settings
//   2 BackgroundWork    MainWindow::shutdownBackgroundWork — the
//                       bounded teardown of every worker this window owns.
//                       Idempotent: closeEvent AND aboutToQuit land here
//   3 Modules           MainWindow::shutdownModules — StudioModule::shutdown()
//                       on every module, while the engine is still alive
//   4 EngineHostRelease finalizeAppExit (app/cli/scriptrunner.cpp) ->
//                       EngineHost::shutdown(): the render driver stops, the
//                       shader cache and warm-up set are written, the HOST's
//                       shared_ptr is dropped. It does NOT destroy the Engine
//   5 WindowBody        this destructor's body: undoStack->clear() first
//                       (incident 1), then the module objects, the services
//                       and the Ui:: struct
//   6 EngineViews       destroyEngineViews() — the widgets holding the last
//                       shared_ptr<Engine> are deleted HERE (incident 2), so
//                       ~OgreEngine runs with the database still open
//   7 DatabaseClosed    db->closeDatabase(), last
//   8 WidgetTree        ~QWidget(MainWindow): whatever step 6 did not reach.
//                       Nothing here may touch the database or the engine
//
//  If you add a participant, add it to shutdownorder.h's enum and to this
//  block. The app.shutdown_order gate reads the steps out of the process's
//  own output and fails when they fire twice or out of order.
// ===========================================================================

void MainWindow::destroyEngineViews()
{
    // STEP 6, and the reason it exists.
    //
    // EngineHost::shutdown() (step 4) drops the HOST's reference and stops the
    // render loop — but the Engine is a shared_ptr and four widgets hold their
    // own copies: the editor viewport (viewport/enginesceneviewport.h), the
    // player view, the Assets page's viewer, and the module previews (materials
    // Display, avatar). Every one of them lives in this window's child widget
    // tree, which Qt destroys in ~QWidget — AFTER this destructor's body, i.e.
    // after closeDatabase().
    //
    // So before this lane the Engine died at a point with no name, after the
    // database was gone, and the ENGINE TEARDOWN LAW (workspaces -> scenes ->
    // drop every MeshPtr -> delete Root) ran there. Nothing in engine teardown
    // writes to the database today, which made it latent rather than live —
    // and exactly the shape of the bug `740e0155` fixed for the undo stack one
    // level up.
    //
    // Deleting the direct child widgets here is precisely what ~QWidget would
    // do a moment later; doing it in the body just moves it in FRONT of
    // closeDatabase() and gives it a name. It is strictly safer than the old
    // order too: widgets are now destroyed while the database connection is
    // still open, not after it closed.
    //
    // QPointer, because deleting one child can delete another (a dock's
    // titlebar widget, a page's children).
    QList<QPointer<QWidget>> kids;
    for (QObject *child : children())
        if (QWidget *w = qobject_cast<QWidget *>(child)) kids.append(w);
    for (QPointer<QWidget> &w : kids)
        if (!w.isNull()) delete w.data();

    // Everything below points into that tree. Nothing runs after this except
    // closeDatabase(), but a dangling `sceneView` is the kind of thing a later
    // edit trips over.
    sceneView = nullptr;
    playerView = nullptr;
    viewPort = nullptr;
    _assetView = nullptr;

    // The Engine must be gone now. It is not an assert because a MainWindow
    // can legitimately be destroyed before finalizeAppExit ran (a CLI path
    // that returns early), in which case EngineHost still holds its reference
    // — that case is excluded, and what is left is the real finding: somebody
    // added a shared_ptr<Engine> holder that is not in this window's widget
    // tree, and the Engine is once again dying after the database closes.
    if (!EngineHost::instance().isRunning() && !mEngineWatch.expired())
        qWarning("[shutdown] step 6: the Engine is STILL referenced after the "
                 "viewports were destroyed — a holder outside MainWindow's "
                 "widget tree exists, and the engine will now be torn down "
                 "after closeDatabase(). See shell/shutdownorder.h.");
}

MainWindow::~MainWindow()
{
    JAH_SHUTDOWN_STEP(ShutdownOrder::WindowBody, "~MainWindow body");

    // ORDER IS LOAD-BEARING. Undo commands write to the database when they die
    // (DeleteSceneNodeCommand finalises the asset row once no undo can reach
    // the delete any more), and undoStack is parented to this window — so it
    // used to be destroyed AFTER this body, i.e. after closeDatabase(), and
    // every pending asset delete failed against a closed connection. Silently:
    // the SQLite driver's only complaint was "Parameter count mismatch" at
    // [info] level. Drain the stack here, while the connection is still open.
    if (undoStack) undoStack->clear();

    // The modules. They are plain heap objects the shell news up in
    // setupViewPort() and nothing ever deleted them (deep audit 2026-09,
    // area 1). shutdown() runs at step 3 on the closeEvent path — but the
    // --script / --dump-api-docs exits NEVER run steps 1-3 (no closeEvent,
    // no aboutToQuit), so it must run here too or deleting the avatar module
    // frees AvatarPreviewModel while AvatarPreviewScene still holds a raw
    // back-pointer to it: the widget tree's release() then jumps through a
    // freed std::function (the fix-wave gate's e2e.avatar SEGV, 2026-09-05).
    // shutdown() is idempotent, so the double call on the closeEvent path is
    // free. Their PAGES belong to the stacked widget and die with the tree.
    for (auto *m : modules)
        if (m) m->shutdown();
    qDeleteAll(modules);
    modules.clear();
    materialsModule = nullptr;
    publishModule = nullptr;
    avatarModule = nullptr;
    shaderGraph = nullptr;

    // The QObject services (selection/playback/sceneEdit) are parented to the
    // window; the plain ones are deleted here.
    delete services;
    delete projectService;
    delete thumbnailService;
    delete assetService;
    delete undoService;
    delete ui;

    JAH_SHUTDOWN_STEP(ShutdownOrder::EngineViews, "engine-holding widgets destroyed");
    destroyEngineViews();

    JAH_SHUTDOWN_STEP(ShutdownOrder::DatabaseClosed, "database closed");
    this->db->closeDatabase();
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

// The camera switcher's list (CAMERAS_SPEC D4). Built on every open from the
// live document; the checkmark shows what the viewport is actually rendering
// through, which is the piloted camera or the explorer.
void MainWindow::rebuildCamerasMenu()
{
    if (!camerasMenu) return;
    camerasMenu->clear();
    auto group = new QActionGroup(camerasMenu);
    group->setExclusive(true);

    const iris::CameraNodePtr piloted = sceneView ? sceneView->pilotedCamera()
                                                  : iris::CameraNodePtr();
    QAction *explorer = camerasMenu->addAction(tr("Viewport"));
    explorer->setCheckable(true);
    explorer->setChecked(piloted.isNull());
    group->addAction(explorer);
    connect(explorer, &QAction::triggered, this,
            [this]() { if (sceneView) sceneView->pilotCamera(iris::CameraNodePtr()); });

    auto scene = sceneView ? sceneView->getScene() : iris::ScenePtr();
    if (!scene || scene->cameras.isEmpty()) {
        QAction *none = camerasMenu->addAction(tr("No scene cameras"));
        none->setEnabled(false);
        return;
    }
    camerasMenu->addSeparator();
    // By NAME, and stable: a QHash's order is not, and a menu that reshuffles
    // between opens is unusable.
    QVector<iris::CameraNodePtr> cameras;
    for (const auto &cam : scene->cameras) if (cam) cameras.push_back(cam);
    std::sort(cameras.begin(), cameras.end(),
              [](const iris::CameraNodePtr &a, const iris::CameraNodePtr &b) {
                  if (a->getName() != b->getName()) return a->getName() < b->getName();
                  return a->getGUID() < b->getGUID();
              });
    for (const iris::CameraNodePtr &cam : cameras) {
        QAction *action = camerasMenu->addAction(
            cam->getName().isEmpty() ? tr("Camera") : cam->getName());
        action->setCheckable(true);
        action->setChecked(piloted == cam);
        group->addAction(action);
        const QString guid = cam->getGUID();
        connect(action, &QAction::triggered, this, [this, guid]() {
            if (!sceneView) return;
            auto sc = sceneView->getScene();
            if (!sc) return;
            if (auto target = sc->cameras.value(guid)) sceneView->pilotCamera(target);
        });
    }
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
