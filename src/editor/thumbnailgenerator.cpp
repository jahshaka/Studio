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

#include <QtMath>

ThumbnailGenerator* ThumbnailGenerator::instance = nullptr;

void RenderThread::requestThumbnail(const ThumbnailRequest &request)
{
    QMutexLocker locker(&requestMutex);
    requests.append(request);
    requestsAvailable.release();
    locker.unlock();
}

void RenderThread::run()
{
    this->setPriority(QThread::LowestPriority);
    context->makeCurrent(surface);
    initScene();

    renderTarget = iris::RenderTarget::create(500, 500);
    tex = iris::Texture2D::create(500, 500);
    renderTarget->addTexture(tex);

    shutdown = false;

    while(!shutdown) {
        requestsAvailable.acquire();

        // the size still has to be checked because there is a case where the size
        // of the requests isnt equal to the available locks in the semaphore i.e.
        // when the thread needs to die but the semaphore is waiting for a lock in order
        // to continue execution
        if (requests.size()>0){

            //qDebug()<<"rendering";
            QMutexLocker locker(&requestMutex);
            auto request = requests.takeFirst();
            locker.unlock();

            prepareScene(request);
            //scene->rootNode->addChild(sceneNode);

            scene->update(0);
            renderer->renderSceneToRenderTarget(renderTarget, cam, true, false);

            cleanupScene();
            //meshNode->

            // save contents to file
            auto img = renderTarget->toImage();

            auto result = new ThumbnailResult;
            result->id = request.id;
            result->type = request.type;
            result->path = request.path;
            result->thumbnail = img;
			result->material = assetMaterial;
			result->textureList = getTextureList();

            emit thumbnailComplete(result);
        }
    }

    // move to main thread to be cleaned up
    // all ui objects must be destroyed on the main thread
    auto mainThread = qApp->instance()->thread();
    context->moveToThread(mainThread);
    surface->moveToThread(mainThread);
}

