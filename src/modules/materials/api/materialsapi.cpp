/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/qtinterop.h"
#include "irisgl/core/math/vec.h"
#include "modules/materials/api/materialsapi.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "scripting/modules/moduleshared.h"
#include "data/constants.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/materialpreset.h"
#include "data/project.h"
#include "commands/changematerialpropertycommand.h"
#include "bridge/enginehost.h"
#include "io/assetmanager.h"
#include "io/materialpresetreader.h"
#include "shell/mainwindow.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/imagematerial.h"
#include "services/projectassets.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/core/properties/property.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"

#include "modules/materials/core/graphbaker.h"
#include "modules/materials/core/materialhelper.h"
#include "modules/materials/core/pbrgraphevaluator.h"
#include "modules/materials/graph/nodegraph.h"
#include "modules/materials/models/libraryv1.h"
#include "modules/materials/models/nodemodel.h"
#include "modules/materials/models/socketmodel.h"
#include "modules/materials/nodes/pbrmasternode.h"
#include "modules/materials/nodes/test.h"     // TextureNode (B2 graph twin)

using namespace scriptmod;

namespace {

QVector<MaterialPreset> loadPresets()
{
    QVector<MaterialPreset> presets;
    const QDir dir(IrisUtils::getAbsoluteAssetPath("app/content/materials"));
    MaterialPresetReader reader;
    const bool pbrOnly = true;   // engine viewport is the only renderer
    for (const auto &file : dir.entryInfoList(QStringList(), QDir::Files)) {
        auto preset = reader.readMaterialPreset(file.absoluteFilePath());
        if (pbrOnly && preset.type.compare("PBR", Qt::CaseInsensitive) != 0) continue;
        presets.append(preset);
    }
    return presets;
}

// The material.* key vocabulary, at file scope so material.set, its refusal
// message and material.properties all read the SAME lists. They used to be
// function-statics inside set(), which is how the "unknown property" error and
// the set of keys that actually work drift apart.
const QStringList kColorKeys = { "baseColor", "emissiveColor",
                                 "ambientColor", "diffuseColor", "specularColor" };
/// The PBR texture slots. PbrMaterial::createProperties DOES declare all six as
/// rows today (verified at this pin), so they arrive through the declared path;
/// this list is what keeps material.set working on a material whose property
/// list does not carry them, and what the F7 refusal message quotes.
const QStringList kPbrMapKeys = { "baseColorMap", "metallicMap", "roughnessMap",
                                  "normalMap", "occlusionMap", "emissiveMap" };
/// CustomMaterial's spellings (Default.shader declares them as Properties).
/// On a PbrMaterial they name nothing and are REFUSED by name (F7) — so they
/// are never writable keys, on either material class.
const QStringList kLegacyMapKeys = { "diffuseTexture", "specularTexture",
                                     "normalTexture", "reflectionTexture" };
const QStringList kMapKeys = kPbrMapKeys + kLegacyMapKeys;

/// The material's declared Property rows: CustomMaterial and PbrMaterial each
/// keep their own list and there is no common accessor.
QList<iris::Property *> declaredProperties(const iris::MaterialPtr &material)
{
    if (auto custom = material.dynamicCast<iris::CustomMaterial>()) return custom->properties;
    if (auto pbr = material.dynamicCast<iris::PbrMaterial>()) return pbr->properties;
    return {};
}

/// The keys material.set will accept on this material — the declared rows plus,
/// on a PbrMaterial only, the undeclared PBR texture slots.
QStringList writableMaterialKeys(const iris::MaterialPtr &material)
{
    QStringList keys;
    for (auto *prop : declaredProperties(material)) keys << prop->name;
    // The union, deduplicated: the six slots are declared rows on today's
    // PbrMaterial, but material.set accepts them either way, and a key listed
    // twice in an error message reads as a bug in the message.
    if (!material.dynamicCast<iris::PbrMaterial>().isNull()) keys << kPbrMapKeys;
    keys.removeDuplicates();
    return keys;
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
        { "loadGraph", "materials.loadGraph(guidOrPath) -> {nodes, master, name, texturesResolved}",
          "Opens an effect graph (a Shader asset guid, or a .effect/.shader file path) as the current graph for "
          "graph.* verbs. Texture nodes carrying an APP-RELATIVE image name — which is how the shipped .effect "
          "presets reference their images — are imported and connected here, exactly as the Effects page does when "
          "it instantiates a template; texturesResolved reports how many.",
          Needs::Document },
        { "regenerate", "materials.regenerate(shaderGuid) -> bool",
          "Re-evaluates and re-bakes a stored shader asset's maps into BakedMaps/<guid>/ (the 'cache deleted / app "
          "upgraded' recovery) and refreshes materials in the open scene that use that cache.",
          Needs::Document },
        { "createFromImage", "materials.createFromImage(textureGuid, {graph}) -> materialGuid",
          "Creates the standard image material asset for a Texture (IMAGE_PLANE_SPEC option B1): a PBR .material "
          "with the image as baseColorMap (roughness 1, metallic 0; alpha images blend), a Material→Texture "
          "dependency row and an image-derived thumbnail. Created in the library; with a project open it is also "
          "pinned into the project (bin-visible, droppable). Direct image add-to-project runs this automatically; "
          "re-creating for the same image returns a fresh asset. With {graph: true} (B2, needs an open project) "
          "it instead creates an editable Shader GRAPH asset — texture → textureSampler → PbrMaster.BaseColor — "
          "returning the shader guid (opens in the Materials page; applies via the drawer/graph.toMaterial). "
          "NOT undoable.",
          Needs::Document },
    };
}

