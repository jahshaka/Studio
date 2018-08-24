#ifndef SHADOWMAP_H
#define SHADOWMAP_H

#include <qopengl.h>
#include "../irisglfwd.h"
#include <QMatrix4x4>

namespace iris
{

enum class ShadowMapType : int
{
    None = 0,
    Hard = 1,
    Soft = 2,
    VerySoft = 3
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
