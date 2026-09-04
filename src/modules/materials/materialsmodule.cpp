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

    // §3a: graph.selectNode/selectedNode/deselect drive the Effects page's
    // canvas selection (and with it the properties panel) whenever the page
    // knows the node id; headless graphs keep an API-local selection.
    if (page) {
        GraphApi::SelectionDelegate delegate;
        auto *effectsPage = page;
        delegate.select = [effectsPage](const QString &id) { return effectsPage->selectGraphNode(id); };
        delegate.selected = [effectsPage]() { return effectsPage->selectedGraphNodeId(); };
        delegate.deselect = [effectsPage]() { effectsPage->deselectGraphNodes(); };
        graphApi->setSelectionDelegate(delegate);

        // graph.undo/graph.redo — the page's ONE edit-stack entry point, the
        // same one the shell's Ctrl+Z reaches while the Materials space is
        // active (the owner's 2026-09 decision that the graph undo wins there).
        GraphApi::UndoDelegate undoDelegate;
        undoDelegate.undo      = [effectsPage]() { return effectsPage->graphUndo(); };
        undoDelegate.redo      = [effectsPage]() { return effectsPage->graphRedo(); };
        undoDelegate.undoCount = [effectsPage]() { return effectsPage->graphUndoCount(); };
        undoDelegate.redoCount = [effectsPage]() { return effectsPage->graphRedoCount(); };
        graphApi->setUndoDelegate(undoDelegate);
    }
    engine.addModule(materialsApi);
    engine.addModule(new MaterialApi(host));
    engine.addModule(graphApi);
}

void MaterialsModule::setAssetView(AssetView *assetView)
{
    if (page) page->setAssetView(assetView);
}
