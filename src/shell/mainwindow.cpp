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
#include "ui_mainwindow.h"


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
#include "services/services.h"
#include "services/framemonitor.h"
#include "services/perfsampler.h"

#include "bridge/enginehost.h"
#include "services/ambienceservice.h"
#include "viewport/enginerenderdriver.h"
#include "services/assethelper.h"

#include <qstandarditemmodel.h>
#include <QDockWidget>
#include <QMessageBox>
#include <QUndoStack>

#include <QApplication>
#include <QScreen>
#include <QFileDialog>


#include <QtConcurrent>
#include <atomic>
#include <math.h>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QAbstractSpinBox>

#include "ui/controls/tilecache.h"
#include "ui/pages/iassetviewer.h"

#include "ui/panels/timeline/animationwidget.h"

#include "data/project.h"

#include "viewport/editorcameracontroller.h"
#include "data/settingsmanager.h"
#include "ui/dialogs/preferencesdialog.h"
#include "ui/dialogs/aboutdialog.h"



#include "ui/pages/projectmanager.h"


#include "data/constants.h"


#include "viewport/editordata.h"
#include "ui/panels/assetwidget.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>


#include "ui/panels/scenehierarchywidget.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "ui/panels/propertywidgets/worldpropertywidget.h"



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
#include "shell/shellscripting.h"
#include "shell/shellheader.h"
#include "shell/editorpage.h"
#include "shell/editortoolbar.h"
#include "shell/shellactions.h"
#include "shell/shellservices.h"
#include "services/projectrunner.h"
#include "player/playerwidget.h"
#include "player/engineplayerview.h"
#include "viewport/headlesseditorviewport.h"

#include "scripting/scriptengine.h"
#include "scripting/claude/claudeassistant.h"

#include "services/shortcutregistry.h"
#include "services/worldmodes.h"
#include "services/testtier.h"
#include "viewport/snapsettings.h"
#include "viewport/cameraspeed.h"
#include "services/undoservice.h"
#include "services/selectionservice.h"
#include "services/playbackservice.h"
#include "services/projectservice.h"
#include "services/framepacing.h"
#include "services/outlinesettings.h"
#include "services/loadtimeline.h"
#include "services/meshbakestore.h"
#include "services/assetstorepaths.h"
#include "services/primitiveassets.h"
#include "services/sceneopenrunner.h"
#include "services/mainthreadwatchdog.h"
#include "services/apppaths.h"
#include "shell/shutdownorder.h"

