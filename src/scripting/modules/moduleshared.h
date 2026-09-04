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

#include "irisgl/core/math/qtinterop.h"
#include "irisgl/core/math/vec.h"
#include <QColor>
#include <QVector3D>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariant>
#include <QVariantMap>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/properties/property.h"
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
///
/// `ok` (AI_SURFACE_AUDIT F8) reports whether the value was UNDERSTOOD. It used
/// to be impossible to tell: an unparseable colour string silently returned the
/// old value and every caller then reported success, so `world.ambient("hot
/// pink")` or a typo'd hex answered `true` and changed nothing. Callers that
/// pass `ok` must fail loudly; the default (nullptr) keeps the old
/// keep-the-fallback behaviour for the read paths that have no error channel.
inline QColor colorFromJs(const QVariant &raw, const QColor &fallback = QColor(),
                          bool *ok = nullptr)
{
    if (ok) *ok = true;
    const QVariant value = normalizeJs(raw);
    if (value.typeId() == QMetaType::QVariantMap) {
        const auto m = value.toMap();
        // A channel that is present but not a number is the map-shaped version
        // of the same lie ({r: "ff"} used to read as 0).
        for (const char *channel : { "r", "g", "b", "a" }) {
            const QString key = QString::fromLatin1(channel);
            if (!m.contains(key)) continue;
            bool numeric = false;
            m.value(key).toDouble(&numeric);
            if (!numeric && ok) *ok = false;
        }
        return QColor(m.value("r", fallback.red()).toInt(),
                      m.value("g", fallback.green()).toInt(),
                      m.value("b", fallback.blue()).toInt(),
                      m.value("a", fallback.alpha() ? fallback.alpha() : 255).toInt());
    }
    const QColor named(value.toString());
    if (named.isValid()) return named;
    if (ok) *ok = false;
    return fallback;
}

/// The one error sentence every colour-refusal shares, so the model learns the
/// accepted spellings once.
inline QString colorHelp(const QVariant &raw)
{
    return QStringLiteral("'%1' is not a colour — use \"#rrggbb\"/\"#aarrggbb\", "
                          "an SVG colour name (\"red\"), or {r,g,b[,a]} with 0-255 channels")
        .arg(normalizeJs(raw).toString());
}

/// The script-facing name of a Property kind. `file` is deliberately reported
/// as "string": FileProperty is what irisgl uses for every QString-valued row
/// (there is no StringProperty), so a node's `name` and a particle emitter's
/// `shape` are FileProperties and calling them "file" would be a lie.
inline QString propertyTypeName(iris::PropertyType type)
{
    switch (type) {
    case iris::PropertyType::Bool:    return QStringLiteral("bool");
    case iris::PropertyType::Int:     return QStringLiteral("int");
    case iris::PropertyType::Float:   return QStringLiteral("float");
    case iris::PropertyType::Vec2:    return QStringLiteral("vec2");
    case iris::PropertyType::Vec3:    return QStringLiteral("vec3");
    case iris::PropertyType::Vec4:    return QStringLiteral("vec4");
    case iris::PropertyType::Color:   return QStringLiteral("color");
    case iris::PropertyType::Texture: return QStringLiteral("texture");
    case iris::PropertyType::File:    return QStringLiteral("string");
    case iris::PropertyType::List:    return QStringLiteral("list");
    case iris::PropertyType::None:    break;
    }
    return QStringLiteral("unknown");
}

/// A Property row in the shape node.properties()/material.properties() report:
/// {name, displayName, type, value} plus min/max ONLY when a range was actually
/// declared.
///
/// The min/max rule is the whole point of the row (AI_SURFACE_PROGRAM_SPEC
/// §3.A): IntProperty/FloatProperty left minValue/maxValue uninitialised until
/// this program, and no scene-node getProperties() has ever assigned them, so
/// reporting them unconditionally would hand a model two indeterminate numbers
/// as fact. They are zero-initialised now, and `max > min` is the declared-range
/// test — an undeclared range is ABSENT from the object rather than reported as
/// 0..0, which a model would read as "this value must be zero".
inline QVariantMap propertyRowToJs(iris::Property *prop)
{
    QVariantMap row;
    if (!prop) return row;
    row["name"] = prop->name;
    row["displayName"] = prop->displayName;
    row["type"] = propertyTypeName(prop->type);

    const QVariant value = prop->getValue();
    switch (value.typeId()) {
    case QMetaType::QColor:    row["value"] = colorToJs(value.value<QColor>()); break;
    case QMetaType::QVector3D: row["value"] = vecToJs(iris::fromQt(value.value<QVector3D>())); break;
    default:                   row["value"] = value; break;
    }

    if (auto *f = dynamic_cast<iris::FloatProperty *>(prop)) {
        if (f->maxValue > f->minValue) { row["min"] = f->minValue; row["max"] = f->maxValue; }
    } else if (auto *i = dynamic_cast<iris::IntProperty *>(prop)) {
        if (i->maxValue > i->minValue) { row["min"] = i->minValue; row["max"] = i->maxValue; }
    }
    return row;
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
