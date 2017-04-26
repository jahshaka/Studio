#ifndef SSAOPOSTPROCESS_H
#define SSAOPOSTPROCESS_H

#include "../graphics/postprocess.h"
#include <QVector3D>

class QOpenGLShaderProgram;

namespace iris {

class SSAOPostProcess;
typedef QSharedPointer<SSAOPostProcess> SSAOPostProcessPtr;

class SSAOPostProcess : public PostProcess
{
    QOpenGLShaderProgram* shader;
    Texture2DPtr normals;
public:
    SSAOPostProcess();
    virtual void process(PostProcessContext* ctx) override;

    static SSAOPostProcessPtr create();
};

}


#endif // SSAOPOSTPROCESS_H
