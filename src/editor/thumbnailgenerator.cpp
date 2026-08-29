/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QQuaternion>
#include "thumbnailgenerator.h"

#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QtMath>
#include <QStandardPaths>
#include <QTimer>

#include "irisgl/src/graphics/forwardrenderer.h"
#include "irisgl/src/graphics/mesh.h"
#include "irisgl/src/graphics/rendertarget.h"
#include "irisgl/src/graphics/texture2d.h"
#include "irisgl/src/scenegraph/cameranode.h"
#include "irisgl/src/scenegraph/lightnode.h"
#include "irisgl/src/scenegraph/meshnode.h"
#include "irisgl/src/scenegraph/scene.h"

#include "constants.h"
#include "io/assetmanager.h"
#include "io/scenereader.h"
#include "io/materialreader.hpp"
#include "engine/enginehost.h"
#include "enginethumbnailrenderer.h"
#include "irisgl/src/materials/defaultmaterial.h"
#include "irisgl/src/materials/pbrmaterial.h"

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
    shutdown = false;

    // Engine viewport (xcb): there is no usable Qt GL context on this thread —
    // makeCurrent() fails and the 3.2 Core functions resolve to null (SIGSEGV in
    // initScene). Stay alive and drain requests without rendering so callers that
    // hold this thread's signals keep working; thumbnails simply do not arrive.
    if (!context->makeCurrent(surface) ||
        !QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_2_Core>(context)) {
        qWarning("ThumbnailGenerator: no usable OpenGL context on the render thread; thumbnails disabled");
        while (!shutdown) {
            requestsAvailable.acquire();
            QMutexLocker locker(&requestMutex);
            requests.clear();
        }
        auto mainThread = qApp->instance()->thread();
        context->moveToThread(mainThread);
        surface->moveToThread(mainThread);
        return;
    }
    initScene();

    renderTarget = iris::RenderTarget::create(512, 512);
    tex = iris::Texture2D::create(512, 512);
    renderTarget->addTexture(tex);

    shutdown = false;

    while(!shutdown) {
        requestsAvailable.acquire();
        // the size still has to be checked because there is a case where the size
        // of the requests isnt equal to the available locks in the semaphore i.e.
        // when the thread needs to die but the semaphore is waiting for a lock in order
        // to continue execution
        if (requests.size() > 0) {
            QMutexLocker locker(&requestMutex);
            auto request = requests.takeFirst();
            locker.unlock();

            prepareScene(request);

            scene->update(0);
            renderer->renderSceneToRenderTarget(renderTarget, cam, true, false);

            cleanupScene();

            // save contents to file
            auto img = renderTarget->toImage();

            auto result = new ThumbnailResult;
            result->id			= request.id;
            result->type		= request.type;
            result->path		= request.path;
            result->preview     = request.preview;
            result->thumbnail	= img;

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
//    auto gl = context->versionFunctions<QOpenGLFunctions_3_2_Core>();
    QOpenGLContext* context = QOpenGLContext::currentContext();
    auto gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_2_Core>(context);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glEnable(GL_CULL_FACE);

    renderer = iris::ForwardRenderer::create(false);
    scene = iris::Scene::create();
    renderer->setScene(scene);

    // create scene and renderer
    cam = iris::CameraNode::create();
    cam->setLocalPos(QVector3D(1, 1, 5));
    cam->lookAt(QVector3D(0,0.5f,0));

	scene->setSkyColor(QColor(25, 25, 25, 0));
	scene->setAmbientColor(QColor(190, 190, 190));

	auto dlight = iris::LightNode::create();
	dlight->color = QColor(255, 255, 240);
	dlight->intensity = 0.76;
	dlight->setLightType(iris::LightType::Directional);
	dlight->setName("Key Light");
	dlight->setLocalRot(QQuaternion::fromEulerAngles(45, 45, 0));
	dlight->setShadowMapType(iris::ShadowMapType::None);
	scene->rootNode->addChild(dlight);

	auto plight = iris::LightNode::create();
	plight->color = QColor(210, 210, 255);
	plight->intensity = 0.47;
	plight->setLightType(iris::LightType::Point);
	plight->setName("Rim Light");
	plight->setLocalPos(QVector3D(0, 0, -3));
	plight->setShadowMapType(iris::ShadowMapType::None);
	scene->rootNode->addChild(plight);

	materialNode = iris::MeshNode::create();
	materialNode->setMesh(IrisUtils::getAbsoluteAssetPath("app/content/primitives/sphere.obj"));
	scene->getRootNode()->addChild(materialNode);
	materialNode->hide();

	auto m = iris::CustomMaterial::create();
	m->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));
	m->setValue("diffuseColor",		QColor(255, 255, 255));
	m->setValue("specularColor",	QColor(255, 255, 255));
	m->setValue("ambientColor",		QColor(110, 110, 110));
	m->setValue("emissionColor",	QColor(0, 0, 0));
	m->setValue("shininess",		0);
	materialNode->setMaterial(m);

    // fog params
    scene->fogEnabled = false;
    scene->shadowEnabled = false;

    cam->update(0); // necessary!
    scene->update(0);
}

