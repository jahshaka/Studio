#ifndef RENDERDATA_H
#define RENDERDATA_H

#include <QSharedPointer>

namespace jah3d
{
class Scene;
class Material;

struct RenderData
{
    QSharedPointer<Scene> scene;
    QSharedPointer<Material> material;

    QMatrix4x4 viewMatrix;
    QMatrix4x4 projMatrix;

    QVector3D eyePos;

    //fog properties
    QColor fogColor;
    float fogStart;
    float fogEnd;
    bool fogEnabled;
};

}

#endif // RENDERDATA_H
