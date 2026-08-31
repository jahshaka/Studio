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

private:
    NodeGraph *graphOrFail(const QString &verb);

    NodeGraph *mGraph = nullptr;
    QString mAssetGuid;
};

#endif // SCRIPTING_MATERIALSAPI_H
