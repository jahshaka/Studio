/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/sceneapi.h"

#include <QFileInfo>

#include "scripting/modules/moduleshared.h"
#include "commands/reparentscenenodecommand.h"
#include "commands/transformscenenodecommand.h"
#include "shell/mainwindow.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "data/database/database.h"

using namespace scriptmod;

namespace {
const QStringList kPrimitives = {
    "Plane", "Cone", "Cube", "Cylinder", "Sphere", "Torus",
    "Capsule", "Gear", "Pyramid", "Teapot", "Sponge", "Steps"
};
}

QVector<VerbInfo> SceneApi::verbs() const
{
    return {
        { "nodes", "scene.nodes() -> [{id, name, type, parent, position, rotation, scale}]",
          "Every node in the open scene, depth-first from the root.",
          Needs::Document },
        { "find", "scene.find(name) -> id | null",
          "The first node with this exact name, or null.",
          Needs::Document },
        { "root", "scene.root() -> id",
          "The scene root's id.",
          Needs::Document },
        { "addPrimitive", "scene.addPrimitive(name, {position, rotation, scale, parent}) -> id",
          "Adds a built-in primitive (plane, cone, cube, cylinder, sphere, torus, capsule, gear, pyramid, teapot, sponge, steps). Undoable.",
          Needs::Document },
        { "addLight", "scene.addLight(type, {position, ...}) -> id",
          "Adds a light: point, spot, directional or area. Undoable.",
          Needs::Document },
        { "addEmpty", "scene.addEmpty({position, parent}) -> id",
          "Adds an empty group node. Undoable.",
          Needs::Document },
        { "addMesh", "scene.addMesh(path, {position, ...}) -> id",
          "Imports a mesh file (obj, fbx, dae, ...) straight into the scene — no dialog, the path is the argument. Undoable.",
          Needs::Document },
        { "addImagePlane", "scene.addImagePlane(textureGuid, {position?, doubleSided?}) -> id",
          "Spawns an image plane for a Texture asset (IMAGE_PLANE_SPEC option A): a plane sized to the image's aspect (long side 1 m), facing the editor camera at creation, with a basic PBR material carrying the image as baseColorMap (roughness 1, metallic 0; images with an alpha channel blend). Bytes resolve pin-first through the CAS. doubleSided defaults true. Undoable.",
          Needs::Document },
        { "addDecal", "scene.addDecal(textureGuid, {position?, rotation?, scale?, parent?, width?, height?, depth?, metalness?, roughness?, ignoreAlphaDiffuse?}) -> id",
          "Adds a projected-texture decal (DECALS_SPEC) bound to a Texture asset: an oriented box that "
          "paints the image onto every surface inside it, projecting down the node's -Y (the light "
          "convention) and masked by the image's alpha. width is the local-X extent, height the local-Z "
          "extent (the image's V axis) and depth the projection thickness; the node's own scale "
          "multiplies all three. textureGuid may be empty — the decal then draws its wire box and "
          "projects nothing until an image is bound. The image is pinned into the project as a "
          "BINDING (a dependency row, no companion PBR material is minted). There is deliberately no per-decal opacity "
          "or colour tint: the renderer packs four floats per decal and neither fits. Undoable.",
          Needs::Document },
    };
}

iris::ScenePtr SceneApi::sceneOrFail()
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                              : iris::ScenePtr();
    if (!scene) fail("scene: no scene is open — project.open() or project.create() first");
    return scene;
}

QVariantList SceneApi::nodes()
{
    QVariantList out;
    auto scene = sceneOrFail();
    if (!scene) return out;
    std::function<void(const iris::SceneNodePtr &)> walk = [&](const iris::SceneNodePtr &node) {
        out.append(nodeToJs(node));
        for (const auto &child : node->children) walk(child);
    };
    walk(scene->getRootNode());
    return out;
}

QVariant SceneApi::find(const QString &name)
{
    auto scene = sceneOrFail();
    if (!scene) return QVariant();
    std::function<iris::SceneNodePtr(const iris::SceneNodePtr &)> walk =
        [&](const iris::SceneNodePtr &node) -> iris::SceneNodePtr {
        if (node->getName() == name) return node;
        for (const auto &child : node->children)
            if (auto hit = walk(child)) return hit;
        return iris::SceneNodePtr();
    };
    auto hit = walk(scene->getRootNode());
    return hit ? QVariant(hit->getGUID()) : QVariant();
}

QString SceneApi::root()
{
    auto scene = sceneOrFail();
    return scene ? scene->getRootNode()->getGUID() : QString();
}

bool SceneApi::applyOptions(const iris::SceneNodePtr &node, const QVariantMap &options, const QString &verb)
{
    if (options.contains("parent")) {
        auto scene = host.services->sceneEdit->scene();
        auto parent = findNodeByGuid(scene->getRootNode(), options.value("parent").toString());
        if (!parent)
            return fail(QStringLiteral("%1: no node with id '%2' for parent").arg(verb, options.value("parent").toString()));
        if (ReparentSceneNodeCommand::wouldCreateCycle(node, parent))
            return fail(QStringLiteral("%1: reparenting would create a cycle").arg(verb));
        host.services->undo->push(new ReparentSceneNodeCommand(node, parent));
    }
    if (options.contains("position") || options.contains("rotation") || options.contains("scale")) {
        const QVector3D pos = vecFromJs(options.value("position"), node->getLocalPos());
        const QVector3D rotEuler = vecFromJs(options.value("rotation"), node->getLocalRot().toEulerAngles());
        const QVector3D scale = vecFromJs(options.value("scale"), node->getLocalScale());
        host.services->undo->push(new TransformSceneNodeCommand(
            node, pos, QQuaternion::fromEulerAngles(rotEuler), scale));
    }
    return true;
}

