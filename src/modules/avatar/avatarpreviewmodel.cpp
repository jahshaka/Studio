/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/vec.h"
#include "modules/avatar/avatarpreviewmodel.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>



#include "irisgl/core/geometry/aabb.h"
#include "irisgl/core/logger.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/import/graphicshelper.h"
#include "irisgl/document/assets/skeleton.h"
#include "modules/avatar/avatarsockets.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/assethelper.h"
#include "services/rigsignature.h"

namespace avatar
{

namespace {


/// Vertical extent of the mesh AABBs under `n`, in WORLD space. Byte for byte
/// the walk AvatarMovement::fitCapsuleToNode uses for its capsule height
/// (avatarmovement.cpp mergeExtent) — the two numbers have to agree, because
/// the page normalizes the subject and the capsule measures what came out.
void mergeHeight(iris::SceneNode *n, float &minY, float &maxY, bool &any)
{
    if (!n) return;
    if (n->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto *meshNode = static_cast<iris::MeshNode *>(n);
        if (auto mesh = meshNode->getMesh()) {
            const iris::Mat4 toWorld = n->getGlobalTransform();
            const iris::AABB local = mesh->getAABB();
            const iris::Vec3 mn = local.getMin();
            const iris::Vec3 mx = local.getMax();
            for (int c = 0; c < 8; ++c) {
                const iris::Vec3 w = toWorld * iris::Vec3((c & 1) ? mx.x() : mn.x(),
                                                          (c & 2) ? mx.y() : mn.y(),
                                                          (c & 4) ? mx.z() : mn.z());
                minY = std::min(minY, w.y());
                maxY = std::max(maxY, w.y());
                any = true;
            }
        }
    }
    const int kids = n->childCount();
    for (int i = 0; i < kids; ++i) mergeHeight(n->childAt(i), minY, maxY, any);
}

} // namespace

float measureCharacterHeight(const iris::SceneNodePtr &node)
{
    if (!node) return 0.0f;
    float minY = std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    bool any = false;
    mergeHeight(node.data(), minY, maxY, any);
    if (!any) return 0.0f;
    const float height = maxY - minY;
    return height > 0.0f ? height : 0.0f;
}

HeightNormalization normalizeCharacterHeight(const iris::SceneNodePtr &node, float targetHeight)
{
    HeightNormalization out;
    if (!node) return out;

    out.sourceHeight = measureCharacterHeight(node);
    out.height = out.sourceHeight;
    out.explicitTarget = targetHeight > 0.0f;
    // No geometry (an animation-only or skeleton-only file) — there is nothing
    // to measure, and guessing from bone positions would normalize a rig whose
    // owner never sees it. Leave it exactly as authored.
    if (!(out.sourceHeight > 0.0f)) return out;

    float factor = 1.0f;
    if (out.explicitTarget) {
        factor = targetHeight / out.sourceHeight;
    } else if (out.sourceHeight < kMinPlausibleHeight || out.sourceHeight > kMaxPlausibleHeight) {
        factor = kTargetCharacterHeight / out.sourceHeight;
    }
    // A factor that rounds to 1 is not worth a scale node or a log line.
    if (std::fabs(factor - 1.0f) < 1e-4f) return out;

    // MULTIPLY: a file may carry its own root scale (that is one of the shapes
    // a mis-declared package arrives in), and it is part of how tall the thing
    // measured, so it must survive.
    const iris::Vec3 scale = node->getLocalScale();
    node->setLocalScale(iris::Vec3(scale.x() * factor, scale.y() * factor, scale.z() * factor));
    out.applied = true;
    out.factor = factor;
    out.height = measureCharacterHeight(node);

    irisLog(QStringLiteral("avatar: '%1' imported %2 m tall — %3 to %4 m (x%5). "
                           "A character outside %6-%7 m is a unit declaration the file got "
                           "wrong; re-export or set an explicit height to override.")
                .arg(node->getName())
                .arg(double(out.sourceHeight), 0, 'f', 3)
                .arg(out.explicitTarget ? QStringLiteral("set") : QStringLiteral("normalized"))
                .arg(double(out.height), 0, 'f', 3)
                .arg(double(out.factor), 0, 'f', 4)
                .arg(double(kMinPlausibleHeight), 0, 'f', 1)
                .arg(double(kMaxPlausibleHeight), 0, 'f', 1));
    return out;
}

QString AvatarPreviewModel::displayNameFor(const QString &rawName, const QString &sourceBaseName)
{
    // ONE implementation (rigsignature.h): the module's Load Animation… route
    // and the scene's avatar.loadClip must name the same clip the same way.
    return rig::displayNameFor(rawName, sourceBaseName);
}

AvatarPreviewModel::AvatarPreviewModel()
{
    buildDocument();
}

AvatarPreviewModel::~AvatarPreviewModel() = default;

void AvatarPreviewModel::buildDocument()
{
    // The lighting rig of the other previews (a warm key, a cool fill), no
    // shadows, no floor and — R0.5 — never any GI: HlmsPbs's VCT/PCC binding
    // is process-wide, so a preview scene that enabled it would take the
    // binding from the editor scene.
    mDocument = iris::Scene::create();
    mDocument->shadowEnabled = false;

    // ONE ceiling AREA panel instead of the old key/fill directionals (owner,
    // 2026-09-05): directionals lit whichever wall they faced and starved the
    // opposite one; a broad rectangle under the ceiling lights the room evenly
    // from all sides, exactly like the light-panel roof the Modern room draws.
    // Area lights emit down -Y unrotated, cast no shadows (this scene has
    // none), and the ambient below carries what the panel cannot reach.
    mPanelLight = iris::LightNode::create();
    mPanelLight->setLightType(iris::LightType::Area);
    mPanelLight->setName("avatar-panel");
    mPanelLight->name = "avatar-panel";
    mPanelLight->color = QColor(255, 250, 244);
    mPanelLight->intensity = 1.25f;
    mPanelLight->rectWidth = 7.5f;
    mPanelLight->rectHeight = 7.5f;
    mPanelLight->distance = 30.0f;             // falloff range: generous for a 4m room
    mPanelLight->setLocalPos(iris::Vec3(0, 3.8f, 0));
    mPanelLight->isBuiltIn = true;
    mDocument->rootNode->addChild(mPanelLight);

    mCamera = iris::CameraNode::create();
    // Framed for the ROOM as much as the (not yet loaded) subject: high enough
    // to read the floor grid, far enough to see the far wall's rhythm. Loading
    // a character reframes anyway (AvatarPreview::framePreview).
    mCamera->setLocalPos(iris::Vec3(0, 2.1f, 6.4f));
    mCamera->lookAt(iris::Vec3(0, 1.1f, 0));
    mDocument->setCamera(mCamera);

    mDocument->setSkyColor(QColor(28, 30, 36));
    mDocument->setAmbientColor(QColor(70, 70, 78));
    mDocument->fogEnabled = false;

    mCamera->update(0);
    mDocument->refresh();
}

bool AvatarPreviewModel::setSpaceMode(avatar::SpaceMode mode)
{
    // Idempotent — but a Modern request with no room built yet (a prior
    // failure, or first call) always tries the build.
    if (mode == mSpaceMode && (mode == avatar::SpaceMode::Grid || mSpaceRoot))
        return true;

    if (mSpaceRoot) {
        mDocument->rootNode->removeChild(mSpaceRoot);
        mSpaceRoot.reset();
    }
    mSpaceMode = mode;
    if (mode == avatar::SpaceMode::Modern) {
        mSpaceRoot = space::buildModernRoom(mDocument);
        if (!mSpaceRoot) {                     // headless: no qrc plane mesh
            mSpaceMode = avatar::SpaceMode::Grid;
            return false;
        }
        rescaleSpace();                        // a subject may already be loaded
    }
    mDocument->refresh();
    return true;
}

bool AvatarPreviewModel::setCharacterHeight(float metres, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error) *error = why;
        return false;
    };
    if (!mFragment) return fail(QStringLiteral("load a character first"));
    // A hard sanity range on the REQUEST, not on the file: the AUTO rule's
    // plausible band is about human bodies, but an explicit height is the
    // author saying "this one is a mouse / a kaiju", and only a value that
    // could not be a length at all is refused.
    if (metres > 0.0f && (metres < 0.001f || metres > 1000.0f))
        return fail(QStringLiteral("a character height of %1 m is not a length "
                                   "(0.001 .. 1000)").arg(double(metres)));

    // Everything is decided against the FILE's height, never against the size
    // an earlier call left behind. That is what makes "go back to automatic"
    // mean something: re-running the rule on the CURRENT subject would be a
    // no-op for every override inside the plausible band — i.e. for every sane
    // override — and the caller would be told it worked while nothing moved
    // (found by avatar.document H5, 2026-09-09).
    const float source = mNormalization.sourceHeight > 0.0f
                             ? mNormalization.sourceHeight
                             : measureCharacterHeight(mFragment);
    if (!(source > 0.0f))
        return fail(QStringLiteral("'%1' has no geometry to measure").arg(mName));

    const bool explicitTarget = metres > 0.0f;
    const float target = explicitTarget
                             ? metres
                             : ((source < kMinPlausibleHeight || source > kMaxPlausibleHeight)
                                    ? kTargetCharacterHeight
                                    : source);

    const HeightNormalization step = normalizeCharacterHeight(mFragment, target);
    if (!(step.sourceHeight > 0.0f))
        return fail(QStringLiteral("'%1' has no geometry to measure").arg(mName));

    // The record stays anchored to the FILE: `sourceHeight` is what the file
    // imported as and `factor` is the total from there to now, so a second
    // call reports the whole story rather than the last step of it.
    mNormalization.sourceHeight = source;
    mNormalization.height = step.height;
    mNormalization.factor = step.height / source;
    mNormalization.explicitTarget = explicitTarget;
    mNormalization.applied = std::fabs(mNormalization.factor - 1.0f) > 1e-4f;

    rescaleSpace();
    mDirty = true;
    evaluate();
    return true;
}

