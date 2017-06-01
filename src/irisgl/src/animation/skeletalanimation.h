#ifndef SKELETALANIMATION_H
#define SKELETALANIMATION_H

#include "../irisglfwd.h"
#include "../animation/keyframeanimation.h"

namespace iris {

// keyframe animation for a single animation for a single bone
class BoneAnimation
{
public:
    QScopedPointer<Vector3DKeyFrame>    posKeys;
    QScopedPointer<QuaternionKeyFrame>  rotKeys;
    QScopedPointer<Vector3DKeyFrame>    scaleKeys;

    BoneAnimation()
        : posKeys(new Vector3DKeyFrame())
        , rotKeys(new QuaternionKeyFrame())
        , scaleKeys(new Vector3DKeyFrame())
    {

    }
};

class SkeletalAnimation
{
    SkeletalAnimation(){}
public:
    // both read-only
    QString name;
    QString source;

    QMap<QString, QSharedPointer<BoneAnimation>> boneAnimations;

    // takes ownership of boneAnim
    void addBoneAnimation(QString boneName,BoneAnimation* boneAnim);

    static SkeletalAnimationPtr create()
    {
        return SkeletalAnimationPtr(new SkeletalAnimation());
    }
};

}

#endif // SKELETALANIMATION_H