QString MaterialsApi::createFromImage(const QString &textureGuid, const QVariantMap &options)
{
    if (!host.db) { fail("materials: not available in this session"); return QString(); }

    if (options.value("graph").toBool())
        return createImageGraph(textureGuid);

    QString error;
    const QString materialGuid =
        ImageMaterial::createMaterialAsset(textureGuid, host.db, host.project, &error);
    if (materialGuid.isEmpty()) {
        fail(QStringLiteral("materials.createFromImage: %1").arg(error));
        return QString();
    }
    // Project context: pin it in (bin membership + session registration) —
    // the same path a direct image add-to-project takes for its companion.
    if (host.project && !host.project->getProjectGuid().isEmpty())
        ProjectAssets::addToProject(materialGuid, host.db, host.project, ProjectAssets::AddKind::Direct);
    return materialGuid;
}

QString MaterialsApi::createImageGraph(const QString &textureGuid)
{
    // IMAGE_PLANE_SPEC option B2: the graph twin — the spec's template
    // texture → textureSampler → PbrMaster.BaseColor as an editable Shader
    // asset. createGraph's registration shape, plus the two nodes.
    if (!requireProject()) return QString();

    const auto record = host.db->fetchAsset(textureGuid);
    if (record.guid.isEmpty()
        || static_cast<ModelTypes>(record.type) != ModelTypes::Texture) {
        fail(QStringLiteral("materials.createFromImage: '%1' is not a texture asset").arg(textureGuid));
        return QString();
    }

    const QString parentFolder = host.project->getProjectGuid();
    QString shaderName = QFileInfo(record.name).completeBaseName();
    const QStringList existing = host.db->fetchAssetNameByParent(parentFolder);
    int increment = 1;
    while (existing.contains(IrisUtils::buildFileName(shaderName, "shader")))
        shaderName = QStringLiteral("%1 %2").arg(QFileInfo(record.name).completeBaseName()).arg(increment++);

    const QString assetGuid = GUIDManager::generateGUID();
    host.db->createAssetEntry(assetGuid, IrisUtils::buildFileName(shaderName, "shader"),
                              static_cast<int>(ModelTypes::Shader), parentFolder,
                              host.project->getProjectGuid());

    auto *lib = new LibraryV1();
    auto *graph = new NodeGraph;
    graph->setNodeLibrary(lib);
    auto *master = new PbrMasterNode();
    graph->addNode(master);
    graph->setMasterNode(master);
    MaterialSettings settings;
    settings.name = shaderName;
    graph->setMaterialSettings(settings);

    auto *texNode = static_cast<TextureNode *>(lib->createNode("texture"));
    texNode->setTextureGuid(textureGuid);   // stored as the asset guid; resolvers go pin-first
    graph->addNode(texNode);
    auto *sampler = lib->createNode("textureSampler");
    graph->addNode(sampler);
    graph->addConnection(texNode, 0, sampler, 0);   // texture -> sampler.Texture
    graph->addConnection(sampler, 0, master, 0);    // sampler.RGBA -> Base Color

    // serializeWithBake so the stored definition carries the pbrMaterial
    // object immediately (a bare sampler chain is Passthrough — no bake
    // files, the image itself is the map).
    MaterialHelper::setProjectRoot(host.project->getProjectFolder());
    QJsonObject definition = MaterialHelper::serializeWithBake(graph, assetGuid);
    definition["name"] = shaderName;
    definition.insert("guid", assetGuid);
    host.db->updateAssetAsset(assetGuid, QJsonDocument(definition).toJson());
    host.db->createDependency(static_cast<int>(ModelTypes::Shader),
                              static_cast<int>(ModelTypes::Texture),
                              assetGuid, textureGuid, host.project->getProjectGuid());

    auto assetShader = new AssetMaterial;
    assetShader->fileName = shaderName;
    assetShader->assetGuid = assetGuid;
    assetShader->path = IrisUtils::join(host.project->getProjectFolder(),
                                        IrisUtils::buildFileName(shaderName, "shader"));
    assetShader->setValue(QVariant::fromValue(definition));
    AssetManager::addAsset(assetShader);

    if (mGraphApi) mGraphApi->setCurrent(graph, assetGuid);
    return assetGuid;
}

