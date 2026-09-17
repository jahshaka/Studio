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
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/core/viewport.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/animation/keyframeset.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/materials/postprocessmanager.h"
#include "irisgl/core/logger.h"
#include "services/jahlog.h"
#include "services/sessionmarkers.h"
#include "services/editgate.h"
#include "services/framemonitor.h"
#include "services/perfsampler.h"

#include "data/guidmanager.h"
#include "services/thumbnailmanager.h"
#include "ui/dialogs/ogrepreviewdialog.h"
#include "bridge/enginehost.h"
#include "viewport/enginerenderdriver.h"
#include "bridge/enginematerialpreview.h"
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
#include <QScreen>
#include <QHash>
#include <QHashIterator>
#include <QDirIterator>
#include <QDockWidget>
#include <QTabBar>
#include <QFileDialog>
#include <QTemporaryDir>

#include <QTreeWidgetItem>

#include <QPushButton>
#include <QTimer>
#include <QtConcurrent>
#include <QFuture>
#include <QThread>
#include <atomic>
#include <math.h>
#include <QDesktopServices>
#include <QShortcut>
#include <QToolButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QAbstractSpinBox>

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
#include "ui/controls/propertiestabstrip.h"
#include "ui/panels/propertywidgets/worldpropertywidget.h"

#include "ui/panels/presets/skypresets.h"

#include "ui/panels/presets/assetmodelpanel.h"
#include "ui/panels/presets/assetmaterialpanel.h"

#include "ui/pages/assetview.h"
#include "ui/dialogs/toast.h"
#include "ui/controls/sceneissuebar.h"
#include "services/sceneissues.h"

#include "zip.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/input/inputmap.h"
#include "irisgl/thirdparty/bullet3/src/btBulletDynamicsCommon.h"

#include "modules/materials/effectspage.h"
#include "modules/materials/materialsmodule.h"
#include "modules/publish/publishmodule.h"
#include "modules/avatar/avatarmodule.h"
#include "modules/vr/vrmodule.h"
#include "modules/avatar/api/avatarapi.h"
#include "player/playermodule.h"
#include "services/playerservice.h"
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
#include "viewport/flyspeedsettings.h"
#include "services/subscriber.h"
#include "services/undoservice.h"
#include "services/selectionservice.h"
#include "services/playbackservice.h"
#include "services/projectservice.h"
#include "services/framepacing.h"
#include "services/outlinesettings.h"
#include "services/loadtimeline.h"
#include "services/meshbakestore.h"
#include "services/sceneopenrunner.h"
#include "services/mainthreadwatchdog.h"
#include "services/apppaths.h"
#include "shell/dockstate.h"
#include "shell/shutdownorder.h"

/// Where the EDITOR docks' layout lives. Deliberately not "windowState": that
/// key is the OUTER window's, written by QMainWindow::saveState, and the two
/// blobs describe two different QMainWindows.
static const char *kViewportDockStateKey = "viewportDockState";
#include "services/projectarchiver.h"
#include "services/sceneextents.h"
#include "ui/dialogs/progressdialog.h"
#include "services/sceneeditservice.h"
#include "services/clipboardservice.h"
#include "services/thumbnailservice.h"
#include "services/assetservice.h"
#include "services/defaultfloor.h"
#include "ui/style/stylesheet.h"
#include "ui/style/thememanager.h"
#include "ui/style/themeroles.h"
#include "ui/style/columnedpage.h"
#include "ui/style/panelmetrics.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    // The one live Project instance (Phase 4: was Globals::project, a static
    // initialised with the same call). Must exist before any setup*() runs —
    // ProjectManager reads it during construction.
    project = Project::createNew();

    ui->setupUi(this);
    // The root sheet mainwindow.ui used to embed (5 KB of classic CSS that,
    // under Qlementine, put QStyleSheetStyle over the WHOLE window — docks,
    // tabs, scrollbars, menus, every label). Classic-only now; the header's
    // near-black band is a palette role under Qlementine.
    setStyleSheet(StyleSheet::MainWindowRoot());
    ThemeRoles::setSurface(ui->header, ThemeRoles::Surface::Header);

	settings = SettingsManager::getDefaultManager();
	SnapSettings::bindSettings(settings->settings);   // snap sizes persist beside the shortcuts
	FlySpeedSettings::bindSettings(settings->settings);   // and the camera fly speeds


    // F-S3: the theme owns the window's font (see ThemeManager::applyWindowFont
    // for why the old device-pixel-ratio multiply is gone).
    ThemeManager::applyWindowFont(this);

    // The legacy iris::Logger file now lives UNDER THE SESSION-LOG ROOT with
    // everything else (SESSION_LOG_SPEC §6). Two things changed:
    //   * the release build no longer writes ~/Documents/jahshaka.log — a
    //     user-visible file in a user-owned folder, for a developer artifact;
    //   * both builds land beside the session log, so "send me your logs" is
    //     one directory.
    // The records themselves are already in the session file under `legacy`
    // (fork F5-A, absorbed at the sink); this file survives for one release so
    // nothing that greps it breaks on the same day.
    {
        const QString logDir = JahLog::paths().value(QStringLiteral("dir")).toString();
        const QString legacyLog =
            logDir.isEmpty() ? IrisUtils::getAbsoluteAssetPath("jahshaka.log")
                             : QDir(logDir).filePath(QStringLiteral("jahshaka.log"));
        iris::Logger::getSingleton()->init(legacyLog);
    }
#ifdef QT_DEBUG
    setWindowTitle(QString("Jahshaka %1 - %2").arg(Constants::CONTENT_VERSION).arg("Developer Build"));
#else
	setWindowTitle(QString("Jahshaka %1").arg(Constants::CONTENT_VERSION));
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
		// "READY" MEANS "CAN RENDER", which is not the same as "exists" any
		// more: since the scene-graph swap a --headless run HAS an engine (the
		// document graph lives in it) and it is the NULL render system, which
		// draws nothing and refuses every View. Verbs guarded by requireEngine()
		// — screenshots, thumbnails, anything reading pixels — must refuse in
		// those runs exactly as they did before an engine existed there at all,
		// and assets.import must go on skipping thumbnail generation.
		auto &engineHost = EngineHost::instance();
		if (!engineHost.isRunning() || engineHost.engine()->isHeadless()) return false;
		return sceneView->isInitialized();
	};
	// THE RUN'S DATABASE SCOPE (CLOSE-2 item 1). A script run is one undo
	// macro, which is the gesture boundary the library writes want too:
	// without this, every scene.addPrimitive in a loop autocommitted its asset
	// row on its own (journal, write, fdatasync, unlink — 300 primitives paid
	// it 300+ times, on the UI thread). The undo half of this hook is gone
	// (round 2, C1): UndoService answers "is a run in progress?" from the run
	// macro it already arms, rather than from a second flag set here. The
	// project verbs close and reopen the scope at a project boundary
	// (ScriptHost::endRunUndoMacro), which is what keeps a transaction from
	// spanning two projects.
	scriptHost->macroOpenChanged = [this](bool open) {
		if (!db) return;
		if (open) db->beginBatch();
		else      db->endBatch();
	};
	// THE RENDER LOOP FOR THE LENGTH OF A RUN (SCRIPTING_LIVE_SPEC §3.1). A
	// script runs off the UI thread now, so the driver's timer WOULD fire
	// between its verbs — which is the live feedback a person wants and the
	// thing a frame-stepping test must not have. Off suspends the tick for the
	// run's duration; Live paces it to one frame per display period (round 2,
	// H1 — unpaced, the loop and the script alternate one frame per verb).
	scriptHost->scriptRunState = [](ScriptRunState state) {
		EngineRenderDriver *driver = EngineHost::instance().driver();
		if (!driver) return;
		switch (state) {
		case ScriptRunState::None: driver->setScriptRun(EngineRenderDriver::ScriptRun::None); break;
		case ScriptRunState::Off:  driver->setScriptRun(EngineRenderDriver::ScriptRun::Off);  break;
		case ScriptRunState::Live: driver->setScriptRun(EngineRenderDriver::ScriptRun::Live); break;
		}
	};
	// The run's one undo entry, ARMED here and created by the first command
	// that lands (UndoService::push) — a query script must leave the stack
	// alone (hygiene lane, 2026-09-09).
	scriptHost->beginUndoMacro = [this](const QString &text) { undoService->beginScriptMacro(text); };
	scriptHost->endUndoMacro = [this]() { undoService->endScriptMacro(); };
	// THE RUN'S ONE NOTICE (owner, ledger §423: "a 'script running' toast
	// later would be cool"). While a script runs the editor is non-editable —
	// the gate refuses every document write coming from the UI — and a refusal
	// with nothing said reads as a frozen app. Raised by the gate the FIRST
	// time an edit is refused in a run and not again until the next one: a
	// drag is dozens of refused events and a toast per event is its own bug.
	//
	// Anchored to the WINDOW, not the viewport: an edit can be refused from
	// any page (a properties row, the hierarchy, the timeline), and the
	// viewport anchor is where gesture feedback lives.
	editgate::setNoticeHook([this]() {
		if (!snapToast) snapToast = new Toast(this);
		snapToast->setAnchor(Toast::Anchor::WindowBottom);
		snapToast->showToast(tr("Script running"),
		                     tr("The editor is read-only until the script finishes. "
		                        "You can still look around, select and switch pages."));
		// AND THE CONTROL SNAPS BACK (round 2, item 3). A refused row keeps the
		// value the user dragged or typed while the document still holds the
		// old one — two different numbers on screen, and nothing to correct
		// them. Re-read the panel from the document so the row shows the truth
		// while the toast is up saying why. Deferred (refreshFromDocument
		// defers by itself; the transform rows do it here) because this is
		// reached from inside a control's own signal handler.
		refreshPropertiesFromDocument();
	});
	scriptEngine = new ScriptEngine(*scriptHost, this);
	// ...AND AGAIN WHEN THE RUN ENDS, for the rows that were refused later in
	// the run, after the one notice had already been raised.
	connect(scriptEngine, &ScriptEngine::runningChanged, this, [this](bool running) {
		if (!running) refreshPropertiesFromDocument();
	});
	// LIVE SCRIPT FEEDBACK, as the user left it (Preferences > Scripting,
	// app.scriptPolicy). Live is the default: a person or an agent driving the
	// editor should see it work.
	scriptEngine->setInteractivePolicy(
		SettingsManager::getDefaultManager()->getValue("script_feedback_live", true).toBool()
			? ScriptRunPolicy::Live : ScriptRunPolicy::Off);
	if (prefsDialog) prefsDialog->wireScripting(scriptEngine);
	registerStudioModules(*scriptEngine);
	for (auto *module : modules) module->registerApi(*scriptEngine);

	// THE CONSOLE IS THE BOTTOM AREA'S THIRD TAB (owner, 2026-09-14, lane
	// SPACE-2). Its DOCK is built in setupDockWidgets — it has to exist before
	// the saved layout is restored there, or a blob that names it leaves Qt
	// guessing at the whole bottom area — and the console widget, which needs
	// the script engine, arrives here. The dock is closed until Ctrl+` (or
	// editor.tray) asks for it, and it is tabified with Assets and the
	// Timeline, so asking for it adds a tab rather than splitting the area
	// (the thing the owner rejected at smoke S1).
	scriptConsole = new ScriptConsole(scriptEngine);
	if (scriptConsoleDock) scriptConsoleDock->setWidget(scriptConsole);

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

	// A FRESH PROFILE MUST STILL FIT ON THE SCREEN (hygiene lane, 2026-09-09).
	//
	// restoreGeometry() returns false when there is nothing stored — a first
	// run, and every hermetic JAHSHAKA_DATA_ROOT session — and the window then
	// keeps the size authored in mainwindow.ui: 1612x1530, TALLER THAN A 1080p
	// DESKTOP. Under a window manager that is merely rude; under none (the Xvfb
	// rig) nothing ever clamps it, so everything along the bottom edge — the
	// Materials palette lives there — is off the screen and unreachable, which
	// is what mis-aimed app.input_keys's palette drag.
	//
	// The .ui size stays the PREFERRED one: only the screen shrinks it.
	if (!restoreGeometry(settings->getValue("geometry", "").toByteArray()))
		fitToScreen();
	restoreState(settings->getValue("windowState", "").toByteArray());

	// Every exit path funnels through aboutToQuit (a window close,
	// QApplication::exit/quit from the CLI runners, quitOnLastWindowClosed) —
	// teardown of background workers must not depend on closeEvent alone.
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
    // The anchor is the Toast's own (audit F-D4: this used to add a global
    // window origin to a local point, which is only right at the screen's
    // origin — i.e. on the test rig and nowhere else).
    viewErrorToast->setAnchor(Toast::Anchor::WindowCentre);
    viewErrorToast->showToast(tr("3D view unavailable"),
                              tr("The 3D view could not be created: %1")
                                  .arg(sceneView->viewCreationError()));
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

    // THE DEFAULT FLOOR (services/defaultfloor.h): the ONE factory, shared
    // with material.reset's default provider, so what a new scene stands on
    // and what a reset restores cannot drift apart.
    auto node = defaultfloor::createNode(db, project);
    scene->rootNode->addChild(node);

    auto dlight = iris::LightNode::create();
    dlight->setLightType(iris::LightType::Directional);
    scene->rootNode->addChild(dlight);
    dlight->setName("Directional Light");
    dlight->setLocalPos(iris::Vec3(4, 4, 0));
    dlight->setLocalRot(iris::Quat::fromEulerAngles(15, 0, 0));
    // Through the funnel: these run AFTER addChild, so the node is already in
    // the scene and a raw field write is a change nothing reports
    // (SPECS/DIRTY_SET_MIRROR_SPEC.md; lead review R2 #10). The first sync of a
    // new scene is a full walk, so nothing depends on it today — which is
    // exactly why it would rot silently.
    dlight->setPropertyValue(QStringLiteral("intensity"), 1.0f);
    dlight->icon = iris::Texture2D::load(":/icons/light.png");
    dlight->markChanged(iris::NodeChange::Params);

    // THE SKY LIGHT (SKY_LIGHT_SPEC.md §2, owner decision §188d). A NEW SCENE IS
    // TWO LIGHTS: the sun above, and the sky's own fill. Delete both and the
    // scene is black — which is the whole point of ambient being a light.
    // (The "Point Light" that used to stand here is GONE, §5: it was a second
    // key light in a scene that needed a skylight, and it is exactly what made
    // "ambient" look like a thing a scene did not need.)
    auto skylight = iris::LightNode::create();
    skylight->setLightType(iris::LightType::Sky);
    scene->rootNode->addChild(skylight);
    skylight->setName("Sky Light");
    // WHERE THE POINT LIGHT STOOD. A Sky Light has no position — it is the sky —
    // but its ICON does, and an icon at the world origin sits exactly where the
    // default camera looks and on top of whatever a user drops there first. The
    // old template's second light stood at (-4, 4, 0); the marker for the light
    // that replaces it stands in the same place.
    skylight->setLocalPos(iris::Vec3(-4, 4, 0));
    skylight->setPropertyValue(QStringLiteral("intensity"), 1.0f);
    skylight->setPropertyValue(QStringLiteral("lightColor"), QColor(255, 255, 255));
    skylight->icon = iris::Texture2D::load(":/icons/light.png");
    skylight->markChanged(iris::NodeChange::Params);

    // THE DEFAULT SKY AND FOG: 96 grey, not 72 (owner pick 1, SKY_LIGHT_SPEC
    // §9.1 option ii). The old flat World ambient was 96,96,96 pushed RAW,
    // which is 0.120 of radiance after the pi split; a 96-grey SKY decoded
    // sRGB->linear and integrated over the hemisphere is 0.117 — the owner's
    // ambient number becomes the sky, the Sky Light's default stays an honest
    // 1.0, and the level the samples were authored against is preserved. The
    // visible backdrop brightens one step, which is the whole cost.
    scene->skyColor = QColor(96, 96, 96);
    scene->fogColor = QColor(96, 96, 96);
    scene->shadowEnabled = true;

    sceneNodeSelected(scene->rootNode);

    return scene;
}

void MainWindow::setSettingsManager(SettingsManager* settings)
{
    this->settings = settings;
}

void MainWindow::wireFramePacing()
{
    // PANEL-AWARE PACING (fps audit F1, services/framepacing.h). Two inputs:
    // the persisted mode and the refresh rate of the screen this window is on.
    EngineRenderDriver *driver = EngineHost::instance().driver();
    if (!driver) return;

    if (settings) {
        bool ok = false;
        const framepacing::Mode m = framepacing::modeFromName(
            settings->getValue(framepacing::settingsKey(), QString()).toString(), &ok);
        // An absent or unreadable value is not an error: Display is the default
        // and writing one back would invent a preference the user never made.
        if (ok) driver->setPacingMode(m);
    }

    // "Which screen is this window on" is a QWindow question, and the QWindow
    // does not exist until the widget is shown — which is AFTER this runs
    // (setupViewPort is constructor work). So take what is available now
    // (QWidget::screen(), the primary screen before a show) and hook the
    // screenChanged signal on the next event-loop turns, once the handle is
    // there. NOT createWinId(): forcing a native window early in engine mode is
    // exactly the class of thing AA_DontCreateNativeWidgetSiblings exists to
    // avoid, and this needs no help from it.
    hookFramePacingScreenSignal(8);
    updateFramePacingScreen();

    // THE VR ICON FOLLOWS THE SESSION, not just the button that started it: a
    // session can end from a script (`vr.end()`), from a lost device or from
    // the runtime itself, and a toolbar showing "in VR" over an editor that is
    // not would be a lie. One bool compare per frame, on the thread that owns
    // the icon, and a QIcon is only rebuilt when the answer moves.
    connect(driver, &EngineRenderDriver::beforeFrame, this, [this]() {
        if (playerService && playerService->isVrActive() != mVrIconActive) refreshVrUi();
    });
}

void MainWindow::hookFramePacingScreenSignal(int retriesLeft)
{
    if (QWindow *handle = windowHandle()) {
        connect(handle, &QWindow::screenChanged, this, [this](QScreen *) { updateFramePacingScreen(); });
        updateFramePacingScreen();   // the real window may sit on another screen
        return;
    }
    if (retriesLeft <= 0) return;   // a session that never shows a window (scripted, headless)
    QTimer::singleShot(0, this, [this, retriesLeft] { hookFramePacingScreenSignal(retriesLeft - 1); });
}

void MainWindow::updateFramePacingScreen()
{
    EngineRenderDriver *driver = EngineHost::instance().driver();
    if (!driver) return;
    QScreen *s = windowHandle() && windowHandle()->screen() ? windowHandle()->screen() : screen();
    // The rate can change WITHOUT the screen changing (a mode switch, a
    // variable-refresh panel renegotiating), so the connection follows the
    // screen and is remade when the window moves.
    if (s != mPacingScreen) {
        if (mPacingRefreshConnection) disconnect(mPacingRefreshConnection);
        mPacingScreen = s;
        if (s) mPacingRefreshConnection =
            connect(s, &QScreen::refreshRateChanged, this,
                    [this](qreal hz) {
                        if (EngineRenderDriver *d = EngineHost::instance().driver())
                            d->setRefreshHz(double(hz));
                    });
    }
    driver->setRefreshHz(s ? double(s->refreshRate()) : 0.0);
}

// ---------------------------------------------------------------------------
// THE VR TOGGLE (SPECS/VR_SPEC.md §4.5, phase 3).
//
// Both surfaces — the editor toolbar's icon (and its Ctrl+Shift+V row) and the
// Player page's own button — end up in these two functions, and the functions
// do nothing but call PlayerService. That is the API-first rule as wiring: the
// capability is `player.play({vr:true})` / `player.stop()`, the verb `vr.toggle()`
// calls the service, and so does every button.

void MainWindow::toggleVrMode()
{
    if (!playerService) return;
    playerService->toggleVr();
    refreshVrUi();
}