void RenderThread::initScene()
{
    auto gl = context->versionFunctions<QOpenGLFunctions_3_2_Core>();
    gl->glEnable(GL_DEPTH_TEST);
    gl->glEnable(GL_CULL_FACE);
    //gl->glDisable(GL_BLEND);

    renderer = iris::ForwardRenderer::create(false);
    scene = iris::Scene::create();
    renderer->setScene(scene);

    // create scene and renderer
    cam = iris::CameraNode::create();
    cam->setLocalPos(QVector3D(1, 1, 5));
    //cam->setLocalRot(QQuaternion::fromEulerAngles(-45, 45, 0));
    cam->lookAt(QVector3D(0,0.5f,0));

    //scene->setSkyColor(QColor(100, 100, 100, 0));
    //scene->setAmbientColor(QColor(255, 255, 255));

	scene->setSkyColor(QColor(25, 25, 25, 0));
	scene->setAmbientColor(QColor(190, 190, 190));

 //   // second node
 //   auto node = iris::MeshNode::create();
 //   node->setMesh(":/models/ground.obj");
 //   node->setLocalPos(QVector3D(0, 0, 0));
 //   node->setName("Ground");
 //   node->setPickable(false);
 //   node->setShadowCastingEnabled(false);

 //   auto dlight = iris::LightNode::create();
 //   dlight->setLightType(iris::LightType::Directional);
 //   scene->rootNode->addChild(dlight);
 //   dlight->setName("Key Light");
 //   dlight->setLocalRot(QQuaternion::fromEulerAngles(45, -45, 0));
 //   dlight->intensity = 1;
	//dlight->setShadowMapType(iris::ShadowMapType::Soft);
 //   //dlight->icon = iris::Texture2D::load(":/icons/light.png");

 //   auto plight = iris::LightNode::create();
 //   plight->setLightType(iris::LightType::Directional);
 //   scene->rootNode->addChild(plight);
 //   plight->setName("Fill Light");
 //   dlight->setLocalRot(QQuaternion::fromEulerAngles(90, 180, 90));
 //   plight->intensity = 1;
 //   plight->color = QColor(255, 200, 200);
 //   //plight->icon = iris::Texture2D::load(":/icons/bulb.png");
	//plight->setShadowMapType(iris::ShadowMapType::None);

 //   plight = iris::LightNode::create();
 //   plight->setLightType(iris::LightType::Directional);
 //   scene->rootNode->addChild(plight);
 //   plight->setName("Rim Light");
 //   dlight->setLocalRot(QQuaternion::fromEulerAngles(60, 0, 0));
 //   plight->intensity = 1;
 //   plight->color = QColor(200, 222, 200);
	//plight->setShadowMapType(iris::ShadowMapType::None);

	auto dlight = iris::LightNode::create();
	dlight->setLightType(iris::LightType::Directional);
	dlight->setName("Key Light");
	dlight->color = QColor(255, 255, 240);
	//dlight->setLocalPos(QVector3D(2, 2, 2));
	dlight->setLocalRot(QQuaternion::fromEulerAngles(45, 45, 0));
	dlight->intensity = 0.76;
	dlight->setShadowMapType(iris::ShadowMapType::None);
	//dlight->shadowMap->shadowType = iris::ShadowMapType::None;
	scene->rootNode->addChild(dlight);

	auto plight = iris::LightNode::create();
	plight->setLightType(iris::LightType::Point);
	plight->setName("Rim Light");
	plight->setLocalPos(QVector3D(0, 0, -3));
	plight->color = QColor(210, 210, 255);
	plight->intensity = 0.47;
	plight->setShadowMapType(iris::ShadowMapType::None);
	plight->shadowMap->shadowType = iris::ShadowMapType::None;
	scene->rootNode->addChild(plight);

    // fog params
    scene->fogEnabled = false;
    scene->shadowEnabled = false;

    cam->update(0);// necessary!
    scene->update(0);
}

void RenderThread::prepareScene(const ThumbnailRequest &request)
{
    if(request.type == ThumbnailRequestType::Mesh)
    {
		ssource = new iris::SceneSource();
        // load mesh as scene
		int iteration = 0;
        sceneNode = iris::MeshNode::loadAsSceneFragment(request.path, [&](iris::MeshPtr mesh, iris::MeshMaterialData& data)
        {
            auto mat = iris::CustomMaterial::create();
            if (mesh->hasSkeleton())
                mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/DefaultAnimated.shader"));
            else
                mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));

            mat->setValue("diffuseColor", data.diffuseColor);
            mat->setValue("specularColor", data.specularColor);
            mat->setValue("ambientColor", QColor(110, 110, 110));
            mat->setValue("emissionColor", data.emissionColor);
            mat->setValue("shininess", data.shininess);

            if (QFile(data.diffuseTexture).exists() && QFileInfo(data.diffuseTexture).isFile())
                mat->setValue("diffuseTexture", data.diffuseTexture);

            if (QFile(data.specularTexture).exists() && QFileInfo(data.specularTexture).isFile())
                mat->setValue("specularTexture", data.specularTexture);

            if (QFile(data.normalTexture).exists() && QFileInfo(data.normalTexture).isFile())
                mat->setValue("normalTexture", data.normalTexture);

			QJsonObject matObj;
			createMaterial(matObj, mat);
			assetMaterial.insert(QString::number(iteration), matObj);

			iteration++;

            return mat;
        }, ssource);

        if (!sceneNode) return;

        scene->rootNode->addChild(sceneNode);

        // fit object in view
        QList<iris::BoundingSphere> spheres;
        getBoundingSpheres(sceneNode, spheres);
        iris::BoundingSphere bound;

        //merge bounding spheres
        if (spheres.count() == 0) {
            bound.pos = QVector3D(0,0,0);
            bound.radius = 1;
        } else if (spheres.count() == 1) {
            bound = spheres[0];
        } else {
            bound.pos = QVector3D(0,0,0);
            bound.radius = 1;

            for(auto& sphere : spheres) {
                bound = iris::BoundingSphere::merge(bound, sphere);
            }
        }

        float dist = (bound.radius * 1.2) / qTan(qDegreesToRadians(cam->angle / 2.0f));
        cam->setLocalPos(QVector3D(0, bound.pos.y(), dist));
        cam->lookAt(bound.pos);
        cam->update(0);
    }
    else
    {
        // load sphere model

        // load material

        // apply
    }
}