bool MaterialsApi::regenerate(const QString &shaderGuid)
{
    if (!host.db) return fail("materials: not available in this session");
    if (!requireProject()) return false;

    const QByteArray blob = host.db->fetchAssetData(shaderGuid);
    if (blob.isEmpty())
        return fail(QStringLiteral("materials.regenerate: no shader asset '%1'").arg(shaderGuid));
    const QJsonObject definition = QJsonDocument::fromJson(blob).object();
    if (!definition.contains("shadergraph"))
        return fail("materials.regenerate: the asset has no 'shadergraph' object");

    NodeGraph *graph = NodeGraph::deserialize(definition["shadergraph"].toObject(), new LibraryV1());
    if (!graph || !graph->getMasterNode())
        return fail("materials.regenerate: the graph has no master node");

    MaterialHelper::setProjectRoot(host.project->getProjectFolder());
    QJsonObject rebaked = MaterialHelper::serializeWithBake(graph, shaderGuid);
    // keep the identity keys the stored definition carried
    if (definition.contains("name")) rebaked["name"] = definition["name"];
    if (definition.contains("guid")) rebaked["guid"] = definition["guid"];
    host.db->updateAssetAsset(shaderGuid, QJsonDocument(rebaked).toJson());

    // refresh applied materials in the open scene: PbrMaterials whose maps
    // point into this shader's BakedMaps cache rebuild from the fresh bake
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (scene && scene->getRootNode()) {
        const QString marker = QStringLiteral("/BakedMaps/") + shaderGuid + QStringLiteral("/");
        std::function<void(const iris::SceneNodePtr &)> walk =
            [&](const iris::SceneNodePtr &n) {
                if (n->getSceneNodeType() == iris::SceneNodeType::Mesh) {
                    auto mesh = n.staticCast<iris::MeshNode>();
                    if (auto pbr = mesh->getMaterial().dynamicCast<iris::PbrMaterial>()) {
                        bool usesCache = false;
                        for (auto it = pbr->textures.constBegin(); it != pbr->textures.constEnd(); ++it)
                            if (it.value() && it.value()->source.contains(marker)) { usesCache = true; break; }
                        if (usesCache) {
                            if (auto fresh = MaterialHelper::createPbrMaterialFromDefinition(rebaked))
                                mesh->setMaterial(fresh);
                        }
                    }
                }
                for (const auto &child : n->children()) walk(child);
            };
        walk(scene->getRootNode());
    }
    return true;
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
    const QString parentFolder = host.project->getProjectGuid();
    QString shaderName = name.trimmed();
    const QStringList existing = host.db->fetchAssetNameByParent(parentFolder);
    int increment = 1;
    while (existing.contains(IrisUtils::buildFileName(shaderName, "shader")))
        shaderName = QStringLiteral("%1 %2").arg(name.trimmed()).arg(increment++);

    const QString assetGuid = GUIDManager::generateGUID();
    host.db->createAssetEntry(assetGuid, IrisUtils::buildFileName(shaderName, "shader"),
                              static_cast<int>(ModelTypes::Shader), parentFolder,
                              host.project->getProjectGuid());

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
    assetShader->path = IrisUtils::join(host.project->getProjectFolder(),
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
    // The same app-relative texture resolution the Effects page does when it
    // instantiates a template (samples audit, 2026-09-04): without it a shipped
    // .effect preset opened through this verb had every texture node
    // unconnected, so graph.bake produced an untextured material.
    out["texturesResolved"] = MaterialHelper::resolveAppRelativeTextures(graph);
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
          "Applies a built-in preset (by name or reserved guid) or a saved project material asset (by guid) to a node. "
          "A container node (an imported model's root) applies to every mesh under it, each with its own material instance. "
          "Also registers preset applies as a project asset, like the presets panel. Undoable.",
          Needs::Document },
        { "set", "material.set(nodeId, {baseColor, roughness, metallic, baseColorMap, ...}) -> bool",
          "Sets material properties on a mesh node (PBR keys; *Map keys take texture paths or asset guids). Undoable per property.",
          Needs::Document },
        { "get", "material.get(nodeId) -> {property: value}",
          "Reads the node material's editor-facing properties. material.properties(nodeId) is the "
          "same values plus their types, ranges and the full writable-key list.",
          Needs::Document },
        { "properties", "material.properties(nodeId) -> {class, rows:[{name, displayName, type, value, min?, max?}], writableKeys:[…]}",
          "What this node's material can be told, without guessing. 'class' is PbrMaterial or "
          "CustomMaterial (they have different vocabularies). 'rows' are the DECLARED properties "
          "in panel order, with 'min'/'max' present only where a range is declared — the PBR "
          "material declares real ones (metallic and roughness are 0..1, emissiveIntensity 0..10), "
          "so this is where a scale actually means something. 'writableKeys' is the exact set "
          "material.set accepts — the row names plus, on a PbrMaterial, its six texture slots "
          "(baseColorMap, metallicMap, roughnessMap, normalMap, occlusionMap, emissiveMap), "
          "which take a file path or an image asset guid. Read 'writableKeys' rather than "
          "deriving keys from 'rows': the two agree today but the slot list is what material.set "
          "actually consults. The legacy shader spellings (diffuseTexture, normalTexture, …) are "
          "NOT writable on a PBR material and are refused by name.",
          Needs::Document },
    };
}

