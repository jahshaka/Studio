/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/shellservices.h"


#include "data/database/database.h"
#include "data/project.h"
#include "services/jahlog.h"
#include "services/ambienceservice.h"
#include "services/assetcas.h"
#include "services/assetservice.h"
#include "services/assetstorepaths.h"
#include "services/clipboardservice.h"
#include "services/loadtimeline.h"
#include "services/materialpreviewservice.h"
#include "services/perfsampler.h"
#include "services/previewdemand.h"
#include "services/previewservice.h"
#include "services/previewwriter.h"
#include "bridge/enginehost.h"
#include "services/playbackservice.h"
#include "player/engineplayerview.h"
#include "services/playerservice.h"
#include "services/projectservice.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "services/services.h"
#include "services/sessionmarkers.h"
#include "services/subscriber.h"
#include "services/undoservice.h"
#include "ui/controls/tilecache.h"

ShellServices::ShellServices(const Deps &deps, QObject *parent) : QObject(parent)
{
    Database *db = deps.db;
    Project *project = deps.project;

    mUndo = new UndoService(deps.undoStack);
    mSelection = new SelectionService(this);
    mPlayback = new PlaybackService(this);
    mPlayback->setViewport(deps.viewport);

    // The session log's PLAY START / PLAY STOP brackets (SESSION_LOG_SPEC §5)
    // ride the SAME signals — no new calls on the play path. The scene-open
    // block's stats lines come from here too, because LoadTimeline (a service)
    // has no way to reach a document.
    mMarkers = new SessionMarkers(this);
    mMarkers->attach(mPlayback);
    auto scene = deps.scene;
    LoadTimeline::setStatsProvider([scene] { return SessionMarkers::sceneStats(scene()); });

    // The PLAYER space (verb-coverage audit F1) — a different state machine
    // from the playback service's play-in-place. The editor page has already
    // built the backend when the engine is up; headless runs leave the host
    // null and the player.* verbs refuse cleanly.
    mPlayer = new PlayerService(this);
    mPlayer->setHost(deps.playerBackend);

    mProject = new ProjectService(db, project, deps.settings, deps.viewport, mUndo, scene);
    mProject->setPlayerHost(deps.playerBackend);   // a save asks the Player's run too (the document's edit pose)
    mSceneEdit = new SceneEditService(db, project, mUndo, mSelection, deps.viewport, scene, this);

    // THE CLIPBOARD (CLIPBOARD_SPEC D3 b) — one component, over the system
    // clipboard, for every space in the app. Constructed after the services it
    // drives (the node domain's fragments, the selection, the undo sink) and
    // handed to the shell, the verbs and the tree menu as one pointer.
    mClipboard = new ClipboardService(db, project, mSceneEdit, mSelection, mUndo, nullptr, this);

    // THE PREVIEWS (PREVIEWS-1): the one adapter that knows where the engine lives is this line.
    mPreviews = new PreviewService(db, [] { return EngineHost::instance().engine(); });
    // AN IMPORT's PICTURES through the one writer (PREVIEWS-1 stage 2): the library's sink, on THIS
    // library's instance, to the service's store door; a picture that is not stored is said once.
    mDb = db;
    if (db) {
        PreviewService *previews = mPreviews;
        db->setImportPictureSink([previews, project](const QString &guid, const QImage &picture) {
            previews->store(project, guid, picture, [guid](const PreviewService::Outcome &outcome) {
                if (!outcome.ok && !outcome.superseded && !outcome.nothingToDraw)
                    JahLog::write(JahLog::legacy, JahLog::Level::Warning, QStringLiteral("import: no preview stored for %1 - %2").arg(guid, outcome.reason));
            });
        });
    }
    // THE RENDER DEMAND (PREVIEWS-1 stage 2): what the shown views want and is missing or stale, read
    // by guid and requested here (a sweep in flight draws them itself: nothing is read meanwhile).
    {
        PreviewService *previews = mPreviews;
        mDemand = new PreviewDemand(
            [previews, project](const QStringList &guids) {
                QHash<QString, QString> need;
                if (previews->sweepRunning()) return need;
                for (const PreviewService::Row &row : previews->state(project, false, -1, guids))
                    if (row.state == PreviewService::State::Missing || row.state == PreviewService::State::Stale)
                        need.insert(row.guid, QStringLiteral("%1|%2").arg(QString::fromLatin1(PreviewService::stateName(row.state)), row.key));
                return need;
            },
            [previews, project](const QString &guid, std::function<void(bool)> done) {
                previews->request(project, guid,
                                  [done](const PreviewService::Outcome &o) { done(!o.ok && o.transient); });
            },
            previews::Writer::kQueueCap, this);
        PreviewDemand *demand = mDemand;
        TileCache::instance().setDemand(TileCache::Kind::Asset, [demand](const QStringList &wanted) { demand->want(wanted); });
    }
    mAssets = new AssetService(db, project);

    // THE HOVER PREVIEW (MATERIAL-PREVIEW-1). Constructed after the service
    // that resolves and applies materials, and handed BACK to it: every apply
    // ends a live preview before it pushes, so an undo step can never capture
    // a material the user only hovered.
    mPreview = new MaterialPreviewService(mSceneEdit);
    mSceneEdit->setMaterialPreview(mPreview);

    mAggregate = new StudioServices;
    mAggregate->eventBus = new Subscriber(this);
    mAggregate->undo = mUndo;
    mAggregate->selection = mSelection;
    mAggregate->playback = mPlayback;
    mAggregate->player = mPlayer;
    mAggregate->project = mProject;
    mAggregate->sceneEdit = mSceneEdit;
    mAggregate->materialPreview = mPreview;
    mAggregate->clipboard = mClipboard;
    mAggregate->previews = mPreviews;
    mAggregate->previewDemand = mDemand;
    mSceneEdit->setPreviews(mPreviews);
    mAggregate->assets = mAssets;
    // THE ONE RESOLVER (ruling 15): the project's pin, else the library's bytes, on this shell's library.
    if (db) {
        mAggregate->resolvePinned = [db](const QString &projectGuid, const QString &guid) {
            return AssetCas::resolvePinned(db->connection(), AssetStorePaths::root(), projectGuid, guid);
        };
    }
    // THE WORLD'S MUSIC (audit D8): a Music guid resolves the way every bound
    // asset does — through the resolver above.
    mAmbience = new AmbienceService([project, aggregate = mAggregate](const QString &guid) {
        if (!project || project->getProjectGuid().isEmpty() || !aggregate->resolvePinned) return QString();
        return aggregate->resolvePinned(project->getProjectGuid(), guid);
    }, this);
    mAggregate->ambience = mAmbience;
    // A world that opens with music starts it here — the reader plays nothing.
    mAggregate->onSceneOpened([this, scene]() { mAmbience->sync(scene()); });

    // The perf sampler (SESSION_LOG_SPEC §8-R3). Started HERE, from the
    // settings, so it is running long before anything the owner does — a
    // sampler a user has to turn on has already missed the session that
    // needed it.
    mPerf = new PerfSampler(this);
    mAggregate->perfSampler = mPerf;
    mPerf->startFromSettings();

    // Commands raise their refreshes through the aggregate (stamped at push);
    // the viewport's gizmos push through the same aggregate.
    mUndo->setServices(mAggregate);
    // THE DEFERRED DATABASE WORK OF THE COMMANDS A CLEAR DESTROYS (CLOSE-1).
    // A command's destructor queues its asset-row cleanup instead of writing
    // it — one transaction for the whole stack, here, instead of one
    // transaction and one fdatasync per command on the UI thread.
    mUndo->setDeferredFlushHook([db]() {
        if (db) db->flushPendingAssetDeletes();
    });
    // THE HOVER PREVIEW ENDS BEFORE ANYTHING COMMITS (MATERIAL-PREVIEW-1).
    // Two hooks, at the two spines: every undo push, and every scene write.
    // Between them they cover the whole "a material on screen that the
    // document does not hold" hazard — including the callers written after
    // this — and the three LIFECYCLE ends (close, space switch, quit) are
    // spelled out at their own sites, where a hook would have nothing to hang
    // on.
    mUndo->setPrePushHook([this]() { if (mPreview) mPreview->end(); });
    mProject->setPreWriteHook([this]() { if (mPreview) mPreview->end(); });
}

void ShellServices::destroyPlain()
{
    delete mAggregate;
    mAggregate = nullptr;
    delete mPreview;
    mPreview = nullptr;
    delete mProject;
    mProject = nullptr;
    // Its writer DRAINS here and joins - before the library closes (shutdown step 7). The import
    // sink and the render demand that name it go first.
    if (mDb) mDb->setImportPictureSink({});
    TileCache::instance().setDemand(TileCache::Kind::Asset, {});
    delete mDemand;
    mDemand = nullptr;
    delete mPreviews;
    mPreviews = nullptr;
    delete mAssets;
    mAssets = nullptr;
    delete mUndo;
    mUndo = nullptr;
}
