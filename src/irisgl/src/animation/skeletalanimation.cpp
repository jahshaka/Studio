#include "skeletalanimation.h"
#include "SkeletonMeshBuilder.h"
#include "../animation/keyframeanimation.h"

namespace iris {

void SkeletalAnimation::addBoneAnimation(QString boneName, iris::BoneAnimation *boneAnim)
{
    boneAnimations.insert(boneName, QSharedPointer<BoneAnimation>(boneAnim));
}

float BoneAnimation::getLength()
{
    float maxLength = 0;
    maxLength = posKeys->getLength();
    maxLength = qMax(maxLength, rotKeys->getLength());
    return qMax(maxLength, scaleKeys->getLength());
}

}