void AvatarPreviewModel::rescaleSpace()
{
    // The room is designed around a HUMAN-scale subject: 1 m tiles, a 4 m
    // interior, a ~1.75 m character (avatarspace.cpp). Scale it to whatever the
    // subject turned out to be, so a deliberately small or large character
    // still stands in a room instead of in a knee-high box or a stadium.
    //
    // THE NUMBERS CHANGED (2026-09-08). Before the FBX unit fix a Mixamo rig
    // arrived ~170 units tall and this factor ran to ~100; the clamp was 400,
    // and the owner's Dreyar (1604 units, wanting 943) was the ONE subject that
    // hit it — which is why his head, and only his, went through the ceiling.
    // Now the file's units are honoured at import and the subject is
    // height-normalized before this runs, so the factor is ~1 for every
    // character and the bound is no longer a scale policy: it is a guard
    // against a degenerate measurement.
    //
    // THE BOUND, honestly: unclamped, the ceiling is 4 * (height / 1.75) =
    // 2.29 * height, so it clears any head by construction. 0.05..20 puts a
    // 9 cm figurine and a 35 m mech in a proportionate room; ABOVE 35 m the
    // room stops growing, and a subject taller than 80 m would finally touch
    // the ceiling again. Normalization keeps every automatic subject inside
    // 0.5..3 m, so only an explicit setCharacterHeight or `normalize: false`
    // can reach that, and at that size the room is scenery, not a room.
    //
    // The MEASURE is the subject's geometry, the same one normalization and
    // the movement capsule use; the rest-pose bone extent is the fallback for
    // a rig with no mesh at all (which normalization deliberately leaves
    // alone), so those still get a room.
    float height = measureCharacterHeight(mFragment);
    if (!(height > 0.05f)) {
        float top = 0.0f, bottom = 0.0f;
        for (const auto &b : mBoneNodes) {
            if (!b.node) continue;
            const float y = b.node->getGlobalTransform().column(3).y();
            top = qMax(top, y);
            bottom = qMin(bottom, y);
        }
        height = top - bottom;
    }
    const float s = height > 0.05f
                        ? qBound(0.05f, height / kTargetCharacterHeight, 20.0f)
                        : 1.0f;
    // Computed even in Grid mode and with no room built (a headless suite has
    // no qrc plane mesh): it is the number the "does the head clear the
    // ceiling" gate reads, and a gate that could only run with a room would
    // not have caught this defect either.
    mRoomScale = s;
    if (!mSpaceRoot) return;
    mSpaceRoot->setLocalScale(iris::Vec3(s, s, s));
    // The ceiling panel light is NOT in the group (its rectangle is a light
    // property, not a transform, so a group scale cannot size it): follow the
    // room explicitly. Intensity is scale-free — a bigger panel at the same
    // radiance lights the bigger room the same way.
    if (mPanelLight) {
        mPanelLight->setLocalPos(iris::Vec3(0, 3.8f * s, 0));
        mPanelLight->rectWidth = 7.5f * s;
        mPanelLight->rectHeight = 7.5f * s;
        // Range must scale too — at a Mixamo rig's ~94x the character stands
        // hundreds of units from the panel; an unscaled range lights nothing
        // (the silhouette capture, 2026-09-05).
        mPanelLight->distance = 30.0f * s;
    }
}

