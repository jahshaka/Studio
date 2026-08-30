/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALSMODULE_H
#define MATERIALSMODULE_H

// MaterialsModule — the materials/effects domain as a StudioModule (audit
// §6.3): the retrofitted shadergraph. Owns the Effects page (the node-based
// material editor) and registers the materials/material/graph API verbs.

#include "modules/studiomodule.h"

class AssetView;
namespace materials { class EffectsPage; }

class MaterialsModule : public StudioModule
{
public:
    QString id() const override { return QStringLiteral("materials"); }

    /// Builds the Effects page from the host context: db, engine-rendered
    /// Display preview (when the engine runs), scene-open probe and project.
    void initialize(ModuleHost &host) override;
    QWidget *createPage() override;
    void registerApi(ScriptEngine &engine) override;
    void shutdown() override {}

    /// The live page, for the shell's direct calls (refresh on page switch,
    /// asset-widget database wiring).
    materials::EffectsPage *effectsPage() const { return page; }

    /// Materials-specific shell wiring: the module's export flows land assets
    /// in the Assets page's browser.
    void setAssetView(AssetView *assetView);

private:
    ModuleHost host;
    materials::EffectsPage *page = nullptr;
};

#endif // MATERIALSMODULE_H
