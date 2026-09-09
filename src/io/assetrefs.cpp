/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "io/assetrefs.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

#include "irisgl/document/materials/pbrmaterial.h"

namespace assetrefs {

const QStringList &nodeAssetKeys()
{
    // Every top-level key SceneWriter fills with an asset guid, by writer:
    //   mesh          writeMeshData      (a `:`-prefixed builtin path otherwise)
    //   texture       writeParticleData  (the emitter's sprite)
    //   colourRampGuid writeParticleData (the ramp image asset)
    //   iesProfile    writeLightData     (photometric profile)
    //   lightTexture  writeLightData     (projector / area mask)
    //   decalTexture/decalNormal/decalEmissive  writeDecalData
    static const QStringList kKeys = {
        QStringLiteral("mesh"),
        QStringLiteral("texture"),
        QStringLiteral("colourRampGuid"),
        QStringLiteral("iesProfile"),
        QStringLiteral("lightTexture"),
        QStringLiteral("decalTexture"),
        QStringLiteral("decalNormal"),
        QStringLiteral("decalEmissive"),
    };
    return kKeys;
}

const QStringList &materialAssetKeys()
{
    static const QStringList kKeys = [] {
        QStringList keys = QStringList(iris::PbrMaterial::mapRowNames());
        // Not a Property row and therefore not in mapRowNames: the shader
        // asset a generated Hlms piece was emitted from (scenewriter.cpp's
        // "GENERATED SHADER PIECES" block). `customPiece`/`customPieceVertex`
        // beside it are cache FILE NAMES, not guids — deliberately absent.
        keys << QStringLiteral("customPieceGraph");
        return keys;
    }();
    return kKeys;
}

const QStringList &nodeGuidKeys()
{
    static const QStringList kKeys = {
        QStringLiteral("guid"),                     // the node's own identity
        QStringLiteral("socketAttachment.owner"),   // CAMERAS_SPEC §5
        QStringLiteral("physicsProperties.constraints[].constraintFrom"),
        QStringLiteral("physicsProperties.constraints[].constraintTo"),
        QStringLiteral("focusTarget"),              // CameraNode, focusMode Track
    };
    return kKeys;
}

bool isReservedGuid(const QString &guid)
{
    return guid.startsWith(QLatin1String("00000000-0000-0000-0000-"));
}

bool isGuidValue(const QString &value)
{
    // The shape IrisUtils::generateGUID mints: a bare UUID, 8-4-4-4-12 hex.
    if (value.size() != 36) return false;
    for (int i = 0; i < 36; ++i) {
        const QChar c = value.at(i);
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != QLatin1Char('-')) return false;
        } else if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
                     (c >= QLatin1Char('a') && c <= QLatin1Char('f')) ||
                     (c >= QLatin1Char('A') && c <= QLatin1Char('F')))) {
            return false;
        }
    }
    return true;
}