QStringList RenderThread::getTextureList()
{
	const aiScene *scene = ssource->importer.GetScene();

	QStringList texturesToCopy;

	for (int i = 0; i < scene->mNumMeshes; i++) {
		auto mesh = scene->mMeshes[i];
		auto material = scene->mMaterials[mesh->mMaterialIndex];

		aiString textureName;

		if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
			material->GetTexture(aiTextureType_DIFFUSE, 0, &textureName);
			texturesToCopy.append(textureName.C_Str());
		}

		if (material->GetTextureCount(aiTextureType_SPECULAR) > 0) {
			material->GetTexture(aiTextureType_SPECULAR, 0, &textureName);
			texturesToCopy.append(textureName.C_Str());
		}

		if (material->GetTextureCount(aiTextureType_NORMALS) > 0) {
			material->GetTexture(aiTextureType_NORMALS, 0, &textureName);
			texturesToCopy.append(textureName.C_Str());
		}

		if (material->GetTextureCount(aiTextureType_HEIGHT) > 0) {
			material->GetTexture(aiTextureType_HEIGHT, 0, &textureName);
			texturesToCopy.append(textureName.C_Str());
		}
	}

	return texturesToCopy;
}

void RenderThread::cleanupScene()
{
    //scene->rootNode->removeChild(sceneNode);
    if (!!sceneNode)
        sceneNode->removeFromParent();
}

float RenderThread::getBoundingRadius(iris::SceneNodePtr node)
{
    auto radius = 0.0f;
    if(node->sceneNodeType== iris::SceneNodeType::Mesh)
        radius = node.staticCast<iris::MeshNode>()->getMesh()->boundingSphere.radius;

    for(auto child : node->children) {
        radius = qMax(radius, getBoundingRadius(child));
    }

    return radius;
}

void RenderThread::getBoundingSpheres(iris::SceneNodePtr node, QList<iris::BoundingSphere> &spheres)
{
    if(node->sceneNodeType== iris::SceneNodeType::Mesh) {
        auto sphere = node.staticCast<iris::MeshNode>()->getTransformedBoundingSphere();
        spheres.append(sphere);
    }

    for(auto child : node->children) {
        getBoundingSpheres(child, spheres);
    }
}

void RenderThread::createMaterial(QJsonObject &matObj, iris::CustomMaterialPtr mat)
{
	matObj["name"] = mat->getName();

	for (auto prop : mat->properties) {
		if (prop->type == iris::PropertyType::Bool) {
			matObj[prop->name] = prop->getValue().toBool();
		}

		if (prop->type == iris::PropertyType::Float) {
			matObj[prop->name] = prop->getValue().toFloat();
		}

		if (prop->type == iris::PropertyType::Color) {
			matObj[prop->name] = prop->getValue().value<QColor>().name();
		}

		if (prop->type == iris::PropertyType::Texture) {
			matObj[prop->name] = QFileInfo(prop->getValue().toString()).fileName();
		}
	}
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
    if (instance == Q_NULLPTR) instance = new ThumbnailGenerator();
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

void ThumbnailGenerator::shutdown()
{
    renderThread->shutdown = true;
    renderThread->requestsAvailable.release();// release 1 so the thread's main loop continues;
    renderThread->wait();
}