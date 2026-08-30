/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "nodeapi.h"

#include "moduleshared.h"
#include "../../commands/reparentscenenodecommand.h"
#include "../../commands/transfrormscenenodecommand.h"
#include "../../mainwindow.h"
#include "../../services/sceneeditservice.h"
#include "../../services/services.h"
#include "../../services/undoservice.h"

using namespace scriptmod;

QVector<VerbInfo> NodeApi::verbs() const
{
    return {
        { "remove", "node.remove(id) -> bool",
          "Deletes the node (undoable; its DB asset row is only dropped once the delete can no longer be undone).",
          Needs::Document },
        { "duplicate", "node.duplicate(id) -> newId",
          "Duplicates the node under the same parent. Undoable.",
          Needs::Document },
        { "reparent", "node.reparent(id, parentId) -> bool",
          "Moves the node under a new parent, keeping its world pose; cycles are refused. Undoable.",
          Needs::Document },
        { "transform", "node.transform(id, {position, rotation, scale}) -> {position, rotation, scale}",
          "Sets any of position/rotation/scale (absolute; rotation in euler degrees; omitted parts keep their value) and returns the result. Undoable.",
          Needs::Document },
        { "property", "node.property(id, key) -> value",
          "Reads a reflected property (position, rotation, scale; lights add intensity, lightColor, distance, spotCutOff, spotCutOffSoftness, rectWidth, rectHeight).",
          Needs::Document },
        { "setProperty", "node.setProperty(id, key, value) -> bool",
          "Writes a reflected property (same keys as node.property). Direct document write — not undoable yet.",
          Needs::Document },
        { "info", "node.info(id) -> {id, name, type, parent, position, rotation, scale}",
          "Everything scene.nodes() reports, for one node.",
          Needs::Document },
    };
}

iris::SceneNodePtr NodeApi::nodeOrFail(const QString &id, const QString &verb)
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                              : iris::ScenePtr();
    if (!scene) {
        fail(QStringLiteral("%1: no scene is open").arg(verb));
        return iris::SceneNodePtr();
    }
    auto node = findNodeByGuid(scene->getRootNode(), id);
    if (!node) fail(QStringLiteral("%1: no node with id '%2'").arg(verb, id));
    return node;
}

bool NodeApi::remove(const QString &id)
{
    auto node = nodeOrFail(id, QStringLiteral("node.remove"));
    if (!node) return false;
    if (!host.services->sceneEdit->deleteNode(node))
        return fail(QStringLiteral("node.remove: '%1' is not removable").arg(node->getName()));
    return true;
}

QString NodeApi::duplicate(const QString &id)
{
    auto node = nodeOrFail(id, QStringLiteral("node.duplicate"));
    if (!node) return QString();
    auto copy = host.services->sceneEdit->duplicateNode(node);
    if (!copy) {
        fail(QStringLiteral("node.duplicate: '%1' is not duplicable").arg(node->getName()));
        return QString();
    }
    return copy->getGUID();
}

bool NodeApi::reparent(const QString &id, const QString &parentId)
{
    auto node = nodeOrFail(id, QStringLiteral("node.reparent"));
    if (!node) return false;
    auto parent = nodeOrFail(parentId, QStringLiteral("node.reparent"));
    if (!parent) return false;
    if (ReparentSceneNodeCommand::wouldCreateCycle(node, parent))
        return fail("node.reparent: that would create a cycle");
    host.services->undo->push(new ReparentSceneNodeCommand(node, parent));
    return true;
}

QVariantMap NodeApi::transform(const QString &id, const QVariantMap &change)
{
    auto node = nodeOrFail(id, QStringLiteral("node.transform"));
    if (!node) return QVariantMap();

    const QVector3D pos = vecFromJs(change.value("position"), node->getLocalPos());
    const QVector3D rotEuler = vecFromJs(change.value("rotation"), node->getLocalRot().toEulerAngles());
    const QVector3D scale = vecFromJs(change.value("scale"), node->getLocalScale());

    host.services->undo->push(new TransformSceneNodeCommand(
        node, pos, QQuaternion::fromEulerAngles(rotEuler), scale));

    return { { "position", vecToJs(node->getLocalPos()) },
             { "rotation", vecToJs(node->getLocalRot().toEulerAngles()) },
             { "scale", vecToJs(node->getLocalScale()) } };
}

QVariant NodeApi::property(const QString &id, const QString &key)
{
    auto node = nodeOrFail(id, QStringLiteral("node.property"));
    if (!node) return QVariant();
    const QVariant value = node->getPropertyValue(key);
    if (!value.isValid()) {
        fail(QStringLiteral("node.property: '%1' has no property '%2'").arg(node->getName(), key));
        return QVariant();
    }
    switch (value.typeId()) {
    case QMetaType::QVector3D: return vecToJs(value.value<QVector3D>());
    case QMetaType::QColor:    return colorToJs(value.value<QColor>());
    default:                   return value;
    }
}

bool NodeApi::setProperty(const QString &id, const QString &key, const QVariant &rawValue)
{
    auto node = nodeOrFail(id, QStringLiteral("node.setProperty"));
    if (!node) return false;
    const QVariant value = normalizeJs(rawValue);

    // The current value tells us the target type — JS maps/strings convert to
    // the vector/colour the document field expects.
    const QVariant current = node->getPropertyValue(key);
    if (!current.isValid())
        return fail(QStringLiteral("node.setProperty: '%1' has no property '%2'").arg(node->getName(), key));

    QVariant converted = value;
    switch (current.typeId()) {
    case QMetaType::QVector3D:
        converted = QVariant::fromValue(vecFromJs(value, current.value<QVector3D>()));
        break;
    case QMetaType::QColor:
        converted = QVariant::fromValue(colorFromJs(value, current.value<QColor>()));
        break;
    case QMetaType::Float:
    case QMetaType::Double:
        converted = value.toFloat();
        break;
    default:
        break;
    }

    if (!node->setPropertyValue(key, converted))
        return fail(QStringLiteral("node.setProperty: '%1' rejected property '%2'").arg(node->getName(), key));
    return true;
}

QVariant NodeApi::info(const QString &id)
{
    auto node = nodeOrFail(id, QStringLiteral("node.info"));
    if (!node) return QVariant();
    return nodeToJs(node);
}
