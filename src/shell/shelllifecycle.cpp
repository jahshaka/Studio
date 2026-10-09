/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/shelllifecycle.h"
#include "services/forcedexit.h"

#include <QCloseEvent>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QThreadPool>
#include <QUndoStack>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "bridge/enginehost.h"
#include "data/constants.h"
#include "data/database/database.h"
#include "data/settingsmanager.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/assets/shippedmeshes.h"
#include "scripting/scriptengine.h"
#include "scripting/claude/claudeassistant.h"
#include "scripting/scripthost.h"
#include "services/apppaths.h"
#include "services/assetstore.h"
#include "services/editgate.h"
#include "services/framemonitor.h"
#include "services/jahlog.h"
#include "services/mainthreadwatchdog.h"
#include "services/materialpresetseeder.h"
#include "services/meshbakestore.h"
#include "services/primitiveassets.h"
#include "services/projectarchiver.h"
#include "services/projectservice.h"
#include "services/sessionmarkers.h"
#include "services/thumbnailgenerator.h"
#include "services/undoservice.h"
#include "shell/editordocks.h"
#include "shell/modulehub.h"
#include "shell/shellservices.h"
#include "services/projectrunner.h"
#include "ui/pages/assetview.h"
#include "ui/panels/assetwidget.h"
#include "shell/shutdownorder.h"
#include "ui/controls/tilecache.h"
#include "viewport/cameraspeed.h"

ShellLifecycle::ShellLifecycle(QObject *parent) : QObject(parent)
{
}

void ShellLifecycle::setParts(const Parts &parts)
{
    mParts = parts;
}

Database *ShellLifecycle::openLibrary()
{
    const QString path = IrisUtils::join(
        AppPaths::dataRoot(), Constants::JAH_DATABASE
    );

    // Library lock (ASSET_PIPELINE preflight §6.2): held for the app's
    // lifetime so store migration tools can refuse while any instance runs.
    // Non-fatal — a second instance simply runs without the lock, as before.
    LibraryLock::acquire(path);

    mDb = new Database();
	if (mDb->initializeDatabase(path)) {
		mDb->createAllTables();
	}
	// THE TILE CACHE READS BY GUID (D11-LIBRARY-SCALE): listings carry no
	// thumbnail, so the cache's batches read the pictures a view paints from
	// here, on this thread; a thumbnail written anywhere drops the cached tile.
	Database *db = mDb;
	TileCache::instance().setSource(TileCache::Kind::Asset, [db](const QStringList &guids) {
		return db ? db->fetchAssetThumbnailBytes(guids) : QHash<QString, QByteArray>();
	});
	TileCache::instance().setSource(TileCache::Kind::Project, [db](const QStringList &guids) {
		return db ? db->fetchProjectThumbnailBytes(guids) : QHash<QString, QByteArray>();
	});
	Database::setAssetThumbnailWritten([](const QString &guid) {
		TileCache::instance().invalidate(TileCache::Kind::Asset, guid);
	});
    // THE SEEDS (services/primitiveassets.h). The primitives and the samples'
    // Teapot are baked library assets now: one import and one bake each,
    // the first time a library is opened, COMMITTED here before this returns —
    // never later from a background thread, because a library whose row count
    // moves while a script runs is the defect MaterialPresetSeeder's header
    // describes. The bakes themselves run on a worker this thread joins
    // (primitiveseed.cpp: a join, not a pump — we are inside MainWindow's
    // constructor). A library that already holds them pays one catalog query
    // per row.
    QStringList seedErrors;
    const int seeded = PrimitiveAssets::seedAll(mDb, &seedErrors);
    if (seeded > 0) irisLog(QStringLiteral("primitives: baked %1 shipped meshes").arg(seeded));
    for (const QString &line : seedErrors) irisLog("primitive seed: " + line);
    // EVERY SHIPPED MESH THE APP DRAWS IS ONE OF THOSE ROWS (SHIPPED-BAKES-1):
    // the preview docks' subjects, the avatar room's cube and the VR controller
    // models ask for theirs by seed key through IrisGL's seam, and get the bake.
    iris::ShippedMeshes::setResolver([db](const QString &seedKey) {
        return PrimitiveAssets::mesh(seedKey, db);
    });
    // STALE BAKES, IN THE BACKGROUND (FORWARD-ONLY-1 D1): a build that changed
    // the bake's producer rebuilds every stale bake from its own source, at the
    // lowest priority; an open rebuilds the ones it needs first, itself.
    MeshBakeStore::startBackgroundRebuild();
    return mDb;
}

