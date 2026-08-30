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

class UndoService;
class SelectionService;
class PlaybackService;
class ProjectService;
class SceneEditService;
class ThumbnailService;
class AssetService;

struct StudioServices
{
    UndoService      *undo       = nullptr;
    SelectionService *selection  = nullptr;
    PlaybackService  *playback   = nullptr;
    ProjectService   *project    = nullptr;
    SceneEditService *sceneEdit  = nullptr;
    ThumbnailService *thumbnails = nullptr;
    AssetService     *assets     = nullptr;
};

#endif // SERVICES_H
