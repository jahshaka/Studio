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
// The rig's SHAPE comes from the SCENE-NODE hierarchy, never from
// Bone::parentBone (§0.1: empty for pivot-preserving FBX rigs) — a bone's
// parent is the NEAREST ANCESTOR that is also a bone. Its POSE comes from the
// engine, through the pose source AvatarPreviewScene installs: clip evaluation
// moved to Ogre (ANIMATION_ENGINE_MIGRATION_SPEC) and the document does not
// compute one any more.

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/vec.h"
#include "modules/avatar/avatarspace.h"

#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>
#include <functional>
#include <memory>

#include "irisgl/irisglfwd.h"
#include "services/fitsize.h"

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
    iris::Vec3 position;      ///< world-space, from getGlobalTransform()
};

// ---- character-height normalization (the FBX unit-scale fix, 2026-09-08) ---
//
// The world is METRES (irisgl/import/importflags.h). Honouring an FBX file's
// UnitScaleFactor fixed the systematic 100x, but it cannot fix a package whose
// DECLARATION is wrong: the owner's Dreyar download says centimetres and is
// authored in millimetres, so it arrives 17.25 m tall — head through the
// ceiling of a 4 m room — with its clips authored to match. Nothing in the file
// says which of the two is wrong, so the module measures the character and,
// when the number is not a plausible human height, scales the SUBJECT (its
// root node, so the rig, the mesh and every clip that plays on it move
// together) instead of leaving the page to cope.
//
// The bounds are deliberately wide: everything from a toddler (0.5 m) to a
// three-metre ogre is left EXACTLY as authored, because a character that size
// is a legitimate art decision. Outside them the file is wrong by orders of
// magnitude, never by taste.

/// What normalization did, for the verb readback and the log line.
struct HeightNormalization
{
    bool  applied = false;      ///< the subject was scaled
    float factor = 1.0f;        ///< what its root scale was multiplied by
    float sourceHeight = 0.0f;  ///< metres, as the file imported
    float height = 0.0f;        ///< metres, after (== sourceHeight when not applied)
    bool  explicitTarget = false; ///< a height was asked for, not inferred
};

/// AUTO leaves anything in [kMinPlausibleHeight, kMaxPlausibleHeight] alone.
///
/// ONE SET OF NUMBERS (fit-to-size, 2026-09-09): these are the import-time
/// size policy's character envelope (fitsize::kCharacter), not a second
/// opinion. The import fits a mis-declared character at the ASSET, and this
/// rule then measures the fitted result and finds it plausible — so a
/// character is never scaled twice. Two independently-written bands would
/// have made "twice" possible the day one of them moved.
constexpr float kMinPlausibleHeight = float(fitsize::kCharacter.min);
constexpr float kMaxPlausibleHeight = float(fitsize::kCharacter.max);
/// What AUTO scales an implausible character TO — and the room's design height.
constexpr float kTargetCharacterHeight = float(fitsize::kCharacter.target);

/// World-space vertical extent of every mesh under `node`, in metres — the
/// same measure AvatarMovement::fitCapsuleToNode calls the capsule height, so
/// the page, the capsule and this agree by construction. 0 when the subtree
/// carries no geometry (a skeleton-only file), which is NOT normalizable.
float measureCharacterHeight(const iris::SceneNodePtr &node);

/// Scales `node` so its measured height becomes `targetHeight` metres, or —
/// with `targetHeight` <= 0, the AUTO default — so an implausible height
/// becomes kTargetCharacterHeight and a plausible one is left untouched.
/// MULTIPLIES the existing local scale (a file's own root scale is part of how
/// tall it is) and logs whenever it changes anything.
HeightNormalization normalizeCharacterHeight(const iris::SceneNodePtr &node,
                                             float targetHeight = 0.0f);

/// A drawable bone→parent segment, in world space.
struct BoneSegment
{
    QString   fromName;      ///< the parent bone
    QString   toName;        ///< the child bone
    iris::Vec3 from;
    iris::Vec3 to;
    /// World-space bone axis of the CHILD bone — its own local +Y, which is
    /// the axis rig bones run along (measured on the synthetic fixture and on a
    /// Mixamo character: 64 of 66 bones point at their child along local +Y).
    /// The overlay draws a leaf bone's stub along this, not along `to - from`.
    iris::Vec3 toAxis;
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
    /// `displayName` names the SUBJECT and every junk-named clip in it. It is
    /// not cosmetic: since the CAS, a stored object's file NAME IS ITS SHA256,
    /// so a subject loaded from the store and named after its file is called
    /// "c826b4bf…" and so is every Mixamo clip inside it (they are all
    /// literally named "mixamo.com", and the display rule falls back to the
    /// file's base name). Callers that load from the store pass the CATALOG
    /// ROW's name — the same rule `attachClipsFromFile` follows for its
    /// refusals. Empty = use the file's base name, which is right for a load
    /// straight off disk.
    bool load(const QString &path, QString *error = nullptr,
              const QString &displayName = QString());
    /// Reads `path` for CLIPS ONLY and appends them to the clip list of the
    /// already-loaded character (the Mixamo workflow: one character file, then
    /// one file per animation). Accepts both shapes an exporter produces — a
    /// with-skin animation file (its mesh is ignored) and an animation-only
    /// file (zero meshes, which every mesh loader in the tree rejects). The
    /// join is by SCENE-NODE NAME, so a clip from a different rig is REFUSED
    /// (false + `error` naming the first unmatched bones) instead of silently
    /// loading a clip that moves nothing.
    /// `displayName` names the clips this file contributes, for exactly the
    /// reason `load`'s does: a clip file resolved through the CAS is named
    /// after its sha256, and every Mixamo animation download is called
    /// "mixamo.com", so the fallback would name them all after the hash.
    bool loadAnimation(const QString &path, QString *error = nullptr,
                       ClipLoadReport *report = nullptr,
                       const QString &displayName = QString());
    void clear();
    bool isLoaded() const { return !mFragment.isNull(); }
    /// Every scene-node name under the loaded fragment — the set a clip's
    /// channels are joined against (the clip -> bone join is by NAME). Read by
    /// `avatar.animations`, which answers "does this library clip fit the
    /// loaded character" with the same key `loadAnimation` refuses on.
    const QSet<QString> &nodeNames() const { return mNodeNames; }

