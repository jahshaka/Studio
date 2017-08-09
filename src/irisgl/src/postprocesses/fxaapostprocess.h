#ifndef FXAAPOSTPROCESS_H
#define FXAAPOSTPROCESS_H

#include "../graphics/postprocess.h"
#include <QVector3D>

class QOpenGLShaderProgram;

namespace iris
{

class FxaaPostProcess;
typedef QSharedPointer<FxaaPostProcess> FxaaPostProcessPtr;

// applies and tonemapping and antialiasing
class FxaaPostProcess : public PostProcess
{
public:
    Texture2DPtr tonemapTex;
    Texture2DPtr fxaaTex;

    QOpenGLShaderProgram* tonemapShader;
    QOpenGLShaderProgram* fxaaShader;

    FxaaPostProcess();

    virtual void process(PostProcessContext* ctx) override;

    static FxaaPostProcessPtr create();
};

}


#endif // FXAAPOSTPROCESS_H