void MainWindow::refreshVrUi()
{
    if (!actionVr) return;
    const bool available = playerService && playerService->vrAvailable();
    const bool active = playerService && playerService->isVrActive();
    actionVr->setEnabled(available);
    actionVr->setChecked(active);
    mVrIconActive = active;
    // THE TOOLTIP CARRIES THE RUNTIME'S OWN REASON when the icon is dead, plus
    // the sentence a user can act on: VR capability is decided once, at boot,
    // because the OpenXR route has the RUNTIME create the Vulkan device the
    // whole engine runs on (VR_SPEC §7 risk 11). Plugging a headset in later
    // needs a restart, and nothing in the editor can change that at runtime.
    if (available) {
        actionVr->setToolTip(active
            ? QStringLiteral("Leave VR | Stop the run and take the headset off")
            : QStringLiteral("Enter VR | Run the scene in the headset (the Player page, "
                             "mirrored here)"));
    } else {
        QString why = playerService ? playerService->vrUnavailableReason() : QString();
        if (!cliVr())
            why = QStringLiteral("VR capability is fixed at boot — restart with --vr");
        actionVr->setToolTip(QStringLiteral("Enter VR | Unavailable: %1").arg(why));
    }
    if (playerView) playerView->showVr(available, active);
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
    // THE TITLE-BAR X IS A DOCK TOGGLE (lane SPACE-1 round 2). `widgetStates`
    // is the session's record of which editor panels are open — it is what the
    // space switch, the queued layout pass and the Toggle Widgets dialog all
    // read — and closing a dock from its own title bar never reached it: the
    // panel came back at the next space round trip, and the dialog showed it
    // ticked in the meantime. A QDockWidget's X calls close() on the dock, so
    // the Close event is exactly that gesture and nothing else (hiding a page
    // hides its docks without closing them).
    if (event->type() == QEvent::Close) {
        if      (obj == sceneHierarchyDock)      widgetStates[(int) Widget::HIERARCHY]  = false;
        else if (obj == sceneNodePropertiesDock) widgetStates[(int) Widget::PROPERTIES] = false;
        else if (obj == presetsDock)             widgetStates[(int) Widget::PRESETS]    = false;
        else if (obj == assetDock)               widgetStates[(int) Widget::ASSETS]     = false;
        else if (obj == animationDock)           widgetStates[(int) Widget::TIMELINE]   = false;
        else if (obj == scriptConsoleDock)       widgetStates[(int) Widget::CONSOLE]    = false;
    }
    // THE PRESETS LINE FOLLOWS THE BOTTOM AREA, however it moves (lane
    // SPACE-2). A RESIZE of the tray was the only trigger, and the area's top
    // also moves without one: the group's tab bar appears when a second panel
    // opens there and disappears when the last one closes, which shifts the
    // whole area's top edge by the bar's height (15 px, measured) — and it
    // happens AFTER the queued alignment pass has run, so the boot layout was
    // left a tab bar's height out of line. A MOVE of any of the three is the
    // same event for this purpose.
    if ((obj == assetDock || obj == animationDock || obj == scriptConsoleDock)
        && (event->type() == QEvent::Resize || event->type() == QEvent::Move)
        && !presetsAlignQueued) {
        presetsAlignQueued = true;
        QTimer::singleShot(0, this, [this]() { presetsAlignQueued = false; alignPresetsWithTray(); });
    }
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
	// A SCRIPT IN FLIGHT IS STOPPED FIRST, and the close waits for it
	// (SCRIPTING_LIVE_SPEC). A run holds the UI thread only between hops now,
	// so this window CAN be closed while a script is working — and closing it
	// destroys the script engine, the host and the modules under a worker
	// thread that is about to hop into them. Stop the run (it ends at its next
	// JavaScript boundary; a run parked inside a long verb ends when that verb
	// returns) and re-post the close for when it has, which is the same
	// promise app.quit() makes.
	if (scriptEngine && scriptEngine->isRunning()) {
		scriptEngine->stop();
		if (scriptHost && scriptHost->afterRun)
			scriptHost->afterRun([this]() { close(); });
		event->ignore();
		return;
	}

	// An open IN FLIGHT is finished first (services/sceneopenrunner.h). Its
	// slices are short and waitForDone pumps the loop that runs them, so this
	// costs at most the rest of one open — and it is what makes the decision
	// below coherent: closing halfway through an install found sceneOpen still
	// false and a dirty undo stack, and asked the user to save a document that
	// was not built yet (a modal QMessageBox that then swallowed the quit and
	// left the process alive — the import.shutdown zombie, wearing a different
	// hat). Re-entrancy is guarded: the pump can deliver another close.
	static bool sSettlingOpen = false;
	if (sSettlingOpen) return;   // a nested close under the settle: the outer one finishes
	if (isOpeningProject()) {
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

	// (THE DONATE DIALOG USED TO RUN HERE, modally, as the last thing a user
	// saw on the way out. It moved to FIRST LAUNCH — app/firstrun.h, called
	// from main() — for two reasons: asking on the way out is the worst moment
	// to ask, and a nested modal event loop inside closeEvent meant app.quit()
	// could not complete until somebody clicked it. Owner decision D3,
	// 2026-09-12. Nothing may be added here that runs its own event loop.)

	// STEP 1 of the shutdown order (the whole sequence is documented in one
	// place, at ~MainWindow, and enumerated in shell/shutdownorder.h). Recorded
	// HERE, past the Cancel branch above: a close the user backed out of is not
	// a shutdown.
	JAH_SHUTDOWN_STEP(ShutdownOrder::CloseEvent, "closeEvent: autosave + settings");

	// The session's own totals (SESSION_LOG_SPEC §5, "clean quit"). Written
	// HERE, past the Cancel branch, for the same reason the step above is: a
	// close the user backed out of is not the end of the session. JahLog's
	// close bracket and by-level roll-up follow later, in finalizeAppExit.
	SessionMarkers::logQuitSummary();

	settings->setValue("geometry", saveGeometry());
	settings->setValue("windowState", saveState());
	// ...and the EDITOR DOCKS, which live in the nested `viewPort` QMainWindow
	// and are therefore not in the line above (shell/dockstate.h).
	//
	// THE EDITOR'S LAYOUT, NOT THIS PAGE'S (lane SPACE-1, 2026-09-14). Every
	// space but the editor hides the editor's docks, and immersive fullscreen
	// hides them inside it — so saving the live state was saving "no panels"
	// for anyone who quit from the Player, from the Materials page or from
	// F11, and that is what the next launch restored. captureEditorDockState()
	// takes the live layout when the editor is the page on screen and does
	// nothing when it is not; the snapshot taken on the way out of the editor
	// then stands. With neither — a session that never opened a scene — the
	// stored layout is left exactly as it was, because this session has
	// nothing better to say about it.
	captureEditorDockState();
	DockState::store(settings->settings, QString::fromLatin1(kViewportDockStateKey),
	                 editorDockState);

    // Orderly teardown BEFORE the window disappears: dialogs close with a
    // window still on screen, and a mid-flight import batch is aborted and
    // joined while the event loop can still service its commit hop. (Also
    // wired to aboutToQuit for the QApplication::exit/quit paths.)
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

    // A RUNNING CAPTURE IS FINISHED AND WRITTEN FIRST, before anything below
    // touches the engine (CLEANUP-1 item 1). The owner presses Ctrl+F4, sees
    // the problem, and closes the window — and until this line the bundle was
    // simply thrown away: finish() never ran, so there was no machine.json, the
    // trace kept its open bracket, and the engine's ring (which holds the last
    // frames of every capture) was never drained. finalizeAppExit stops the
    // monitor too, but it runs after this function, after the modules are down
    // and after a forced exit can already have taken the process — which is
    // precisely the quit the owner is recording when something is wrong.
    //
    // Idempotent and free when idle: stop() returns false with no capture
    // running and the second call at finalizeAppExit then does nothing.
    FrameMonitor::instance().stop();

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

    // EVERY MODULE IS TOLD TO STOP FIRST (item 2). Module workers ride the same
    // global pool the wait below joins, and shutdownModules() — where a
    // module's own abort used to live — runs AFTER that wait and after the
    // forced exit behind it. Nothing here joins: abort, flush, return, and let
    // the one pool wait below do the joining for all of them.
    for (auto *module : modules)
        if (module) module->abortBackgroundWork();

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
        AppPaths::dataRoot(), Constants::JAH_DATABASE
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
    // The SET fan-out (EDITOR_MULTISELECT_SPEC §2.1). Deliberately a second
    // connection and not a widened applySelectionToUi: the primary signal
    // drives the single-node panels (properties, timeline) and fires only when
    // the primary changes, while this one runs on every set change — a
    // Ctrl+click storm must repaint the tree and the outline without rebuilding
    // the properties panel N times (§3.3). Emitted AFTER selectionChanged, so
    // the set is what the tree ends up showing.
    connect(selectionService, &SelectionService::selectionSetChanged,
            this, &MainWindow::applySelectionSetToUi);

    playbackService = new PlaybackService(this);
    playbackService->setViewport(sceneView);
    connect(playbackService, &PlaybackService::editModeEntered,
            this, &MainWindow::applyEditModeUi);
    connect(playbackService, &PlaybackService::playModeEntered,
            this, &MainWindow::applyPlayModeUi);

    // The session log's PLAY START / PLAY STOP brackets (SESSION_LOG_SPEC §5)
    // ride the SAME signals — no new calls on the play path. The scene-open
    // block's stats lines come from here too, because LoadTimeline (a service)
    // has no way to reach a document.
    sessionMarkers = new SessionMarkers(this);
    sessionMarkers->attach(playbackService);
    LoadTimeline::setStatsProvider([this] { return SessionMarkers::sceneStats(scene); });

    // The PLAYER space (verb-coverage audit F1) — a different state machine
    // from playbackService's play-in-place. setupViewPort has already built the
    // backend when the engine is up; headless runs leave the host null and the
    // player.* verbs refuse cleanly.
    playerService = new PlayerService(this);
    playerService->setHost(playerBackend);
    // SHOWING THE PLAYER PAGE is the one thing the service cannot do for
    // itself, and the VR toggle's whole contract is "put me in the Player, in
    // the headset" — from a button, a script or an MCP session. The shell
    // hands it the one call rather than the service learning about windows.
    playerService->setSpaceActivator([this]() { this->switchSpace(WindowSpaces::PLAYER); });
    if (playerView) {
        auto *widget = playerView;
        connect(playerService, &PlayerService::playingChanged, widget,
                [widget](bool playing) { widget->showPlaying(playing); });
        QVariantMap vrIconOptions;
        vrIconOptions.insert("color", QColor(255, 255, 255));
        vrIconOptions.insert("color-active", QColor(255, 255, 255));
        widget->setVrToggle([this]() { this->toggleVrMode(); },
                            fontIcons->icon(fa::binoculars, vrIconOptions));
    }

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
        // A node that has left the document cannot stay in the selection SET
        // (EDITOR_MULTISELECT_SPEC §2.1). The single selection was pruned by
        // the delete command's select(null); a set member three rows down was
        // not, and a stale member would keep an outline shell alive and feed a
        // dead node to the next group transform.
        if (selectionService) selectionService->remove(node);
    });
    connect(sceneEditService, &SceneEditService::transformRefreshRequested, this, [this]() {
        if (sceneNodePropertiesWidget) sceneNodePropertiesWidget->refreshTransform();
    });
    connect(sceneEditService, &SceneEditService::assetViewRefreshRequested, this, [this]() {
        assetWidget->updateAssetView(assetWidget->assetItem.selectedGuid);
    });
    connect(sceneEditService, &SceneEditService::materialApplied, this, [this](const QString &) {
        sceneNodePropertiesWidget->refreshMaterial();
    });

    // THE CLIPBOARD (CLIPBOARD_SPEC D3 b) — one component, over the system
    // clipboard, for every space in the app. Constructed after the services it
    // drives (the node domain's fragments, the selection, the undo sink) and
    // handed to the shell, the verbs and the tree menu as one pointer.
    clipboardService = new ClipboardService(db, project, sceneEditService,
                                            selectionService, undoService, nullptr, this);
    connect(clipboardService, &ClipboardService::assetsImported, this,
            [this](const QStringList &) {
        // A paste that imported library assets has changed the library.
        if (assetWidget) assetWidget->updateAssetView(assetWidget->assetItem.selectedGuid);
    });

    thumbnailService = new ThumbnailService(db, project);
    assetService = new AssetService(db, project);

    services = new StudioServices;
    services->eventBus = new Subscriber(this);
    services->undo = undoService;
    services->selection = selectionService;
    services->playback = playbackService;
    services->player = playerService;
    services->project = projectService;
    services->sceneEdit = sceneEditService;
    services->clipboard = clipboardService;
    services->thumbnails = thumbnailService;
    services->assets = assetService;

    // The perf sampler (SESSION_LOG_SPEC §8-R3). Started HERE, from the
    // settings, so it is running long before anything the owner does — a
    // sampler a user has to turn on has already missed the session that
    // needed it.
    perfSampler = new PerfSampler(this);
    services->perfSampler = perfSampler;
    perfSampler->startFromSettings();

    // THE MONITOR'S ONLY VISIBLE OUTPUT (owner, 2026-09-12): a toast when a
    // capture starts and a toast naming the bundle when it stops. The monitor
    // owns no widgets and shows nothing itself — it asks, the shell answers —
    // and it is told what was really shown, so the START toast's LIFETIME goes
    // into events.jsonl and analysis knows which frames it overlapped.
    //
    // Connected unconditionally, and that costs nothing: it fires twice per
    // capture and never while idle.
    connect(&FrameMonitor::instance(), &FrameMonitor::toastRequested, this,
            [this](const QString &title, const QString &text, int holdMs) {
                if (!snapToast) snapToast = new Toast(this);
                // BOTTOM-CENTRE of the window, not over the viewport: the stop
                // toast carries a path, and the viewport anchor is the place
                // gesture feedback (snap size, fly speed) lives.
                snapToast->setAnchor(Toast::Anchor::WindowBottom);
                snapToast->showToast(title, text, holdMs);
                FrameMonitor::instance().noteToastShown(title, text,
                                                        holdMs > 0 ? holdMs : 1650);
            });

    // THE SCENE-ERROR AREA (services/sceneissues.h, owner Q1b/Q1c). A visible,
    // dismissible list of the things wrong with the OPEN SCENE that the person
    // using the editor can fix — beside the frame-rate readout, because that is
    // where the owner asked for it. Engine diagnostics never come here: they go
    // to the log and to the monitor's capture bundle.
    //
    // The scanner runs on a slow timer rather than per frame: the conditions it
    // looks for are authoring state, not frame state, and raising an issue that
    // is already live is a no-op by construction, so a second of latency costs
    // nothing and a per-frame walk of every light against every mesh would.
    wireSceneIssues();

    // THE LIBRARY ITSELF CAN FAIL, AND THE USER HAS TO BE TOLD (CLOSE-2 round
    // 2, H7). A gesture's database writes ride one transaction now, so a
    // commit that fails rolls back EVERYTHING that gesture wrote — a whole
    // script run's asset rows — and until this line existed the only trace was
    // a warn in the log, which has an audience of one. It is a scene-issue and
    // not a toast for the reason the bar exists: it stays up until the
    // condition is gone, and the condition going away is the very next gesture
    // committing. No node to select; the action is the only thing to say.
    Database::setBatchCommitListener([](bool ok) {
        const QString id = QStringLiteral("library.write");
        if (ok) { SceneIssues::instance().clear(id); return; }
        SceneIssue issue;
        issue.id = id;
        issue.kind = QStringLiteral("library.write");
        issue.message = tr("The library could not be saved, so the changes from the last "
                           "action were not kept.");
        issue.action = tr("Check that the disk is not full and that the library file is not "
                          "read-only, then try the action again.");
        SceneIssues::instance().raise(issue);
    });

    // Commands raise their refreshes through the aggregate (stamped at push);
    // the viewport's gizmos push through the same aggregate.
    undoService->setServices(services);
    // AN UNDO REPAINTS THE PANEL (debt L6): every properties row is undoable
    // now, and the rows are the document's state on screen. One hook, deferred
    // by the panel itself, rather than a refresh callback on every command.
    undoService->setStackMovedHook([this]() {
        if (sceneNodePropertiesWidget) sceneNodePropertiesWidget->refreshFromDocument();
    });
    // THE DEFERRED DATABASE WORK OF THE COMMANDS A CLEAR DESTROYS (CLOSE-1).
    // A command's destructor queues its asset-row cleanup instead of writing
    // it — one transaction for the whole stack, here, instead of one
    // transaction and one fdatasync per command on the UI thread.
    undoService->setDeferredFlushHook([this]() {
        if (db) db->flushPendingAssetDeletes();
    });
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
	ThemeManager::applyTopMenuButton(editor_menu, ThemeManager::TopMenuState::Disabled);
	editor_menu->setDisabled(true);
	editor_menu->setCursor(Qt::ArrowCursor);
	ThemeManager::applyTopMenuButton(player_menu, ThemeManager::TopMenuState::Disabled);
	player_menu->setDisabled(true);
	player_menu->setCursor(Qt::ArrowCursor);
}

/// Space names for the log — the same words app.space() accepts, so a record
/// and a script read the same way.
static const char *spaceName(WindowSpaces s)
{
	switch (s) {
	case WindowSpaces::DESKTOP: return "desktop";
	case WindowSpaces::PLAYER:  return "player";
	case WindowSpaces::EDITOR:  return "editor";
	case WindowSpaces::EFFECT:  return "materials";
	case WindowSpaces::ASSETS:  return "assets";
	case WindowSpaces::PUBLISH: return "publish";
	case WindowSpaces::AVATAR:  return "avatar";
	}
	return "?";
}

void MainWindow::spawnAvatarAsset(const QString &guid, const iris::Vec3 &position,
                                  bool hasPosition)
{
    if (!avatarModule) return;
    auto *api = avatarModule->api();
    if (!api) return;
    QVariantMap options;
    if (hasPosition)
        options.insert(QStringLiteral("position"),
                       QVariantMap{ { "x", position.x() }, { "y", position.y() },
                                    { "z", position.z() } });
    if (api->quietly([&] { return api->spawn(guid, options); }).isEmpty()
        && !api->lastError().isEmpty())
        QMessageBox::warning(this, tr("Add Avatar to Scene"), api->lastError());
}

void MainWindow::assignAnimationAsset(const QString &guid, const iris::SceneNodePtr &node)
{
    // NOTHING UNDER THE CURSOR (R2, 2026-09-11: an Animation tile dropped in
    // the viewport did nothing at all, with no message). A clip is not a scene
    // object — it is something a character wears — so the drop says that.
    if (!node) {
        showViewportToast(tr("Animation"),
                          tr("Drop an animation onto a character to assign the clip"));
        return;
    }
    if (!avatarModule) return;
    auto *api = avatarModule->api();
    if (!api) return;
    const QVariantMap result =
        api->quietly([&] { return api->loadClip(node->getGUID(), guid, QVariantMap()); });
    if (result.isEmpty()) {
        showViewportToast(tr("Animation"),
                          api->lastError().isEmpty()
                              ? tr("'%1' cannot take this clip").arg(node->getName())
                              : api->lastError());
        return;
    }
    const QVariantList added = result.value(QStringLiteral("clips")).toList();
    showViewportToast(tr("Animation"),
                      tr("%1 clip(s) added to %2").arg(added.size()).arg(node->getName()));
}

void MainWindow::openAssetInModule(const QString &guid, const QString &moduleId,
                                   const QString &scope)
{
    if (moduleId != QLatin1String("avatar") || !avatarModule) return;
    switchSpace(WindowSpaces::AVATAR);
    if (auto *api = avatarModule->api()) {
        QVariantMap options;
        if (!scope.isEmpty()) options.insert(QStringLiteral("scope"), scope);
        const QVariantMap opened = api->quietly([&] { return api->open(guid, options); });
        // A refusal is the module's own message (a definition that will not
        // parse, a project scope with nothing pinned) — shown here because a
        // menu click has no JS engine to throw into.
        if (opened.isEmpty() && !api->lastError().isEmpty())
            QMessageBox::warning(this, tr("Edit in Avatar Module"), api->lastError());
    }
}

void MainWindow::switchSpace(WindowSpaces space, bool force)
{
	if (currentSpace == space && !force)
		return;
	SessionMarkers::logSpaceSwitch(QString::fromLatin1(spaceName(currentSpace)),
	                               QString::fromLatin1(spaceName(space)));
	ListWidget::stopHighlightedNode();

	// properly shutdown previous space
	switch (currentSpace) {
	case WindowSpaces::PLAYER:
		playerView->end();
		break;
	case WindowSpaces::EDITOR:
		// THE LAYOUT THE EDITOR HAD, taken before the next space hides its
		// docks (lane SPACE-1). Everything below this line is a page that
		// shows no panels, and saving THAT at exit is what left the owner
		// with an editor that opened empty.
		captureEditorDockState();
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

            hideEditorPanels();
            ui->actionClose->setDisabled(true);
            break;
        }

        case WindowSpaces::EDITOR: {
            ui->stackedWidget->setCurrentIndex(1);

			applyDockVisibilityForSpace();
			playerControls->setVisible(false);

			applyColumnWidthsOnce();

			this->sceneView->setWindowSpace(space);
            playSceneBtn->show();
            this->enterEditMode();
            playbackService->setSceneMode(SceneMode::EditMode);

            assetWidget->refresh();
			isSceneOpen = true;
			// The dropdown follows the VIEWPORT, and a scene open resets it to
			// perspective (per-view camera memory is per scene session) — so
			// re-read it here rather than leaving "Top" over a fresh scene.
			setViewsButtonLabel(sceneView->cameraView());

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
            hideEditorPanels();
            toolBar->setVisible(false);

			this->sceneView->setWindowSpace(space);
            playbackService->setSceneMode(SceneMode::PlayMode);
            playSceneBtn->hide();
            this->enterPlayMode();
			playerView->begin();
            // PLAY, not toggle (audit F4): entering the space is a statement,
            // not a button press. `app.space("player")` after `player.play()`
            // used to STOP the scene.
            playerView->playScene();

            break;
        }

        case WindowSpaces::ASSETS: {
            ui->stackedWidget->setCurrentIndex(2);
            ui->stackedWidget->currentWidget()->setFocus();
			static_cast<AssetView*>(ui->stackedWidget->currentWidget())->spaceSplits();
    		hideEditorPanels();
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
			hideEditorPanels();
			toolBar->setVisible(false);
			if (projectService->isSceneOpen()) playSceneBtn->hide();
			break;
		}

		case WindowSpaces::AVATAR: {
			ui->stackedWidget->setCurrentIndex(6);
			ui->stackedWidget->currentWidget()->setFocus();
			hideEditorPanels();
			toolBar->setVisible(false);
			if (projectService->isSceneOpen()) playSceneBtn->hide();
			break;
		}

        default: break;
    }

	updateTopMenuStates(space);
	// The scene-issue bar belongs to the EDITOR and is a top-level window that
	// stays on top: it has to go NOW, not on the scanner's next tick (item 3).
	updateSceneIssues();
}

