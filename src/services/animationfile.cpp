/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/animationfile.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QXmlStreamReader>
#include <QtEndian>

#include <algorithm>
#include <functional>

#include "assimp/Importer.hpp"
#include "assimp/anim.h"
#include "assimp/scene.h"

#include "data/constants.h"
#include "irisgl/import/importflags.h"
#include "services/rigsignature.h"

namespace animfile {

namespace {

// ---------------------------------------------------------------- glTF / GLB
//
// Both are JSON. A .glb's first chunk IS the JSON document, so the structure
// costs one 12-byte header read plus the chunk — never the binary payload.

QJsonObject gltfHeader(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QJsonObject();

    const QByteArray magic = file.peek(4);
    if (magic == QByteArrayLiteral("glTF")) {
        const QByteArray header = file.read(20);
        if (header.size() != 20) return QJsonObject();
        const quint32 chunkLen = qFromLittleEndian<quint32>(header.constData() + 12);
        const quint32 chunkType = qFromLittleEndian<quint32>(header.constData() + 16);
        if (chunkType != 0x4E4F534A /* 'JSON' */) return QJsonObject();
        return QJsonDocument::fromJson(file.read(chunkLen)).object();
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

Shape shapeOfGltf(const QString &path)
{
    const QJsonObject root = gltfHeader(path);
    if (root.isEmpty()) return Shape::Unknown;          // unreadable: let assimp say why
    const int meshes = root.value(QStringLiteral("meshes")).toArray().size();
    const int anims = root.value(QStringLiteral("animations")).toArray().size();
    if (meshes > 0) return Shape::Geometry;
    return anims > 0 ? Shape::AnimationOnly : Shape::Unknown;
}

// ----------------------------------------------------------------- binary FBX
//
// The FBX binary container is a tree of records: [endOffset, numProperties,
// propertyListLen, nameLen, name, properties…, children…, null record]. Every
// record knows where it ENDS, so counting the object classes under the
// top-level `Objects` node is a walk over ~70 record headers with no payload
// read at all — a 53 MB character file classifies in microseconds. (Offsets
// are 32-bit below FBX 7500 and 64-bit from 7500 on; Mixamo ships 7700.)
//
// The classes are the file's own vocabulary: a `Geometry` object is a mesh, an
// `AnimationStack` is a clip. A download "without skin" has zero of the first
// and one of the second — measured across the owner's 23 Mixamo files, where
// this exactly separates the 10 clip files from the 13 characters.

struct FbxRecord
{
    quint64 end = 0;
    quint64 propertyListLen = 0;
    QByteArray name;
    qint64 childrenStart = 0;
};

bool readFbxRecord(QFile &file, qint64 offset, bool wide, FbxRecord *out)
{
    if (!file.seek(offset)) return false;
    const int headerBytes = wide ? 25 : 13;
    const QByteArray header = file.read(headerBytes);
    if (header.size() != headerBytes) return false;

    quint8 nameLen = 0;
    if (wide) {
        out->end = qFromLittleEndian<quint64>(header.constData());
        out->propertyListLen = qFromLittleEndian<quint64>(header.constData() + 16);
        nameLen = quint8(header.at(24));
    } else {
        out->end = qFromLittleEndian<quint32>(header.constData());
        out->propertyListLen = qFromLittleEndian<quint32>(header.constData() + 8);
        nameLen = quint8(header.at(12));
    }
    if (out->end == 0) return false;                     // the null record: end of a list
    out->name = file.read(nameLen);
    if (out->name.size() != nameLen) return false;
    out->childrenStart = qint64(file.pos() + out->propertyListLen);
    return true;
}

Shape shapeOfFbx(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return Shape::Unknown;
    const QByteArray head = file.read(27);
    if (head.size() != 27 || !head.startsWith(QByteArrayLiteral("Kaydara FBX Binary")))
        return Shape::Unknown;                           // ASCII FBX: assimp decides
    const quint32 version = qFromLittleEndian<quint32>(head.constData() + 23);
    const bool wide = version >= 7500;
    const qint64 fileEnd = file.size();

    qint64 offset = 27;
    while (offset > 0 && offset < fileEnd) {
        FbxRecord record;
        if (!readFbxRecord(file, offset, wide, &record)) break;
        if (record.name != QByteArrayLiteral("Objects")) {
            offset = qint64(record.end);
            continue;
        }
        int geometry = 0, animations = 0;
        qint64 child = record.childrenStart;
        while (child > 0 && child < qint64(record.end)) {
            FbxRecord object;
            if (!readFbxRecord(file, child, wide, &object)) break;
            if (object.name == QByteArrayLiteral("Geometry")) ++geometry;
            else if (object.name == QByteArrayLiteral("AnimationStack")) ++animations;
            child = qint64(object.end);
        }
        if (geometry > 0) return Shape::Geometry;
        return animations > 0 ? Shape::AnimationOnly : Shape::Unknown;
    }
    return Shape::Unknown;
}

// -------------------------------------------------------------------- COLLADA
//
// Streaming, and it stops at the first <geometry>: a character file answers
// after its first library, a clip file after the document.

Shape shapeOfCollada(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return Shape::Unknown;
    QXmlStreamReader xml(&file);
    bool animation = false, wellFormed = false;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) continue;
        wellFormed = true;
        const auto name = xml.name();
        if (name == QLatin1String("geometry")) return Shape::Geometry;
        if (name == QLatin1String("animation")) animation = true;
    }
    if (xml.hasError() || !wellFormed) return Shape::Unknown;
    return animation ? Shape::AnimationOnly : Shape::Unknown;
}

// ------------------------------------------------------------------ pose strip

/// Interpolated local transform of one animated node at `tick`.
aiMatrix4x4 sampleChannel(const aiNodeAnim *channel, double tick)
{
    auto lerpKey = [&](auto *keys, unsigned count, auto fallback) {
        using KeyType = decltype(fallback);
        if (count == 0) return fallback;
        if (count == 1 || tick <= keys[0].mTime) return KeyType(keys[0].mValue);
        for (unsigned i = 1; i < count; ++i) {
            if (tick > keys[i].mTime) continue;
            const double span = keys[i].mTime - keys[i - 1].mTime;
            const float f = span > 0.0 ? float((tick - keys[i - 1].mTime) / span) : 0.0f;
            KeyType a(keys[i - 1].mValue), b(keys[i].mValue);
            if constexpr (std::is_same_v<KeyType, aiQuaternion>) {
                aiQuaternion out;
                aiQuaternion::Interpolate(out, a, b, f);
                out.Normalize();
                return out;
            } else {
                return KeyType(a + (b - a) * f);
            }
        }
        return KeyType(keys[count - 1].mValue);
    };

    const aiVector3D position = lerpKey(channel->mPositionKeys, channel->mNumPositionKeys,
                                        aiVector3D(0, 0, 0));
    const aiQuaternion rotation = lerpKey(channel->mRotationKeys, channel->mNumRotationKeys,
                                          aiQuaternion(1, 0, 0, 0));
    const aiVector3D scale = lerpKey(channel->mScalingKeys, channel->mNumScalingKeys,
                                     aiVector3D(1, 1, 1));

    aiMatrix4x4 out(scale, rotation, position);
    return out;
}

struct Segment
{
    QPointF a, b;
};

/// The extent of the drawn poses, accumulated point by point. NOT a QRectF
/// united with degenerate rects: a rect of zero size is `isNull()`, and
/// uniting with one is a no-op — the bounds would never leave the origin.
struct Bounds
{
    double minX = 0, maxX = 0, minY = 0, maxY = 0;
    bool empty = true;
    void add(const QPointF &p)
    {
        if (empty) { minX = maxX = p.x(); minY = maxY = p.y(); empty = false; return; }
        minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y()); maxY = std::max(maxY, p.y());
    }
    double width() const { return maxX - minX; }
    double height() const { return maxY - minY; }
    double centreX() const { return 0.5 * (minX + maxX); }
};

/// Joint positions of one pose, as parent→child segments in FILE space
/// (x right, y up — the projection every DCC front view uses).
QVector<Segment> posePlanar(const aiScene *scene, const aiAnimation *anim, double tick,
                            Bounds *boundsInOut)
{
    QMap<QString, const aiNodeAnim *> channels;
    for (unsigned i = 0; i < anim->mNumChannels; ++i)
        channels.insert(QString::fromUtf8(anim->mChannels[i]->mNodeName.C_Str()),
                        anim->mChannels[i]);

    QVector<Segment> segments;
    std::function<void(const aiNode *, const aiMatrix4x4 &, bool)> walk =
        [&](const aiNode *node, const aiMatrix4x4 &parentXform, bool parentPlaced) {
            if (!node) return;
            const QString name = QString::fromUtf8(node->mName.C_Str());
            const auto channel = channels.constFind(name);
            const aiMatrix4x4 local = channel != channels.constEnd()
                                          ? sampleChannel(*channel, tick)
                                          : node->mTransformation;
            const aiMatrix4x4 global = parentXform * local;
            const aiVector3D p(global.a4, global.b4, global.c4);
            const aiVector3D parentPos(parentXform.a4, parentXform.b4, parentXform.c4);

            const QPointF here(p.x, p.y);
            if (parentPlaced) {
                const QPointF from(parentPos.x, parentPos.y);
                segments.append({ from, here });
                boundsInOut->add(here);
                boundsInOut->add(from);
            }
            for (unsigned i = 0; i < node->mNumChildren; ++i)
                walk(node->mChildren[i], global, true);
        };
    walk(scene->mRootNode, aiMatrix4x4(), false);
    return segments;
}

QImage drawPoseStrip(const aiScene *scene, int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(24, 24, 28));
    if (!scene || scene->mNumAnimations == 0 || !scene->mRootNode) return image;