QString AvatarPreviewModel::extractDir() const
{
    return mScratch ? mScratch->path() : QString();
}

bool AvatarPreviewModel::load(const QString &path, QString *error,
                              const QString &displayName)
{
    const auto fail = [error](const QString &why) {
        if (error) *error = why;
        return false;
    };

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return fail(QStringLiteral("no such file: %1").arg(path));

    // One subject at a time, deliberately (R0.4): pose state lives on the
    // shared iris::Mesh asset, so two instances of one rig would share a pose.
    clear();

    // R0.12: an empty extract dir writes embedded textures BESIDE the source —
    // into the owner's Downloads folder. Per-session scratch, cleaned on clear().
    mScratch.reset(new QTemporaryDir(QDir::tempPath() + "/jahshaka-avatar-XXXXXX"));
    if (!mScratch->isValid())
        return fail(QStringLiteral("could not create a scratch directory for embedded textures"));

    QStringList textureList, texturesFullPath;
    bool hasEmbedded = false;
    auto node = AssetHelper::extractTexturesAndMaterialFromMesh(
        path, textureList, texturesFullPath, hasEmbedded, nullptr, mScratch->path());
    if (!node)
        return fail(QStringLiteral("could not read %1 (unsupported or corrupt model)").arg(info.fileName()));

    mFilePath = info.absoluteFilePath();
    mName = displayName.isEmpty() ? info.completeBaseName() : displayName;
    // The fragment root keeps the name the file gave it: it may itself be a
    // bone (or a clip channel target), and renaming it would silently unhook
    // the pose lookup, which is name-matched end to end.
    mDocument->rootNode->addChild(node);
    mFragment = node;

    // AUTO height normalization, BEFORE the rig is collected and before the
    // room is scaled, so every number the page and the verbs read afterwards
    // (bone positions, segments, the room scale) is already in the subject's
    // final scale. A plausible character is untouched and this costs one AABB
    // walk; an implausible one is scaled here and nowhere else.
    mNormalization = normalizeCharacterHeight(node);

    collectRig();
    captureRestPose();
    rescaleSpace();

    // Clip list, in the order the fragment carries them, with display names
    // uniquified the way Mesh::extractAnimations uniquifies raw ones.
    QSet<QString> used;
    for (const auto &anim : node->getAnimations()) {
        if (!anim) continue;
        Clip clip;
        clip.raw = anim->getName();
        clip.source = mFilePath;
        clip.skel = anim->getSkeletalAnimation();
        // Root motion is a policy of the PREVIEW, not of the file: the clip
        // the document plays is built from the authored one either way.
        clip.anim = clip.skel ? buildClipAnimation(clip.skel) : anim;
        const QString base = displayNameFor(clip.raw, mName);
        QString unique = base;
        for (int suffix = 2; used.contains(unique); ++suffix)
            unique = base + QStringLiteral(" %1").arg(suffix);
        used.insert(unique);
        clip.display = unique;
        mClips.append(clip);
    }

    // R0.13: loadAsSceneFragment leaves whichever clip QMap::keys() yielded
    // last as the active one (alphabetical, so "Walk" beats "Idle"). Pick the
    // FILE-order first clip instead, explicitly.
    mActiveClip = mClips.isEmpty() ? -1 : 0;
    if (mActiveClip >= 0) mFragment->setAnimation(mClips[0].anim);


    mTime = 0.0f;
    mPlaying = false;
    mDirty = true;
    applyMeshVisibility();
    evaluate();
    return true;
}