iris::MeshNodePtr MaterialApi::meshNodeOrFail(const QString &nodeId, const QString &verb)
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene() : iris::ScenePtr();
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

    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene() : iris::ScenePtr();
    if (!scene) return fail("material.apply: no scene is open");
    auto node = findNodeByGuid(scene->getRootNode(), nodeId);
    if (!node) return fail(QStringLiteral("material.apply: no node '%1'").arg(nodeId));

    // A mesh applies directly; a container (an imported model's Empty root —
    // exactly what the viewport's click-selects-the-root rule selects) applies
    // to every mesh underneath. A target with no meshes at all is an error,
    // not a silent no-op — the silent path is how applied materials used to
    // vanish without ever reaching the document.
    std::function<bool(const iris::SceneNodePtr &)> hasMesh =
        [&hasMesh](const iris::SceneNodePtr &n) -> bool {
            if (n->getSceneNodeType() == iris::SceneNodeType::Mesh) return true;
            for (const auto &child : n->children())
                if (hasMesh(child)) return true;
            return false;
        };
    if (!hasMesh(node))
        return fail(QStringLiteral("material.apply: '%1' has no mesh nodes to apply to").arg(nodeId));

    host.services->selection->select(node);

    QString name = Constants::Reserved::DefaultMaterials.value(presetOrGuid);
    if (name.isEmpty()) name = presetOrGuid;

    const auto presets = loadPresets();
    for (const auto &preset : presets) {
        if (preset.name.compare(name, Qt::CaseInsensitive) == 0) {
            host.services->sceneEdit->applyMaterialPreset(preset, node);
            return true;
        }
    }

    // Not a built-in preset: a saved project material asset guid (the same
    // fallback the viewport drop uses).
    if (host.services->sceneEdit->applyMaterialAsset(presetOrGuid, node)) return true;

    return fail(QStringLiteral("material.apply: no preset or material asset '%1' (materials.presets() and assets.list list them)").arg(presetOrGuid));
}

