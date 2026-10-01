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
#include "services/assetshare.h"
#include "ui/dialogs/bundleexportdialog.h"
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
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/core/viewport.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/animation/keyframeset.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/core/logger.h"
#include "services/jahlog.h"
#include "services/sessionmarkers.h"
#include "services/editgate.h"
#include "services/framemonitor.h"
#include "services/perfsampler.h"

#include "data/guidmanager.h"
#include "services/thumbnailmanager.h"
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
#include <QSpinBox>
#include <QSlider>
#include <QMenu>
#include <QWidgetAction>
#include <QHBoxLayout>

#include "ui/controls/tilecache.h"
#include "ui/pages/iassetviewer.h"
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

#include "modules/moduleregistry.h"
#include "services/playerservice.h"
#include "modules/studiomodule.h"
#include "shell/actionhost.h"
#include "shell/modulehub.h"
#include "shell/pagehost.h"
#include "shell/shelllifecycle.h"
#include "shell/shellview.h"
#include "shell/viewcontroller.h"
#include "shell/sceneissuewatch.h"
#include "shell/editordocks.h"
#include "player/playerwidget.h"
#include "player/engineplayerview.h"
#include "viewport/headlesseditorviewport.h"

#include "scripting/scripthost.h"
#include "scripting/scriptengine.h"
#include "scripting/claude/claudeassistant.h"
#include "ui/panels/scriptconsole.h"
#include "scripting/modules/studiomodules.h"

#include "services/services.h"
#include "services/shortcutregistry.h"
#include "services/worldmodes.h"
#include "services/testtier.h"
#include "viewport/snapsettings.h"
#include "viewport/cameraspeed.h"
#include "services/subscriber.h"
#include "services/undoservice.h"
#include "services/selectioncost.h"
#include "services/selectionservice.h"
#include "services/playbackservice.h"
#include "services/projectservice.h"
#include "services/framepacing.h"
#include "services/outlinesettings.h"
#include "services/loadtimeline.h"
#include "services/meshbakestore.h"
#include "services/assetstorepaths.h"
#include <QSqlDatabase>
#include "services/primitiveassets.h"
#include "services/sceneopenrunner.h"
#include "services/mainthreadwatchdog.h"
#include "services/apppaths.h"
#include "shell/dockstate.h"
#include "shell/shutdownorder.h"

#include "services/projectarchiver.h"
#include "services/sceneextents.h"
#include "ui/dialogs/progressdialog.h"
#include "app/firstrun.h"
#include "services/materialpresetseeder.h"
#include "services/materialpreviewservice.h"
#include "services/sceneeditservice.h"
#include "services/clipboardservice.h"
#include "services/thumbnailservice.h"
#include "services/assetservice.h"
#include "services/defaultfloormaterial.h"
#include "services/scenetemplate.h"
#include "irisgl/document/physics/physicsproperties.h"
#include <QJsonDocument>
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
	CameraSpeed::bindSettings(settings->settings);   // and THE camera speed (owner R15)


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

	// THE SHELL'S PARTS (D10-SHELL-MODULES). The lifecycle opens the library
	// now and owns the whole shutdown order; the pages are keyed by id; every
	// keyboard action, menu row and toolbar slot goes through the action host;
	// the modules are driven through their hooks by the hub.
	lifecycle = new ShellLifecycle(this);
	db = lifecycle->openLibrary();
	pageHost = new PageHost(ui->stackedWidget, this, this);
	shortcutRegistry = new ShortcutRegistry(settings->settings, this);
	actionHost = new ActionHost(shortcutRegistry, this, [this]() { return currentSpaceId(); }, this);
	viewController = new ViewController(this);
	docks = new EditorDocks(this);
	issueWatch = new SceneIssueWatch(this, [this]() { return currentSpace == WindowSpaces::EDITOR; }, this);
	moduleHub = new ModuleHub(this);
	moduleHub->setSpaceEditTarget(spaces::id(WindowSpaces::EDITOR), [this]() { return editorEditTarget(); });
	shellView = new ShellView(this);

    prefsDialog = new PreferencesDialog(nullptr, db, settings);
    aboutDialog = new AboutDialog();

    camControl = Q_NULLPTR;

    setupFileMenu();
	// THE PROCESS'S ICON SET (QTAWESOME-1): one QtAwesome, one font load, one
	// 786-entry codepoint map, shared with every other widget that draws a
	// glyph icon — the shell used to make its own and never delete it, and
	// the materials page made a second (and a third per property row).
	fontIcons = &fonticons::shared();

    setupViewPort();
	setupUndoRedo();
	// Services before pages: pages and modules are constructed against the
	// service layer (audit §6.2 — ModuleHost carries it).
	setupServices();
    setupDesktop();
    setupToolBar();
    // THE EDITOR'S PANELS (shell/editordocks.h): the six docks, the default
    // layout and the user's restored one.
    EditorDocks::Deps dockDeps;
    dockDeps.shell = this;
    dockDeps.window = this;
    dockDeps.viewPort = viewPort;
    dockDeps.viewport = sceneView;
    dockDeps.db = db;
    dockDeps.services = services;
    dockDeps.project = project;
    dockDeps.settings = settings;
    dockDeps.gridAction = gridCheckAction;
    dockDeps.groundPlaneAction = groundPlaneCheckAction;
    dockDeps.editorOnScreen = [this]() {
        return pageHost->isCurrent(spaces::id(WindowSpaces::EDITOR))
               && !viewController->isImmersiveFullscreen();
    };
    dockDeps.editorActive = [this]() { return currentSpace == WindowSpaces::EDITOR; };
    docks->build(dockDeps);
    docks->setToolbar(toolBar);
    setupShortcuts();

	// scripting (SCRIPTING_SPEC §2): the host sees the live app; the console
	// dock starts hidden — Ctrl+` toggles it in the editor space.
	scriptHost = new ScriptHost;
	scriptHost->shell = shellView;
	scriptHost->db = db;
	scriptHost->project = project;
	scriptHost->viewport = sceneView;
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
		// EITHER PAGE'S VIEW MAKES THIS SESSION ABLE TO RENDER (SMOKE-FIX-1's
		// fix round). This asked the EDITOR viewport alone, which is right
		// whenever the editor page has been shown — and that is every windowed
		// script run, because the harness shows it. It is wrong for the session
		// a person on a `--vr` boot actually has: Desktop page, straight into
		// the PLAYER, whose own View is the one drawing. `player.frame` and
		// `player.screenshot` refused there with "no rendering engine is
		// available", which is not true of a process that is rendering.
		return sceneView->isInitialized()
		       || (playerBackend && playerBackend->view() != nullptr);
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
	scriptHost->scriptRunState = [this](ScriptRunState state) {
		// A preview the run's own verb started ends with the run (F3 of
		// MATERIAL-PREVIEW-1's read) — before the driver lookup, which a
		// document-only session fails.
		if (state == ScriptRunState::None && materialPreviewService
		    && materialPreviewService->verbOwned())
			materialPreviewService->end();
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
		SettingsManager::getDefaultManager()->get(settingkeys::scriptFeedbackLive)
			? ScriptRunPolicy::Live : ScriptRunPolicy::Off);
	if (prefsDialog) prefsDialog->wireScripting(scriptEngine);
	registerStudioModules(*scriptEngine);
	moduleHub->registerApi(*scriptEngine);

	// THE CONSOLE IS THE BOTTOM AREA'S THIRD TAB (owner, 2026-09-14, lane
	// SPACE-2). Its DOCK is built in setupDockWidgets — it has to exist before
	// the saved layout is restored there, or a blob that names it leaves Qt
	// guessing at the whole bottom area — and the console widget, which needs
	// the script engine, arrives here. The dock is closed until Ctrl+` (or
	// editor.tray) asks for it, and it is tabified with Assets and the
	// Timeline, so asking for it adds a tab rather than splitting the area
	// (the thing the owner rejected at smoke S1).
	scriptConsole = new ScriptConsole(scriptEngine);
	docks->setConsole(scriptConsole);

	// THE MCP ENDPOINT AND THE CLAUDE CHAT (scripting/claude/claudeassistant.h):
	// one owner, OFF by default; the Preferences page starts it through the
	// assistant so the console gets the connect line. Its toolbar action and
	// chord are its contribution.
	ClaudeAssistant::Deps assistantDeps;
	assistantDeps.engine = scriptEngine;
	assistantDeps.settings = settings;
	assistantDeps.projectService = projectService;
	assistantDeps.project = project;
	assistantDeps.console = scriptConsole;
	assistantDeps.window = this;
	assistant = new ClaudeAssistant(assistantDeps, this);
	prefsDialog->wireMcp(assistant->mcp(), [this](quint16 port, QString *error) {
		return assistant->startMcpServer(port, error);
	});
	assistant->startFromSettings();
	{
		Contributions c;
		assistant->contribute(c, fontIcons);
		actionHost->apply(c);
	}

	// EVERY ROW IS IN: the shell's, the modules' (contributed at boot) and the
	// assistant's, each with its `after` anchor — registered in one pass in the
	// Preferences order, and only then handed to the Preferences page, which
	// builds its table from the registry.
	actionHost->commit();
	refreshGameplayShortcutRows();
	prefsDialog->wireShortcuts(shortcutRegistry);

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
	connect(qApp, &QCoreApplication::aboutToQuit, lifecycle, &ShellLifecycle::stopBackgroundWork);

	// WHAT THE SHUTDOWN ORDER NEEDS FROM THIS WINDOW (shell/shelllifecycle.h).
	ShellLifecycle::Parts parts;
	parts.window = this;
	parts.settings = settings;
	parts.modules = moduleHub;
	parts.scriptEngine = scriptEngine;
	parts.scriptHost = scriptHost;
	parts.projectService = projectService;
	parts.undoService = undoService;
	parts.undoStack = undoStack;
	parts.openInFlight = [this]() { return isOpeningProject(); };
	parts.settleOpen = [this](int budgetMs) {
		openRunner->waitForDone(budgetMs);
		if (openRunner->isRunning()) openRunner->requestAbort();
	};
	parts.stopOpen = [this](int budgetMs) {
		if (!openRunner) return true;
		openRunner->requestAbort();
		return openRunner->waitForDone(budgetMs);
	};
	parts.saveScene = [this]() { saveScene(); };
	parts.storeEditorLayout = [this]() { docks->storeLayout(); };
	parts.stopImports = [this](int budgetMs) {
		bool stopped = true;
		if (docks->assetTray()) stopped &= docks->assetTray()->shutdownImports(budgetMs);
		if (_assetView) stopped &= _assetView->shutdownImports(budgetMs);
		return stopped;
	};
	parts.stopAssistant = [this]() { if (assistant) assistant->shutdown(); };
	parts.deleteServices = [this]() {
		// The QObject services (selection/playback/sceneEdit) are parented to
		// the window; the plain ones are deleted here.
		delete services;
		services = nullptr;
		// Before sceneEditService (which it points at) and before the scene
		// dies: its destructor puts any borrowed material back.
		delete materialPreviewService;
		materialPreviewService = nullptr;
		delete projectService;
		projectService = nullptr;
		delete thumbnailService;
		thumbnailService = nullptr;
		delete assetService;
		assetService = nullptr;
		delete undoService;
		undoService = nullptr;
		delete ui;
		ui = nullptr;
	};
	parts.forgetViews = [this]() {
		sceneView = nullptr;
		playerView = nullptr;
		viewPort = nullptr;
		_assetView = nullptr;
		assetsPlaceholder = nullptr;
		assetsPreviewViewer = nullptr;   // a child of this window: gone with the sweep
	};
	lifecycle->setParts(parts);

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

void MainWindow::goToDesktop()
{
    show();
    switchSpace(WindowSpaces::DESKTOP, true);
}

void MainWindow::setShowFrameStats(bool on)
{
    // ONE code path for the F3 key, the View Options row, the Preferences
    // checkbox and editor.setOverlays({stats}) — and one stored value, so the
    // readout is still there after a restart (STATS_OVERLAY_SPEC §5.3). The
    // View Options checkmark follows the viewport's overlaysChanged
    // (syncOverlayChecks), never this call.
    if (sceneView) sceneView->setShowFps(on);
    SettingsManager::getDefaultManager()->set(settingkeys::showFps, on);
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
    const QString why = tr("the 3D view could not be created: %1").arg(sceneView->viewCreationError());
    goToDesktop();
    // AFTER the bounce: the switch it makes clears the reason on the way in
    // (every attempt starts with a clean slate), so recording it first would
    // record it into the space we are leaving for.
    spaceRefusal = why;
    return true;
}

// THE PLAYER'S HALF OF THE SAME RULE (SMOKE-FIX-1). Entering a page that cannot
// draw is not a space switch: say why, and put the window back on the space the
// user was looking at — never leave the Player selected over the previous
// page's pixels.
void MainWindow::bounceFromPlayer(const QString &why)
{
    qWarning("Jahshaka: the Player page refused to start - %s", qPrintable(why));
    if (!viewErrorToast) viewErrorToast = new Toast(this);
    viewErrorToast->setAnchor(Toast::Anchor::WindowCentre);
    viewErrorToast->showToast(tr("Player unavailable"), why);
    // WHERE BACK IS. With a world open that is the EDITOR, whatever page the
    // user came from: this switch already put the app in PlayMode and hid the
    // editor's furniture, and only switchSpace(EDITOR) undoes both (enterEditMode
    // + SceneMode::EditMode) — landing on the desktop instead would leave an
    // open project in play mode with nobody playing it. With no world open there
    // is nothing to edit, so it is the desktop.
    //
    // Going back runs the PLAYER shutdown leg on the way, which is a no-op after
    // a refusal because nothing started.
    const WindowSpaces back = projectService && projectService->isSceneOpen()
                                  ? WindowSpaces::EDITOR
                                  : WindowSpaces::DESKTOP;
    // THE PLAY MODE THIS SWITCH ENTERED COMES OFF HERE, whichever page we land
    // on. switchSpace(EDITOR) would do it on its own; switchSpace(DESKTOP) does
    // NOT — and leaving it on means the app is in play mode on the desktop, the
    // top bar dressed for a run, and the session log's `=== PLAY START ===`
    // (PlaybackService::playModeEntered, which enterPlayMode above already
    // emitted) never gets its `=== PLAY STOP ===`. Doing it before the switch
    // keeps the bracket closed on both roads.
    playbackService->setSceneMode(SceneMode::EditMode);
    enterEditMode();
    switchSpace(back, true);
    spaceRefusal = why;     // after the bounce: see bounceIfViewportIsDead
}

QVariantList MainWindow::toolbarActions() const
{
    QVariantList out;
    if (!toolBar) return out;
    for (const QAction *a : toolBar->actions()) {
        if (a->isSeparator()) continue;
        QString id = a->objectName();
        if (id.startsWith(QLatin1String("action"))) id = id.mid(6);
        if (id.isEmpty()) continue;
        id = id.left(1).toLower() + id.mid(1);
        out.append(QVariantMap{ { QStringLiteral("id"), id },
                                { QStringLiteral("visible"), a->isVisible() },
                                { QStringLiteral("enabled"), a->isEnabled() },
                                { QStringLiteral("tooltip"), a->toolTip() } });
    }
    return out;
}

