#include "../irisglfwd.h"
#include "skeleton.h"
#include "../animation/skeletalanimation.h"
#include <functional>

namespace iris
{

BonePtr Skeleton::getBone(QString name)
{
    if (boneMap.contains(name))
        return bones[boneMap[name]];
    return BonePtr();// null
}

void Skeleton::applyAnimation(iris::SkeletalAnimationPtr anim, float time)
{
    for (auto i = 0; i < bones.size(); i++) {
        bones[i]->transformMatrix.setToIdentity();
        bones[i]->localMatrix.setToIdentity();
        bones[i]->skinMatrix.setToIdentity();
    }

    //qDebug() << "== Animation Begin ==";
    for (auto boneName : anim->boneAnimations.keys()) {
        auto boneAnim = anim->boneAnimations[boneName];
        auto bone = bones[boneMap[boneName]];
        //qDebug()<< boneName;
        if ( boneMap.contains(boneName))
        {
            //qDebug()<< boneName <<" verified";
            auto pos = boneAnim->posKeys->getValueAt(time);
            auto rot = boneAnim->rotKeys->getValueAt(time);
            auto scale = boneAnim->scaleKeys->getValueAt(time);


            bone->localMatrix.setToIdentity();
            bone->localMatrix.translate(pos);
            bone->localMatrix.rotate(rot);
            bone->localMatrix.scale(scale);
        }
        else
        {
            bone->localMatrix.setToIdentity();
            //bone->localMatrix = bone->poseMatrix;
            //qDebug()<< boneName <<" unverified";
        }
    }
    //qDebug() << "== Animation End ==";

    //recursively calculate final transform
    std::function<void(BonePtr)> calcFinalTransform;
    calcFinalTransform = [&calcFinalTransform](BonePtr bone)
    {
        for (auto child : bone->childBones) {
            child->transformMatrix = bone->transformMatrix * child->localMatrix;
            child->skinMatrix = child->transformMatrix * child->inversePoseMatrix;
            //qDebug() << child->transformMatrix;
            calcFinalTransform(child);
        }
    };

    //auto rootBone = bones[0];//its assumed that the first bone is the root bone
    //auto rootBone = this->getRootBone();
    auto roots = this->getRootBones();
    for (auto rootBone : roots) {
        rootBone->transformMatrix = rootBone->localMatrix;
        //rootBone->transformMatrix.setToIdentity();
        rootBone->skinMatrix = rootBone->transformMatrix * rootBone->inversePoseMatrix;
        calcFinalTransform(rootBone);
    }

    //assign transforms to list
    for (auto i = 0; i < bones.size(); i++) {
        boneTransforms[i] = bones[i]->skinMatrix;
    }
}

// https://github.com/acgessler/open3mod/blob/master/open3mod/SceneAnimator.cs#L338
void Skeleton::applyAnimation(QMatrix4x4 inverseMeshMatrix, QMap<QString, QMatrix4x4> skeletonSpaceMatrices)
{
    for (auto i = 0; i < bones.size(); i++) {
        auto bone = bones[i];
        if (skeletonSpaceMatrices.contains(bone->name)) {
            //bones->transformMatrix.setToIdentity();
            //bones->localMatrix.setToIdentity();

            // https://github.com/acgessler/open3mod/blob/master/open3mod/SceneAnimator.cs#L356
            bone->skinMatrix = inverseMeshMatrix * skeletonSpaceMatrices[bone->name] * bone->inversePoseMatrix;
        }
        else {
            bone->skinMatrix.setToIdentity();
        }
        boneTransforms[i] = bone->skinMatrix;
    }
}

}