bool MaterialApi::set(const QString &nodeId, const QVariantMap &values)
{
    auto meshNode = meshNodeOrFail(nodeId, QStringLiteral("material.set"));
    if (!meshNode) return false;
    auto material = meshNode->getMaterial();
    if (!material) return fail("material.set: the node has no material");

    // The vocabularies live at file scope (top of this file) so this function,
    // its refusal message and material.properties can never disagree.
    const QStringList &colorKeys = kColorKeys;
    const QStringList &legacyMapKeys = kLegacyMapKeys;
    const QStringList &mapKeys = kMapKeys;

    const bool isCustomMaterial = !material.dynamicCast<iris::CustomMaterial>().isNull();

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const QString &key = it.key();
        QVariant newValue = normalizeJs(it.value());

        // THE KEY IS CHECKED BEFORE ITS VALUE. Found building lane A: F7's
        // "that is a legacy shader texture name" message was UNREACHABLE for
        // the values people actually pass. A legacy key fell into the texture
        // branch below first, and any path that is not an existing file and not
        // an asset guid — i.e. every plausible typo — was refused with "no
        // texture file or asset 'x.png'", which sends the reader off hunting for
        // a missing file when the real problem is that this material has no such
        // slot at all. Only a legacy key whose value happened to resolve ever
        // reached the message written for it.
        if (!isCustomMaterial && legacyMapKeys.contains(key)) {
            static const QMap<QString, QString> replacement{
                { QStringLiteral("diffuseTexture"),    QStringLiteral("baseColorMap") },
                { QStringLiteral("normalTexture"),     QStringLiteral("normalMap") },
                { QStringLiteral("specularTexture"),   QString() },
                { QStringLiteral("reflectionTexture"), QString() },
            };
            const QString instead = replacement.value(key);
            return fail(QStringLiteral(
                            "material.set: '%1' is a legacy shader texture name and this "
                            "node's PBR material has no such slot — %2 (the PBR maps are "
                            "baseColorMap, metallicMap, roughnessMap, normalMap, "
                            "occlusionMap, emissiveMap)")
                            .arg(key,
                                 instead.isEmpty()
                                     ? QStringLiteral("there is no PBR equivalent")
                                     : QStringLiteral("use '%1'").arg(instead)));
        }

        if (colorKeys.contains(key)) {
            // F8: a colour string the parser cannot read used to keep the old
            // value and still report success.
            bool colourOk = false;
            const QColor colour = colorFromJs(newValue, QColor(), &colourOk);
            if (!colourOk)
                return fail(QStringLiteral("material.set: %1 (property '%2')")
                                .arg(colorHelp(newValue), key));
            newValue = QVariant::fromValue(colour);
        } else if (mapKeys.contains(key)) {
            // texture: an absolute/existing path passes through, an asset guid
            // resolves through the CAS (pinned bytes in project context; the
            // flat projectFolder/name join pointed at an unpopulated folder),
            // empty clears
            const QString ref = newValue.toString();
            if (!ref.isEmpty() && !QFileInfo::exists(ref)) {
                if (!host.db || !host.isProjectOpen())
                    return fail(QStringLiteral("material.set: '%1' is not a file and no project is open to resolve it as an asset").arg(ref));
                const auto record = host.db->fetchAsset(ref);
                if (record.guid.isEmpty())
                    return fail(QStringLiteral("material.set: no texture file or asset '%1'").arg(ref));
                QSqlDatabase conn = QSqlDatabase::database();
                QString resolved = AssetCas::resolvePinned(conn, AssetStorePaths::root(),
                                                           host.project->getProjectGuid(), ref);
                if (resolved.isEmpty())
                    resolved = AssetCas::resolveSource(conn, AssetStorePaths::root(), ref);
                if (resolved.isEmpty())
                    resolved = QDir(host.project->getProjectFolder()).filePath(record.name);
                newValue = resolved;
            }
        }

        // Old value for undo: from the material's editor property list when the
        // key is declared there; texture/map keys fall back to empty.
        QVariant oldValue;
        QList<iris::Property *> props = declaredProperties(material);
        bool known = false;
        for (auto prop : props) {
            if (prop->name == key) { oldValue = prop->getValue(); known = true; break; }
        }
        if (!known) {
            // CustomMaterial's command path asserts on unknown properties;
            // a PbrMaterial map key is legal even if its Property row were ever
            // dropped. The legacy *Texture spellings never reach here — they are
            // refused by name at the top of the loop (F7).
            if (isCustomMaterial || !mapKeys.contains(key))
                // The key list is the ONE thing that turns this from a bare
                // rejection into a usable answer (AI_SURFACE_PROGRAM_SPEC §3.A
                // item #1): exactly the keys that survive the F7 fix. The legacy
                // spellings are deliberately NOT in it.
                return fail(QStringLiteral("material.set: unknown property '%1' — this "
                                           "material accepts: %2 (material.properties('%3') "
                                           "gives their types, values and ranges)")
                                .arg(key, writableMaterialKeys(material).join(QStringLiteral(", ")),
                                     nodeId));
        }

        host.services->undo->push(new ChangeMaterialPropertyCommand(material, key, oldValue, newValue));
    }
    return true;
}