iris::ScenePtr MainWindow::getScene()
{
    return scene;
}

iris::ScenePtr MainWindow::createDefaultScene(SceneTemplate kind)
{
    auto scene = iris::Scene::create();
    // New scenes start on EPIC (POST_CHAIN_SPEC.md §12 decision 8, owner call).
    // Applied through the registry rather than by hardcoding the values here, so
    // the tier table stays the single place any of them is written. Every
    // template, Empty included.
    worldmodes::setMode(scene, worldmodes::Mode::Epic);

    // EMPTY IS NOTHING (WORLD-MODEL-1, services/scenetemplate.h): the root, the
    // tier, no lights, no floor and NO SKY — SkyType::NONE (SKY-ATMOSPHERE-1):
    // a black background, nothing captured, no light, no reflection, no ambient
    // from a sky. It stops here, before anything is added: a user who asks for
    // empty gets a document a script would have built.
    if (kind == SceneTemplate::Empty) {
        scene->skyType = iris::SkyType::NONE;
        sceneNodeSelected(scene->rootNode);
        return scene;
    }

    // THE FLOOR (WORLD-MODEL-1): an ORDINARY cube, exactly what the Add menu
    // makes (the shipped, baked cube primitive — so it has its LOD chain, its
    // cards and its SDF, and Atom draws it), scaled to 100 x 1 x 100 m with
    // its TOP face at y = 0, wearing the default floor material. It is
    // re-materialable, movable and deletable like any node, and it SHIPS LOCKED
    // (owner, 2026-09-15, restated 2026-09-30: not pickable, so a click on the
    // empty floor selects nothing and a drop on it is refused by name until the
    // user unlocks it in the outliner); `defaultFloor`
    // only says which material `material.reset` brings back and what the
    // Player's "hide the floor" setting hides. It casts no shadow (nothing is
    // under it, and a 100 m caster would widen the sun's fit to the whole
    // floor) and is a static box to physics.
    // THE TILE IS PINNED ONCE PER TEMPLATE (one import-pipeline visit, not one
    // per floor: World has 25). Each floor still gets its OWN material instance
    // — they could share one, but then editing one floor's material would edit
    // all 25, and a floor is an ordinary node; the decode buckets by shader words
    // anyway, so sharing would buy no draw.
    QString tileGuid;
    const QString tilePath = defaultfloormaterial::pinTile(db, project, &tileGuid);
    auto makeFloor = [this, &tileGuid, &tilePath](const QString &name, const iris::Vec3 &centre) {
        const QString guid = GUIDManager::generateGUID();
        iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
            QStringLiteral(":/content/primitives/cube.obj"), name, guid, db);
        // cube.obj is a 2 m cube about its centre.
        node->setLocalScale(iris::Vec3(scenetemplate::kFloorSize * 0.5f,
                                       scenetemplate::kFloorThickness * 0.5f,
                                       scenetemplate::kFloorSize * 0.5f));
        node->setLocalPos(centre + iris::Vec3(0, -scenetemplate::kFloorThickness * 0.5f, 0));
        node->setShadowCastingEnabled(false);
        node->setPickable(false);          // ships LOCKED (above)
        node->defaultFloor = true;
        iris::PhysicsProperty physics;
        physics.objectMass = 0.0f;
        physics.isStatic = true;
        physics.objectCollisionMargin = 0.1f;
        physics.objectRestitution = 0.01f;
        physics.type = iris::PhysicsType::Static;
        physics.shape = iris::PhysicsCollisionShape::Cube;
        node->isPhysicsBody = true;
        node->physicsProperty = physics;

        const bool realProject = db && project && !project->getProjectGuid().isEmpty();
        if (realProject) {
            // The node's own Object row, as addPrimitive writes for any cube.
            QJsonObject props;
            props.insert(QStringLiteral("type"), QStringLiteral("builtin"));
            db->createAssetEntry(guid, name, static_cast<int>(ModelTypes::Object),
                                 project->getProjectGuid(), project->getProjectGuid(),
                                 QString(), QString(), QByteArray(),
                                 QJsonDocument(props).toJson(), QByteArray(), QByteArray());
        }
        node->setMaterial(defaultfloormaterial::createUnpinned(tilePath));
        if (realProject && !tileGuid.isEmpty())
            db->createDependency(static_cast<int>(ModelTypes::Object),
                                 static_cast<int>(ModelTypes::Texture), guid, tileGuid,
                                 project->getProjectGuid());
        return node;
    };

    if (kind == SceneTemplate::World) {
        // WORLD: twenty-five Basic floors, 5 x 5, edge to edge, centred on the
        // origin — a 500 m square standing in for terrain (Terra later). 500 m
        // is exactly the dynamic shadows' reach (OgreEngine's shadow far).
        auto group = iris::SceneNode::create();
        group->setName(QStringLiteral("World Floor"));
        group->setPickable(false);         // locked like the floors it holds
        scene->rootNode->addChild(group);
        const int n = scenetemplate::kWorldTilesPerSide;
        const float s = scenetemplate::kFloorSize;
        int index = 0;
        for (int row = 0; row < n; ++row) {
            for (int col = 0; col < n; ++col) {
                const iris::Vec3 centre((col - (n - 1) * 0.5f) * s, 0.0f,
                                        (row - (n - 1) * 0.5f) * s);
                group->addChild(makeFloor(QStringLiteral("Floor %1").arg(++index), centre));
            }
        }
    } else {
        scene->rootNode->addChild(makeFloor(QStringLiteral("Floor"), iris::Vec3(0, 0, 0)));
    }

    auto dlight = iris::LightNode::create();
    dlight->setLightType(iris::LightType::Directional);
    scene->rootNode->addChild(dlight);
    dlight->setName("Directional Light");
    dlight->setLocalPos(iris::Vec3(4, 4, 0));
    // THE SUN HIGH AND BEHIND THE DEFAULT CAMERA (SKY-DEFAULTS-1; Unreal's default
    // class): 50 degrees of elevation, and its azimuth the camera's back — the
    // editor camera stands at (0, 5, 14) looking at the origin, down -Z, so the
    // sun stands towards +Z and its light travels (0, -sin 50, -cos 50). The
    // floor in view is fully lit and the sky ahead is the deep-blue side, away
    // from the sun. A pitch of 40 degrees about X turns the light's -Y to that.
    dlight->setLocalRot(iris::Quat::fromEulerAngles(scenetemplate::kSunPitchDegrees, 0, 0));
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

    // THE DEFAULT SKY IS THE REAL ONE (owner answer Q1, 2026-09-18: "the
    // default new scene = the realistic real-time sky WITH the sun following
    // it"). The planet's atmosphere is drawn on the GPU (SKY-ATMOSPHERE-1),
    // it takes its sun — direction and light — from the scene's sun, the
    // directional light above, which is why the light is created first, and
    // the Sky Light integrates it for the scene's ambient. Sun
    // Follows Atmosphere needs no line here: LightNode::followsAtmosphere is
    // TRUE by default, and this is the sky that makes it mean something (on a
    // picked colour the tint is white and the row says so). Its dials are
    // SkyRealistic::defaults(), written through the one setter so the typed
    // fields and the JSON half cannot disagree.
    //
    // BOTH SELFTEST HASHES MOVE WITH THIS, by design: the self-test renders
    // this template, and the template's backdrop and ambient are now an
    // atmosphere instead of a flat 96-grey.
    scene->skyType = iris::SkyType::REALISTIC;
    scene->setSkyRealistic(iris::SkyRealistic::defaults());
    // The picked sky COLOUR stays what it was: it is what the World panel
    // shows the moment a user switches the sky back to Single Color, and the
    // fog colour reads from it (96 grey — owner pick 1, SKY_LIGHT_SPEC §9.1
    // option ii: srgb(96) decoded and integrated over the hemisphere is 0.117
    // of radiance against the old flat path's 0.120).
    scene->skyColor = QColor(96, 96, 96);
    scene->fogColor = QColor(96, 96, 96);
    scene->shadowEnabled = true;
    // THE EXPONENTIAL HEIGHT FOG ON (SKY-DEFAULTS-1; the owner's Unreal Basic
    // level): the world's medium at iris::HeightFog's dials — Unreal's density
    // and falloff, from 100 m, so the floor is untouched and the far world, the
    // horizon and everything under it take the sky's own blue. The World fog
    // stays off.
    scene->heightFog = iris::HeightFog();
    scene->heightFog.enabled = true;

    sceneNodeSelected(scene->rootNode);

    return scene;
}

void MainWindow::setSettingsManager(SettingsManager* settings)
{
    this->settings = settings;
}

// THE WINDOW-CENTRE NOTICE — a page that cannot start, VR that did not. One
// toast, reused; its anchor is the Toast's own (audit F-D4).
void MainWindow::showNotice(const QString &title, const QString &text)
{
    if (!viewErrorToast) viewErrorToast = new Toast(this);
    viewErrorToast->setAnchor(Toast::Anchor::WindowCentre);
    viewErrorToast->showToast(title, text);
}

void MainWindow::showPlayerVrState(bool available, bool active)
{
    if (playerView) playerView->showVr(available, active);
}

