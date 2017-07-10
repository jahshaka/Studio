#ifndef SKELETON_H
#define SKELETON_H

#include "../irisglfwd.h"
#include <Qt>
#include <QMatrix4x4>

namespace iris
{

class Bone : public QEnableSharedFromThis<Bone>
{
    Bone(){}
public:
    QString name;
    QMatrix4x4 inversePoseMatrix;// skeleton space
    QMatrix4x4 poseMatrix;// skeleton space
    QMatrix4x4 transformMatrix;// skeleton space
    QMatrix4x4 localMatrix;// local space

    QMatrix4x4 skinMatrix;// final transform sent to the shader

    QList<BonePtr> childBones;
    BonePtr parentBone;

    void addChild(BonePtr bone)
    {
        bone->parentBone = this->sharedFromThis();
        childBones.append(bone);
    }

    static BonePtr create(QString name = "")
    {
        auto bone = new Bone();
        bone->name = name;
        bone->inversePoseMatrix.setToIdentity();
        bone->poseMatrix.setToIdentity();
        bone->transformMatrix.setToIdentity();
        bone->localMatrix.setToIdentity();
        bone->skinMatrix.setToIdentity();
        return BonePtr(bone);
    }
};

class Skeleton
{
    Skeleton(){}
public:
    QMap<QString, int> boneMap;
    QList<BonePtr> bones;

    BonePtr getBone(QString name);
    QVector<QMatrix4x4> boneTransforms;

    void addBone(BonePtr bone)
    {
        bones.append(bone);
        boneMap.insert(bone->name, bones.size()-1);

        QMatrix4x4 transform;
        transform.setToIdentity();
        boneTransforms.append(transform);
    }

    BonePtr getRootBone()
    {
        for(auto bone : bones)
            if(!bone->parentBone)
                return bone;
    }

    QList<BonePtr> getRootBones()
    {
        QList<BonePtr> roots;
        for(auto bone : bones)
            if(!bone->parentBone)
                roots.append(bone);

        return roots;
    }

    void applyAnimation(SkeletalAnimationPtr anim, float time);

    void applyAnimation(QMatrix4x4 inverseMeshMatrix, QMap<QString, QMatrix4x4> skeletonSpaceMatrices);

    static SkeletonPtr create()
    {
        return SkeletonPtr(new Skeleton());
    }
};



}
#endif // SKELETON_H
