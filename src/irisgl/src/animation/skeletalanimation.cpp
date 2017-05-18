#include "skeletalanimation.h"
#include "SkeletonMeshBuilder.h"

namespace iris {

void SkeletalAnimation::addBoneAnimation(QString boneName, iris::BoneAnimation *boneAnim)
{
    boneAnimations.insert(boneName, QSharedPointer<BoneAnimation>(boneAnim));
}

}