QVariantMap MaterialApi::properties(const QString &nodeId)
{
    QVariantMap out;
    auto meshNode = meshNodeOrFail(nodeId, QStringLiteral("material.properties"));
    if (!meshNode) return out;
    auto material = meshNode->getMaterial();
    if (!material) { fail("material.properties: the node has no material"); return out; }

    // Unlike SceneNode::getProperties(), a material's rows are OWNED by the
    // material (iris::Material holds the QList<Property*> for its lifetime) —
    // read them, never delete them.
    out["class"] = !material.dynamicCast<iris::PbrMaterial>().isNull()
                       ? QStringLiteral("PbrMaterial")
                   : !material.dynamicCast<iris::CustomMaterial>().isNull()
                       ? QStringLiteral("CustomMaterial")
                       : QStringLiteral("Material");

    QVariantList rows;
    for (auto *prop : declaredProperties(material)) rows.append(propertyRowToJs(prop));
    out["rows"] = rows;
    out["writableKeys"] = writableMaterialKeys(material);
    return out;
}

QVariantMap MaterialApi::get(const QString &nodeId)
{
    QVariantMap out;
    auto meshNode = meshNodeOrFail(nodeId, QStringLiteral("material.get"));
    if (!meshNode) return out;
    auto material = meshNode->getMaterial();
    if (!material) return out;

    for (auto prop : declaredProperties(material)) {
        const QVariant value = prop->getValue();
        if (value.typeId() == QMetaType::QColor) out[prop->name] = colorToJs(value.value<QColor>());
        else if (value.typeId() == QMetaType::QVector3D) out[prop->name] = vecToJs(iris::fromQt(value.value<QVector3D>()));
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
          "Every node type the library can create (plus the master type PbrMaterial).",
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
        { "evaluate", "graph.evaluate() -> {values, unsupported, approximated, animated, hasPbrMaster}",
          "Folds the current graph to PBR material values (the evaluator is GL-free by design). Pure math chains fold; "
          "approximated lists nodes evaluated against the fake fragment context (worldNormal, fresnel, time at t=0, ...).",
          Needs::Document },
        { "bakeInfo", "graph.bakeInfo() -> {perSocket: {socketName: class}}",
          "Classifies each master input: 'uniform' | 'passthrough' | 'baked' | 'unsupported' | 'unconnected'.",
          Needs::Document },
        { "bake", "graph.bake({resolution?, time?}) -> {values, maps, passthrough, approximated, unsupported, animated, msElapsed}",
          "Full-quality synchronous bake of the current graph: UV-varying chains render per texel into "
          "<project>/BakedMaps/<guid>/ PNGs (hash-cached, headless-capable - CPU only), uniform chains fold, "
          "bare textures pass through. Map values are project-relative paths.",
          Needs::Document },
        { "toMaterial", "graph.toMaterial(nodeId) -> bool",
          "Evaluates the current graph and applies the resulting PBR material to a mesh node.",
          Needs::Document },
        { "save", "graph.save() -> bool",
          "Serializes the current graph back into its shader asset (only for graphs opened from an asset guid).",
          Needs::Document },
        { "selectNode", "graph.selectNode(id) -> bool",
          "Selects a node. When the Effects page has a node with this id its canvas selection (and the properties panel) follows; "
          "otherwise the id must belong to the current script graph.",
          Needs::Document },
        { "selectedNode", "graph.selectedNode() -> id|null",
          "The selected node's id: the Effects page's canvas selection when one exists, else the script-side selection.",
          Needs::Document },
        { "deselect", "graph.deselect() -> bool",
          "Clears the selection (canvas and script-side).",
          Needs::Document },
        { "settings", "graph.settings() -> {name, blendMode, bakeResolution}",
          "The current graph's material settings; blendMode is one of "
          "'Opaque' | 'Masked' | 'Translucent' | 'Additive' | 'Modulate'.",
          Needs::Document },
        { "setBlendMode", "graph.setBlendMode(mode) -> bool",
          "Sets the master material's blend mode ('Opaque' | 'Masked' | 'Translucent' | 'Additive' | 'Modulate' — "
          "the Unreal set; 'Blend' is accepted as the legacy name for 'Translucent'). Material state only: bakes are "
          "unaffected, the evaluated material's alphaMode changes.",
          Needs::Document },
        { "undo", "graph.undo() -> bool",
          "Undoes one edit on the Materials page's graph — the SAME stack the page's toolbar arrows and "
          "Ctrl+Z on that page drive (node adds and deletes, moves, connections, pastes, property edits). "
          "False when there is nothing to undo, or when this session has no Materials page (the API-local "
          "script graph keeps no command history). Scene edits are editor.undo's; the two stacks are separate.",
          Needs::Window },
        { "redo", "graph.redo() -> bool",
          "Redoes the last undone graph edit. False when there is nothing to redo, or with no Materials page.",
          Needs::Window },
        { "undoState", "graph.undoState() -> {available, canUndo, canRedo, undoCount, redoCount}",
          "The depth of the Materials page's graph edit stack. `available` is false in sessions with no "
          "Materials page, which is also why the counts are then zero.",
          Needs::Document },
    };
}