void AvatarPreviewModel::clear()
{
    if (mFragment) {
        mFragment->removeFromParent();
        mFragment.reset();
    }
    mClips.clear();
    mBoneNodes.clear();
    mNodeNames.clear();
    mActiveClip = -1;
    mBoneCount = mMeshCount = mVertexCount = 0;
    mFilePath.clear();
    mName.clear();
    mNormalization = HeightNormalization();
    mTime = 0.0f;
    mPlaying = false;
    mDirty = true;
    mScratch.reset();       // QTemporaryDir removes the extracted textures
    rescaleSpace();
}

void AvatarPreviewModel::collectRig()
{
    mBoneNodes.clear();
    mBoneCount = mMeshCount = mVertexCount = 0;
    if (!mFragment) return;

    // Every bone name any mesh under the fragment knows about. A rig may be
    // split over several skinned meshes sharing one bone hierarchy.
    QSet<QString> boneNames;
    std::function<void(const iris::SceneNodePtr &)> scanMeshes =
        [&](const iris::SceneNodePtr &node) {
            if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
                auto meshNode = node.staticCast<iris::MeshNode>();
                if (auto mesh = meshNode->getMesh()) {
                    ++mMeshCount;
                    mVertexCount += mesh->numVerts;
                    if (mesh->hasSkeleton())
                        for (const auto &name : mesh->getSkeleton()->boneMap.keys())
                            boneNames.insert(name);
                }
            }
            for (const auto &child : node->children()) scanMeshes(child);
        };
    scanMeshes(mFragment);
    mBoneCount = boneNames.size();

    // The bone tree, read off the SCENE NODES (§0.5.1): a node is a bone when
    // its name is in the bone set, and its parent is the NEAREST ANCESTOR that
    // is also a bone — not the immediate parent (assimp's `$AssimpFbx$` pivot
    // nodes sit between real bones) and not Bone::parentBone (empty for a
    // pivot-preserving FBX rig).
    std::function<void(const iris::SceneNodePtr &, const QString &)> walk =
        [&](const iris::SceneNodePtr &node, const QString &boneAncestor) {
            QString nextAncestor = boneAncestor;
            if (boneNames.contains(node->name)) {
                BoneNode bone;
                bone.node = node.data();
                bone.name = node->name;
                bone.parent = boneAncestor;
                mBoneNodes.append(bone);
                nextAncestor = node->name;
            }
            for (const auto &child : node->children()) walk(child, nextAncestor);
        };
    walk(mFragment, QString());
}

