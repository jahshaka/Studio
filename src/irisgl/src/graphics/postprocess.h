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
    void process()
    {

    }
};

class PostProcessContext;

class PostProcess
{
public:
    bool enabled;

    virtual void process(PostProcessContext* ctx)
    {

    }

};

}

#endif // POSTPROCESS_H
