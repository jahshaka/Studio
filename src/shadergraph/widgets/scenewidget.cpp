/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include <QQuaternion>
#include "scenewidget.h"
#include <irisgl/IrisGL.h>
//#include "irisgl/src/graphics/graphicshelper.h"
//#include "nodemodel.h"
#include "../models/properties.h"
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QMouseEvent>
#include <QWheelEvent>
#include <qmath.h>
#include <QOpenGLFunctions>
#include <QImage>

#include "../graph/nodegraph.h"
#include "../graph/graphnodescene.h"
#include "../core/texturemanager.h"
#include "materialsettingswidget.h"
#include "../core/materialhelper.h"
#include "../assets.h"

float SceneWidget::renderTime = 0;

QString assetPath(QString relPath)
{
   QDir::cleanPath(QDir::currentPath() + QDir::separator() + relPath);
    return QDir::cleanPath(QDir::currentPath() + QDir::separator() + relPath);
}

// NOTE! Context resets when widget is undocked
void SceneWidget::start()
{
	if (initialized)
		return;

	screenshotRT = iris::RenderTarget::create(500, 500);
	screenshotTex = iris::Texture2D::create(500, 500);
	screenshotRT->addTexture(screenshotTex);

    iris::VertexLayout layout;
    layout.addAttrib(iris::VertexAttribUsage::Position, GL_FLOAT, 3, sizeof (float) * 3);
    vertexBuffer = iris::VertexBuffer::create(layout);
    //vertexBuffer->setData()

	clearColor = QColor(125, 125, 125);

    cam = iris::CameraNode::create();
    cam->setLocalPos(QVector3D(2, 0, 3));
    cam->lookAt(QVector3D(0,0,0));
    /*
    shader = iris::Shader::load(
                ":assets/shaders/color.vert",
                ":assets/shaders/color.frag");
                */

	//mesh = iris::Mesh::loadMesh(MaterialHelper::assetPath("lowpoly_sphere.obj"));
	sphereMesh = iris::Mesh::loadMesh(MaterialHelper::assetPath("lowpoly_sphere.obj"));
	cubeMesh = iris::Mesh::loadMesh(MaterialHelper::assetPath("cube.obj"));
	planeMesh = iris::Mesh::loadMesh(MaterialHelper::assetPath("plane.obj"));
	cylinderMesh = iris::Mesh::loadMesh(MaterialHelper::assetPath("cylinder.obj"));
	capsuleMesh = iris::Mesh::loadMesh(MaterialHelper::assetPath("capsule.obj"));
	torusMesh = iris::Mesh::loadMesh(MaterialHelper::assetPath("torus.obj"));
	mesh = sphereMesh;
	mesh = shadergraph::Assets::sphereMesh;
    //mat = iris::DefaultMaterial::create();

    font = iris::Font::create(device);

    vertString = iris::GraphicsHelper::loadAndProcessShader(MaterialHelper::assetPath("surface.vert"));
    fragString = iris::GraphicsHelper::loadAndProcessShader(MaterialHelper::assetPath("surface.frag"));
	generatedVertString = "void surface(inout vec3 vertexOffset, inout float vertexExtrusion){}";
	generatedFragString = "void surface(inout Material material){}";
	//updateShader("void surface(inout Material material){}");
	updateShader();

    renderTime = 0;

	lights.clear();

    // setup lights
    auto main = iris::LightNode::create();
    main->setLightType(iris::LightType::Point);
    main->setLocalPos(QVector3D(-3, 0, 3));
    main->setVisible(true);
    main->color = QColor(255, 255, 255);
    main->intensity = 0.8f;
    lights.append(main);

    auto light = iris::LightNode::create();
    light->setLightType(iris::LightType::Directional);
    light->setLocalPos(QVector3D(0, 3, 0));
    light->setVisible(true);
    light->color = QColor(255, 255, 255);
    light->intensity = 1;
    lights.append(light);

    light = iris::LightNode::create();
    light->setLightType(iris::LightType::Point);
    light->setLocalPos(QVector3D(3, 0, 3));
    light->setVisible(true);
    light->color = QColor(255, 255, 255);
    light->intensity = 1;
    lights.append(light);

	initialized = true;
}

void SceneWidget::update(float dt)
{
    cam->aspectRatio = viewportWidth/(float)viewportHeight;
    cam->update(dt);

    //qDebug()<<1.0/dt;
    fps = 1.0/dt;
    renderTime += dt;
}

