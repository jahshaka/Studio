#ifndef RADIALBLURPOSTPROCESS_H
#define RADIALBLURPOSTPROCESS_H

#include "../graphics/postprocess.h"
#include <QVector3D>

class QOpenGLShaderProgram;

namespace iris
{

class RadialBlurPostProcess;
typedef QSharedPointer<RadialBlurPostProcess> RadialBlurPostProcessPtr;

class RadialBlurPostProcess : public PostProcess
{
    QOpenGLShaderProgram* shader;

    float blurSize;
    Texture2DPtr final;
public:
    RadialBlurPostProcess();

    virtual void process(PostProcessContext* ctx) override;

    virtual QList<Property*> getProperties() override;
    virtual void setProperty(Property* prop) override;

    static RadialBlurPostProcessPtr create();
};

}


#endif // RADIALBLURPOSTPROCESS_H
