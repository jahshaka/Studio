/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ANIMATIONFILE_H
#define ANIMATIONFILE_H

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

/// ANIMATION FILES — the reader behind `ModelTypes::Animation`
/// (the library type for a clip file: what Mixamo calls a download
/// "without skin").
///
/// The shape of the problem: an animation-only export is a MODEL file
/// (.fbx/.dae/.glb/…) that carries a node hierarchy and animation channels and
/// NO meshes. Every mesh path in the product refuses it —
/// `iris::MeshNode::loadAsSceneFragment` returns null on `mNumMeshes == 0`
/// (meshnode.cpp) and nine callers rely on that, so the loader must not be
/// relaxed. Such a file gets its own importer instead, and this file is the
/// part that can look at one without the document model.
///
/// Two entry points, deliberately separate:
///
///   shapeOf()  — STRUCTURE ONLY, no parse: does this file carry geometry?
///                It is what the import pipeline's sniff asks, once per
///                import, about every model file the user drops. Answering it
///                with a parse would double the cost of every mesh import, so
///                each format is read structurally: glTF/GLB by its JSON
///                header, binary FBX by walking the `Objects` record headers,
///                COLLADA by a streaming XML scan. Formats that cannot carry
///                animation at all (obj/ply/stl) answer immediately, and
///                anything not understood answers Unknown — the caller pays
///                the parse only then.
///
///   read()     — the one full parse (iris::ClipFileInfo, IrisGL's clip
///                reader: no geometry post-processing, which is all the
///                canonical preset does, but keeping the file's unit factor
///                because a clip's translation keys are in the file's
///                units). Produces the clip table, the rig signature the row
///                advertises, and optionally the POSE STRIP thumbnail.
///
/// Pure file inspection: no database, no engine, no widgets — safe on the
/// import worker thread (QImage/QPainter are, QPixmap would not be).
namespace animfile {

/// One clip of a file.
struct ClipInfo
{
    QString rawName;        ///< the clip's name in the file ("mixamo.com", "Take 001", …)
    QString name;           ///< the display name (rig::displayNameFor: junk -> file base name)
    double length = 0.0;    ///< seconds (ticks / ticksPerSecond, 25 fps when unstated)
    int channels = 0;       ///< animation channels
    int boneChannels = 0;   ///< … of which are not assimp pivot bookkeeping
};

/// What `read()` found.
struct Contents
{
    bool parsed = false;            ///< the importer read the file at all
    QString error;                  ///< why not, when it did not
    int meshes = 0;
    int animations = 0;
    QVector<ClipInfo> clips;
    QStringList boneChannelNames;   ///< sorted, de-duplicated non-pivot channel names
    QString rigId;                  ///< rig::rigId over those names — WHICH RIG THIS CLIP FITS
    double duration = 0.0;          ///< the longest clip, seconds

    /// The library type's definition: a model file with no geometry and at
    /// least one animation.
    bool isAnimationOnly() const { return parsed && meshes == 0 && animations > 0; }
};

enum class Shape
{
    NotAModel,        ///< an extension no model importer of ours handles
    Geometry,         ///< carries meshes: an Object, not a clip (structurally certain)
    AnimationOnly,    ///< no meshes, has animation (structurally certain)
    Unknown           ///< the structure could not be read — ask read()
};

/// Structural classification, no assimp parse. See the file comment.
Shape shapeOf(const QString &path);

/// The sniff `AnimationImporter` asks: is this file a clip library?
/// Structural where that is certain, one parse where it is not.
bool isAnimationFile(const QString &path);

/// ONE parse. `poseStripOut`, when given, receives the thumbnail: three
/// projected skeleton poses sampled across the first clip — the only way to
/// tell two clip files apart at tile size.
Contents read(const QString &path, QImage *poseStripOut = nullptr,
              int stripWidth = 256, int stripHeight = 256);

}   // namespace animfile

#endif   // ANIMATIONFILE_H
