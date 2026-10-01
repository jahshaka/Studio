/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/shellscripting.h"

#include "bridge/enginehost.h"
#include "data/database/database.h"
#include "data/project.h"
#include "data/settingsmanager.h"
#include "player/engineplayerview.h"
#include "scripting/claude/claudeassistant.h"
#include "scripting/modules/studiomodules.h"
#include "scripting/scriptengine.h"
#include "scripting/scripthost.h"
#include "services/editgate.h"
#include "services/materialpreviewservice.h"
#include "services/projectservice.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "shell/modulehub.h"
#include "ui/dialogs/preferencesdialog.h"
#include "ui/panels/scriptconsole.h"
#include "viewport/enginerenderdriver.h"
#include "viewport/ieditorviewport.h"

ShellScripting::~ShellScripting()
{
	// The script engine, its ApiModules and the assistant hold the ScriptHost
	// by reference: they go first, then mHost (a member) after this body.
	qDeleteAll(findChildren<QObject *>(Qt::FindDirectChildrenOnly));
}

ShellScripting::ShellScripting(const Deps &deps, QObject *parent) : QObject(parent)
{
	// scripting (SCRIPTING_SPEC §2): the host sees the live app; the console
	// dock starts hidden — Ctrl+` toggles it in the editor space.
	mHost = std::make_unique<ScriptHost>();
	ScriptHost *scriptHost = mHost.get();
	scriptHost->shell = deps.shell;
	scriptHost->db = deps.db;
	scriptHost->project = deps.project;
	scriptHost->viewport = deps.viewport;
	scriptHost->undoStack = deps.undoStack;
	scriptHost->services = deps.services;
	ProjectService *projectService = deps.services->project;
	Project *project = deps.project;
	scriptHost->projectOpen = [projectService, project]() {
		return projectService->isSceneOpen() && !project->getProjectGuid().isEmpty();
	};
	IEditorViewport *sceneView = deps.viewport;
	EnginePlayerView *playerBackend = deps.playerBackend;
	scriptHost->engineReady = [sceneView, playerBackend]() {
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
	Database *db = deps.db;
	scriptHost->macroOpenChanged = [db](bool open) {
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
	MaterialPreviewService *materialPreviewService = deps.services->materialPreview;
	scriptHost->scriptRunState = [materialPreviewService](ScriptRunState state) {
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
	UndoService *undoService = deps.services->undo;
	scriptHost->beginUndoMacro = [undoService](const QString &text) { undoService->beginScriptMacro(text); };
	scriptHost->endUndoMacro = [undoService]() { undoService->endScriptMacro(); };
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
	auto windowToast = deps.windowToast;
	auto refreshProperties = deps.refreshProperties;
	editgate::setNoticeHook([windowToast, refreshProperties]() {
		if (windowToast)
			windowToast(tr("Script running"),
			            tr("The editor is read-only until the script finishes. "
			               "You can still look around, select and switch pages."));
		// AND THE CONTROL SNAPS BACK (round 2, item 3). A refused row keeps the
		// value the user dragged or typed while the document still holds the
		// old one — two different numbers on screen, and nothing to correct
		// them. Re-read the panel from the document so the row shows the truth
		// while the toast is up saying why. Deferred (refreshFromDocument
		// defers by itself; the transform rows do it here) because this is
		// reached from inside a control's own signal handler.
		if (refreshProperties) refreshProperties();
	});
	ScriptEngine *scriptEngine = new ScriptEngine(*scriptHost, this);
	mEngine = scriptEngine;
	// ...AND AGAIN WHEN THE RUN ENDS, for the rows that were refused later in
	// the run, after the one notice had already been raised.
	connect(scriptEngine, &ScriptEngine::runningChanged, this, [refreshProperties](bool running) {
		if (!running && refreshProperties) refreshProperties();
	});
	// LIVE SCRIPT FEEDBACK, as the user left it (Preferences > Scripting,
	// app.scriptPolicy). Live is the default: a person or an agent driving the
	// editor should see it work.
	scriptEngine->setInteractivePolicy(
		SettingsManager::getDefaultManager()->get(settingkeys::scriptFeedbackLive)
			? ScriptRunPolicy::Live : ScriptRunPolicy::Off);
	if (deps.prefs) deps.prefs->wireScripting(scriptEngine);
	registerStudioModules(*scriptEngine);
	if (deps.modules) deps.modules->registerApi(*scriptEngine);

	// THE CONSOLE IS THE BOTTOM AREA'S THIRD TAB (owner, 2026-09-14, lane
	// SPACE-2). Its DOCK is built with the editor's docks — it has to exist before
	// the saved layout is restored there, or a blob that names it leaves Qt
	// guessing at the whole bottom area — and the console widget, which needs
	// the script engine, arrives here. The dock is closed until Ctrl+` (or
	// editor.tray) asks for it, and it is tabified with Assets and the
	// Timeline, so asking for it adds a tab rather than splitting the area
	// (the thing the owner rejected at smoke S1).
	mConsole = new ScriptConsole(scriptEngine);

	// THE MCP ENDPOINT AND THE CLAUDE CHAT (scripting/claude/claudeassistant.h):
	// one owner, OFF by default; the Preferences page starts it through the
	// assistant so the console gets the connect line. Its toolbar action and
	// chord are its contribution.
	ClaudeAssistant::Deps assistantDeps;
	assistantDeps.engine = scriptEngine;
	assistantDeps.settings = deps.settings;
	assistantDeps.projectService = projectService;
	assistantDeps.project = project;
	assistantDeps.console = mConsole;
	assistantDeps.window = deps.window;
	mAssistant = new ClaudeAssistant(assistantDeps, this);
	ClaudeAssistant *assistant = mAssistant;
	if (deps.prefs)
		deps.prefs->wireMcp(assistant->mcp(), [assistant](quint16 port, QString *error) {
			return assistant->startMcpServer(port, error);
		});
	assistant->startFromSettings();
}