void RenderThread::prepareScene(const ThumbnailRequest &request)
{
    auto guid = request.id;

    if (request.type == ThumbnailRequestType::ImportedMesh) {
        QJsonDocument document = QJsonDocument::fromJson(db->fetchAssetData(guid));
        QJsonObject objectHierarchy = document.object();

        SceneReader *reader = new SceneReader;
        reader->setBaseDirectory(IrisUtils::join(
            Globals::project->getProjectFolder())
        );

        sceneNode = reader->readSceneNode(objectHierarchy);
        delete reader;

        if (!sceneNode) return;

        materialNode->hide();
        scene->rootNode->addChild(sceneNode);

        // fit object in view
        QList<iris::BoundingSphere> spheres;
        getBoundingSpheres(sceneNode, spheres);
        iris::BoundingSphere bound;

        //merge bounding spheres
        if (spheres.count() == 0) {
            bound.pos = QVector3D(0, 0, 0);
            bound.radius = 1;
        }
        else if (spheres.count() == 1) {
            bound = spheres[0];
        }
        else {
            bound.pos = QVector3D(0, 0, 0);
            bound.radius = 1;
            for (auto &sphere : spheres) bound = iris::BoundingSphere::merge(bound, sphere);
        }

        float dist = (bound.radius * 1.2) / qTan(qDegreesToRadians(cam->angle / 2.0f));
        cam->setLocalPos(QVector3D(0, bound.pos.y(), dist));
        cam->lookAt(bound.pos);
        cam->update(0);
    }
    else if (request.type == ThumbnailRequestType::Mesh)
    {
		ssource = new iris::SceneSource();
        // load mesh as scene
        sceneNode = iris::MeshNode::loadAsSceneFragment(request.path, [&](iris::MeshPtr mesh, iris::MeshMaterialData& data)
        {
            auto mat = iris::CustomMaterial::create();
            mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));

            mat->setValue("diffuseColor",	data.diffuseColor);
            mat->setValue("specularColor",	data.specularColor);
            mat->setValue("ambientColor",	QColor(110, 110, 110));	// assume this color, some formats set this to pitch black
            mat->setValue("emissionColor",	data.emissionColor);
            mat->setValue("shininess",		data.shininess);
			mat->setValue("useAlpha",		true);

            if (QFile(data.diffuseTexture).exists() && QFileInfo(data.diffuseTexture).isFile())
                mat->setValue("diffuseTexture", data.diffuseTexture);

            if (QFile(data.specularTexture).exists() && QFileInfo(data.specularTexture).isFile())
                mat->setValue("specularTexture", data.specularTexture);

            if (QFile(data.normalTexture).exists() && QFileInfo(data.normalTexture).isFile())
                mat->setValue("normalTexture", data.normalTexture);

            return mat;
        }, ssource);

        if (!sceneNode) return;

		materialNode->hide();
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
            for (auto &sphere : spheres) bound = iris::BoundingSphere::merge(bound, sphere);
        }

        float dist = (bound.radius * 1.2) / qTan(qDegreesToRadians(cam->angle / 2.0f));
        cam->setLocalPos(QVector3D(0, bound.pos.y(), dist));
        cam->lookAt(bound.pos);
        cam->update(0);
    }
	else if (request.type == ThumbnailRequestType::Material) {
		QFile *file = new QFile(request.path);
		file->open(QIODevice::ReadOnly | QIODevice::Text);
		QJsonDocument doc = QJsonDocument::fromJson(file->readAll());

		MaterialReader reader;
		auto material = reader.parseMaterial(doc.object(), db);
		/*
        auto material = iris::CustomMaterial::create();
		const QJsonObject materialDefinition = doc.object();
        auto shaderGuid = materialDefinition["guid"].toString();
        material->setName(materialDefinition["name"].toString());
        material->setGuid(shaderGuid);

        QFileInfo shaderFile;

        QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
        while (it.hasNext()) {
            it.next();
            if (it.key() == shaderGuid) {
                shaderFile = QFileInfo(IrisUtils::getAbsoluteAssetPath(it.value()));
                break;
            }
        }

        if (shaderFile.exists()) {
            material->generate(shaderFile.absoluteFilePath());
        }
        else {
            // Reading is thread safe...
            for (auto asset : AssetManager::getAssets()) {
                if (asset->type == ModelTypes::Shader) {
                    if (asset->assetGuid == material->getGuid()) {
                        auto def = asset->getValue().toJsonObject();
                        auto vertexShader = def["vertex_shader"].toString();
                        auto fragmentShader = def["fragment_shader"].toString();
                        for (auto asset : AssetManager::getAssets()) {
                            if (asset->type == ModelTypes::File) {
                                if (vertexShader == asset->assetGuid) vertexShader = asset->path;
                                if (fragmentShader == asset->assetGuid) fragmentShader = asset->path;
                            }
                        }
                        def["vertex_shader"] = vertexShader;
                        def["fragment_shader"] = fragmentShader;
                        material->generate(def);
                    }
                }
            }
        }

		for (const auto &prop : material->properties) {
			if (materialDefinition.contains(prop->name)) {
				if (prop->type == iris::PropertyType::Texture) {
					auto textureStr = !materialDefinition[prop->name].toString().isEmpty()
										? QDir(QFileInfo(request.path).absolutePath())
											.filePath(materialDefinition[prop->name].toString())
										: QString();

					if (!textureStr.isEmpty()) {
						material->setValue(prop->name, textureStr);
					}
				}
				else {
					material->setValue(prop->name, materialDefinition[prop->name].toVariant());
				}
			}
		}
		*/
		materialNode->setMaterial(material);
		materialNode->show();

		float dist = 1.2f / qTan(qDegreesToRadians(cam->angle / 2.f));
		cam->setLocalPos(QVector3D(0, 0, dist));
		cam->lookAt(QVector3D(0, 0, 0));
		cam->update(0);
    }
}
void RenderThread::cleanupScene()
{
    if (!!sceneNode) sceneNode->removeFromParent();
}

