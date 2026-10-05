/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/projectrunner.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFuture>
#include <QSqlDatabase>
#include <QThread>
#include <QtConcurrent>

#include <atomic>

#include "bridge/enginehost.h"
#include "data/project.h"
#include "data/settingsmanager.h"
#include "irisgl/core/irisutils.h"
#include "io/assetmanager.h"
#include "services/assetstorepaths.h"
#include "services/loadtimeline.h"
#include "services/meshbakestore.h"
#include "services/projectarchiver.h"
#include "services/projectservice.h"
#include "services/sceneextents.h"
#include "data/database/database.h"
#include "services/sceneopenrunner.h"
#include "viewport/ieditorviewport.h"

/// How long the blocking open pumps for its own open before it gives up and
/// says so. Ninety seconds against a worst measured open of ~3 s: this is a
/// deadlock guard, not a budget — a caller that waits is a caller that was
/// promised a loaded world.
static const int kOpenWaitBudgetMs = 90000;
/// The pump's idle nap. The runner puts ONE millisecond between its slices, so
/// a five-millisecond sleep per turn would add five to every slice of every
/// scripted open; one keeps the wait honest (measured: ~15 slices).
static const int kOpenWaitIdleMs = 1;

ProjectRunner::ProjectRunner(Database *db, Project *project, ProjectService *projectService,
                             SettingsManager *settings, IEditorViewport *viewport, Host *host,
                             QObject *parent)
    : QObject(parent), mDb(db), mProject(project), mProjectService(projectService),
      mSettings(settings), mViewport(viewport), mHost(host)
{
}

ProjectRunner::~ProjectRunner() = default;

// ---- the open, in stages (shared by the synchronous and threaded paths) ------

void ProjectRunner::stageBegin()
{
	// The bake cache window (MESH_BAKE_SPEC phase 1): while it is open, the
	// scene reader and the session registrations share ONE deserialized model
	// per source file. stageReveal closes it, so nothing is retained between
	// opens.
	MeshBakeStore::endScope();      // idempotent: an abandoned open's scope
	MeshBakeStore::beginScope();

	// The cover goes up FIRST, before any teardown: opening a world from
	// inside the editor (load in place) must not leave the previous world on
	// screen while this one loads.
	LoadTimeline::mark(QStringLiteral("cover+teardown"));
	mViewport->beginSceneLoad(mProject ? mProject->getProjectName() : QString());

	mHost->teardownWorld();
}

void ProjectRunner::stageRead(const iris::MeshPrewarmPtr &prewarm)
{
	LoadTimeline::mark(QStringLiteral("readProjectScene"));
	mPendingEditorData = Q_NULLPTR;
	mPendingScene = mProjectService->readProjectScene(&mPendingEditorData, prewarm);
}

void ProjectRunner::stageBind(bool playMode)
{
	LoadTimeline::mark(QStringLiteral("setScene"));
	auto scene = mPendingScene;
	EditorData *editorData = mPendingEditorData;
	mPendingScene.clear();
	mPendingEditorData = Q_NULLPTR;
	mHost->bindWorld(scene, editorData, playMode);
}

void ProjectRunner::stageReveal(bool playMode)
{
	LoadTimeline::mark(QStringLiteral("switchSpace"));
	// A REVEAL INTO A VIEW THAT COULD NOT BE CREATED stops early. It leaves the
	// bake scope open behind it (the next stageBegin closes it) — but NOT the
	// ledger's run any more (SHADER-WARM-2's merge read): an open run is a
	// compile window (services/livecompiles.h), and one left open by a failed
	// reveal switched the live-compile sentry off for the rest of the session.
	if (!mHost->revealWorld(playMode)) { LoadTimeline::end(); return; }
	MeshBakeStore::endScope();
	LoadTimeline::end();
}

