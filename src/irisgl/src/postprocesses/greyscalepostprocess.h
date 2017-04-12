#ifndef GREYSCALEPOSTPROCESS_H
#define GREYSCALEPOSTPROCESS_H

#include "../graphics/postprocess.h"
#include <QVector3D>

class QOpenGLShaderProgram;

namespace  iris {

class GreyscalePostProcess : public PostProcess
{
    QOpenGLShaderProgram* shader;
public:
    GreyscalePostProcess();

    virtual void process(PostProcessContext* ctx) override;
};

}

#endif // GREYSCALEPOSTPROCESS_H
