/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "assetviewer.h"

#include "irisgl/src/graphics/rasterizerstate.h"
#include "irisgl/src/scenegraph/particlesystemnode.h" 

#include "constants.h"
#include "globals.h"
#include "sceneviewwidget.h"
#include "core/keyboardstate.h"
#include "core/project.h"
#include "core/assethelper.h"
#include "io/assetmanager.h"
#include "io/scenewriter.h"
#include "io/scenereader.h"

#include <QApplication>
#include <QFileDialog>
#include <QStandardPaths>

#include <QPointer>

AssetViewer::AssetViewer(QWidget *parent) : QOpenGLWidget(parent)
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setVersion(3, 2);
    format.setSamples(4);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapInterval(0);
    setFormat(format);
    
    //QTimer *timer = new QTimer(this);
    //connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    //timer->start(1000.f / 60.f);
    //connect(this, SIGNAL(frameSwapped()), this, SLOT(update()));

    viewport = new iris::Viewport();
    render = false;

    pdialog = new ProgressDialog();
    pdialog->setWindowModality(Qt::WindowModal);
    pdialog->setRange(0, 100);

    connect(this, &AssetViewer::progressChanged, pdialog, &ProgressDialog::setValue);

    // needed in order to get mouse events
    setMouseTracking(true);
    // needed in order to get key events http://stackoverflow.com/a/7879484/991834
    setFocusPolicy(Qt::ClickFocus);
}

void AssetViewer::paintGL()
{
    makeCurrent();

    float dt = elapsedTimer.nsecsElapsed() / (1000.0f * 1000.0f * 1000.0f);
    elapsedTimer.restart();

    if (!!renderer && !!scene) {
        camController->update(dt);
        scene->update(dt);
        renderer->renderScene(dt, viewport);
    }

    doneCurrent();
}

void AssetViewer::updateScene()
{

}

void AssetViewer::initializeGL()
{
    QOpenGLWidget::initializeGL();

    initializeOpenGLFunctions();
    gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();

    gl->glEnable(GL_DEPTH_TEST);
    gl->glEnable(GL_CULL_FACE);

    renderer = iris::ForwardRenderer::create(false);
    scene = iris::Scene::create();
    scene->shadowEnabled = true;
    renderer->setScene(scene);

	previewRT = iris::RenderTarget::create(640, 480);
	screenshotTex = iris::Texture2D::create(640, 480);
	previewRT->addTexture(screenshotTex);

    auto dlight = iris::LightNode::create();
    dlight->setLightType(iris::LightType::Directional);
    dlight->setName("ae98cx7u");
	dlight->color = QColor(255, 255, 240);
	//dlight->setLocalPos(QVector3D(2, 2, 2));
    dlight->setLocalRot(QQuaternion::fromEulerAngles(45, 45, 0));
    dlight->intensity = 0.76;
    dlight->setShadowMapType(iris::ShadowMapType::Soft);
    scene->rootNode->addChild(dlight);

    auto plight = iris::LightNode::create();
    plight->setLightType(iris::LightType::Point);
	plight->setName("ae98cx7u");
    plight->setLocalPos(QVector3D(0, 0, -3));
    plight->color = QColor(210, 210, 255);
    plight->intensity = 0.47;
    plight->setShadowMapType(iris::ShadowMapType::Soft);
    plight->setShadowMapResolution(2048);
    scene->rootNode->addChild(plight);

    auto node = iris::MeshNode::create();
    node->setMesh(":/models/ground.obj");
    node->setLocalPos(QVector3D(0, -5, 0)); // prevent z-fighting with the default plane reset (iKlsR)
    node->setName("ae98cx7u_floor");
    node->setPickable(false);
	node->isBuiltIn = true;
    node->setFaceCullingMode(iris::FaceCullingMode::None);
    node->setShadowCastingEnabled(true);
    scene->rootNode->addChild(node);

    auto m = iris::CustomMaterial::create();
    m->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));
    m->setValue("diffuseTexture", IrisUtils::getAbsoluteAssetPath("app/content/textures/tile.png"));
    m->setValue("textureScale", 4.f);
    node->setMaterial(m);

    defaultCam = new EditorCameraController(nullptr);
    orbitalCam = new OrbitalCameraController(nullptr);
	orbitalCam->previewMode = true;

    camera = iris::CameraNode::create();
    camera->setLocalPos(QVector3D(5, 6, 12));
    camera->lookAt(QVector3D(0, 0.5f, 0));

    scene->setCamera(camera);

    scene->setSkyColor(QColor(25, 25, 25));
    scene->setAmbientColor(QColor(190, 190, 190));

    scene->fogColor = QColor(25, 25, 25);
    scene->fogEnabled = true;
    scene->shadowEnabled = true;

    camera->update(0);
    scene->update(0);

    //camController->end();
    camController = orbitalCam;
    camController->setCamera(camera);
    // has to be set after setCamera
    orbitalCam->pivot = QVector3D(0, 0, 0);
    orbitalCam->distFromPivot = 5;
    orbitalCam->setRotationSpeed(.5f);
    camController->resetMouseStates();
    camController->start();

    auto timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    timer->start(Constants::FPS_60);
}