void MainWindow::updateTopMenuStates(WindowSpaces activeSpace)
{
	toolBar->setVisible(activeSpace == WindowSpaces::EDITOR);

	// One state per space button: the active space, the rest, and — while no
	// scene is open — Editor and Player disabled. ThemeManager owns what each
	// state looks like in each theme (Classic's border-colour swap, or the
	// Qlementine header sheet with the accent-coloured active label).
	const bool sceneOpen = projectService->isSceneOpen();
	const QList<QPair<QPushButton *, WindowSpaces>> spaceButtons = {
		{ worlds_menu, WindowSpaces::DESKTOP }, { assets_menu, WindowSpaces::ASSETS },
		{ effect_menu, WindowSpaces::EFFECT }, { avatar_menu, WindowSpaces::AVATAR },
		{ editor_menu, WindowSpaces::EDITOR }, { player_menu, WindowSpaces::PLAYER }
	};
	for (const auto &pair : spaceButtons) {
		QPushButton *button = pair.first;
		const bool needsScene = pair.second == WindowSpaces::EDITOR
		                        || pair.second == WindowSpaces::PLAYER;
		const bool enabled = sceneOpen || !needsScene;
		if (needsScene) button->setEnabled(enabled);
		button->setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
		ThemeManager::applyTopMenuButton(
			button, !enabled                        ? ThemeManager::TopMenuState::Disabled
			        : activeSpace == pair.second    ? ThemeManager::TopMenuState::Active
			                                        : ThemeManager::TopMenuState::Idle);
	}

	// publish_menu is an ICON in the right cluster with its own glyph sheet;
	// active-space feedback comes from the page itself. Re-applying the sheet
	// re-polishes the button, and Qlementine's polish re-sets its font, so the
	// icon font is pushed again HERE (the helper does both, in that order) —
	// otherwise the arrow drops to the inherited UI font while Help and
	// Preferences stay at 28 (owner report 2026-09-07).
	ThemeManager::applyHeaderGlyphButton(publish_menu, headerGlyphFont());
	publish_menu->setCursor(Qt::PointingHandCursor);
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
/// How long MainWindow::openProject pumps for its own open before it gives up
/// and says so. Ninety seconds against a worst measured open of ~3 s: this is
/// a deadlock guard, not a budget — a caller that waits is a caller that was
/// promised a loaded world.
static const int kOpenWaitBudgetMs = 90000;
/// The pump's idle nap. The runner puts ONE millisecond between its slices, so
/// a five-millisecond sleep per turn would add five to every slice of every
/// scripted open; one keeps the wait honest (measured: ~15 slices).
static const int kOpenWaitIdleMs = 1;

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

	// THE SCENE IS OPEN (AVATAR_ASSET_SPEC §4 D4, the load-time half). Fired
	// HERE and not when the reader returned: a subscriber's job is to walk the
	// scene that is now installed, and until setScene above it was not.
	if (services) services->announceSceneOpened();
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
	// A SCENE OPENS ON THE WORLD. The properties panel is bound to the root, so
	// the World settings are what a freshly opened scene shows.
	//
	// The tree leg used to call a `selectNode(guid)` that DEFINED a lambda and
	// never called it (and compared a node id against a GUID string): for years
	// "highlight root node" highlighted nothing. Deleted with the dead function
	// (RIGHT-TABS-1, CRUD); the row is selected through the panel's real API.
	if (!!scene) {
		sceneHierarchyWidget->setSelectedNode(scene->getRootNode());
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

QStringList MainWindow::plannedOpenModelPaths()
{
	// Every model file this open will need, resolved on the thread that owns
	// the database connection: the session membership's Objects and the
	// scene blob's mesh sources. ONE definition, used by the threaded open's
	// plan and by the synchronous open's prewarm.
	QStringList paths = pmContainer ? pmContainer->plannedSessionModelPaths() : QStringList();
	if (projectService)
		for (const QString &path : projectService->plannedModelPaths())
			if (!paths.contains(path)) paths.append(path);
	return paths;
}

iris::MeshPrewarmPtr MainWindow::prewarmModelsPumped()
{
	// THE PARSE, OFF THIS THREAD, WITH THE CALLER STILL BLOCKED
	// (OPEN-ASSIMP-1). The synchronous open owes its caller a loaded world
	// when it returns — that is what `project.open()`, every headless script
	// and every e2e suite are written against — but it does not owe anyone an
	// assimp parse on the thread that draws. Measured on the eight shipped
	// samples (2026-09-15, spikes/open-assimp-1/): 1 086 ms of parse inside a
	// 1 872 ms unbroken UI-thread block for Matcaps, 986 / 1 998 for World
	// Background, 391 / 1 104 for Skeletal Animation.
	//
	// So the plan is resolved here (database work, per-thread connection), the
	// files are read on a worker, and this thread PUMPS while it waits — user
	// input excluded, the pattern ProjectArchiver and SceneOpenRunner already
	// use. The window keeps painting and answering its heartbeat through the
	// second that used to freeze it, and the stages below then run back to
	// back exactly as they always have, with the parses already in hand.
	//
	// WHY THIS PUMP IS SAFE, BY CONSTRUCTION. Pumping delivers DeferredDelete
	// events, and a DeferredDelete is only delivered by a sendPostedEvents
	// running BELOW the loop level it was posted at — so what this pump can
	// free is what an outer loop has already finished with. It runs BEFORE
	// openStageBegin, with the previous world still installed and the desktop
	// still the current page: no panel is being torn down or rebuilt inside
	// it, so nothing here can free a row that a panel is about to touch. That
	// ordering is the invariant; moving this call after openStageBegin would
	// break it.
	//
	// WHY NOT THE RUNNER'S SLICES TOO (and this is a measured decision, not a
	// preference): slicing the INSTALL means returning to the event loop
	// between the stages, and the properties panel's rows are retired with
	// deleteLater() while raw pointers to them are kept — a window that only
	// closes when the loop turns (ui/controls/accordionbladewidget.cpp says so
	// in as many words: "EVERY script- or MCP-driven scene build ... is one
	// call that never yields"). A sliced synchronous open turned that latent
	// lifetime defect into a crash in five of the eight shipped samples
	// (spikes/open-assimp-1/, the decoded backtraces), while the same eight
	// pass with the parse hoisted and the install left alone. The defect is
	// real and is reported; it is not this lane's to fix under it.
	auto prewarm = std::make_shared<iris::MeshPrewarm>();
	const QStringList modelPaths = plannedOpenModelPaths();
	if (modelPaths.isEmpty()) return prewarm;

	LoadTimeline::mark(QStringLiteral("plan"));
	QVector<iris::PrewarmItem> plan;
	plan.reserve(modelPaths.size());
	for (const QString &path : modelPaths) plan.append(MeshBakeStore::planFor(path));

	std::atomic<bool> done { false };
	QFuture<void> future = QtConcurrent::run([plan, prewarm, &done]() {
		// THE FLAG IS FLIPPED BY A SCOPE GUARD, not by the last statement: a
		// throw out of a parse (assimp's importers do throw) would otherwise
		// leave `done` false and this thread pumping for the whole budget
		// before the future rethrew — ninety seconds of "nothing is wrong".
		struct Finish { std::atomic<bool> &flag; ~Finish() { flag.store(true); } } finish{ done };
		for (const iris::PrewarmItem &item : plan) {
			// Named "assimp" for continuity of the ledger, but a bake hit
			// never reaches assimp — worker:bakeHits is the split.
			LoadTimeline::Accumulate parse(QStringLiteral("worker:assimp"));
			prewarm->parse(item);
		}
	});
	LoadTimeline::mark(QStringLiteral("parse(worker)"));

	QElapsedTimer waited;
	waited.start();
	while (!done.load() && waited.elapsed() < kOpenWaitBudgetMs) {
		// Timers and posted events, no user input: the heartbeat ticks, the
		// engine paints, nothing re-enters the editor from the outside.
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
		if (done.load()) break;
		QThread::msleep(static_cast<unsigned long>(kOpenWaitIdleMs));
	}
	// The join is not optional: the worker writes into `prewarm` and into two
	// stack locals. A budget this large (90 s against a worst measured parse
	// of ~1.1 s) is a deadlock guard, and blocking without the pump is still
	// better than reading a half-filled prewarm.
	if (!done.load())
		qWarning("project open: the model parse is still running after %d ms — waiting for it "
		         "without pumping", kOpenWaitBudgetMs);
	future.waitForFinished();
	LoadTimeline::add(QStringLiteral("worker:bakeHits"), 0.0, prewarm->bakedCount());
	return prewarm;
}

void MainWindow::openProject(bool playMode)
{
	// The ledger (services/loadtimeline.h). Both open paths mark the same
	// stage names, so the synchronous open and the threaded one are directly
	// comparable in the log and in app.openTimings().
	if (!LoadTimeline::isRunning())
		LoadTimeline::begin(QStringLiteral("open(sync) %1")
		                        .arg(project ? project->getProjectName() : QString()));

	// THE BACKSTOP. A caller that points the project at another world must
	// drain an in-flight open BEFORE it does so (MainWindow::waitForOpen, and
	// both project verbs call it there); by the time we are here the pointers
	// have already moved, so all this can still do is refuse to interleave two
	// worlds through one set of slices.
	if (isOpeningProject()) {
		qWarning("project open: a threaded open was still in flight when a blocking open "
		         "started — draining it (the caller should have waited first)");
		openRunner->waitForDone(kOpenWaitBudgetMs, kOpenWaitIdleMs);
	}

	// The models, parsed on a worker while this thread pumps (above).
	const iris::MeshPrewarmPtr prewarm = prewarmModelsPumped();

	openStageBegin();
	// The session registrations, in the threaded open's order and with the
	// worker's models in hand — this is the "synchronous preload" that used to
	// run in ProjectService::prepareOpen BEFORE the open and parse every
	// pinned Object on this thread (deleted with this change).
	LoadTimeline::mark(QStringLiteral("sessionRegistrations"));
	AssetManager::clearAssetList();
	if (pmContainer) pmContainer->registerProjectSessionAssets(prewarm);

	openStageReadDocument(playMode, prewarm);
	openStagePanels();
	// The LAST thing before the page switch: push the whole document into the
	// renderer (meshes, materials, textures) while the desktop page is still
	// the page on screen. Whatever this costs is spent under the progress
	// dialog instead of under a viewport that has nothing to show. Skipped
	// silently on the very first open, when no render view exists yet.
	LoadTimeline::mark(QStringLiteral("primeSceneSync"));
	sceneView->primeSceneSync();
	// The synchronous open's half of the F1a recording. No warm-up slice here
	// on purpose — the sync path has no cover to hide one behind — but the
	// RECORD is cheap (a memory-manager walk, no GPU work) and the next
	// launch's warm-up is only as good as the sets it was given.
	LoadTimeline::mark(QStringLiteral("recordWarmUpSet"));
	sceneView->recordWarmUpSet();
	openStageReveal(playMode);
}

bool MainWindow::isOpeningProject() const
{
	return openRunner && openRunner->isRunning();
}

unsigned MainWindow::openSliceBoundaries() const
{
	return openRunner ? openRunner->boundaryRuns() : 0u;
}

bool MainWindow::waitForOpen()
{
	if (!isOpeningProject()) return true;
	// THE PUMP HERE IS NOT THE SAFE ONE (see prewarmModelsPumped): the slices
	// it services install a world — they mount panels, bind the properties
	// tree and switch the page, and they retire panel rows whose owners keep
	// raw pointers to them. That is the threaded open's own exposure, not one
	// this call adds: the very same slices run from the very same event loop
	// when nobody is waiting. What this does add is that they finish BEFORE
	// the caller tears the project down, which is the hybrid this exists to
	// prevent.
	return openRunner->waitForDone(kOpenWaitBudgetMs, kOpenWaitIdleMs);
}

void MainWindow::openProjectAsync(bool playMode)
{
	// One open at a time, and the same backstop as the blocking open above:
	// the caller drains an in-flight open through waitForOpen() before it
	// re-points the project; this only stops two worlds sharing one set of
	// slices if one ever gets here anyway.
	if (isOpeningProject()) {
		qWarning("project open: a threaded open was still in flight when another started — "
		         "draining it (the caller should have waited first)");
		openRunner->waitForDone(kOpenWaitBudgetMs, kOpenWaitIdleMs);
	}

	if (!LoadTimeline::isRunning())
		LoadTimeline::begin(QStringLiteral("open(async) %1")
		                        .arg(project ? project->getProjectName() : QString()));
	startOpenRun(playMode);
}

/// THE ONE OPEN (OPEN-ASSIMP-1). Plans the model parses, starts the worker and
/// queues the install slices; the caller decides whether to wait.
void MainWindow::startOpenRun(bool playMode)
{
	// Without a project service there is no document to read and the stages
	// below would dereference it; without a project manager there is simply no
	// session membership to register (a shell that never built its desktop).
	// Neither happens in a running app — both are built in the constructor —
	// and neither is a reason to fall back to a second open path.
	if (!projectService) {
		qWarning("project open: no project service — nothing was opened");
		return;
	}

	// The cover goes up NOW, not in the first slice: the parse phase runs on
	// a worker for up to a second, and opening a world from inside the editor
	// must not leave the previous one on screen while it does. openStageBegin
	// raises it again (idempotent — it only rebases the present counter, and
	// nothing has presented in between).
	sceneView->beginSceneLoad(project ? project->getProjectName() : QString());

	// ---- plan: the DB half, here, on the thread that owns the connection ----
	LoadTimeline::mark(QStringLiteral("plan"));
	const QStringList modelPaths = plannedOpenModelPaths();

	if (!openRunner) {
		openRunner = new SceneOpenRunner(db, project, this);
		connect(openRunner, &SceneOpenRunner::progress, this,
		        [this](int percent, const QString &text) {
			        if (pmContainer) pmContainer->showOpenProgress(percent, text);
		        });
		connect(openRunner, &SceneOpenRunner::finished, this, [this](bool) {
			if (pmContainer) pmContainer->hideOpenProgress();
		});
		// THE INSTALL DRIVES ITS OWN FRAME (lane OPEN-FRAMES-1, 2026-09-15).
		// Set once, on the runner this window keeps for its whole life.
		//
		// THE INSTALL HAS ALWAYS ASSUMED A FRAME BETWEEN ITS SLICES — that is
		// the entire reason it runs one slice per event-loop turn — and it has
		// never been entitled to one. The render tick is a 16 ms QTimer, and a
		// chain of posted events (a script polling a verb, a user driving a
		// panel, an MCP client) outranks a timer in Qt's dispatcher, so an open
		// driven that way installs a whole world with NO frame rendered at all.
		// The renderer's per-frame machinery then never turns: the texture
		// worker's command buffer, the staging recycle, and the buffer
		// manager's delayed-block release (Engine::advanceResources spells out
		// which). Measured on this lane's build, a scripted open that renders
		// no frame crashes 9 times in 12 with a corrupt heap; with one frame at
		// every slice boundary, 0 in 12.
		//
		// SO THE BOUNDARY RENDERS THE FRAME, instead of hoping the timer fired.
		// This is not an extra picture — it is the picture the tick would have
		// drawn if it had been scheduled, drawn at exactly the moment the
		// install expected one, and it costs one frame per slice (about a dozen
		// over an open) on a path already behind the loading cover.
		//
		// MEASURED, NOT ASSUMED, AND THE CHEAPER THING WAS TRIED FIRST: the
		// bare resource advance (Engine::advanceResources, no rendering) at the
		// same boundaries took the same script from 9/12 to 6/12 — real, and
		// not a cure. What a frame does beyond it is not yet named; the open
		// pays for the whole frame until it is (the finding is in the lane's
		// report and the pin's entry in SPECS/OGRE_UPSTREAM_ISSUES.md).
		//
		// WHEN THERE IS NO VIEWPORT TO RENDER (a headless shell, the engine
		// still starting) the advance is still made: it is strictly less, but
		// it is what that session can do, and it keeps the boundary's promise
		// that SOMETHING turned the renderer's bookkeeping.
		openRunner->setSliceBoundary([this]() {
			if (sceneView && sceneView->canRenderFrames()) {
				sceneView->renderFrames(1);
				++openSliceBoundaryFrameCount;
				return;
			}
			if (auto engine = EngineHost::instance().engine()) engine->advanceResources();
		});
	}

	// ---- install: UI-thread slices, one per event-loop turn ----------------
	//
	// The session hydration is spread over SEVERAL turns: it is the one
	// install step whose cost grows with the project (49 assets in the
	// Showroom sample), and a single 200 ms slice plus an engine frame is
	// most of the responsiveness budget on its own.
	const QStringList sessionGuids = pmContainer ? pmContainer->sessionAssetGuids() : QStringList();
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
			if (pmContainer) pmContainer->registerSessionAssetGuids(batch, openRunner->prewarm());
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
	// ON BY DEFAULT (SHADER_CACHE_AUDIT F3), and the default flipped on a
	// MEASUREMENT, not on an opinion. It used to ship OFF against these
	// numbers (open.responsive, Showroom, worst UI-thread gap, ms):
	//
	//                        cold open        second open
	//   without this slice   1691 - 1789      439 - 476
	//   with it              1723 - 1761      646 - 736      <- the objection
	//
	// The objection was that the second open pays ~250 ms for a warm-up that
	// compiles NOTHING, against a 500 ms budget with ~25 ms of headroom.
	//
	// RE-MEASURED on this build, same suite, same box: cold 425.7 ms, warm
	// 381.8 ms — both inside the budget, and the warm one BELOW the
	// no-warm-up figure above. What changed is not the cost of a warm-up but
	// where it lands: the self-disarming idle check (enginesceneviewport.cpp,
	// mWarmUpIdleAt) skips every warm-up after one that compiled nothing, so
	// the single no-op frame is paid on the COLD open — budgeted at 4000 ms,
	// and paying those compiles either way — and the warm open pays nothing.
	//
	// Note what this route is NOT: Ogre's CompositorPassWarmUp, which renders a
	// 4x4 target and reaches permutations the camera cannot see. ogre-patch
	// 0016 makes that route run at all, but a second upstream read-after-destroy
	// kills the app on the second world of a session, so it ships behind
	// JAHSHAKA_WARMUP_PASS=1 (the crash is documented in OgreChain.cpp).
	// The switch stays in Preferences -> Cache for anyone who wants it off.
	if (settings->getValue("shader_warmup_on_open", true).toBool()) {
		slices.append({ QStringLiteral("Precompiling shaders…"), 95, [this]() {
			LoadTimeline::mark(QStringLiteral("warmUpShaders"));
			const unsigned built = sceneView->warmUpShaders();
			if (built) qInfo("scene open: precompiled %u shader(s) behind the cover", built);
		} });
	}
	// WRITE THE WORLD DOWN for the next launch (SHADER_CACHE_AUDIT F1a). Behind
	// the cover, in its own event-loop turn, and AFTER the geometry and
	// environment pushes — the recording reads each renderable's Hlms hash and
	// vertex declaration, both of which exist as soon as the mirror has bound
	// the datablocks. Unconditional: the recorded set is what makes the NEXT
	// startup warm, so it must not be gated on this session's precache setting.
	slices.append({ QStringLiteral("Precompiling shaders…"), 96, [this]() {
		LoadTimeline::mark(QStringLiteral("recordWarmUpSet"));
		sceneView->recordWarmUpSet();
	} });
	slices.append({ QStringLiteral("Opening…"), 100,
	                [this, playMode]() { openStageReveal(playMode); } });

	openRunner->setPlan(modelPaths, slices,
	                    project ? project->getProjectName() : QStringLiteral("scene"));
	openRunner->start();
}

void MainWindow::closeProject()
{
    // AN OPEN IN FLIGHT IS DRAINED FIRST (lane OPEN-FRAMES-1, item 3), the way
    // the window-close and shutdown paths already do it (closeEvent above,
    // shutdownBackgroundWork below). Without this a queued install slice could run
    // AFTER this function tore the project down — it would mount panels on a
    // document that no longer exists, push a scene that was just destroyed and
    // switch the page to a world nobody opened. Nothing but ProjectApi's
    // refusal stood between that and the user, and the refusal only covers the
    // scripted route: a tile's close control, the menu and the shutdown path
    // reach here with slices still queued.
    //
    // DRAIN, THEN ABANDON. waitForDone pumps the loop the slices run on, so the
    // healthy case is simply "the open finishes, then it closes" — the same
    // coherence the close-event settle buys. Only an install that will not
    // finish inside the budget is abandoned, and requestAbort stops the NEXT
    // slice rather than interrupting one.
    //
    // RE-ENTRANCY IS GUARDED, and it must be: the pump can deliver another
    // close (a second click, a queued menu action, an MCP request), and this
    // function is not re-entrant below.
    static bool sDrainingOpen = false;
    // A NESTED close arriving through the drain's pump (an MCP project.close,
    // a queued metacall — not user input, so ExcludeUserInputEvents lets it in)
    // must RETURN, not fall through to the teardown under the outer drain
    // (the second read of OPEN-FRAMES-1): the outer close finishes the job.
    if (sDrainingOpen) return;
    if (isOpeningProject()) {
        sDrainingOpen = true;
        openRunner->waitForDone(kOpenWaitBudgetMs, kOpenWaitIdleMs);
        if (openRunner->isRunning()) {
            qWarning("project close: the open in flight did not finish inside its budget — "
                     "abandoning the rest of its install");
            openRunner->requestAbort();
            openRunner->waitForDone(2000, kOpenWaitIdleMs);
        }
        sDrainingOpen = false;
    }

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

        // Put every body and avatar back where Play found it before the
        // autosave below — the Environment's own restore (recursive, exact),
        // not the top-level-only matrix copy this used to be.
        if (!scene->getPhysicsEnvironment()->nodeTransforms.isEmpty())
            scene->getPhysicsEnvironment()->restoreNodeTransformations(scene->getRootNode());

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

    // The desktop's tiles carry the open marker (dark blue caption bar,
    // "[ Open ]" caption, Close instead of Play/Edit). Refresh them the moment
    // the flag goes false: NEITHER exit path below rebuilds the grid — closing
    // while already on the desktop returns early, and switchSpace(DESKTOP)'s
    // repopulate is gated on a scene being open, which it no longer is. Found
    // by scripting.e2e.desktops' open-flag assertion (2026-09-08); the stale
    // marker predates the blue bar, it was just less visible.
    if (pmContainer) pmContainer->refreshOpenTiles();

    playbackService->setPlaying(false);
    ui->actionClose->setDisabled(false);
    refreshClaudeChatContext();   // D1: an open chat loses its project

    undoService->clear();
    AssetManager::clearAssetList();

    setWindowTitle(originalTitle);

    // A5a (ENGINEERING_DEBT_SPEC addendum 5): the viewport keeps the world it
    // was showing unless somebody says otherwise, and closeProject never did.
    // removeScene() had exactly ONE caller — openStageBegin, the load-in-place
    // path — so a plain close left EngineSceneViewport::mScene, the engine
    // scene, the mirror and every datablock alive behind the desktop, and the
    // "noscene" cover state was unreachable through the ordinary close. It
    // runs BEFORE scene->cleanup(): clearScene() writes the warm-up set down
    // from the still-live engine scene, and the mirror is dropped while the
    // document it mirrors still exists.
    removeScene();

    scene->cleanup();
    scene.clear();

    // R4: the document's nodes are gone from the staging manager now, and
    // the engine scene went with removeScene() — the one moment a SIMD-pool
    // shrink has something to give back (Engine::reclaimMemory; the GPU pools
    // free themselves a few frames after this).
    if (auto eng = EngineHost::instance().engine()) eng->reclaimMemory();

	undoService->resetSavedCount();

	if (currentSpace == WindowSpaces::DESKTOP) {
		deselectViewports();
		// Closing FROM the desktop skips switchSpace (already there), which is
		// the only caller of updateTopMenuStates — so Player/Editor stayed
		// white and enabled with no scene open (owner, 2026-09-05). Re-style
		// explicitly: the scene-open flag just went false.
		updateTopMenuStates(WindowSpaces::DESKTOP);
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
    // EVERY BIND CLEARS THE ISSUE STORE (CLEANUP-1 item 7). The scanner only
    // forgets the two kinds it owns, so an `editor.raiseIssue` of any other
    // kind used to survive a project switch and describe a node that is no
    // longer in the scene — the store is scene-scoped, and this is where the
    // scene changes. (The scanner's own null-scene reset stays: that is the
    // close path, and this one is the open path.)
    SceneIssues::instance().reset();

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

// WHAT A SELECTION COSTS (ADD-1, 2026-09-15). Three of these four are cheap and
// IMMEDIATE — the outline and gizmo in the viewport, the highlighted row in the
// Hierarchy, the timeline's subject. The fourth, the Properties column, is the
// expensive one (44 ms of a scripted add's 50 before this lane), and it is the
// only one nobody can see until the frame paints: it settles its rebuild at the
// end of the event-loop turn instead, coalescing repeated selections into one
// mount (SceneNodePropertiesWidget::applyTab). A click is one turn, so the pick
// is unchanged in feel; an undo of a 64-object macro selects 64 times and mounts
// once. A scripted add is a turn of its own (every verb hops to this thread), so
// a script still mounts per add — the win there is the material blade's REFILL
// and the mesh cache (~3 ms per add, not 44).
void MainWindow::applySelectionToUi(iris::SceneNodePtr sceneNode)
{
    sceneView->setSelectedNode(sceneNode);
    this->sceneNodePropertiesWidget->setSceneNode(sceneNode);
    this->sceneHierarchyWidget->setSelectedNode(sceneNode);
    animationWidget->setSceneNode(sceneNode);
}

// The consumers that understand a SET: the outliner's selected rows and the
// viewport (outline, gizmo group, focus/orbit/floor). The properties panel and
// the timeline stay on the primary — multi-edit is out of scope for v1
// (EDITOR_MULTISELECT_SPEC §4).
void MainWindow::applySelectionSetToUi(const QList<iris::SceneNodePtr> &nodes)
{
    if (sceneView) sceneView->setSelectedSet(nodes);
    if (sceneHierarchyWidget) sceneHierarchyWidget->setSelectedSet(nodes);
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

void MainWindow::addSkyLight()
{
    sceneEditService->addSkyLight();
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

void MainWindow::addMaterialMesh(const QString &path, bool ignore, iris::Vec3 position,
                                 const QString &guid, const QString &assetName,
                                 surfaceplacement::Placement placement)
{
    sceneEditService->addMaterialMesh(path, ignore, position, guid, assetName, placement);
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

// THE SELECTION, not the primary (EDITOR_MULTISELECT_SPEC §2.5). Both of these
// are what the toolbar buttons, the outliner's context menu and the Del/Ctrl+D
// shortcuts call, so all three act on the whole set and land as one undo step.
void MainWindow::duplicateNode()
{
    if (!selectionService) return;
    const auto set = selectionService->selectedSet();
    if (set.size() > 1) { sceneEditService->duplicateNodes(set); return; }
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
    if (!selectionService) return;
    const auto set = selectionService->selectedSet();
    if (set.size() > 1) { sceneEditService->deleteNodes(set); return; }
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
    // The manifest's scene-scale block, measured from the live document — the
    // archiver only ever sees the database (services/sceneextents.h).
    if (sceneView)
        archiver->setSceneMetadata(sceneextents::describe(sceneView->getScene(),
                                                          sceneView->editorCamera()));
    archiver->startExport(filePath);
}

namespace {

// A DOCK BODY THAT *ASKS* FOR A WIDTH (smoke S1, F-X1).
//
// A dock area lays its docks out from their sizeHint and refuses to go below
// their minimumSizeHint. The right column used to get its 396 px from a
// MINIMUM — `presetsTabWidget->setMinimumWidth(396)` — which is why the column
// could never be dragged to the 300 px `rightColumnMinWidth` advertises: the
// default width was being expressed as a constraint. (And resizeDocks cannot
// fix it from outside: the right column is a vertically split PAIR, a nested
// dock layout, and Qt applies a horizontal resizeDocks to nested items only
// approximately — measured on the rig 2026-09-11, the request simply does not
// land, while the flat left column's does.)
//
// This is the same statement made the way Qt reads it: sizeHint = the column's
// default width, minimum untouched. The column OPENS at PanelMetrics::
// rightColumnWidth and drags down to rightColumnMinWidth, which is exactly what
// the two constants say.
class ColumnBody : public QWidget
{
public:
    explicit ColumnBody(int hintWidth, QWidget *parent = nullptr)
        : QWidget(parent), mHintWidth(hintWidth) {}

    QSize sizeHint() const override
    {
        const QSize base = QWidget::sizeHint();
        return QSize(qMax(base.width(), mHintWidth), base.height());
    }

private:
    int mHintWidth;
};

}   // namespace

void MainWindow::setupDockWidgets()
{
    // Hierarchy Dock
    sceneHierarchyDock = new QDockWidget("Hierarchy", viewPort);
    // The NAME restoreState matches this dock on. (It used to be overwritten
    // one line below with the WIDGET's name — a leftover that made every saved
    // layout call the left column `sceneHierarchyWidget`; DockState::kVersion
    // 3 retires those blobs. Lane SPACE-1.)
    sceneHierarchyDock->setObjectName(QStringLiteral("sceneHierarchyDock"));
    sceneHierarchyWidget = new SceneHierarchyWidget;
    sceneHierarchyDock->setWidget(sceneHierarchyWidget);
    // THE LEFT COLUMN IS ONE COLUMN, on every page (ui/style/panelmetrics.h).
    // The editor's left column is the one the other pages copy, so it is sized
    // from the constant rather than from whatever the tree's sizeHint asks for.
    sceneHierarchyWidget->setMinimumWidth(PanelMetrics::leftColumnMinWidth);
    sceneHierarchyWidget->setMainWindow(this);
    if (sceneView) sceneView->setHierarchyDragSource(sceneHierarchyWidget->getWidget());

    connect(sceneHierarchyWidget,   SIGNAL(sceneNodeSelected(iris::SceneNodePtr)),
            this,                   SLOT(sceneNodeSelected(iris::SceneNodePtr)));
    // The outliner's SET (EDITOR_MULTISELECT_SPEC §2.2): straight into the
    // service, primary first, the same way the single-node signal goes.
    connect(sceneHierarchyWidget, &SceneHierarchyWidget::sceneNodeSetSelected,
            this, [this](const QList<iris::SceneNodePtr> &nodes) {
        if (selectionService) selectionService->select(nodes);
    });

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
    sceneNodePropertiesDock->setStyleSheet(StyleSheet::MainWindowPropertiesDock());

    QWidget *sceneNodeDockWidgetContents = new ColumnBody(PanelMetrics::rightColumnWidth, viewPort);
    QScrollArea *sceneNodeScrollArea = new QScrollArea(sceneNodeDockWidgetContents);
    // THE RIGHT COLUMN IS ONE COLUMN (ui/style/panelmetrics.h): this dock and
    // the Presets panel below it are sized from the same number. The minimum is
    // a contract — there is no horizontal scrollbar below, so a panel that does
    // not fit here is CLIPPED (ui.properties_width).
    sceneNodeScrollArea->setMinimumWidth(PanelMetrics::rightColumnMinWidth);
    sceneNodeScrollArea->setStyleSheet(StyleSheet::BorderNone());
    sceneNodeScrollArea->setFrameShape(QFrame::NoFrame);
    sceneNodeScrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    sceneNodeScrollArea->setWidget(sceneNodePropertiesWidget);
    sceneNodeScrollArea->setWidgetResizable(true);
    sceneNodeScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QVBoxLayout *sceneNodeLayout = new QVBoxLayout(sceneNodeDockWidgetContents);
    sceneNodeLayout->setContentsMargins(0, 0, 0, 0);
    // THE TAB BAR SITS ABOVE THE SCROLL AREA (PROPERTY_FILTER_SPEC §2/§6.5):
    // World | Selection, pinned, so the rows scroll under it and the panel's
    // own minimum width — the column-width law — keeps measuring exactly the
    // rows it measured before.
    propertiesTabStrip = new PropertiesTabStrip(sceneNodePropertiesWidget,
                                                sceneNodeDockWidgetContents);
    sceneNodeLayout->addWidget(propertiesTabStrip);
    sceneNodeLayout->addWidget(sceneNodeScrollArea);
    sceneNodeDockWidgetContents->setLayout(sceneNodeLayout);
    sceneNodePropertiesDock->setWidget(sceneNodeDockWidgetContents);
    // THE COLUMN FILLS THE MOMENT THIS DOCK COMES FORWARD (TABS-HIDDEN-1). A
    // selection only raises a mount DEBT while nobody can see the column
    // (SceneNodePropertiesWidget::onScreen) — and "nobody can see it" includes
    // this dock sitting behind another tab of its group, which Qt SHOWS and
    // parks off-screen. Qt emits visibilityChanged(true) both when the dock is
    // opened and when its tab is raised (QMainWindowLayout::tabChanged), so
    // this is the one wire that settles the debt in the SAME turn as the click.
    connect(sceneNodePropertiesDock, &QDockWidget::visibilityChanged,
            this, [this](bool shown) {
                if (shown && sceneNodePropertiesWidget)
                    sceneNodePropertiesWidget->flushPendingMount();
            });

    // Presets Dock
    presetsDock = new QDockWidget("Presets", viewPort);
    presetsDock->setObjectName(QStringLiteral("presetsDock"));

    QWidget *presetDockContents = new ColumnBody(PanelMetrics::presetsPanelWidth);
    presetDockContents->setStyleSheet(StyleSheet::MainWindowPresetsDock());
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
    // F-X1 (platform audit, 2026-09-10): this used to be `presetsPanelWidth`
    // (396) — a MINIMUM 96 px wider than the column's own advertised minimum,
    // so the right column could never actually be dragged to
    // `rightColumnMinWidth` and the two constants contradicted each other.
    // The panel OPENS at the column's default width (applyColumnWidthsOnce
    // below); what it may be squeezed to is the column's minimum, one number
    // for the whole column.
    presetsTabWidget->setMinimumWidth(PanelMetrics::rightColumnMinWidth);
    presetsTabWidget->addTab(assetModelPanel, "Models");
    presetsTabWidget->addTab(assetMaterialPanel, "Materials");
    presetsTabWidget->addTab(skyPresets, "Skyboxes");
    presetDockContents->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    QGridLayout *presetsLayout = new QGridLayout(presetDockContents);
    presetsLayout->setContentsMargins(0, 0, 0, 0);
    presetsLayout->addWidget(presetsTabWidget);
    presetsDock->setWidget(presetDockContents);

    // Asset Dock — titled "Assets" again (lane SPACE-2). It was renamed "Tray"
    // at smoke L10 item 6 because the dock tab bar read "Timeline | Asset
    // Browser" under a tray whose OWN tabs already said "Assets | Console" —
    // one concept named twice. That second tab bar is gone: the dock's title is
    // now the only name the bottom area shows for the asset browser, and it is
    // what the owner calls it.
    assetDock = new QDockWidget(tr("Assets"), viewPort);
    assetDock->setObjectName(QStringLiteral("assetDock"));
    assetWidget = new AssetWidget(db, viewPort);
    assetWidget->setMainWindow(this);
    assetWidget->setEventBus(services->eventBus);
    assetWidget->setProject(project);
    assetWidget->setAcceptDrops(true);
    assetWidget->installEventFilter(this);

	connect(assetWidget, SIGNAL(assetItemSelected(QListWidgetItem*)), this, SLOT(assetItemSelected(QListWidgetItem*)));
    assetWidget->setServices(services);
    // The drawer's avatar rows (AVATAR_ASSET_SPEC §5.5) come back here: the
    // panel decides WHAT it wants, the shell knows WHERE the modules are.
    connect(assetWidget, &AssetWidget::editAssetInModule, this, &MainWindow::openAssetInModule);
    connect(assetWidget, &AssetWidget::spawnAvatarInScene, this,
            [this](const QString &guid) { spawnAvatarAsset(guid, iris::Vec3(), false); });
    // THE IMPORT DECISION (SPECS/IMPORT_DIALOG_SPEC.md §8), both halves. The
    // shell owns the dialog: it is the one place with the widget layer AND the
    // ScriptHost, so a reimport commits through the assets.reimport verb.
    connect(assetWidget, &AssetWidget::reimportAssetRequested, this,
            [this](const QString &guid) { openImportSettings(guid); });


	assetWidget->sceneView = sceneView;

    QWidget *assetDockContents = new QWidget(viewPort);
    QGridLayout *assetsLayout = new QGridLayout(assetDockContents);
    assetsLayout->addWidget(assetWidget);
    assetsLayout->setContentsMargins(0, 0, 0, 0);

    // THE BOTTOM AREA IS ONE TAB GROUP (owner, 2026-09-14, lane SPACE-2):
    // "Assets and Timeline as tabs, and the script Console (Ctrl+`) as a third
    // tab when it is turned on".
    //
    // It used to be TWO tab bars. The tray's widget was a QTabWidget carrying
    // "Assets" and "Console" at the top (smoke S1), while the Timeline — a dock
    // tabified with the tray since the phase-3 refactor — could only be reached
    // through Qt's OWN dock tab bar, which for a bottom dock area is drawn at
    // the very BOTTOM edge of the window, a ~20 px strip under the tray (rig
    // measurement 2026-09-14: the Timeline dock sat at x=-1239, Qt's off-screen
    // parking spot for a tab that is not in front, and its only handle was that
    // strip). The editor therefore read as a single "Assets" panel and the
    // Timeline as GONE — the owner's report.
    //
    // So: the nested QTabWidget is deleted (the tray holds the asset browser
    // directly), the console goes back to being a dock, and the ONE tab bar the
    // three share is moved to the TOP of the area, which is where the tray's
    // own bar used to be and where a user looks for tabs. Tabified docks do NOT
    // split the area, so the objection that retired the console dock at smoke
    // S1 (opening it shrank the viewport) does not apply to this shape.
    assetDock->setWidget(assetDockContents);
    // The Presets line follows the Tray (owner 2026-09-12): a Tray resize —
    // a drag of its top edge, a layout restore — re-aligns the right column.
    assetDock->installEventFilter(this);

    // Animation Dock
    animationDock = new QDockWidget("Timeline", viewPort);
    animationDock->setObjectName(QStringLiteral("animationDock"));
    animationWidget = new AnimationWidget;
    // F16: the Timeline's edits are undoable — the panel pushes the same
    // commands the anim.* verbs push (services/animationedits.h is the shared
    // edit, src/commands/animationcommands.h the shared record).
    animationWidget->setServices(services);

    QWidget *animationDockContents = new QWidget;
    QGridLayout *animationLayout = new QGridLayout(animationDockContents);
    animationLayout->setContentsMargins(0, 0, 0, 0);
    animationLayout->addWidget(animationWidget);

    animationDock->setWidget(animationDockContents);

    // Script Console Dock — the bottom area's third tab (lane SPACE-2). The
    // DOCK is built here, empty: it must exist before DockState::restore below,
    // because a layout blob that names a dock the window does not have leaves
    // Qt guessing at the whole area (the reason kVersion went to 2 when the
    // console STOPPED being a dock). Its widget is the ScriptConsole, which
    // needs the script engine and is handed over where that is built.
    scriptConsoleDock = new QDockWidget(tr("Console"), viewPort);
    scriptConsoleDock->setObjectName(QStringLiteral("scriptConsoleDock"));

    // THE DEFAULT LAYOUT. Presets lives in the RIGHT COLUMN, under Properties
    // (owner layout, 2026-09-08) — it used to open in the BOTTOM area beside
    // the Asset Browser and the Timeline, which is not where anybody uses it
    // and not the column PanelMetrics sizes it for: `presetsPanelWidth` IS
    // `rightColumnWidth`, and a bottom-area Presets panel forced that width
    // onto a dock that spans the whole window instead.
    //
    // splitDockWidget, not addDockWidget: it puts the two in ONE column split
    // vertically, which is the arrangement the shared width constant describes.
    viewPort->addDockWidget(Qt::LeftDockWidgetArea, sceneHierarchyDock);
    viewPort->addDockWidget(Qt::RightDockWidgetArea, sceneNodePropertiesDock);
    viewPort->splitDockWidget(sceneNodePropertiesDock, presetsDock, Qt::Vertical);
    viewPort->addDockWidget(Qt::BottomDockWidgetArea, assetDock);
    viewPort->addDockWidget(Qt::BottomDockWidgetArea, animationDock);
    viewPort->addDockWidget(Qt::BottomDockWidgetArea, scriptConsoleDock);
    // ONE GROUP, IN THE ORDER THE TABS READ (lane SPACE-2): Assets, Timeline,
    // Console. tabifyDockWidget(a, b) puts b AFTER a, so the pair of calls is
    // the tab order — the old single call read "Timeline | Tray", which put the
    // panel the user opens on the right of the panel they rarely open.
    viewPort->tabifyDockWidget(assetDock, animationDock);
    viewPort->tabifyDockWidget(animationDock, scriptConsoleDock);
    // AND THE BAR GOES AT THE TOP. Qt's default for a bottom dock area is
    // QTabWidget::South: a tab strip along the very bottom edge of the window,
    // which is where the Timeline's only handle was hiding (owner report,
    // 2026-09-14) and the first thing a window a few pixels too tall for the
    // screen loses. North puts it where the tray's own tab bar used to be.
    viewPort->setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);
    // The console is CLOSED until Ctrl+` asks for it — a hidden dock has no
    // tab, which is exactly "a third tab when it is turned on". Hidden before
    // any layout is restored, so a blob that recorded it open can say so.
    scriptConsoleDock->hide();
    // AND THE EDITOR OPENS ON ASSETS. tabifyDockWidget leaves the dock it
    // inserted LAST in front, which would hand a fresh profile the Timeline —
    // a panel most sessions never touch — in front of the asset browser every
    // session starts in. (The restored layout below carries the user's LAST
    // front tab; it is raised again after that restore — see there.)
    assetDock->raise();

    // ...and the USER's layout on top of it, if there is one. The docks belong
    // to this nested QMainWindow, so MainWindow's own restoreState (which the
    // constructor calls) never reached them: every move, resize, float, tab
    // and close was forgotten at exit. `restoredViewportDocks` is what tells
    // applyColumnWidthsOnce to keep its hands off — a remembered column
    // width must win over the compiled-in default (shell/dockstate.h).
    restoredViewportDocks =
        settings ? DockState::restore(viewPort, settings->settings, kViewportDockStateKey) : false;
    // A saved dock layout carries the dock-area CORNERS: restoring one saved
    // before the corner rule would put the default corner back. Re-assert it.
    viewPort->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    // EVERY SESSION STARTS IN THE ASSET BROWSER (owner, 2026-09-15, ledger
    // §351). The restored blob carries whichever bottom tab was in front when
    // the last session ended — the Timeline, or the Console after a Ctrl+` —
    // and which tab is in front is SESSION state, not a preference: within a
    // session it still persists across space switches (SPACE-2), but a launch
    // raises Assets over whatever the blob remembered.
    assetDock->raise();
    // A dock closed from its own title bar is a dock the user closed: the
    // Close event goes into `widgetStates` (see eventFilter), so the panel
    // stays closed across space switches and the Toggle Widgets dialog agrees.
    for (QDockWidget *dock : { sceneHierarchyDock, sceneNodePropertiesDock, presetsDock,
                               assetDock, animationDock, scriptConsoleDock })
        dock->installEventFilter(this);
    // WHICH PANELS ARE OPEN IS `widgetStates`, FROM NOW ON (lane SPACE-1).
    // A restored layout says which docks the user had closed, and until this
    // line that answer survived exactly until the first space switch, which
    // re-showed everything from the compiled-in defaults. Seeding the session's
    // own record from the blob is what makes a closed panel stay closed — and
    // it is the same record applyDockVisibilityForSpace reads, so the layout
    // and the space can no longer disagree.
    //
    // `isHidden()`, AND IT HAS TO BE (lane SPACE-2, measured). A tabified dock
    // that is not the front tab is NOT hidden by Qt: it stays shown and is
    // parked off-screen (a negative x — the rig read the Timeline at x=-1239),
    // which is why isHidden() is the predicate that answers "did the user close
    // this panel" for a tab as well as for a lone dock. The obvious
    // alternatives are both WRONG here: `isVisible()` is false for every one of
    // these docks while their page is not on screen, and
    // `toggleViewAction()->isChecked()` is false for ALL of them until the
    // window is first shown — this runs in the constructor. A standalone Qt
    // 6.10 probe of the three readings is in the lane's spike directory.
    if (restoredViewportDocks) {
        widgetStates[(int) Widget::HIERARCHY]  = !sceneHierarchyDock->isHidden();
        widgetStates[(int) Widget::PROPERTIES] = !sceneNodePropertiesDock->isHidden();
        widgetStates[(int) Widget::PRESETS]    = !presetsDock->isHidden();
        widgetStates[(int) Widget::ASSETS]     = !assetDock->isHidden();
        widgetStates[(int) Widget::TIMELINE]   = !animationDock->isHidden();
        // The console comes back the way the user left it: a session that
        // quit with the Console tab open opens with it (owner, 2026-09-14).
        widgetStates[(int) Widget::CONSOLE]    = !scriptConsoleDock->isHidden();
    }

	viewPort->setStyleSheet(StyleSheet::QMenuFlat());
}

QFont MainWindow::headerGlyphFont() const
{
	// 28px of the icon font — the size Help and Preferences always had, now
	// the size all three header glyphs share.
	return fontIcons->font(28);
}

/// THE COLUMNS OPEN AT THEIR WIDTHS (owner, 2026-09-08: "make the presets
/// right column the same width as the presets panel on the main screen — a
/// little wider to match it"; extended to the LEFT column 2026-09-11, smoke
/// S1: "all right columns and left columns unify on the Editor's widths").
/// The right column used to open at whatever the dock's old 326 px minimum and
/// the viewport's stretch produced — 299 px measured on the rig, visibly
/// narrower than the Presets panel it shares the column with — and the left
/// column at whatever the tree's sizeHint asked for, which is the width every
/// other page is now told to copy, so it has to be a number we chose.
///
/// NOT in setupDockWidgets: a dock that is made visible is re-laid-out from its
/// widget's sizeHint, and every entry to the editor page shows these docks, so
/// a resizeDocks from the constructor (queued or not) is simply undone —
/// measured twice on the rig before this landed. It has to run after the page
/// is up, which is why it is queued, and it has to run for BOTH ways the editor
/// page appears: a user switching space, and the scripted/MCP boot, which shows
/// the page directly (beginEngineSelftest) and never calls switchSpace.
///
/// ONCE per session. After that the user's drag is the answer — these are
/// starting sizes, not constraints.
void MainWindow::applyColumnWidthsOnce()
{
    if (columnsSized) return;
    columnsSized = true;
    // A RESTORED LAYOUT ALREADY SAID HOW WIDE THE COLUMNS ARE: the widths below
    // are the compiled-in DEFAULTS, applied once per session, and overriding a
    // width the user dragged and this window just restored would make the dock
    // state look like it was not saved at all. (The restored case is not simply
    // skipped — see the queued block.)
    QTimer::singleShot(0, this, [this]() {
        if (!viewPort || !sceneNodePropertiesDock) return;
        // A RESTORED LAYOUT IS RESTORED AGAIN, HERE (smoke S1). The blob went
        // in from setupDockWidgets — the constructor — where the nested
        // viewPort QMainWindow has no size yet, and Qt scales the saved dock
        // sizes down to whatever width it does have, clamping at the docks'
        // minimums; the window then grows and the slack all goes to the central
        // widget, so the columns come back NARROWER than the user left them,
        // every launch. Measured on the base build 2026-09-11: a Hierarchy dock
        // saved at 328 px came back at 288, and with the S1 minimums a column
        // saved at 396 came back at its 300 floor — which would have put the
        // editor's columns out of step with every other page's on the second
        // launch. Applying the SAME blob now, at the real width, lands the
        // sizes the user actually left.
        if (restoredViewportDocks) {
            if (settings) DockState::restore(viewPort, settings->settings, kViewportDockStateKey);
            viewPort->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);   // see setupDockWidgets
            // THE BLOB DOES NOT DECIDE WHICH PANELS ARE OPEN (lane SPACE-1).
            // It carries each dock's visibility, and applying it here — after
            // switchSpace(EDITOR) has just shown the panels — is what closed
            // them again one event-loop turn after the editor opened. The
            // space owns visibility; this pass owns sizes.
            applyDockVisibilityForSpace();
            alignPresetsWithTray();
            return;
        }
        // BOTH docks in the right column, from the one constant. Presets sits
        // under Properties in the default layout now, and a horizontal
        // resizeDocks that names only one of a vertically split pair leaves the
        // other free to argue about the width. The Hierarchy dock is the left
        // column and rides the same call.
        QList<QDockWidget *> column{ sceneNodePropertiesDock };
        QList<int> widths{ PanelMetrics::rightColumnWidth };
        if (presetsDock) { column << presetsDock; widths << PanelMetrics::rightColumnWidth; }
        if (sceneHierarchyDock) {
            column << sceneHierarchyDock;
            widths << PanelMetrics::leftColumnWidth;
        }
        viewPort->resizeDocks(column, widths, Qt::Horizontal);
        alignPresetsWithTray();
    });
}

// THE PRESETS LINE (owner, 2026-09-12): "the bottom drawer with the materials
// and preset shapes needs to start at the same horizontal line as the asset
// module". The right column runs to the bottom (the corner rule), so the
// Presets panel's top is set to the Tray's top: Presets gets the Tray's height
// and Properties the rest of the column. Skipped when either panel is hidden or
// floating — there is no shared line to meet then.
void MainWindow::alignPresetsWithTray(int retries)
{
    if (!viewPort || !assetDock || !presetsDock || !sceneNodePropertiesDock) return;
    if (!assetDock->isVisible() || !presetsDock->isVisible() || !sceneNodePropertiesDock->isVisible())
        return;
    if (assetDock->isFloating() || presetsDock->isFloating() || sceneNodePropertiesDock->isFloating())
        return;
    const int trayTop = bottomAreaTop();
    const int columnTop = sceneNodePropertiesDock->geometry().top();
    const int columnBottom = presetsDock->geometry().bottom();
    if (trayTop <= columnTop || columnBottom <= trayTop) return;
    // Two passes at most: the dock separators take a few pixels the first
    // request cannot know about, so measure what landed and correct once.
    int presetsH = columnBottom - trayTop + 1;
    for (int pass = 0; pass < 2; ++pass) {
        const int delta = presetsDock->geometry().top() - trayTop;
        if (qAbs(delta) <= 1) break;                        // on the line
        if (pass == 1) presetsH += delta;                   // the separator's share
        const int propsH = qMax(1, (columnBottom - columnTop + 1) - presetsH);
        viewPort->resizeDocks({ sceneNodePropertiesDock, presetsDock }, { propsH, presetsH }, Qt::Vertical);
        if (QLayout *l = viewPort->layout()) l->activate();
    }
    // AND THEN LOOK AGAIN, ONCE THE LAYOUT HAS SETTLED (lane SPACE-2). The two
    // passes above measure what the dock area reports in THIS turn, and that is
    // not always where things end up: the bottom area's tab bar appears a turn
    // later (a second panel opening there) and takes its height off the top of
    // the area, and the right column follows — so the pass that ran at boot
    // reported itself exactly on the line and the user saw it 15 px below.
    // Bounded, and a no-op the moment the two edges agree.
    if (retries <= 0) return;
    QTimer::singleShot(0, this, [this, retries]() {
        if (!presetsDock || !presetsDock->isVisible() || presetsDock->isFloating()) return;
        if (qAbs(presetsDock->geometry().top() - bottomAreaTop()) <= 1) return;
        alignPresetsWithTray(retries - 1);
    });
}

bool MainWindow::setTrayHeight(int height)
{
    if (!viewPort || !assetDock || height < 40) return false;
    viewPort->resizeDocks({ bottomFrontDock() }, { height }, Qt::Vertical);
    QCoreApplication::processEvents();
    alignPresetsWithTray();
    return true;
}

// ---------------------------------------------------------------------------
// THE COLUMN LAW, MEASURED (smoke S1). A constant every page is SUPPOSED to
// use proves nothing about the page that forgot; this reads the widgets the
// pages name as their columns (ui/style/columnedpage.h) and reports the width
// they actually have and the minimum they can actually be dragged to. The
// editor answers from its own docks — the viewport page is a nested QMainWindow
// the shell builds itself, not a ColumnedPage.
MainWindow::ColumnMetrics MainWindow::activeColumns() const
{
    ColumnMetrics m;
    const QWidget *left = nullptr;
    const QWidget *right = nullptr;
    if (currentSpace == WindowSpaces::EDITOR) {
        left = sceneHierarchyDock;
        right = sceneNodePropertiesDock;
    } else if (ui && ui->stackedWidget) {
        if (auto *page = dynamic_cast<ColumnedPage *>(ui->stackedWidget->currentWidget())) {
            left = page->leftColumn();
            right = page->rightColumn();
        }
    }
    if (!left && !right) return m;
    m.valid = true;
    // The EFFECTIVE minimum: what a user's drag hits. A widget cannot go under
    // its layout's minimumSizeHint even when nothing called setMinimumWidth, so
    // the larger of the two is the real floor.
    auto effectiveMin = [](const QWidget *w) {
        return qMax(w->minimumWidth(), w->minimumSizeHint().width());
    };
    if (left)  { m.leftWidth  = left->width();  m.leftMin  = effectiveMin(left); }
    if (right) { m.rightWidth = right->width(); m.rightMin = effectiveMin(right); }
    return m;
}

// THE EDITOR'S DOCKS, MEASURED (lane SPACE-1, 2026-09-14). The panels the
// owner reported missing after a player -> editor switch were not always
// hidden: a restored layout can also bring them back at a degenerate WIDTH
// (the 2026-09-14 screenshot: a left column 20 px wide showing nothing but the
// hierarchy rows' lock icons), which looks exactly the same from the user's
// chair. "Is the panel there" is therefore two numbers, not one, and this is
// where a script reads both.
QVariantList MainWindow::dockReport() const
{
    QVariantList out;
    const QDockWidget *docks[] = { sceneHierarchyDock, sceneNodePropertiesDock, presetsDock,
                                   assetDock, animationDock, scriptConsoleDock };
    for (const QDockWidget *d : docks) {
        if (!d) continue;
        QVariantMap m;
        m.insert("name", d->objectName());
        m.insert("title", d->windowTitle());
        // isVisible() is false for every dock while another page is on screen
        // (they are children of the editor page): the honest reading of "on
        // screen". isVisibleTo(viewPort) is the dock's OWN state — what the
        // editor will show when its page comes back — and the two together are
        // what tells a space-switch defect from a page-switch.
        m.insert("visible", d->isVisible());
        m.insert("shown", viewPort ? d->isVisibleTo(viewPort) : d->isVisible());
        m.insert("floating", d->isFloating());
        // TABS ARE A THIRD THING (lane SPACE-2, owner report: "the timeline
        // widget is gone"). A dock can be open AND unreachable: tabified docks
        // share one space and Qt parks the ones that are not in front
        // off-screen, so `shown` says true for a panel the user cannot see a
        // pixel of. `tabbed` is whether it shares a tab bar with anything, and
        // `current` whether it is the tab in FRONT — the two numbers that tell
        // "behind another tab" from "closed".
        m.insert("tabbed", viewPort ? !viewPort->tabifiedDockWidgets(
                                           const_cast<QDockWidget *>(d)).isEmpty()
                                    : false);
        m.insert("current", isFrontTab(d));
        m.insert("width", d->width());
        m.insert("height", d->height());
        // WHERE IT IS, in the window's own coordinates: what a rig driving
        // xdotool needs to put a pointer on a panel (its title bar's close
        // button, a row in it) without guessing at the dock layout.
        const QPoint topLeft = d->mapTo(const_cast<MainWindow *>(this), QPoint(0, 0));
        m.insert("x", topLeft.x());
        m.insert("y", topLeft.y());
        m.insert("minWidth", qMax(d->minimumWidth(), d->minimumSizeHint().width()));
        m.insert("area", viewPort ? int(viewPort->dockWidgetArea(const_cast<QDockWidget *>(d)))
                                  : 0);
        out.append(m);
    }
    return out;
}

// ---------------------------------------------------------------------------
// THE BOTTOM TRAY'S TABS (smoke S1). One place decides what "the console is
// showing" means, and both the Ctrl+` chord and the editor.tray verb come
// through it.

// WHICH TAB IS IN FRONT, without a QTabBar to ask (lane SPACE-2). Qt gives a
// tabified QDockWidget no "am I the current tab" accessor, and the tab bar
// itself is a private child of the dock area — but it gives the docks a
// reading that IS the answer and that QDockWidget's own code uses for exactly
// this: a tab that is not in front is shown and parked OFF-SCREEN, so
// `geometry().right() < 0` (qdockwidget.cpp emits visibilityChanged(geometry()
// .right() >= 0) on Show for this reason). Measured on the rig: the front tab
// at x=0, the other at x=-1239.
//
// A dock that is not tabbed at all trivially passes, which is what we want:
// with the Timeline and the Console closed, the Assets dock IS the front tab.
bool MainWindow::isFrontTab(const QDockWidget *dock)
{
    return dock && !dock->isHidden() && dock->geometry().right() >= 0;
}

// The bottom area's docks in tab-bar order, and the name each answers to.
QVector<QPair<QString, QDockWidget *>> MainWindow::bottomAreaTabs() const
{
    QVector<QPair<QString, QDockWidget *>> tabs;
    if (assetDock)        tabs.append({ QStringLiteral("assets"), assetDock });
    if (animationDock)    tabs.append({ QStringLiteral("timeline"), animationDock });
    if (scriptConsoleDock) tabs.append({ QStringLiteral("console"), scriptConsoleDock });
    return tabs;
}

// The bottom area's geometry belongs to whichever tab is in FRONT: the other
// two are parked off-screen, so reading the asset browser's own rectangle while
// the Timeline is up answers with Qt's parking spot (x=-836 on the rig) instead
// of the area every one of them fills. Everything that measures "the tray" —
// the Presets line, the height verb, trayState's geometry — reads it here.
QDockWidget *MainWindow::bottomFrontDock() const
{
    for (const auto &tab : bottomAreaTabs())
        if (isFrontTab(tab.second)) return tab.second;
    return assetDock;
}

// WHERE THE BOTTOM AREA STARTS ON SCREEN — the line the Presets panel is
// supposed to meet (owner, 2026-09-12: "the bottom drawer … needs to start at
// the same horizontal line as the asset module").
//
// That is NOT the dock's own top any more: the group's tab bar sits ABOVE the
// dock (setTabPosition(North), lane SPACE-2) and is part of what the user sees
// as the bottom panel, so aligning to the dock left the Presets panel a tab
// bar's height (27 px, measured) below the line. Qt keeps that bar private —
// it is a QTabBar child of the dock area, not reachable through any dock API —
// so it is found by GEOMETRY: the visible tab bar sitting directly on top of
// the front dock, in the same horizontal span.
int MainWindow::bottomAreaTop() const
{
    const QDockWidget *dock = bottomFrontDock();
    if (!dock || !viewPort) return 0;
    const QRect area = dock->geometry();
    int top = area.top();
    for (const QTabBar *bar : viewPort->findChildren<QTabBar *>()) {
        if (!bar->isVisible() || bar->parentWidget() == dock) continue;
        QRect g = bar->geometry();
        if (bar->parentWidget() && bar->parentWidget() != viewPort)
            g.moveTopLeft(bar->parentWidget()->mapTo(viewPort, g.topLeft()));
        if (g.bottom() > area.top() || g.bottom() < area.top() - 8) continue;   // not on top of it
        if (g.right() < area.left() || g.left() > area.right()) continue;       // not over it
        top = std::min(top, g.top());
    }
    return top;
}

QString MainWindow::trayTab() const
{
    if (!assetDock) return QString();
    for (const auto &tab : bottomAreaTabs())
        if (isFrontTab(tab.second)) return tab.first;
    // Nothing in the bottom area is on screen (every panel there is closed, or
    // the editor page is not up): no tab is in front, but the area exists.
    return QStringLiteral("assets");
}

QStringList MainWindow::trayTabs() const
{
    QStringList names;
    for (const auto &tab : bottomAreaTabs())
        if (!tab.second->isHidden()) names << tab.first;
    return names;
}

bool MainWindow::isConsoleTabVisible() const
{
    return scriptConsoleDock && !scriptConsoleDock->isHidden();
}

bool MainWindow::isTrayVisible() const
{
    return assetDock && assetDock->isVisible();
}

bool MainWindow::isConsoleInputFocused() const
{
    return scriptConsole && scriptConsole->inputHasFocus();
}

// THE CONSOLE TAB IS THE CONSOLE DOCK (lane SPACE-2). Showing it adds a tab to
// the bottom area's one tab bar and brings it to the front; hiding it takes the
// tab away and leaves the other two exactly as they were. `widgetStates` is
// updated with it, like every other panel, so a space switch and a restart
// carry the answer (applyDockVisibilityForSpace is the only other writer).
//
// The old implementation had to un-hide the TRAY to show the console, because
// the console lived inside the tray's widget — and then put it back, which is
// what `trayForcedVisible` was for. A dock of its own needs none of that: the
// console can be open with the asset browser closed.
void MainWindow::setConsoleTabVisible(bool visible, bool focusInput)
{
    if (!scriptConsoleDock) return;
    if (visible) {
        // WHAT CTRL+` INTERRUPTED, so the second press can put it back. Qt
        // picks the NEIGHBOURING tab when the current one disappears, which
        // handed the bottom area to the Timeline every time the console was
        // closed (app.input_keys caught it) — the console is a visitor, and a
        // visitor leaves the room the way it found it.
        for (const auto &tab : bottomAreaTabs())
            if (tab.first != QLatin1String("console") && isFrontTab(tab.second)) {
                bottomReturnTab = tab.first;
                break;
            }
    }
    widgetStates[(int) Widget::CONSOLE] = visible;
    scriptConsoleDock->setVisible(visible);
    if (!visible) {
        bottomFrontTab = bottomReturnTab;
        raiseBottomFrontTab();
        return;
    }
    bottomFrontTab = QStringLiteral("console");
    scriptConsoleDock->raise();
    if (focusInput && scriptConsole) scriptConsole->focusInput();
}

bool MainWindow::setTrayTab(const QString &tab, bool focusConsoleInput)
{
    if (!assetDock) return false;
    const QString wanted = tab.trimmed().toLower();
    if (wanted == QLatin1String("console")) {
        setConsoleTabVisible(true, focusConsoleInput);
        return true;
    }
    // Selecting a tab does NOT close any other: the tabs stay in the bar (that
    // is what a tab bar is for) — the title-bar X and Ctrl+` are what remove
    // one. A panel the user closed cannot be raised, though: naming a closed
    // tab OPENS it, which is what "show me this tab" means from a script.
    for (const auto &entry : bottomAreaTabs()) {
        if (entry.first != wanted) continue;
        if (entry.second->isHidden()) {
            entry.second->setVisible(true);
            if (entry.second == assetDock)     widgetStates[(int) Widget::ASSETS]   = true;
            if (entry.second == animationDock) widgetStates[(int) Widget::TIMELINE] = true;
        }
        entry.second->raise();
        return true;
    }
    return false;
}

void MainWindow::toggleScriptConsole()
{
    if (!scriptConsoleDock) return;
    // "Showing" means both: the tab is in the bar AND it is the tab in front.
    // Anything less and Ctrl+` brings it forward rather than closing something
    // the user cannot see.
    setConsoleTabVisible(!(isConsoleTabVisible() && isFrontTab(scriptConsoleDock)));
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
	ThemeManager::applyHeaderGlyphButton(publish_menu, headerGlyphFont());
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
	// Sheet + font together, through the one helper: the three header glyphs
	// (Publish, Help, Preferences) are the same size and sit on the header's
	// own colour instead of Qlementine's grey button plate.
	ThemeManager::applyHeaderGlyphButton(help, headerGlyphFont());
	help->setCursor(Qt::PointingHandCursor);

    connect(help, &QPushButton::pressed, []() {
        QDesktopServices::openUrl(QUrl("https://www.jahshaka.com/learn/resources/"));
	});

	prefs = new QPushButton;
	prefs->setObjectName("prefsButton");

    //prefs->setText(QChar(fa::cog));
    // for adapting Qt6.9.0
    prefs->setText(QChar(static_cast<ushort>(fa::cog)));
	// (Classic still gets PrefsButton() — the helper picks by object name.)
	ThemeManager::applyHeaderGlyphButton(prefs, headerGlyphFont());
	prefs->setCursor(Qt::PointingHandCursor);

	connect(prefs, &QPushButton::pressed, [this]() { showPreferences(); });

	QWidget *buttons = new QWidget;
	QHBoxLayout *bl = new QHBoxLayout;
	buttons->setLayout(bl);
	bl->setSpacing(20);
	ThemeManager::applyHeaderGlyphButton(publish_menu, headerGlyphFont());
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
    screenShotBtn->setToolTip(tr("Photograph the viewport at 1920x1080 — this camera, this lens, "
                                 "and the world's own settings (global illumination, reflections, "
                                 "ambient occlusion, bloom, anti-aliasing, the looks stack and the "
                                 "exposure the view is currently at)"));
    screenShotBtn->setToolTipDuration(-1);
    screenShotBtn->setStyleSheet(StyleSheet::BackgroundTransparent());
    screenShotBtn->setIcon(QIcon(":/icons/icons8-camera-48.png"));
	screenShotBtn->setIconSize(QSize(16,17));

    wireFramesButton = new QToolButton;
    wireFramesButton->setStyleSheet(StyleSheet::ViewportMenuButton());
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
        SettingsManager::getDefaultManager()->getValue("show_fps", Constants::SHOW_FPS_DEFAULT).toBool());
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
    viewsButton->setStyleSheet(StyleSheet::ViewportMenuButton());
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
    // The button SHOWS the current view, it does not advertise the menu: a
    // static "Views" label told the user nothing about which view they were
    // in (owner report 2026-09-07). It starts on Perspective — the viewport's
    // own starting view — and follows every path that changes it, the
    // dropdown, the view.* shortcuts and editor.setView alike, because they
    // all land in applyCameraView.
    viewsButton->setToolTip(tr("Canonical camera views"));
    setViewsButtonLabel(QStringLiteral("perspective"));
    viewsButton->setPopupMode(QToolButton::InstantPopup);

    // Camera ▾ — the switcher (CAMERAS_SPEC D4): the Viewport (explorer) plus
    // every scene camera by name. Choosing a camera PILOTS it; choosing
    // Viewport ejects. It is rebuilt on every open rather than kept in sync,
    // because the list is the document's and the document changes underneath it
    // (a camera added, renamed, deleted, a whole world closed) — and a stale
    // entry would hand the viewport a stale node.
    camerasButton = new QToolButton;
    camerasButton->setStyleSheet(StyleSheet::ViewportMenuButton());
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
	cameraView->setStyleSheet(StyleSheet::ViewportCameraToggle());
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
    playerControls->setStyleSheet(StyleSheet::PlayerControlsBar());

    auto playerControlsLayout = new QHBoxLayout;

    restartBtn = new QPushButton;
    restartBtn->setCursor(Qt::PointingHandCursor);
    restartBtn->setToolTip("Restart playback");
    restartBtn->setToolTipDuration(-1);
    restartBtn->setStyleSheet(StyleSheet::BackgroundTransparent());
    ThemeRoles::setFlat(restartBtn);
    restartBtn->setIcon(QIcon(":/icons/rotate-to-right.svg"));
    restartBtn->setIconSize(QSize(16, 16));

    playBtn = new QPushButton;
    playBtn->setCursor(Qt::PointingHandCursor);
    playBtn->setToolTip("Play the scene");
    playBtn->setToolTipDuration(-1);
    playBtn->setStyleSheet(StyleSheet::BackgroundTransparent());
    ThemeRoles::setFlat(playBtn);
    playBtn->setIcon(QIcon(":/icons/g_play.svg"));
    playBtn->setIconSize(QSize(24, 24));

    stopBtn = new QPushButton;
    stopBtn->setCursor(Qt::PointingHandCursor);
    stopBtn->setToolTip("Stop playback");
    stopBtn->setToolTipDuration(-1);
    stopBtn->setStyleSheet(StyleSheet::BackgroundTransparent());
    ThemeRoles::setFlat(stopBtn);
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
    // THE RIGHT COLUMN RUNS TO THE BOTTOM (owner, 2026-09-12): the bottom-right
    // corner belongs to the right dock area, so Properties + Presets extend the
    // full height of the editor and the bottom Tray (Assets | Console,
    // Timeline) stops at the right column's edge instead of running under it.
    viewPort->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    // The engine viewport is the only renderer. When the engine cannot start
    // (offscreen platform: --headless scripts, --dump-api-docs) a document-only
    // stand-in serves the document verbs; nothing renders.
    sceneView = nullptr;
    {
        auto &host = EngineHost::instance();
        QString error;
        // The engine starts even on the offscreen platform now — the document
        // scene graph IS the engine's (SPECS/SCENEGRAPH_SPEC.md D2), so a
        // --headless run needs one. What it does NOT get is an on-screen
        // viewport: nothing can present into a widget that has no native
        // window, and the document-only stand-in below is what those runs have
        // always used.
        // ASK THE ENGINE, not the platform name. A headless engine (NULL render
        // system, SPECS/SCENEGRAPH_SPEC.md §3b) can hold no View of any kind, so
        // it gets the stand-in; anything else renders. Testing for "xcb" here
        // was a Linux-shaped bug: on macOS the platform is `cocoa` and the
        // editor would have fallen back to the document-only viewport on a
        // machine whose on-screen Metal/Vulkan viewport works
        // (SPECS/MACOS_VIEWPORT_SPEC.md).
        if (host.start(error) && !host.engine()->isHeadless()) {
            sceneView = createEngineSceneViewport(host.engine(), host.driver(), viewPort);
            // Non-owning: step 5 of the shutdown order checks it (see
            // destroyEngineViews / shell/shutdownorder.h).
            mEngineWatch = host.engine();
            // PANEL-AWARE PACING (fps audit F1): no more literal 16. The driver
            // derives its interval from the screen the window is on and from
            // the persisted pacing mode; wireFramePacing() below feeds it both
            // and keeps feeding it across screen and refresh-rate changes.
            wireFramePacing();
            host.driver()->start();
        } else if (!error.isEmpty()) {
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
	playerBackend = nullptr;
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
    setShowFrameStats(SettingsManager::getDefaultManager()->getValue("show_fps", Constants::SHOW_FPS_DEFAULT).toBool());

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
    // A DROP RESTS ON WHAT IT WAS DROPPED ON (owner, 2026-09-14). These two
    // signals are the drag-and-drop route and nothing else, so this is where
    // "the point under the cursor" becomes "the surface under the cursor";
    // assets.addToScene and the menus keep placing the pivot.
    connect(events, &EditorViewportEvents::addDroppedMesh, this, [this](QString path, bool v, iris::Vec3 pos, QString guid, QString name) {
        addMaterialMesh(path, v, pos, guid, name, surfaceplacement::Placement::OnSurface);
    });

    // Straight to the service, WITH the drop point (smoke S2). The shell hop
    // this replaced (MainWindow::addPrimitiveObject) forwarded one argument and
    // had exactly one caller — this lambda.
    connect(events, &EditorViewportEvents::addPrimitive, this,
            [this](QString guid, iris::Vec3 position) {
        sceneEditService->addPrimitive(guid, position, surfaceplacement::Placement::OnSurface);
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

	// WHICH EDITOR PANELS ARE OPEN. These are the defaults — all five panels
	// open, the script console closed — and a
	// restored dock layout overwrites them in setupDockWidgets, which runs
	// after this. (The commented-out `widgets` QSettings key that used to sit
	// here was dead for years: nothing ever wrote it. The dock layout itself
	// carries the answer now. Lane SPACE-1, CRUD.)
	widgetStates = QVector<bool>(6);
	widgetStates[static_cast<int>(Widget::HIERARCHY)]	= true;
	widgetStates[static_cast<int>(Widget::PROPERTIES)]	= true;
	widgetStates[static_cast<int>(Widget::ASSETS)]		= true;
	widgetStates[static_cast<int>(Widget::TIMELINE)]	= true;
	widgetStates[static_cast<int>(Widget::PRESETS)]		= true;
	// …and the console CLOSED: Ctrl+` is what opens it (lane SPACE-2).
	widgetStates[static_cast<int>(Widget::CONSOLE)]		= false;
}

void MainWindow::setupDesktop()
{
	pmContainer = new ProjectManager(db, project, this);
	pmContainer->mainWindow = this;
	projectService->setProjectManager(pmContainer);
	// Preferences -> Desktop -> Slider Rows applies LIVE (re-audit F8): the
	// page's signal reaches the desktop through the same ProjectManager entry
	// point desktop.setSliderRows uses.
	if (prefsDialog) prefsDialog->wireDesktop(pmContainer);
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
	// THE PAGE -> MODULE SEAM (AVATAR_ASSET_SPEC §5.5): a page asks for an
	// asset to be opened in a module; the shell switches space and calls that
	// module's VERB. Neither side learns about the other.
	connect(_assetView, &AssetView::editAssetInModule, this, &MainWindow::openAssetInModule);
	// THE IMPORT DECISION (§8) — the same two handlers the project tray gets.
	connect(_assetView, &AssetView::reimportAssetRequested, this,
	        [this](const QString &guid) { openImportSettings(guid); });
	// A reimport changed the asset's bake, its size line and its thumbnail:
	// the library view and the project tray both re-read the row.
	// QUEUED: the signal is emitted from inside the dialog's accept(), and a
	// tile re-selection loads a preview — not something to start while the
	// dialog is still closing.
	connect(this, &MainWindow::assetReimported, this, [this](const QString &guid) {
		if (_assetView) _assetView->selectAsset(guid);
		if (assetWidget) assetWidget->refresh();
	}, Qt::QueuedConnection);

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
	// VR (SPECS/VR_SPEC.md §4.6, phase 2): verbs only, NO page — the session's
	// UI is phase 3's (the Player's VR mode) and phase 4's (the editor
	// preview), and both will call the same `vr.*` verbs this module
	// registers. A module with no page still gets a place in the loop.
	vrModule = new VrModule;
	// The Player space contributes VERBS only (verb-coverage audit F1): its
	// page is PlayerWidget, built in setupViewPort, because the stacked-widget
	// index order is load-bearing (PLAYER = 4).
	playerModule = new PlayerModule;
	modules = { materialsModule, publishModule, avatarModule, playerModule, vrModule };
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

    actionGlobalSpace = new QAction;
    actionGlobalSpace->setObjectName(QStringLiteral("actionGlobalSpace"));
    actionGlobalSpace->setCheckable(true);
	actionGlobalSpace->setToolTip("Global Space | Move objects relative to the global world");
	actionGlobalSpace->setIcon(fontIcons->icon(fa::globe, options));
	toolBar->addAction(actionGlobalSpace);

    actionLocalSpace = new QAction;
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

	// CAMERA SPEED (owner request 2026-09-07, Unreal's control). A multiplier
	// on the fly base, in the toolbar beside the two camera-mode buttons it
	// belongs with. The value lives in FlySpeedSettings (persisted) and the
	// verb editor.setFlySpeed owns writing it; this combo reads and writes
	// through the same place the scroll wheel does, so the three can never
	// disagree.
	flySpeedCombo = new QComboBox;
	flySpeedCombo->setObjectName(QStringLiteral("flySpeedCombo"));
	flySpeedCombo->setToolTip("Camera Speed | Multiplier on the fly speed (8 u/s). "
	                          "Scroll the wheel while holding the right mouse button in the viewport.");
	for (float step : FlySpeedSettings::steps())
		flySpeedCombo->addItem(QString("%1x").arg(double(step)));
	flySpeedCombo->setFocusPolicy(Qt::NoFocus);   // never steal the fly keys
	connect(flySpeedCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
		const QVector<float> &steps = FlySpeedSettings::steps();
		if (index < 0 || index >= steps.size()) return;   // the off-ladder entry
		FlySpeedSettings::setMultiplier(FlySpeedSettings::Editor, steps[index]);
	});
	toolBar->addWidget(flySpeedCombo);
	syncFlySpeedUi();

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
    // The toolbar starts on whatever the GIZMOS actually are, not on a guess.
    // It used to hard-check Global while Gizmo's constructor left every gizmo
    // in LOCAL space and nothing ever reconciled the two — the buttons lied
    // until the user clicked one (found writing editor.gizmoSpace, F12).
    if (gizmoTransformSpace() == QLatin1String("local")) actionLocalSpace->setChecked(true);
    else                                                 actionGlobalSpace->setChecked(true);

    connect(actionFreeCamera,   SIGNAL(triggered(bool)), SLOT(useFreeCamera()));
    connect(actionArcballCam,   SIGNAL(triggered(bool)), SLOT(useArcballCam()));

    cameraGroup = new QActionGroup(viewPort);
    cameraGroup->addAction(actionFreeCamera);
    cameraGroup->addAction(actionArcballCam);
    actionFreeCamera->setChecked(true);

    // THE VR TOGGLE (SPECS/VR_SPEC.md §4.5, phase 3). One action, beside the
    // camera controls it belongs with: press it and the Player page comes up
    // with the scene running in the headset; press it again and the run stops.
    // It calls PlayerService, which is what the `vr.toggle()` verb calls — the
    // button is a caller of the capability, never a second path into it.
    //
    // fa::binoculars is the closest thing the shipped icon font (Font Awesome
    // 4) has to a headset: a two-lens device held to the eyes. Stated because
    // it is a choice, not an obvious match.
    actionVr = new QAction;
    actionVr->setObjectName(QStringLiteral("actionVr"));
    actionVr->setCheckable(true);
    actionVr->setIcon(fontIcons->icon(fa::binoculars, options));
    toolBar->addAction(actionVr);
    connect(actionVr, &QAction::triggered, this, [this]() { toggleVrMode(); });
    refreshVrUi();

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

	// A REPORT, NOT A COMMAND. The viewport telling the toolbar what its camera
	// now is must only repaint the button — routing it through
	// projectionChangeRequested would make every such report re-issue a view
	// change (and, since the change is a canonical view now, snap the camera).
	connect(sceneView->events(), &EditorViewportEvents::updateToolbarButton, this, [this]() {
		if (!sceneView || !sceneView->editorCamera()) return;
		syncProjectionButton(sceneView->editorCamera()->isPerspective);
		setViewsButtonLabel(sceneView->cameraView());
	});

	// The scroll wheel stepped the fly speed while the camera was flying: show
	// the new multiplier over the viewport and move the dropdown to match.
	connect(sceneView->events(), &EditorViewportEvents::flySpeedChanged, this, [this]() {
		syncFlySpeedUi();
		showViewportToast("Camera Speed",
		                  QString("%1x  (%2 u/s)")
		                      .arg(double(FlySpeedSettings::multiplier(FlySpeedSettings::Editor)))
		                      .arg(double(FlySpeedSettings::speed(FlySpeedSettings::Editor))));
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
    // The ARROW CLUSTER, not W/A/S/D (owner decision 2026-09-09): the editor's
    // fly moved off the letters so tool shortcuts can have them back. The
    // PLAYER still answers to both spellings — its rows are the Gameplay
    // section below, driven by the InputMap.
    reg.addFixed("camera.fly", "Fly Camera (free camera)", "Camera",
                 "RMB (hold) + Arrow keys + PageUp/PageDown \xc2\xb7 Shift: 3x");
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
    // Ctrl+F4 — THE CAPTURE KEY (owner, 2026-09-12: "I would prefer to activate
    // the monitor Ctrl+F4 and then it captures the next 20 seconds of data for
    // you"). EDITOR ONLY, and deliberately: the monitor's scope is the editor
    // viewport's frame and everything it drives (RENDER_LOOP_MONITOR_SPEC
    // SCOPE). Pressed again while recording it stops early and writes what it
    // has. It goes through perf.capture / perf.stop — the same verbs a script
    // and the MCP tool call — never a second path (SCRIPTING_SPEC §2.3).
    //
    // NOTHING IS DRAWN by this beyond the two toasts wired in
    // connectFrameMonitorToasts(): an on-screen display would itself cost frame
    // time and passes and contaminate what the capture measures.
    reg.add("perf.capture", "Capture Render Monitor Data (20 s)", "View",
            QKeySequence(Qt::CTRL | Qt::Key_F4), this, [this]() {
                if (currentSpace != WindowSpaces::EDITOR) return;
                if (FrameMonitor::instance().isRecording()) { FrameMonitor::instance().stop(); return; }
                FrameMonitor::Request request;
                if (project) request.label = project->getProjectName();
                QString error;
                if (!FrameMonitor::instance().start(request, &error))
                    showViewportToast(tr("Render Monitor"), error);
            });

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

    // Delete / Ctrl+D / Ctrl+C / Ctrl+V (EDITOR_MULTISELECT_SPEC §2.6). Same
    // single-claimant rule as Ctrl+Z, for the same measured reason: the
    // Materials graph used to own bare WindowShortcuts on exactly these four
    // chords, and two WindowShortcut claimants make Qt drop the chord entirely.
    // The registry is the one claimant and the ACTIVE SPACE decides what it
    // means — the editor's selection SET here, the node graph there.
    //
    // A text field is safe: QLineEdit/QTextEdit accept the ShortcutOverride for
    // their standard editing keys, so a WindowShortcut never fires while one
    // has focus (the tree's inline rename editor is the case that matters, and
    // app.input_keys probes it on the rig).
    reg.add("edit.delete", "Delete Selection", "Editing", QKeySequence(Qt::Key_Delete), this,
            [this]() { deleteActiveSpace(); });
    reg.add("edit.duplicate", "Duplicate Selection", "Editing", QKeySequence(Qt::CTRL | Qt::Key_D), this,
            [this]() { duplicateActiveSpace(); });
    reg.add("edit.copy", "Copy Selection", "Editing", QKeySequence(Qt::CTRL | Qt::Key_C), this,
            [this]() { copyActiveSpace(); });
    // Ctrl+X. The third chord of the set, and the one that was missing: a
    // clipboard whose copy travels to another instance but whose CUT does not
    // exist is half a clipboard. Same single-claimant routing, same text-field
    // rule as the two above (a focused QLineEdit accepts the ShortcutOverride
    // for Cut before a WindowShortcut can fire).
    reg.add("edit.cut", "Cut Selection", "Editing", QKeySequence(Qt::CTRL | Qt::Key_X), this,
            [this]() { cutActiveSpace(); });
    reg.add("edit.paste", "Paste", "Editing", QKeySequence(Qt::CTRL | Qt::Key_V), this,
            [this]() { pasteActiveSpace(); });
    // Ctrl+A (EDITOR_MULTISELECT_SPEC §8.7, decided 2026-09-09). Same
    // single-claimant routing as the four chords above — and the same TEXT
    // FIELD rule, made explicit rather than left to Qt: selectAllActiveSpace
    // hands the chord to a focused QLineEdit/QTextEdit/QPlainTextEdit/spin box
    // instead of the scene, so Ctrl+A in the console input, an inline rename or
    // a transform field selects THAT text. Qt's own ShortcutOverride usually
    // gets there first (QWidgetLineControl accepts QKeySequence::SelectAll),
    // but "usually" is not a contract to hang the scene selection on.
    reg.add("edit.selectAll", "Select All", "Editing", QKeySequence(Qt::CTRL | Qt::Key_A), this,
            [this]() { selectAllActiveSpace(); });

    // ---- file / windows ----
    reg.add("file.save", "Save Scene", "File", QKeySequence(Qt::CTRL | Qt::Key_S), this,
            [this]() { saveScene(); });
    // Ctrl+` = the Console TAB of the bottom tray (smoke S1). One function for
    // the chord and for `editor.tray`, so the verb the suites drive is the code
    // path the key takes: show the tab, raise the tray, AND put the keyboard in
    // the input line (Ctrl+` used to open a console that still needed a mouse
    // click before it would take a character, which also meant the chord rules
    // the console is the natural place to exercise — Ctrl+A belongs to a
    // focused text field — could not be reached from the keyboard at all).
    reg.add("console.toggle", "Script Console", "Windows",
            QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft), this,
            [this]() { toggleScriptConsole(); });
    reg.add("claude.toggle", "Claude Assistant", "Windows",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), this,
            [this]() { toggleClaudeChat(); });
    // THE RIGHT COLUMN'S TWO TABS (PROPERTY_FILTER_SPEC D2): one toggle, not two
    // keys. Ctrl+Tab is taken by space.previous, so Ctrl+Shift+P — verified
    // free against the 50 rows already registered here, and remappable in
    // Preferences → Shortcuts like every other row. (ShortcutRegistry's
    // conflict check runs on a USER rebinding, not on these defaults: two
    // defaults claiming one chord would simply both be registered, so the
    // default above was checked by hand.)
    reg.add("properties.tab", "Properties: World / Selection Tab", "Windows",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this, [this]() {
        if (!sceneNodePropertiesWidget) return;
        sceneNodePropertiesWidget->setPropertiesTab(
            sceneNodePropertiesWidget->propertiesTab() == SceneNodePropertiesWidget::Tab::World
                ? SceneNodePropertiesWidget::Tab::Selection
                : SceneNodePropertiesWidget::Tab::World);
    });
    // THE PROPERTY FILTER'S BOX (PROPERTY_FILTER_SPEC D1): Ctrl+F, which is the
    // universal find key and was free in the registry — the only "Ctrl+F" in
    // src/ is the Ctrl+F4 render-capture tooltip, and plain F (camera.focus) is
    // a different chord. It focuses the box of the tab ON SCREEN, since each
    // tab has its own filter. (A QLineEdit accepts the ShortcutOverride for
    // unmodified printable keys, so typing "f" into the box does not fire
    // camera.focus.)
    reg.add("properties.filter", "Properties: Filter Rows", "Windows",
            QKeySequence(Qt::CTRL | Qt::Key_F), this, [this]() {
        if (!propertiesTabStrip) return;
        // A dock tabbed BEHIND another is visible (shown, parked off-screen — the
        // SPACE-2 fact), so the test is "in front", not "visible": otherwise the
        // shortcut focused a filter box the user could not see (PROPS-SMALL-1).
        if (sceneNodePropertiesDock && !isFrontTab(sceneNodePropertiesDock))
            setPanelOpen(QStringLiteral("properties"), true);
        propertiesTabStrip->focusFilter();
    });
    // Esc is a widget-level key inside the box, not a registry binding — the
    // row exists so the Preferences table says so.
    reg.addFixed("properties.filter.clear", "Properties: Clear the Filter", "Windows",
                 "Esc (while the filter box has focus)");
    // VR (SPECS/VR_SPEC.md §4.5). Its own row rather than a "Windows" one: it
    // is not a space switch, it is a MODE — the Player page comes up and the
    // run happens in the headset.
    reg.add("vr.toggle", "Enter / leave VR", "VR", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V),
            this, [this]() { toggleVrMode(); });
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

    // ---- gameplay (AVATAR_LOCOMOTION_SPEC §8.2) ----
    // FIXED rows on purpose. These four are not QShortcuts and must never
    // become any: they are HELD, combined and polled (W+A is a diagonal, Shift
    // is a modifier held for seconds), they only exist while the scene is
    // playing, and a QShortcut on W is exactly what stops W from reaching play
    // mode today. The rebindable half lives in the InputMap — `input.bind`
    // writes it and refreshGameplayShortcutRows() re-labels these rows.
    iris::InputSystem::instance().setSettings(settings->settings);
    reg.addFixed("gameplay.move",   "Move (play mode)",   "Gameplay", "W / S / A / D");
    reg.addFixed("gameplay.look",   "Look (play mode)",   "Gameplay", "Mouse");
    reg.addFixed("gameplay.jump",   "Jump (play mode)",   "Gameplay", "Space");
    reg.addFixed("gameplay.sprint", "Sprint (play mode)", "Gameplay", "Shift");
    refreshGameplayShortcutRows();
}

void MainWindow::refreshGameplayShortcutRows()
{
    if (!shortcutRegistry) return;
    const iris::InputMap &map = iris::InputSystem::instance().map();
    shortcutRegistry->setFixedText("gameplay.move",   map.displayText(iris::InputAction::Move));
    shortcutRegistry->setFixedText("gameplay.look",   map.displayText(iris::InputAction::Look));
    shortcutRegistry->setFixedText("gameplay.jump",   map.displayText(iris::InputAction::Jump));
    shortcutRegistry->setFixedText("gameplay.sprint", map.displayText(iris::InputAction::Sprint));
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
    showViewportToast("Snap Size", text);
}

// The transient readout over the viewport — one toast, reused, for every
// "you just changed this with a gesture" message (snap size, fly speed). It was
// stepSnapSize's tail; the fly-speed wheel needed the identical five lines.
// THE SCENE-ERROR AREA. The bar is a view of SceneIssues and owns no state;
// this is the whole of the shell's involvement — build it lazily over the
// viewport and tick the scanner. Nothing is wired INTO the bar: it has no
// buttons and emits nothing (owner, 2026-09-13 — it shows the errors and the
// user fixes them in the scene).
void MainWindow::wireSceneIssues()
{
    if (sceneIssueTimer) return;
    sceneIssueTimer = new QTimer(this);
    sceneIssueTimer->setInterval(1000);
    connect(sceneIssueTimer, &QTimer::timeout, this, [this]() { updateSceneIssues(); });
    sceneIssueTimer->start();
}

// ONE PASS: scan the open scene, and decide whether the bar may be on screen.
// Driven by the 1 Hz timer and by every space switch.
void MainWindow::updateSceneIssues()
{
    // THE BAR IS AN EDITOR SURFACE, and it is a FRAMELESS TOP-LEVEL WITH
    // WindowStaysOnTopHint (sceneissuebar.cpp) — so without this check it
    // floated over the Desktop, Assets, Player and Materials pages, describing
    // a scene nobody is looking at (item 3). The comment below promised this
    // check for a week; here it is.
    if (currentSpace != WindowSpaces::EDITOR) {
        if (sceneIssueBar) sceneIssueBar->setEditorActive(false);
        return;
    }
    if (sceneIssueBar) sceneIssueBar->setEditorActive(true);
    if (!sceneEditService) return;
    auto scene = sceneEditService->scene();
    if (!scene) { SceneIssues::instance().reset(); return; }
    SceneIssues::instance().scan(scene);
    if (!sceneIssueBar && SceneIssues::instance().count() > 0) {
        sceneIssueBar = new SceneIssueBar(this);
        // Under the engine-drawn frame-stats rows (three lines plus their
        // inset) so the two never overlap when F3 is on.
        sceneIssueBar->setAnchor(sceneView ? sceneView->asWidget() : nullptr, 96);
        sceneIssueBar->refresh();
    }
}

// What the bar is showing, for `editor.issueBar()` — the seam the shell's half
// of the error area is tested through (item 3's case in
// scripting.e2e.scene_issues).
QVariantMap MainWindow::sceneIssueBarState() const
{
    QVariantMap out;
    out[QStringLiteral("editorActive")] = (currentSpace == WindowSpaces::EDITOR);
    out[QStringLiteral("exists")] = sceneIssueBar != nullptr;
    out[QStringLiteral("visible")] = sceneIssueBar && sceneIssueBar->isVisible();
    out[QStringLiteral("rows")] = SceneIssues::instance().count();
    // What is actually BUILT: one line per issue (plus the "+N more" line), and
    // no clickable control anywhere in it. `buttons` is asserted to be zero by
    // scripting.e2e.scene_issues — the owner's "just show the error" rule, in a
    // form that cannot rot.
    out[QStringLiteral("lines")] = sceneIssueBar ? sceneIssueBar->lineCount() : 0;
    out[QStringLiteral("buttons")] = sceneIssueBar ? sceneIssueBar->buttonCount() : 0;
    return out;
}

// THE PANEL RE-READS THE DOCUMENT (round 2, item 3). Used by the edit gate:
// a row whose write was refused is still showing the refused value, and the
// document is the only thing that knows better. Both halves are deferred —
// refreshFromDocument defers its own rebuild (a blade rebuilt inside a
// control's signal handler is the sky panel's crash), and the transform rows
// are refreshed on the same turn for symmetry.
void MainWindow::refreshPropertiesFromDocument()
{
    if (!sceneNodePropertiesWidget) return;
    sceneNodePropertiesWidget->refreshFromDocument();
    QPointer<MainWindow> self(this);
    QTimer::singleShot(0, this, [self]() {
        if (self && self->sceneNodePropertiesWidget) self->sceneNodePropertiesWidget->refreshTransform();
    });
}

void MainWindow::showViewportToast(const QString &title, const QString &text)
{
    if (!sceneView) return;
    if (!snapToast) snapToast = new Toast(this);
    // Top-centre of the VIEWPORT, through the widget itself (audit F-D4).
    snapToast->setAnchor(Toast::Anchor::WidgetTop, sceneView->asWidget());
    snapToast->showToast(title, text);   // auto-hides
}

// THE FLY-SPEED DROPDOWN follows FlySpeedSettings, never the other way round
// (API-first: editor.setFlySpeed is the verb, this is a view of its value).
// Reached from three directions — the dropdown's own activation, the scroll
// wheel while flying (EditorViewportEvents::flySpeedChanged) and the verb
// (invoked by name) — so the signal is blocked while the index is written or
// the first two would fight.
void MainWindow::syncFlySpeedUi()
{
    if (!flySpeedCombo) return;
    const float mult = FlySpeedSettings::multiplier(FlySpeedSettings::Editor);
    const QVector<float> &steps = FlySpeedSettings::steps();
    int index = -1;
    for (int i = 0; i < steps.size(); ++i)
        if (qFuzzyCompare(steps[i], mult)) { index = i; break; }
    QSignalBlocker blocked(flySpeedCombo);
    if (index >= 0) {
        // A step value: show the ladder entry.
        if (flySpeedCombo->count() > steps.size()) flySpeedCombo->removeItem(steps.size());
        flySpeedCombo->setCurrentIndex(index);
    } else {
        // A verb set something off the ladder (0.05..32 is legal, the ladder is
        // only what the UI offers). Show it as a trailing entry rather than
        // lying about which step is active.
        const QString label = QString("%1x").arg(double(mult));
        if (flySpeedCombo->count() > steps.size()) flySpeedCombo->setItemText(steps.size(), label);
        else                                       flySpeedCombo->addItem(label);
        flySpeedCombo->setCurrentIndex(steps.size());
    }
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

	// (A dead "Save" button lived here — built, connected to a body that was
	// entirely commented out, and never added to the layout. The panels the
	// user leaves open are saved with the rest of the dock layout at exit now.
	// Lane SPACE-1, CRUD.)
	QWidget *cw = new QWidget;
	QHBoxLayout *cl = new QHBoxLayout;
    cl->setContentsMargins(0, 0, 0, 0);
	cw->setLayout(cl);
	cl->addWidget(closeAll);
	cl->addWidget(restoreAll);
	dl->addWidget(cw);

	// EVERY BUTTON IS `setPanelOpen` (lane SPACE-2). The five toggles used to
	// call setVisible and write `widgetStates` themselves, five copies of the
	// two lines, and Close All used close() instead — so "closed from the
	// dialog" and "closed from the X" were two different states of the same
	// panel. One function, one meaning, and the same one the `editor.panel`
	// verb and the tests drive. (The script console is deliberately not among
	// these: Ctrl+` is its switch, and "Restore All" is about the panels the
	// editor is made of.)
	static const QStringList kDialogPanels = { QStringLiteral("hierarchy"),
											   QStringLiteral("properties"),
											   QStringLiteral("presets"),
											   QStringLiteral("timeline"),
											   QStringLiteral("assets") };
	connect(hierarchy,  &QPushButton::toggled, [this](bool set) { setPanelOpen("hierarchy", set); });
	connect(properties, &QPushButton::toggled, [this](bool set) { setPanelOpen("properties", set); });
	connect(presets,    &QPushButton::toggled, [this](bool set) { setPanelOpen("presets", set); });
	connect(timeline,   &QPushButton::toggled, [this](bool set) { setPanelOpen("timeline", set); });
	connect(assets,     &QPushButton::toggled, [this](bool set) { setPanelOpen("assets", set); });

	connect(closeAll,	&QPushButton::pressed,	[&]() {
		for (const QString &panel : kDialogPanels) setPanelOpen(panel, false);

		hierarchy->setChecked(false);
		properties->setChecked(false);
		assets->setChecked(false);
		timeline->setChecked(false);
		presets->setChecked(false);
	});

	connect(restoreAll, &QPushButton::pressed,	[&]() {
		for (const QString &panel : kDialogPanels) setPanelOpen(panel, true);

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

void MainWindow::updateSceneSettings()
{
	// All three outline values in one push, from the one place that owns them
	// (services/outlinesettings.h). The page used to hand over two member
	// variables it had parsed itself, so a value written by anything other than
	// the page — a verb, a fresh install's default — was invisible here.
	if (projectService->isSceneOpen() || !!scene) outlinesettings::apply(scene.data());

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

// The four edit chords, routed the same way and for the same reason
// (EDITOR_MULTISELECT_SPEC §2.6). Deliberately NOT fallbacks: with the
// Materials space active they act on the GRAPH and never quietly on a scene
// selection the user cannot see, exactly as undoActiveSpace decided.
void MainWindow::deleteActiveSpace()
{
    if (currentSpace == WindowSpaces::EFFECT) {
        if (shaderGraph) shaderGraph->graphDeleteSelected();
        return;
    }
    if (currentSpace == WindowSpaces::EDITOR) deleteNode();
}

void MainWindow::duplicateActiveSpace()
{
    if (currentSpace == WindowSpaces::EFFECT) {
        if (shaderGraph) shaderGraph->graphDuplicateSelected();
        return;
    }
    if (currentSpace == WindowSpaces::EDITOR) duplicateNode();
}

void MainWindow::copyActiveSpace()
{
    if (currentSpace == WindowSpaces::EFFECT) {
        if (shaderGraph) shaderGraph->graphCopySelected();
        return;
    }
    // ONE clipboard now (CLIPBOARD_SPEC D3 b): the editor writes the same
    // system clipboard the Materials graph does, as a self-identifying text
    // payload, so a copy crosses to a second instance and back. The Materials
    // space keeps its own payload shape for one release (§2.2 `graph` items are
    // P2) — hence the branch above, not a second clipboard.
    if (currentSpace == WindowSpaces::EDITOR && services && services->clipboard &&
        services->selection) {
        const auto result = services->clipboard->copyNodes(services->selection->selectedSet());
        if (result.ok())
            showViewportToast(tr("Copy"), tr("%1 object(s) copied").arg(result.items));
    }
}

void MainWindow::cutActiveSpace()
{
    if (currentSpace == WindowSpaces::EFFECT) {
        // The graph has no cut of its own; deliberately NOT a fallback to the
        // scene, for the reason undoActiveSpace records.
        return;
    }
    if (currentSpace == WindowSpaces::EDITOR && services && services->clipboard &&
        services->selection) {
        const auto result = services->clipboard->cutNodes(services->selection->selectedSet());
        if (result.ok())
            showViewportToast(tr("Cut"), tr("%1 object(s) cut").arg(result.copy.items));
    }
}

// Ctrl+A. Two rules in one place: a focused TEXT ENTRY owns the chord, and
// otherwise the active space decides (EDITOR_MULTISELECT_SPEC §8.7).
//
// The text-entry branch does not merely decline — it performs the select-all on
// the widget. Declining would leave Ctrl+A doing NOTHING in a field Qt did not
// intercept for itself (a spin box is the case), which is a worse answer than
// either alternative. Every one of the four classes exposes `selectAll()` as a
// public slot, so one invokeMethod covers them all — and covers any future
// widget that offers the same slot.
void MainWindow::selectAllActiveSpace()
{
    if (QWidget *focus = QApplication::focusWidget()) {
        if (qobject_cast<QLineEdit *>(focus) || qobject_cast<QTextEdit *>(focus) ||
            qobject_cast<QPlainTextEdit *>(focus) || qobject_cast<QAbstractSpinBox *>(focus)) {
            QMetaObject::invokeMethod(focus, "selectAll");
            return;
        }
    }
    if (currentSpace == WindowSpaces::EFFECT) {
        // The node graph has no select-all of its own yet; deliberately NOT a
        // fallback to the scene, for the reason undoActiveSpace records — a
        // chord must never quietly act on a selection the user cannot see.
        return;
    }
    if (currentSpace == WindowSpaces::EDITOR && services && services->sceneEdit)
        services->sceneEdit->selectAll();
}

void MainWindow::pasteActiveSpace()
{
    if (currentSpace == WindowSpaces::EFFECT) {
        if (shaderGraph) shaderGraph->graphPaste();
        return;
    }
    if (currentSpace != WindowSpaces::EDITOR || !services || !services->clipboard) return;

    const auto result = services->clipboard->paste();
    // WHAT THE PASTE COULD NOT DO IS SAID OUT LOUD. A clipboard that holds
    // nothing of ours, or objects whose textures this library has never seen,
    // used to be a silent no-op — the worst possible answer for a chord.
    if (!result.error.isEmpty()) {
        showViewportToast(tr("Paste"), result.error);
        return;
    }
    if (!result.missing.isEmpty()) {
        showViewportToast(tr("Paste"),
                          tr("%1 object(s) pasted — %2 asset(s) missing from this library")
                              .arg(result.pasted.size()).arg(result.missing.size()));
        return;
    }
    if (result.pasted.isEmpty()) {
        const QString reason = result.skipped.isEmpty()
                                   ? tr("the clipboard holds nothing to paste here")
                                   : result.skipped.first().reason;
        showViewportToast(tr("Paste"), reason);
        return;
    }
    QString message = tr("%1 object(s) pasted").arg(result.pasted.size());
    if (!result.imported.isEmpty())
        message += tr(", %1 asset(s) imported").arg(result.imported.size());
    showViewportToast(tr("Paste"), message);
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
    // THE USER'S DOOR, AND IT ASKS FOR THE SCENE'S OWN PICTURE (owner,
    // 2026-09-13: "match the screenshot to the scene properly"). The grade is
    // named here rather than left to the viewport's default because the default
    // door is the THUMBNAIL grade and has other callers — project preview tiles
    // and the asset viewer — which must stay cheap. See
    // IEditorViewport::ScreenshotGrade for what each answer is a picture of.
    auto img = sceneView->takeScreenshot(1920, 1080,
                                         IEditorViewport::ScreenshotGrade::Scene);
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
namespace {
/// The widgets immersive fullscreen hides, in one place: the toggle and the
/// leave-by-somebody-else path must hide and restore exactly the same list.
/// The script console is a dock of the bottom area again (lane SPACE-2), so it
/// is back on the list — hiding the tray no longer hides it.
constexpr int kImmersiveDockCount = 7;
}   // namespace

void MainWindow::toggleImmersiveFullscreen()
{
    if (immersiveFullscreen) { leaveImmersiveFullscreen(true); return; }
    QWidget *editorDocks[kImmersiveDockCount] = { sceneHierarchyDock, sceneNodePropertiesDock,
                                                  presetsDock, assetDock, animationDock,
                                                  scriptConsoleDock, toolBar };
    immersiveFullscreen = true;
    enteringFullscreen = true;      // until the window manager says we are there
    // The layout the editor has WITH its chrome, before the next two lines
    // take it away: a quit from immersive fullscreen must not store an editor
    // with no panels (lane SPACE-1).
    captureEditorDockState();
    preFullscreenMaximized = isMaximized();
    preFullscreenWidgets.clear();
    if (currentSpace == WindowSpaces::EDITOR) {
        // WHICH TAB WAS IN FRONT, before the chrome goes away (round 2). F11
        // hides these docks itself rather than going through
        // applyDockVisibilityForSpace, so nothing else records it — and
        // re-showing them in list order hands the front tab to the last one
        // shown, which is the Console if it is open and the Timeline if it is
        // not. Same mechanism, same remedy as the space switch.
        for (const auto &tab : bottomAreaTabs())
            if (isFrontTab(tab.second)) { bottomFrontTab = tab.first; break; }
        for (QWidget *w : editorDocks) {
            preFullscreenWidgets.append(w && w->isVisible());
            if (w) w->hide();
        }
    }
    showFullScreen();
}

void MainWindow::leaveImmersiveFullscreen(bool restoreWindow)
{
    QWidget *editorDocks[kImmersiveDockCount] = { sceneHierarchyDock, sceneNodePropertiesDock,
                                                  presetsDock, assetDock, animationDock,
                                                  scriptConsoleDock, toolBar };
    // FIRST, so that the showNormal()/showMaximized() below — and any state
    // change somebody else made — cannot re-enter through changeEvent.
    immersiveFullscreen = false;
    enteringFullscreen = false;
    if (preFullscreenWidgets.size() == kImmersiveDockCount) {
        // THE FRONT TAB GOES LAST, because showing a tabified dock raises it
        // (round 2) — the same two-pass order applyDockVisibilityForSpace
        // uses, so leaving fullscreen comes back to the tab F11 interrupted
        // instead of to whichever dock happens to sit last in this list.
        QDockWidget *front = nullptr;
        for (const auto &tab : bottomAreaTabs())
            if (tab.first == bottomFrontTab) { front = tab.second; break; }
        for (int i = 0; i < preFullscreenWidgets.size(); ++i)
            if (editorDocks[i] && editorDocks[i] != front)
                editorDocks[i]->setVisible(preFullscreenWidgets[i]);
        for (int i = 0; i < preFullscreenWidgets.size(); ++i)
            if (editorDocks[i] && editorDocks[i] == front)
                editorDocks[i]->setVisible(preFullscreenWidgets[i]);
        raiseBottomFrontTab();
    }
    preFullscreenWidgets.clear();
    if (restoreWindow) preFullscreenMaximized ? showMaximized() : showNormal();
}

// THE WINDOW STATE THIS CLASS DOES NOT OWN (RR2's finding, lane ENGINE-7 item
// 5). Immersive fullscreen is a window state PLUS a set of hidden docks, and
// anything can take the window out of that state without telling us:
// `app.resizeWindow()` calls showNormal() before resizing, a window manager
// offers its own control, and a desktop environment may un-fullscreen a window
// on a workspace change. The flag then claimed fullscreen while the window was
// windowed with its docks still hidden — and `setImmersiveFullscreen(true)`,
// which is idempotent against the flag, did NOTHING, so F11 was dead until it
// was pressed twice.
void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange) return;
    if (!immersiveFullscreen) return;
    // ARRIVED: from here a state change that is not fullscreen is a departure.
    if (isFullScreen()) { enteringFullscreen = false; return; }
    // STILL ON THE WAY IN (round-2 review, item 5). showFullScreen() is a
    // request, and a window manager may answer a maximized window with an
    // intermediate state that carries neither flag; restoring the docks there
    // would put the whole editor chrome back INSIDE a window that is about to
    // go fullscreen.
    if (enteringFullscreen) return;
    // A MINIMISED fullscreen window is still fullscreen (Qt ORs the minimise
    // bit in), so isFullScreen() stays true and this does not fire for it.
    leaveImmersiveFullscreen(false);
}

void MainWindow::toggleDebugDrawer(bool state)
{
	sceneView->setShowDebugDrawFlags(state);
}

// EVERY PAGE THAT IS NOT THE EDITOR TAKES THE EDITOR'S CHROME DOWN, and it does
// it through the ONE function that knows what "the editor's panels" are (lane
// SPACE-2).
//
// This used to be a toggle taking a bool, which hid FIVE docks by name — and all
// five call sites passed `false`, so the other half had been dead for years
// (CRUD). Naming the docks is what made it wrong the moment the bottom area
// grew a third one: the script console stayed on screen over the Desktop, the
// Player and the Materials page, and — because closeEvent stores the layout of
// a window that still has a visible dock in it — a session that quit from the
// Player saved "the console, alone" as the editor's whole layout. The next
// launch restored exactly that: an editor with one panel, the console.
// applyDockVisibilityForSpace reads the page, so it hides all of them here and
// shows the right ones when the editor comes back.
void MainWindow::hideEditorPanels()
{
    applyDockVisibilityForSpace();
    playerControls->setVisible(true);
}

// THE DOCKS FOLLOW THE PAGE ON SCREEN, FROM ONE PLACE (lane SPACE-1,
// 2026-09-14).
//
// The editor shows the panels `widgetStates` says are open; every other page
// shows none, because they are the editor page's and that page is not up. Both
// the space switch and the queued layout pass (applyColumnWidthsOnce, which
// re-applies the saved blob once the window has its real size) end by calling
// this, so a restored layout can no longer undo the visibility the page just
// asked for — which is exactly what made the editor open empty after a
// restart, and stay empty until the user visited another page and came back
// (owner report, 2026-09-14).
//
// THE PAGE, NOT `currentSpace` (round-2 review). They are the same thing for
// every path a user takes, and different for the one a SCRIPT takes:
// beginEngineSelftest shows page 1 directly and calls applyColumnWidthsOnce
// with currentSpace still DESKTOP (the scripted/MCP boot never calls
// switchSpace), so keying on the space hid all five docks one loop turn into
// every scripted session that had a stored layout. `ui->stackedWidget`'s
// current index is what "the editor is what the user is looking at" actually
// means — it is the same reading app.docks() reports as `visible`.
//
// IMMERSIVE FULLSCREEN is the other way the editor page legitimately has no
// chrome (F11, EDITOR_SHORTCUTS_SPEC §3): without this term a space round trip
// inside fullscreen would put the docks back on top of it, and
// leaveImmersiveFullscreen would then restore a state nobody was in.
void MainWindow::applyDockVisibilityForSpace()
{
    if (!sceneHierarchyDock || !ui || !ui->stackedWidget) return;
    const bool editor = ui->stackedWidget->currentIndex() == 1 && !immersiveFullscreen;
    // WHICH TAB IS IN FRONT SURVIVES THE ROUND TRIP (lane SPACE-2). Showing a
    // tabified dock RAISES it, so the loop below would hand the front tab to
    // whichever dock it shows last — a trip to the Player and back came home on
    // the Timeline no matter what the user was doing. Remember the front tab
    // while it is still readable, restore it once they are all back.
    for (const auto &tab : bottomAreaTabs()) {
        if (!isFrontTab(tab.second)) continue;
        bottomFrontTab = tab.first;
        break;
    }
    sceneHierarchyDock->setVisible(editor && widgetStates[(int) Widget::HIERARCHY]);
    sceneNodePropertiesDock->setVisible(editor && widgetStates[(int) Widget::PROPERTIES]);
    presetsDock->setVisible(editor && widgetStates[(int) Widget::PRESETS]);
    // THE BOTTOM AREA'S THREE, AND THE FRONT TAB GOES LAST (lane SPACE-2).
    // Showing a tabified dock RAISES it, so the order these are shown in IS
    // which tab comes up in front — and a raise() afterwards does not stick:
    // the dock area's layout pass runs later and leaves the last dock it
    // inserted in front (measured on the rig — every trip home landed on the
    // Timeline, and so did the boot). Ordering the calls needs no timer and
    // cannot flip a tab in front of the user.
    auto wanted = [&](const QDockWidget *dock) {
        if (dock == assetDock)     return editor && widgetStates[(int) Widget::ASSETS];
        if (dock == animationDock) return editor && widgetStates[(int) Widget::TIMELINE];
        return editor && widgetStates[(int) Widget::CONSOLE];
    };
    const QVector<QPair<QString, QDockWidget *>> bottom = bottomAreaTabs();
    for (const auto &tab : bottom)
        if (tab.first != bottomFrontTab) tab.second->setVisible(wanted(tab.second));
    for (const auto &tab : bottom)
        if (tab.first == bottomFrontTab) tab.second->setVisible(wanted(tab.second));
    // A dock that was ALREADY visible is not re-shown by the line above (Qt
    // returns early), so an ordinary raise covers the case where nothing about
    // the bottom area's visibility changed and only the front tab is wrong.
    if (editor) raiseBottomFrontTab();
}

// OPEN OR CLOSE AN EDITOR PANEL, IN ONE PLACE (lane SPACE-2, API-first).
//
// Closing is `close()` and not `setVisible(false)` ON PURPOSE: the title-bar X
// is close(), the Close event is what writes `widgetStates` (see eventFilter),
// and a panel that two gestures close by two different routes is how the Toggle
// Widgets dialog and the X came to disagree in the first place. Opening shows
// the dock AND raises it — in the bottom area's tab group, a panel the user
// asked for that comes back behind another tab has not come back.
//
// `name` is the panel's script name; the empty QString answer means "no such
// panel", which is what the verb reports back.
QDockWidget *MainWindow::panelDock(const QString &name) const
{
    const QString wanted = name.trimmed().toLower();
    if (wanted == QLatin1String("hierarchy"))  return sceneHierarchyDock;
    if (wanted == QLatin1String("properties")) return sceneNodePropertiesDock;
    if (wanted == QLatin1String("presets"))    return presetsDock;
    if (wanted == QLatin1String("assets"))     return assetDock;
    if (wanted == QLatin1String("timeline"))   return animationDock;
    if (wanted == QLatin1String("console"))    return scriptConsoleDock;
    return nullptr;
}

QVariantList MainWindow::propertyRows(const QString &tabName) const
{
    QVariantList out;
    if (!sceneNodePropertiesWidget) return out;
    SceneNodePropertiesWidget::Tab tab = sceneNodePropertiesWidget->propertiesTab();
    if (!tabName.trimmed().isEmpty()
        && !SceneNodePropertiesWidget::tabFromName(tabName, tab)) return out;
    const QString name = SceneNodePropertiesWidget::tabName(tab);
    for (const auto &row : sceneNodePropertiesWidget->propertyRows(tab)) {
        QVariantMap entry;
        entry[QStringLiteral("tab")] = name;
        entry[QStringLiteral("section")] = row.sections;
        entry[QStringLiteral("label")] = row.label;
        entry[QStringLiteral("key")] = row.key;
        entry[QStringLiteral("keywords")] = row.keywords;
        entry[QStringLiteral("panelVisible")] = row.panelVisible;
        entry[QStringLiteral("filteredOut")] = row.filteredOut;
        entry[QStringLiteral("visible")] = row.visible;
        out.append(entry);
    }
    return out;
}

bool MainWindow::isPropertiesTab(const QString &tabName) const
{
    if (tabName.trimmed().isEmpty()) return true;
    SceneNodePropertiesWidget::Tab tab;
    return SceneNodePropertiesWidget::tabFromName(tabName, tab);
}

QString MainWindow::propertiesFilter(const QString &tabName) const
{
    if (!sceneNodePropertiesWidget) return QString();
    SceneNodePropertiesWidget::Tab tab = sceneNodePropertiesWidget->propertiesTab();
    if (!tabName.isEmpty() && !SceneNodePropertiesWidget::tabFromName(tabName, tab)) return QString();
    return sceneNodePropertiesWidget->propertiesFilter(tab);
}

bool MainWindow::setPropertiesFilter(const QString &tabName, const QString &text)
{
    if (!sceneNodePropertiesWidget) return false;
    SceneNodePropertiesWidget::Tab tab = sceneNodePropertiesWidget->propertiesTab();
    if (!tabName.isEmpty() && !SceneNodePropertiesWidget::tabFromName(tabName, tab)) return false;
    sceneNodePropertiesWidget->setPropertiesFilter(tab, text);
    return true;
}

QPair<int, int> MainWindow::propertiesFilterCounts(const QString &tabName) const
{
    if (!sceneNodePropertiesWidget) return { 0, 0 };
    SceneNodePropertiesWidget::Tab tab = sceneNodePropertiesWidget->propertiesTab();
    if (!tabName.isEmpty() && !SceneNodePropertiesWidget::tabFromName(tabName, tab)) return { 0, 0 };
    const auto c = sceneNodePropertiesWidget->filterCounts(tab);
    return { c.visible, c.hidden };
}

QVariantMap MainWindow::propertiesStats() const
{
    QVariantMap out;
    if (!sceneNodePropertiesWidget) return out;
    const auto s = sceneNodePropertiesWidget->propertiesStats();
    out[QStringLiteral("mounts")] = s.mounts;
    out[QStringLiteral("refills")] = s.refills;
    out[QStringLiteral("rebuilds")] = s.rebuilds;
    out[QStringLiteral("rows")] = s.rows;
    out[QStringLiteral("pending")] = s.pending;
    out[QStringLiteral("deferredHidden")] = s.deferredHidden;
    out[QStringLiteral("visible")] = s.visible;
    return out;
}

QString MainWindow::propertiesTab() const
{
    return sceneNodePropertiesWidget
        ? SceneNodePropertiesWidget::tabName(sceneNodePropertiesWidget->propertiesTab())
        : QString();
}

bool MainWindow::setPropertiesTab(const QString &name)
{
    if (!sceneNodePropertiesWidget) return false;
    SceneNodePropertiesWidget::Tab tab;
    if (!SceneNodePropertiesWidget::tabFromName(name, tab)) return false;
    sceneNodePropertiesWidget->setPropertiesTab(tab);
    return true;
}

bool MainWindow::setPanelOpen(const QString &name, bool open)
{
    QDockWidget *dock = panelDock(name);
    if (!dock) return false;
    if (!open) {
        dock->close();                       // the X's own gesture: see eventFilter
        return true;
    }
    // The record first: showing a dock whose page is not up would be undone by
    // the next applyDockVisibilityForSpace, and the user's answer is the bit.
    if      (dock == sceneHierarchyDock)      widgetStates[(int) Widget::HIERARCHY]  = true;
    else if (dock == sceneNodePropertiesDock) widgetStates[(int) Widget::PROPERTIES] = true;
    else if (dock == presetsDock)             widgetStates[(int) Widget::PRESETS]    = true;
    else if (dock == assetDock)               widgetStates[(int) Widget::ASSETS]     = true;
    else if (dock == animationDock)           widgetStates[(int) Widget::TIMELINE]   = true;
    else if (dock == scriptConsoleDock) {
        // ONE OPENER FOR THE CONSOLE (round 2): setConsoleTabVisible is where
        // the tab it interrupts is recorded, so a console opened through this
        // verb and one opened with Ctrl+` return to the same tab when they
        // close. `false`: opening a panel is not a request for the keyboard.
        setConsoleTabVisible(true, false);
        return true;
    }
    dock->show();
    dock->raise();
    for (const auto &tab : bottomAreaTabs())
        if (tab.second == dock) bottomFrontTab = tab.first;
    return true;
}

bool MainWindow::isPanelOpen(const QString &name) const
{
    const QDockWidget *dock = panelDock(name);
    return dock && !dock->isHidden();
}

void MainWindow::raiseBottomFrontTab()
{
    for (const auto &tab : bottomAreaTabs())
        if (tab.first == bottomFrontTab && !tab.second->isHidden()) { tab.second->raise(); return; }
}

// THE LAYOUT THE EDITOR LAST HAD (lane SPACE-1). Called on the way out of the
// editor space and before immersive fullscreen hides the chrome; a no-op once
// the docks are down, so the caller never has to think about ordering. What it
// holds is what closeEvent writes — never the Player's empty one.
void MainWindow::captureEditorDockState()
{
    if (!viewPort || !DockState::hasVisibleDock(viewPort)) return;
    editorDockState = DockState::snapshot(viewPort);
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

// A BRAND-NEW SCENE STARTS AT THE DEFAULTS (owner report 2026-09-07). Opening a
// scene pushes its saved EditorData into the viewport and the View menu
// (openStage's `editorData` branch); creating one pushed NOTHING, so a new
// scene silently inherited the last opened scene's helper state — the grid in
// particular, which is why "new scene has the grid on" and "loaded scene does
// not" could both be true in one session. A default-constructed EditorData IS
// the statement of what a new scene looks like; applying it here is the same
// operation the open path performs, with the same three settings.
void MainWindow::newScene()
{
    auto scene = this->createDefaultScene();
    this->setScene(scene);
    this->sceneView->resetEditorCam();

    const EditorData defaults;
    sceneView->setShowGrid(defaults.showGrid);
    sceneView->setShowLightWires(defaults.showLightWires);
    sceneView->setShowDebugDrawFlags(defaults.showDebugDrawFlags);
    if (gridCheckAction)    gridCheckAction->setChecked(defaults.showGrid);
    if (wireCheckAction)    wireCheckAction->setChecked(defaults.showLightWires);
    if (physicsCheckAction) physicsCheckAction->setChecked(defaults.showDebugDrawFlags);
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
    // The scripted/MCP boot shows this page without switchSpace(), so it needs
    // its own call — a screenshot taken over MCP must show the layout a user
    // gets, not a narrower one.
    applyColumnWidthsOnce();
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
    // closeDatabase(), but a stale `sceneView` is the kind of thing a later
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

    // The edit gate's notice captured this window (ledger §423). The gate
    // outlives every window — it is process-wide — so the hook goes first,
    // before anything here can raise it.
    editgate::setNoticeHook({});

    // ORDER IS LOAD-BEARING. Undo commands owe the database work when they die
    // (DeleteSceneNodeCommand finalises the asset row once no undo can reach
    // the delete any more), and undoStack is parented to this window — so it
    // used to be destroyed AFTER this body, i.e. after closeDatabase(), and
    // every pending asset delete failed against a closed connection. Silently:
    // the SQLite driver's only complaint was "Parameter count mismatch" at
    // [info] level. Drain the stack here, while the connection is still open.
    //
    // Since CLOSE-1 the destructors only QUEUE that work (the quit path is the
    // same freeze as the project close: hundreds of commands, hundreds of
    // syncs), so the drain is followed by the one flush that applies it.
    // closeDatabase() flushes too — this call is what makes the order above
    // say what it means.
    if (undoStack) undoStack->clear();
    if (db) db->flushPendingAssetDeletes();

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
    if (actionLocalSpace) actionLocalSpace->setChecked(true);
}

void MainWindow::useGlobalTransform()
{
    sceneView->setGizmoTransformToGlobal();
    if (actionGlobalSpace) actionGlobalSpace->setChecked(true);
}

QString MainWindow::gizmoTransformSpace() const
{
    return sceneView ? sceneView->gizmoTransformSpace() : QStringLiteral("global");
}

bool MainWindow::applyGizmoTransformSpace(const QString &space)
{
    if (space == QLatin1String("local"))       useLocalTransform();
    else if (space == QLatin1String("global")) useGlobalTransform();
    else return false;
    return true;
}

void MainWindow::setPhysicsDebugOverlay(bool on)
{
    // The action's toggled() signal calls toggleDebugDrawer, which is the one
    // path to the viewport — so setting the checkmark IS setting the overlay.
    // (A no-op setChecked emits nothing, hence the explicit fallback.)
    if (physicsCheckAction && physicsCheckAction->isChecked() != on)
        physicsCheckAction->setChecked(on);
    else
        toggleDebugDrawer(on);
}

void MainWindow::setImmersiveFullscreen(bool on)
{
    if (immersiveFullscreen == on) return;
    toggleImmersiveFullscreen();
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

    // The projection button is a VIEW of the state, so it is updated and never
    // asked to re-apply anything (it used to call changeProjection, which is
    // now the command and would recurse).
    const bool perspective = (name == QLatin1String("perspective"));
    syncProjectionButton(perspective);
    // ...and an axis view is remembered, so the toggle's "orthographic" means
    // "back to the one I was in".
    if (!perspective) lastOrthographicView = name;

    for (QAction *action : viewsActions)
        action->setChecked(action->data().toString() == name);
    setViewsButtonLabel(name);
    return true;
}

void MainWindow::setViewsButtonLabel(const QString &view)
{
    if (!viewsButton) return;
    // The label is the checked action's own text, so the button and the menu
    // can never spell the same view differently.
    for (QAction *action : viewsActions) {
        if (action->data().toString() != view) continue;
        viewsButton->setText(action->text() + QStringLiteral(" "));
        return;
    }
    viewsButton->setText(QStringLiteral("Perspective "));
}

// THE PROJECTION TOGGLE IS A VIEW CHANGE (hygiene lane, 2026-09-09).
//
// Three defects in one small function, all of them the same mistake — it did
// the work itself instead of asking the viewport:
//
//  1. It wrote `sceneView->getScene()->camera`, the SCENE's camera node. The
//     explorer this viewport flies is a different node (EngineSceneViewport::
//     editorCamera), so the button changed a camera nothing was looking
//     through and the picture did not change at all until something else
//     happened to re-push.
//  2. It never went through setCameraView, so the AXIS-VIEW ROTATION LOCK was
//     never armed or disarmed (the lock reads the projection precisely because
//     this button used to bypass it — enginesceneviewport.cpp says so at
//     cameraRotationLocked) and the per-view camera memory was not consulted.
//  3. It left the Views label reading "Perspective" over an orthographic
//     picture, because only applyCameraView relabels it.
//
// So it now asks for a canonical view, and "orthographic" means the last AXIS
// view this window was in (Top on a fresh window). That is the same state the
// Views menu produces, which is the point: two controls that mean the same
// thing must not be able to leave the editor in two different states.
void MainWindow::changeProjection(bool val)
{
	applyCameraView(val ? QStringLiteral("perspective") : lastOrthographicView);
}

// The first-run size clamp (see the restoreGeometry call site). Before the
// first show() there is no QWindow yet, so screen() answers with the primary
// screen — which is the one a first window lands on anyway. availableGeometry()
// is the right rectangle: it excludes panels and docks, so "fits" means fits
// where the user can actually reach it.
void MainWindow::fitToScreen()
{
    QScreen *s = screen();
    if (!s) return;                       // no GUI screen at all (offscreen boots can have one)
    const QRect avail = s->availableGeometry();
    if (avail.isEmpty()) return;

    const QSize want = size();
    const QSize fit(qMin(want.width(), avail.width()), qMin(want.height(), avail.height()));
    if (fit != want) resize(fit);
    // ...and it has to START inside the screen too: with no window manager
    // nobody moves it for us, and a clamped size at an off-screen position is
    // still an unreachable window.
    if (!avail.contains(QRect(pos(), fit))) move(avail.topLeft());
}

void MainWindow::syncProjectionButton(bool perspective)
{
	if (!cameraView) return;
	if (perspective) {
		cameraView->setIcon(QIcon(":/icons/perspective-view-80.png"));
		cameraView->setToolTip(tr("Perspective view | Toggle to switch to orthogonal view"));
	} else {
		cameraView->setIcon(QIcon(":/icons/orthogonal-view-80.png"));
		cameraView->setToolTip(tr("Orthogonal view | Toggle to switch to perspective view"));
	}
}