void AvatarPreviewModel::captureRestPose()
{
    // WHAT THIS IS NOW: the set of scene-node NAMES, which loadAnimation scores
    // a foreign clip's channels against.
    //
    // WHAT IT USED TO BE: also a snapshot of every node's local transform, which
    // applyRestPose wrote back before every clip switch — because the document's
    // clip evaluator only wrote the nodes a clip had channels for, so a bone the
    // OLD clip moved and the NEW one does not mention kept the old pose forever.
    // That evaluator is retired (ANIMATION_ENGINE_MIGRATION_SPEC): the document
    // does not pose bones at all, and an engine skeleton resets every bone to
    // its bind pose before a clip accumulates, so the whole hack is unnecessary
    // by construction. The rest transforms themselves live on the scene nodes
    // now (SceneNode::applyDefaultPose captures them, ClipExtractor reads them).
    mNodeNames.clear();
    if (!mFragment) return;
    std::function<void(const iris::SceneNodePtr &)> walk =
        [&](const iris::SceneNodePtr &node) {
            mNodeNames.insert(node->name);
            for (const auto &child : node->children()) walk(child);
        };
    walk(mFragment);
}

QString AvatarPreviewModel::rootMotionChannel(const iris::SkeletalAnimationPtr &skel) const
{
    if (!skel || !mFragment) return QString();
    // Root-most FIRST: a breadth-first walk of the fragment, taking the first
    // node that the clip both animates and actually translates (more than one
    // position key). For a Mixamo rig that is `mixamorig:Hips`, whose position
    // channel carries the whole locomotion of the clip.
    QVector<iris::SceneNode *> queue;
    queue.append(mFragment.data());
    for (int i = 0; i < queue.size(); ++i) {
        auto *node = queue[i];
        const auto it = skel->boneAnimations.constFind(node->name);
        if (it != skel->boneAnimations.constEnd() && !it.value().isNull() &&
            it.value()->posKeys->keys.size() > 1)
            return node->name;
        for (const auto &child : node->children()) queue.append(child.data());
    }
    return QString();
}

iris::AnimationPtr AvatarPreviewModel::buildClipAnimation(const iris::SkeletalAnimationPtr &skel) const
{
    // A ZERO-LENGTH clip must not loop: Animation::getSampleTime is
    // `fmod(time, length)`, so a looping clip of length 0 samples at NaN and
    // the pose it produces is undefined. Mixamo ships exactly such a clip in
    // every CHARACTER download — a single-frame "mixamo.com" T-pose — and it
    // is the clip the page selects by default.
    const auto finish = [](iris::AnimationPtr anim) {
        if (anim && !(anim->getLength() > 0.0f)) anim->setLooping(false);
        return anim;
    };
    if (!skel) return iris::AnimationPtr();
    if (mRootMotion) return finish(iris::Animation::createFromSkeletalAnimation(skel));

    const QString rootChannel = rootMotionChannel(skel);
    if (rootChannel.isEmpty()) return finish(iris::Animation::createFromSkeletalAnimation(skel));

    // In-place playback: the root channel keeps its authored vertical motion
    // (a jump still leaves the ground) and its rotation, but its HORIZONTAL
    // translation is pinned to the first key. The other channels are shared,
    // not copied — only one BoneAnimation is ever rebuilt.
    auto source = skel->boneAnimations.value(rootChannel);
    auto stripped = new iris::BoneAnimation();
    for (const auto *key : source->rotKeys->keys) stripped->rotKeys->addKey(key->value, key->time);
    for (const auto *key : source->scaleKeys->keys) stripped->scaleKeys->addKey(key->value, key->time);
    const iris::Vec3 anchor = source->posKeys->keys.isEmpty()
                                 ? iris::Vec3()
                                 : source->posKeys->keys.first()->value;
    for (const auto *key : source->posKeys->keys)
        stripped->posKeys->addKey(iris::Vec3(anchor.x(), key->value.y(), anchor.z()), key->time);

    auto inPlace = iris::SkeletalAnimation::create();
    inPlace->name = skel->name;
    inPlace->source = skel->source;
    inPlace->boneAnimations = skel->boneAnimations;
    inPlace->boneAnimations[rootChannel] = QSharedPointer<iris::BoneAnimation>(stripped);
    return finish(iris::Animation::createFromSkeletalAnimation(inPlace));
}

