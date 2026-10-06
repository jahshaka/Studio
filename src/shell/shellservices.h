/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SHELLSERVICES_H
#define SHELLSERVICES_H

// ShellServices — THE SERVICE LAYER, COMPOSED (APP_ARCHITECTURE_AUDIT §3.3;
// D10-SHELL-MODULES). The shell constructs the services in dependency order,
// hooks them to EACH OTHER (the undo sink and the aggregate, the hover preview
// ending before any commit, the deferred database work of the commands a clear
// destroys, the session log's markers and the perf sampler) and hands the
// aggregate to the pages, the modules and the scripting host. Phase 4
// dissolved the UiManager hub: the services own the state their statics used
// to hold. What a service's signal does to a WIDGET is the window's wiring,
// not this.

#include <QObject>

#include <functional>

#include "irisgl/irisglfwd.h"

class AmbienceService;
class AssetService;
class ClipboardService;
class Database;
class EnginePlayerView;
class IEditorViewport;
class MaterialPreviewService;
class PerfSampler;
class PlaybackService;
class PlayerService;
class Project;
class ProjectService;
class QUndoStack;
class SceneEditService;
class SelectionService;
class SessionMarkers;
class SettingsManager;
class ThumbnailService;
class UndoService;
struct StudioServices;

class ShellServices : public QObject
{
    Q_OBJECT
public:
    struct Deps {
        QUndoStack *undoStack = nullptr;
        Database *db = nullptr;
        Project *project = nullptr;
        SettingsManager *settings = nullptr;
        IEditorViewport *viewport = nullptr;
        EnginePlayerView *playerBackend = nullptr;
        /// The window's open scene (the services read the document through it).
        std::function<iris::ScenePtr()> scene;
    };
    ShellServices(const Deps &deps, QObject *parent = nullptr);

    /// The step-5 teardown of the services that are not QObjects of the
    /// window: the aggregate, the hover preview (before the scene-edit service
    /// it points at, and before the scene dies — it puts any borrowed material
    /// back), the project, thumbnail, asset and undo services.
    void destroyPlain();

    StudioServices *aggregate() const { return mAggregate; }
    UndoService *undo() const { return mUndo; }
    SelectionService *selection() const { return mSelection; }
    PlaybackService *playback() const { return mPlayback; }
    PlayerService *player() const { return mPlayer; }
    ProjectService *project() const { return mProject; }
    SceneEditService *sceneEdit() const { return mSceneEdit; }
    MaterialPreviewService *materialPreview() const { return mPreview; }
    ClipboardService *clipboard() const { return mClipboard; }
    ThumbnailService *thumbnails() const { return mThumbnails; }
    AssetService *assets() const { return mAssets; }
    AmbienceService *ambience() const { return mAmbience; }

private:
    StudioServices *mAggregate = nullptr;
    UndoService *mUndo = nullptr;
    SelectionService *mSelection = nullptr;
    PlaybackService *mPlayback = nullptr;
    SessionMarkers *mMarkers = nullptr;
    PlayerService *mPlayer = nullptr;
    ProjectService *mProject = nullptr;
    SceneEditService *mSceneEdit = nullptr;
    MaterialPreviewService *mPreview = nullptr;
    ClipboardService *mClipboard = nullptr;
    ThumbnailService *mThumbnails = nullptr;
    AssetService *mAssets = nullptr;
    AmbienceService *mAmbience = nullptr;
    PerfSampler *mPerf = nullptr;
};

#endif // SHELLSERVICES_H