    const aiAnimation *anim = scene->mAnimations[0];
    if (!anim || anim->mNumChannels == 0 || anim->mDuration <= 0.0) return image;

    // Three poses across the clip — enough to read a walk from a stride, and
    // enough to tell two clip files apart at tile size.
    const double samples[3] = { anim->mDuration * 0.1, anim->mDuration * 0.5,
                                anim->mDuration * 0.9 };
    Bounds bounds;
    QVector<Segment> poses[3];
    for (int i = 0; i < 3; ++i) poses[i] = posePlanar(scene, anim, samples[i], &bounds);
    if (bounds.empty || bounds.height() <= 0.0) return image;

    // ONE transform for all three poses (so root motion reads as motion), and
    // uniform scale (so a skeleton is never stretched into a caricature).
    const double margin = 0.08 * height;
    const double cellWidth = double(width) / 3.0;
    const double scale = std::min((cellWidth - 2 * margin) / std::max(bounds.width(), 1e-4),
                                  (height - 2 * margin) / bounds.height());

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < 3; ++i) {
        const double originX = cellWidth * (i + 0.5);
        const double originY = height - margin;
        const double centreX = bounds.centreX();
        auto project = [&](const QPointF &p) {
            return QPointF(originX + (p.x() - centreX) * scale,
                           originY - (p.y() - bounds.minY) * scale);
        };
        // The middle pose is the bright one: a thumbnail needs a subject.
        const int alpha = i == 1 ? 255 : 130;
        painter.setPen(QPen(QColor(120, 200, 255, alpha), i == 1 ? 2.0 : 1.4));
        for (const auto &segment : poses[i])
            painter.drawLine(project(segment.a), project(segment.b));
    }
    painter.end();
    return image;
}

}   // namespace