void AssetViewer::wheelEvent(QWheelEvent *event)
{
    if (camController != nullptr) {
        camController->onMouseWheel(event->delta());
    }
}

void AssetViewer::renderObject() {
	render = true;
}

void AssetViewer::mouseMoveEvent(QMouseEvent *e)
{
    // @ISSUE - only fired when mouse is dragged
    QPointF localPos = e->localPos();
    QPointF dir = localPos - prevMousePos;

    if (camController != nullptr) {
        camController->onMouseMove(-dir.x(), -dir.y());
    }

    prevMousePos = localPos;
}

void AssetViewer::mousePressEvent(QMouseEvent *e)
{
    prevMousePos = e->localPos();

    if (camController != nullptr) {
        //if (e->button() != Qt::MiddleButton) // disable MMB
        camController->onMouseDown(e->button());
    }
}

void AssetViewer::mouseReleaseEvent(QMouseEvent *e)
{
    if (camController != nullptr) {
        camController->onMouseUp(e->button());
    }
}

void AssetViewer::resetViewerCamera()
{
    camera->setLocalPos(localPos);
	camera->setLocalRot(QQuaternion::fromEulerAngles(localRot));
    camera->lookAt(lookAt);
	camera->update(0);

    camController->setCamera(camera);
    orbitalCam->pivot = QVector3D(lookAt);
    orbitalCam->distFromPivot = distanceFromPivot;
    orbitalCam->setRotationSpeed(.5f);
    orbitalCam->updateCameraRot();
    camera->update(0);
}

void AssetViewer::resetViewerCameraAfter()
{
	camera->setLocalPos(localPos);
	camera->setLocalRot(QQuaternion::fromEulerAngles(localRot));
	camera->update(0);

	orbitalCam->distFromPivot = distanceFromPivot;
	orbitalCam->setCamera(camera);
	orbitalCam->setRotationSpeed(.5f);
}

void AssetViewer::loadJafMaterial(QString guid, bool firstAdd, bool cache, bool firstLoad) {
    pdialog->setLabelText(tr("Loading asset preview..."));
    pdialog->show();
    QApplication::processEvents();
    makeCurrent();
    addJafMaterial(guid, firstAdd, cache);
    if (firstLoad) {
        //resetViewerCamera();
    }
    else {
        resetViewerCameraAfter();
    }
    renderObject();
    doneCurrent();
    pdialog->close();
}

void AssetViewer::loadJafShader(QString guid, QMap<QString, QString> &outGuids, bool firstAdd, bool cache, bool firstLoad) {
    pdialog->setLabelText(tr("Loading asset preview..."));
    pdialog->show();
    QApplication::processEvents();
    makeCurrent();
    addJafShader(guid, outGuids, firstAdd, cache);
    if (firstLoad) {
        //resetViewerCamera();
    }
    else {
        resetViewerCameraAfter();
    }
    renderObject();
    doneCurrent();
    pdialog->close();
}

void AssetViewer::loadJafModel(QString str, QString guid, bool firstAdd, bool cache, bool firstLoad) {
    pdialog->setLabelText(tr("Loading asset preview..."));
    pdialog->show();
    QApplication::processEvents();
    makeCurrent();

    addJafMesh(str, guid, firstAdd, cache);
    if (firstLoad) resetViewerCamera();
    else resetViewerCameraAfter();
    
    renderObject();
    doneCurrent();

    pdialog->close();
}