void SceneWidget::render()
{
    //spriteBatch->begin();
    //spriteBatch->drawString(font, QString("fps %1").arg(fps), QVector2D(), Qt::black);
    //spriteBatch->end();
	
	// reset render states
    //device->setBlendState(iris::BlendState::createOpaque(), true);
    //device->setDepthState(iris::DepthState(), true);
	//device->setRasterizerState(iris::RasterizerState::createCullCounterClockwise(), true);

	// apply effect states
	setMaterialSettings(graph->settings);

	device->setBlendState(blendState, true);
	device->setDepthState(depthState, true);
	device->setRasterizerState(rasterState, true);

    auto& graphics = device;
    device->setViewport(QRect(0, 0, viewportWidth, viewportHeight));
	device->clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, clearColor);


    device->setShader(shader);
    device->setShaderUniform("u_viewMatrix", cam->viewMatrix);
    device->setShaderUniform("u_projMatrix", cam->projMatrix);
    QMatrix4x4 world;
    world.setToIdentity();
	world.rotate(rot);
	world.scale(scale);

    device->setShaderUniform("u_worldMatrix", world);
    device->setShaderUniform("u_normalMatrix", world.normalMatrix());
    device->setShaderUniform("color", QVector4D(0.0, 0.0, 0.0, 1.0));
    device->setShaderUniform("u_textureScale", 1.0f);

    graphics->setShaderUniform("u_eyePos", cam->getLocalPos());
    graphics->setShaderUniform("u_sceneAmbient", QVector3D(0,0,0));
    graphics->setShaderUniform("u_time", renderTime);

	// pass textures
	auto texMan = TextureManager::getSingleton();
	texMan->loadUnloadedTextures();
	int i = 0;
	for (auto tex : texMan->textures) {
		auto uniformName = getTextureUniformName(tex->guid);

		graphics->setTexture(i, tex->texture);
		graphics->setShaderUniform(uniformName, i);
		i++;
	}
	

    // lights
    //qDebug()<<lights.size();
	if (!graph->settings.acceptLighting) {
		graphics->setShaderUniform("u_lightCount", 0);
	}
	else {
        graphics->setShaderUniform("u_lightCount", static_cast<int>(lights.size()));

		for (int i = 0; i < lights.size(); i++)
		{
			QString lightPrefix = QString("u_lights[%0].").arg(i);

			auto light = lights[i];
			if (!light->isVisible())
			{
				//quick hack for now
				graphics->setShaderUniform(lightPrefix + "color", QColor(0, 0, 0));
				continue;
			}

			graphics->setShaderUniform(lightPrefix + "type", (int)light->lightType);
			graphics->setShaderUniform(lightPrefix + "position", light->getLocalPos());
			//mat->setUniformValue(lightPrefix+"direction", light->getDirection());
			graphics->setShaderUniform(lightPrefix + "distance", light->distance);
			graphics->setShaderUniform(lightPrefix + "direction", light->getLightDir());
			graphics->setShaderUniform(lightPrefix + "cutOffAngle", light->spotCutOff);
			graphics->setShaderUniform(lightPrefix + "cutOffSoftness", light->spotCutOffSoftness);
			graphics->setShaderUniform(lightPrefix + "intensity", light->intensity);
			graphics->setShaderUniform(lightPrefix + "color", light->color);

			graphics->setShaderUniform(lightPrefix + "constantAtten", 1.0f);
			graphics->setShaderUniform(lightPrefix + "linearAtten", 0.0f);
			graphics->setShaderUniform(lightPrefix + "quadtraticAtten", 1.0f);

			graphics->setShaderUniform(lightPrefix + "shadowType", (int)iris::ShadowMapType::None);
		}
	}

    passNodeGraphUniforms();

    mesh->draw(device);

	//device->setRasterizerState(iris::RasterizerState::createCullNone(), true);
	//device->setDepthState(iris::DepthState(false, false), true);
}

void SceneWidget::setVertexShader(QString vertShader)
{
	generatedVertString = vertShader;
}

void SceneWidget::setFragmentShader(QString fragShader)
{
	generatedFragString = fragShader;
}

/*
void SceneWidget::updateShader(QString shaderCode)
{
    shader = iris::Shader::create(
                vertString,
                fragString + shaderCode);
}
*/

void SceneWidget::updateShader()
{
	shader = iris::Shader::create(
		vertString + generatedVertString,
		fragString + generatedFragString);
}

void SceneWidget::resetRenderTime()
{
    renderTime = 0;
}

QString SceneWidget::getTextureUniformName(QString texGuid)
{
	for (auto prop : graph->properties) {
		if (prop->type == PropertyType::Texture)
			if (prop->getValue() == texGuid)
				return prop->getUniformName();
	}

	return "";
}

