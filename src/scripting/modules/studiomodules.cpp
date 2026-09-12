/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/studiomodules.h"

#include "scripting/scriptengine.h"
#include "scripting/modules/animapi.h"
#include "scripting/modules/clipboardapi.h"
#include "scripting/modules/appapi.h"
#include "scripting/modules/assetsapi.h"
#include "scripting/modules/cameraapi.h"
#include "scripting/modules/desktopapi.h"
#include "scripting/modules/editorapi.h"
#include "scripting/modules/inputapi.h"
#include "scripting/modules/logapi.h"
#include "scripting/modules/nodeapi.h"
#include "scripting/modules/particlesapi.h"
#include "scripting/modules/perfapi.h"
#include "scripting/modules/projectapi.h"
#include "scripting/modules/sceneapi.h"
#include "scripting/modules/textureapi.h"
#include "scripting/modules/videoapi.h"
#include "scripting/modules/worldapi.h"

void registerStudioModules(ScriptEngine &engine)
{
    auto &host = engine.scriptHost();
    engine.addModule(new ProjectApi(host));
    engine.addModule(new SceneApi(host));
    engine.addModule(new NodeApi(host));
    engine.addModule(new EditorApi(host));
    engine.addModule(new AppApi(host));
    engine.addModule(new DesktopApi(host));
    engine.addModule(new WorldApi(host));
    engine.addModule(new AssetsApi(host));
    engine.addModule(new ParticlesApi(host));
    engine.addModule(new AnimApi(host));
    // Scene cameras (CAMERAS_SPEC §6). Appended, so the registry order every
    // generated doc and tool schema already has stays unchanged.
    engine.addModule(new CameraApi(host));
    // Gameplay input (AVATAR_LOCOMOTION_SPEC §8.2). Appended for the same
    // reason CameraApi was: the registry order every generated doc and tool
    // schema already has stays unchanged.
    engine.addModule(new InputApi(host));
    // The session log (SESSION_LOG_SPEC §7). Appended for the same reason
    // CameraApi and InputApi were: the registry order every generated doc and
    // tool schema already has stays unchanged.
    engine.addModule(new LogApi(host));
    // Live textures and the video that drives them (MATERIAL_GAPS_SPEC A-1).
    // Appended for the same reason CameraApi, InputApi and LogApi were: the
    // registry order every generated doc and tool schema already has stays
    // unchanged.
    engine.addModule(new TextureApi(host));
    engine.addModule(new VideoApi(host));
    // The deep clipboard (CLIPBOARD_SPEC §5). Appended for the same reason
    // CameraApi, InputApi and LogApi were: the registry order every generated
    // doc and tool schema already has stays unchanged.
    engine.addModule(new ClipboardApi(host));
    // The render-loop monitor's capture verbs (RENDER_LOOP_MONITOR_SPEC §4.6).
    // Appended for the same reason every module since CameraApi was: the
    // registry order every generated doc and tool schema already has stays
    // unchanged.
    engine.addModule(new PerfApi(host));
    // The materials/material/graph verbs are the materials module's — the
    // shell's module loop calls MaterialsModule::registerApi right after this
    // (audit §6.3.4), keeping the registry order unchanged.
}