void AssetViewer::loadModel(QString str, bool firstAdd, bool cache, bool firstLoad) {
    pdialog->setLabelText(tr("Loading asset preview..."));
    pdialog->show();
    QApplication::processEvents();
	makeCurrent();
    addMesh(str, firstAdd, cache);
	if (firstLoad) {
		resetViewerCamera();
	}
	else {
		resetViewerCameraAfter();
	}
	renderObject();
	doneCurrent();
    pdialog->close();
}

void AssetViewer::update() {
	QOpenGLWidget::update();
}

void AssetViewer::resizeGL(int width, int height)
{
    // we do an explicit call to glViewport(...) in forwardrenderer
    // with the "good DPI" values so it is not needed here initially (iKlsR)
    viewport->pixelRatioScale = devicePixelRatio();
    viewport->width = width;
    viewport->height = height;
}

void AssetViewer::addJafShader(const QString &guid, QMap<QString, QString> &guidCompareMap, bool firstAdd, bool cache, QVector3D position)
{
    QString assetPath = IrisUtils::join(
        QStandardPaths::writableLocation(QStandardPaths::DataLocation),
        Constants::ASSET_FOLDER, guid
    );
    
    auto shaderDefinition = QJsonDocument::fromBinaryData(db->fetchAssetData(guid)).object();

    auto vAsset = db->fetchAsset(shaderDefinition["vertex_shader"].toString());
    auto fAsset = db->fetchAsset(shaderDefinition["fragment_shader"].toString());

    if (!vAsset.name.isEmpty()) {
        shaderDefinition["vertex_shader"] = QDir(assetPath).filePath(vAsset.name);
    }

    if (!fAsset.name.isEmpty()) {
        shaderDefinition["fragment_shader"] = QDir(assetPath).filePath(fAsset.name);
    }

    iris::CustomMaterialPtr material = iris::CustomMaterialPtr::create();
    material->generate(shaderDefinition);

    auto matball = iris::MeshNode::create();
    matball->setMesh(":/content/primitives/hp_sphere.obj");
    matball->setLocalPos(QVector3D(0, 0, 0)); // prevent z-fighting with the default plane reset (iKlsR)
    matball->setName("ae98cx7u_shader_ball");
    matball->setPickable(false);
    //matball->setFaceCullingMode(iris::FaceCullingMode::None);
    //matball->setShadowCastingEnabled(true);
    matball->setMaterial(material);
    matball->setLocalPos(position);

    addNodeToScene(matball, guid, false, true);
    lastNode = matball->getName();
}

void AssetViewer::addJafMaterial(const QString &guid, bool firstAdd, bool cache, QVector3D position)
{
    QJsonDocument matDoc = QJsonDocument::fromBinaryData(db->fetchAssetData(guid));
    QJsonObject matObject = matDoc.object();
    iris::CustomMaterialPtr material = iris::CustomMaterialPtr::create();

	auto shaderGuid = matObject["guid"].toString();

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
		QString assetPath = IrisUtils::join(
			QStandardPaths::writableLocation(QStandardPaths::DataLocation), Constants::ASSET_FOLDER, guid
		);

		auto shaderDefinition = QJsonDocument::fromBinaryData(db->fetchAssetData(shaderGuid)).object();

		auto vAsset = db->fetchAsset(shaderDefinition["vertex_shader"].toString());
		auto fAsset = db->fetchAsset(shaderDefinition["fragment_shader"].toString());

		if (!vAsset.name.isEmpty()) {
			shaderDefinition["vertex_shader"] = QDir(assetPath).filePath(vAsset.name);
		}

		if (!fAsset.name.isEmpty()) {
			shaderDefinition["fragment_shader"] = QDir(assetPath).filePath(fAsset.name);
		}

		material->setMaterialDefinition(shaderDefinition);
		material->generate(shaderDefinition);
    }

    for (const auto &prop : material->properties) {
        if (prop->type == iris::PropertyType::Color) {
            QColor col;
            col.setNamedColor(matObject.value(prop->name).toString());
            material->setValue(prop->name, col);
        }
        else if (prop->type == iris::PropertyType::Texture) {
            QString materialName = db->fetchAsset(matObject.value(prop->name).toString()).name;
            QString textureStr = IrisUtils::join(
                QStandardPaths::writableLocation(QStandardPaths::DataLocation), Constants::ASSET_FOLDER, guid, materialName
            );
            material->setValue(prop->name, !materialName.isEmpty() ? textureStr : QString());
        }
        else {
            material->setValue(prop->name, QVariant::fromValue(matObject.value(prop->name)));
        }
    }

    auto matball = iris::MeshNode::create();
    matball->setMesh(":/content/primitives/hp_sphere.obj");
    matball->setLocalPos(QVector3D(0, 0, 0)); // prevent z-fighting with the default plane reset (iKlsR)
    matball->setName("ae98cx7u_mat_ball");
    matball->setPickable(false);
    matball->setFaceCullingMode(iris::FaceCullingMode::None);
    matball->setShadowCastingEnabled(true);
    matball->setMaterial(material);
    matball->setLocalPos(position);

    addNodeToScene(matball, guid, false, true, false);
    lastNode = matball->getName();
}