Shape shapeOf(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("bvh")) {
        // MOCAP: the format has no geometry at all. (assimp SYNTHESISES a
        // stick-figure mesh for one — SkeletonMeshBuilder — which is exactly
        // why a .bvh must never reach the mesh importer.)
        return Shape::AnimationOnly;
    }
    if (!Constants::MODEL_EXTS.contains(suffix)) return Shape::NotAModel;
    if (suffix == QStringLiteral("obj") || suffix == QStringLiteral("ply")
        || suffix == QStringLiteral("stl"))
        return Shape::Geometry;                          // formats with no animation at all
    if (suffix == QStringLiteral("glb") || suffix == QStringLiteral("gltf"))
        return shapeOfGltf(path);
    if (suffix == QStringLiteral("fbx")) return shapeOfFbx(path);
    if (suffix == QStringLiteral("dae")) return shapeOfCollada(path);
    return Shape::Unknown;
}

bool isAnimationFile(const QString &path)
{
    switch (shapeOf(path)) {
    case Shape::NotAModel:
    case Shape::Geometry:
        return false;
    case Shape::AnimationOnly:
        return true;
    case Shape::Unknown:
        break;
    }
    // Structure unreadable (an ASCII FBX, a truncated header, a format we do
    // not take apart): the parse is the honest answer. Rare by construction —
    // every format the product ships classifies structurally above.
    return read(path).isAnimationOnly();
}

