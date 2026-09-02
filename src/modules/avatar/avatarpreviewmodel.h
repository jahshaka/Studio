/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef AVATARPREVIEWMODEL_H
#define AVATARPREVIEWMODEL_H

// AvatarPreviewModel — the Avatar module's subject, with NO engine in it
// (AVATAR_MODULE_SPEC §0.9).
//
// It owns a small iris document (key light, fill light, camera, and the rigged
// fragment loaded straight off disk), the clip list and its display names, the
// transport state, the two visibility toggles, and the bone segments the
// overlay draws. This is what the `avatar` verbs call and what the headless
// suite drives — every verb except `avatar.snapshot` is Needs::Document
// because everything interesting lives here.
//
// Load route (§0.6 D0.1 A): AssetHelper::extractTexturesAndMaterialFromMesh
// with a per-session scratch extract dir. Nothing is written to the library,
// the database or the project — this is a viewer, not an importer. An empty
// extract dir would write embedded textures BESIDE the source file (into the
// owner's Downloads folder); the scratch dir is not optional.
//
// Bone poses come from the SCENE-NODE hierarchy, never from Bone::transformMatrix
// (§0.5.1: that field is written only by the dead Model::applyAnimation path)
// and never from Bone::parentBone (§0.1: empty for pivot-preserving FBX rigs).
// A bone's parent is the NEAREST ANCESTOR that is also a bone.

#include <QMap>
#include <QString>
#include <QTemporaryDir>
#include <QVector>
#include <QVector3D>
#include <memory>

#include "irisgl/irisglfwd.h"

namespace avatar
{

/// One clip of the loaded file. `name` is the display name (§0.8: every Mixamo
/// clip is literally called "mixamo.com"); `rawName` is what the file said.
struct ClipInfo
{
    QString name;
    QString rawName;
    float   length = 0.0f;
    bool    looping = true;
    bool    active = false;
};

/// A bone as the preview sees it: a scene node whose name is in the skeleton's
/// boneMap. `parent` is the nearest ancestor that is also a bone ("" for a root).
struct BoneInfo
{
    QString   name;
    QString   parent;
    QVector3D position;      ///< world-space, from getGlobalTransform()
};

/// A drawable bone→parent segment, in world space.
struct BoneSegment
{
    QString   fromName;      ///< the parent bone
    QString   toName;        ///< the child bone
    QVector3D from;
    QVector3D to;
};

class AvatarPreviewModel
{
public:
    AvatarPreviewModel();
    ~AvatarPreviewModel();

    // ---- load / clear -----------------------------------------------------
    /// Loads `path` (any model extension assimp reads) as the one preview
    /// subject, replacing whatever was loaded. False + `error` on failure.
    bool load(const QString &path, QString *error = nullptr);
    void clear();
    bool isLoaded() const { return !mFragment.isNull(); }

    QString filePath() const { return mFilePath; }
    /// The file's base name — also the fallback display name for junk clips.
    QString name() const { return mName; }
    /// Where embedded textures were extracted (per-session scratch).
    QString extractDir() const;

    // ---- the document -----------------------------------------------------
    iris::ScenePtr      document() const { return mDocument; }
    iris::SceneNodePtr  fragment() const { return mFragment; }
    iris::CameraNodePtr camera() const { return mCamera; }

    // ---- what the details panel shows -------------------------------------
    int boneCount() const { return mBoneCount; }
    int meshCount() const { return mMeshCount; }
    int vertexCount() const { return mVertexCount; }
    /// Fixed 4 for a skinned rig (MAX_BONE_INDICES, mesh.cpp), 0 when unskinned.
    int influencesPerVertex() const { return mBoneCount > 0 ? 4 : 0; }
    bool hasSkeleton() const { return mBoneCount > 0; }

    // ---- clips ------------------------------------------------------------
    QVector<ClipInfo> clips() const;
    /// The active clip's DISPLAY name, or "" when nothing is loaded.
    QString activeClip() const;
    /// Selects by display name or raw name; false when there is no such clip.
    bool setClip(const QString &name);
    float duration() const;
    bool looping() const;
    void setLooping(bool on);

    // ---- transport (the preview's own clock; never the editor scene's) ----
    void play();
    void pause();
    void stop();                 ///< pause + time 0
    bool isPlaying() const { return mPlaying; }
    float time() const { return mTime; }
    void setTime(float seconds);
    /// Advances by `dt` when playing and re-evaluates the pose if anything
    /// changed. Cheap and idempotent when paused and clean.
    void advance(float dt);
    /// Evaluates the document at the current time right now.
    void evaluate();

    // ---- the two independent toggles (§0.7) -------------------------------
    bool meshVisible() const { return mMeshVisible; }
    void setMeshVisible(bool on);
    bool skeletonVisible() const { return mSkeletonVisible; }
    void setSkeletonVisible(bool on) { mSkeletonVisible = on; }

    // ---- the rig ----------------------------------------------------------
    /// Every bone that has a scene node, in tree order.
    QVector<BoneInfo> bones() const;
    /// One segment per bone that has a bone ancestor: count == bones − roots.
    QVector<BoneSegment> boneSegments() const;

    /// The display name a clip gets: its own, unless it is empty or one of the
    /// exporter's junk names ("mixamo.com", "Take 001", …), in which case the
    /// source file's base name. Public because the docs and the suite pin it.
    static QString displayNameFor(const QString &rawName, const QString &sourceBaseName);

private:
    void buildDocument();
    void collectRig();
    void applyMeshVisibility();

    iris::ScenePtr      mDocument;
    iris::CameraNodePtr mCamera;
    iris::SceneNodePtr  mFragment;

    QString mFilePath;
    QString mName;
    std::unique_ptr<QTemporaryDir> mScratch;

    // Clip display names, in the order the node carries them.
    struct Clip { QString display; QString raw; iris::AnimationPtr anim; };
    QVector<Clip> mClips;
    int mActiveClip = -1;

    // The rig, resolved once at load: bone node name -> nearest bone ancestor.
    struct BoneNode { iris::SceneNode *node = nullptr; QString name; QString parent; };
    QVector<BoneNode> mBoneNodes;

    int mBoneCount = 0;
    int mMeshCount = 0;
    int mVertexCount = 0;

    float mTime = 0.0f;
    bool  mPlaying = false;
    bool  mDirty = true;
    bool  mMeshVisible = true;
    bool  mSkeletonVisible = false;
};

} // namespace avatar

#endif // AVATARPREVIEWMODEL_H
