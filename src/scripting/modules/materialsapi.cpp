/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "materialsapi.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "moduleshared.h"
#include "../../constants.h"
#include "../../core/database/database.h"
#include "../../core/guidmanager.h"
#include "../../core/materialpreset.h"
#include "../../core/project.h"
#include "../../commands/changematerialpropertycommand.h"
#include "../../engine/enginehost.h"
#include "../../globals.h"
#include "../../io/assetmanager.h"
#include "../../io/materialpresetreader.h"
#include "../../mainwindow.h"
#include "../../services/services.h"
#include "../../services/undoservice.h"
#include "../../irisgl/src/core/irisutils.h"
#include "../../irisgl/src/core/property.h"
#include "../../irisgl/src/materials/custommaterial.h"
#include "../../irisgl/src/materials/pbrmaterial.h"
#include "../../irisgl/src/scenegraph/meshnode.h"

#include "../../shadergraph/core/materialhelper.h"
#include "../../shadergraph/core/pbrgraphevaluator.h"
#include "../../shadergraph/graph/nodegraph.h"
#include "../../shadergraph/models/libraryv1.h"
#include "../../shadergraph/models/nodemodel.h"
#include "../../shadergraph/models/socketmodel.h"
#include "../../shadergraph/nodes/pbrmasternode.h"

using namespace scriptmod;

namespace {

QVector<MaterialPreset> loadPresets()
{
    QVector<MaterialPreset> presets;
    const QDir dir(IrisUtils::getAbsoluteAssetPath("app/content/materials"));
    MaterialPresetReader reader;
    const bool pbrOnly = EngineHost::viewportBackend() == ViewportBackend::Engine;
    for (const auto &file : dir.entryInfoList(QStringList(), QDir::Files)) {
        auto preset = reader.readMaterialPreset(file.absoluteFilePath());
        if (pbrOnly && preset.type.compare("PBR", Qt::CaseInsensitive) != 0) continue;
        presets.append(preset);
    }
    return presets;
}

} // namespace

// ---------------------------------------------------------------- materials.*

QVector<VerbInfo> MaterialsApi::verbs() const
{
    return {
        { "presets", "materials.presets() -> [{name, type, guid}]",
          "The built-in material presets (PBR only in engine mode); guid is the reserved id when one exists.",
          Needs::Document },
        { "createGraph", "materials.createGraph(name) -> guid",
          "Creates a new effect-graph asset in the open project from the shader template and opens it as the current graph.",
          Needs::Document },
        { "loadGraph", "materials.loadGraph(guidOrPath) -> {nodes, master}",
          "Opens an effect graph (a Shader asset guid, or a .effect/.shader file path) as the current graph for graph.* verbs.",
          Needs::Document },
    };
}

QVariantList MaterialsApi::presets()
{
    QVariantList out;
    for (const auto &preset : loadPresets()) {
        out.append(QVariantMap{
            { "name", preset.name },
            { "type", preset.type },
            { "guid", Constants::Reserved::DefaultMaterials.key(preset.name) } });
    }
    return out;
}