void GraphApi::setCurrent(NodeGraph *graph, const QString &assetGuid)
{
    // NodeGraph has no proper deep-delete; dropping the old pointer leaks a
    // little, matching how the shadergraph window itself swaps graphs.
    mGraph = graph;
    mAssetGuid = assetGuid;
    mSelectedNodeId.clear();
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
    // "Material" (the legacy Blinn-Phong master) is deliberately NOT listed:
    // addNode cannot create one, so listing it made scripts throw (audit D14).
    out.append(QStringLiteral("PbrMaterial"));
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
        // ("property" retired §3b — it is simply no longer a library type)
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
    out["approximated"] = QVariant(result.approximatedNodes);
    out["animated"] = result.animated;
    out["hasPbrMaster"] = result.hasPbrMaster;
    return out;
}

QVariantMap GraphApi::bakeInfo()
{
    QVariantMap out;
    auto graph = graphOrFail(QStringLiteral("graph.bakeInfo"));
    if (!graph) return out;
    return PbrGraphEvaluator::bakeInfo(graph, MaterialHelper::textureResolver()).toVariantMap();
}

QVariantMap GraphApi::bake(const QVariantMap &options)
{
    QVariantMap out;
    auto graph = graphOrFail(QStringLiteral("graph.bake"));
    if (!graph) return out;
    if (!requireProject()) return out; // baked maps land in the project folder

    QString guid = mAssetGuid.isEmpty() ? graph->materialGuid : mAssetGuid;
    if (guid.isEmpty()) guid = QStringLiteral("scratch");

    const QString projectFolder = host.project->getProjectFolder();
    MaterialHelper::setProjectRoot(projectFolder);

    materials::GraphBaker::Options opts;
    opts.resolution = options.value(QStringLiteral("resolution"), 1024).toInt();
    opts.time = options.value(QStringLiteral("time"), 0.0).toDouble();
    opts.outputDir = projectFolder + QStringLiteral("/BakedMaps/") + guid;
    opts.relativePrefix = QStringLiteral("BakedMaps/") + guid + QStringLiteral("/");

    const auto result = materials::GraphBaker::run(graph, opts, MaterialHelper::textureResolver());
    out["values"] = result.eval.values.toVariantMap();
    out["maps"] = result.maps.toVariantMap();
    out["passthrough"] = result.passthrough.toVariantMap();
    out["approximated"] = QVariant(result.eval.approximatedNodes);
    out["unsupported"] = QVariant(result.eval.unsupportedNodes);
    out["animated"] = result.eval.animated;
    out["msElapsed"] = double(result.msElapsed);
    return out;
}

bool GraphApi::toMaterial(const QString &nodeId)
{
    auto graph = graphOrFail(QStringLiteral("graph.toMaterial"));
    if (!graph) return false;
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene() : iris::ScenePtr();
    if (!scene) return fail("graph.toMaterial: no scene is open");
    auto node = findNodeByGuid(scene->getRootNode(), nodeId);
    if (!node || node->getSceneNodeType() != iris::SceneNodeType::Mesh)
        return fail(QStringLiteral("graph.toMaterial: '%1' is not a mesh node").arg(nodeId));

    // applying is a final-bake trigger (spec section 2): with a project open,
    // UV-varying chains land as BakedMaps PNGs and ride the material's map keys
    iris::PbrMaterialPtr material;
    if (host.isProjectOpen() && host.project) {
        MaterialHelper::setProjectRoot(host.project->getProjectFolder());
        QString guid = mAssetGuid.isEmpty() ? graph->materialGuid : mAssetGuid;
        if (guid.isEmpty()) guid = QStringLiteral("scratch");

        materials::GraphBaker::Options opts;
        opts.resolution = graph->settings.bakeResolution;
        opts.outputDir = host.project->getProjectFolder() + QStringLiteral("/BakedMaps/") + guid;
        opts.relativePrefix = QStringLiteral("BakedMaps/") + guid + QStringLiteral("/");
        const auto baked = materials::GraphBaker::run(graph, opts, MaterialHelper::textureResolver());
        material = PbrGraphEvaluator::materialFromValues(baked.eval.values, MaterialHelper::textureResolver());
    } else {
        material = PbrGraphEvaluator::createMaterial(graph, MaterialHelper::textureResolver());
    }
    if (!material) return fail("graph.toMaterial: evaluation produced no material");
    node.staticCast<iris::MeshNode>()->setMaterial(material);
    return true;
}

