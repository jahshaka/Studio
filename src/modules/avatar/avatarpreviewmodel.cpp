/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/avatar/avatarpreviewmodel.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QQuaternion>
#include <QSet>
#include <cmath>
#include <functional>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/assethelper.h"

namespace avatar
{

namespace {

// Exporter placeholders that carry no information. Mixamo names EVERY clip
// "mixamo.com"; the FBX SDK's default take names are the other three. A file
// of N such clips would otherwise show as N identical rows (§0.8).
//
// "Motion" is the BVH case and it is not a heuristic: the format has no place
// to record a clip name, so assimp's BVHLoader::CreateAnimation hard-codes
// `anim->mName.Set("Motion")` for every .bvh file that will ever exist. Left
// off this list, a mocap library loads as "Motion", "Motion 2", "Motion 3" —
// with it, each clip shows as its own file's base name, which is the only
// information the format actually carries.
bool isJunkClipName(const QString &raw)
{
    static const QSet<QString> junk = {
        QStringLiteral("mixamo.com"), QStringLiteral("take 001"),
        QStringLiteral("default take"), QStringLiteral("unreal take"),
        QStringLiteral("armature|mixamo.com|layer0"), QStringLiteral("animstack::take 001"),
        QStringLiteral("motion"),
    };
    return raw.trimmed().isEmpty() || junk.contains(raw.trimmed().toLower());
}

// assimp's FBX importer preserves the exporter's pivots as extra nodes named
// `<bone>_$AssimpFbx$_Rotation` / `_Translation` / `_Scaling`, and MOST of a
// Mixamo clip's channels target those, not bones: 46 of Walking(1).fbx's 52.
// A rig-match test that counted channels would therefore be measuring pivot
// bookkeeping, so the match ratio is computed over the BONE channels only.
bool isPivotChannel(const QString &name)
{
    return name.contains(QStringLiteral("$AssimpFbx$"));
}

// How much of a clip has to land on the loaded rig before it is worth playing.
// A same-rig Mixamo clip scores 1.0 (verified: Ely + Walking/Running/Idle all
// match 6 of 6 bone channels, and even their pivot channels match 48 of 52);
// a foreign rig scores 0. Half is a wide gap to fall into.
const double kRigMatchThreshold = 0.5;

} // namespace

QString AvatarPreviewModel::displayNameFor(const QString &rawName, const QString &sourceBaseName)
{
    if (!isJunkClipName(rawName)) return rawName;
    return sourceBaseName.isEmpty() ? QStringLiteral("Clip") : sourceBaseName;
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
    // is process-wide, so a preview scene that enabled it would steal the
    // binding from the editor scene.
    mDocument = iris::Scene::create();
    mDocument->shadowEnabled = false;

    auto key = iris::LightNode::create();
    key->setLightType(iris::LightType::Directional);
    key->setName("avatar-key");
    key->name = "avatar-key";
    key->color = QColor(255, 255, 240);
    key->setLocalRot(QQuaternion::fromEulerAngles(45, 35, 0));
    key->intensity = 1.0f;
    key->isBuiltIn = true;
    mDocument->rootNode->addChild(key);

    auto fill = iris::LightNode::create();
    fill->setLightType(iris::LightType::Directional);
    fill->setName("avatar-fill");
    fill->name = "avatar-fill";
    fill->color = QColor(200, 215, 255);
    fill->setLocalRot(QQuaternion::fromEulerAngles(20, -140, 0));
    fill->intensity = 0.45f;
    fill->isBuiltIn = true;
    mDocument->rootNode->addChild(fill);

    mCamera = iris::CameraNode::create();
    mCamera->setLocalPos(QVector3D(0, 1, 4));
    mCamera->lookAt(QVector3D(0, 1, 0));
    mDocument->setCamera(mCamera);

    mDocument->setSkyColor(QColor(28, 30, 36));
    mDocument->setAmbientColor(QColor(70, 70, 78));
    mDocument->fogEnabled = false;

    mCamera->update(0);
    mDocument->update(0);
}

QString AvatarPreviewModel::extractDir() const
{
    return mScratch ? mScratch->path() : QString();
}

bool AvatarPreviewModel::load(const QString &path, QString *error)
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
    mName = info.completeBaseName();
    // The fragment root keeps the name the file gave it: it may itself be a
    // bone (or a clip channel target), and renaming it would silently unhook
    // the pose lookup, which is name-matched end to end.
    mDocument->rootNode->addChild(node);
    mFragment = node;

    collectRig();
    captureRestPose();

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