namespace {

/// Every place an ASSET guid can sit in one node object (children excluded),
/// as a mutable visit: `fn(keyPath, value)` returns the value to store back
/// (the same value = no change).
template <typename Fn>
void visitAssetSlots(QJsonObject &nodeObj, const QString &prefix, Fn fn)
{
    for (const QString &key : nodeAssetKeys()) {
        const auto it = nodeObj.find(key);
        if (it == nodeObj.end() || !it->isString()) continue;
        const QString before = it->toString();
        const QString after = fn(prefix + key, before);
        if (after != before) nodeObj[key] = after;
    }

    // material.values.<texture slot>
    if (nodeObj.contains(QStringLiteral("material"))) {
        QJsonObject material = nodeObj.value(QStringLiteral("material")).toObject();
        QJsonObject values = material.value(QStringLiteral("values")).toObject();
        bool changed = false;
        for (const QString &key : materialAssetKeys()) {
            const auto it = values.find(key);
            if (it == values.end() || !it->isString()) continue;
            const QString before = it->toString();
            const QString after = fn(prefix + QStringLiteral("material.values.") + key, before);
            if (after != before) { values[key] = after; changed = true; }
        }
        if (changed) {
            material[QStringLiteral("values")] = values;
            nodeObj[QStringLiteral("material")] = material;
        }
    }

    // animations[].skeletalAnimation.guid — the skeletal CLIP asset. The
    // sibling `source` is a relative path the reader falls back to; it is not
    // a guid and is not touched.
    if (nodeObj.contains(QStringLiteral("animations"))) {
        QJsonArray animations = nodeObj.value(QStringLiteral("animations")).toArray();
        bool changed = false;
        for (int i = 0; i < animations.size(); ++i) {
            QJsonObject anim = animations.at(i).toObject();
            if (!anim.contains(QStringLiteral("skeletalAnimation"))) continue;
            QJsonObject skel = anim.value(QStringLiteral("skeletalAnimation")).toObject();
            const auto it = skel.find(QStringLiteral("guid"));
            if (it == skel.end() || !it->isString()) continue;
            const QString before = it->toString();
            const QString after = fn(prefix + QStringLiteral("animations[%1].skeletalAnimation.guid")
                                                  .arg(i), before);
            if (after == before) continue;
            skel[QStringLiteral("guid")] = after;
            anim[QStringLiteral("skeletalAnimation")] = skel;
            animations.replace(i, anim);
            changed = true;
        }
        if (changed) nodeObj[QStringLiteral("animations")] = animations;
    }

    // avatar.asset — the AvatarLink (AVATAR_ASSET_SPEC §5.4). Its own
    // dependencies (model + clips) are inside the DEFINITION, which travels as
    // a catalog asset, so they join the closure through the dependency table
    // rather than through this walk.
    if (nodeObj.contains(QStringLiteral("avatar"))) {
        QJsonObject avatar = nodeObj.value(QStringLiteral("avatar")).toObject();
        const auto it = avatar.find(QStringLiteral("asset"));
        if (it != avatar.end() && it->isString()) {
            const QString before = it->toString();
            const QString after = fn(prefix + QStringLiteral("avatar.asset"), before);
            if (after != before) {
                avatar[QStringLiteral("asset")] = after;
                nodeObj[QStringLiteral("avatar")] = avatar;
            }
        }
    }
}

/// Walks `nodeObj` and every descendant, applying `fn` to the subtree object.
template <typename Fn>
void walkSubtree(QJsonObject &nodeObj, const QString &prefix, Fn fn)
{
    fn(nodeObj, prefix);
    const auto it = nodeObj.find(QStringLiteral("children"));
    if (it == nodeObj.end() || !it->isArray()) return;
    QJsonArray children = it->toArray();
    bool changed = false;
    for (int i = 0; i < children.size(); ++i) {
        QJsonObject child = children.at(i).toObject();
        const QJsonObject before = child;
        walkSubtree(child, prefix + QStringLiteral("children[%1].").arg(i), fn);
        if (child != before) { children.replace(i, child); changed = true; }
    }
    if (changed) nodeObj[QStringLiteral("children")] = children;
}

} // namespace

QVector<Ref> collectAssetRefs(const QJsonObject &nodeObj)
{
    QVector<Ref> refs;
    QSet<QString> seen;
    QJsonObject copy = nodeObj;   // visited mutably; nothing is written back
    walkSubtree(copy, QString(), [&](QJsonObject &node, const QString &prefix) {
        visitAssetSlots(node, prefix, [&](const QString &key, const QString &value) {
            if (isGuidValue(value) && !isReservedGuid(value) && !seen.contains(value)) {
                seen.insert(value);
                refs.append({ value, key });
            }
            return value;
        });
    });
    return refs;
}

QStringList collectAssetGuids(const QJsonObject &nodeObj)
{
    QStringList out;
    for (const Ref &ref : collectAssetRefs(nodeObj)) out << ref.guid;
    return out;
}

int remapAssetGuids(QJsonObject &nodeObj, const QHash<QString, QString> &map)
{
    if (map.isEmpty()) return 0;
    int rewritten = 0;
    walkSubtree(nodeObj, QString(), [&](QJsonObject &node, const QString &prefix) {
        visitAssetSlots(node, prefix, [&](const QString &, const QString &value) {
            const auto it = map.constFind(value);
            if (it == map.constEnd()) return value;
            ++rewritten;
            return it.value();
        });
    });
    return rewritten;
}

} // namespace assetrefs