#include "services/projectarchiver.h"
#include "ui/dialogs/progressdialog.h"
#include "app/firstrun.h"
#include "services/materialpresetseeder.h"
#include "services/materialpreviewservice.h"
#include "services/sceneeditservice.h"
#include "services/clipboardservice.h"
#include "services/thumbnailservice.h"
#include "services/defaultfloormaterial.h"
#include "services/scenetemplate.h"
#include "services/scenetemplatebuilder.h"
#include "irisgl/document/physics/physicsproperties.h"
#include "ui/style/stylesheet.h"
#include "ui/style/thememanager.h"
#include "ui/style/themeroles.h"
#include "ui/style/columnedpage.h"

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
	// keyboard action and toolbar slot goes through the action host;
	// the modules are driven through their hooks by the hub.
	lifecycle = new ShellLifecycle(this);
	db = lifecycle->openLibrary();
	pageHost = new PageHost(ui->stackedWidget, this);
	shortcutRegistry = new ShortcutRegistry(settings->settings, this);
	actionHost = new ActionHost(shortcutRegistry, this, [this]() { return currentSpaceId(); }, this);
	viewController = new ViewController(this);
	docks = new EditorDocks(this);
	header = new ShellHeader(this);
	page = new EditorPage(this);
	toolbar = new EditorToolbar(this);
	issueWatch = new SceneIssueWatch(this, [this]() { return currentSpace == WindowSpaces::EDITOR; }, this);
	moduleHub = new ModuleHub(this);
	// THE EDITOR'S EDIT TARGET: the editor page's chords on the selection SET
	// and the clipboard, and the scene's stack — moved through UndoService (the
	// edit gate, the end of a live material preview and the panel's repaint ride
	// every undo), the title following.
	moduleHub->setSpaceEditTarget(spaces::id(WindowSpaces::EDITOR), [this]() {
		EditTarget t = page->editTarget();
		t.undoStack = undoStack;
		t.undo = [this]() { undo(); updateWindowTitle(); };
		t.redo = [this]() { redo(); updateWindowTitle(); };
		return t;
	});
	shellView = std::make_unique<ShellView>(this);

    prefsDialog = new PreferencesDialog(nullptr, db, settings);
    aboutDialog = new AboutDialog();


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
	// THE OPEN, THE CREATE AND THE CLOSE (services/projectrunner.h): the order,
	// the slices and the drains; this window is its Host — the stage bodies.
	projects = new ProjectRunner(db, project, projectService, settings, sceneView, this, this);
	// THE EXPORT'S PROGRESS, AND ITS FAILURE SAID OUT LOUD — the dialog is the
	// window's; the archive is the runner's.
	connect(projects, &ProjectRunner::exportStarted, this, [this]() {
		if (!archiveProgress) {
			archiveProgress = new ProgressDialog(this);
			// SIGNAL-driven, never pumping: a pump from inside a slice re-enters
			// the loop and can destroy objects the slice is still using
			// (ProgressDialog::setPumpsEventLoop documents the scar).
			archiveProgress->setPumpsEventLoop(false);
			connect(archiveProgress, &ProgressDialog::canceled, projects, &ProjectRunner::cancelExport);
		}
		archiveProgress->setLabelText(tr("Exporting scene…"));
		archiveProgress->resetCancel();
		archiveProgress->setCancelVisible(true);
		archiveProgress->setValue(0);
		archiveProgress->show();
	});
	connect(projects, &ProjectRunner::exportProgress, this, [this](int percent, const QString &text) {
		if (archiveProgress) archiveProgress->setValueAndText(percent, text);
	});
	connect(projects, &ProjectRunner::exportFinished, this,
	        [this](bool canceled, bool ok, const QString &error) {
		if (archiveProgress) archiveProgress->close();
		if (!canceled && !ok && !FirstRun::isDrivenSession())
			QMessageBox::warning(this, tr("Export failed"), error, QMessageBox::Ok);
	});
    // THE EDITOR TOOLBAR (shell/editortoolbar.h), with its `editor.vr` and
    // `editor.end` slots for the contributions.
    EditorToolbar::Deps toolbarDeps;
    toolbarDeps.viewPort = viewPort;
    toolbarDeps.viewport = sceneView;
    toolbarDeps.icons = fontIcons;
    toolbarDeps.actions = actionHost;
    toolbarDeps.undoAction = [this](QObject *parent) { return moduleHub->createUndoAction(parent); };
    toolbarDeps.redoAction = [this](QObject *parent) { return moduleHub->createRedoAction(parent); };
    toolbarDeps.exportScene = [this]() { exportSceneAsZip(); };
    toolbarDeps.saveScene = [this]() { saveScene(); };
    toolbarDeps.toggleDocks = [this]() { docks->openToggleDialog(); };
    toolbarDeps.toast = [this](const QString &title, const QString &text) { showViewportToast(title, text); };
    toolbarDeps.editorActive = [this]() { return currentSpace == WindowSpaces::EDITOR; };
    // "Compiling shaders (N)" (ASYNC-SHADERS-1): the background compiler's pending count.
    toolbarDeps.pendingShaders = []() -> unsigned {
        auto eng = EngineHost::instance().engine();
        return eng ? eng->asyncShaderStats().pending : 0u;
    };
    toolbar->build(toolbarDeps);
    toolBar = toolbar->bar();
    actionSaveScene = toolbar->saveAction();
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
    dockDeps.gridAction = page->gridAction();
    dockDeps.groundPlaneAction = page->groundPlaneAction();
    dockDeps.editorOnScreen = [this]() {
        return pageHost->isCurrent(spaces::id(WindowSpaces::EDITOR))
               && !viewController->isImmersiveFullscreen();
    };
    dockDeps.editorActive = [this]() { return currentSpace == WindowSpaces::EDITOR; };
    docks->build(dockDeps);
    docks->setToolbar(toolBar);
    setupShortcuts();

	// THE SCRIPTING SURFACE (shell/shellscripting.h): the host the verbs see
	// the app through, the engine with every domain's and module's verbs, the
	// console (the bottom area's third tab) and the Claude assistant.
	ShellScripting::Deps scriptingDeps;
	scriptingDeps.shell = shellView.get();
	scriptingDeps.db = db;
	scriptingDeps.project = project;
	scriptingDeps.viewport = sceneView;
	scriptingDeps.undoStack = undoStack;
	scriptingDeps.services = services;
	scriptingDeps.playerBackend = playerBackend;
	scriptingDeps.settings = settings;
	scriptingDeps.prefs = prefsDialog;
	scriptingDeps.modules = moduleHub;
	scriptingDeps.window = this;
	scriptingDeps.windowToast = [this](const QString &title, const QString &text) {
		if (!snapToast) snapToast = new Toast(this);
		snapToast->setAnchor(Toast::Anchor::WindowBottom);
		snapToast->showToast(title, text);
	};
	scriptingDeps.refreshProperties = [this]() { refreshPropertiesFromDocument(); };
	scripting_ = new ShellScripting(scriptingDeps, this);
	scriptHost = scripting_->host();
	scriptEngine = scripting_->engine();
	scriptConsole = scripting_->console();
	assistant = scripting_->assistant();
	// THE CONSOLE IS THE BOTTOM AREA'S THIRD TAB (owner, 2026-09-14, lane
	// SPACE-2): its dock exists since the docks were built (a restored layout
	// that names it needs it), and the widget, which needs the engine, joins it.
	docks->setConsole(scriptConsole);
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

	// WHAT THE SHUTDOWN ORDER DRIVES (shell/shelllifecycle.h).
	ShellLifecycle::Parts parts;
	parts.window = this;
	parts.settings = settings;
	parts.modules = moduleHub;
	parts.scriptEngine = scriptEngine;
	parts.scriptHost = scriptHost;
	parts.services = serviceLayer;
	parts.undoStack = undoStack;
	parts.projects = projects;
	parts.docks = docks;
	parts.assistant = assistant;
	parts.assetsPage = [this]() { return _assetView; };
	parts.saveScene = [this]() { saveScene(); };
	parts.deleteUi = [this]() {
		// The plain services are gone (step 5): nothing reaches them after.
		services = nullptr;
		materialPreviewService = nullptr;
		projectService = nullptr;
		thumbnailService = nullptr;
		assetService = nullptr;
		undoService = nullptr;
		delete ui;
		ui = nullptr;
	};
	parts.forget = [this]() {
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
	// ShellLifecycle::stopBackgroundWork so a normal teardown is never photographed.
	MainThreadWatchdog::start();
}

