/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_MATERIALSAPI_H
#define SCRIPTING_MATERIALSAPI_H

// materials.* / material.* / graph.* — the material system and the Effects
// module (SCRIPTING_SPEC §1.4).
//
// The graph module drives the shadergraph headlessly: NodeGraph +
// PbrGraphEvaluator were built script-first (the evaluator is GL-free by
// design) — the bridge here is glue. graph.* operates on ONE current graph,
// opened by materials.loadGraph or created by materials.createGraph.

#include <QVariantList>
#include <QVariantMap>
#include <functional>

#include "scripting/apimodule.h"
#include "irisgl/irisglfwd.h"

class NodeGraph;
class GraphApi;

/// materials.* — presets and effect-graph assets.
class MaterialsApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("materials"); }
    QVector<VerbInfo> verbs() const override;

    /// loadGraph hands the deserialized graph to the graph.* module.
    void setGraphModule(GraphApi *graphApi) { mGraphApi = graphApi; }

    Q_INVOKABLE QVariantList presets();
    Q_INVOKABLE QString createGraph(const QString &name);
    Q_INVOKABLE QVariantMap loadGraph(const QString &guidOrPath);
    Q_INVOKABLE bool regenerate(const QString &shaderGuid);

private:
    GraphApi *mGraphApi = nullptr;
};

/// material.* — the material on one scene node.
class MaterialApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("material"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE bool apply(const QString &nodeId, const QString &presetOrGuid);
    Q_INVOKABLE bool set(const QString &nodeId, const QVariantMap &values);
    Q_INVOKABLE QVariantMap get(const QString &nodeId);

private:
    iris::MeshNodePtr meshNodeOrFail(const QString &nodeId, const QString &verb);
};

/// graph.* — the current effect graph (see MaterialsApi::loadGraph).
class GraphApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("graph"); }
    QVector<VerbInfo> verbs() const override;

    /// Adopts a graph (called by MaterialsApi); takes ownership of the old one.
    void setCurrent(NodeGraph *graph, const QString &assetGuid);
    NodeGraph *current() const { return mGraph; }

    /// §3a: when the Effects page is alive its scene owns selection — the
    /// module wires these to EffectsPage::selectGraphNode & co. Verbs fall
    /// back to API-local selection on the current script graph when the
    /// page has no node with the id (or no delegate is set: headless slices).
    struct SelectionDelegate {
        std::function<bool(const QString &)> select;
        std::function<QString()> selected;
        std::function<void()> deselect;
    };
    void setSelectionDelegate(const SelectionDelegate &delegate) { mSelection = delegate; }

    Q_INVOKABLE QVariantList nodes();
    Q_INVOKABLE QVariantList nodeTypes();
    Q_INVOKABLE QString addNode(const QString &type);
    Q_INVOKABLE bool connect(const QString &fromId, const QVariant &fromSocket,
                             const QString &toId, const QVariant &toSocket);
    Q_INVOKABLE bool setValue(const QString &nodeId, const QVariant &value);
    Q_INVOKABLE QVariant getValue(const QString &nodeId);
    Q_INVOKABLE QVariantMap evaluate();
    Q_INVOKABLE QVariantMap bakeInfo();
    Q_INVOKABLE QVariantMap bake(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE bool toMaterial(const QString &nodeId);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool selectNode(const QString &nodeId);
    Q_INVOKABLE QVariant selectedNode();
    Q_INVOKABLE bool deselect();

private:
    NodeGraph *graphOrFail(const QString &verb);

    NodeGraph *mGraph = nullptr;
    QString mAssetGuid;
    QString mSelectedNodeId;      // API-local selection (headless fallback)
    SelectionDelegate mSelection; // the Effects page, when wired
};

#endif // SCRIPTING_MATERIALSAPI_H
