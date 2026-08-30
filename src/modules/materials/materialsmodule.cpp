/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/materials/materialsmodule.h"

#include "bridge/enginehost.h"
#include "bridge/enginematerialpreview.h"
#include "modules/materials/api/materialsapi.h"
#include "modules/materials/effectspage.h"
#include "scripting/scriptengine.h"
#include "services/projectservice.h"
#include "services/services.h"

void MaterialsModule::initialize(ModuleHost &host)
{
    this->host = host;

    page = new materials::EffectsPage(host.shellWidget, host.db);

    // The Display dock gets an engine-rendered preview (its own engine Scene +
    // preview document) when the engine runs; headless hosts get none.
    if (host.engine && host.engine->isRunning())
        page->setEnginePreview(new EngineMaterialPreview(host.engine->engine(),
                                                         host.engine->driver(), page));

    if (host.services && host.services->project) {
        auto *projectService = host.services->project;
        page->setSceneOpenProbe([projectService]() { return projectService->isSceneOpen(); });
    }
    page->setProject(host.project);
}

QWidget *MaterialsModule::createPage()
{
    return page;
}

void MaterialsModule::registerApi(ScriptEngine &engine)
{
    // Registered exactly as registerStudioModules used to: materials first
    // (it fronts graph), then material, then graph — the registry order (and
    // docs/SCRIPTING.md) is unchanged.
    auto &host = engine.scriptHost();
    auto *graphApi = new GraphApi(host);
    auto *materialsApi = new MaterialsApi(host);
    materialsApi->setGraphModule(graphApi);
    engine.addModule(materialsApi);
    engine.addModule(new MaterialApi(host));
    engine.addModule(graphApi);
}

void MaterialsModule::setAssetView(AssetView *assetView)
{
    if (page) page->setAssetView(assetView);
}