QStringList ProjectRunner::plannedOpenModelPaths()
{
	// Every model file this open will need, resolved on the thread that owns
	// the database connection: the session membership's Objects and the
	// scene blob's mesh sources. ONE definition, used by the threaded open's
	// plan and by the synchronous open's prewarm.
	QStringList paths = mHost->plannedSessionModelPaths();
	if (mProjectService)
		for (const QString &path : mProjectService->plannedModelPaths())
			if (!paths.contains(path)) paths.append(path);
	return paths;
}

QStringList ProjectRunner::plannedOpenClipPaths()
{
	return mProjectService ? mProjectService->plannedClipPaths() : QStringList();
}

iris::MeshPrewarmPtr ProjectRunner::prewarmModelsPumped()
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
	// stageBegin, with the previous world still installed and the desktop
	// still the current page: no panel is being torn down or rebuilt inside
	// it, so nothing here can free a row that a panel is about to touch. That
	// ordering is the invariant; moving this call after stageBegin would
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
	const QStringList clipPaths = plannedOpenClipPaths();
	if (modelPaths.isEmpty() && clipPaths.isEmpty()) return prewarm;

	// STALE BAKES ARE REBUILT FROM THEIR OWN SOURCES FIRST (FORWARD-ONLY-1
	// D1): a bake is a cache of the parse, and a build that changed the code
	// producing it rebuilds it — on a worker, this thread pumping, with the
	// open's progress up — before anything reads. Never a parse on the open.
	// ...the clip files' CLIP bakes with them (SHIPPED-BAKES-1).
	MeshBakeStore::rebuildPumped(
	    MeshBakeStore::staleJobsFor(QSqlDatabase::database(), AssetStorePaths::root(),
	                                modelPaths + clipPaths),
	    [this](int i, int n) {
		    mHost->showOpenProgress(5 + (20 * i) / qMax(1, n),
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

void ProjectRunner::open(bool playMode)
{
	// The ledger (services/loadtimeline.h). Both open paths mark the same
	// stage names, so the synchronous open and the threaded one are directly
	// comparable in the log and in app.openTimings().
	if (!LoadTimeline::isRunning())
		LoadTimeline::begin(QStringLiteral("open(sync) %1")
		                        .arg(mProject ? mProject->getProjectName() : QString()));

	// THE BACKSTOP. A caller that points the project at another world must
	// drain an in-flight open BEFORE it does so (waitForOpen, and both project
	// verbs call it there); by the time we are here the pointers have already
	// moved, so all this can still do is refuse to interleave two worlds
	// through one set of slices.
	if (isOpening()) {
		qWarning("project open: a threaded open was still in flight when a blocking open "
		         "started — draining it (the caller should have waited first)");
		mRunner->waitForDone(kOpenWaitBudgetMs, kOpenWaitIdleMs);
	}

	// The models, parsed on a worker while this thread pumps (above).
	const iris::MeshPrewarmPtr prewarm = prewarmModelsPumped();

	stageBegin();
	// The session registrations, in the threaded open's order and with the
	// worker's models in hand — this is the "synchronous preload" that used to
	// run in ProjectService::prepareOpen BEFORE the open and parse every
	// pinned Object on this thread (deleted with this change).
	LoadTimeline::mark(QStringLiteral("sessionRegistrations"));
	AssetManager::clearAssetList();
	mHost->registerSessionAssets(prewarm);

	stageRead(prewarm);
	stageBind(playMode);
	LoadTimeline::mark(QStringLiteral("assetWidget.trigger"));
	mHost->buildPanels(false);
	// The LAST thing before the page switch: push the whole document into the
	// renderer (meshes, materials, textures) while the desktop page is still
	// the page on screen. Whatever this costs is spent under the progress
	// dialog instead of under a viewport that has nothing to show. Skipped
	// silently on the very first open, when no render view exists yet.
	LoadTimeline::mark(QStringLiteral("primeSceneSync"));
	mViewport->primeSceneSync();
	// The pass shape this machine's editor draws with, for the NEXT launch's
	// startup gate (F1b): two settings values, written only on change.
	mViewport->rememberPassShape();
	stageReveal(playMode);
	// THE LOAD IS OVER (OPEN_COVER_SPEC §2 A). Said here and at the runner's
	// `finished` — the two ends of the two routes — and NOT inside
	// stageReveal, which is a SLICE on the threaded route with one more
	// boundary frame behind it: that frame is the world's first, and letting it
	// build the whole GI arm is the block this lane removes, one frame later.
	mViewport->endSceneLoad();
}

bool ProjectRunner::isOpening() const
{
	return mRunner && mRunner->isRunning();
}

unsigned ProjectRunner::sliceBoundaries() const
{
	return mRunner ? mRunner->boundaryRuns() : 0u;
}

bool ProjectRunner::waitForOpen()
{
	if (!isOpening()) return true;
	// THE PUMP HERE IS NOT THE SAFE ONE (see prewarmModelsPumped): the slices
	// it services install a world — they mount panels, bind the properties
	// tree and switch the page, and they retire panel rows whose owners keep
	// raw pointers to them. That is the threaded open's own exposure, not one
	// this call adds: the very same slices run from the very same event loop
	// when nobody is waiting. What this does add is that they finish BEFORE
	// the caller tears the project down, which is the hybrid this exists to
	// prevent.
	return mRunner->waitForDone(kOpenWaitBudgetMs, kOpenWaitIdleMs);
}

void ProjectRunner::openAsync(bool playMode)
{
	// One open at a time, and the same backstop as the blocking open above:
	// the caller drains an in-flight open through waitForOpen() before it
	// re-points the project; this only stops two worlds sharing one set of
	// slices if one ever gets here anyway.
	if (isOpening()) {
		qWarning("project open: a threaded open was still in flight when another started — "
		         "draining it (the caller should have waited first)");
		mRunner->waitForDone(kOpenWaitBudgetMs, kOpenWaitIdleMs);
	}

	if (!LoadTimeline::isRunning())
		LoadTimeline::begin(QStringLiteral("open(async) %1")
		                        .arg(mProject ? mProject->getProjectName() : QString()));
	startOpenRun(playMode);
}

// THE RUNNER AND WHAT HAPPENS BETWEEN TWO OF ITS SLICES. Built once per window,
// by whichever of the two routes — the open or the create — reaches it first.
void ProjectRunner::startRunnerIfNeeded()
{
	if (mRunner) return;
	mRunner = new SceneOpenRunner(mDb, mProject, this);
	connect(mRunner, &SceneOpenRunner::progress, this,
	        [this](int percent, const QString &text) { mHost->showOpenProgress(percent, text); });
	connect(mRunner, &SceneOpenRunner::finished, this, [this](bool) {
		mHost->hideOpenProgress();
		// EVERY SLICE HAS RUN, AND SO HAS THE LAST BOUNDARY FRAME: the world is
		// installed and on screen, so the engine may build its first GI arm
		// (OPEN_COVER_SPEC §2 A). One frame later than the reveal, and
		// deliberately — see open().
		if (mViewport) mViewport->endSceneLoad();
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
	mRunner->setSliceBoundary([this]() {
		LoadTimeline::Accumulate row(QStringLiteral("slice:boundaryFrame"));
		if (mViewport && mViewport->canRenderFrames()) {
			mViewport->renderFrames(1);
			++mSliceBoundaryFrames;
			return;
		}
		if (auto engine = EngineHost::instance().engine()) engine->advanceResources();
	});
}

/// THE ONE OPEN (OPEN-ASSIMP-1). Plans the model parses, starts the worker and
/// queues the install slices; the caller decides whether to wait.
void ProjectRunner::startOpenRun(bool playMode)
{
	// Without a project service there is no document to read and the stages
	// below would dereference it. It never happens in a running app — the
	// service is built in the window's constructor — and it is not a reason to
	// fall back to a second open path.
	if (!mProjectService) {
		qWarning("project open: no project service — nothing was opened");
		return;
	}

	// The cover goes up NOW, not in the first slice: the parse phase runs on
	// a worker for up to a second, and opening a world from inside the editor
	// must not leave the previous one on screen while it does. stageBegin
	// raises it again (idempotent — it only rebases the present counter, and
	// nothing has presented in between).
	mViewport->beginSceneLoad(mProject ? mProject->getProjectName() : QString());

	// ---- plan: the DB half, here, on the thread that owns the connection ----
	LoadTimeline::mark(QStringLiteral("plan"));
	const QStringList modelPaths = plannedOpenModelPaths();

	startRunnerIfNeeded();

	// ---- install: UI-thread slices, one per event-loop turn ----------------
	//
	// The session hydration is spread over SEVERAL turns: it is the one
	// install step whose cost grows with the project (49 assets in the
	// Showroom sample), and a single 200 ms slice plus an engine frame is
	// most of the responsiveness budget on its own.
	const QStringList sessionGuids = mHost->sessionAssetGuids();
	const int kAssetsPerSlice = 8;

	QVector<SceneOpenRunner::Slice> slices;
	slices.append({ QStringLiteral("Preparing assets…"), 45, [this]() {
		stageBegin();
		LoadTimeline::mark(QStringLiteral("sessionRegistrations"));
		AssetManager::clearAssetList();
	} });
	for (int at = 0; at < sessionGuids.size(); at += kAssetsPerSlice) {
		const QStringList batch = sessionGuids.mid(at, kAssetsPerSlice);
		const int pct = 45 + (10 * (at + batch.size())) / qMax(1, sessionGuids.size());
		slices.append({ QStringLiteral("Preparing assets (%1 of %2)…")
		                    .arg(at + batch.size()).arg(sessionGuids.size()),
		                pct, [this, batch]() {
			mHost->registerSessionAssetGuids(batch, mRunner->prewarm());
		} });
	}
	slices.append({ QStringLiteral("Reading the scene…"), 60,
	                [this]() { stageRead(mRunner->prewarm()); } });
	slices.append({ QStringLiteral("Binding the scene…"), 65,
	                [this, playMode]() { stageBind(playMode); } });
	slices.append({ QStringLiteral("Building the asset panel…"), 70, [this]() {
		LoadTimeline::mark(QStringLiteral("assetWidget.trigger"));
		mHost->buildPanels(false);
	} });
	slices.append({ QStringLiteral("Uploading geometry…"), 80, [this]() {
		LoadTimeline::mark(QStringLiteral("primeSceneSync"));
		mViewport->primeSceneGeometry();
	} });
	slices.append({ QStringLiteral("Lighting the world…"), 90, [this]() {
		LoadTimeline::mark(QStringLiteral("primeSceneEnvironment"));
		mViewport->primeSceneEnvironment();
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
	if (mSettings->get(settingkeys::shaderWarmupOnOpen)) {
		slices.append({ QStringLiteral("Compiling shaders…"), 95, [this]() {
			LoadTimeline::mark(QStringLiteral("warmUpShaders"));
			const unsigned built = mViewport->warmUpShaders(
			    [this](unsigned n) { mHost->showOpenCompileProgress(n); });
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
	                [this]() { mViewport->rememberPassShape(); } });
	slices.append({ QStringLiteral("Opening…"), 100,
	                [this, playMode]() { stageReveal(playMode); } });

	mRunner->setPlan(modelPaths, plannedOpenClipPaths(), slices,
	                 mProject ? mProject->getProjectName() : QStringLiteral("scene"));
	mRunner->start();
}

// ---- the create --------------------------------------------------------------
//
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
// the engine is told a world is on its way (stageBegin's `beginSceneLoad`),
// which is what puts the arm on the streaming path instead of into the first
// frame the user sees.
//
// THE PAGE SWITCH MOVED TO THE END, with the open's reveal. It used to be first
// — "to ensure the editor's context is created" — and that ordering is what put
// the NoScene cover's inline presents and the panels' first paint inside the
// verb. The open path has always bound its scene with no View yet on the first
// world of a session (`primeSceneGeometry` says so and returns), and a create
// is no different.
//
// AND THE CONTRACT IS UNCHANGED FOR BOTH CALLERS: create() still returns with
// the project open. The wait is `waitForOpen`, which PUMPS the event loop rather
// than blocking it (the same drain `project.open` has used since OPEN-ASSIMP-1),
// so `project.create` keeps its synchronous promise to scripts and the desktop's
// Create button keeps a window that answers.
void ProjectRunner::create(const QString &guid, const QString &filename,
                           const QString &projectPath, SceneTemplate kind)
{
    startCreateRun(guid, filename, projectPath, kind);
    waitForOpen();
}

// ...AND THE SAME CREATE WITHOUT THE WAIT (§2 C/§4, `project.createAsync`).
// The ONE difference is the drain: the slices are the same slices, queued on
// the same runner, and `isOpening()` — which `project.openState()` reads —
// covers a create exactly as it covers an open. It exists because a caller
// that wants to WATCH a world arrive has to own the frames between the slices,
// and `create` spends them itself inside `waitForOpen`.
void ProjectRunner::createAsync(const QString &guid, const QString &filename,
                                const QString &projectPath, SceneTemplate kind)
{
    startCreateRun(guid, filename, projectPath, kind);
}

void ProjectRunner::startCreateRun(const QString &guid, const QString &filename,
                                   const QString &projectPath, SceneTemplate kind)
{
    // AN OPEN IN FLIGHT FINISHES FIRST — before this create's ledger begins:
    // its run is the one LoadTimeline holds until the open ends it, so a begin
    // skipped because "a run is running" would leave the create's marks on no
    // run at all once the drain's end() closed the open's (fix round).
    if (isOpening()) {
        qWarning("project create: an open was still in flight — draining it first");
        mRunner->waitForDone(kOpenWaitBudgetMs, kOpenWaitIdleMs);
    }
    // THE LEDGER COVERS THE CLOSE (CREATE-GAP-1). It began after it, so the
    // create's own record was the smaller half of the verb: 800-1200 ms of a
    // create over an open world — the autosave, the teardown, the page switch
    // — sat in front of the run and in no stage. `closePrevious` is that
    // span, and its counters (closePrevious:save / :teardown / :switch, and
    // the save's own saveOpen:*) say where it went.
    if (!LoadTimeline::isRunning())
        LoadTimeline::begin(QStringLiteral("create %1").arg(filename));
    if (mProjectService->isSceneOpen()) {
        LoadTimeline::mark(QStringLiteral("closePrevious"));
        close(false);
    }
    // ...AND ONLY NOW IS THE CURRENT PROJECT THE NEW ONE: the close above
    // autosaved the old world into the old project's own row (createProject-
    // Shell used to re-point first, and the old project's edits went into the
    // new one's row — CREATE-GAP-1).
    mProjectService->pointAtProject(guid, filename);

    // The runner and its slice boundary are set up once, by whichever route
    // reaches them first, so there is ONE definition of what a boundary does.
    startRunnerIfNeeded();

    QVector<SceneOpenRunner::Slice> slices;
    slices.append({ QStringLiteral("Preparing…"), 30, [this]() {
        // The cover, the teardown and the bake scope — the open's own first
        // slice, and the call that tells the engine a world is arriving.
        stageBegin();
    } });
    slices.append({ QStringLiteral("Creating the scene…"), 45, [this, kind]() {
        LoadTimeline::mark(QStringLiteral("createDefaultScene"));
        mPendingScene = mHost->createWorld(kind);
    } });
    slices.append({ QStringLiteral("Binding the scene…"), 60, [this]() {
        LoadTimeline::mark(QStringLiteral("setScene"));
        auto created = mPendingScene;
        mPendingScene.clear();
        mHost->bindNewWorld(created);
    } });
    slices.append({ QStringLiteral("Building the asset panel…"), 75, [this]() {
        LoadTimeline::mark(QStringLiteral("assetWidget.trigger"));
        mHost->buildPanels(true);
    } });
    slices.append({ QStringLiteral("Uploading geometry…"), 80, [this]() {
        LoadTimeline::mark(QStringLiteral("primeSceneSync"));
        mViewport->primeSceneGeometry();
    } });
    slices.append({ QStringLiteral("Lighting the world…"), 90, [this]() {
        LoadTimeline::mark(QStringLiteral("primeSceneEnvironment"));
        mViewport->primeSceneEnvironment();
    } });
    if (mSettings->get(settingkeys::shaderWarmupOnOpen)) {
        slices.append({ QStringLiteral("Compiling shaders…"), 95, [this]() {
            LoadTimeline::mark(QStringLiteral("warmUpShaders"));
            const unsigned built = mViewport->warmUpShaders(
                [this](unsigned n) { mHost->showOpenCompileProgress(n); });
            if (built) qInfo("scene create: precompiled %u shader(s) behind the cover", built);
        } });
    }
    // The pass shape for the next launch's startup gate (SHADER_CACHE_AUDIT
    // F1b) — two settings values, written only on change. (This slice used to
    // RECORD the world's permutation set as well; the set named its materials
    // by a process-unique datablock name and warmed nothing in the next
    // process, so the machinery was deleted — WARMUPSET-2, 2026-09-21.)
    slices.append({ QStringLiteral("Precompiling shaders…"), 96,
                    [this]() { mViewport->rememberPassShape(); } });
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
                    [this, projectPath, hadViewport]() {
        LoadTimeline::mark(QStringLiteral("saveInitialScene"));
        *hadViewport = mViewport->isInitialized();
        mProjectService->saveInitialScene(projectPath);
    } });
    slices.append({ QStringLiteral("Opening…"), 100,
                    [this, projectPath, hadViewport]() {
        stageReveal(false);
        if (!*hadViewport && mViewport->isInitialized()) {
            qInfo("scene create: the tile was taken after the reveal — the editor page "
                  "had never been shown, so the initial save had no viewport");
            mProjectService->saveInitialScene(projectPath);
        }
    } });

    mRunner->setPlan(QStringList(), QStringList(), slices,
                     filename.isEmpty() ? QStringLiteral("scene") : filename);
    mRunner->start();
}

// ---- the close ---------------------------------------------------------------

void ProjectRunner::close(bool reopenInPlace)
{
    mHost->prepareClose();
    // AN OPEN IN FLIGHT IS DRAINED FIRST (lane OPEN-FRAMES-1, item 3), the way
    // the window-close and shutdown paths already do it (settle / stop below).
    // Without this a queued install slice could run AFTER the teardown — it
    // would mount panels on a document that no longer exists, push a scene
    // that was just destroyed and switch the page to a world nobody opened.
    // Nothing but ProjectApi's refusal stood between that and the user, and
    // the refusal only covers the scripted route: a tile's close control, the
    // menu and the shutdown path reach here with slices still queued.
    //
    // DRAIN, THEN ABANDON. waitForDone pumps the loop the slices run on, so the
    // healthy case is simply "the open finishes, then it closes" — the same
    // coherence the close-event settle buys. Only an install that will not
    // finish inside the budget is abandoned, and requestAbort stops the NEXT
    // slice rather than interrupting one.
    //
    // RE-ENTRANCY IS GUARDED, and it must be: the pump can deliver another
    // close (a second click, a queued menu action, an MCP request), and the
    // teardown is not re-entrant.
    static bool sDrainingOpen = false;
    // A NESTED close arriving through the drain's pump (an MCP project.close,
    // a queued metacall — not user input, so ExcludeUserInputEvents lets it in)
    // must RETURN, not fall through to the teardown under the outer drain
    // (the second read of OPEN-FRAMES-1): the outer close finishes the job.
    if (sDrainingOpen) return;
    if (isOpening()) {
        sDrainingOpen = true;
        mRunner->waitForDone(kOpenWaitBudgetMs, kOpenWaitIdleMs);
        if (mRunner->isRunning()) {
            qWarning("project close: the open in flight did not finish inside its budget — "
                     "abandoning the rest of its install");
            mRunner->requestAbort();
            mRunner->waitForDone(2000, kOpenWaitIdleMs);
        }
        sDrainingOpen = false;
    }
    mHost->closeWorld(reopenInPlace);
}

// ---- the export --------------------------------------------------------------

bool ProjectRunner::startExport(const QString &guid, const QString &zipPath, QString *why)
{
    const auto refuse = [why](const QString &reason) {
        if (why) *why = reason;
        return false;
    };
    if (mArchiver && mArchiver->isRunning())
        return refuse(tr("An archive operation is already running."));
    if (guid.isEmpty() || !mDb->fetchProjectTile(guid, nullptr))
        return refuse(tr("No project with guid '%1'.").arg(guid));

    // THE OPEN WORLD IS SAVED ONLY WHEN IT IS THE ONE BEING EXPORTED (CREATE-
    // GAP-1's fix round). The tile's Export used to re-point the LIVE project
    // at the exported tile and then save "the scene" — the open world, written
    // into the exported project's row and folder — and the pointer stayed
    // there, so every later autosave of the open world landed in that row too.
    const bool exportingOpenWorld = mHost->openWorld() && mProjectService->isSceneOpen()
                                    && guid == mProject->getProjectGuid();
    if (exportingOpenWorld) mHost->saveOpenWorld();

    if (!mArchiver) {
        // It exports `mExportTarget` — a Project naming the row — never the
        // live project. SIGNAL-driven, never pumping: a pump from inside a
        // slice re-enters the loop and can destroy objects the slice is still
        // using (ProgressDialog::setPumpsEventLoop documents the scar).
        mExportTarget = std::make_unique<Project>();
        mArchiver = new ProjectArchiver(mDb, mExportTarget.get(), this);
        connect(mArchiver, &ProjectArchiver::progress, this, &ProjectRunner::exportProgress);
        connect(mArchiver, &ProjectArchiver::finished, this, [this](bool canceled) {
            emit exportFinished(canceled, mArchiver->result().ok(), mArchiver->result().error);
        });
    }
    mExportTarget->setProjectPath(mProjectService->projectFolderFor(guid), QString());
    mExportTarget->setProjectGuid(guid);

    // Pin-world archives (phase 4): catalog snapshot + manifest v2 + the
    // pinned CAS objects, through the one archive implementation the
    // project.exportArchive verb also calls — THREADED here (Lane 4), so the
    // window keeps painting while a multi-hundred-megabyte world compresses.
    emit exportStarted();
    // The manifest's scene-scale block, measured from the live document — the
    // archiver only ever sees the database (services/sceneextents.h). Only
    // the OPEN world has a live document; another project's archive carries
    // no scale block rather than the open world's.
    mArchiver->setSceneMetadata(exportingOpenWorld && mViewport
                                    ? sceneextents::describe(mViewport->getScene(),
                                                             mViewport->editorCamera())
                                    : exportformat::ManifestScene());
    if (!mArchiver->startExport(zipPath))
        return refuse(mArchiver->result().error);
    return true;
}

void ProjectRunner::cancelExport()
{
    if (mArchiver) mArchiver->requestCancel();
}

// ---- teardown ----------------------------------------------------------------

void ProjectRunner::settle(int budgetMs)
{
    if (!mRunner) return;
    mRunner->waitForDone(budgetMs);
    if (mRunner->isRunning()) mRunner->requestAbort();
}

bool ProjectRunner::stop(int budgetMs)
{
    if (!mRunner) return true;
    mRunner->requestAbort();
    return mRunner->waitForDone(budgetMs);
}
