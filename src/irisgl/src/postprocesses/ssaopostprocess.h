#ifndef SSAOPOSTPROCESS_H
#define SSAOPOSTPROCESS_H

#include "../graphics/postprocess.h"
#include <QVector3D>

class QOpenGLShaderProgram;

namespace iris {

class SSAOPostProcess : public PostProcess
{
    QOpenGLShaderProgram* shader;
    Texture2DPtr normals;
public:
    SSAOPostProcess();
    virtual void process(PostProcessContext* ctx) override;
};

}


#endif // SSAOPOSTPROCESS_H
