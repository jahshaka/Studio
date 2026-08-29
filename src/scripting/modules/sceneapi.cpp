/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "sceneapi.h"

#include <QFileInfo>

#include "moduleshared.h"
#include "../../commands/reparentscenenodecommand.h"
#include "../../commands/transfrormscenenodecommand.h"
#include "../../mainwindow.h"
#include "../../uimanager.h"

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
    };
}

iris::ScenePtr SceneApi::sceneOrFail()
{
    auto scene = host.mainWindow ? host.mainWindow->getScene() : iris::ScenePtr();
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
        auto scene = host.mainWindow->getScene();
        auto parent = findNodeByGuid(scene->getRootNode(), options.value("parent").toString());
        if (!parent)
            return fail(QStringLiteral("%1: no node with id '%2' for parent").arg(verb, options.value("parent").toString()));
        if (ReparentSceneNodeCommand::wouldCreateCycle(node, parent))
            return fail(QStringLiteral("%1: reparenting would create a cycle").arg(verb));
        UiManager::pushUndoStack(new ReparentSceneNodeCommand(node, parent));
    }
    if (options.contains("position") || options.contains("rotation") || options.contains("scale")) {
        const QVector3D pos = vecFromJs(options.value("position"), node->getLocalPos());
        const QVector3D rotEuler = vecFromJs(options.value("rotation"), node->getLocalRot().toEulerAngles());
        const QVector3D scale = vecFromJs(options.value("scale"), node->getLocalScale());
        UiManager::pushUndoStack(new TransformSceneNodeCommand(
            node, pos, QQuaternion::fromEulerAngles(rotEuler), scale));
    }
    return true;
}

QString SceneApi::finishAdd(const QVariantMap &options, const QString &verb)
{
    // The add-verb funnel selects the new node (AddSceneNodeCommand::redo);
    // callers deselected beforehand so a silent failure reads as "no node".
    auto node = host.mainWindow->selectedSceneNode();
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

    host.mainWindow->sceneNodeSelected(iris::SceneNodePtr());
    host.mainWindow->addPrimitiveObject(normalized);
    return finishAdd(options, QStringLiteral("scene.addPrimitive"));
}

QString SceneApi::addLight(const QString &type, const QVariantMap &options)
{
    if (!sceneOrFail()) return QString();
    const QString t = type.trimmed().toLower();

    host.mainWindow->sceneNodeSelected(iris::SceneNodePtr());
    if (t == "point")            host.mainWindow->addPointLight();
    else if (t == "spot")        host.mainWindow->addSpotLight();
    else if (t == "directional") host.mainWindow->addDirectionalLight();
    else if (t == "area")        host.mainWindow->addAreaLight();
    else {
        fail(QStringLiteral("scene.addLight: unknown type '%1' (point, spot, directional, area)").arg(type));
        return QString();
    }
    return finishAdd(options, QStringLiteral("scene.addLight"));
}

QString SceneApi::addEmpty(const QVariantMap &options)
{
    if (!sceneOrFail()) return QString();
    host.mainWindow->sceneNodeSelected(iris::SceneNodePtr());
    host.mainWindow->addEmpty();
    return finishAdd(options, QStringLiteral("scene.addEmpty"));
}

QString SceneApi::addMesh(const QString &path, const QVariantMap &options)
{
    if (!requireProject()) return QString();
    if (!sceneOrFail()) return QString();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        fail(QStringLiteral("scene.addMesh: no such file '%1'").arg(path));
        return QString();
    }
    host.mainWindow->sceneNodeSelected(iris::SceneNodePtr());
    host.mainWindow->addMesh(path, true, vecFromJs(options.value("position")));
    return finishAdd(options, QStringLiteral("scene.addMesh"));
}
