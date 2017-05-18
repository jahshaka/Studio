#ifndef SKELETON_H
#define SKELETON_H

#include "../irisglfwd.h"
#include <Qt>
#include <QMatrix4x4>

namespace iris
{

class Bone
{
    Bone(){}
public:
    QString name;
    QMatrix4x4 inversePoseMatrix;
    QMatrix4x4 transformMatrix;

    static BonePtr create(QString name = "")
    {
        auto bone = new Bone();
        bone->name = name;
        bone->inversePoseMatrix.setToIdentity();
        bone->transformMatrix.setToIdentity();
        return BonePtr(bone);
    }
};

class Skeleton
{
    Skeleton(){}
public:
    QMap<QString, int> boneMap;
    QList<BonePtr> bones;

    void addBone(BonePtr bone)
    {
        bones.append(bone);
        boneMap.insert(bone->name, bones.size()-1);
    }

    static SkeletonPtr create()
    {
        return SkeletonPtr(new Skeleton());
    }
};

}
#endif // SKELETON_H
