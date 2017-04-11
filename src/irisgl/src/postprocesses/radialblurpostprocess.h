#ifndef RADIALBLURPOSTPROCESS_H
#define RADIALBLURPOSTPROCESS_H

#include "../graphics/postprocess.h"
#include <QVector3D>

class QOpenGLShaderProgram;

namespace iris
{

class RadialBlurPostProcess : public PostProcess
{
    QOpenGLShaderProgram* shader;
public:
    RadialBlurPostProcess();

    virtual void process(PostProcessContext* ctx) override;
};

}


#endif // RADIALBLURPOSTPROCESS_H
