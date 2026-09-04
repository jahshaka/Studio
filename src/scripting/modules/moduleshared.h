/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_MODULESHARED_H
#define SCRIPTING_MODULESHARED_H

// Conversions shared by the Studio API modules: JSON-native values in and out
// (SCRIPTING_SPEC — ids are GUID strings, vectors are {x,y,z}, colors are
// "#rrggbb" strings or {r,g,b} maps with 0-255 channels).

#include "irisgl/core/math/vec.h"
#include <QColor>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariant>
#include <QVariantMap>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "data/project.h"   // ModelTypes

namespace scriptmod {

/// The assets.* type vocabulary for a ModelTypes value ("object", "texture",
/// …; "undefined" for anything unmapped). Shared by every module that reports
/// asset rows (assets.list/metadata, the export verbs' manifests).
inline QString assetTypeName(int type)
{
    switch (static_cast<ModelTypes>(type)) {
    case ModelTypes::Material: return QStringLiteral("material");
    case ModelTypes::Texture: return QStringLiteral("texture");
    case ModelTypes::Video: return QStringLiteral("video");
    case ModelTypes::Sky: return QStringLiteral("sky");
    case ModelTypes::Object: return QStringLiteral("object");
    case ModelTypes::Mesh: return QStringLiteral("mesh");
    case ModelTypes::SoundEffect: return QStringLiteral("soundeffect");
    case ModelTypes::Music: return QStringLiteral("music");
    case ModelTypes::Shader: return QStringLiteral("shader");
    case ModelTypes::Variant: return QStringLiteral("variant");
    case ModelTypes::File: return QStringLiteral("file");
    case ModelTypes::ParticleSystem: return QStringLiteral("particles");
    case ModelTypes::LightProfile: return QStringLiteral("lightprofile");
    default: return QStringLiteral("undefined");
    }
}

inline QVariantMap vecToJs(const iris::Vec3 &v)
{
    return { { "x", v.x() }, { "y", v.y() }, { "z", v.z() } };
}

/// A bare QVariant parameter receives the raw QJSValue wrapper (only typed
/// parameters like QVariantMap force conversion); JSON types can appear too.
/// Normalize everything to plain QVariantMap/List/scalars first.
inline QVariant normalizeJs(const QVariant &value)
{
    if (value.userType() == qMetaTypeId<QJSValue>())
        return value.value<QJSValue>().toVariant();
    if (value.typeId() == QMetaType::QJsonObject)
        return value.toJsonObject().toVariantMap();
    if (value.typeId() == QMetaType::QJsonArray)
        return value.toJsonArray().toVariantList();
    return value;
}

/// Accepts {x,y,z} maps and [x,y,z] arrays; missing components fall back to
/// the given default (so {y: 2} nudges one axis).
inline iris::Vec3 vecFromJs(const QVariant &raw, const iris::Vec3 &fallback = iris::Vec3())
{
    const QVariant value = normalizeJs(raw);
    if (value.typeId() == QMetaType::QVariantMap) {
        const auto m = value.toMap();
        return iris::Vec3(m.value("x", fallback.x()).toFloat(),
                         m.value("y", fallback.y()).toFloat(),
                         m.value("z", fallback.z()).toFloat());
    }
    if (value.typeId() == QMetaType::QVariantList) {
        const auto l = value.toList();
        return iris::Vec3(l.value(0, fallback.x()).toFloat(),
                         l.value(1, fallback.y()).toFloat(),
                         l.value(2, fallback.z()).toFloat());
    }
    return fallback;
}

inline QString colorToJs(const QColor &c)
{
    return c.name(c.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb);
}

/// Accepts "#rrggbb"/named strings and {r,g,b[,a]} maps (0-255 channels).
inline QColor colorFromJs(const QVariant &raw, const QColor &fallback = QColor())
{
    const QVariant value = normalizeJs(raw);
    if (value.typeId() == QMetaType::QVariantMap) {
        const auto m = value.toMap();
        return QColor(m.value("r", fallback.red()).toInt(),
                      m.value("g", fallback.green()).toInt(),
                      m.value("b", fallback.blue()).toInt(),
                      m.value("a", fallback.alpha() ? fallback.alpha() : 255).toInt());
    }
    const QColor named(value.toString());
    return named.isValid() ? named : fallback;
}

/// Depth-first search of the document by GUID.
inline iris::SceneNodePtr findNodeByGuid(const iris::SceneNodePtr &node, const QString &guid)
{
    if (!node) return iris::SceneNodePtr();
    if (node->getGUID() == guid) return node;
    for (const auto &child : node->children) {
        auto hit = findNodeByGuid(child, guid);
        if (hit) return hit;
    }
    return iris::SceneNodePtr();
}

inline QString nodeTypeName(iris::SceneNodeType type)
{
    switch (type) {
    case iris::SceneNodeType::Empty:          return QStringLiteral("empty");
    case iris::SceneNodeType::ParticleSystem: return QStringLiteral("particles");
    case iris::SceneNodeType::Mesh:           return QStringLiteral("mesh");
    case iris::SceneNodeType::Light:          return QStringLiteral("light");
    case iris::SceneNodeType::Camera:         return QStringLiteral("camera");
    case iris::SceneNodeType::Viewer:         return QStringLiteral("viewer");
    case iris::SceneNodeType::Decal:          return QStringLiteral("decal");
    }
    return QStringLiteral("unknown");
}

inline QVariantMap nodeToJs(const iris::SceneNodePtr &node)
{
    QVariantMap m;
    m["id"] = node->getGUID();
    m["name"] = node->getName();
    m["type"] = nodeTypeName(node->getSceneNodeType());
    auto parentNode = node->getParent();
    m["parent"] = parentNode ? parentNode->getGUID() : QString();
    m["position"] = vecToJs(node->getLocalPos());
    m["rotation"] = vecToJs(node->getLocalRot().toEulerAngles());
    m["scale"] = vecToJs(node->getLocalScale());
    return m;
}

} // namespace scriptmod

#endif // SCRIPTING_MODULESHARED_H
