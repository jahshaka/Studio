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

#include <QHash>
#include <QMap>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>
#include <QVector3D>
#include <functional>
#include <memory>

#include "irisgl/irisglfwd.h"

namespace avatar
{

/// One clip the preview knows about. `name` is the display name (§0.8: every
/// Mixamo clip is literally called "mixamo.com"); `rawName` is what the file
/// said. Clips accumulate: the ones the character file carried, plus every one
/// `loadAnimation` has added from a separate file since (`external`).
struct ClipInfo
{
    QString name;
    QString rawName;
    float   length = 0.0f;
    bool    looping = true;
    bool    active = false;
    QString source;             ///< the file this clip was read from
    bool    external = false;   ///< came from a separate animation file
};

/// What `loadAnimation` found in an animation file: how well its channels
/// matched the loaded rig, and what it added.
struct ClipLoadReport
{
    int added = 0;              ///< clips appended to the list
    int channels = 0;           ///< animation channels in the best clip
    int boneChannels = 0;       ///< ... of which are NOT assimp pivot channels
    int matched = 0;            ///< ... of which name a node of the loaded rig
    QStringList unmatched;      ///< the first few bone channels with no node
    QString firstClip;          ///< display name of the first clip added
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
    /// World-space bone axis of the CHILD bone — its own local +Y, which is
    /// the axis rig bones run along (measured on the synthetic fixture and on a
    /// Mixamo character: 64 of 66 bones point at their child along local +Y).
    /// The overlay draws a leaf bone's stub along this, not along `to - from`.
    QVector3D toAxis;
    /// The child bone has no bone children of its own — the end of a chain.
    bool      toIsLeaf = false;
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
    /// Reads `path` for CLIPS ONLY and appends them to the clip list of the
    /// already-loaded character (the Mixamo workflow: one character file, then
    /// one file per animation). Accepts both shapes an exporter produces — a
    /// with-skin animation file (its mesh is ignored) and an animation-only
    /// file (zero meshes, which every mesh loader in the tree rejects). The
    /// join is by SCENE-NODE NAME, so a clip from a different rig is REFUSED
    /// (false + `error` naming the first unmatched bones) instead of silently
    /// loading a clip that moves nothing.
    bool loadAnimation(const QString &path, QString *error = nullptr,
                       ClipLoadReport *report = nullptr);
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
    /// Rewinds to 0 and keeps the transport state (switching while playing
    /// keeps playing, from the start of the new clip). Every node is put back
    /// on its REST pose first, so channels the new clip does not carry cannot
    /// inherit the previous clip's last pose.
    bool setClip(const QString &name);
    float duration() const;
    bool looping() const;
    void setLooping(bool on);

    /// Root motion. Off (the default) plays locomotion clips IN PLACE: the
    /// horizontal translation of the clip's root-most animated node is pinned
    /// to its first key, so a walk cycle walks on the spot instead of leaving
    /// the frame. On plays the clip exactly as authored. Vertical motion is
    /// never stripped (a jump still leaves the ground).
    bool rootMotion() const { return mRootMotion; }
    void setRootMotion(bool on);

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
    /// Where a POSE comes from.
    ///
    /// The document stopped computing one (ANIMATION_ENGINE_MIGRATION_SPEC:
    /// clip evaluation is Ogre's now), so a bone's world matrix has to be read
    /// back from the engine. AvatarPreviewScene installs a source that does
    /// exactly that; with none installed the bone list still has the right
    /// SHAPE — names, parents, hierarchy — but its positions are the rig's REST
    /// pose, because there is no engine to have posed it.
    using PoseSource = std::function<bool(QHash<QString, QMatrix4x4> &)>;
    void setPoseSource(PoseSource source) { mPoseSource = std::move(source); }
    bool hasPoseSource() const { return bool(mPoseSource); }

    /// Every bone's WORLD matrix by name — from the pose source when one is
    /// installed, from the rig's rest transforms otherwise.
    QHash<QString, QMatrix4x4> boneWorldMatrices() const;
    /// Every bone that has a scene node, in tree order.
    QVector<BoneInfo> bones() const;
    /// One segment per bone that has a bone ancestor: count == bones − roots.
    QVector<BoneSegment> boneSegments() const;

    // ---- the session list the page's left column shows --------------------
    /// Every character file loaded in this session, oldest first. Module-local
    /// and deliberately not persisted: Part 1's library rows replace it.
    QStringList history() const { return mHistory; }
    /// Drops `path` from the session list (the left column's right-click
    /// Delete). Clears the preview when the dropped file is the loaded one.
    bool forget(const QString &path);

    /// The display name a clip gets: its own, unless it is empty or one of the
    /// exporter's junk names ("mixamo.com", "Take 001", …), in which case the
    /// source file's base name. Public because the docs and the suite pin it.
    static QString displayNameFor(const QString &rawName, const QString &sourceBaseName);

private:
    void buildDocument();
    void collectRig();
    void applyMeshVisibility();
    /// Snapshots every node's local transform right after a load, and puts
    /// them back before a clip switch.
    void captureRestPose();
    /// Builds the AnimationPtr the document plays for a clip, applying the
    /// root-motion policy. Called again for every clip when it is toggled.
    iris::AnimationPtr buildClipAnimation(const iris::SkeletalAnimationPtr &skel) const;
    void rebuildClipAnimations();
    /// The name of the clip's root-most animated node that actually translates
    /// — the one root motion lives on. Empty when the clip has none.
    QString rootMotionChannel(const iris::SkeletalAnimationPtr &skel) const;

    iris::ScenePtr      mDocument;
    iris::CameraNodePtr mCamera;
    iris::SceneNodePtr  mFragment;

    QString mFilePath;
    QString mName;
    std::unique_ptr<QTemporaryDir> mScratch;

    // Clip display names, in the order they were added: the character file's
    // own first, then each loadAnimation's. `skel` is the clip AS AUTHORED —
    // `anim` is what the document plays, rebuilt when root motion is toggled.
    struct Clip
    {
        QString display;
        QString raw;
        QString source;
        bool external = false;
        iris::SkeletalAnimationPtr skel;
        iris::AnimationPtr anim;
    };
    QVector<Clip> mClips;
    int mActiveClip = -1;

    // The rig, resolved once at load: bone node name -> nearest bone ancestor.
    struct BoneNode { iris::SceneNode *node = nullptr; QString name; QString parent; };
    QVector<BoneNode> mBoneNodes;
    PoseSource mPoseSource;

    // Every scene-node name under the fragment — the set a foreign clip's
    // channels are matched against (the clip -> bone join is by NAME).
    QSet<QString> mNodeNames;

    // The loaded file's own pose, so a clip switch starts from rest.

    QStringList mHistory;
    bool mRootMotion = false;

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
