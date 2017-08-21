#include "thumbnailgenerator.h"
#include <QOpenGLFunctions_3_2_Core>

#include "../irisgl/src/graphics/rendertarget.h"
#include "../irisgl/src/graphics/texture2d.h"

void RenderThread::run()
{
    context->makeCurrent(surface);
    auto gl = context->versionFunctions<QOpenGLFunctions_3_2_Core>();

    //renderer = iris::ForwardRenderer::create();
    //scene = iris::Scene::create();

    // create scene and renderer
    //cam = iris::CameraNode::create();
    //cam->setLocalPos(QVector3D(0, 5, 14));
    //cam->setLocalRot(QQuaternion::fromEulerAngles(-5, 0, 0));

    renderTarget = iris::RenderTarget::create(500, 500);
    tex = iris::Texture2D::create(500, 500);
    renderTarget->addTexture(tex);

    renderTarget->bind();
    // render scene to fbo
    gl->glClearColor(255,0,0,255);
    gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    gl->glViewport(0,0,500,500);
    //scene->update(0);
    //renderer->renderSceneToRenderTarget(renderTarget, cam, false);
    renderTarget->unbind();

    // save contents to file
    auto img = renderTarget->toImage();
    img.save("/home/nicolas/Desktop/screenshot_thumb.png");
}

ThumbnialGenerator::ThumbnialGenerator()
{
    renderThread = new RenderThread();

    auto curCtx = QOpenGLContext::currentContext();
    curCtx->doneCurrent();

    auto context = new QOpenGLContext();
    context->setFormat(curCtx->format());
    context->setShareContext(curCtx);
    context->create();
    context->moveToThread(renderThread);
    renderThread->context = context;

    auto surface = new QOffscreenSurface();
    surface->setFormat(context->format());
    surface->create();
    surface->moveToThread(renderThread);
    renderThread->surface = surface;

    renderThread->start();
}

void ThumbnialGenerator::run()
{
    //renderThread->start();
}
