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
#include "services/presetedit.h"
#include "services/undoservice.h"
#include "services/sceneeditservice.h"
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
    // The open TAB SET lives in the app's settings, per project (§2.7).
    page->setSettings(host.settings);

    // A GRAPH EDIT REACHES THE SCENE (OWNER_REVIEW 9, R19 D2). The page
    // commits a definition; this puts it on every mesh already wearing that
    // material, through the ONE apply path the drop and `material.apply` use.
    if (host.services && host.services->sceneEdit) {
        auto *sceneEdit = host.services->sceneEdit;
        page->mMaterialChanged = [sceneEdit](const QString &materialGuid) {
            sceneEdit->refreshMaterialUsers(materialGuid);
        };
    }

    // A PRESET A PROJECT HOLDS IS EDITABLE THERE, AND THE FIRST EDIT MAKES
    // THE COPY (PRESET-EDIT-1). The page commits definitions and has neither
    // an undo service nor a scene; the copy-on-write needs both (it is ONE
    // undo step, and it re-points the meshes wearing the master), so the
    // shell hands the page the one call. The verb `materials.edit` is the
    // same call — one implementation for the click and the script.
    {
        Database *db = host.db;
        Project *project = host.project;
        auto *projectService = host.services ? host.services->project : nullptr;
        auto *undo = host.services ? host.services->undo : nullptr;
        auto *sceneEdit = host.services ? host.services->sceneEdit : nullptr;
        page->mMakeEditable = [db, project, projectService, undo, sceneEdit](const QString &guid) {
            // THE PROJECT ONLY WHEN ONE IS REALLY OPEN (presetedit.h): the one
            // live Project instance keeps its guid after a close.
            const bool open = projectService && projectService->isSceneOpen()
                              && project && !project->getProjectGuid().isEmpty();
            return presetedit::forEdit(db, open ? project : nullptr, guid, undo, sceneEdit);
        };
    }
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

        // F2: graph.removeNode / graph.disconnect on the page's canvas go
        // through that same stack (the page pushes the canvas's own delete
        // commands), so a scripted deletion is undoable like a clicked one.
        GraphApi::EditDelegate editDelegate;
        editDelegate.removeNode = [effectsPage](const QString &id) {
            return effectsPage->removeGraphNode(id);
        };
        editDelegate.removeConnection = [effectsPage](const QString &id) {
            return effectsPage->removeGraphConnection(id);
        };
        graphApi->setEditDelegate(editDelegate);

        // graph.paletteTile — where a node tile IS, so the rig can drag it
        // instead of aiming at a fraction of the window (hygiene lane,
        // 2026-09-09; app.input_keys's palette drag).
        GraphApi::PaletteDelegate paletteDelegate;
        paletteDelegate.tile = [effectsPage](const QString &name) {
            return effectsPage->paletteTileRect(name);
        };
        graphApi->setPaletteDelegate(paletteDelegate);

        // materials.open / tabs / activate / closeTab / activeTab — THE TABS
        // (MATERIALS_TABS_SPEC §3). The five verbs and the tab bar call the
        // same five page methods, so a click and a verb cannot disagree about
        // what "open a material" means.
        MaterialsApi::PageDelegate pageDelegate;
        pageDelegate.open = [effectsPage](const QString &guid, const QString &scope) {
            return effectsPage->openMaterialTab(guid, scope);
        };
        pageDelegate.newMaterial = [effectsPage](const QString &preset, const QString &name) {
            return effectsPage->newMaterialTab(preset, name);
        };
        pageDelegate.tabs = [effectsPage]() { return effectsPage->materialTabs(); };
        pageDelegate.activate = [effectsPage](const QVariant &ref) {
            return effectsPage->activateMaterialTab(ref);
        };
        pageDelegate.closeTab = [effectsPage](const QVariant &ref) {
            return effectsPage->closeMaterialTab(ref);
        };
        pageDelegate.activeTab = [effectsPage]() { return effectsPage->activeMaterialTab(); };
        // materials.projectDrawer — what the module's Project drawer is
        // showing, straight off the widget (DRAWERS-1).
        pageDelegate.projectDrawer = [effectsPage]() { return effectsPage->projectDrawerTiles(); };
        materialsApi->setPageDelegate(pageDelegate);
    }
    engine.addModule(materialsApi);
    engine.addModule(new MaterialApi(host));
    engine.addModule(graphApi);
}

void MaterialsModule::setAssetView(AssetView *assetView)
{
    if (page) page->setAssetView(assetView);
}