    /// Sets the loaded subject's height in metres, exactly (`metres` > 0), or
    /// re-runs the AUTO rule (`metres` <= 0). False when nothing is loaded or
    /// the subject has no geometry to measure.
    bool setCharacterHeight(float metres, QString *error = nullptr);
    /// What normalization did to the loaded subject (all zeros when nothing is
    /// loaded).
    const HeightNormalization &normalization() const { return mNormalization; }

    QString filePath() const { return mFilePath; }
    /// The file's base name — also the fallback display name for junk clips.
    QString name() const { return mName; }
    /// Where embedded textures were extracted (per-session scratch).
    QString extractDir() const;

    // ---- the document -----------------------------------------------------
    iris::ScenePtr      document() const { return mDocument; }
    iris::SceneNodePtr  fragment() const { return mFragment; }
    iris::CameraNodePtr camera() const { return mCamera; }

    // ---- the space (SPECS/AVATAR_SPACE_SPEC.md) ---------------------------
    /// Switches the environment: Grid (empty founding look) or Modern (the
    /// Tron room). Idempotent; returns false only when the room's geometry
    /// resources are unavailable (headless tests). The choice is NOT persisted
    /// here — the module owns the setting.
    bool setSpaceMode(avatar::SpaceMode mode);
    avatar::SpaceMode spaceMode() const { return mSpaceMode; }
    /// What the Modern room is scaled by to fit the loaded subject (1.0 with
    /// no subject). Tracked in Grid mode and headlessly too — it is the number
    /// the "head clears the ceiling" gate reads.
    float roomScale() const { return mRoomScale; }
    /// The room's ceiling in world metres at the current room scale — the
    /// ceiling slab's UNDERSIDE, i.e. what the subject's head has to stay under
    /// (the 2026-09-08 owner defect). 1 u = 1 m, so this is directly comparable
    /// with measureCharacterHeight (the subject stands on the floor at y = 0).
    float ceilingHeight() const { return avatar::space::kRoomInteriorHeight * mRoomScale; }

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
    /// THE THIRD TOGGLE (S9, owner 2026-09-11): the RIG — the attachment
    /// points the avatar module owns (the built-in `head` and `shoulder`
    /// sockets), marked at the joints they resolve to. Independent of the
    /// other two, like they are of each other.
    bool rigVisible() const { return mRigVisible; }
    void setRigVisible(bool on) { mRigVisible = on; }
    /// Where the rig markers go, in world space: one point per BUILT-IN socket
    /// this character's skeleton actually has (a rig with no recognizable head
    /// contributes nothing — avatar::sockets fails soft, and so does this).
    /// Empty with nothing loaded.
    QVector<iris::Vec3> rigPoints() const;

    // ---- the rig ----------------------------------------------------------
    /// Where a POSE comes from.
    ///
    /// The document stopped computing one (ANIMATION_ENGINE_MIGRATION_SPEC:
    /// clip evaluation is Ogre's now), so a bone's world matrix has to be read
    /// back from the engine. AvatarPreviewScene installs a source that does
    /// exactly that; with none installed the bone list still has the right
    /// SHAPE — names, parents, hierarchy — but its positions are the rig's REST
    /// pose, because there is no engine to have posed it.
    using PoseSource = std::function<bool(QHash<QString, iris::Mat4> &)>;
    void setPoseSource(PoseSource source) { mPoseSource = std::move(source); }
    bool hasPoseSource() const { return bool(mPoseSource); }

    /// Every bone's WORLD matrix by name — from the pose source when one is
    /// installed, from the rig's rest transforms otherwise.
    QHash<QString, iris::Mat4> boneWorldMatrices() const;
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
    /// Scales the Modern room to the loaded subject (Mixamo rigs are ~170
    /// units tall; the room is designed for 1.8m). Grid mode: no-op.
    void rescaleSpace();
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
    iris::SceneNodePtr  mSpaceRoot;   // the Modern room group, null in Grid mode
    iris::LightNodePtr  mPanelLight;  // the ceiling area panel — the whole rig
    avatar::SpaceMode   mSpaceMode = avatar::SpaceMode::Grid;
    float               mRoomScale = 1.0f;
    iris::CameraNodePtr mCamera;
    iris::SceneNodePtr  mFragment;

    QString mFilePath;
    QString mName;
    HeightNormalization mNormalization;
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

    bool mRootMotion = false;

    int mBoneCount = 0;
    int mMeshCount = 0;
    int mVertexCount = 0;

    float mTime = 0.0f;
    bool  mPlaying = false;
    bool  mDirty = true;
    bool  mMeshVisible = true;
    bool  mSkeletonVisible = false;
    bool  mRigVisible = false;
};

} // namespace avatar

#endif // AVATARPREVIEWMODEL_H
