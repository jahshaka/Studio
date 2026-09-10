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

#include "data/constants.h"
#include "irisgl/import/clipfileinfo.h"
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
//
// The poses are the importer's own evaluation (iris::ClipFileInfo samples the
// first clip at the requested fractions, in FILE space — x right, y up, the
// projection every DCC front view uses); what happens here is the projection
// and the drawing, and nothing else.

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

/// Joint positions of one pose, as parent→child segments in the file's
/// front-view plane.
QVector<Segment> posePlanar(const iris::ClipFileInfo &info, const iris::ClipFileInfo::Pose &pose,
                            Bounds *boundsInOut)
{
    QVector<Segment> segments;
    const int count = std::min(pose.positions.size(), info.nodeParents.size());
    for (int i = 0; i < count; ++i) {
        const int parent = info.nodeParents[i];
        if (parent < 0) continue;                        // the root has nothing to hang from
        const QPointF here(pose.positions[i].x(), pose.positions[i].y());
        const QPointF from(pose.positions[parent].x(), pose.positions[parent].y());
        segments.append({ from, here });
        boundsInOut->add(here);
        boundsInOut->add(from);
    }
    return segments;
}

/// The fractions of the first clip the strip samples: three poses — enough
/// to read a walk from a stride, and enough to tell two clip files apart at
/// tile size.
const QVector<double> kStripFractions = { 0.1, 0.5, 0.9 };

QImage drawPoseStrip(const iris::ClipFileInfo &info, int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(24, 24, 28));
    if (info.poses.size() != kStripFractions.size()) return image;   // nothing to sample

    Bounds bounds;
    QVector<Segment> poses[3];
    for (int i = 0; i < 3; ++i) poses[i] = posePlanar(info, info.poses[i], &bounds);
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
    // ClipNamesOnly, not the canonical preset: every step of that preset is
    // geometry work this file has no geometry for, while the file's UNIT
    // factor still has to apply — a clip's translation keys are in the file's
    // units (the FBX unit-scale fix, avatarpreviewmodel.cpp). The parse is
    // IrisGL's (assimp is its private dependency); the poses for the strip
    // come out of the same parse.
    const iris::ClipFileInfo info =
        iris::ClipFileInfo::read(path, poseStripOut ? kStripFractions : QVector<double>());
    if (!info.parsed) {
        out.error = info.error;
        return out;
    }
    out.parsed = true;
    out.meshes = info.meshes;
    out.animations = info.animations;

    const QString baseName = QFileInfo(path).completeBaseName();
    QSet<QString> boneSeen;
    for (const auto &source : info.clips) {
        ClipInfo clip;
        clip.rawName = source.name;
        clip.name = rig::displayNameFor(clip.rawName, baseName);
        clip.length = source.lengthSeconds;
        clip.channels = source.channelNames.size();
        for (const QString &channel : source.channelNames) {
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

    if (poseStripOut) *poseStripOut = drawPoseStrip(info, stripWidth, stripHeight);
    return out;
}

}   // namespace animfile
