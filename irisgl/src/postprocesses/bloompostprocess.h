#ifndef BLOOMPOSTPROCESS_H
#define BLOOMPOSTPROCESS_H

#include "../graphics/postprocess.h"
#include <QVector3D>

class QOpenGLShaderProgram;

namespace iris
{

class BloomPostProcess;
typedef QSharedPointer<BloomPostProcess> BloomPostProcessPtr;
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
    Texture2DPtr final;

    float bloomThreshold;
    float bloomStrength;
    float dirtStrength;
    Texture2DPtr dirtyLens;

    BloomPostProcess();

    virtual void process(PostProcessContext* context) override;

    virtual QList<Property*> getProperties() override;
    virtual void setProperty(Property* prop) override;

    static BloomPostProcessPtr create();

};

}

#endif // BLOOMPOSTPROCESS_H
