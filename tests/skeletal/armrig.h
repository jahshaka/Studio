#pragma once
// The programmatic two-bone arm, shared by every skeletal suite.
//
// A flat quad strip at z=0 facing +Z: rows y=0 and y=1 weighted to bone 0
// ("jointRoot", bind at the origin), row y=2 to bone 1 ("jointTip", bind at
// y=1). Nothing is loaded from disk, so the rig is reviewable and the suites
// can be reasoned about together (tests/avatar/fixtures/rig2.glb is the same
// shape, as a real single-mesh glTF).
//
// The document half of an imported rig is TWO things: the iris::Mesh (geometry
// + the rig TEMPLATE skeleton) and the scene-node hierarchy that carries the
// pose (SceneNode::updateAnimation walks `children` by name — the bone
// hierarchy IS the scene-node hierarchy for an imported model). buildArmRig
// builds both, exactly as MeshNode::loadAsSceneFragment's _buildScene would.
#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector3D>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/materials/defaultmaterial.h"

namespace armrig {

/// Geometry + the rig template. ONE of these is shared by every MeshNode that
/// references it, which is exactly the multiple-avatars case.
inline iris::MeshPtr buildArmMesh()
{
    const float hw = 0.15f;
    const float positions[] = { -hw, 0, 0,  hw, 0, 0,  -hw, 1, 0,  hw, 1, 0,  -hw, 2, 0,  hw, 2, 0 };
    const float normals[]   = { 0, 0, 1,  0, 0, 1,  0, 0, 1,  0, 0, 1,  0, 0, 1,  0, 0, 1 };
    const float boneIdx[]   = { 0,0,0,0,  0,0,0,0,  0,0,0,0,  0,0,0,0,  1,0,0,0,  1,0,0,0 };
    const float boneW[]     = { 1,0,0,0,  1,0,0,0,  1,0,0,0,  1,0,0,0,  1,0,0,0,  1,0,0,0 };
    const unsigned indices[] = { 0, 1, 3,  0, 3, 2,  2, 3, 5,  2, 5, 4 };   // CCW from +Z

    auto mesh = iris::Mesh::create();
    auto addBuf = [&mesh](iris::VertexAttribUsage usage, const float *data, int floats, int comps) {
        iris::VertexLayout layout;
        layout.addAttrib(usage, iris::AttribTypeFloat, comps, comps * int(sizeof(float)));
        auto vb = iris::VertexBuffer::create(layout);
        vb->setData(const_cast<float *>(data), unsigned(floats * sizeof(float)));
        mesh->addVertexBuffer(vb);
    };
    addBuf(iris::VertexAttribUsage::Position,    positions, 18, 3);
    addBuf(iris::VertexAttribUsage::Normal,      normals,   18, 3);
    addBuf(iris::VertexAttribUsage::BoneIndices, boneIdx,   24, 4);
    addBuf(iris::VertexAttribUsage::BoneWeights, boneW,     24, 4);
    auto ib = iris::IndexBuffer::create();
    ib->setData(const_cast<unsigned *>(indices), sizeof(indices));
    mesh->setIndexBuffer(ib);
    mesh->setVertexCount(6);

    auto skel = iris::Skeleton::create();
    auto root = iris::Bone::create("jointRoot");            // bind: mesh origin
    auto tip = iris::Bone::create("jointTip");              // bind: y=1 in mesh space
    tip->meshSpacePoseMatrix.translate(0, 1, 0);
    tip->inverseMeshSpacePoseMatrix.translate(0, -1, 0);
    skel->addBone(root);                                    // index 0
    skel->addBone(tip);                                     // index 1
    root->addChild(tip);
    mesh->setSkeleton(skel);
    return mesh;
}

/// The clip: jointTip swings `degrees` about Z over one second.
inline iris::AnimationPtr buildSwingClip(float degrees, float length = 1.0f)
{
    auto skelAnim = iris::SkeletalAnimation::create();
    auto boneAnim = new iris::BoneAnimation();
    boneAnim->posKeys->addKey(QVector3D(0, 1, 0), 0.0);
    boneAnim->posKeys->addKey(QVector3D(0, 1, 0), length);
    boneAnim->rotKeys->addKey(QQuaternion(), 0.0);
    boneAnim->rotKeys->addKey(QQuaternion::fromAxisAndAngle(0, 0, 1, degrees), length);
    boneAnim->scaleKeys->addKey(QVector3D(1, 1, 1), 0.0);
    boneAnim->scaleKeys->addKey(QVector3D(1, 1, 1), length);
    skelAnim->addBoneAnimation("jointTip", boneAnim);
    return iris::Animation::createFromSkeletalAnimation(skelAnim);
}

/// A complete rig fragment: the MeshNode plus the two bone SceneNodes the FBX/
/// glTF path would create. `mesh` is passed in so several fragments can share
/// ONE mesh asset — the shape MeshNode::createDuplicate produces.
inline iris::MeshNodePtr buildArmNode(const iris::MeshPtr &mesh, const QString &name = "arm")
{
    auto arm = iris::MeshNode::create();
    arm->setName(name);
    arm->setMesh(mesh);
    auto mat = iris::DefaultMaterial::create();
    mat->setDiffuseColor(QColor(255, 0, 0));
    arm->setMaterial(mat);

    auto jointRoot = iris::SceneNode::create();
    jointRoot->setName("jointRoot");
    arm->addChild(jointRoot);
    auto jointTip = iris::SceneNode::create();
    jointTip->setName("jointTip");
    jointTip->setLocalPos(QVector3D(0, 1, 0));
    jointRoot->addChild(jointTip);
    return arm;
}

}  // namespace armrig