SettingsManager* MainWindow::getSettingsManager()
{
    return settings;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // (The docks' own events — the title-bar X and the Presets line — are the
    // EditorDocks' filter.)
    switch (event->type()) {
        case QEvent::MouseButtonPress: {
            // (`dragging = true` stood here: a write-only member nothing has
            // ever read, uninitialised until this event — deleted with the
            // play-mode flag it sat beside, SMOKE-FIX-1.)
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
    // STEP 1 of the shutdown order — the whole sequence is ShellLifecycle's.
    lifecycle->closeRequested(event);
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
    // AND IT ANSWERS (SMOKE-FIX-1's fix round): the switch can refuse — no
    // world open, or a Player page that cannot draw and bounced — and the
    // service must stop rather than start the scene on the page the user was
    // left on.
    playerService->setSpaceActivator([this]() {
        if (!projectService || !projectService->isSceneOpen()) return false;
        this->switchSpace(WindowSpaces::PLAYER);
        return currentSpace == WindowSpaces::PLAYER;
    });
    if (playerView) {
        auto *widget = playerView;
        connect(playerService, &PlayerService::playingChanged, widget,
                [widget](bool playing) { widget->showPlaying(playing); });
        QVariantMap vrIconOptions;
        vrIconOptions.insert("color", QColor(255, 255, 255));
        vrIconOptions.insert("color-active", QColor(255, 255, 255));
        // The Player page's own VR button fires the vr.toggle row — the VR
        // module's — exactly as the chord and the toolbar action do.
        widget->setVrToggle([this]() { actionHost->trigger(QStringLiteral("vr.toggle")); },
                            fontIcons->icon(fa::binoculars, vrIconOptions));
    }

    projectService = new ProjectService(db, project, settings,
                                        sceneView, undoService,
                                        [this]() { return scene; });

    sceneEditService = new SceneEditService(db, project, undoService,
                                            selectionService, sceneView,
                                            [this]() { return scene; }, this);
    connect(sceneEditService, &SceneEditService::hierarchyChanged, this, [this]() {
        docks->hierarchy()->repopulateTree();
    });
    // The undo commands' refresh notifications (Phase 4: was
    // UiManager::docks->hierarchy() / ::propertyWidget reach-ins).
    connect(sceneEditService, &SceneEditService::nodeInserted, this,
            [this](const iris::SceneNodePtr &node) {
        if (docks->hierarchy()) docks->hierarchy()->insertChild(node);
    });
    connect(sceneEditService, &SceneEditService::nodeRemoved, this,
            [this](const iris::SceneNodePtr &node) {
        if (docks->hierarchy()) docks->hierarchy()->removeChild(node);
        // A node that has left the document cannot stay in the selection SET
        // (EDITOR_MULTISELECT_SPEC §2.1). The single selection was pruned by
        // the delete command's select(null); a set member three rows down was
        // not, and a stale member would keep an outline shell alive and feed a
        // dead node to the next group transform.
        if (selectionService) selectionService->remove(node);
    });
    connect(sceneEditService, &SceneEditService::transformRefreshRequested, this, [this]() {
        if (docks->properties()) docks->properties()->refreshTransform();
    });
    connect(sceneEditService, &SceneEditService::assetViewRefreshRequested, this, [this]() {
        docks->assetTray()->updateAssetView(docks->assetTray()->assetItem.selectedGuid);
    });
    connect(sceneEditService, &SceneEditService::materialApplied, this, [this](const QString &) {
        docks->properties()->refreshMaterial();
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
        if (docks->assetTray()) docks->assetTray()->updateAssetView(docks->assetTray()->assetItem.selectedGuid);
    });

    thumbnailService = new ThumbnailService(db, project);
    assetService = new AssetService(db, project);

    // THE HOVER PREVIEW (MATERIAL-PREVIEW-1). Constructed after the service
    // that resolves and applies materials, and handed BACK to it: every apply
    // ends a live preview before it pushes, so an undo step can never capture
    // a material the user only hovered.
    materialPreviewService = new MaterialPreviewService(sceneEditService);
    sceneEditService->setMaterialPreview(materialPreviewService);

    services = new StudioServices;
    services->eventBus = new Subscriber(this);
    services->undo = undoService;
    services->selection = selectionService;
    services->playback = playbackService;
    services->player = playerService;
    services->project = projectService;
    services->sceneEdit = sceneEditService;
    services->materialPreview = materialPreviewService;
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

    // THE SCENE-ERROR AREA (services/sceneissues.h, owner Q1b/Q1c): the bar
    // over the viewport and its 1 Hz scanner, and the library's own write
    // failure raised into it (shell/sceneissuewatch.h).
    issueWatch->setScene(sceneEditService, sceneView);
    issueWatch->start();

    // Commands raise their refreshes through the aggregate (stamped at push);
    // the viewport's gizmos push through the same aggregate.
    undoService->setServices(services);
    // AN UNDO REPAINTS THE PANEL (debt L6): every properties row is undoable
    // now, and the rows are the document's state on screen. One hook, deferred
    // by the panel itself, rather than a refresh callback on every command.
    undoService->setStackMovedHook([this]() {
        if (docks->properties()) docks->properties()->refreshFromDocument();
    });
    // THE DEFERRED DATABASE WORK OF THE COMMANDS A CLEAR DESTROYS (CLOSE-1).
    // A command's destructor queues its asset-row cleanup instead of writing
    // it — one transaction for the whole stack, here, instead of one
    // transaction and one fdatasync per command on the UI thread.
    undoService->setDeferredFlushHook([this]() {
        if (db) db->flushPendingAssetDeletes();
    });
    // THE HOVER PREVIEW ENDS BEFORE ANYTHING COMMITS (MATERIAL-PREVIEW-1).
    // Two hooks, at the two spines: every undo push, and every scene write.
    // Between them they cover the whole "a material on screen that the
    // document does not hold" hazard — including the callers written after
    // this — and the three LIFECYCLE ends (close, space switch, quit) are
    // spelled out at their own sites, where a hook would have nothing to hang
    // on.
    undoService->setPrePushHook([this]() {
        if (materialPreviewService) materialPreviewService->end();
    });
    projectService->setPreWriteHook([this]() {
        if (materialPreviewService) materialPreviewService->end();
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

// THE ASSET SEAMS (AVATAR_ASSET_SPEC §5.5): a page or the viewport asks for an
// asset to be opened / spawned / assigned; the hub hands it to the module that
// contributed its KIND, and the module calls its own verb. Neither side learns
// about the other.
void MainWindow::spawnAvatarAsset(const QString &guid, const iris::Vec3 &position,
                                  bool hasPosition)
{
    AssetRef ref;
    ref.guid = guid;
    ref.kind = QStringLiteral("avatar");
    ref.intent = AssetRef::Intent::Spawn;
    ref.hasPosition = hasPosition;
    ref.position[0] = position.x();
    ref.position[1] = position.y();
    ref.position[2] = position.z();
    moduleHub->openAsset(ref);
}

void MainWindow::assignAnimationAsset(const QString &guid, const iris::SceneNodePtr &node)
{
    AssetRef ref;
    ref.guid = guid;
    ref.kind = QStringLiteral("avatar");
    ref.intent = AssetRef::Intent::Assign;
    if (node) {
        ref.targetGuid = node->getGUID();
        ref.targetName = node->getName();
    }
    moduleHub->openAsset(ref);
}

void MainWindow::openAssetInModule(const QString &guid, const QString &kind,
                                   const QString &scope)
{
    AssetRef ref;
    ref.guid = guid;
    ref.kind = kind;
    ref.scope = scope;
    ref.intent = AssetRef::Intent::Open;
    moduleHub->openAsset(ref);
}

void MainWindow::switchSpace(WindowSpaces space, bool force)
{
	if (currentSpace == space && !force)
		return;
	// The material hover preview belongs to the editor viewport's drag; leaving
	// the space ends the gesture, so it ends the preview (MATERIAL-PREVIEW-1).
	if (materialPreviewService) materialPreviewService->end();
	// Every attempt starts with a clean slate: whatever refused last time is
	// not the reason this one might (SMOKE-FIX-1).
	spaceRefusal.clear();
	SessionMarkers::logSpaceSwitch(spaces::id(currentSpace), spaces::id(space));

	// properly shutdown previous space
	switch (currentSpace) {
	case WindowSpaces::PLAYER:
		playerView->end();
		break;
	case WindowSpaces::EDITOR:
		leaveEditorSpace();
		break;
    default:
        break;
	}

    previousSpace = currentSpace;
    switch (currentSpace = space) {
        case WindowSpaces::DESKTOP: {
			if (projectService->isSceneOpen() && sceneView->isInitialized())
				updateCurrentSceneThumbnail();
			// THE GRID IS NOT REBUILT HERE (CREATE-GAP-1). It was, on every entry
			// (TRAY-REPOP-1: a close used to come back to a grid that had never
			// seen the projects made since boot) — correct, and O(N) PNG decodes
			// on the UI thread inside every close: 450 ms at 40 tiles, the create
			// gap open.responsive reds on. The grid is a MODEL now: a project's
			// tile is added when its row is made, removed with it, re-ordered and
			// re-thumbnailed by its saves — so a close has nothing to rebuild.
			// The entry builds the grid only the first time the desktop shows,
			// and otherwise refreshes the open markers (ProjectManager::
			// enterDesktop).
			pmContainer->enterDesktop();

			pageHost->show(spaces::id(WindowSpaces::DESKTOP));

            hideEditorPanels();
            ui->actionClose->setDisabled(true);
            break;
        }

        case WindowSpaces::EDITOR: {
			// The whole entry lives in enterEditorSpace() — the reveal of a load
			// IN PLACE needs exactly this and never comes through here
			// (VIEW-REBUILD-1). `return`, not `break`, when it bounces: goToDesktop
			// has already run a whole switchSpace(DESKTOP) inside that call, so
			// falling through to this one's trailing updateTopMenuStates(EDITOR)
			// would dress the menus for a page nobody is looking at.
			if (!enterEditorSpace()) return;
            break;
        }

        case WindowSpaces::PLAYER: {
            pageHost->show(spaces::id(WindowSpaces::PLAYER));
            hideEditorPanels();
            toolBar->setVisible(false);

            playbackService->setSceneMode(SceneMode::PlayMode);
            playSceneBtn->hide();
            this->enterPlayMode();
			// A PAGE THAT CANNOT DRAW GOES BACK (SMOKE-FIX-1) — the same
			// contract bounceIfViewportIsDead gives the editor. The Player is a
			// second view on the editor's engine scene; when that scene cannot
			// be had, an enabled View with nothing bound presents NO pixels and
			// the window keeps showing the page underneath ("it says the Player
			// is selected but I only see the desktop"). Say why and stay where
			// the user could see something.
			QString playerWhy;
			if (!playerView->begin(&playerWhy)) {
				bounceFromPlayer(playerWhy);
				return;
			}
            // PLAY, not toggle (audit F4): entering the space is a statement,
            // not a button press. `app.space("player")` after `player.play()`
            // used to STOP the scene.
            playerView->playScene();

            break;
        }

        case WindowSpaces::ASSETS: {
            ensureAssetsPage();   // built on its first showing (D11-LIBRARY-SCALE)
            pageHost->show(spaces::id(WindowSpaces::ASSETS));
            _assetView->setFocus();
			_assetView->spaceSplits();
    		hideEditorPanels();
    		toolBar->setVisible(false);
			if (projectService->isSceneOpen()) {
				playSceneBtn->hide();
			}
    		
			break;
    	}

		case WindowSpaces::EFFECT: {
			pageHost->show(spaces::id(WindowSpaces::EFFECT), true);
			toolBar->setVisible(false);
			break;
		}

		case WindowSpaces::PUBLISH: {
			pageHost->show(spaces::id(WindowSpaces::PUBLISH));
			hideEditorPanels();
			toolBar->setVisible(false);
			if (projectService->isSceneOpen()) playSceneBtn->hide();
			break;
		}

		case WindowSpaces::AVATAR: {
			pageHost->show(spaces::id(WindowSpaces::AVATAR), true);
			hideEditorPanels();
			toolBar->setVisible(false);
			if (projectService->isSceneOpen()) playSceneBtn->hide();
			break;
		}

        default: break;
    }

	// EVERY MODULE HEARS THE SWITCH (StudioModule::onSpaceChanged) — the
	// Materials page re-reads its graph on the way in. A refused switch never
	// gets here: its bounce made a switch of its own.
	moduleHub->spaceChanged(spaces::id(previousSpace), spaces::id(space));

	updateTopMenuStates(space);
	// The scene-issue bar belongs to the EDITOR and is a top-level window that
	// stays on top: it has to go NOW, not on the scanner's next tick (item 3).
	issueWatch->update();
}

// ENTERING THE EDITOR PAGE — switchSpace's EDITOR case, and the reveal's
// (VIEW-REBUILD-1, 2026-09-21).
//
// Every line below used to run for a load in place too, because the open's
// close half went to the Desktop first and the reveal came back. It no longer
// does (MainWindow::CloseIntent), so the reveal calls this directly. Each call
// here is idempotent on a page that never left — the page index, the dock
// visibility (Qt returns early for a dock that is already visible), the edit
// mode, `sceneView->begin()` — and two of them are the point: the views label,
// which a scene open resets to perspective, and the cover/bounce check.
bool MainWindow::enterEditorSpace()
{
    pageHost->show(spaces::id(WindowSpaces::EDITOR));

	docks->applyVisibility();
	playerControls->setVisible(false);

	docks->applyColumnWidthsOnce();

    playSceneBtn->show();
    this->enterEditMode();
    playbackService->setSceneMode(SceneMode::EditMode);

    docks->assetTray()->refresh();
	// The dropdown follows the VIEWPORT, and a scene open resets it to
	// perspective (per-view camera memory is per scene session) — so
	// re-read it here rather than leaving "Top" over a fresh scene.
	viewController->followViewport();

	sceneView->begin();
	// The on-screen View could not be created at all: nothing will ever present
	// into this page, so say why and go back to one that works, rather than
	// leaving the user on a permanent blank (STATS_OVERLAY_SPEC.md §6.4 — this
	// is where ViewportCover's Failed state went).
	if (bounceIfViewportIsDead()) return false;
	// The viewport's native window has just been mapped. Until the engine
	// presents into it the X server shows whatever was on that part of the
	// screen before — the page we just left. Present the cover NOW
	// (synchronously; a queued driver tick would arrive after the rest of this
	// open) unless the engine already owns those pixels. On a load in place the
	// window never went away and the engine has been presenting its background
	// all along (STALE-VIEW-1), so this is a no-op there.
	sceneView->coverIfNotPresenting();
    return true;
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
    if (!docks->assetTray()) return false;
    return docks->assetTray()->importFiles(files);
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

	if (!!scene) {
		playerView->endVrForSceneClose();   // the Player's session before its scene (see closeProject)
		removeScene();
	}

	updateWindowTitle();
}

void MainWindow::openStageRead(const iris::MeshPrewarmPtr &prewarm)
{
	LoadTimeline::mark(QStringLiteral("readProjectScene"));
	openPendingEditorData = Q_NULLPTR;
	openPendingScene = projectService->readProjectScene(&openPendingEditorData, prewarm);
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
	// A MODEL WITH NO CURRENT BAKE IS MISSING, AND SAID SO (FORWARD-ONLY-1):
	// the reader never parses in its place. setScene cleared the issue store,
	// so this is raised after it.
	SceneIssues::instance().raiseMissingModels(projectService->missingModels());
	assistant->refreshChatContext();   // D1: rebind an open chat to the new project
	// THE MODULES HEAR THE PROJECT (StudioModule::onProjectChanged): the
	// Materials page's open tabs are per project (MATERIALS_TABS_SPEC §2.7).
	moduleHub->projectChanged(project);

	if (editorData != Q_NULLPTR) {
		sceneView->setEditorData(editorData);
		// needs to be done so controllers can have the correct
		// camera
		playerView->setScene(scene);
		// (The grid and light-wire checkmarks follow setEditorData's
		// overlaysChanged — syncOverlayChecks.)
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
	docks->assetTray()->trigger();
	undoService->resetSavedCount();
}

void MainWindow::openStageReveal(bool playMode)
{
	LoadTimeline::mark(QStringLiteral("switchSpace"));
	// THE PAGE THIS OPEN STARTED ON (VIEW-REBUILD-1). A load in place never
	// leaves the editor any more, and switchSpace returns at once when the space
	// it is asked for is already current — so the editor's own per-open dressing
	// (the views label a fresh scene resets, the edit-mode chrome, the cover
	// check) has to be asked for here, through the same function switchSpace
	// calls. An open from the Desktop, a create and a play-mode open are
	// unchanged: for them the page switch IS the reveal.
	const bool alreadyInEditor = !playMode && currentSpace == WindowSpaces::EDITOR;
	playMode ? switchSpace(WindowSpaces::PLAYER) : switchSpace(WindowSpaces::EDITOR);
	if (alreadyInEditor) enterEditorSpace();
	// A REVEAL THAT ASKED FOR THE PLAYER AND DID NOT GET IT IS NOT A PLAYER
	// REVEAL (SMOKE-FIX-1's fix round, F2). bounceFromPlayer can send this open
	// to the editor instead, and everything below — the top bar's dressing, the
	// autoplay — was still dressing the Player: `setPlayerMode(true)` was
	// latched at bind time (openStageBind) and playScene() would have started
	// play-IN-PLACE in an editor the user is looking at. The rest of this reveal
	// treats the space the window actually landed on as the truth.
	if (playMode && currentSpace != WindowSpaces::PLAYER) {
		playbackService->setPlayerMode(false);
		playMode = false;
	}
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
		docks->hierarchy()->setSelectedNode(scene->getRootNode());
		docks->properties()->setSceneNode(scene->getRootNode());
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
	// THE BAKE READS, OFF THIS THREAD, WITH THE CALLER STILL BLOCKED
	// (OPEN-ASSIMP-1; since FORWARD-ONLY-1 the worker only ever READS BAKES —
	// a model with no current bake is shown missing, never parsed). The
	// synchronous open owes its caller a loaded world
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

	// STALE BAKES ARE REBUILT FROM THEIR OWN SOURCES FIRST (FORWARD-ONLY-1
	// D1): a bake is a cache of the parse, and a build that changed the code
	// producing it rebuilds it — on a worker, this thread pumping, with the
	// open's progress up — before anything reads. Never a parse on the open.
	MeshBakeStore::rebuildPumped(
	    MeshBakeStore::staleJobsFor(QSqlDatabase::database(), AssetStorePaths::root(), modelPaths),
	    [this](int i, int n) {
		    if (pmContainer)
			    pmContainer->showOpenProgress(5 + (20 * i) / qMax(1, n),
			                                  tr("Rebuilding models (%1 of %2)…").arg(i + 1).arg(n));
	    });

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
			// Named "assimp" for continuity of the ledger; it is a bake read.
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
	// The pass shape this machine's editor draws with, for the NEXT launch's
	// startup gate (F1b): two settings values, written only on change.
	sceneView->rememberPassShape();
	openStageReveal(playMode);
	// THE LOAD IS OVER (OPEN_COVER_SPEC §2 A). Said here and at the runner's
	// `finished` — the two ends of the two routes — and NOT inside
	// `openStageReveal`, which is a SLICE on the threaded route with one more
	// boundary frame behind it: that frame is the world's first, and letting it
	// build the whole GI arm is the block this lane removes, one frame later.
	sceneView->endSceneLoad();
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

// THE RUNNER AND WHAT HAPPENS BETWEEN TWO OF ITS SLICES. Built once per window,
// by whichever of the two routes — the open or the create — reaches it first.
void MainWindow::startOpenRunnerIfNeeded()
{
	if (openRunner) return;
	openRunner = new SceneOpenRunner(db, project, this);
	connect(openRunner, &SceneOpenRunner::progress, this,
	        [this](int percent, const QString &text) {
		        if (pmContainer) pmContainer->showOpenProgress(percent, text);
	        });
	connect(openRunner, &SceneOpenRunner::finished, this, [this](bool) {
		if (pmContainer) pmContainer->hideOpenProgress();
		// EVERY SLICE HAS RUN, AND SO HAS THE LAST BOUNDARY FRAME: the world is
		// installed and on screen, so the engine may build its first GI arm
		// (OPEN_COVER_SPEC §2 A). One frame later than the reveal, and
		// deliberately — see openStageReveal.
		if (sceneView) sceneView->endSceneLoad();
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
		LoadTimeline::Accumulate row(QStringLiteral("slice:boundaryFrame"));
		if (sceneView && sceneView->canRenderFrames()) {
			sceneView->renderFrames(1);
			++openSliceBoundaryFrameCount;
			return;
		}
		if (auto engine = EngineHost::instance().engine()) engine->advanceResources();
	});
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

	startOpenRunnerIfNeeded();

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
	// 4x4 target and reaches permutations the camera cannot see. fork 8282f6d70
	// (was 0016) makes that route run at all, but a second upstream read-after-destroy
	// kills the app on the second world of a session, so it ships behind
	// JAHSHAKA_WARMUP_PASS=1 (the crash is documented in OgreChain.cpp).
	// The switch stays in Preferences -> Cache for anyone who wants it off.
	if (settings->get(settingkeys::shaderWarmupOnOpen)) {
		slices.append({ QStringLiteral("Precompiling shaders…"), 95, [this]() {
			LoadTimeline::mark(QStringLiteral("warmUpShaders"));
			const unsigned built = sceneView->warmUpShaders();
			if (built) qInfo("scene open: precompiled %u shader(s) behind the cover", built);
		} });
	}
	// THE PASS SHAPE for the next launch's startup gate (SHADER_CACHE_AUDIT
	// F1b): two settings values, written only on change.
	//
	// (What used to be here as well — WARMUPSET-2, 2026-09-21 — was the
	// RECORDING of this world's permutation set "for the next launch". That set
	// named its materials by a process-unique datablock name, so it resolved
	// nothing in the next process and warmed the default datablock instead; the
	// whole machinery is deleted. The slice above, the per-scene PSO precache
	// behind the cover, is what actually precompiles a world, and it stays.)
	slices.append({ QStringLiteral("Precompiling shaders…"), 96,
	                [this]() { sceneView->rememberPassShape(); } });
	slices.append({ QStringLiteral("Opening…"), 100,
	                [this, playMode]() { openStageReveal(playMode); } });

	openRunner->setPlan(modelPaths, slices,
	                    project ? project->getProjectName() : QStringLiteral("scene"));
	openRunner->start();
}

// The close a user asked for: the world goes and the window lands on the
// Desktop (VIEW-REBUILD-1 gave the other half of this function a name).
void MainWindow::closeProject()
{
    closeProject(CloseIntent::ToDesktop);
}

void MainWindow::closeProject(CloseIntent intent)
{
    // The borrowed material goes back before the scene it is borrowed from is
    // torn down (MATERIAL-PREVIEW-1).
    if (materialPreviewService) materialPreviewService->end();
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
            // closePrevious:save — a create's ledger shows its close now
            // (CREATE-GAP-1); a no-op outside a LoadTimeline run.
            LoadTimeline::Accumulate row(QStringLiteral("closePrevious:save"));
            if (settings->get(settingkeys::autoSave)) saveScene();
        }

        scene->getPhysicsEnvironment()->destroyPhysicsWorld();

        playSimBtn->setText("Simulate Physics");
        playSimBtn->setToolTip("Simulate physics only");

        QVariantMap options;
        options.insert("color", QColor(52, 152, 219));
        options.insert("color-active", QColor(52, 152, 219));
        playSimBtn->setIcon(fontIcons->icon(fa::play, options));
    }

    projectService->setSceneOpen(false);
    // Nothing to force-save any more (owner, 2026-09-18: the button is always
    // THERE, and it is live exactly while there is a world under it).
    actionSaveScene->setEnabled(false);

    // The desktop's tiles carry the open marker (dark blue caption bar,
    // "[ Open ]" caption, Close instead of Play/Edit). Refresh them the moment
    // the flag goes false: closing while already on the desktop returns early
    // below and never reaches switchSpace(DESKTOP)'s rebuild. Found by
    // scripting.e2e.desktops' open-flag assertion (2026-09-08).
    if (pmContainer) pmContainer->refreshOpenTiles();

    playbackService->setPlaying(false);
    ui->actionClose->setDisabled(false);
    assistant->refreshChatContext();   // D1: an open chat loses its project
    // The modules hear the close: the Materials page saves this project's open
    // tabs and closes the ones that were the PROJECT's copies.
    moduleHub->projectChanged(nullptr);

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
    //
    // THE PLAYER'S VR SESSION ENDS BEFORE ITS SCENE GOES (the lead, from the
    // Fable read of VR-4-FIX): the Player's session is bound to the editor's
    // one engine scene, and playerView->end() used to run only AFTER
    // switchSpace below — so a close with the headset on reached the engine's
    // own safety net in OgreEngine::destroyScene instead of the Player's end.
    // The net stays; the ordinary flow does not need it.
    LoadTimeline::Accumulate teardown(QStringLiteral("closePrevious:teardown"));
    playerView->endVrForSceneClose();
    removeScene();

    scene->cleanup();
    scene.clear();
    teardown.stop();

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

	// A CLOSE THAT IS THE FIRST HALF OF AN OPEN STAYS ON THE PAGE IT IS ON
	// (VIEW-REBUILD-1). Everything above this line has already run — the
	// autosave, the physics stop, the tile refresh, the undo clear, the asset
	// list, removeScene() (which is what takes the world off the viewport),
	// scene->cleanup(), the engine's reclaim — so the teardown is identical to a
	// ToDesktop close. Only the last three lines are skipped, and each of them
	// is about LEAVING:
	//
	//   switchSpace(DESKTOP)  would hide the editor page, and the page is a
	//                         native X ancestor of the viewport's own window:
	//                         measured 499-2,973 ms of an in-place open with the
	//                         viewport UNVIEWABLE and the app's watermark on
	//                         screen, five rect changes as the docks came back.
	//   sceneView->end()      would disable the editor's View for the whole load
	//                         — and a disabled view presents nothing, which is
	//                         the stale frame STALE-VIEW-1 removed coming back
	//                         through the other door. The view stays live and
	//                         draws its background until the new world binds.
	//
	// THE PLAYER'S VIEW STILL ENDS, either way, and it is not a page statement:
	// it is bound to the scene that has just been destroyed above, and the page
	// it belongs to is not the page an open reveals into. Measured: without this
	// line, `project.open` from the PLAYER space comes back to a BLACK player
	// frame (scripting.e2e.space_switch's "the Player renders it, not a black
	// frame"), because the Player's view kept its binding to the dead scene
	// across the swap.
	playerView->end();

	// The reveal calls enterEditorSpace() itself when the page never left.
	if (intent == CloseIntent::ReopenInPlace) return;

    LoadTimeline::Accumulate toDesktop(QStringLiteral("closePrevious:switch"));
    switchSpace(WindowSpaces::DESKTOP);

	if (sceneView->isInitialized())
		sceneView->end();
}

// (MainWindow::applyMaterialPreset is GONE, both overloads — MATERIAL-PREVIEW-1.
// It was a SECOND material dispatcher beside material.apply's, reached only by
// the viewport's drop and the tray's double-click, and the two had already
// drifted in what they accepted. Both callers ask
// SceneEditService::applyMaterial now, with the target explicit.)


// THE PANELS' TWO CALLS INTO THE WINDOW, answered by the editor's docks: the
// tray's favourite (the Presets panel of its kind) and the edit gate's repaint.
void MainWindow::favoriteItem(QListWidgetItem *item)
{
    docks->favoriteItem(item);
}

void MainWindow::refreshPropertiesFromDocument()
{
    docks->refreshPropertiesFromDocument();
}

SceneHierarchyWidget *MainWindow::hierarchyPanel() const
{
    return docks ? docks->hierarchy() : nullptr;
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

    // THE PROCESS'S TEST TIER (TEST-TIER-1, services/testtier.h): a test process
    // that asked for one puts EVERY scene it binds on that World Mode — new or
    // opened, after the reader, through the call world.mode makes — before the
    // viewport below hands it to the engine, so the Epic chain is never built.
    // A process with none (the product) keeps the scene's own tier.
    if (scene && testtier::active())
        worldmodes::setMode(scene, worldmodes::modeFromName(testtier::name()));

    this->scene = scene;
    //this->sceneView->context()->setShareContext(loadingContext);
    { LoadTimeline::Accumulate a(QStringLiteral("setScene:viewport")); this->sceneView->setScene(scene); }
    { LoadTimeline::Accumulate a(QStringLiteral("setScene:player"));   this->playerView->setScene(scene); }
    { LoadTimeline::Accumulate a(QStringLiteral("setScene:hierarchy")); docks->hierarchy()->setScene(scene); }
    { LoadTimeline::Accumulate a(QStringLiteral("setScene:properties")); docks->properties()->setScene(scene); }

    // interim...
    { LoadTimeline::Accumulate a(QStringLiteral("setScene:updateSettings")); updateSceneSettings(); }
}

void MainWindow::removeScene()
{
    // Scene-scoped teardown only — the engine view must survive a project
    // swap (script sessions never re-trigger the showEvent that recreates it).
    sceneView->clearScene();
    docks->properties()->setScene(iris::ScenePtr());
    docks->properties()->setSceneNode(iris::SceneNodePtr());
}

void MainWindow::assetItemSelected(QListWidgetItem *item)
{
	emit sceneNodeSelected(iris::SceneNodePtr());
	docks->properties()->setAssetItem(item);
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
    // WHAT THIS COSTS, PER CONSUMER (SELECT-COST-1, 2026-09-18): `vr.select()`
    // measured 16-17 ms per call and a desktop click paid the same, which at
    // 90 Hz is more than a frame for a trigger press. The four calls below are
    // charged separately — plus the Properties column's DEFERRED mount, which
    // lands in a later turn and no timer around this function can see — and
    // `editor.selectionCost()` reads them back.
    //
    // A RE-SELECTION IS NOT A NO-OP HERE, deliberately: three callers
    // re-select the node they already have precisely to REFRESH the panels
    // after changing the document under them (ReparentSceneNodeCommand's
    // undo and redo, material.apply), and the service's own contract says a
    // replace always re-emits. What makes it cheap is that the consumers
    // themselves build nothing when nothing changed — the column re-points
    // its blades (0.26 ms) instead of re-showing them — so the counter below
    // records honestly how many of these fan-outs really moved the primary.
    const bool primaryChanged = lastAppliedSelection.toStrongRef() != sceneNode;
    lastAppliedSelection = sceneNode.toWeakRef();
    selcost::noteSelection(primaryChanged);
    { selcost::Scope s(selcost::Viewport);   sceneView->setSelectedNode(sceneNode); }
    { selcost::Scope s(selcost::Properties); docks->properties()->setSceneNode(sceneNode); }
    { selcost::Scope s(selcost::Hierarchy);  docks->hierarchy()->setSelectedNode(sceneNode); }
    { selcost::Scope s(selcost::Timeline);   docks->timeline()->setSceneNode(sceneNode); }
}

// The consumers that understand a SET: the outliner's selected rows and the
// viewport (outline, gizmo group, focus/orbit/floor). The properties panel and
// the timeline stay on the primary — multi-edit is out of scope for v1
// (EDITOR_MULTISELECT_SPEC §4).
//
// THIS RUNS ON EVERY SINGLE PICK TOO, which is why its two calls are charged
// like the four above (SELECT-COST-1's second read): `SelectionService::select`
// emits selectionChanged AND selectionSetChanged, so a plain click, a verb and
// a `vr.select` all write the viewport and the outliner twice — once with the
// primary, once with the set of one. `editor.selectionCost()` would otherwise
// call four consumers "the whole cost as the user pays it".
void MainWindow::applySelectionSetToUi(const QList<iris::SceneNodePtr> &nodes)
{
    { selcost::Scope s(selcost::SetViewport);
      if (sceneView) sceneView->setSelectedSet(nodes); }
    { selcost::Scope s(selcost::SetHierarchy);
      if (docks->hierarchy()) docks->hierarchy()->setSelectedSet(nodes); }
}

// ONE SLOT FOR EVERY PRIMITIVE (owner review R6). Thirteen identical
// forwarding slots stood here — one per shape, four of them (Teapot, Sponge,
// Steps, Gear) connected to nothing at all — and every new primitive needed a
// slot, a declaration and a hand-written menu entry. The Add menu builds itself
// from src/data/primitives.h now and carries the row's NAME on the action.
void MainWindow::addPrimitiveFromAction()
{
    const QAction *action = qobject_cast<QAction *>(sender());
    if (!action || !sceneEditService) return;
    sceneEditService->addPrimitive(action->data().toString());
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
                                             docks->assetTray()->assetItem.selectedGuid);
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
        QStringLiteral("%1_%2.%3").arg(node->getName(),
                                      QString::number(static_cast<time_t>(currentDateTime.toSecsSinceEpoch())),
                                      QLatin1String(assetshare::extension())),
        assetshare::fileFilter()
    );

    if (filePath.isEmpty() || filePath.isNull()) return;

    // THE VERB'S STAGE (node.exportArchive stages the same way) and the same
    // worker job, behind the progress dialog (EXPORT-THREAD-1).
    const auto result = bundleexportdialog::run(
        this, sceneEditService->stageNodeExport(node, modelType), filePath, tr("Export"));
    if (result.canceled) return;
    if (!result.ok()) {
        // TOLD, not only logged — this was a silent void (the project export's
        // shape, exportSceneAsZip).
        irisLog(QStringLiteral("Export failed: %1").arg(result.error));
        if (!FirstRun::isDrivenSession())
            QMessageBox::warning(this, tr("Export failed"),
                                 tr("%1 could not be exported: %2").arg(node->getName(), result.error));
    }
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

void MainWindow::updateCurrentSceneThumbnail()
{
    projectService->updateCurrentSceneThumbnail();
}

void MainWindow::exportSceneAsZip()
{
    if (!projectService->isSceneOpen() || project->getProjectGuid().isEmpty()) return;
    exportProjectWithDialog(project->getProjectGuid(), project->getProjectName());
}

void MainWindow::exportProjectWithDialog(const QString &guid, const QString &name)
{
    // get the export file path from a save dialog
    auto filePath = QFileDialog::getSaveFileName(
                        this,
                        "Choose export path",
                        QString("%1_export").arg(name),
                        "Supported Export Formats (*.zip)"
                    );

    if (filePath.isEmpty() || filePath.isNull()) return;
    if (!filePath.endsWith(".zip")) filePath += ".zip";
    QString why;
    if (!startProjectExport(guid, filePath, &why))
        QMessageBox::information(this, tr("Export"), why, QMessageBox::Ok);
}

bool MainWindow::startProjectExport(const QString &guid, const QString &zipPath, QString *why)
{
    const auto refuse = [why](const QString &reason) {
        if (why) *why = reason;
        return false;
    };
    if (archiver && archiver->isRunning())
        return refuse(tr("An archive operation is already running."));
    if (guid.isEmpty() || !db->fetchProjectTile(guid, nullptr))
        return refuse(tr("No project with guid '%1'.").arg(guid));

    // THE OPEN WORLD IS SAVED ONLY WHEN IT IS THE ONE BEING EXPORTED (CREATE-
    // GAP-1's fix round). The tile's Export used to re-point the LIVE project
    // at the exported tile and then save "the scene" — the open world, written
    // into the exported project's row and folder — and the pointer stayed
    // there, so every later autosave of the open world landed in that row too.
    const bool exportingOpenWorld = scene && projectService->isSceneOpen()
                                    && guid == project->getProjectGuid();
    if (exportingOpenWorld) saveScene();

    if (!archiver) {
        // Parented: it dies with this window (step 5 of the shutdown order),
        // and shutdownBackgroundWork cancels + joins it before that. It exports
        // `exportTarget` — a Project naming the row — never the live project.
        exportTarget = std::make_unique<Project>();
        archiver = new ProjectArchiver(db, exportTarget.get(), this);
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
            if (!canceled && !archiver->result().ok() && !FirstRun::isDrivenSession())
                QMessageBox::warning(this, tr("Export failed"),
                                     archiver->result().error, QMessageBox::Ok);
        });
    }
    exportTarget->setProjectPath(projectService->projectFolderFor(guid), QString());
    exportTarget->setProjectGuid(guid);

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
    // archiver only ever sees the database (services/sceneextents.h). Only
    // the OPEN world has a live document; another project's archive carries
    // no scale block rather than the open world's.
    archiver->setSceneMetadata(exportingOpenWorld && sceneView
                                   ? sceneextents::describe(sceneView->getScene(),
                                                            sceneView->editorCamera())
                                   : exportformat::ManifestScene());
    if (!archiver->startExport(zipPath))
        return refuse(archiver->result().error);
    return true;
}



QFont MainWindow::headerGlyphFont() const
{
	// 28px of the icon font — the size Help and Preferences always had, now
	// the size all three header glyphs share.
	return fontIcons->font(28);
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
        left = docks->hierarchyDock();
        right = docks->propertiesDock();
    } else if (pageHost) {
        if (auto *page = dynamic_cast<ColumnedPage *>(pageHost->currentPage())) {
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


// ---------------------------------------------------------------------------
// THE BOTTOM TRAY'S TABS (smoke S1). One place decides what "the console is
// showing" means, and both the Ctrl+` chord and the editor.tray verb come
// through it.













void MainWindow::setupViewPort()
{

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
		// `!currentSpace == WindowSpaces::DESKTOP` stood here. It parses as
		// "(!currentSpace) == DESKTOP" — and because DESKTOP is 0 that
		// accidentally evaluated exactly like the `!=` below, so the BEHAVIOUR
		// was never wrong; it is written as what it means, and stops being one
		// renumbering of the enum away from being wrong (SMOKE-FIX-1's audit).
		if (currentSpace != WindowSpaces::DESKTOP) switchSpace(WindowSpaces::DESKTOP);
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

    // Ground plane (WORLD-MODEL-1): the editor's infinite matte ground, beside
    // the grid; per-scene persisted, default OFF (EditorData::showGroundPlane).
    groundPlaneCheckAction = new QAction(QIcon(), "Ground Plane");
    groundPlaneCheckAction->setObjectName(QStringLiteral("groundPlaneCheckAction"));
    groundPlaneCheckAction->setCheckable(true);
    connect(groundPlaneCheckAction, &QAction::toggled, this, [this](bool on) {
        if (sceneView) sceneView->setShowGroundPlane(on);
    });
    wireFramesMenu->addAction(groundPlaneCheckAction);

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
        SettingsManager::getDefaultManager()->get(settingkeys::showFps));
    connect(statsCheckAction, &QAction::toggled, this,
            [this](bool on) { setShowFrameStats(on); });
    wireFramesMenu->addAction(statsCheckAction);

    // THE ATOM VIEW (D0-ATOM-VIEW): the visibility buffer in false colour. The
    // rows call the scene's setAtomView — the path world.setAtomView takes — and
    // re-read it whenever the menu opens, so a script's change shows here too.
    {
        QMenu *atomMenu = wireFramesMenu->addMenu(tr("Atom View"));
        auto *atomGroup = new QActionGroup(atomMenu);
        atomGroup->setExclusive(true);
        const QStringList atomLabels = { tr("Off"), tr("Triangles"), tr("Levels"), tr("Buckets"),
                                         tr("Objects") };
        for (int mode = 0; mode < atomLabels.size(); ++mode) {
            QAction *action = atomMenu->addAction(atomLabels[mode]);
            action->setCheckable(true);
            action->setChecked(mode == 0);
            atomGroup->addAction(action);
            connect(action, &QAction::triggered, this, [this, mode]() { setAtomViewMode(mode); });
            atomViewActions.push_back(action);
        }
        // ...and DISABLES the painting rows where nothing could paint (the Low
        // tier's passthrough viewport, the split shut) — the verb refuses there too.
        // Off stays enabled: a view left on can always be switched off.
        connect(atomMenu, &QMenu::aboutToShow, this, [this]() {
            const int mode = atomViewMode();
            jahshaka::engine::Scene *es = sceneView ? sceneView->engineScene() : nullptr;
            const bool paintable = es && es->atomViewPaintable();
            for (int i = 0; i < atomViewActions.size(); ++i) {
                atomViewActions[i]->setChecked(i == mode);
                atomViewActions[i]->setEnabled(i == 0 || paintable);
            }
        });
    }

    // THE PHOTON VIEW (PHOTON-VIEW-1): the lighting's debug pictures, beside the
    // Atom view and in its shape — the rows call the scene's setPhotonView behind
    // its refusal (world.setPhotonView's path), re-read on open, and a row that
    // cannot paint here is disabled.
    {
        QMenu *photonMenu = wireFramesMenu->addMenu(tr("Photon View"));
        auto *photonGroup = new QActionGroup(photonMenu);
        photonGroup->setExclusive(true);
        const QStringList photonLabels = { tr("Off"), tr("Voxels"), tr("Probes"), tr("Cards"),
                                           tr("Screen Probes"), tr("Diffuse GI Only"),
                                           tr("Reflections Only"), tr("Ray Hits") };
        for (int mode = 0; mode < photonLabels.size(); ++mode) {
            QAction *action = photonMenu->addAction(photonLabels[mode]);
            action->setCheckable(true);
            action->setChecked(mode == 0);
            photonGroup->addAction(action);
            connect(action, &QAction::triggered, this, [this, mode]() { setPhotonViewMode(mode); });
            photonViewActions.push_back(action);
        }
        connect(photonMenu, &QMenu::aboutToShow, this, [this]() {
            const int mode = photonViewMode();
            jahshaka::engine::Scene *es = sceneView ? sceneView->engineScene() : nullptr;
            for (int i = 0; i < photonViewActions.size(); ++i) {
                photonViewActions[i]->setChecked(i == mode);
                const bool paints =
                    i == 0 || (es && es->photonViewRefusal(
                                         static_cast<jahshaka::engine::PhotonView>(i)).empty());
                photonViewActions[i]->setEnabled(paints);
            }
        });
    }

    // Qlementine: the checkable actions become Switch rows (and stay in sync
    // with their QActions); a bonus is the menu no longer closes per toggle.
    ThemeManager::switchifyMenuToggles(wireFramesMenu);

    wireFramesButton->setMenu(wireFramesMenu);
    wireFramesButton->setText("View Options ");
    wireFramesButton->setPopupMode(QToolButton::InstantPopup);

    // The projection toggle, Views ▾ and Camera ▾ — the editor camera's
    // controls, owned by the ViewController.
    const ViewController::CameraControls cameraControls = viewController->createCameraControls();

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

    controlBarLayout->setSpacing(8);
    controlBarLayout->addWidget(screenShotBtn);
	controlBarLayout->addWidget(cameraControls.projection);
    controlBarLayout->addWidget(wireFramesButton);
    controlBarLayout->addWidget(cameraControls.views);
    controlBarLayout->addWidget(cameraControls.cameras);
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
             std::initializer_list<QWidget *>{ screenShotBtn, cameraControls.projection,
                                               wireFramesButton, cameraControls.views,
                                               cameraControls.cameras,
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
            lifecycle->watchEngine(host.engine());
            // PANEL-AWARE PACING (fps audit F1): no more literal 16. The driver
            // derives its interval from the screen the window is on and from
            // the persisted pacing mode; the ViewController feeds it both and
            // keeps feeding it across screen and refresh-rate changes.
            viewController->startFramePacing(this, settings);
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
    viewController->setViewport(sceneView);
    ViewController::FullscreenChrome chrome;
    chrome.captureLayout = [this]() { docks->captureLayout(); };
    chrome.hide = [this]() { docks->hideForFullscreen(); };
    chrome.restore = [this]() { docks->restoreAfterFullscreen(); };
    viewController->setWindow(this, chrome);

	// The player page: PlayerWidget gets an EnginePlayerView (a second engine
	// Scene mirroring the same document), or none in headless runs.
	playerBackend = nullptr;
	if (EngineHost::instance().isRunning()) {
		auto &host = EngineHost::instance();
		playerBackend = createEnginePlayerView(host.engine(), host.driver(), viewPort);
		playerBackend->setEditorViewport(sceneView);
	}
	playerView = new PlayerWidget(viewPort, playerBackend);

    // ONE DOOR (STUDIO-CRUD-1 item 8): the menu's grid / light-wire / stats
    // checkmarks follow the viewport's state through overlaysChanged — so
    // editor.setOverlays, the shortcuts and a scene open move them exactly as
    // a click does — and the actions' toggled() call the path the verb calls.
    connect(sceneView->events(), &EditorViewportEvents::overlaysChanged,
            this, &MainWindow::syncOverlayChecks);
    syncOverlayChecks();
	physicsCheckAction->setChecked(sceneView->getShowDebugDrawFlags());
    // The persisted readout state reaches the viewport HERE, not when the menu
    // action was built: the View Options menu is constructed before sceneView
    // exists, so its initial setChecked found nothing to switch on.
    setShowFrameStats(SettingsManager::getDefaultManager()->get(settingkeys::showFps));

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

}

// THE ASSETS PAGE, BUILT ONCE, WHEN FIRST WANTED (D11-LIBRARY-SCALE §3.3). The
// page and its engine preview viewer (a third engine Scene) used to be built at
// BOOT, and the page read and decoded the whole library in its constructor — the
// +116-162 s a 10,000-asset library added to the boot. Now nothing of it exists
// until the space is entered (switchSpace) or a verb drives the page
// (assets.select / selected / preview): both call here BEFORE any show, so no
// widget is made inside a show walk (the create-crash rule).
AssetView *MainWindow::ensureAssetsPage()
{
	// Built once; never in a session without the shell's pages, and never
	// after teardown (the placeholder is gone with the stack by then).
	if (_assetView || !assetsPlaceholder || !pageHost) return _assetView;
	// The Assets page: AssetView gets the EngineAssetViewer made at boot (a
	// third engine Scene with its own preview document), or none in headless runs.
	_assetView = new AssetView(db, this, assetsPreviewViewer);
	_assetView->installEventFilter(this);
	_assetView->setServices(services);
	_assetView->setProject(project);
	// A pin made on the Assets page must show up in the editor's project
	// panel live (both can be open in one session) — the panel repopulates
	// from the pinned membership on every add.
	connect(_assetView, &AssetView::assetAddedToProject, this,
	        [this](const QString &) { docks->assetTray()->refresh(); });
	// THE PAGE -> MODULE SEAM (AVATAR_ASSET_SPEC §5.5): a page asks for an
	// asset to be opened in a module; the shell switches space and calls that
	// module's VERB. Neither side learns about the other.
	connect(_assetView, &AssetView::editAssetInModule, this, &MainWindow::openAssetInModule);
	// THE IMPORT DECISION (§8) — the same two handlers the project tray gets.
	connect(_assetView, &AssetView::reimportAssetRequested, this,
	        [this](const QString &guid) { openImportSettings(guid); });
	// The page takes the placeholder's place under the same id.
	pageHost->replacePage(spaces::id(WindowSpaces::ASSETS), _assetView);
	assetsPlaceholder = nullptr;
	return _assetView;
}

void MainWindow::setupDesktop()
{
	pmContainer = new ProjectManager(db, project, this);
	pmContainer->mainWindow = this;
	projectService->setProjectManager(pmContainer);
	// ...and the other half of that pairing: the desktop's New Scene button
	// creates through the SERVICE, not through a second copy of it (R1).
	pmContainer->setProjectService(projectService);
	// Preferences -> Desktop -> Slider Rows applies LIVE (re-audit F8): the
	// page's signal reaches the desktop through the same ProjectManager entry
	// point desktop.setSliderRows uses.
	if (prefsDialog) prefsDialog->wireDesktop(pmContainer);
	// A reimport changed the asset's bake, its size line and its thumbnail:
	// the library view and the project tray both re-read the row.
	// QUEUED: the signal is emitted from inside the dialog's accept(), and a
	// tile re-selection loads a preview — not something to start while the
	// dialog is still closing.
	connect(this, &MainWindow::assetReimported, this, [this](const QString &guid) {
		if (_assetView) _assetView->selectAsset(guid);
		if (docks->assetTray()) docks->assetTray()->refresh();
	}, Qt::QueuedConnection);

	// THE PAGES, BY ID (PageHost). The order they are added in means nothing.
	pageHost->addPage(spaces::id(WindowSpaces::DESKTOP), pmContainer);
	pageHost->addPage(spaces::id(WindowSpaces::EDITOR), viewPort);
	// THE ASSETS PAGE IS BUILT ON ITS FIRST SHOWING, NOT AT BOOT
	// (D11-LIBRARY-SCALE §3.3): a placeholder holds its id until
	// ensureAssetsPage() swaps the page in.
	assetsPlaceholder = new QWidget;
	pageHost->addPage(spaces::id(WindowSpaces::ASSETS), assetsPlaceholder);
	// ITS PREVIEW VIEWER IS STILL MADE HERE, AT BOOT — A WORKAROUND, NOT A FIX
	// (ASSETS-VISIT-DEATH-1). With the page AND this viewer both made on the
	// first Assets visit, ui.window_minimum's app died once on `app.space('assets')`
	// in a D11 gate; no text was captured (the suite does not print the app's
	// log). NOT REPRODUCED in 60 loaded runs on 2026-09-29 (the lazy order beside
	// the lane's own --engine-selftest load: 20 watchdog off, 20 validation layer
	// on, 20 the gate's configuration under a double load — 0 deaths, 0 cores,
	// 0 Xid). What this line moves to boot is the viewer WIDGET (its native
	// window) and its preview document: the engine View and Scene are made on
	// the first showing in either order (EngineAssetViewer::showEvent). Only the
	// page (what scaled with the library) waits for its first use.
	if (EngineHost::instance().isRunning()) {
		auto &host = EngineHost::instance();
		assetsPreviewViewer = createEngineAssetViewer(host.engine(), host.driver(), this);
		// EXPLICITLY HIDDEN until the page mounts it: a child of the window that
		// no page holds would be shown with the window and draw its View over
		// the Desktop (app.input_keys: "a View is still enabled on the Desktop
		// page"). The page's stacked layout shows it when it becomes current.
		if (assetsPreviewViewer) assetsPreviewViewer->asWidget()->hide();
	}
	// The Player's page is PlayerWidget, built in setupViewPort with its engine
	// backend.
	pageHost->addPage(spaces::id(WindowSpaces::PLAYER), playerView);

	// THE MODULES (StudioModule v2): built from the compiled-in list, given the
	// one context, and asked what they contribute — their pages land in the
	// PageHost under their ids, their rows and toolbar actions in the
	// ActionHost. The shell never names a module's class.
	StudioContext context;
	context.db = db;
	context.settings = settings;
	context.viewport = sceneView;
	context.engine = &EngineHost::instance();
	context.services = services;
	context.project = project;
	context.shellWidget = this;
	context.shell = shellView;
	moduleHub->setModules(moduleregistry::createAll());
	moduleHub->initialize(context);
	moduleHub->contribute(pageHost, actionHost);

	connect(pmContainer, SIGNAL(closeProject()), SLOT(closeProject()));
	connect(pmContainer, &ProjectManager::fileToCreate,
	        this, [this](const QString &guid, const QString &name, const QString &path,
	                     SceneTemplate kind) {
		newProject(guid, name, path, kind);
	});
	connect(pmContainer, &ProjectManager::exportProject, this, &MainWindow::exportProjectWithDialog);
}

void MainWindow::setupToolBar()
{

	QVariantMap options;
	options.insert("color", QColor(255, 255, 255));
	options.insert("color-active", QColor(255, 255, 255));
  
    toolBar = new QToolBar("Tool Bar");
	// Named: DockState's snapshot of `viewPort` matches toolbars by objectName.
	toolBar->setObjectName(QString::fromLatin1(DockState::kEditorToolBarName));
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

	// THE CAMERA SPEED (owner R15). ONE integer 1..32 for every way a person
	// moves through a scene — the RMB fly, the Player's free camera and a VR
	// wearer — shown on a compact toolbar button beside the two camera-mode
	// buttons it belongs with. The value lives in CameraSpeed (persisted as the
	// preference camera/speed) and the verb editor.cameraSpeed owns writing it;
	// the button, the popover's two controls and the scroll wheel are four
	// views of that one number, so they can never disagree.
	//
	// A BUTTON WITH A POPOVER, not a dropdown of fixed rungs: 32 menu entries
	// would be a scroll, and the owner asked for a slider with a number field
	// beside it. A QMenu carrying a QWidgetAction is the house's popover (the
	// theme already styles one — ThemeManager puts its Switch rows in one), and
	// it costs the toolbar less width than the ladder combo it replaces, which
	// the 1366 x 768 floor (ui.window_minimum) cares about.
	cameraSpeedButton = new QToolButton;
	cameraSpeedButton->setObjectName(QStringLiteral("cameraSpeedButton"));
	cameraSpeedButton->setToolTip("Camera Speed | One speed for the editor fly, the Player and "
	                              "VR: 1 to 32, where 10 is normal. Scroll the wheel while "
	                              "holding the right mouse button in the viewport (Shift steps "
	                              "by five).");
	cameraSpeedButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	cameraSpeedButton->setPopupMode(QToolButton::InstantPopup);
	cameraSpeedButton->setFocusPolicy(Qt::NoFocus);   // never steal the fly keys
	cameraSpeedButton->setAutoRaise(true);

	auto *speedMenu = new QMenu(cameraSpeedButton);
	auto *speedPanel = new QWidget(speedMenu);
	auto *speedRow = new QHBoxLayout(speedPanel);
	speedRow->setContentsMargins(10, 6, 10, 6);
	speedRow->setSpacing(8);
	cameraSpeedSlider = new QSlider(Qt::Horizontal, speedPanel);
	cameraSpeedSlider->setObjectName(QStringLiteral("cameraSpeedSlider"));
	cameraSpeedSlider->setRange(CameraSpeed::kMin, CameraSpeed::kMax);
	cameraSpeedSlider->setPageStep(5);
	cameraSpeedSlider->setMinimumWidth(180);
	cameraSpeedSpin = new QSpinBox(speedPanel);
	cameraSpeedSpin->setObjectName(QStringLiteral("cameraSpeedSpin"));
	cameraSpeedSpin->setRange(CameraSpeed::kMin, CameraSpeed::kMax);
	cameraSpeedSpin->setKeyboardTracking(false);   // 3 on the way to 30 is not a speed
	speedRow->addWidget(cameraSpeedSlider, 1);
	speedRow->addWidget(cameraSpeedSpin, 0);
	auto *speedAction = new QWidgetAction(speedMenu);
	speedAction->setDefaultWidget(speedPanel);
	speedMenu->addAction(speedAction);
	cameraSpeedButton->setMenu(speedMenu);

	// LIVE, both of them: dragging the slider moves the number and the camera
	// at the same time (the owner's ask), and each control writes through the
	// one setter so the other follows from syncCameraSpeedUi rather than from a
	// second copy of the value.
	connect(cameraSpeedSlider, &QSlider::valueChanged, this, [this](int n) {
		if (n == CameraSpeed::value()) return;
		CameraSpeed::setValue(n);
		syncCameraSpeedUi();
	});
	connect(cameraSpeedSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int n) {
		if (n == CameraSpeed::value()) return;
		CameraSpeed::setValue(n);
		syncCameraSpeedUi();
	});
	toolBar->addWidget(cameraSpeedButton);

	// THE BUTTON FOLLOWS THE DIAL BY CONSTRUCTION, not by every caller
	// remembering to say so (fix round item 1): the Player's wheel writes the
	// same one value from a page with no toolbar of its own, and used to leave
	// this button reading the old number until something else moved it — the
	// per-controller hook it called was assigned nowhere. Every writer — both
	// wheels, the popover, the verb — announces through here now.
	CameraSpeed::setOnChanged([this] { syncCameraSpeedUi(); });
	syncCameraSpeedUi();

	// ...AND THE POPOVER CLOSING IS A GESTURE ENDING: the store write is
	// deferred (CameraSpeed::flush's note) so a slider drag is not one durable
	// rewrite of jahsettings.ini per mouse-move, and this is where a drag with
	// this window's hand on it is over.
	connect(speedMenu, &QMenu::aboutToHide, this, [] { CameraSpeed::flush(); });
	connect(cameraSpeedSlider, &QSlider::sliderReleased, this, [] { CameraSpeed::flush(); });

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

    // THE VR SLOT: the VR module's toggle lands here, beside the camera
    // controls it belongs with (its contribution).
    actionHost->addToolbarSlot(toolBar, QStringLiteral("editor.vr"));

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
	// ALWAYS THERE (owner, 2026-09-18: "show it even with auto save, as I may
	// want a force save"). Its visibility used to be `!auto_save`, which — with
	// auto-save ON by default — meant the Save button was hidden on every
	// default install: the one control that lets somebody write the world down
	// AT THE MOMENT THEY CHOOSE was missing, and nothing said why. Auto-save
	// keeps its own behaviour; this is a force save on top of it.
	actionSaveScene->setVisible(true);
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

	// THE TOOLBAR'S END SLOT: contributions land here, after the shell's own
	// actions (the Claude assistant's chat button).
	actionHost->addToolbarSlot(toolBar, QStringLiteral("editor.end"));

	// The scroll wheel stepped the camera speed while the EDITOR's camera was
	// flying: show the new number over the viewport. The toolbar button needs
	// no telling — it follows the dial itself (CameraSpeed::setOnChanged,
	// installed above) — and this signal exists for the TOAST, which is
	// anchored to this viewport and therefore belongs to this gesture alone.
	connect(sceneView->events(), &EditorViewportEvents::cameraSpeedChanged, this, [this]() {
		showViewportToast("Camera Speed",
		                  QString("%1  (%2 u/s)")
		                      .arg(CameraSpeed::value())
		                      .arg(double(CameraSpeed::editorSpeed())));
	});
	
	connect(actionExport,		SIGNAL(triggered(bool)), SLOT(exportSceneAsZip()));
	connect(viewDocks, &QAction::triggered, docks, &EditorDocks::openToggleDialog);
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
    //
    // Every row goes through the ActionHost (shell/actionhost.h): it is defined
    // once, and its handler is SPACE-SCOPED — `editor` rows run only while the
    // editor is the space, and a module adds the handler for its own space to
    // the same row (the Materials page's Space and F). The registry itself was
    // made in the constructor, where the modules' rows could join it.
    ActionHost &actions = *actionHost;
    const QString editor = spaces::id(WindowSpaces::EDITOR);
    const QString player = spaces::id(WindowSpaces::PLAYER);
    const QString any;
    auto row = [&actions](const char *id, const char *label, const char *category,
                          const QKeySequence &keys, const QString &space,
                          const std::function<void()> &run) {
        Contributions::Shortcut r;
        r.id = QString::fromLatin1(id);
        r.label = QString::fromUtf8(label);
        r.category = QString::fromLatin1(category);
        r.keys = keys;
        r.space = space;
        r.run = run;
        actions.addRow(r);
    };
    auto fixed = [&actions](const char *id, const char *label, const char *category,
                            const char *text) {
        Contributions::FixedRow r;
        r.id = QString::fromLatin1(id);
        r.label = QString::fromUtf8(label);
        r.category = QString::fromLatin1(category);
        r.text = QString::fromUtf8(text);
        actions.addFixedRow(r);
    };

    // ---- tools (Unreal keys: W/E/R; T kept as the historical translate key.
    // While RMB is held these keys fly the camera — the viewport withholds
    // them from the shortcut system, see EngineSceneViewport::event) ----
    row("tool.translate", "Translate Tool", "Tools", QKeySequence(Qt::Key_W), editor,
            [this]() { translateGizmo(); });
    row("tool.translate.alt", "Translate Tool (alias)", "Tools", QKeySequence(Qt::Key_T), editor,
            [this]() { translateGizmo(); });
    row("tool.rotate", "Rotate Tool", "Tools", QKeySequence(Qt::Key_E), editor,
            [this]() { rotateGizmo(); });
    row("tool.scale", "Scale Tool", "Tools", QKeySequence(Qt::Key_R), editor,
            [this]() { scaleGizmo(); });
    // Space is page-scoped, exactly like Ctrl+Z: ONE registry claimant, routed
    // by the active space — the gizmo cycle here, the Materials module's node
    // search on its own space (its contribution to this row).
    row("tool.cycle", "Cycle Gizmo Mode / Node Search", "Tools", QKeySequence(Qt::Key_Space), editor,
        [this]() { cycleGizmoMode(); });

    // ---- camera ----
    // F is page-scoped like Space: ONE registry claimant (the graph view's own
    // QShortcut made it ambiguous on the Materials page — STUDIO-CRUD-1 item 7),
    // routed by the active space: the Materials module frames its graph.
    // (graph.resetZoom, H, is the Materials module's own row, listed after this
    // one — its contribution.)
    row("camera.focus", "Focus Selection / Frame Graph Nodes", "Camera",
        QKeySequence(Qt::Key_F), editor, [this]() { sceneView->focusOnSelection(); });
    row("view.orthographic", "Orthographic Projection", "Camera", QKeySequence(Qt::Key_O), any,
            [this]() { viewController->changeProjection(false); });
    row("view.perspective", "Perspective Projection", "Camera", QKeySequence(Qt::Key_P), any,
            [this]() { viewController->changeProjection(true); });
    // Canonical axis views (historical X/Y/Z keys, moved out of the arcball
    // controller's raw key handling so they are remappable, listed in
    // Preferences -> Shortcuts, and work in the free camera too). Ctrl+Z
    // stays undo — "back" gets Shift+Z instead.
    row("view.top", "Top View", "Camera", QKeySequence(Qt::Key_Y), editor,
            [this]() { viewController->applyCameraView("top"); });
    row("view.bottom", "Bottom View", "Camera", QKeySequence(Qt::CTRL | Qt::Key_Y), editor,
            [this]() { viewController->applyCameraView("bottom"); });
    row("view.left", "Left View", "Camera", QKeySequence(Qt::Key_X), editor,
            [this]() { viewController->applyCameraView("left"); });
    row("view.right", "Right View", "Camera", QKeySequence(Qt::CTRL | Qt::Key_X), editor,
            [this]() { viewController->applyCameraView("right"); });
    row("view.front", "Front View", "Camera", QKeySequence(Qt::Key_Z), editor,
            [this]() { viewController->applyCameraView("front"); });
    row("view.back", "Back View", "Camera", QKeySequence(Qt::SHIFT | Qt::Key_Z), editor,
            [this]() { viewController->applyCameraView("back"); });
    // The ARROW CLUSTER, not W/A/S/D (owner decision 2026-09-09): the editor's
    // fly moved off the letters so tool shortcuts can have them back. The
    // PLAYER still answers to both spellings — its rows are the Gameplay
    // section below, driven by the InputMap.
    fixed("camera.fly", "Fly Camera (free camera)", "Camera",
                 "RMB (hold) + Arrow keys + PageUp/PageDown \xc2\xb7 Shift: 3x");
    fixed("camera.wheel", "Zoom / Dolly", "Camera", "Mouse Wheel");
    // Held-modifier input, like the fly keys: listed read-only, never a
    // QShortcut. Alt ON the gizmo keeps its duplicate-while-dragging meaning
    // (snap.altdrag below) — the gizmo hit-test runs first.
    fixed("camera.orbit", "Orbit Around Selection", "Camera",
                 "Alt + LMB drag (off the gizmo)");

    // ---- view ----
    row("view.gameView", "Game View (hide editor helpers)", "View", QKeySequence(Qt::Key_G), editor,
            [this]() { sceneView->setGameView(!sceneView->isGameView()); });
    row("view.grid", "Toggle Ground Grid", "View", QKeySequence(), any,
            [this]() { if (sceneView) sceneView->setShowGrid(!sceneView->getShowGrid()); });
    // F3 — the games convention (Minecraft, idTech-adjacent), and the only free
    // F-key in this registry besides F11 (STATS_OVERLAY_SPEC D3). Category
    // "View" so it lands beside gameView/grid/fullscreen in the generated
    // Preferences page. Goes through the same verb path as the checkbox and
    // never a separate one — and persists, because a diagnostic you have to
    // switch on again after every restart is a diagnostic nobody uses.
    row("view.stats", "Show Frame Stats", "View", QKeySequence(Qt::Key_F3), any,
            [this]() { setShowFrameStats(!sceneView->getShowFps()); });
    // F6 — THE ATOM VIEW, cycled Off -> Triangles -> Levels -> Buckets -> Objects
    // -> Off (the View Options sub-menu picks one directly).
    row("view.atomView", "Cycle Atom View", "View", QKeySequence(Qt::Key_F6), any,
            [this]() { setAtomViewMode((atomViewMode() + 1) % 5); });
    // F7 — THE PHOTON VIEW, cycled Off -> Voxels -> ... -> Ray Hits -> Off through
    // the modes that can paint here (the View Options sub-menu picks one directly).
    row("view.photonView", "Cycle Photon View", "View", QKeySequence(Qt::Key_F7), any,
            [this]() {
                jahshaka::engine::Scene *es = sceneView ? sceneView->engineScene() : nullptr;
                if (!es) return;
                const int n = jahshaka::engine::kPhotonViewCount;
                int next = photonViewMode();
                for (int step = 0; step < n; ++step) {
                    next = (next + 1) % n;
                    if (next == 0 || es->photonViewRefusal(
                                         static_cast<jahshaka::engine::PhotonView>(next)).empty())
                        break;
                }
                setPhotonViewMode(next);
            });
    row("window.fullscreen", "Immersive Fullscreen", "View", QKeySequence(Qt::Key_F11), any,
            [this]() { viewController->toggleImmersiveFullscreen(); });
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
    row("perf.capture", "Capture Render Monitor Data (20 s)", "View",
        QKeySequence(Qt::CTRL | Qt::Key_F4), editor, [this]() {
                if (FrameMonitor::instance().isRecording()) { FrameMonitor::instance().stop(); return; }
                FrameMonitor::Request request;
                if (project) request.label = project->getProjectName();
                QString error;
                if (!FrameMonitor::instance().start(request, &error))
                    showViewportToast(tr("Render Monitor"), error);
            });

    // ---- playback (Space is the gizmo cycle now — Unreal PIE puts play on
    // Alt+P; the toolbar Play button is unchanged) ----
    row("play.toggle", "Play / Stop Scene", "Playback",
        QKeySequence(Qt::ALT | Qt::Key_P), editor, [this]() { onPlaySceneButton(); });
    actions.handle(QStringLiteral("play.toggle"), player, [this]() { playerView->onPlayScene(); });

    // F8 — EJECT (PLAY-SELECT-1, owner R13). Unreal's key, and free in this
    // registry (the only other F-keys here are F3 and F11). It hands the mouse
    // and the keyboard back to the editor WITHOUT stopping the run; pressed
    // again it gives them back to the run. Editor space only, and only while
    // something is playing — said out loud either way, because an eject that
    // changes nothing visible is indistinguishable from a dead key.
    //
    // ONE PATH with `editor.playEject` (SCRIPTING_SPEC §2.3): both this lambda
    // and the verb set the VIEWPORT's latch, which is the flag its event
    // handlers branch on.
    row("play.eject", "Eject (editor input during play)", "Playback",
        QKeySequence(Qt::Key_F8), editor, [this]() {
                if (!sceneView) return;
                if (!sceneView->isPlaying()) return;
                const bool ejected = !sceneView->playEjected();
                sceneView->setPlayEjected(ejected);
                showViewportToast(ejected ? tr("Ejected") : tr("Possessed"),
                                  ejected ? tr("The editor has the input; the scene keeps playing.")
                                          : tr("Input is back with the running scene."));
            });

    // ---- snapping (SnapSettings, EDITOR_SHORTCUTS_SPEC §4) ----
    row("snap.decrease", "Decrease Snap / Grid Size", "Snapping", QKeySequence(Qt::Key_BracketLeft),
        any, [this]() { stepSnapSize(-1); });
    row("snap.increase", "Increase Snap / Grid Size", "Snapping", QKeySequence(Qt::Key_BracketRight),
        any, [this]() { stepSnapSize(+1); });
    row("snap.floor", "Snap Selection To Floor", "Snapping", QKeySequence(Qt::Key_End), editor,
            [this]() { sceneView->snapSelectionToFloor(); });
    fixed("snap.relative", "Snap While Dragging", "Snapping", "Ctrl (hold)");
    fixed("snap.altdrag", "Duplicate While Dragging", "Snapping", "Alt + drag gizmo");
    fixed("snap.vertex", "Snap To Vertex", "Snapping", "V (hold) while moving");

    // ---- editing ----
    // Ctrl+Z/Ctrl+Shift+Z had been DEAD since the menubar went away: the .ui's
    // actionEditUndo/actionEditRedo carried the QKeySequence but were attached
    // to no widget, so the shortcut never fired (the toolbar buttons were the
    // only working trigger). Registered here like every other binding.
    // Redo is explicit Ctrl+Shift+Z — QKeySequence::Redo's Ctrl+Y alternate
    // would collide with view.bottom.
    // The ONE claimant for each chord — see undoActiveSpace() for why that
    // matters and which stack each space owns.
    row("edit.undo", "Undo", "Editing", QKeySequence(Qt::CTRL | Qt::Key_Z), any,
            [this]() { undoActiveSpace(); });
    row("edit.redo", "Redo", "Editing", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), any,
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
    //
    // The ACTIVE SPACE's edit target answers (ModuleHub::runEdit): the editor's
    // selection SET, the Materials graph, or nothing — deliberately NOT a
    // fallback, so a chord never acts on a selection the user cannot see.
    using Edit = ModuleHub::Edit;
    row("edit.delete", "Delete Selection", "Editing", QKeySequence(Qt::Key_Delete), any,
        [this]() { moduleHub->runEdit(currentSpaceId(), Edit::Delete); });
    row("edit.duplicate", "Duplicate Selection", "Editing", QKeySequence(Qt::CTRL | Qt::Key_D), any,
        [this]() { moduleHub->runEdit(currentSpaceId(), Edit::Duplicate); });
    row("edit.copy", "Copy Selection", "Editing", QKeySequence(Qt::CTRL | Qt::Key_C), any,
        [this]() { moduleHub->runEdit(currentSpaceId(), Edit::Copy); });
    // Ctrl+X. The third chord of the set, and the one that was missing: a
    // clipboard whose copy travels to another instance but whose CUT does not
    // exist is half a clipboard. Same single-claimant routing, same text-field
    // rule as the two above (a focused QLineEdit accepts the ShortcutOverride
    // for Cut before a WindowShortcut can fire).
    row("edit.cut", "Cut Selection", "Editing", QKeySequence(Qt::CTRL | Qt::Key_X), any,
        [this]() { moduleHub->runEdit(currentSpaceId(), Edit::Cut); });
    row("edit.paste", "Paste", "Editing", QKeySequence(Qt::CTRL | Qt::Key_V), any,
        [this]() { moduleHub->runEdit(currentSpaceId(), Edit::Paste); });
    // Ctrl+A (EDITOR_MULTISELECT_SPEC §8.7, decided 2026-09-09). Same
    // single-claimant routing as the four chords above — and the same TEXT
    // FIELD rule, made explicit rather than left to Qt: selectAllActiveSpace
    // hands the chord to a focused QLineEdit/QTextEdit/QPlainTextEdit/spin box
    // instead of the scene, so Ctrl+A in the console input, an inline rename or
    // a transform field selects THAT text. Qt's own ShortcutOverride usually
    // gets there first (QWidgetLineControl accepts QKeySequence::SelectAll),
    // but "usually" is not a contract to hang the scene selection on.
    row("edit.selectAll", "Select All", "Editing", QKeySequence(Qt::CTRL | Qt::Key_A), any,
            [this]() { selectAllActiveSpace(); });

    // ---- file / windows ----
    row("file.save", "Save Scene", "File", QKeySequence(Qt::CTRL | Qt::Key_S), any,
            [this]() { saveScene(); });
    // Ctrl+` = the Console TAB of the bottom tray (smoke S1). One function for
    // the chord and for `editor.tray`, so the verb the suites drive is the code
    // path the key takes: show the tab, raise the tray, AND put the keyboard in
    // the input line (Ctrl+` used to open a console that still needed a mouse
    // click before it would take a character, which also meant the chord rules
    // the console is the natural place to exercise — Ctrl+A belongs to a
    // focused text field — could not be reached from the keyboard at all).
    row("console.toggle", "Script Console", "Windows",
            QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft), any,
            [this]() { docks->toggleScriptConsole(); });
    // (claude.toggle, Ctrl+Shift+C, is the Claude assistant's own row, listed
    // after this one — its contribution.)
    // THE RIGHT COLUMN'S TWO TABS (PROPERTY_FILTER_SPEC D2): one toggle, not two
    // keys. Ctrl+Tab is taken by space.previous, so Ctrl+Shift+P — verified
    // free against the 50 rows already registered here, and remappable in
    // Preferences → Shortcuts like every other row. (ShortcutRegistry's
    // conflict check runs on a USER rebinding, not on these defaults: two
    // defaults claiming one chord would simply both be registered, so the
    // default above was checked by hand.)
    row("properties.tab", "Properties: World / Selection Tab", "Windows",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), any, [this]() { docks->togglePropertiesTab(); });
    // THE PROPERTY FILTER'S BOX (PROPERTY_FILTER_SPEC D1): Ctrl+F, which is the
    // universal find key and was free in the registry — the only "Ctrl+F" in
    // src/ is the Ctrl+F4 render-capture tooltip, and plain F (camera.focus) is
    // a different chord. It focuses the box of the tab ON SCREEN, since each
    // tab has its own filter. (A QLineEdit accepts the ShortcutOverride for
    // unmodified printable keys, so typing "f" into the box does not fire
    // camera.focus.)
    row("properties.filter", "Properties: Filter Rows", "Windows",
            QKeySequence(Qt::CTRL | Qt::Key_F), any, [this]() { docks->focusPropertiesFilter(); });
    // Esc is a widget-level key inside the box, not a registry binding — the
    // row exists so the Preferences table says so.
    fixed("properties.filter.clear", "Properties: Clear the Filter", "Windows",
                 "Esc (while the filter box has focus)");
    // (vr.toggle, Ctrl+Shift+V, is the VR module's own row, listed after this
    // one — its contribution.)
    row("space.desktop", "Desktop Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_1), any,
            [this]() { this->switchSpace(WindowSpaces::DESKTOP); });
    row("space.player", "Player Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_2), any,
            [this]() { if (projectService->isSceneOpen()) this->switchSpace(WindowSpaces::PLAYER); });
    row("space.editor", "Editor Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_3), any,
            [this]() { if (projectService->isSceneOpen()) this->switchSpace(WindowSpaces::EDITOR); });
    row("space.effects", "Effects Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_4), any,
            [this]() { this->switchSpace(WindowSpaces::EFFECT); });
    row("space.assets", "Assets Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_5), any,
            [this]() { this->switchSpace(WindowSpaces::ASSETS); });
    row("space.previous", "Previous Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_Tab), any,
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
    fixed("gameplay.move",   "Move (play mode)",   "Gameplay", "W / S / A / D");
    fixed("gameplay.look",   "Look (play mode)",   "Gameplay", "Mouse");
    fixed("gameplay.jump",   "Jump (play mode)",   "Gameplay", "Space");
    fixed("gameplay.sprint", "Sprint (play mode)", "Gameplay", "Shift");
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
// "you just changed this with a gesture" message (snap size, fly speed).
void MainWindow::showViewportToast(const QString &title, const QString &text)
{
    if (!sceneView) return;
    if (!snapToast) snapToast = new Toast(this);
    // Top-centre of the VIEWPORT, through the widget itself (audit F-D4).
    snapToast->setAnchor(Toast::Anchor::WidgetTop, sceneView->asWidget());
    snapToast->showToast(title, text);   // auto-hides
}

// THE SPEED BUTTON AND ITS POPOVER follow CameraSpeed, never the other way
// round (API-first: editor.cameraSpeed is the verb, these are views of its
// value). Reached from four directions — the slider, the number field, the
// scroll wheel while flying (EditorViewportEvents::cameraSpeedChanged) and the
// verb (invoked by name) — so both signals are blocked while the controls are
// written or the first two would fight.
void MainWindow::syncCameraSpeedUi()
{
    const int n = CameraSpeed::value();
    if (cameraSpeedButton) cameraSpeedButton->setText(QString::number(n));
    if (cameraSpeedSlider) {
        QSignalBlocker blocked(cameraSpeedSlider);
        cameraSpeedSlider->setValue(n);
    }
    if (cameraSpeedSpin) {
        QSignalBlocker blocked(cameraSpeedSpin);
        cameraSpeedSpin->setValue(n);
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

	// SAVE IS ALWAYS OFFERED, AND ENABLED WHENEVER THERE IS A WORLD TO SAVE
	// (owner, 2026-09-18). It used to be hidden whenever `auto_save` was on —
	// which is the default — so the force save the owner wanted did not exist
	// on a stock install. (That read was ALSO of an uninitialised copy of the
	// preference on the Preferences page, SMOKE-FIX-1's audit; both are gone:
	// the auto-save reads the stored `auto_save` where it acts, and this button
	// asks no preference at all.)
	actionSaveScene->setVisible(true);
	actionSaveScene->setEnabled(projectService->isSceneOpen() || !!scene);
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
// decision is that on the Materials page the GRAPH stack is the one it drives —
// the Materials module's edit target.
//
// Deliberately not a fallback: with the Materials space active, Ctrl+Z with an
// empty graph stack does NOTHING rather than quietly undoing a scene edit the
// user cannot see.
//
// AND A PAGE WITH NO DOCUMENT UNDOES NOTHING (D10; audit S4a). The rule above
// was the Materials page's only: on the Desktop, Assets, Avatar, Publish and
// Player pages Ctrl+Z fell through to the editor's stack and undid the scene
// invisibly. The stacks are a QUndoGroup now (ModuleHub): the ACTIVE stack is
// the one the active space's edit target names — the editor's scene stack, the
// Materials page's open tab — and a space with none has no active stack, so the
// chord moves nothing.

void MainWindow::undoActiveSpace()
{
    moduleHub->undo(currentSpaceId());
}

void MainWindow::redoActiveSpace()
{
    moduleHub->redo(currentSpaceId());
}

// THE EDITOR'S EDIT TARGET (EDITOR_MULTISELECT_SPEC §2.6): the four chords act
// on the selection SET, the clipboard is the one system clipboard. The hub asks
// for it each time a chord fires on the editor space; on any other space the
// space's own target (or none) answers instead — never this one.
EditTarget MainWindow::editorEditTarget()
{
    EditTarget t;
    // The scene's stack, moved through UndoService — the edit gate, the end of
    // a live material preview and the panel's repaint ride every undo — and the
    // title follows.
    t.undoStack = undoStack;
    t.undo = [this]() { undo(); updateWindowTitle(); };
    t.redo = [this]() { redo(); updateWindowTitle(); };
    t.deleteSelection = [this]() { deleteNode(); };
    t.duplicateSelection = [this]() { duplicateNode(); };
    t.copySelection = [this]() { copyEditorSelection(); };
    t.cutSelection = [this]() { cutEditorSelection(); };
    t.paste = [this]() { pasteIntoEditor(); };
    t.selectAll = [this]() {
        if (services && services->sceneEdit) services->sceneEdit->selectAll();
    };
    return t;
}

void MainWindow::copyEditorSelection()
{
    // ONE clipboard now (CLIPBOARD_SPEC D3 b): the editor writes the same
    // system clipboard the Materials graph does, as a self-identifying text
    // payload, so a copy crosses to a second instance and back. The Materials
    // space keeps its own payload shape for one release (§2.2 `graph` items are
    // P2) — its own edit target, not a second clipboard.
    if (!services || !services->clipboard || !services->selection) return;
    const auto result = services->clipboard->copyNodes(services->selection->selectedSet());
    if (result.ok())
        showViewportToast(tr("Copy"), tr("%1 object(s) copied").arg(result.items));
}

void MainWindow::cutEditorSelection()
{
    if (!services || !services->clipboard || !services->selection) return;
    const auto result = services->clipboard->cutNodes(services->selection->selectedSet());
    if (result.ok())
        showViewportToast(tr("Cut"), tr("%1 object(s) cut").arg(result.copy.items));
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
    // The node graph has no select-all of its own yet; its target leaves the
    // chord unanswered rather than falling back to the scene.
    moduleHub->runEdit(currentSpaceId(), ModuleHub::Edit::SelectAll);
}

void MainWindow::pasteIntoEditor()
{
    if (!services || !services->clipboard) return;

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

int MainWindow::atomViewMode()
{
    jahshaka::engine::Scene *es = sceneView ? sceneView->engineScene() : nullptr;
    return es ? int(es->atomView()) : 0;
}

void MainWindow::setAtomViewMode(int mode)
{
    jahshaka::engine::Scene *es = sceneView ? sceneView->engineScene() : nullptr;
    if (!es || mode < 0 || mode > 4) return;
    if (mode != 0 && !es->atomViewPaintable()) return;   // world.setAtomView's refusal
    es->setAtomView(static_cast<jahshaka::engine::AtomView>(mode));
    for (int i = 0; i < atomViewActions.size(); ++i) atomViewActions[i]->setChecked(i == mode);
}

int MainWindow::photonViewMode()
{
    jahshaka::engine::Scene *es = sceneView ? sceneView->engineScene() : nullptr;
    return es ? int(es->photonView()) : 0;
}

void MainWindow::setPhotonViewMode(int mode)
{
    jahshaka::engine::Scene *es = sceneView ? sceneView->engineScene() : nullptr;
    if (!es || mode < 0 || mode >= jahshaka::engine::kPhotonViewCount) return;
    const auto view = static_cast<jahshaka::engine::PhotonView>(mode);
    if (!es->photonViewRefusal(view).empty()) return;   // world.setPhotonView's refusal
    es->setPhotonView(view);
    for (int i = 0; i < photonViewActions.size(); ++i) photonViewActions[i]->setChecked(i == mode);
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

void MainWindow::syncOverlayChecks()
{
    if (!sceneView) return;
    // Not signal-blocked: the World panel's Show Grid row follows the grid
    // action's toggled(), and the round trip ends at the viewport's setter,
    // which ignores a value it already holds.
    if (gridCheckAction) gridCheckAction->setChecked(sceneView->getShowGrid());
    if (groundPlaneCheckAction) groundPlaneCheckAction->setChecked(sceneView->getShowGroundPlane());
    if (wireCheckAction) wireCheckAction->setChecked(sceneView->getShowLightWires());
    // The stats action's toggled() persists show_fps (setShowFrameStats), so it
    // is blocked: the state it follows was written by whoever moved it.
    if (statsCheckAction && statsCheckAction->isChecked() != sceneView->getShowFps()) {
        QSignalBlocker block(statsCheckAction);
        statsCheckAction->setChecked(sceneView->getShowFps());
    }
}

QVariantMap MainWindow::viewOptionChecks() const
{
    QVariantMap out;
    if (gridCheckAction) out[QStringLiteral("grid")] = gridCheckAction->isChecked();
    if (groundPlaneCheckAction)
        out[QStringLiteral("groundPlane")] = groundPlaneCheckAction->isChecked();
    if (wireCheckAction) out[QStringLiteral("lightWires")] = wireCheckAction->isChecked();
    if (statsCheckAction) out[QStringLiteral("stats")] = statsCheckAction->isChecked();
    if (physicsCheckAction) out[QStringLiteral("physicsDebug")] = physicsCheckAction->isChecked();
    return out;
}




void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && viewController)
        viewController->windowStateChanged();
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
    docks->applyVisibility();
    playerControls->setVisible(true);
}


















// A BRAND-NEW SCENE STARTS AT THE DEFAULTS (owner report 2026-09-07). Opening a
// scene pushes its saved EditorData into the viewport and the View menu
// (openStage's `editorData` branch); creating one pushed NOTHING, so a new
// scene silently inherited the last opened scene's helper state — the grid in
// particular, which is why "new scene has the grid on" and "loaded scene does
// not" could both be true in one session. A default-constructed EditorData IS
// the statement of what a new scene looks like; applying it here is the same
// operation the open path performs, with the same three settings.
void MainWindow::newScene(SceneTemplate kind)
{
    auto scene = this->createDefaultScene(kind);
    this->setScene(scene);
    this->sceneView->resetEditorCam();
    resetOverlaysToDefaults();
}

// A BRAND-NEW SCENE STARTS AT THE DEFAULTS (owner report 2026-09-07) — the
// three overlays the open path pushes out of the saved EditorData. ONE body for
// newScene and the create run (it was two copies). The grid and light-wire
// checkmarks follow through overlaysChanged (syncOverlayChecks).
void MainWindow::resetOverlaysToDefaults()
{
    const EditorData defaults;
    sceneView->setShowGrid(defaults.showGrid);
    sceneView->setShowGroundPlane(defaults.showGroundPlane);
    sceneView->setShowLightWires(defaults.showLightWires);
    sceneView->setShowDebugDrawFlags(defaults.showDebugDrawFlags);
    if (physicsCheckAction) physicsCheckAction->setChecked(defaults.showDebugDrawFlags);
}

// THE SCRIPTED BOOT TAKES THE PRODUCT ROUTE (VIEWS-XID-1). It used to be a
// second way onto the editor page — the stacked index set directly, newScene(),
// sceneView->begin() — and the old pool runner used it to RE-SHOW the page
// after an arm had a camera PiP up: the Xid 13 "3D WIDTH ZT" device loss of
// 2026-09-27 (spikes/views-xid-1/). Now it is the scene a File > New makes and
// the one editor entry a user's switch and a load's reveal both run.
bool MainWindow::enterEditorOnNewScene(QString &why)
{
    if (!EngineHost::instance().isRunning()) {
        why = "the engine is not running (engine failed to start?)";
        return false;
    }
    newScene();
    if (!enterEditorSpace()) {
        why = spaceRefusal.isEmpty() ? QStringLiteral("the editor page refused to start")
                                     : spaceRefusal;
        return false;
    }
    if (!sceneView->isInitialized()) {
        why = "the engine viewport has no view after being shown";
        return false;
    }
    return true;
}

void MainWindow::leaveEditorSpace()
{
    // THE LAYOUT THE EDITOR HAD, taken before the next space hides its docks
    // (lane SPACE-1). Everything after this is a page that shows no panels, and
    // saving THAT at exit is what left the owner with an editor that opened
    // empty.
    docks->captureLayout();
    sceneView->end();
}

// A CREATE IS AN OPEN OF A WORLD NOBODY WROTE DOWN YET (SPECS/OPEN_COVER_SPEC.md
// §2 C, lane OPEN-COVER-2a).
//
// This was ONE synchronous function, and it did every one of the open path's
// stages back to back on the UI thread: the page switch (which presents the
// NoScene cover inline and pays the panels' first paint), the document, the
// initial save, the asset tray, the undo clear. Measured on a quiet box it
// blocked for 244-451 ms and then handed the FIRST DRIVER FRAME the whole GI
// arm — a second block of 298-443 ms — because a create never went anywhere
// near `beginSceneLoad` and so never told the engine a world was arriving.
//
// So it runs through the SAME runner the open uses, in the same order, one
// slice per event-loop turn with a frame at every boundary. Two things follow
// for free and both are the point: the window answers between the slices, and
// the engine is told a world is on its way (`openStageBegin`'s
// `beginSceneLoad`), which is what puts the arm on the streaming path instead
// of into the first frame the user sees.
//
// THE PAGE SWITCH MOVED TO THE END, with the open's reveal. It used to be first
// — "to ensure the editor's context is created" — and that ordering is what put
// the NoScene cover's inline presents and the panels' first paint inside the
// verb. The open path has always bound its scene with no View yet on the first
// world of a session (`primeSceneGeometry` says so and returns), and a create
// is no different.
//
// AND THE CONTRACT IS UNCHANGED FOR BOTH CALLERS: this still returns with the
// project open. The wait is `waitForOpen`, which PUMPS the event loop rather
// than blocking it (the same drain `project.open` has used since OPEN-ASSIMP-1),
// so `project.create` keeps its synchronous promise to scripts and the desktop's
// Create button keeps a window that answers. `project.createAsync` — a create
// that returns before the world is installed — is phase 2b.
void MainWindow::newProject(const QString &guid, const QString &filename,
                            const QString &projectPath, SceneTemplate kind)
{
    startCreateRun(guid, filename, projectPath, kind);
    waitForOpen();
}

// ...AND THE SAME CREATE WITHOUT THE WAIT (§2 C/§4, `project.createAsync`).
// The ONE difference is the drain: the slices are the same slices, queued on
// the same runner, and `isOpeningProject()` — which `project.openState()`
// reads — covers a create exactly as it covers an open. It exists because a
// caller that wants to WATCH a world arrive has to own the frames between the
// slices, and `newProject` spends them itself inside `waitForOpen`.
void MainWindow::newProjectAsync(const QString &guid, const QString &filename,
                                 const QString &projectPath, SceneTemplate kind)
{
    startCreateRun(guid, filename, projectPath, kind);
}

void MainWindow::startCreateRun(const QString &guid, const QString &filename,
                                const QString &projectPath, SceneTemplate kind)
{
    // AN OPEN IN FLIGHT FINISHES FIRST — before this create's ledger begins:
    // its run is the one LoadTimeline holds until the open ends it, so a begin
    // skipped because "a run is running" would leave the create's marks on no
    // run at all once the drain's end() closed the open's (fix round).
    if (isOpeningProject()) {
        qWarning("project create: an open was still in flight — draining it first");
        openRunner->waitForDone(kOpenWaitBudgetMs, kOpenWaitIdleMs);
    }
    // THE LEDGER COVERS THE CLOSE (CREATE-GAP-1). It began after it, so the
    // create's own record was the smaller half of the verb: 800-1200 ms of a
    // create over an open world — the autosave, the teardown, the page switch
    // — sat in front of the run and in no stage. `closePrevious` is that
    // span, and its counters (closePrevious:save / :teardown / :switch, and
    // the save's own saveOpen:*) say where it went.
    if (!LoadTimeline::isRunning())
        LoadTimeline::begin(QStringLiteral("create %1").arg(filename));
    if (projectService->isSceneOpen()) {
        LoadTimeline::mark(QStringLiteral("closePrevious"));
        closeProject();
    }
    // ...AND ONLY NOW IS THE CURRENT PROJECT THE NEW ONE: the close above
    // autosaved the old world into the old project's own row (createProject-
    // Shell used to re-point first, and the old project's edits went into the
    // new one's row — CREATE-GAP-1).
    projectService->pointAtProject(guid, filename);

    // The runner and its slice boundary are set up once, by whichever route
    // reaches them first, so there is ONE definition of what a boundary does.
    startOpenRunnerIfNeeded();

    QVector<SceneOpenRunner::Slice> slices;
    slices.append({ QStringLiteral("Preparing…"), 30, [this]() {
        // The cover, the teardown and the bake scope — the open's own first
        // slice, and the call that tells the engine a world is arriving.
        openStageBegin();
    } });
    slices.append({ QStringLiteral("Creating the scene…"), 45, [this, kind]() {
        LoadTimeline::mark(QStringLiteral("createDefaultScene"));
        openPendingScene = createDefaultScene(kind);
    } });
    slices.append({ QStringLiteral("Binding the scene…"), 60, [this]() {
        LoadTimeline::mark(QStringLiteral("setScene"));
        auto created = openPendingScene;
        openPendingScene.clear();
        projectService->setSceneOpen(true);
        ui->actionClose->setDisabled(false);
        setScene(created);
        sceneView->resetEditorCam();
        resetOverlaysToDefaults();   // a brand-new scene starts at the defaults
        assistant->refreshChatContext();   // D1: rebind an open chat to the new project
        moduleHub->projectChanged(project);   // the modules hear the new project
        if (services) services->announceSceneOpened();
    } });
    slices.append({ QStringLiteral("Building the asset panel…"), 75, [this]() {
        LoadTimeline::mark(QStringLiteral("assetWidget.trigger"));
        docks->assetTray()->trigger();
        undoService->clear();
        updateWindowTitle();
    } });
    slices.append({ QStringLiteral("Uploading geometry…"), 80, [this]() {
        LoadTimeline::mark(QStringLiteral("primeSceneSync"));
        sceneView->primeSceneGeometry();
    } });
    slices.append({ QStringLiteral("Lighting the world…"), 90, [this]() {
        LoadTimeline::mark(QStringLiteral("primeSceneEnvironment"));
        sceneView->primeSceneEnvironment();
    } });
    if (settings->get(settingkeys::shaderWarmupOnOpen)) {
        slices.append({ QStringLiteral("Precompiling shaders…"), 95, [this]() {
            LoadTimeline::mark(QStringLiteral("warmUpShaders"));
            const unsigned built = sceneView->warmUpShaders();
            if (built) qInfo("scene create: precompiled %u shader(s) behind the cover", built);
        } });
    }
    // The pass shape for the next launch's startup gate (SHADER_CACHE_AUDIT
    // F1b) — two settings values, written only on change. (This slice used to
    // RECORD the world's permutation set as well; the set named its materials
    // by a process-unique datablock name and warmed nothing in the next
    // process, so the machinery was deleted — WARMUPSET-2, 2026-09-21.)
    slices.append({ QStringLiteral("Precompiling shaders…"), 96,
                    [this]() { sceneView->rememberPassShape(); } });
    // THE INITIAL SAVE GOES LAST, AFTER THE WARM-UP, and the order is measured
    // rather than tidy. It renders the project's TILE — an offscreen view of
    // the new world — and on a cold shader cache that view's first frame
    // compiles its whole PSO set: 460 ms of the create's 891 when the save ran
    // before the warm-up, against 156 warm. Running it after `warmUpShaders`
    // lets it find those permutations already built.
    // Nothing downstream reads the row in between: the reveal below switches
    // the page, and the desktop re-reads the tile when it is next shown.
    //
    // ...AND THE TILE NEEDS A VIEW (fix round item 5). `ProjectService::
    // saveInitialScene` takes a headless branch when `viewport->isInitialized()`
    // is false, writing the row with NO tile and no editor data — and a View is
    // born in `EngineSceneViewport::showEvent`, because it needs the widget's
    // MAPPED NATIVE WINDOW (the engine's own startup order: a render window
    // before a scene manager). There is therefore no "create the editor context
    // without switching page": showing the page IS what births it, and the page
    // switch is the reveal below.
    //
    // MEASURED on the rig before this was written, and it does NOT fire today —
    // the editor viewport is shown once during startup, so the View exists from
    // boot and survives every close (`clearScene` keeps it deliberately): a
    // create from the desktop wrote a 149-179 KB tile and real editor data,
    // both as the session's first project and after a close. So this is a
    // guarantee, not a repair: the flag records whether the save really had a
    // viewport, and the reveal — which has just mapped the window — takes the
    // tile if it did not.
    auto hadViewport = std::make_shared<bool>(true);
    slices.append({ QStringLiteral("Saving the scene…"), 97,
                    [this, filename, projectPath, hadViewport]() {
        LoadTimeline::mark(QStringLiteral("saveInitialScene"));
        *hadViewport = sceneView->isInitialized();
        saveScene(filename, projectPath);
    } });
    slices.append({ QStringLiteral("Opening…"), 100,
                    [this, filename, projectPath, hadViewport]() {
        openStageReveal(false);
        if (!*hadViewport && sceneView->isInitialized()) {
            qInfo("scene create: the tile was taken after the reveal — the editor page "
                  "had never been shown, so the initial save had no viewport");
            saveScene(filename, projectPath);
        }
    } });

    openRunner->setPlan(QStringList(), slices,
                        filename.isEmpty() ? QStringLiteral("scene") : filename);
    openRunner->start();
}

MainWindow::~MainWindow()
{
    // STEPS 5-7 of the shutdown order (shell/shelllifecycle.h): the undo
    // drain, the modules, the services, the engine-holding widgets, and the
    // database last.
    lifecycle->teardownWindow();
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
		sceneView->stopPlayingScene();
    }
    else {
        enterPlayMode();
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

