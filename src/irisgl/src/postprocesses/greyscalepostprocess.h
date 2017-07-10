#ifndef GREYSCALEPOSTPROCESS_H
#define GREYSCALEPOSTPROCESS_H

#include "../graphics/postprocess.h"
#include <QVector3D>

class QOpenGLShaderProgram;

namespace  iris {

class GreyscalePostProcess;
typedef QSharedPointer<GreyscalePostProcess> GreyscalePostProcessPtr;

class GreyscalePostProcess : public PostProcess
{
    QOpenGLShaderProgram* shader;
    Texture2DPtr final;
public:
    GreyscalePostProcess();

    virtual void process(PostProcessContext* ctx) override;

    static GreyscalePostProcessPtr create();
};

}

#endif // GREYSCALEPOSTPROCESS_H