Contents read(const QString &path, QImage *poseStripOut, int stripWidth, int stripHeight)
{
    Contents out;
    Assimp::Importer importer;
    // ClipNamesOnly, not the canonical preset: every step of that preset is
    // geometry work this file has no geometry for, while the file's UNIT
    // factor still has to apply — a clip's translation keys are in the file's
    // units (the FBX unit-scale fix, avatarpreviewmodel.cpp).
    const aiScene *scene = importer.ReadFile(path.toStdString().c_str(),
                                             iris::ImportFlags::ClipNamesOnly);
    if (!scene) {
        out.error = QString::fromUtf8(importer.GetErrorString());
        if (out.error.isEmpty()) out.error = QStringLiteral("the file could not be read");
        return out;
    }
    out.parsed = true;
    out.meshes = int(scene->mNumMeshes);
    out.animations = int(scene->mNumAnimations);

    const QString baseName = QFileInfo(path).completeBaseName();
    QSet<QString> boneSeen;
    for (unsigned i = 0; i < scene->mNumAnimations; ++i) {
        const aiAnimation *anim = scene->mAnimations[i];
        if (!anim) continue;
        // ticksPerSecond is 0 in more exports than not — the same fallback
        // iris::Mesh::extractAnimations uses.
        const double tps = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 25.0;

        ClipInfo clip;
        clip.rawName = QString::fromUtf8(anim->mName.C_Str());
        clip.name = rig::displayNameFor(clip.rawName, baseName);
        clip.length = anim->mDuration / tps;
        clip.channels = int(anim->mNumChannels);
        for (unsigned c = 0; c < anim->mNumChannels; ++c) {
            const QString channel = QString::fromUtf8(anim->mChannels[c]->mNodeName.C_Str());
            if (rig::isPivotChannel(channel)) continue;
            ++clip.boneChannels;
            boneSeen.insert(channel);
        }
        out.clips.append(clip);
        out.duration = std::max(out.duration, clip.length);
    }

    // THE RIG SIGNATURE OF A CLIP: the bone names its channels DRIVE, hashed
    // by the same function the model side hashes its bone names with
    // (rigsignature.h). Equal rigId means "this clip fits that rig" without
    // opening either file — which is the whole point of the clip advertising
    // one (AVATAR_ASSET_SPEC §5.1).
    out.boneChannelNames = QStringList(boneSeen.constBegin(), boneSeen.constEnd());
    out.boneChannelNames.sort();
    out.rigId = rig::rigId(out.boneChannelNames);

    if (poseStripOut) *poseStripOut = drawPoseStrip(scene, stripWidth, stripHeight);
    return out;
}

}   // namespace animfile
