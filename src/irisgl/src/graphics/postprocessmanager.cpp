#include <QOpenGLContext>
#include <QOpenGLFunctions_3_2_Core>

#include "postprocessmanager.h"
#include "rendertarget.h"
#include "utils/fullscreenquad.h"
#include "texture2d.h"
#include "postprocess.h"

#include "../postprocesses/coloroverlaypostprocess.h"
#include "../postprocesses/radialblurpostprocess.h"
#include "../postprocesses/bloompostprocess.h"
#include "../postprocesses/greyscalepostprocess.h"

namespace iris
{

PostProcessManager::PostProcessManager()
{
    gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    rtInitialized = false;
    fsQuad = new FullScreenQuad();

    //postProcesses.append(new ColorOverlayPostProcess());
    //postProcesses.append(new RadialBlurPostProcess());
    //postProcesses.append(new BloomPostProcess());
    postProcesses.append(new GreyscalePostProcess());
}

void PostProcessManager::blit(iris::Texture2DPtr source, iris::Texture2DPtr dest, QOpenGLShaderProgram *shader)
{
    initRenderTarget();

    renderTarget->clearTextures();
    renderTarget->addTexture(dest);

    renderTarget->bind();

    gl->glViewport(0, 0, dest->texture->width(), dest->texture->height());
    gl->glClear(GL_COLOR_BUFFER_BIT);

    if (!!source)
        source->bind(0);

    if (shader)
        fsQuad->draw(gl, shader);
    else
        fsQuad->draw(gl);

    renderTarget->unbind();
    renderTarget->clearTextures();
}

void PostProcessManager::process(PostProcessContext *context)
{
    context->manager = this;

    for (auto process : postProcesses) {
        process->process(context);
    }
}

void PostProcessManager::initRenderTarget()
{
    if (!rtInitialized)
    {
        // the size shouldnt matter
        renderTarget = RenderTarget::create(100, 100);
        renderTarget->clearRenderBuffer();

        rtInitialized = true;
    }
}

}
