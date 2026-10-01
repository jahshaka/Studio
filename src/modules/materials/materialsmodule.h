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

namespace materials { class EffectsPage; }

class MaterialsModule : public StudioModule
{
public:
    QString id() const override { return QStringLiteral("materials"); }

    /// Builds the Effects page from the host context: db, engine-rendered
    /// Display preview (when the engine runs), scene-open probe and project.
    void initialize(StudioContext &ctx) override;
    /// The page, and the chords the graph answers on this space: Space opens
    /// the node search, F frames the selection, H resets the zoom.
    void contribute(Contributions &c) override;
    void registerApi(ScriptEngine &engine) override;
    /// The page's open tabs are per project (MATERIALS_TABS_SPEC §2.7).
    void onProjectChanged(Project *project) override;
    /// Entering the space re-reads the graph.
    void onSpaceChanged(const QString &from, const QString &to) override;
    /// The OPEN TAB's stack and the graph's own edit chords. Cut and select-all
    /// are deliberately unanswered: the graph has neither, and a chord must
    /// never act on a selection the user cannot see.
    EditTarget editTarget() override;
    void shutdown() override {}

private:
    StudioContext host;
    materials::EffectsPage *page = nullptr;
};

#endif // MATERIALSMODULE_H