void AssetViewer::addJafMesh(const QString &path, const QString &guid, bool firstAdd, bool cache, QVector3D position)
{
    QJsonDocument document = QJsonDocument::fromBinaryData(db->fetchAssetData(guid));
    QJsonObject objectHierarchy = document.object();

    SceneReader *reader = new SceneReader;
    reader->setBaseDirectory(IrisUtils::join(
        QStandardPaths::writableLocation(QStandardPaths::DataLocation),
        Constants::ASSET_FOLDER, guid)
    );

    iris::SceneNodePtr node = reader->readSceneNode(objectHierarchy);
    delete reader;

    // rename animation sources to relative paths
    auto relativePath = QDir(Globals::project->folderPath).relativeFilePath(path);
    for (auto anim : node->getAnimations()) {
        if (!!anim->skeletalAnimation) anim->skeletalAnimation->source = relativePath;
    }

    node->setLocalPos(position);

    addNodeToScene(node, QFileInfo(path).baseName(), false, true);
    lastNode = node->getName();
}

void AssetViewer::addMesh(const QString &path, bool firstAdd, bool cache, QVector3D position)
{
	QString filename;
	if (path.isEmpty()) {
		filename = QFileDialog::getOpenFileName(this, "Load Mesh", "Mesh Files (*.obj *.fbx *.3ds *.dae *.c4d *.blend)");
	}
	else {
		filename = path;
	}

	if (filename.isEmpty()) return;

    ssource = new iris::SceneSource();

	if (firstAdd) {
		assetMaterial = QJsonObject();
	}

	QJsonArray materialList;

	int iter = 0;
    std::function<void(QJsonObject, QJsonArray&)> extractMeshMaterial = [&](QJsonObject node, QJsonArray &materialList) -> void {
		if (!node["material"].toObject().isEmpty()) materialList.append(node["material"].toObject());

		QJsonArray children = node["children"].toArray();
		if (!children.isEmpty()) {
            for (auto child : children) {
				extractMeshMaterial(child.toObject(), materialList);
				iter++;
			}
		}
	};

	extractMeshMaterial(assetMaterial, materialList);

	int iteration = 0;
	auto node = iris::MeshNode::loadAsSceneFragment(filename, [&, this](iris::MeshPtr mesh, iris::MeshMaterialData& data) {
		auto mat = iris::CustomMaterial::create();

		if (mesh->hasSkeleton())
			mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/DefaultAnimated.shader"));
		else
			mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));

		if (firstAdd) {
			mat->setValue("diffuseColor",	data.diffuseColor);
			mat->setValue("specularColor",	data.specularColor);
			mat->setValue("ambientColor",	QColor(130, 130, 130));
			mat->setValue("emissionColor",	data.emissionColor);
			mat->setValue("shininess",		data.shininess);
			mat->setValue("useAlpha",		true);

			if (QFile(data.diffuseTexture).exists() && QFileInfo(data.diffuseTexture).isFile())
				mat->setValue("diffuseTexture", data.diffuseTexture);

			if (QFile(data.specularTexture).exists() && QFileInfo(data.specularTexture).isFile())
				mat->setValue("specularTexture", data.specularTexture);

			if (QFile(data.normalTexture).exists() && QFileInfo(data.normalTexture).isFile()) {
				mat->setValue("normalTexture", data.normalTexture);
				mat->setValue("normalIntensity", 1.f);
			}

			//QJsonObject matObj;
			//createMaterial(matObj, mat);
			//assetMaterial.insert(QString::number(iteration), matObj);
		}
		else {
			iris::MeshMaterialData cdata;
			auto matinfo = materialList[iteration].toObject();

			QColor col;
			col.setNamedColor(matinfo["ambientColor"].toString());
			cdata.ambientColor = col;
			col.setNamedColor(matinfo["diffuseColor"].toString());
			cdata.diffuseColor = col;
			cdata.diffuseTexture = matinfo["diffuseTexture"].toString();
			cdata.normalTexture = matinfo["normalTexture"].toString();
			cdata.shininess = matinfo["shininess"].toDouble(1.f);
			col.setNamedColor(matinfo["specularColor"].toString());
			cdata.specularColor = col;
			cdata.specularTexture = matinfo["specularTexture"].toString();

			mat->setValue("diffuseColor",	cdata.diffuseColor);
			mat->setValue("specularColor",	cdata.specularColor);
			mat->setValue("ambientColor",	cdata.ambientColor);
			mat->setValue("emissionColor",	cdata.emissionColor);
			mat->setValue("shininess",		cdata.shininess);
			mat->setValue("useAlpha",		true);

			auto libraryTextureIsValid = [](const QString &path, const QString texturePath) {
				return (
					QFile(QDir(QFileInfo(path).absoluteDir()).filePath(texturePath)).exists() &&
					QFileInfo(QDir(QFileInfo(path).absoluteDir()).filePath(texturePath)).isFile()
				);
			};

			if (libraryTextureIsValid(filename, cdata.diffuseTexture))
				mat->setValue("diffuseTexture", QDir(QFileInfo(filename).absoluteDir()).filePath(cdata.diffuseTexture));

			if (libraryTextureIsValid(filename, cdata.specularTexture))
				mat->setValue("specularTexture", QDir(QFileInfo(filename).absoluteDir()).filePath(cdata.specularTexture));

			if (libraryTextureIsValid(filename, cdata.normalTexture)) {
				mat->setValue("normalTexture", QDir(QFileInfo(filename).absoluteDir()).filePath(cdata.normalTexture));
				mat->setValue("normalIntensity", 1.f);
			}
		}

		iteration++;

		mat->renderStates.rasterState = iris::RasterizerState(iris::CullMode::None, GL_FILL);
		return mat;
	}, ssource, this);

	if (firstAdd) {
		SceneWriter::writeSceneNode(assetMaterial, node, false);
	}

	// model file may be invalid so null gets returned
	if (!node) return;

	// rename animation sources to relative paths
	auto relPath = QDir(Globals::project->folderPath).relativeFilePath(filename);
	for (auto anim : node->getAnimations()) {
		if (!!anim->skeletalAnimation)
			anim->skeletalAnimation->source = relPath;
	}

	node->setLocalPos(position);

	addNodeToScene(node, QFileInfo(filename).baseName(), false, true);
}