void AvatarPreviewModel::rebuildClipAnimations()
{
    for (auto &clip : mClips) {
        if (!clip.skel) continue;
        const bool looping = clip.anim ? clip.anim->getLooping() : true;
        clip.anim = buildClipAnimation(clip.skel);
        if (clip.anim) clip.anim->setLooping(looping);
    }
    if (mFragment && mActiveClip >= 0 && mActiveClip < mClips.size())
        mFragment->setAnimation(mClips[mActiveClip].anim);
    mDirty = true;
    evaluate();
}

void AvatarPreviewModel::setRootMotion(bool on)
{
    if (mRootMotion == on) return;
    mRootMotion = on;
    rebuildClipAnimations();
}

bool AvatarPreviewModel::loadAnimation(const QString &path, QString *error, ClipLoadReport *report,
                                       const QString &displayName)
{
    const auto fail = [error](const QString &why) {
        if (error) *error = why;
        return false;
    };

    if (!isLoaded())
        return fail(QStringLiteral("load a character first — an animation needs a rig to play on"));

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return fail(QStringLiteral("no such file: %1").arg(path));

    // NOT AssetHelper/loadAsSceneFragment: those need a mesh and would build a
    // second character. An animation file is parsed (iris::ClipFileInfo) and read
    // for clips only — which is also the only way an ANIMATION-ONLY export
    // (zero meshes) can be read at all, since every mesh loader rejects those.
    //
    // No GEOMETRY post-processing: every step in the canonical preset is
    // geometry work, and node and channel NAMES — most of what this path
    // cares about — come out identical either way (measured on Ely +
    // Walking(1).fbx: same 207 node names, same 52 channel names, 3x faster).
    //
    // ClipNamesOnly, not 0: a clip's translation keys are in the FILE's units,
    // and the CHARACTER was parsed with the canonical preset, which honours
    // them. Reading the clip without the unit factor would drive a metre-scale
    // rig with centimetre-scale offsets — a rig that flies apart on the first
    // frame (the FBX unit-scale fix, importflags.h).
    QString readError;
    const auto anims =
        iris::GraphicsHelper::loadAnimationsFromClipFile(info.absoluteFilePath(), &readError);
    if (!readError.isEmpty())
        return fail(QStringLiteral("could not read %1 (%2)").arg(info.fileName(), readError));
    if (anims.isEmpty())
        return fail(QStringLiteral("%1 contains no animation").arg(info.fileName()));

    // The clip -> bone join is by SCENE-NODE NAME (SceneNode::updateAnimation
    // matches `anim->boneAnimations.contains(node->name)`), so a clip from
    // another rig loads, plays, and moves absolutely nothing. Score every clip
    // against the loaded rig's node names and refuse the file when none lands
    // — through the ONE matcher (rigsignature.h), so the module and the scene
    // verbs give the same file the same answer, word for word.
    const QVector<rig::ClipScore> scored = rig::scoreClips(anims, mNodeNames);
    const int best = rig::bestClip(scored);
    if (best < 0) return fail(QStringLiteral("%1 contains no animation").arg(info.fileName()));

    auto toReport = [](const rig::ClipScore &s) {
        ClipLoadReport r;
        r.channels = s.channels;
        r.boneChannels = s.boneChannels;
        r.matched = s.matched;
        r.unmatched = s.unmatched;
        return r;
    };

    if (scored[best].ratio < rig::kRigMatchThreshold) {
        if (report) *report = toReport(scored[best]);
        return fail(rig::mismatchMessage(scored[best], info.fileName(), mName));
    }

    QSet<QString> used;
    for (const auto &clip : mClips) used.insert(clip.display);

    const QString sourceBase = displayName.isEmpty() ? info.completeBaseName() : displayName;
    ClipLoadReport out = toReport(scored[best]);
    for (const auto &s : scored) {
        if (s.ratio < rig::kRigMatchThreshold) continue;   // a foreign clip in a mixed file
        Clip clip;
        clip.raw = s.raw;
        clip.source = info.absoluteFilePath();
        clip.external = true;
        clip.skel = s.skel;
        clip.anim = buildClipAnimation(s.skel);
        if (!clip.anim) continue;
        // Every Mixamo clip is called "mixamo.com", so the ANIMATION file's
        // base name is the display name — "Walking(1)", not a third
        // "mixamo.com" row under the character's own.
        const QString base = displayNameFor(clip.raw, sourceBase);
        QString unique = base;
        for (int suffix = 2; used.contains(unique); ++suffix)
            unique = base + QStringLiteral(" %1").arg(suffix);
        used.insert(unique);
        clip.display = unique;
        mFragment->addAnimation(clip.anim);
        mClips.append(clip);
        ++out.added;
        if (out.firstClip.isEmpty()) out.firstClip = clip.display;
    }
    if (out.added == 0) return fail(QStringLiteral("%1 contains no usable clip").arg(info.fileName()));

    // Accumulate: loading an animation never changes what is playing. The
    // caller (a double-click in the ANIMATIONS list, or avatar.setClip)
    // decides when to switch.
    if (report) *report = out;
    return true;
}

