/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef RIGSIGNATURE_H
#define RIGSIGNATURE_H

#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include "irisgl/irisglfwd.h"

/// Rig identity and the clip <-> rig match — ONE implementation
/// (AVATAR_ASSET_SPEC §5.1 / AVATAR_MODULE_SPEC R-A1).
///
/// Before this file the rule lived twice: in the module's preview
/// (`avatarpreviewmodel.cpp`, the Load Animation… route) and in the scene
/// verbs (`avatarapi.cpp`, the `avatar.loadClip` route), each with its own
/// copy of the pivot rule, its own literal 0.5 threshold and its own refusal
/// wording. Two copies of a *refusal* is the worst kind: the user is told two
/// different stories about the same file. Everything here is pure data — no
/// Qt widgets, no engine, no database — so the services, the module and the
/// tests all link it.
namespace rig {

/// assimp's FBX importer preserves the exporter's pivots as extra nodes named
/// `<bone>_$AssimpFbx$_Rotation` / `_Translation` / `_Scaling`, and MOST of a
/// Mixamo clip's channels target those, not bones: 46 of Walking(1).fbx's 52.
/// A rig-match test that counted channels would therefore be measuring pivot
/// bookkeeping, so the match ratio is computed over the BONE channels only.
bool isPivotChannel(const QString &name);

/// Clip names no format actually carries: every Mixamo download is literally
/// called "mixamo.com", every FBX SDK export "Take 001", and assimp's
/// BVHLoader hard-codes "Motion" for every .bvh file that will ever exist.
bool isJunkClipName(const QString &raw);

/// The display name of a clip: its own name when it has one, else the file's
/// base name — which is what makes "Walking.fbx" bind the walk role.
QString displayNameFor(const QString &rawName, const QString &sourceBaseName);

/// How much of a clip has to land on the loaded rig before it is worth
/// playing. A same-rig Mixamo clip scores 1.0 (verified: Ely + Walking/
/// Running/Idle all match 6 of 6 bone channels, and even their pivot channels
/// match 48 of 52); a foreign rig scores 0. Half is a wide gap to fall into.
extern const double kRigMatchThreshold;

/// One clip of a file, scored against a rig's scene-node names. The clip ->
/// bone join is by SCENE-NODE NAME, so a clip from another rig loads, plays,
/// and moves absolutely nothing — which is what the score exists to catch.
struct ClipScore
{
    QString raw;                       ///< the clip's name in the file
    iris::SkeletalAnimationPtr skel;   ///< the extracted channels
    int channels = 0;                  ///< animation channels in this clip
    int boneChannels = 0;              ///< ... of which are NOT assimp pivots
    int matched = 0;                   ///< ... of which name a node of the rig
    QStringList unmatched;             ///< the first few bone channels with no node
    double ratio = 0.0;                ///< matched / boneChannels, 0 when none
};

/// Scores every clip of an extracted file against `rigNodeNames`.
QVector<ClipScore> scoreClips(const QMap<QString, iris::SkeletalAnimationPtr> &anims,
                              const QSet<QString> &rigNodeNames);

/// Index of the best-scoring clip, or -1 for an empty list.
int bestClip(const QVector<ClipScore> &scored);

/// THE refusal sentence, shared by every caller so the same file gets the same
/// answer wherever the user asks. `shownFile` names the catalog row when there
/// is one (since the CAS a stored object's file name is its sha256).
QString mismatchMessage(const ClipScore &best, const QString &shownFile, const QString &rigName);

/// Stable identity of a rig: sha1 over the sorted, de-duplicated bone names.
/// Two exports of the same skeleton hash equal whatever their node order;
/// `AVATAR_RIG_PERF` §3.1 hashes the same thing, so skeleton sharing and the
/// avatar definition can never disagree about what "the same rig" means.
/// Empty in, empty out (an unrigged model has no rig id, not a hash of
/// nothing).
QString rigId(const QStringList &boneNames);

}   // namespace rig

#endif   // RIGSIGNATURE_H