void ShellLifecycle::closeRequested(QCloseEvent *event)
{
	// A SCRIPT IN FLIGHT IS STOPPED FIRST, and the close waits for it
	// (SCRIPTING_LIVE_SPEC). A run holds the UI thread only between hops now,
	// so this window CAN be closed while a script is working — and closing it
	// destroys the script engine, the host and the modules under a worker
	// thread that is about to hop into them. Stop the run (it ends at its next
	// JavaScript boundary; a run parked inside a long verb ends when that verb
	// returns) and re-post the close for when it has, which is the same
	// promise app.quit() makes.
	if (mParts.scriptEngine && mParts.scriptEngine->isRunning()) {
		mParts.scriptEngine->stop();
		QPointer<QMainWindow> window(mParts.window);
		if (mParts.scriptHost && mParts.scriptHost->afterRun)
			mParts.scriptHost->afterRun([window]() { if (window) window->close(); });
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
	if (mParts.projects && mParts.projects->isOpening()) {
		sSettlingOpen = true;
		mParts.projects->settle(5000);
		sSettlingOpen = false;
	}

	ProjectService *projectService = mParts.services ? mParts.services->project() : nullptr;
	UndoService *undoService = mParts.services ? mParts.services->undo() : nullptr;
	const bool autoSave = mParts.settings->get(settingkeys::autoSave);
	const bool sceneOpen = projectService && projectService->isSceneOpen();

	if (autoSave && sceneOpen) {
		if (mParts.saveScene) mParts.saveScene();
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
		if (undoService && undoService->isDirty()
		    && !undoService->savedCountMatchesCurrent() && sceneOpen) {
			QMessageBox::StandardButton reply;
			reply = QMessageBox::question(mParts.window,
				"Unsaved Changes",
				"There are unsaved changes, save before closing?",
				QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
			if (reply == QMessageBox::Yes) {
				if (mParts.saveScene) mParts.saveScene();
				event->accept();
			}
			else if (reply == QMessageBox::No) {
				event->accept();
			}
			else {
				event->ignore();
				return;
			}
		}
		else {
			event->accept();
		}
	}

	// (THE DONATE DIALOG USED TO RUN HERE, modally, as the last thing a user
	// saw on the way out. It moved to FIRST LAUNCH — app/firstrun.h, called
	// from main() — for two reasons: asking on the way out is the worst moment
	// to ask, and a nested modal event loop inside closeEvent meant app.quit()
	// could not complete until somebody clicked it. Owner decision D3,
	// 2026-09-12. Nothing may be added here that runs its own event loop.)

	// STEP 1 of the shutdown order (the whole sequence is documented in one
	// place, in shelllifecycle.h, and enumerated in shell/shutdownorder.h).
	// Recorded HERE, past the Cancel branch above: a close the user backed out
	// of is not a shutdown.
	JAH_SHUTDOWN_STEP(ShutdownOrder::CloseEvent, "closeEvent: autosave + settings");

	// The session's own totals (SESSION_LOG_SPEC §5, "clean quit"). Written
	// HERE, past the Cancel branch, for the same reason the step above is: a
	// close the user backed out of is not the end of the session. JahLog's
	// close bracket and by-level roll-up follow later, in finalizeAppExit.
	SessionMarkers::logQuitSummary();

	mParts.settings->setValue("geometry", mParts.window->saveGeometry());
	mParts.settings->setValue("windowState", mParts.window->saveState());
	// ...and the EDITOR DOCKS, which live in the nested `viewPort` QMainWindow
	// and are therefore not in the line above (shell/dockstate.h) — the
	// editor's layout, not this page's (lane SPACE-1; EditorDocks says why).
	if (mParts.docks) mParts.docks->storeLayout();

    // Orderly teardown BEFORE the window disappears: dialogs close with a
    // window still on screen, and a mid-flight import batch is aborted and
    // joined while the event loop can still service its commit hop. (Also
    // wired to aboutToQuit for the QApplication::exit/quit paths.)
    stopBackgroundWork();
}

void ShellLifecycle::stopBackgroundWork()
{
    // Idempotent: closeEvent AND aboutToQuit both land here.
    if (mBackgroundStopped) return;
    mBackgroundStopped = true;

    // STEP 2 of the shutdown order (see shelllifecycle.h / shell/shutdownorder.h).
    JAH_SHUTDOWN_STEP(ShutdownOrder::BackgroundWork, "shutdownBackgroundWork: workers joined");

    // THE CLOSE'S TILE (CLOSE-SHOT-2). closeEvent's autosave ASKED for the
    // presented frame; it is satisfied here, first, with the world and the engine
    // whole (step 4 releases the engine) — one frame and its fence, never a
    // settle — and its encode is then drained with the others below.
    if (ProjectService *projectService = mParts.services ? mParts.services->project() : nullptr)
        projectService->settleThumbnailCapture();

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
    forcedexit::arm(20, "teardown exceeded 20s");

    // The library's background bake rebuild (FORWARD-ONLY-1 D1): joined here,
    // a bake in flight finishes into its own temp and is discarded.
    MeshBakeStore::stopBackgroundRebuild();

    // THE FIRST-RUN PRESET SEED (RESET-LIBRARY-1's fix round). Its own header
    // said "the app's shutdown calls it" and only the --script path
    // (scriptrunner.cpp) ever did — so a window closed during the first
    // launch's seed left a worker copying and fsyncing map files into the
    // store while the rest of this function tore the app down around it, to be
    // reaped by the pool wait below only if it happened to finish, and by the
    // forced exit if it did not. It is a WARM-UP: aborting costs at most the
    // file in flight, the next launch finishes what was skipped, and the abort
    // reaches the runner underneath it too, which is what lets the pool wait
    // further down do the joining.
    MaterialPresetSeeder::instance().requestAbort();

    // EVERY MODULE IS TOLD TO STOP FIRST (item 2). Module workers ride the same
    // global pool the wait below joins, and the modules' shutdown() — where a
    // module's own abort used to live — runs AFTER that wait and after the
    // forced exit behind it. Nothing here joins: abort, flush, return, and let
    // the one pool wait below do the joining for all of them.
    if (mParts.modules) mParts.modules->abortBackgroundWork();

    // Import pipeline: abort batches, join workers (bounded), close the
    // progress dialogs, drop viewer-tail queues.
    bool workersStopped = true;
    // The open runner: abandon whatever is left and join its parse worker.
    if (mParts.projects) workersStopped &= mParts.projects->stop(3000);
    if (mParts.docks && mParts.docks->assetTray())
        workersStopped &= mParts.docks->assetTray()->shutdownImports(3000);
    if (AssetView *page = mParts.assetsPage ? mParts.assetsPage() : nullptr)
        workersStopped &= page->shutdownImports(3000);

    // Archive export/import (STABILITY_PROGRAM_SPEC Lane 4): cancelled and
    // joined, bounded, exactly like the import batches. Every live archiver —
    // this window's exporter and the project page's importer — is covered by
    // the one static call.
    workersStopped &= ProjectArchiver::shutdownArchives(3000);

    // The MCP endpoint must not accept requests into a half-torn-down app, and
    // the Claude chat subprocess closes stdin, waits briefly, and is killed.
    if (mParts.assistant) mParts.assistant->shutdown();

    ThumbnailGenerator::getSingleton()->shutdown();

    // THE LAST SAVE'S THUMBNAIL (CREATE-GAP-1): a save encodes its PNG on a
    // worker and writes it when the worker is done — closeEvent's autosave
    // above is exactly such a save. Waited for and WRITTEN here, while the
    // database is still open, so a quit never drops the picture of the world
    // it just saved. Bounded by one PNG encode (~100 ms).
    if (ProjectService *projectService = mParts.services ? mParts.services->project() : nullptr) {
        const int drained = projectService->drainThumbnailEncodes();
        if (drained) qInfo("shutdown: wrote %d pending project thumbnail(s)", drained);
    }

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
        forcedexit::now("background workers did not stop in time (settings are saved; no "
                        "teardown race)");
    }

    // STEP 3 of the shutdown order: StudioModule::shutdown() on every module,
    // after the workers are joined and BEFORE EngineHost::shutdown() (step 4),
    // so a module still sees a live engine while it lets go of it. The module
    // OBJECTS are deleted at step 5: a module's page is still in the stacked
    // widget at this point and the destructor order of the two must stay the
    // Qt one.
    JAH_SHUTDOWN_STEP(ShutdownOrder::Modules, "modules shut down");
    if (mParts.modules) mParts.modules->shutdownModules();
}

void ShellLifecycle::teardownWindow()
{
    JAH_SHUTDOWN_STEP(ShutdownOrder::WindowBody, "~MainWindow body");

    // The edit gate's notice captured the window (ledger §423). The gate
    // outlives every window — it is process-wide — so the hook goes first,
    // before anything here can raise it.
    editgate::setNoticeHook({});

    // ...and so did the camera-speed dial (fix round item 1). CameraSpeed is
    // process-wide too, so a handler capturing the window must not outlive it.
    CameraSpeed::setOnChanged({});

    // ORDER IS LOAD-BEARING. Undo commands owe the database work when they die
    // (DeleteSceneNodeCommand finalises the asset row once no undo can reach
    // the delete any more), and undoStack is parented to the window — so it
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
    if (mParts.undoStack) mParts.undoStack->clear();
    if (mDb) mDb->flushPendingAssetDeletes();
    // ...and the last save's thumbnail, for the same reason and the same exits:
    // the --script / --dump-api-docs paths never reach stopBackgroundWork,
    // where a window close drains it (CREATE-GAP-1). Idempotent; nothing left
    // is nothing done.
    // A tile still asked for is answered first (the engine views die at step 6);
    // on the window-close path step 2 already did, and this is nothing.
    if (ProjectService *projectService = mParts.services ? mParts.services->project() : nullptr) {
        projectService->settleThumbnailCapture();
        projectService->drainThumbnailEncodes();
    }

    // The modules. ONE TEARDOWN PATH: on the window-close path step 3 has
    // already shut them down and this only deletes them; the --script /
    // --dump-api-docs exits NEVER run steps 1-3 (no closeEvent, no
    // aboutToQuit), so the hub shuts them down HERE — or deleting the avatar
    // module frees AvatarPreviewModel while AvatarPreviewScene still holds a
    // raw back-pointer to it: the widget tree's release() then jumps through a
    // freed std::function (the fix-wave gate's e2e.avatar SEGV, 2026-09-05).
    // Their PAGES belong to the stacked widget and die with the tree.
    if (mParts.modules) mParts.modules->releaseModules();

    // The services that are not QObjects of the window, then the Ui:: struct
    // (the QObject services are the service layer's children and die with it).
    if (mParts.services) mParts.services->destroyPlain();
    if (mParts.deleteUi) mParts.deleteUi();

    JAH_SHUTDOWN_STEP(ShutdownOrder::EngineViews, "engine-holding widgets destroyed");
    destroyEngineViews();

    JAH_SHUTDOWN_STEP(ShutdownOrder::DatabaseClosed, "database closed");
    // The tile cache's reads end with the connection (a batch queued behind
    // the close reads nothing).
    TileCache::instance().setSource(TileCache::Kind::Asset, nullptr);
    TileCache::instance().setSource(TileCache::Kind::Project, nullptr);
    Database::setAssetThumbnailWritten(nullptr);
    iris::ShippedMeshes::setResolver(nullptr);
    if (mDb) mDb->closeDatabase();
}

void ShellLifecycle::destroyEngineViews()
{
    // STEP 6, and the reason it exists.
    //
    // EngineHost::shutdown() (step 4) drops the HOST's reference and stops the
    // render loop — but the Engine is a shared_ptr and four widgets hold their
    // own copies: the editor viewport (viewport/enginesceneviewport.h), the
    // player view, the Assets page's viewer, and the module previews (materials
    // Display, avatar). Every one of them lives in the window's child widget
    // tree, which Qt destroys in ~QWidget — AFTER the destructor's body, i.e.
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
    if (mParts.window)
        for (QObject *child : mParts.window->children())
            if (QWidget *w = qobject_cast<QWidget *>(child)) kids.append(w);
    for (QPointer<QWidget> &w : kids)
        if (!w.isNull()) delete w.data();

    // Everything the window kept points into that tree. Nothing runs after
    // this except closeDatabase(), but a stale viewport pointer is the kind of
    // thing a later edit trips over.
    if (mParts.forget) mParts.forget();

    // The Engine must be gone now. It is not an assert because a MainWindow
    // can legitimately be destroyed before finalizeAppExit ran (a CLI path
    // that returns early), in which case EngineHost still holds its reference
    // — that case is excluded, and what is left is the real finding: somebody
    // added a shared_ptr<Engine> holder that is not in the window's widget
    // tree, and the Engine is once again dying after the database closes.
    if (!EngineHost::instance().isRunning() && !mEngineWatch.expired())
        qWarning("[shutdown] step 6: the Engine is STILL referenced after the "
                 "viewports were destroyed — a holder outside MainWindow's "
                 "widget tree exists, and the engine will now be torn down "
                 "after closeDatabase(). See shell/shutdownorder.h.");
}
