#ifndef POSTPROCESS_H
#define POSTPROCESS_H

#include "../irisglfwd.h"

class QOpenGLShader;

namespace iris
{

class PostProcessPass
{
    Texture2DPtr output;
    QOpenGLShader* shader;
    QString name;

public:
    void Process()
    {

    }
};

class PostProcess
{
public:
    virtual void process()
    {

    }

};

}

#endif // POSTPROCESS_H
