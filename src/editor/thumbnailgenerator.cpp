#include "thumbnailgenerator.h"
#include <QOpenGLFunctions_3_2_Core>

#include "../constants.h"

#include "../irisgl/src/graphics/rendertarget.h"
#include "../irisgl/src/graphics/texture2d.h"
#include "../irisgl/src/graphics/mesh.h"
#include "../irisgl/src/graphics/forwardrenderer.h"
#include "../irisgl/src/scenegraph/scene.h"
#include "../irisgl/src/scenegraph/lightnode.h"
#include "../irisgl/src/scenegraph/cameranode.h"
#include "../irisgl/src/scenegraph/meshnode.h"
#include "../irisgl/src/materials/custommaterial.h"

#include <QMutex>
#include <QMutexLocker>

ThumbnailGenerator* ThumbnailGenerator::instance = nullptr;

void RenderThread::requestThumbnail(const ThumbnailRequest &request)
{
    QMutexLocker locker(&requestMutex);
    requests.append(request);
}

void RenderThread::run()
{
    context->makeCurrent(surface);
    initScene();

    renderTarget = iris::RenderTarget::create(500, 500);
    tex = iris::Texture2D::create(500, 500);
    renderTarget->addTexture(tex);

    while(true) {
        //qDebug()<<"rendering";
        ThumbnailRequest request;
        bool hasRequest = false;
        {
            QMutexLocker locker(&requestMutex);
            if(requests.size()>0)
            {
                request = requests.takeFirst();
                hasRequest = true;
            }
        }

        if (hasRequest) {

            prepareScene(request);
            scene->rootNode->addChild(sceneNode);

            scene->update(0);
            renderer->renderSceneToRenderTarget(renderTarget, cam, false);

            cleanupScene();
            //meshNode->

            // save contents to file
            auto img = renderTarget->toImage();
            //todo: strip alpha channel from image
            img.save("/home/nicolas/Desktop/screenshot_thumb.jpg");

            ThumbnailResult result;
            result.id = request.id;
            result.type = request.type;
            result.path = request.path;
            result.thumbnail = img;

            emit thumbnailComplete(result);
        }
    }
}

void RenderThread::initScene()
{
    auto gl = context->versionFunctions<QOpenGLFunctions_3_2_Core>();
    gl->glEnable(GL_DEPTH_TEST);
    gl->glEnable(GL_CULL_FACE);
    //gl->glDisable(GL_BLEND);

    renderer = iris::ForwardRenderer::create();
    scene = iris::Scene::create();
    renderer->setScene(scene);

    // create scene and renderer
    cam = iris::CameraNode::create();
    cam->setLocalPos(QVector3D(1, 1, 5));
    //cam->setLocalRot(QQuaternion::fromEulerAngles(-45, 45, 0));
    cam->lookAt(QVector3D(0,0.5f,0));

    scene->setSkyColor(QColor(50, 50, 50));
    scene->setAmbientColor(QColor(255, 255, 255));

    // second node
    auto node = iris::MeshNode::create();
    node->setMesh(":/models/ground.obj");
    node->setLocalPos(QVector3D(0, 0, 0));
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

    //scene->rootNode->addChild(node);

    auto dlight = iris::LightNode::create();
    dlight->setLightType(iris::LightType::Directional);
    scene->rootNode->addChild(dlight);
    dlight->setName("Directional Light");
    dlight->setLocalPos(QVector3D(4, 4, 0));
    dlight->setLocalRot(QQuaternion::fromEulerAngles(15, 0, 0));
    dlight->intensity = 1;
    //dlight->icon = iris::Texture2D::load(":/icons/light.png");

    auto plight = iris::LightNode::create();
    plight->setLightType(iris::LightType::Point);
    scene->rootNode->addChild(plight);
    plight->setName("Point Light");
    plight->setLocalPos(QVector3D(-4, 4, 0));
    plight->intensity = 1;
    //plight->icon = iris::Texture2D::load(":/icons/bulb.png");

    // fog params
    scene->fogColor = QColor(72, 72, 72);
    scene->fogEnabled = false;
    scene->shadowEnabled = false;

    cam->update(0);// necessary!
}

void RenderThread::prepareScene(const ThumbnailRequest &request)
{
    if(request.type == ThumbnailRequestType::Mesh)
    {
        // load mesh as scene
        sceneNode = iris::MeshNode::loadAsSceneFragment(request.path,[](iris::MeshPtr mesh, iris::MeshMaterialData& data)
        {
            auto mat = iris::CustomMaterial::create();
            if (mesh->hasSkeleton())
                mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/DefaultAnimated.shader"));
            else
                mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));

            mat->setValue("diffuseColor", data.diffuseColor);
            mat->setValue("specularColor", data.specularColor);
            mat->setValue("ambientColor", data.ambientColor);
            mat->setValue("emissionColor", data.emissionColor);

            mat->setValue("shininess", data.shininess);

            if (QFile(data.diffuseTexture).exists() && QFileInfo(data.diffuseTexture).isFile())
                mat->setValue("diffuseTexture", data.diffuseTexture);

            if (QFile(data.specularTexture).exists() && QFileInfo(data.specularTexture).isFile())
                mat->setValue("specularTexture", data.specularTexture);

            if (QFile(data.normalTexture).exists() && QFileInfo(data.normalTexture).isFile())
                mat->setValue("normalTexture", data.normalTexture);

            return mat;
        });

        scene->rootNode->addChild(sceneNode);

        // fit object in view
        auto boundRadius = getBoundingRadius(sceneNode);


        cam->setLocalPos(QVector3D(1, 1, 5).normalized()*(boundRadius+5));
        cam->lookAt(QVector3D(0, boundRadius/2.0f, 0));
        cam->update(0);

        // apply default material
    }
    else
    {
        // load sphere model

        // load material

        // apply
    }
}

void RenderThread::cleanupScene()
{
    scene->rootNode->removeChild(sceneNode);
}

float RenderThread::getBoundingRadius(iris::SceneNodePtr node)
{
    auto radius = 0.0f;
    if(node->sceneNodeType== iris::SceneNodeType::Mesh)
        radius = node.staticCast<iris::MeshNode>()->getMesh()->boundingSphere->radius;

    for(auto child : node->children) {
        radius = qMax(radius, getBoundingRadius(child));
    }

    return radius;
}

ThumbnailGenerator::ThumbnailGenerator()
{
    renderThread = new RenderThread();

    auto curCtx = QOpenGLContext::currentContext();
    if (curCtx != nullptr)
        curCtx->doneCurrent();

    QSurfaceFormat format;
    format.setDepthBufferSize(32);
    format.setMajorVersion(3);
    format.setMinorVersion(2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    //format.setOption();
    format.setSamples(1);

    auto context = new QOpenGLContext();
    context->setFormat(format);
    //context->setShareContext(curCtx);
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

ThumbnailGenerator *ThumbnailGenerator::getSingleton()
{
    if(instance==nullptr)
        instance = new ThumbnailGenerator();

    return instance;
}

void ThumbnailGenerator::requestThumbnail(ThumbnailRequestType type, QString path, QString id)
{
    ThumbnailRequest req;
    req.type = type;
    req.path = path;
    req.id = id;
    renderThread->requestThumbnail(req);
}

//void ThumbnialGenerator::run()
//{
//    //renderThread->start();
//}
