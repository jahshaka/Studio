#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>

#include "ssaopostprocess.h"
#include "../graphics/postprocessmanager.h"
#include "../graphics/postprocess.h"
#include "../graphics/graphicshelper.h"
#include "../graphics/texture2d.h"

namespace iris
{

SSAOPostProcess::SSAOPostProcess()
{
    shader = GraphicsHelper::loadShader(":assets/shaders/postprocesses/default.vs",
                                        ":assets/shaders/postprocesses/ssao.fs");

    normals = Texture2D::load(":assets/textures/random_normal.png");
}

void SSAOPostProcess::process(PostProcessContext *ctx)
{
    shader->bind();
    ctx->sceneTexture->bind(0);
    ctx->depthTexture->bind(1);
    normals->bind(2);
    shader->setUniformValue("u_sceneTexture", 0);
    shader->setUniformValue("u_depthTexture", 1);
    shader->setUniformValue("u_randomNormal", 2);
    ctx->manager->blit(Texture2D::null(), ctx->finalTexture, shader);
    shader->release();
}

}
