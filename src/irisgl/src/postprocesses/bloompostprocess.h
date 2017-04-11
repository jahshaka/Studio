#ifndef BLOOMPOSTPROCESS_H
#define BLOOMPOSTPROCESS_H

#include "../graphics/postprocess.h"
#include <QVector3D>

class QOpenGLShaderProgram;

namespace iris
{

class PostProcessContext;

class BloomPostProcess : public PostProcess
{
public:
    QOpenGLShaderProgram* thresholdShader;
    QOpenGLShaderProgram* blurShader;
    QOpenGLShaderProgram* combineShader;

    Texture2DPtr threshold;
    Texture2DPtr hBlur;
    Texture2DPtr vBlur;

    BloomPostProcess();

    virtual void process(PostProcessContext* context) override;

};

}

#endif // BLOOMPOSTPROCESS_H