QString MaterialsApi::createGraph(const QString &name)
{
    if (!host.db) { fail("materials: not available in this session"); return QString(); }
    if (!requireProject()) return QString();
    if (name.trimmed().isEmpty()) { fail("materials.createGraph: a name is required"); return QString(); }

    // ShaderAssetWidget::createShader minus the QListWidget bookkeeping.
    const QString parentFolder = Globals::project->getProjectGuid();
    QString shaderName = name.trimmed();
    const QStringList existing = host.db->fetchAssetNameByParent(parentFolder);
    int increment = 1;
    while (existing.contains(IrisUtils::buildFileName(shaderName, "shader")))
        shaderName = QStringLiteral("%1 %2").arg(name.trimmed()).arg(increment++);

    const QString assetGuid = GUIDManager::generateGUID();
    host.db->createAssetEntry(assetGuid, IrisUtils::buildFileName(shaderName, "shader"),
                              static_cast<int>(ModelTypes::Shader), parentFolder);

    // A minimal graph with a PbrMaterial master — the current Effects format
    // (the old ShaderTemplate.shader predates the graph and cannot be reopened
    // by the graph loader; the .effect presets are the live shape).
    auto *graph = new NodeGraph;
    graph->setNodeLibrary(new LibraryV1());
    auto *master = new PbrMasterNode();
    graph->addNode(master);
    graph->setMasterNode(master);
    MaterialSettings settings;
    settings.name = shaderName;
    graph->setMaterialSettings(settings);

    QJsonObject definition = MaterialHelper::serialize(graph);
    definition["name"] = shaderName;
    definition.insert("guid", assetGuid);
    host.db->updateAssetAsset(assetGuid, QJsonDocument(definition).toJson());

    auto assetShader = new AssetMaterial;
    assetShader->fileName = shaderName;
    assetShader->assetGuid = assetGuid;
    assetShader->path = IrisUtils::join(Globals::project->getProjectFolder(),
                                        IrisUtils::buildFileName(shaderName, "shader"));
    assetShader->setValue(QVariant::fromValue(definition));
    AssetManager::addAsset(assetShader);

    // Adopt the freshly built graph as the current one directly.
    if (mGraphApi) mGraphApi->setCurrent(graph, assetGuid);
    return assetGuid;
}

QVariantMap MaterialsApi::loadGraph(const QString &guidOrPath)
{
    QVariantMap out;
    if (!mGraphApi) { fail("materials.loadGraph: no graph module"); return out; }

    QJsonObject definition;
    QString assetGuid;
    if (QFileInfo::exists(guidOrPath)) {
        QFile file(guidOrPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            fail(QStringLiteral("materials.loadGraph: cannot open '%1'").arg(guidOrPath));
            return out;
        }
        definition = QJsonDocument::fromJson(file.readAll()).object();
    } else if (host.db) {
        const QByteArray blob = host.db->fetchAssetData(guidOrPath);
        if (blob.isEmpty()) {
            fail(QStringLiteral("materials.loadGraph: no shader asset or file '%1'").arg(guidOrPath));
            return out;
        }
        definition = QJsonDocument::fromJson(blob).object();
        assetGuid = guidOrPath;
    } else {
        fail("materials: not available in this session");
        return out;
    }

    if (!definition.contains("shadergraph")) {
        fail("materials.loadGraph: the definition has no 'shadergraph' object");
        return out;
    }
    // The real loader path (pixel-parity-tested): deserialize with LibraryV1.
    NodeGraph *graph = NodeGraph::deserialize(definition["shadergraph"].toObject(), new LibraryV1());
    mGraphApi->setCurrent(graph, assetGuid);

    out["nodes"] = graph->nodes.size();
    out["master"] = graph->masterNode ? graph->masterNode->typeName : QString();
    out["name"] = definition.value("name").toString();
    return out;
}

// ----------------------------------------------------------------- material.*

QVector<VerbInfo> MaterialApi::verbs() const
{
    return {
        { "apply", "material.apply(nodeId, presetOrGuid) -> bool",
          "Applies a built-in preset (by name or reserved guid) to a mesh node. Also registers the material as a project asset, like the presets panel.",
          Needs::Document },
        { "set", "material.set(nodeId, {baseColor, roughness, metallic, baseColorMap, ...}) -> bool",
          "Sets material properties on a mesh node (PBR keys; *Map keys take texture paths or asset guids). Undoable per property.",
          Needs::Document },
        { "get", "material.get(nodeId) -> {property: value}",
          "Reads the node material's editor-facing properties.",
          Needs::Document },
    };
}

iris::MeshNodePtr MaterialApi::meshNodeOrFail(const QString &nodeId, const QString &verb)
{
    auto scene = host.mainWindow ? host.mainWindow->getScene() : iris::ScenePtr();
    if (!scene) {
        fail(QStringLiteral("%1: no scene is open").arg(verb));
        return iris::MeshNodePtr();
    }
    auto node = findNodeByGuid(scene->getRootNode(), nodeId);
    if (!node || node->getSceneNodeType() != iris::SceneNodeType::Mesh) {
        fail(QStringLiteral("%1: '%2' is not a mesh node").arg(verb, nodeId));
        return iris::MeshNodePtr();
    }
    return node.staticCast<iris::MeshNode>();
}