QString SceneApi::finishAdd(const QVariantMap &options, const QString &verb)
{
    // The add-verb funnel selects the new node (AddSceneNodeCommand::redo);
    // callers deselected beforehand so a silent failure reads as "no node".
    auto node = host.services->selection->selected();
    if (!node) {
        fail(QStringLiteral("%1: the node was not created").arg(verb));
        return QString();
    }
    if (!applyOptions(node, options, verb)) return QString();
    return node->getGUID();
}

QString SceneApi::addPrimitive(const QString &name, const QVariantMap &options)
{
    if (!requireProject()) return QString();   // the primitive gets a DB asset row
    if (!sceneOrFail()) return QString();

    QString normalized = name.trimmed().toLower();
    if (!normalized.isEmpty()) normalized[0] = normalized[0].toUpper();
    if (!kPrimitives.contains(normalized)) {
        fail(QStringLiteral("scene.addPrimitive: unknown primitive '%1' (try: %2)")
                 .arg(name, kPrimitives.join(", ").toLower()));
        return QString();
    }

    host.services->selection->select(iris::SceneNodePtr());
    host.services->sceneEdit->addPrimitive(normalized);
    return finishAdd(options, QStringLiteral("scene.addPrimitive"));
}

QString SceneApi::addLight(const QString &type, const QVariantMap &options)
{
    if (!sceneOrFail()) return QString();
    const QString t = type.trimmed().toLower();

    host.services->selection->select(iris::SceneNodePtr());
    if (t == "point")            host.services->sceneEdit->addPointLight();
    else if (t == "spot")        host.services->sceneEdit->addSpotLight();
    else if (t == "directional") host.services->sceneEdit->addDirectionalLight();
    else if (t == "area")        host.services->sceneEdit->addAreaLight();
    else {
        fail(QStringLiteral("scene.addLight: unknown type '%1' (point, spot, directional, area)").arg(type));
        return QString();
    }
    return finishAdd(options, QStringLiteral("scene.addLight"));
}

QString SceneApi::addEmpty(const QVariantMap &options)
{
    if (!sceneOrFail()) return QString();
    host.services->selection->select(iris::SceneNodePtr());
    host.services->sceneEdit->addEmpty();
    return finishAdd(options, QStringLiteral("scene.addEmpty"));
}

QString SceneApi::addImagePlane(const QString &textureGuid, const QVariantMap &options)
{
    if (!requireProject()) return QString();   // the plane gets a DB object row + dependency
    if (!sceneOrFail()) return QString();

    ImagePlaneOptions opts;
    opts.doubleSided = options.value("doubleSided", true).toBool();

    host.services->selection->select(iris::SceneNodePtr());
    auto node = host.services->sceneEdit->addImagePlane(
        textureGuid, vecFromJs(options.value("position")), opts);
    if (!node) {
        fail(QStringLiteral("scene.addImagePlane: '%1' is not a resolvable image texture asset")
                 .arg(textureGuid));
        return QString();
    }
    // position is consumed by the service (the drop point); strip it so
    // applyOptions does not re-apply it as a second transform command.
    QVariantMap rest = options;
    rest.remove("position");
    rest.remove("doubleSided");
    return finishAdd(rest, QStringLiteral("scene.addImagePlane"));
}

QString SceneApi::addDecal(const QString &textureGuid, const QVariantMap &options)
{
    if (!requireProject()) return QString();   // the decal gets a DB object row + dependency
    if (!sceneOrFail()) return QString();

    if (!textureGuid.isEmpty() && host.db &&
        host.db->fetchAsset(textureGuid).guid.isEmpty()) {
        fail(QStringLiteral("scene.addDecal: no asset with guid '%1'").arg(textureGuid));
        return QString();
    }

    DecalOptions opts;
    opts.width  = options.value("width",  double(opts.width)).toFloat();
    opts.height = options.value("height", double(opts.height)).toFloat();
    opts.depth  = options.value("depth",  double(opts.depth)).toFloat();
    opts.metalness = options.value("metalness", double(opts.metalness)).toFloat();
    opts.roughness = options.value("roughness", double(opts.roughness)).toFloat();
    opts.ignoreAlphaDiffuse = options.value("ignoreAlphaDiffuse", false).toBool();
    if (options.contains("position")) {
        opts.positionGiven = true;
        opts.position = vecFromJs(options.value("position"));
    }

    host.services->selection->select(iris::SceneNodePtr());
    auto node = host.services->sceneEdit->addDecal(textureGuid, opts);
    if (!node) {
        fail(QStringLiteral("scene.addDecal: no scene is open"));
        return QString();
    }
    // The service consumed the box/material options and (when given) the
    // position; strip them so applyOptions does not re-apply a second transform.
    QVariantMap rest = options;
    for (const char *k : { "position", "width", "height", "depth",
                           "metalness", "roughness", "ignoreAlphaDiffuse" })
        rest.remove(QString::fromLatin1(k));
    return finishAdd(rest, QStringLiteral("scene.addDecal"));
}

QString SceneApi::addMesh(const QString &path, const QVariantMap &options)
{
    if (!requireProject()) return QString();
    if (!sceneOrFail()) return QString();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        fail(QStringLiteral("scene.addMesh: no such file '%1'").arg(path));
        return QString();
    }
    host.services->selection->select(iris::SceneNodePtr());
    host.services->sceneEdit->addMesh(path, true, vecFromJs(options.value("position")));
    return finishAdd(options, QStringLiteral("scene.addMesh"));
}
