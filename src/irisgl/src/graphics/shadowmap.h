#ifndef SHADOWMAP_H
#define SHADOWMAP_H

#include <qopengl.h>
#include "../irisglfwd.h"
#include <QMatrix4x4>

namespace iris
{

enum class ShadowMapType
{
    None,
    Hard,
    Soft
};

class ShadowMap
{
public:
    ShadowMapType shadowType;
    Texture2DPtr shadowTexture;
    //GLuint shadowTexId;
    QMatrix4x4 shadowMatrix;
    int resolution;
    float bias;

    ShadowMap();

    void setResolution(int size);
};


}

#endif // SHADOWMAP_H