bool MaterialApi::apply(const QString &nodeId, const QString &presetOrGuid)
{
    if (!host.mainWindow) return fail("material: not available in this session");
    if (!requireProject()) return false;   // the delegate registers DB rows
    auto meshNode = meshNodeOrFail(nodeId, QStringLiteral("material.apply"));
    if (!meshNode) return false;

    QString name = Constants::Reserved::DefaultMaterials.value(presetOrGuid);
    if (name.isEmpty()) name = presetOrGuid;

    const auto presets = loadPresets();
    const MaterialPreset *match = nullptr;
    for (const auto &preset : presets) {
        if (preset.name.compare(name, Qt::CaseInsensitive) == 0) { match = &preset; break; }
    }
    if (!match)
        return fail(QStringLiteral("material.apply: no preset named or guid '%1' (materials.presets() lists them)").arg(presetOrGuid));

    // The delegate targets the selection — select the node, then apply.
    host.mainWindow->sceneNodeSelected(meshNode);
    host.mainWindow->applyMaterialPreset(*match);
    return true;
}

bool MaterialApi::set(const QString &nodeId, const QVariantMap &values)
{
    auto meshNode = meshNodeOrFail(nodeId, QStringLiteral("material.set"));
    if (!meshNode) return false;
    auto material = meshNode->getMaterial();
    if (!material) return fail("material.set: the node has no material");

    static const QStringList colorKeys = { "baseColor", "emissiveColor",
                                           "ambientColor", "diffuseColor", "specularColor" };
    static const QStringList mapKeys = { "baseColorMap", "metallicMap", "roughnessMap",
                                         "normalMap", "occlusionMap", "emissiveMap",
                                         "diffuseTexture", "specularTexture", "normalTexture",
                                         "reflectionTexture" };

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const QString &key = it.key();
        QVariant newValue = normalizeJs(it.value());
        if (colorKeys.contains(key)) {
            newValue = QVariant::fromValue(colorFromJs(newValue));
        } else if (mapKeys.contains(key)) {
            // texture: an absolute/existing path passes through, an asset guid
            // resolves to the project file, empty clears
            const QString ref = newValue.toString();
            if (!ref.isEmpty() && !QFileInfo::exists(ref)) {
                if (!host.db || !host.isProjectOpen())
                    return fail(QStringLiteral("material.set: '%1' is not a file and no project is open to resolve it as an asset").arg(ref));
                const auto record = host.db->fetchAsset(ref);
                if (record.guid.isEmpty())
                    return fail(QStringLiteral("material.set: no texture file or asset '%1'").arg(ref));
                newValue = QDir(Globals::project->getProjectFolder()).filePath(record.name);
            }
        }

        // Old value for undo: from the material's editor property list when the
        // key is declared there; texture/map keys fall back to empty.
        QVariant oldValue;
        QList<iris::Property *> props;
        if (auto custom = material.dynamicCast<iris::CustomMaterial>()) props = custom->properties;
        else if (auto pbr = material.dynamicCast<iris::PbrMaterial>()) props = pbr->properties;
        bool known = false;
        for (auto prop : props) {
            if (prop->name == key) { oldValue = prop->getValue(); known = true; break; }
        }
        if (!known) {
            // CustomMaterial's command path asserts on unknown properties;
            // PbrMaterial map keys are legal without a Property declaration.
            const bool isCustom = !material.dynamicCast<iris::CustomMaterial>().isNull();
            if (isCustom || !mapKeys.contains(key))
                return fail(QStringLiteral("material.set: unknown property '%1'").arg(key));
        }

        host.services->undo->push(new ChangeMaterialPropertyCommand(material, key, oldValue, newValue));
    }
    return true;
}

QVariantMap MaterialApi::get(const QString &nodeId)
{
    QVariantMap out;
    auto meshNode = meshNodeOrFail(nodeId, QStringLiteral("material.get"));
    if (!meshNode) return out;
    auto material = meshNode->getMaterial();
    if (!material) return out;

    QList<iris::Property *> props;
    if (auto custom = material.dynamicCast<iris::CustomMaterial>()) props = custom->properties;
    else if (auto pbr = material.dynamicCast<iris::PbrMaterial>()) props = pbr->properties;
    for (auto prop : props) {
        const QVariant value = prop->getValue();
        if (value.typeId() == QMetaType::QColor) out[prop->name] = colorToJs(value.value<QColor>());
        else if (value.typeId() == QMetaType::QVector3D) out[prop->name] = vecToJs(value.value<QVector3D>());
        else out[prop->name] = value;
    }
    return out;
}