void MainWindow::goToDesktop()
{
    show();
    switchSpace(WindowSpaces::DESKTOP, true);
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
    playbackService->enterEditMode();
    switchSpace(back, true);
    spaceRefusal = why;     // after the bounce: see bounceIfViewportIsDead
}


iris::ScenePtr MainWindow::getScene()
{
    return scene;
}

iris::ScenePtr MainWindow::createDefaultScene(SceneTemplate kind)
{
    // The template's document (services/scenetemplatebuilder.h); a new scene
    // opens on the World — its root is the selection.
    auto scene = scenetemplate::build(kind, db, project);
    sceneNodeSelected(scene->rootNode);
    return scene;
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


void MainWindow::closeEvent(QCloseEvent *event)
{
    // STEP 1 of the shutdown order — the whole sequence is ShellLifecycle's.
    lifecycle->closeRequested(event);
}

void MainWindow::setupFileMenu()
{
    connect(prefsDialog,            SIGNAL(PreferencesDialogClosed()), SLOT(updateSceneSettings()));
}



void MainWindow::setupServices()
{
    // THE SERVICE LAYER (shell/shellservices.h): constructed in dependency
    // order and hooked to each other there; what their signals do to this
    // window's widgets is wired here.
    ShellServices::Deps serviceDeps;
    serviceDeps.undoStack = undoStack;
    serviceDeps.db = db;
    serviceDeps.project = project;
    serviceDeps.settings = settings;
    serviceDeps.viewport = sceneView;
    serviceDeps.playerBackend = playerBackend;
    serviceDeps.scene = [this]() { return scene; };
    serviceLayer = new ShellServices(serviceDeps, this);
    services = serviceLayer->aggregate();
    undoService = serviceLayer->undo();
    selectionService = serviceLayer->selection();
    playbackService = serviceLayer->playback();
    playerService = serviceLayer->player();
    projectService = serviceLayer->project();
    sceneEditService = serviceLayer->sceneEdit();
    materialPreviewService = serviceLayer->materialPreview();
    clipboardService = serviceLayer->clipboard();
    thumbnailService = serviceLayer->thumbnails();
    assetService = serviceLayer->assets();
    // THE WORLD'S MUSIC FOLLOWS THE DOCUMENT EVERY TICK (audit D8), like the
    // mirror: the driver's beforeFrame fires on every tick, drawn or not, and a
    // still frame costs AmbienceService two compares. Undo of the music row,
    // the volume slider and the verb all land here without a hook of their own.
    if (EngineRenderDriver *driver = EngineHost::instance().driver()) {
        AmbienceService *ambience = serviceLayer->ambience();
        connect(driver, &EngineRenderDriver::beforeFrame, ambience,
                [this, ambience]() {
            ambience->sync(projectService && projectService->isSceneOpen() ? scene
                                                                           : iris::ScenePtr());
        });
    }

    connect(selectionService, &SelectionService::selectionChanged,
            docks, &EditorDocks::showSelection);
    // The SET fan-out (EDITOR_MULTISELECT_SPEC §2.1). Deliberately a second
    // connection and not a widened applySelectionToUi: the primary signal
    // drives the single-node panels (properties, timeline) and fires only when
    // the primary changes, while this one runs on every set change — a
    // Ctrl+click storm must repaint the tree and the outline without rebuilding
    // the properties panel N times (§3.3). Emitted AFTER selectionChanged, so
    // the set is what the tree ends up showing.
    connect(selectionService, &SelectionService::selectionSetChanged,
            docks, &EditorDocks::showSelectionSet);
    // The play-button chrome follows the play-in-place mode.
    connect(playbackService, &PlaybackService::editModeEntered,
            page, &EditorPage::applyEditModeUi);
    connect(playbackService, &PlaybackService::playModeEntered,
            page, &EditorPage::applyPlayModeUi);

    // SHOWING THE PLAYER PAGE is the one thing the Player service cannot do for
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

    // THE PANELS FOLLOW THE SERVICES (the edits' refreshes, the paste's import,
    // an undo's repaint) — the editor's docks' own wiring.
    docks->followServices(services);

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

    if (sceneView) { sceneView->setServices(services); sceneView->setProject(project); }
    page->setServices(services);
    if (prefsDialog) prefsDialog->wireEditor(sceneView, shellView.get());
}

void MainWindow::setupUndoRedo()
{
    // The scene's stack. It joins the hub's QUndoGroup as the editor space's
    // edit target (the ctor's setSpaceEditTarget); the chords are the
    // registry's edit.undo / edit.redo rows and the buttons the editor
    // toolbar's group-made actions — the .ui's four undo/redo actions, on no
    // widget since the menubar went, are deleted.
    undoStack = new QUndoStack(this);
}

WindowSpaces MainWindow::getWindowSpace()
{
	return currentSpace;
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

	// THE PROJECT'S TILE IS TAKEN BEFORE THE PAGE IT COMES FROM IS LEFT
	// (CLOSE-SHOT-2): it is the editor's PRESENTED frame, and leaving the editor
	// takes its view off screen — a hidden view has no frame to read
	// (EngineSceneViewport::settlePresentedFrame). From any other page the editor
	// is already hidden and the tile keeps the last editor frame, which is what
	// the user last saw of the world.
	if (space == WindowSpaces::DESKTOP && projectService->isSceneOpen() && sceneView->isInitialized())
		updateCurrentSceneThumbnail();

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
            page->playSceneButton()->hide();
            playbackService->enterPlayMode();
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
				page->playSceneButton()->hide();
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
			if (projectService->isSceneOpen()) page->playSceneButton()->hide();
			break;
		}

		case WindowSpaces::AVATAR: {
			pageHost->show(spaces::id(WindowSpaces::AVATAR), true);
			hideEditorPanels();
			toolBar->setVisible(false);
			if (projectService->isSceneOpen()) page->playSceneButton()->hide();
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
// does (ProjectRunner::close(reopenInPlace)), so the reveal calls this directly. Each call
// here is idempotent on a page that never left — the page index, the dock
// visibility (Qt returns early for a dock that is already visible), the edit
// mode, `sceneView->begin()` — and two of them are the point: the views label,
// which a scene open resets to perspective, and the cover/bounce check.
bool MainWindow::enterEditorSpace()
{
    pageHost->show(spaces::id(WindowSpaces::EDITOR));

	docks->applyVisibility();
	page->playerControls()->setVisible(false);

	docks->applyColumnWidthsOnce();

    page->playSceneButton()->show();
    playbackService->enterEditMode();
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
	header->updateStates(activeSpace, projectService->isSceneOpen());
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



















// (MainWindow::applyMaterialPreset is GONE, both overloads — MATERIAL-PREVIEW-1.
// It was a SECOND material dispatcher beside material.apply's, reached only by
// the viewport's drop and the tray's double-click, and the two had already
// drifted in what they accepted. Both callers ask
// SceneEditService::applyMaterial now, with the target explicit.)


// ---- the shell's half of a world arriving and leaving (ProjectRunner::Host) ---
//
// The runner (services/projectrunner.h) owns the ORDER, the slices, the budgets
// and the drains; these are the stage bodies only the window can run — its
// panels, its page, its toolbar.

void MainWindow::teardownWorld()
{
	if (!!scene) {
		playerView->endVrForSceneClose();   // the Player's session before its scene (see closeProject)
		removeScene();
	}

	updateWindowTitle();
}

void MainWindow::bindWorld(const iris::ScenePtr &scene, EditorData *editorData, bool playMode)
{
	playbackService->setPlayerMode(playMode);
	projectService->setSceneOpen(true);
	ui->actionClose->setDisabled(false);
	setScene(scene);
	// A MODEL WITH NO CURRENT BAKE IS MISSING, AND SAID SO (FORWARD-ONLY-1):
	// the reader never parses in its place. setScene cleared the issue store,
	// so this is raised after it.
	SceneIssues::instance().raiseMissingModels(projectService->missingModels());
	SceneIssues::instance().raiseMissingClips(projectService->missingClips());
	SceneIssues::instance().raiseCascadeSetClamped(projectService->cascadeRowsDropped());
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
		page->setPhysicsDebugChecked(editorData->showDebugDrawFlags);
	}

	// THE SCENE IS OPEN (AVATAR_ASSET_SPEC §4 D4, the load-time half). Fired
	// HERE and not when the reader returned: a subscriber's job is to walk the
	// scene that is now installed, and until setScene above it was not.
	if (services) services->announceSceneOpened();
}

void MainWindow::bindNewWorld(const iris::ScenePtr &created)
{
    projectService->setSceneOpen(true);
    ui->actionClose->setDisabled(false);
    setScene(created);
    sceneView->resetEditorCam();
    page->resetOverlaysToDefaults();   // a brand-new scene starts at the defaults
    assistant->refreshChatContext();   // D1: rebind an open chat to the new project
    moduleHub->projectChanged(project);   // the modules hear the new project
    if (services) services->announceSceneOpened();
}

iris::ScenePtr MainWindow::createWorld(SceneTemplate kind)
{
    return createDefaultScene(kind);
}

void MainWindow::buildPanels(bool fresh)
{
    docks->assetTray()->trigger();
    if (fresh) {
        // A create's world has no history to keep.
        undoService->clear();
        updateWindowTitle();
    } else {
        undoService->resetSavedCount();
    }
}

QStringList MainWindow::plannedSessionModelPaths()
{
    return pmContainer ? pmContainer->plannedSessionModelPaths() : QStringList();
}

QStringList MainWindow::sessionAssetGuids()
{
    return pmContainer ? pmContainer->sessionAssetGuids() : QStringList();
}

void MainWindow::registerSessionAssets(const iris::MeshPrewarmPtr &prewarm)
{
    if (pmContainer) pmContainer->registerProjectSessionAssets(prewarm);
}

void MainWindow::registerSessionAssetGuids(const QStringList &guids,
                                           const iris::MeshPrewarmPtr &prewarm)
{
    if (pmContainer) pmContainer->registerSessionAssetGuids(guids, prewarm);
}

void MainWindow::showOpenProgress(int percent, const QString &text)
{
    if (pmContainer) pmContainer->showOpenProgress(percent, text);
}

void MainWindow::showOpenCompileProgress(unsigned compiled)
{
    if (pmContainer) pmContainer->showOpenCompileProgress(compiled);
}

void MainWindow::hideOpenProgress()
{
    if (pmContainer) pmContainer->hideOpenProgress();
}

void MainWindow::saveOpenWorld()
{
    saveScene();
}

iris::ScenePtr MainWindow::openWorld() const
{
    return scene;
}

// The close a user asked for: the world goes and the window lands on the
// Desktop (VIEW-REBUILD-1 gave the other half of this function a name).
void MainWindow::closeProject()
{
    projects->close(false);
}

void MainWindow::openProjectAsync(bool playMode)
{
    projects->openAsync(playMode);
}

bool MainWindow::revealWorld(bool playMode)
{
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
	// latched at bind time (bindWorld) and playScene() would have started
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
	if (!sceneView->viewCreationError().isEmpty()) return false;
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
		page->showPlayerPlaying();
		playbackService->playScene();
		playerView->onPlayScene();
	}

	// force a refresh
	this->update();
	return true;
}

void MainWindow::prepareClose()
{
    // The borrowed material goes back before the scene it is borrowed from is
    // torn down (MATERIAL-PREVIEW-1).
    if (materialPreviewService) materialPreviewService->end();
}

// THE CLOSE'S TEARDOWN (the runner has already drained an open in flight).
void MainWindow::closeWorld(bool reopenInPlace)
{
    // A tile's close control can fire with no scene open (double-fired close,
    // or closing while an open never completed): every line below dereferences
    // `scene`, so the first one crashed on null (crash-1788555267.log,
    // the first line at offset 0x320 of a null Scene). Nothing open means
    // nothing to close.
    if (!scene) return;
    {
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

        page->resetSimulationButton();
    }

    projectService->setSceneOpen(false);
    // The world's music stops with the world (audit D8). The tick's sync reads
    // the scene only while one is OPEN, so nothing restarts it before the
    // scene pointer goes.
    if (services && services->ambience) services->ambience->stop();
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
    // removeScene() had exactly ONE caller — the open's teardown, the load-in-place
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
		header->disableSceneSpaces();
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
	if (reopenInPlace) return;

    LoadTimeline::Accumulate toDesktop(QStringLiteral("closePrevious:switch"));
    switchSpace(WindowSpaces::DESKTOP);

	if (sceneView->isInitialized())
		sceneView->end();
}

// THE PANELS' CALLS INTO THE WINDOW, answered by the editor's docks: the
// tray's favourite (the Presets panel of its kind), the edit gate's repaint and
// the outliner's Export.
void MainWindow::exportNode(const iris::SceneNodePtr &node, ModelTypes modelType)
{
    docks->exportNode(node, modelType);
}

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



void MainWindow::sceneNodeSelected(iris::SceneNodePtr sceneNode)
{
    selectionService->select(sceneNode);
}

iris::SceneNodePtr MainWindow::selectedSceneNode() const
{
    return selectionService->selected();
}
















/**
 * Adds sceneNode to selected scene node. If there is no selected scene node,
 * sceneNode is added to the root node
 * @param sceneNode
 */

/**
 * adds sceneNode directly to the scene's rootNode
 * applied default material to mesh if one isnt present
 * ignore set to false means we only add it visually, usually to discard it afterw
 */



// THE OUTLINER'S DELETE AND DUPLICATE, through the editor page's edit target.
void MainWindow::deleteNode()
{
    page->deleteSelection();
}

void MainWindow::duplicateNode()
{
    page->duplicateSelection();
}

void MainWindow::createMaterial()
{
    sceneEditService->createMaterialFromNode(selectionService->selected(),
                                             docks->assetTray()->assetItem.selectedGuid);
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
    if (!projects->startExport(guid, filePath, &why))
        QMessageBox::information(this, tr("Export"), why, QMessageBox::Ok);
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
	// THE HEADER BAND (shell/shellheader.h): the logo, the space buttons and
	// the Publish / Help / Preferences glyphs.
	header->build(ui->ohlayout, fontIcons,
	              [this](WindowSpaces space) { switchSpace(space); },
	              [this]() { return currentSpace; },
	              [this]() { showPreferences(); });

	// THE EDITOR PAGE (shell/editorpage.h): its nested window, the viewport bar,
	// the engine viewport (or the headless stand-in) and the Player's widget.
	EditorPage::Deps pageDeps;
	pageDeps.shell = this;
	pageDeps.icons = fontIcons;
	pageDeps.db = db;
	pageDeps.cameraControls = viewController->createCameraControls();
	pageDeps.actions = actionHost;
	pageDeps.engineStarted = [this](const std::shared_ptr<jahshaka::engine::Engine> &engine) {
		// Non-owning: step 6 of the shutdown order checks it died with the
		// viewports (shell/shelllifecycle.h).
		lifecycle->watchEngine(engine);
		viewController->startFramePacing(this, settings);
	};
	page->build(pageDeps);
	viewPort = page->viewPort();
	sceneView = page->viewport();
	playerView = page->player();
	playerBackend = page->playerBackend();
	viewController->setViewport(sceneView);
	ViewController::FullscreenChrome chrome;
	chrome.captureLayout = [this]() { docks->captureLayout(); };
	chrome.hide = [this]() { docks->hideForFullscreen(); };
	chrome.restore = [this]() { docks->restoreAfterFullscreen(); };
	viewController->setWindow(this, chrome);
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
	pmContainer->shell = shellView.get();
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
	context.shell = shellView.get();
	moduleHub->setModules(moduleregistry::createAll());
	moduleHub->initialize(context);
	moduleHub->contribute(pageHost, actionHost);

	connect(pmContainer, SIGNAL(closeProject()), SLOT(closeProject()));
	connect(pmContainer, &ProjectManager::fileToCreate,
	        this, [this](const QString &guid, const QString &name, const QString &path,
	                     SceneTemplate kind) {
		projects->create(guid, name, path, kind);
	});
	connect(pmContainer, &ProjectManager::exportProject, this, &MainWindow::exportProjectWithDialog);
}


void MainWindow::setupShortcuts()
{
    // THE SHELL'S OWN ROWS, in the Preferences order (shell/shellactions.cpp);
    // the modules' and the assistant's rows join them by their `after` anchors.
    shellactions::define(*actionHost, *this);
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

IShellView *MainWindow::view() const
{
    return shellView.get();
}

void MainWindow::undoActiveSpace()
{
    moduleHub->undo();
}

void MainWindow::redoActiveSpace()
{
    moduleHub->redo();
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














void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && viewController)
        viewController->windowStateChanged();
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
    page->playerControls()->setVisible(true);
}


















// A BRAND-NEW SCENE STARTS AT THE DEFAULTS (owner report 2026-09-07). Opening a
// scene pushes its saved EditorData into the viewport and the View menu
// (bindWorld's `editorData` branch); creating one pushed NOTHING, so a new
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
    page->resetOverlaysToDefaults();
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




MainWindow::~MainWindow()
{
    // STEPS 5-7 of the shutdown order (shell/shelllifecycle.h): the undo
    // drain, the modules, the services, the engine-holding widgets, and the
    // database last.
    lifecycle->teardownWindow();
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

