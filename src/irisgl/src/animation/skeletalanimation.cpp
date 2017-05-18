#include "skeletalanimation.h"
#include "SkeletonMeshBuilder.h"

namespace iris {

SkeletalAnimation::SkeletalAnimation()
{

}

void SkeletalAnimation::addBoneAnimation(QString boneName, iris::BoneAnimation *boneAnim)
{
    boneAnimations.insert(boneName, QScopedPointer<BoneAnimation>(boneAnim));
}

}