bool GraphApi::selectNode(const QString &nodeId)
{
    // the live Effects canvas wins when it knows the id (§3a: the panel
    // follows its selection)
    if (mSelection.select && mSelection.select(nodeId)) {
        mSelectedNodeId.clear();
        return true;
    }

    auto graph = graphOrFail(QStringLiteral("graph.selectNode"));
    if (!graph) return false;
    if (!graph->nodes.contains(nodeId))
        return fail(QStringLiteral("graph.selectNode: no node '%1' in the Effects page or the current graph").arg(nodeId));
    mSelectedNodeId = nodeId;
    return true;
}

QVariant GraphApi::selectedNode()
{
    if (mSelection.selected) {
        const QString pageId = mSelection.selected();
        if (!pageId.isEmpty()) return pageId;
    }
    if (!mSelectedNodeId.isEmpty() && mGraph && mGraph->nodes.contains(mSelectedNodeId))
        return mSelectedNodeId;
    return QVariant(); // null: nothing selected anywhere
}

bool GraphApi::deselect()
{
    if (mSelection.deselect) mSelection.deselect();
    mSelectedNodeId.clear();
    return true;
}

namespace {
// One name per BlendMode, matching the settings-view combo labels and the
// serialized strings (nodegraph.cpp keeps "Blend" on disk for Translucent).
const char *blendModeName(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Opaque:      return "Opaque";
    case BlendMode::Masked:      return "Masked";
    case BlendMode::Translucent: return "Translucent";
    case BlendMode::Additive:    return "Additive";
    case BlendMode::Modulate:    return "Modulate";
    }
    return "Opaque";
}
} // namespace

QVariantMap GraphApi::settings()
{
    QVariantMap out;
    auto graph = graphOrFail(QStringLiteral("graph.settings"));
    if (!graph) return out;
    out["name"] = graph->settings.name;
    out["blendMode"] = QString::fromLatin1(blendModeName(graph->settings.blendMode));
    out["bakeResolution"] = graph->settings.bakeResolution;
    return out;
}

bool GraphApi::setBlendMode(const QString &mode)
{
    auto graph = graphOrFail(QStringLiteral("graph.setBlendMode"));
    if (!graph) return false;
    const QString m = mode.trimmed().toLower();
    BlendMode want;
    if      (m == "opaque")                       want = BlendMode::Opaque;
    else if (m == "masked")                       want = BlendMode::Masked;
    else if (m == "translucent" || m == "blend")  want = BlendMode::Translucent;
    else if (m == "additive")                     want = BlendMode::Additive;
    else if (m == "modulate")                     want = BlendMode::Modulate;
    else return fail(QStringLiteral("graph.setBlendMode: unknown mode '%1' "
                     "(Opaque | Masked | Translucent | Additive | Modulate)").arg(mode));
    MaterialSettings s = graph->settings;
    s.blendMode = want;
    graph->setMaterialSettings(s);
    return true;
}

// ---- the Materials page's edit stack ---------------------------------------
// The verbs and the shell's Ctrl+Z on that page are two callers of ONE entry
// point (EffectsPage::graphUndo/graphRedo), reached through the delegate
// MaterialsModule::registerApi installs. No page (headless slices, the
// document-only script host) = no stack: say so instead of silently doing
// nothing, because "graph.undo() returned true" is what a test believes.

bool GraphApi::undo()
{
    if (!mUndo.undo)
        return fail("graph.undo: no Materials page in this session (the script-local graph keeps no history)");
    return mUndo.undo();
}

bool GraphApi::redo()
{
    if (!mUndo.redo)
        return fail("graph.redo: no Materials page in this session (the script-local graph keeps no history)");
    return mUndo.redo();
}

QVariantMap GraphApi::undoState()
{
    QVariantMap out;
    const bool available = bool(mUndo.undoCount) && bool(mUndo.redoCount);
    const int undoCount = available ? mUndo.undoCount() : 0;
    const int redoCount = available ? mUndo.redoCount() : 0;
    out.insert("available", available);
    out.insert("canUndo", undoCount > 0);
    out.insert("canRedo", redoCount > 0);
    out.insert("undoCount", undoCount);
    out.insert("redoCount", redoCount);
    return out;
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

    // saving is a final-bake trigger (spec section 2)
    if (host.isProjectOpen() && host.project)
        MaterialHelper::setProjectRoot(host.project->getProjectFolder());
    const QJsonObject definition = MaterialHelper::serializeWithBake(graph, mAssetGuid);
    return host.db->updateAssetAsset(mAssetGuid, QJsonDocument(definition).toJson());
}
