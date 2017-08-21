#include "thumbnailgenerator.h"
#include <QOpenGLFunctions_3_2_Core>

#include "../constants.h"

#include "../irisgl/src/graphics/rendertarget.h"
#include "../irisgl/src/graphics/texture2d.h"
#include "../irisgl/src/graphics/forwardrenderer.h"
#include "../irisgl/src/scenegraph/scene.h"
#include "../irisgl/src/scenegraph/lightnode.h"
#include "../irisgl/src/scenegraph/cameranode.h"
#include "../irisgl/src/scenegraph/meshnode.h"
#include "../irisgl/src/materials/custommaterial.h"

void RenderThread::run()
{
    context->makeCurrent(surface);
    auto gl = context->versionFunctions<QOpenGLFunctions_3_2_Core>();
    gl->glEnable(GL_DEPTH_TEST);
    gl->glEnable(GL_CULL_FACE);
    gl->glDisable(GL_BLEND);

    renderer = iris::ForwardRenderer::create();
    scene = iris::Scene::create();
    renderer->setScene(scene);

    // create scene and renderer
    cam = iris::CameraNode::create();
    cam->setLocalPos(QVector3D(0, 5, 14));
    cam->setLocalRot(QQuaternion::fromEulerAngles(-30, 0, 0));

    scene->setSkyColor(QColor(255, 72, 72));
    scene->setAmbientColor(QColor(255, 255, 255));

    // second node
    auto node = iris::MeshNode::create();
    node->setMesh(":/models/cube.obj");
    node->setLocalPos(QVector3D(0, 0, 0)); // prevent z-fighting with the default plane
    node->setName("Ground");
    node->setPickable(false);
    node->setShadowEnabled(false);

    auto m = iris::CustomMaterial::create();
    m->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));
    m->setValue("diffuseTexture", ":/content/textures/tile.png");
    //m->setValue("diffuseTexture", ":/content/skies/alternative/cove/back.jpg");
    m->setValue("textureScale", 4.f);
    //m->setValue("emission",QColor(255, 255, 255));
    m->setValue("diffuseColor", QColor(255, 255, 255, 255));
    m->setValue("useAlpha", false);
    node->setMaterial(m);

    scene->rootNode->addChild(node);

    auto dlight = iris::LightNode::create();
    dlight->setLightType(iris::LightType::Directional);
    scene->rootNode->addChild(dlight);
    dlight->setName("Directional Light");
    dlight->setLocalPos(QVector3D(4, 4, 0));
    dlight->setLocalRot(QQuaternion::fromEulerAngles(15, 0, 0));
    dlight->intensity = 1;
    dlight->icon = iris::Texture2D::load(":/icons/light.png");

    auto plight = iris::LightNode::create();
    plight->setLightType(iris::LightType::Point);
    scene->rootNode->addChild(plight);
    plight->setName("Point Light");
    plight->setLocalPos(QVector3D(-4, 4, 0));
    plight->intensity = 1;
    plight->icon = iris::Texture2D::load(":/icons/bulb.png");

    // fog params
    scene->fogColor = QColor(72, 72, 72);
    scene->fogEnabled = false;
    scene->shadowEnabled = false;


    renderTarget = iris::RenderTarget::create(500, 500);
    tex = iris::Texture2D::create(500, 500);
    renderTarget->addTexture(tex);

    //renderTarget->bind();
    // render scene to fbo
    gl->glClearColor(255,0,0,255);
    gl->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    gl->glViewport(0,0,500,500);
    scene->update(0);
    cam->update(0);// necessary!

    renderer->renderSceneToRenderTarget(renderTarget, cam, false);
    //renderTarget->unbind();

    // save contents to file
    auto img = renderTarget->toImage();
    //todo: strip alpha channel from image
    img.save("/home/nicolas/Desktop/screenshot_thumb.jpg");
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
