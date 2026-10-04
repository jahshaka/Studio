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
#include "services/assetservice.h"
#include "services/clipboardservice.h"
#include "services/loadtimeline.h"
#include "services/materialpreviewservice.h"
#include "services/perfsampler.h"
#include "services/playbackservice.h"
#include "player/engineplayerview.h"
#include "services/playerservice.h"
#include "services/projectservice.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "services/services.h"
#include "services/sessionmarkers.h"
#include "services/subscriber.h"
#include "services/thumbnailgenerator.h"
#include "services/thumbnailservice.h"
#include "services/undoservice.h"

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
    mSceneEdit = new SceneEditService(db, project, mUndo, mSelection, deps.viewport, scene, this);

    // THE CLIPBOARD (CLIPBOARD_SPEC D3 b) — one component, over the system
    // clipboard, for every space in the app. Constructed after the services it
    // drives (the node domain's fragments, the selection, the undo sink) and
    // handed to the shell, the verbs and the tree menu as one pointer.
    mClipboard = new ClipboardService(db, project, mSceneEdit, mSelection, mUndo, nullptr, this);

    mThumbnails = new ThumbnailService(db, project);
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
    mAggregate->thumbnails = mThumbnails;
    mAggregate->assets = mAssets;

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

    ThumbnailGenerator::getSingleton()->setProject(project);
}

void ShellServices::destroyPlain()
{
    delete mAggregate;
    mAggregate = nullptr;
    delete mPreview;
    mPreview = nullptr;
    delete mProject;
    mProject = nullptr;
    delete mThumbnails;
    mThumbnails = nullptr;
    delete mAssets;
    mAssets = nullptr;
    delete mUndo;
    mUndo = nullptr;
}
