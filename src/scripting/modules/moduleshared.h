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

#include "irisgl/core/math/quat.h"
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
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
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

/// Accepts BOTH rotation spellings the surface already uses:
///   {x,y,z,scalar}  a quaternion, exactly what editor.camera() hands back
///                   ("w" is accepted as an alias for "scalar")
///   {x,y,z}         Euler degrees (pitch, yaw, roll) — what node.info() and
///                   node.transform() report — and [x,y,z] arrays likewise.
/// `ok` reports whether the value was UNDERSTOOD (the F8 rule: a rotation a
/// verb cannot parse must fail loudly, never silently keep the old one).
inline iris::Quat quatFromJs(const QVariant &raw, const iris::Quat &fallback = iris::Quat(),
                             bool *ok = nullptr)
{
    if (ok) *ok = true;
    const QVariant value = normalizeJs(raw);
    if (value.typeId() == QMetaType::QVariantMap) {
        const auto m = value.toMap();
        const bool quaternion = m.contains(QStringLiteral("scalar")) || m.contains(QStringLiteral("w"));
        if (quaternion) {
            const QVariant w = m.contains(QStringLiteral("scalar")) ? m.value(QStringLiteral("scalar"))
                                                                    : m.value(QStringLiteral("w"));
            iris::Quat q(w.toFloat(), m.value("x").toFloat(),
                         m.value("y").toFloat(), m.value("z").toFloat());
            if (q.length() < 1e-4f) { if (ok) *ok = false; return fallback; }
            q.normalize();
            return q;
        }
        if (m.contains(QStringLiteral("x")) || m.contains(QStringLiteral("y"))
            || m.contains(QStringLiteral("z")))
            return iris::Quat::fromEulerAngles(vecFromJs(value));
    }
    if (value.typeId() == QMetaType::QVariantList && value.toList().size() >= 3)
        return iris::Quat::fromEulerAngles(vecFromJs(value));
    if (ok) *ok = false;
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

// ---- optional per-node enrichment (AI_SURFACE_PROGRAM_SPEC lane B #8) ------
//
// The blocks scene.nodes({include: […]}) can attach, and therefore what
// describe_scene's `include` carries. They are HERE, beside nodeToJs, so the
// tool and the verb serialize identically by construction — the tool is only
// ever the byte-carrying view of the verb.

inline QString lightTypeName(iris::LightType type)
{
    switch (type) {
    case iris::LightType::Point:       return QStringLiteral("point");
    case iris::LightType::Directional: return QStringLiteral("directional");
    case iris::LightType::Spot:        return QStringLiteral("spot");
    case iris::LightType::Area:        return QStringLiteral("area");
    }
    return QStringLiteral("unknown");
}

/// The light parameters, or an empty map for a node that is not a light. Only
/// the rows that MEAN something for that light type: a point light has no
/// spot cone and a spot light has no rectangle, and reporting them anyway
/// teaches a model to set fields that do nothing.
inline QVariantMap lightToJs(const iris::SceneNodePtr &node)
{
    QVariantMap m;
    if (!node || node->getSceneNodeType() != iris::SceneNodeType::Light) return m;
    auto light = node.staticCast<iris::LightNode>();
    m["lightType"] = lightTypeName(light->lightType);
    m["color"] = colorToJs(light->color);
    m["intensity"] = light->intensity;
    m["shadowAlpha"] = light->shadowAlpha;
    m["shadowColor"] = colorToJs(light->shadowColor);
    if (light->lightType != iris::LightType::Directional)
        m["distance"] = light->distance;
    if (light->lightType == iris::LightType::Spot) {
        m["spotCutOff"] = light->spotCutOff;
        m["spotCutOffSoftness"] = light->spotCutOffSoftness;
    }
    if (light->lightType == iris::LightType::Area) {
        m["rectWidth"] = light->rectWidth;
        m["rectHeight"] = light->rectHeight;
        m["doubleSided"] = light->doubleSided;
        m["accurate"] = light->accurate;
    }
    if (!light->iesProfileGuid.isEmpty()) m["lightProfile"] = light->iesProfileGuid;
    if (!light->lightTextureGuid.isEmpty()) m["lightTexture"] = light->lightTextureGuid;
    return m;
}

/// A mesh node's material as a SUMMARY, not as material.get()'s full property
/// dump: what a model needs to decide whether to look closer (the class, the
/// handful of numbers that describe the look, and which texture slots are in
/// use). material.get(id) is still the full read.
inline QVariantMap materialSummaryToJs(const iris::SceneNodePtr &node)
{
    QVariantMap m;
    if (!node || node->getSceneNodeType() != iris::SceneNodeType::Mesh) return m;
    auto material = node.staticCast<iris::MeshNode>()->getMaterial();
    if (!material) return m;
    if (auto pbr = material.dynamicCast<iris::PbrMaterial>()) {
        static const char *kAlphaModes[] = { "opaque", "cutout", "blend", "glass",
                                             "additive", "modulate", "refractive" };
        m["class"] = QStringLiteral("pbr");
        m["baseColor"] = colorToJs(pbr->baseColor);
        m["metallic"] = pbr->metallicFactor;
        m["roughness"] = pbr->roughnessFactor;
        m["emissiveColor"] = colorToJs(pbr->emissiveColor);
        m["emissiveIntensity"] = pbr->emissiveIntensity;
        m["alpha"] = pbr->alpha;
        m["alphaMode"] = kAlphaModes[qBound(0, pbr->alphaMode, 6)];
        QVariantList maps;
        if (pbr->useBaseColorMap) maps << QStringLiteral("baseColor");
        if (pbr->useMetallicMap)  maps << QStringLiteral("metallic");
        if (pbr->useRoughnessMap) maps << QStringLiteral("roughness");
        if (pbr->useNormalMap)    maps << QStringLiteral("normal");
        if (pbr->useOcclusionMap) maps << QStringLiteral("occlusion");
        if (pbr->useEmissiveMap)  maps << QStringLiteral("emissive");
        m["maps"] = maps;
    } else if (auto custom = material.dynamicCast<iris::CustomMaterial>()) {
        // The base Material carries no name — only CustomMaterial does.
        m["class"] = QStringLiteral("custom");
        m["name"] = custom->getName();
    } else {
        m["class"] = QStringLiteral("material");
    }
    return m;
}

/// Visibility, with the answer a caller actually wants: `visible` is the
/// node's own flag, `visibleInScene` is false as soon as ANY ancestor is
/// hidden — which is why a node can be `visible: true` and still not be on
/// screen, a question that used to need a manual parent walk.
inline QVariantMap visibilityToJs(const iris::SceneNodePtr &node)
{
    QVariantMap m;
    if (!node) return m;
    bool inherited = node->isVisible();
    for (auto p = node->getParent(); p && inherited; p = p->getParent())
        inherited = p->isVisible();
    m["visible"] = node->isVisible();
    m["visibleInScene"] = inherited;
    return m;
}

} // namespace scriptmod

#endif // SCRIPTING_MODULESHARED_H