void SceneWidget::passNodeGraphUniforms()
{
    for( auto prop : graph->properties) {
        switch(prop->type) {
        case PropertyType::Bool:
            device->setShaderUniform(prop->getUniformName(), prop->getValue().toBool());
        break;
        case PropertyType::Int:
            device->setShaderUniform(prop->getUniformName(), prop->getValue().toInt());
        break;
        case PropertyType::Float:
            //qDebug()<<prop->getUniformName()<<" - "<<prop->getValue().toFloat();
            device->setShaderUniform(prop->getUniformName(), prop->getValue().toFloat());
        break;
        case PropertyType::Vec2:
            device->setShaderUniform(prop->getUniformName(), prop->getValue().value<QVector2D>());
        break;
        case PropertyType::Vec3:
            device->setShaderUniform(prop->getUniformName(), prop->getValue().value<QVector3D>());
        break;
        case PropertyType::Vec4:
            device->setShaderUniform(prop->getUniformName(), prop->getValue().value<QVector4D>());
        break;
        }
    }
}

void SceneWidget::setNodeGraph(NodeGraph *graph)
{
    this->graph = graph;
}

void SceneWidget::mousePressEvent(QMouseEvent * evt)
{
	dragging = true;
	lastMousePos = evt->pos();
}

void SceneWidget::mouseMoveEvent(QMouseEvent * evt)
{
	if (dragging) {
		auto curPos = evt->pos();
		auto diff = lastMousePos - curPos;
		lastMousePos = curPos;
		float dragScale = 0.7f;

		auto tx = QQuaternion::fromEulerAngles(-diff.y() * dragScale, -diff.x() * dragScale, 0);
		rot = tx * rot;
	}
}

void SceneWidget::mouseReleaseEvent(QMouseEvent * evt)
{
	dragging = false;
}

void SceneWidget::wheelEvent(QWheelEvent* evt)
{
    scale += (evt->angleDelta().y()/480.0f);
}

QImage SceneWidget::takeScreenshot(int width, int height)
{
	this->makeCurrent();
	auto tempW = viewportWidth;
	auto tempH = viewportHeight;
	viewportWidth = width;
	viewportHeight = height;
	QColor savedClearColor = clearColor;
	clearColor = QColor(0, 0, 0, 0);

	cam->aspectRatio = viewportWidth / (float)viewportHeight;
	cam->update(0.01f);// just a non-zero value is needed here

	screenshotRT->resize(width, height, true);
	device->setRenderTarget(screenshotRT);
	render();
	device->clearRenderTarget();
	
	viewportWidth = tempW;
	viewportHeight = tempH;
	clearColor = savedClearColor;

	auto img = screenshotRT->toImage();
	this->doneCurrent();

	return img;
}

void SceneWidget::resizeEvent(QResizeEvent* evt)
{
	viewportWidth = (int)(width() * devicePixelRatioF());
	viewportHeight = (int)(height() * devicePixelRatioF());

	iris::RenderWidget::resizeEvent(evt);
}

void SceneWidget::setPreviewModel(PreviewModel model)
{
	this->previewModel = model;

	if (initialized) {
		switch (previewModel) {
		case PreviewModel::Sphere:
			mesh = shadergraph::Assets::sphereMesh;
			break;
		case PreviewModel::Cube:
			mesh = shadergraph::Assets::cubeMesh;
			break;
		case PreviewModel::Plane:
			mesh = shadergraph::Assets::planeMesh;
			break;
		case PreviewModel::Cylinder:
			mesh = shadergraph::Assets::cylinderMesh;
			break;
		case PreviewModel::Capsule:
			mesh = shadergraph::Assets::capsuleMesh;
			break;
		case PreviewModel::Torus:
			mesh = shadergraph::Assets::torusMesh;
			break;
		}
	}
}

SceneWidget::SceneWidget():
    iris::RenderWidget(nullptr)
{
	rot = QQuaternion::fromEulerAngles(0, 0, 0);
	scale = 1;
	dragging = false;

	generatedVertString = "";
	generatedFragString = "";
}

void SceneWidget::setMaterialSettings(MaterialSettings settings)
{
	this->materialSettings = settings;

	// blend state
	if (settings.blendMode == BlendMode::Additive)
		blendState = iris::BlendState::createAdditive();
	if (settings.blendMode == BlendMode::Blend)
		blendState = iris::BlendState::createAlphaBlend();
	if (settings.blendMode == BlendMode::Opaque)
		blendState = iris::BlendState::createOpaque();

	// depth state
	depthState = iris::DepthState(false, false);
	depthState.depthBufferEnabled = settings.depthTest;
	depthState.depthWriteEnabled = settings.zwrite;

	// rasterizer state
	if (settings.cullMode == CullMode::Back)
		rasterState = iris::RasterizerState::createCullCounterClockwise();
	if (settings.cullMode == CullMode::Front)
		rasterState = iris::RasterizerState::createCullClockwise();
	if (settings.cullMode == CullMode::None)
		rasterState = iris::RasterizerState::createCullNone();
}
