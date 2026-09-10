/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SERVICES_H
#define SERVICES_H

// StudioServices — the service layer's front door (APP_ARCHITECTURE_AUDIT §3.3).
//
// One aggregate of non-owning pointers, filled by the shell (MainWindow) after
// it constructs the services, and handed to every consumer that used to reach
// for MainWindow directly: the scripting ApiModules receive it through
// ScriptHost::services; panels receive it as they are touched (§2.3's
// opportunistic rule). All members are nullable — headless/test hosts fill in
// what they have, and consumers must null-check exactly as they null-checked
// host.mainWindow before.
//
// The services themselves live beside this header. They are constructor-
// injected (no service reads Globals/UiManager for a dependency a constructor
// can hand it) and UI-free: widget side effects happen in the shell, driven by
// the QObject services' signals.

class Subscriber;
class UndoService;
class SelectionService;
class PlaybackService;
class PlayerService;
class ProjectService;
class SceneEditService;
class ClipboardService;
class ThumbnailService;
class AssetService;
class PerfSampler;

#include <functional>
#include <vector>

struct StudioServices
{
    UndoService      *undo       = nullptr;
    SelectionService *selection  = nullptr;
    PlaybackService  *playback   = nullptr;
    /// The PLAYER SPACE (verb-coverage audit F1) — a different thing from
    /// `playback`, which is the editor's play-in-place. Null in headless runs.
    PlayerService    *player     = nullptr;
    ProjectService   *project    = nullptr;
    SceneEditService *sceneEdit  = nullptr;
    /// THE clipboard (CLIPBOARD_SPEC D3 b) — one component for every space,
    /// over the system clipboard. Null in hosts with no library.
    ClipboardService *clipboard  = nullptr;
    ThumbnailService *thumbnails = nullptr;
    AssetService     *assets     = nullptr;
    /// The session log's periodic performance sampler (SESSION_LOG_SPEC §8-R3).
    /// Behind log.perf / log.sample; null in hosts with no shell.
    PerfSampler      *perfSampler = nullptr;

    /// UI event bus (sky-asset updates between panels). Owned by the shell;
    /// was the Globals::eventSubscriber static (Phase 4 injected it).
    Subscriber       *eventBus   = nullptr;

    // ---- SCENE OPENED (AVATAR_ASSET_SPEC §4 D4, the load-time half) -------
    //
    // Fired once the freshly-read scene is INSTALLED (MainWindow::openStageBind,
    // after setScene) — not when the reader returns, because a subscriber's
    // whole job is to walk the scene that is now open.
    //
    // It exists because a document can be STALE the moment it is loaded: an
    // avatar instance records the definition version it last resolved, and a
    // scene saved before a module save comes back naming an older one. The pin
    // signal covers "the asset changed while the scene was open"; this covers
    // "the scene arrived after the asset changed", which is the same defect
    // seen from the other end.
    //
    // A plain callback list rather than a Qt signal, exactly like
    // AssetService::onPinChanged: this struct is a bundle of injected pointers
    // with no QObject in it. Subscriptions live for the session.
    using SceneOpenedFn = std::function<void()>;
    void onSceneOpened(SceneOpenedFn callback)
    {
        if (callback) sceneOpenedSubscribers.push_back(std::move(callback));
    }
    void announceSceneOpened()
    {
        // Iterate a COPY: a subscriber that reacts by editing the scene must
        // not invalidate the list mid-loop.
        const auto subscribers = sceneOpenedSubscribers;
        for (const auto &callback : subscribers) callback();
    }

private:
    std::vector<SceneOpenedFn> sceneOpenedSubscribers;
};

#endif // SERVICES_H