/**
* adds sceneNode directly to the scene's rootNode
* applied default material to mesh if one isnt present
* ignore set to false means we only add it visually, usually to discard it afterw
*/
void AssetViewer::addNodeToScene(QSharedPointer<iris::SceneNode> sceneNode, QString guid, bool viewed, bool cache, bool isOnGround)
{
	if (!scene) {
		// @TODO: set alert that a scene needs to be set before this can be done
		return;
	}

	auto aabb = getNodeBoundingBox(sceneNode);
    // change position for materials here
	if (isOnGround) sceneNode->setLocalPos(QVector3D(0, -aabb.getMin().y() -5, 0));

	// apply default material to mesh nodes if they have none
	if (sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
		auto meshNode = sceneNode.staticCast<iris::MeshNode>();
		if (!meshNode->getMaterial()) {
			auto mat = iris::CustomMaterial::create();
			mat->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));
			meshNode->setMaterial(mat);
		}
	}

    clearScene();

    scene->rootNode->addChild(sceneNode);

	if (cache) cachedAssets.insert(guid, sceneNode);

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

        for (auto& sphere : spheres) {
            bound = iris::BoundingSphere::merge(bound, sphere);
        }
    }

	bound = aabb.getMinimalEnclosingSphere();
    float dist = (bound.radius * 1.2) / qTan(qDegreesToRadians(camera->angle / 2.f));

	if (!viewed) {
		lookAt = bound.pos;
		localPos = QVector3D(0, bound.pos.y(), 12);
	}

	this->distanceFromPivot = dist;
}