void AvatarPreviewModel::applyMeshVisibility()
{
    if (!mFragment) return;
    // D0.5 A: the document flag, which the mirror pushes every sync. Only
    // MeshNodes are touched — setNodeVisible hides a node AND its subtree, and
    // bone nodes can hang under a mesh node in some rigs.
    std::function<void(const iris::SceneNodePtr &)> apply =
        [&](const iris::SceneNodePtr &node) {
            if (node->getSceneNodeType() == iris::SceneNodeType::Mesh)
                node->setVisible(mMeshVisible);
            for (const auto &child : node->children()) apply(child);
        };
    apply(mFragment);
}

void AvatarPreviewModel::setMeshVisible(bool on)
{
    mMeshVisible = on;
    applyMeshVisibility();
}

QVector<ClipInfo> AvatarPreviewModel::clips() const
{
    QVector<ClipInfo> out;
    out.reserve(mClips.size());
    for (int i = 0; i < mClips.size(); ++i) {
        ClipInfo info;
        info.name = mClips[i].display;
        info.rawName = mClips[i].raw;
        info.length = mClips[i].anim ? mClips[i].anim->getLength() : 0.0f;
        info.looping = mClips[i].anim ? mClips[i].anim->getLooping() : false;
        info.active = (i == mActiveClip);
        info.source = mClips[i].source;
        info.external = mClips[i].external;
        out.append(info);
    }
    return out;
}

QString AvatarPreviewModel::activeClip() const
{
    return mActiveClip >= 0 && mActiveClip < mClips.size() ? mClips[mActiveClip].display : QString();
}

bool AvatarPreviewModel::setClip(const QString &name)
{
    for (int i = 0; i < mClips.size(); ++i) {
        if (mClips[i].display != name && mClips[i].raw != name) continue;
        mActiveClip = i;
        // No rest-pose restore any more: an engine skeleton resets every bone to
        // its BIND pose before a clip accumulates, so a bone the new clip does
        // not mention cannot inherit the old clip's last value.
        if (mFragment) mFragment->setAnimation(mClips[i].anim);
        // The transport state is deliberately untouched: switching while
        // playing keeps playing, from the start of the new clip.
        mTime = 0.0f;
        mDirty = true;
        evaluate();
        return true;
    }
    return false;
}

float AvatarPreviewModel::duration() const
{
    if (mActiveClip < 0 || mActiveClip >= mClips.size() || !mClips[mActiveClip].anim) return 0.0f;
    return mClips[mActiveClip].anim->getLength();
}

bool AvatarPreviewModel::looping() const
{
    if (mActiveClip < 0 || mActiveClip >= mClips.size() || !mClips[mActiveClip].anim) return false;
    return mClips[mActiveClip].anim->getLooping();
}

void AvatarPreviewModel::setLooping(bool on)
{
    if (mActiveClip < 0 || mActiveClip >= mClips.size() || !mClips[mActiveClip].anim) return;
    mClips[mActiveClip].anim->setLooping(on);
    mDirty = true;
}

void AvatarPreviewModel::play()  { mPlaying = true; }
void AvatarPreviewModel::pause() { mPlaying = false; }

void AvatarPreviewModel::stop()
{
    mPlaying = false;
    setTime(0.0f);
}

void AvatarPreviewModel::setTime(float seconds)
{
    const float clamped = seconds < 0.0f ? 0.0f : seconds;
    if (qFuzzyCompare(clamped + 1.0f, mTime + 1.0f) && !mDirty) return;
    mTime = clamped;
    mDirty = true;
    evaluate();
}

void AvatarPreviewModel::advance(float dt)
{
    if (mPlaying && dt > 0.0f) {
        mTime += dt;
        const float len = duration();
        // Non-looping clips park on the last frame; looping ones wrap (the
        // document's own getSampleTime does the wrap, but the reported time
        // has to wrap too or the scrubber runs off the end).
        if (len > 0.0f) {
            if (looping()) mTime = std::fmod(mTime, len);
            else if (mTime >= len) { mTime = len; mPlaying = false; }
        }
        mDirty = true;
    }
    if (mDirty) evaluate();
}

