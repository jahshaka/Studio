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

#include "irisgl/document/animation/animation.h"
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
bool isJunkClipName(const QString &raw)
{
    static const QSet<QString> junk = {
        QStringLiteral("mixamo.com"), QStringLiteral("take 001"),
        QStringLiteral("default take"), QStringLiteral("unreal take"),
        QStringLiteral("armature|mixamo.com|layer0"), QStringLiteral("animstack::take 001"),
    };
    return raw.trimmed().isEmpty() || junk.contains(raw.trimmed().toLower());
}

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

    // Clip list, in the order the fragment carries them, with display names
    // uniquified the way Mesh::extractAnimations uniquifies raw ones.
    QSet<QString> used;
    for (const auto &anim : node->getAnimations()) {
        if (!anim) continue;
        Clip clip;
        clip.raw = anim->getName();
        clip.anim = anim;
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
        if (mFragment) mFragment->setAnimation(mClips[i].anim);
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

QVector<BoneInfo> AvatarPreviewModel::bones() const
{
    QVector<BoneInfo> out;
    out.reserve(mBoneNodes.size());
    for (const auto &bone : mBoneNodes) {
        if (!bone.node) continue;
        BoneInfo info;
        info.name = bone.name;
        info.parent = bone.parent;
        info.position = bone.node->getGlobalTransform().column(3).toVector3D();
        out.append(info);
    }
    return out;
}

QVector<BoneSegment> AvatarPreviewModel::boneSegments() const
{
    QHash<QString, QVector3D> positions;
    for (const auto &bone : mBoneNodes)
        if (bone.node) positions.insert(bone.name, bone.node->getGlobalTransform().column(3).toVector3D());

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
        out.append(seg);
    }
    return out;
}

} // namespace avatar