float AssetViewer::getBoundingRadius(iris::SceneNodePtr node)
{
    auto radius = 0.0f;
    if (node->sceneNodeType == iris::SceneNodeType::Mesh)
        radius = node.staticCast<iris::MeshNode>()->getMesh()->boundingSphere.radius;

    for (auto child : node->children) {
        radius = qMax(radius, getBoundingRadius(child));
    }

    return radius;
}

void AssetViewer::getBoundingSpheres(iris::SceneNodePtr node, QList<iris::BoundingSphere> &spheres)
{
    if (node->sceneNodeType == iris::SceneNodeType::Mesh) {
        auto sphere = node.staticCast<iris::MeshNode>()->getTransformedBoundingSphere();
        spheres.append(sphere);
    }

    for (auto child : node->children) {
        getBoundingSpheres(child, spheres);
    }
}

iris::AABB AssetViewer::getNodeBoundingBox(iris::SceneNodePtr node)
{
	iris::AABB aabb;
	if (node->sceneNodeType == iris::SceneNodeType::Mesh) {
		//auto sphere = node.staticCast<iris::MeshNode>()->getTransformedBoundingSphere();
		//aabb.merge(sphere.getAABB());
		auto box = node.staticCast<iris::MeshNode>()->getMesh()->getAABB();
		box.offset(node->getGlobalPosition());// todo: include other transforms
		aabb.merge(box);
	}

	for (auto child : node->children) {
		aabb.merge(getNodeBoundingBox(child));
	}

	return aabb;
}

QImage AssetViewer::takeScreenshot(int width, int height)
{
	makeCurrent();

	previewRT->resize(width, height, true);
    scene->renderSky = false;
	scene->update(0);
    scene->renderSky = true;
	renderer->renderLightBillboards = false;
	renderer->renderSceneToRenderTarget(previewRT, camera, false, false);
	renderer->renderLightBillboards = true;
	auto img = previewRT->toImage();
	doneCurrent();

	return img;
}

void AssetViewer::changeBackdrop(unsigned int id)
{
	switch (id) {
	    case 1:
            scene->fogEnabled = false;
            scene->shadowEnabled = false;
		    scene->setSkyColor(QColor(25, 25, 25, 0));
            if (scene->rootNode->hasChildren()) {
                for (auto child : scene->rootNode->children) {
                    if (child->getName() == "ae98cx7u_floor") {
                        child->hide();
                    }
                }
            }
	    break;
	    case 2:
            scene->fogEnabled = false;
            scene->shadowEnabled = false;
		    scene->setSkyColor(QColor(82, 82, 82, 0));
            if (scene->rootNode->hasChildren()) {
                for (auto child : scene->rootNode->children) {
                    if (child->getName() == "ae98cx7u_floor") {
                        child->hide();
                    }
                }
            }
	    break;
        case 3: {
            scene->setSkyColor(QColor(25, 25, 25));
            scene->fogEnabled = true;
            scene->fogColor = QColor(25, 25, 25);
            scene->shadowEnabled = true;
            if (scene->rootNode->hasChildren()) {
                for (auto child : scene->rootNode->children) {
                    if (child->getName() == "ae98cx7u_floor") {
                        child->show();
                    }
                }
            }
            break;
        }
	}
}

void AssetViewer::createMaterial(QJsonObject &matObj, iris::CustomMaterialPtr mat)
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

void AssetViewer::cacheCurrentModel(QString guid)
{
	if (scene->rootNode->hasChildren()) {
		for (auto child : scene->rootNode->children) {
			// clear the scene of anything that is not a light for the next asset
			if (child->sceneNodeType != iris::SceneNodeType::Light) {
				cachedAssets.insert(guid, child);
			}
		}
	}
}

QJsonObject AssetViewer::getSceneProperties()
{
	auto jsonToVec3 = [](const QVector3D &vec) {
		QJsonObject obj;
		obj["x"] = vec.x();
		obj["y"] = vec.y();
		obj["z"] = vec.z();
		return obj;
	};

	QJsonObject properties;
	QJsonObject cameraObj;
	cameraObj["pos"] = jsonToVec3(camera->getLocalPos());
	cameraObj["distFromPivot"] = orbitalCam->distFromPivot;
	cameraObj["rot"] = jsonToVec3(camera->getLocalRot().toEulerAngles());

	properties["camera"] = cameraObj;

	return properties;
}
