#ifndef POSTPROCESS_H
#define POSTPROCESS_H

#include "../irisglfwd.h"

class QOpenGLShader;

namespace iris
{

class PostProcessPass
{
    Texture2DPtr outTexture;
    QOpenGLShader* shader;
public:
    void Process()
    {

    }
};

class PostProcess
{
public:
    void Process()
    {

    }

};

}

#endif // POSTPROCESS_H