// -------------------------------------------------------------------- graph.*

QVector<VerbInfo> GraphApi::verbs() const
{
    return {
        { "nodes", "graph.nodes() -> [{id, type, master}]",
          "The current graph's nodes.",
          Needs::Document },
        { "nodeTypes", "graph.nodeTypes() -> [type]",
          "Every node type the library can create (plus the master types PbrMaterial and Material).",
          Needs::Document },
        { "addNode", "graph.addNode(type) -> id",
          "Adds a node to the current graph ('PbrMaterial' adds and sets the master).",
          Needs::Document },
        { "connect", "graph.connect(fromId, fromSocket, toId, toSocket) -> bool",
          "Connects an output socket to an input socket; sockets by index or name (e.g. 'Base Color').",
          Needs::Document },
        { "setValue", "graph.setValue(nodeId, value) -> bool",
          "Sets a node's value through the same path the editor uses (numbers, {r,g,b,a} colors, {x,y,z} vectors).",
          Needs::Document },
        { "getValue", "graph.getValue(nodeId) -> value",
          "Reads a node's value back.",
          Needs::Document },
        { "evaluate", "graph.evaluate() -> {values, unsupported, hasPbrMaster}",
          "Folds the current graph to PBR material values (the evaluator is GL-free by design).",
          Needs::Document },
        { "toMaterial", "graph.toMaterial(nodeId) -> bool",
          "Evaluates the current graph and applies the resulting PBR material to a mesh node.",
          Needs::Document },
        { "save", "graph.save() -> bool",
          "Serializes the current graph back into its shader asset (only for graphs opened from an asset guid).",
          Needs::Document },
    };
}

void GraphApi::setCurrent(NodeGraph *graph, const QString &assetGuid)
{
    // NodeGraph has no proper deep-delete; dropping the old pointer leaks a
    // little, matching how the shadergraph window itself swaps graphs.
    mGraph = graph;
    mAssetGuid = assetGuid;
}

NodeGraph *GraphApi::graphOrFail(const QString &verb)
{
    if (!mGraph) fail(QStringLiteral("%1: no graph is open — materials.loadGraph()/createGraph() first").arg(verb));
    return mGraph;
}

QVariantList GraphApi::nodes()
{
    QVariantList out;
    auto graph = graphOrFail(QStringLiteral("graph.nodes"));
    if (!graph) return out;
    for (auto it = graph->nodes.constBegin(); it != graph->nodes.constEnd(); ++it) {
        NodeModel *node = it.value();
        out.append(QVariantMap{ { "id", node->id },
                                { "type", node->typeName },
                                { "master", node == graph->masterNode } });
    }
    return out;
}

QVariantList GraphApi::nodeTypes()
{
    QVariantList out;
    LibraryV1 library;
    for (auto item : library.getItems()) out.append(item->name);
    out.append(QStringLiteral("PbrMaterial"));
    out.append(QStringLiteral("Material"));
    return out;
}

QString GraphApi::addNode(const QString &type)
{
    auto graph = graphOrFail(QStringLiteral("graph.addNode"));
    if (!graph) return QString();

    NodeModel *node = nullptr;
    if (type == "PbrMaterial") {
        node = new PbrMasterNode();
    } else {
        LibraryV1 library;
        if (!library.hasNode(type)) {
            fail(QStringLiteral("graph.addNode: unknown type '%1' (graph.nodeTypes() lists them)").arg(type));
            return QString();
        }
        node = library.createNode(type);
    }
    graph->addNode(node);
    if (type == "PbrMaterial") graph->setMasterNode(node);
    return node->id;
}