void AvatarPreviewModel::evaluate()
{
    if (!mDocument) return;
    mDocument->updateSceneAnimation(mTime);
    mDirty = false;
}

QHash<QString, iris::Mat4> AvatarPreviewModel::boneWorldMatrices() const
{
    // The engine is where a pose lives now. The bone scene nodes still describe
    // the rig's SHAPE (names, parents, and the rest transform the file
    // authored), but nothing writes a clip's pose into them any more, so a
    // source that reads the engine back is the only thing that makes these
    // positions move.
    QHash<QString, iris::Mat4> world;
    if (mPoseSource && mPoseSource(world) && !world.isEmpty()) return world;
    world.clear();
    for (const auto &bone : mBoneNodes)
        if (bone.node) world.insert(bone.name, bone.node->getGlobalTransform());
    return world;
}

QVector<BoneInfo> AvatarPreviewModel::bones() const
{
    const QHash<QString, iris::Mat4> world = boneWorldMatrices();
    QVector<BoneInfo> out;
    out.reserve(mBoneNodes.size());
    for (const auto &bone : mBoneNodes) {
        if (!bone.node) continue;
        const auto it = world.constFind(bone.name);
        if (it == world.constEnd()) continue;
        BoneInfo info;
        info.name = bone.name;
        info.parent = bone.parent;
        info.position = it->column(3).toVector3D();
        out.append(info);
    }
    return out;
}

QVector<iris::Vec3> AvatarPreviewModel::rigPoints() const
{
    QVector<iris::Vec3> out;
    if (!mFragment) return out;

    // EVERY SKINNED PIECE, not the first one — the same lesson
    // avatar::sockets::installBuiltInsInSubtree records: a Mixamo export is
    // several meshes bound to SUBSETS of one skeleton, and the first piece
    // depth-first is the limbs (46 bones, no Head, no Shoulder). A
    // first-piece-only read finds no attachment points on a perfectly good
    // humanoid.
    QVector<iris::SkeletonPtr> skeletons;
    std::function<void(const iris::SceneNodePtr &)> find =
        [&](const iris::SceneNodePtr &node) {
            if (!node) return;
            if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
                auto meshNode = node.staticCast<iris::MeshNode>();
                if (auto mesh = meshNode->getMesh())
                    if (mesh->hasSkeleton()) skeletons.append(mesh->getSkeleton());
            }
            for (const auto &child : node->children()) find(child);
        };
    find(mFragment);
    if (skeletons.isEmpty()) return out;

    const QHash<QString, iris::Mat4> world = boneWorldMatrices();
    QSet<QString> placed;
    for (const QString &socket : sockets::builtInNames()) {
        for (const iris::SkeletonPtr &skeleton : skeletons) {
            const QString bone = sockets::mapBone(skeleton, socket);
            if (bone.isEmpty()) continue;          // fail soft, like the install
            const auto it = world.constFind(bone);
            if (it == world.constEnd()) continue;
            if (placed.contains(bone)) break;      // one marker per joint
            placed.insert(bone);
            out.append(it->column(3).toVector3D());
            break;                                  // the first piece that knows it answers
        }
    }
    return out;
}

QVector<BoneSegment> AvatarPreviewModel::boneSegments() const
{
    const QHash<QString, iris::Mat4> world = boneWorldMatrices();
    QHash<QString, iris::Vec3> positions;
    QHash<QString, iris::Vec3> axes;          // each bone's own local +Y, in world space
    QSet<QString> hasChild;
    for (const auto &bone : mBoneNodes) {
        if (!bone.node) continue;
        const auto it = world.constFind(bone.name);
        if (it == world.constEnd()) continue;
        const iris::Mat4 global = *it;
        positions.insert(bone.name, global.column(3).toVector3D());
        iris::Vec3 axis = global.column(1).toVector3D();      // scale is carried here too
        if (axis.lengthSquared() > 1e-12f) axis.normalize();
        axes.insert(bone.name, axis);
        if (!bone.parent.isEmpty()) hasChild.insert(bone.parent);
    }

    QVector<BoneSegment> out;
    for (const auto &bone : mBoneNodes) {
        if (!bone.node || bone.parent.isEmpty()) continue;   // roots draw nothing
        const auto parentPos = positions.constFind(bone.parent);
        if (parentPos == positions.constEnd()) continue;
        BoneSegment seg;
        seg.fromName = bone.parent;
        seg.toName = bone.name;
        seg.from = parentPos.value();
        seg.to = positions.value(bone.name);
        seg.toAxis = axes.value(bone.name);
        seg.toIsLeaf = !hasChild.contains(bone.name);
        out.append(seg);
    }
    return out;
}

} // namespace avatar