    if (!mHistory.contains(mFilePath)) mHistory.append(mFilePath);

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
    mTime = 0.0f;
    mPlaying = false;
    mDirty = true;
    mScratch.reset();       // QTemporaryDir removes the extracted textures
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
            for (const auto &child : node->children) scanMeshes(child);
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
            for (const auto &child : node->children) walk(child, nextAncestor);
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
            for (const auto &child : node->children) walk(child);
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
        for (const auto &child : node->children) queue.append(child.data());
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
    const QVector3D anchor = source->posKeys->keys.isEmpty()
                                 ? QVector3D()
                                 : source->posKeys->keys.first()->value;
    for (const auto *key : source->posKeys->keys)
        stripped->posKeys->addKey(QVector3D(anchor.x(), key->value.y(), anchor.z()), key->time);

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

bool AvatarPreviewModel::loadAnimation(const QString &path, QString *error, ClipLoadReport *report)
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
    // second character. An animation file is parsed for its aiScene and read
    // for clips only — which is also the only way an ANIMATION-ONLY export
    // (zero meshes) can be read at all, since every mesh loader rejects those.
    //
    // No post-processing flags: every step in the canonical preset is
    // geometry work, and node and channel NAMES — the only thing this path
    // cares about — come out identical either way (measured on Ely +
    // Walking(1).fbx: same 207 node names, same 52 channel names, 3x faster).
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(path.toStdString().c_str(), 0);
    if (!scene)
        return fail(QStringLiteral("could not read %1 (%2)")
                        .arg(info.fileName(), QString::fromUtf8(importer.GetErrorString())));
    if (scene->mNumAnimations == 0)
        return fail(QStringLiteral("%1 contains no animation").arg(info.fileName()));

    const auto anims = iris::Mesh::extractAnimations(scene, info.absoluteFilePath());

    // The clip -> bone join is by SCENE-NODE NAME (SceneNode::updateAnimation
    // matches `anim->boneAnimations.contains(node->name)`), so a clip from
    // another rig loads, plays, and moves absolutely nothing. Score every clip
    // against the loaded rig's node names and refuse the file when none lands.
    struct Scored { QString raw; iris::SkeletalAnimationPtr skel; ClipLoadReport report; double ratio = 0.0; };
    QVector<Scored> scored;
    for (auto it = anims.constBegin(); it != anims.constEnd(); ++it) {
        Scored s;
        s.raw = it.key();
        s.skel = it.value();
        if (!s.skel) continue;
        for (auto ch = s.skel->boneAnimations.constBegin(); ch != s.skel->boneAnimations.constEnd(); ++ch) {
            ++s.report.channels;
            if (isPivotChannel(ch.key())) continue;
            ++s.report.boneChannels;
            if (mNodeNames.contains(ch.key())) ++s.report.matched;
            else if (s.report.unmatched.size() < 5) s.report.unmatched.append(ch.key());
        }
        s.ratio = s.report.boneChannels > 0
                      ? double(s.report.matched) / double(s.report.boneChannels)
                      : 0.0;
        scored.append(s);
    }
    if (scored.isEmpty()) return fail(QStringLiteral("%1 contains no animation").arg(info.fileName()));

    int best = 0;
    for (int i = 1; i < scored.size(); ++i)
        if (scored[i].ratio > scored[best].ratio) best = i;
    if (scored[best].ratio < kRigMatchThreshold) {
        if (report) *report = scored[best].report;
        const auto &r = scored[best].report;
        const QString names = r.unmatched.isEmpty() ? QStringLiteral("(pivot channels only)")
                                                    : r.unmatched.join(QStringLiteral(", "));
        return fail(QStringLiteral("%1 is animating a different rig — %2 of its %3 bones exist "
                                   "in '%4' (no match for %5)")
                        .arg(info.fileName())
                        .arg(r.matched)
                        .arg(r.boneChannels)
                        .arg(mName, names));
    }

    QSet<QString> used;
    for (const auto &clip : mClips) used.insert(clip.display);

    const QString sourceBase = info.completeBaseName();
    ClipLoadReport out = scored[best].report;
    for (const auto &s : scored) {
        if (s.ratio < kRigMatchThreshold) continue;     // a foreign clip in a mixed file
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

bool AvatarPreviewModel::forget(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    int removed = mHistory.removeAll(path);
    if (absolute != path) removed += mHistory.removeAll(absolute);
    if (removed == 0) return false;
    if (mFilePath == absolute || mFilePath == path) clear();
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
            for (const auto &child : node->children) apply(child);
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

QHash<QString, QMatrix4x4> AvatarPreviewModel::boneWorldMatrices() const
{
    // The engine is where a pose lives now. The bone scene nodes still describe
    // the rig's SHAPE (names, parents, and the rest transform the file
    // authored), but nothing writes a clip's pose into them any more, so a
    // source that reads the engine back is the only thing that makes these
    // positions move.
    QHash<QString, QMatrix4x4> world;
    if (mPoseSource && mPoseSource(world) && !world.isEmpty()) return world;
    world.clear();
    for (const auto &bone : mBoneNodes)
        if (bone.node) world.insert(bone.name, bone.node->getGlobalTransform());
    return world;
}

QVector<BoneInfo> AvatarPreviewModel::bones() const
{
    const QHash<QString, QMatrix4x4> world = boneWorldMatrices();
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

QVector<BoneSegment> AvatarPreviewModel::boneSegments() const
{
    const QHash<QString, QMatrix4x4> world = boneWorldMatrices();
    QHash<QString, QVector3D> positions;
    QHash<QString, QVector3D> axes;          // each bone's own local +Y, in world space
    QSet<QString> hasChild;
    for (const auto &bone : mBoneNodes) {
        if (!bone.node) continue;
        const auto it = world.constFind(bone.name);
        if (it == world.constEnd()) continue;
        const QMatrix4x4 global = *it;
        positions.insert(bone.name, global.column(3).toVector3D());
        QVector3D axis = global.column(1).toVector3D();      // scale is carried here too
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
