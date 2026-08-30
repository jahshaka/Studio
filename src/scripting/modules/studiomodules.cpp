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
#include "scripting/modules/appapi.h"
#include "scripting/modules/assetsapi.h"
#include "scripting/modules/editorapi.h"
#include "scripting/modules/materialsapi.h"
#include "scripting/modules/nodeapi.h"
#include "scripting/modules/projectapi.h"
#include "scripting/modules/sceneapi.h"
#include "scripting/modules/worldapi.h"

void registerStudioModules(ScriptEngine &engine)
{
    auto &host = engine.scriptHost();
    engine.addModule(new ProjectApi(host));
    engine.addModule(new SceneApi(host));
    engine.addModule(new NodeApi(host));
    engine.addModule(new EditorApi(host));
    engine.addModule(new AppApi(host));
    engine.addModule(new WorldApi(host));
    engine.addModule(new AssetsApi(host));

    auto *graphApi = new GraphApi(host);
    auto *materialsApi = new MaterialsApi(host);
    materialsApi->setGraphModule(graphApi);
    engine.addModule(materialsApi);
    engine.addModule(new MaterialApi(host));
    engine.addModule(graphApi);
}
