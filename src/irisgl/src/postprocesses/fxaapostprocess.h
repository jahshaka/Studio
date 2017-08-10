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

    // between 1 and 5
    int quality;

    FxaaPostProcess();

    QList<Property *> getProperties();
    void setProperty(Property *prop) override;

    void setQuality(int quality);
    int getQuality();

    virtual void process(PostProcessContext* ctx) override;

    static FxaaPostProcessPtr create();
};

}


#endif // FXAAPOSTPROCESS_H