bool GraphApi::connect(const QString &fromId, const QVariant &fromSocket,
                       const QString &toId, const QVariant &toSocket)
{
    auto graph = graphOrFail(QStringLiteral("graph.connect"));
    if (!graph) return false;
    if (!graph->nodes.contains(fromId) || !graph->nodes.contains(toId))
        return fail("graph.connect: no such node id");
    NodeModel *from = graph->nodes[fromId];
    NodeModel *to = graph->nodes[toId];

    auto resolve = [this](const QVariant &ref, const QVector<SocketModel *> &sockets,
                          const char *side) -> int {
        const QVariant v = normalizeJs(ref);
        bool isInt = false;
        const int index = v.toInt(&isInt);
        if (isInt && v.typeId() != QMetaType::QString) {
            if (index >= 0 && index < sockets.size()) return index;
        } else {
            const QString name = v.toString();
            for (int i = 0; i < sockets.size(); ++i)
                if (sockets[i]->name.compare(name, Qt::CaseInsensitive) == 0) return i;
        }
        fail(QStringLiteral("graph.connect: no %1 socket '%2'").arg(side, v.toString()));
        return -1;
    };

    const int outIndex = resolve(fromSocket, from->outSockets, "output");
    if (outIndex < 0) return false;
    const int inIndex = resolve(toSocket, to->inSockets, "input");
    if (inIndex < 0) return false;

    graph->addConnection(from, outIndex, to, inIndex);
    return true;
}

bool GraphApi::setValue(const QString &nodeId, const QVariant &value)
{
    auto graph = graphOrFail(QStringLiteral("graph.setValue"));
    if (!graph) return false;
    if (!graph->nodes.contains(nodeId)) return fail("graph.setValue: no such node id");
    // The NodeModel interface is the public route (some overrides are private).
    static_cast<NodeModel *>(graph->nodes[nodeId])
        ->deserializeWidgetValue(QJsonValue::fromVariant(normalizeJs(value)));
    return true;
}

QVariant GraphApi::getValue(const QString &nodeId)
{
    auto graph = graphOrFail(QStringLiteral("graph.getValue"));
    if (!graph) return QVariant();
    if (!graph->nodes.contains(nodeId)) { fail("graph.getValue: no such node id"); return QVariant(); }
    return static_cast<NodeModel *>(graph->nodes[nodeId])->serializeWidgetValue().toVariant();
}

QVariantMap GraphApi::evaluate()
{
    QVariantMap out;
    auto graph = graphOrFail(QStringLiteral("graph.evaluate"));
    if (!graph) return out;
    const auto result = PbrGraphEvaluator::evaluate(graph, MaterialHelper::textureResolver());
    out["values"] = result.values.toVariantMap();
    out["unsupported"] = QVariant(result.unsupportedNodes);
    out["hasPbrMaster"] = result.hasPbrMaster;
    return out;
}

bool GraphApi::toMaterial(const QString &nodeId)
{
    auto graph = graphOrFail(QStringLiteral("graph.toMaterial"));
    if (!graph) return false;
    auto scene = host.mainWindow ? host.mainWindow->getScene() : iris::ScenePtr();
    if (!scene) return fail("graph.toMaterial: no scene is open");
    auto node = findNodeByGuid(scene->getRootNode(), nodeId);
    if (!node || node->getSceneNodeType() != iris::SceneNodeType::Mesh)
        return fail(QStringLiteral("graph.toMaterial: '%1' is not a mesh node").arg(nodeId));

    auto material = PbrGraphEvaluator::createMaterial(graph, MaterialHelper::textureResolver());
    if (!material) return fail("graph.toMaterial: evaluation produced no material");
    node.staticCast<iris::MeshNode>()->setMaterial(material);
    return true;
}

bool GraphApi::save()
{
    auto graph = graphOrFail(QStringLiteral("graph.save"));
    if (!graph) return false;
    if (!host.db) return fail("graph.save: not available in this session");
    if (mAssetGuid.isEmpty())
        return fail("graph.save: this graph was loaded from a file, not an asset — no destination");
    if (!graph->masterNode)
        return fail("graph.save: the graph has no master node (serialize would crash)");

    const QJsonObject definition = MaterialHelper::serialize(graph);
    return host.db->updateAssetAsset(mAssetGuid, QJsonDocument(definition).toJson());
}