float RenderThread::getBoundingRadius(iris::SceneNodePtr node)
{
    auto radius = 0.0f;
    if (node->sceneNodeType == iris::SceneNodeType::Mesh)
        radius = node.staticCast<iris::MeshNode>()->getMesh()->boundingSphere.radius;

    for (auto child : node->children) {
        radius = qMax(radius, getBoundingRadius(child));
    }

    return radius;
}

void RenderThread::getBoundingSpheres(iris::SceneNodePtr node, QList<iris::BoundingSphere> &spheres)
{
    if (node->sceneNodeType == iris::SceneNodeType::Mesh) {
        auto sphere = node.staticCast<iris::MeshNode>()->getTransformedBoundingSphere();
        spheres.append(sphere);
    }

    for (auto child : node->children) {
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
    renderThread->setDatabase(db);

    if (EngineHost::viewportBackend() == ViewportBackend::Engine) {
        // Engine viewport: no Qt GL context anywhere, so no render thread. Requests
        // are drained one per tick on the main thread once the Engine is up
        // (EngineHost starts it after this singleton may already exist).
        engineMode = true;
        tick = new QTimer();
        tick->setInterval(16);
        QObject::connect(tick, &QTimer::timeout, [this] { processOneEngineRequest(); });
        return;
    }

    auto curCtx = QOpenGLContext::currentContext();
    if (curCtx != Q_NULLPTR) curCtx->doneCurrent();

    QSurfaceFormat format;
    format.setDepthBufferSize(32);
    format.setMajorVersion(3);
    format.setMinorVersion(2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setSamples(1);

    auto context = new QOpenGLContext();
    context->setFormat(format);
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

void ThumbnailGenerator::requestThumbnail(ThumbnailRequestType type, QString path, QString id, bool preview,
                                          QSize size)
{
    ThumbnailRequest req;
    req.type	= type;
    req.path	= path;
    req.id		= id;
    req.preview = preview;
    if (engineMode) {
        pending.append({req, size});
        if (tick && !tick->isActive()) tick->start();
        return;
    }
    renderThread->requestThumbnail(req);
}

void ThumbnailGenerator::shutdown()
{
    if (engineMode) {
        // Must run while the Engine is alive (EngineHost::shutdown() comes after
        // the main window is gone); the renderer checks anyway.
        if (tick) tick->stop();
        pending.clear();
        engineRenderer.reset();
        return;
    }
    renderThread->shutdown = true;
    renderThread->requestsAvailable.release();// release 1 so the thread's main loop continues;
    renderThread->wait();
}

// ---------------------------------------------------------------------------
// Engine path (main thread)
// ---------------------------------------------------------------------------

void ThumbnailGenerator::processOneEngineRequest()
{
    if (pending.isEmpty()) { if (tick) tick->stop(); return; }
    // Re-entrancy guard: a result handler may open a modal dialog (material preview
    // export), whose nested event loop would tick us again mid-render.
    static bool inFlight = false;
    if (inFlight) return;
    struct Guard { Guard() { inFlight = true; } ~Guard() { inFlight = false; } } guard;
    auto engine = EngineHost::instance().engine();
    if (!engine) {
        // Engine not started yet, or the app fell back to the legacy viewport (no
        // engine will ever come): drop the backlog and stop ticking for good.
        if (!EngineHost::instance().isRunning()) { pending.clear(); if (tick) tick->stop(); return; }
        while (pending.size() > 256) pending.removeFirst();
        return;
    }
    if (!engineRenderer) engineRenderer.reset(new EngineThumbnailRenderer(engine));
    // One request per tick: never block the UI for a batch.
    const EngineRequest job = pending.takeFirst();
    QImage img = renderEngineRequest(job.request, job.size);
    auto result = new ThumbnailResult;
    result->id          = job.request.id;
    result->type        = job.request.type;
    result->path        = job.request.path;
    result->preview     = job.request.preview;
    result->thumbnail   = img;
    // Deliver from the event loop, not from inside this tick: receivers may block
    // (a save dialog) and must never re-enter the renderer.
    QMetaObject::invokeMethod(renderThread, [rt = renderThread, result] { emit rt->thumbnailComplete(result); },
                              Qt::QueuedConnection);
}

iris::MaterialPtr ThumbnailGenerator::previewMaterialFor(iris::MaterialPtr material)
{
    // The engine mirror understands PbrMaterial and DefaultMaterial. Anything else
    // (shader-graph CustomMaterial) contributes its diffuse/base colour if it has one.
    if (!material) return iris::DefaultMaterial::create().staticCast<iris::Material>();
    if (dynamic_cast<iris::PbrMaterial *>(material.data()) ||
        dynamic_cast<iris::DefaultMaterial *>(material.data()))
        return material;

    auto def = iris::DefaultMaterial::create();
    for (auto prop : material->properties) {
        if (!prop || prop->type != iris::PropertyType::Color) continue;
        if (prop->name == "diffuseColor" || prop->name == "color" || prop->name == "albedo" ||
            prop->name == "baseColor") {
            def->setDiffuseColor(prop->getValue().value<QColor>());
            break;
        }
    }
    return def.staticCast<iris::Material>();
}

QImage ThumbnailGenerator::renderEngineRequest(const ThumbnailRequest &request, QSize size)
{
    if (request.type == ThumbnailRequestType::ImportedMesh) {
        if (!db || !Globals::project) return QImage();
        QJsonDocument document = QJsonDocument::fromJson(db->fetchAssetData(request.id));
        SceneReader reader;
        reader.setBaseDirectory(IrisUtils::join(Globals::project->getProjectFolder()));
        QJsonObject objectHierarchy = document.object();
        auto node = reader.readSceneNode(objectHierarchy);
        if (!node) return QImage();
        return engineRenderer->renderNode(node, size);
    }

    if (request.type == ThumbnailRequestType::Mesh) {
        iris::SceneSource source;   // owns the assimp importer for the load's duration
        auto node = iris::MeshNode::loadAsSceneFragment(request.path,
            [](iris::MeshPtr, iris::MeshMaterialData &data)
        {
            // Textures are not yet in the engine material model (materials v0).
            auto mat = iris::DefaultMaterial::create();
            mat->setDiffuseColor(data.diffuseColor.isValid() ? data.diffuseColor : QColor(200, 200, 200));
            mat->setSpecularColor(data.specularColor.isValid() ? data.specularColor : QColor(255, 255, 255));
            mat->setAmbientColor(QColor(110, 110, 110));
            mat->setShininess(data.shininess);
            return mat.staticCast<iris::Material>();
        }, &source);
        if (!node) return QImage();
        return engineRenderer->renderNode(node, size);
    }

    if (request.type == ThumbnailRequestType::Material) {
        QFile file(request.path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QImage();
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        MaterialReader reader;
        auto material = reader.parseMaterial(doc.object(), db);
        return engineRenderer->renderMaterial(previewMaterialFor(material.staticCast<iris::Material>()), size);
    }
    return QImage();
}
