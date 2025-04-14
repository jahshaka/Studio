/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "outlinerenderer.h"
#include "irisgl/src/graphics/graphicsdevice.h"
#include "irisgl/src/graphics/utils/fullscreenquad.h"
#include "irisgl/src/graphics/graphicshelper.h"
#include "irisgl/src/graphics/skeleton.h"
#include "irisgl/src/graphics/renderdata.h"
#include "irisgl/src/graphics/utils/fullscreenquad.h"

#include "irisgl/src/scenegraph/scenenode.h"
#include "irisgl/src/scenegraph/meshnode.h"
#include "irisgl/src/scenegraph/cameranode.h"
#include "irisgl/src/scenegraph/particlesystemnode.h"

#include "irisgl/src/graphics/shader.h"

OutlinerRenderer::OutlinerRenderer()
{
	// load resources
}

void OutlinerRenderer::loadAssets()
{
	particleShader = iris::Shader::load(":/assets/shaders/particle.vert",
		":/assets/shaders/particle.frag");

	meshShader = iris::Shader::load(":assets/shaders/color.vert",
		":assets/shaders/color.frag");

	//meshShader->bind();
	//meshShader->setUniformValue("color", QColor(255, 255, 255, 255));

	skinnedShader = iris::Shader::load(":assets/shaders/skinned_color.vert",
		":assets/shaders/color.frag");

	//skinnedShader->bind();
	//skinnedShader->setUniformValue("color", QColor(255, 255, 255, 255));
	//skinnedShader->release();

	outlineShader = iris::Shader::load(":shaders/outlinepp.vert",
//		":assets/shaders/fullscreen.frag");
	":shaders/outlinepp.frag");

	objectTexture = iris::Texture2D::create(100, 100);
	outlineTexture = iris::Texture2D::create(100, 100);

	fsQuad = new iris::FullScreenQuad();

	renderData = new iris::RenderData;
}

void OutlinerRenderer::renderOutline(iris::GraphicsDevicePtr device,
	iris::SceneNodePtr selectedNode, iris::CameraNodePtr cam, float lineWidth, QColor color)
{
	if (!selectedNode)
		return;

	// resize textures
	auto vp = device->getViewport();
	auto size = vp.size();
	objectTexture->resize(size.width(), size.height());
	outlineTexture->resize(size.width(), size.height());

	//device->blit
	device->setRenderTarget(objectTexture);
	device->setViewport(vp);
	device->clear(QColor(0, 0, 0, 0));
	renderNode(device, selectedNode, cam); 
	device->clearRenderTarget();

	device->setRenderTarget(outlineTexture);
	device->setViewport(vp);
	//device->clear(QColor(0, 0, 0, 0));

	device->setShader(outlineShader);
	device->setShaderUniform("u_sceneTex", 0);
	device->setShaderUniform("u_lineWidth", lineWidth);
	device->setShaderUniform("u_color", QVector3D(color.redF(), color.greenF(), color.blueF()));
	device->setTexture(0, objectTexture);
	fsQuad->draw(device, outlineShader);
	//fsQuad->draw();
	device->clearRenderTarget();

	device->setShader(fsQuad->shader);
	device->setBlendState(iris::BlendState::createAlphaBlend());
	device->setTexture(0, outlineTexture);
	//device->setTexture(0, objectTexture);
	fsQuad->draw(device);

}

void OutlinerRenderer::renderNode(iris::GraphicsDevicePtr device,
	iris::SceneNodePtr node,
	iris::CameraNodePtr cam)
{
	//iris::ShaderPtr shader;

	if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
		auto meshNode = node.staticCast<iris::MeshNode>();

		if (meshNode->mesh != nullptr) {
			//QOpenGLShaderProgram* shader;
			if (meshNode->mesh->hasSkeleton())
				device->setShader(skinnedShader);
			else
				device->setShader(meshShader);

			//shader->bind();

			device->setShaderUniform("color", QColor(255, 255, 255, 255));
			device->setShaderUniform("u_worldMatrix", node->globalTransform);
			device->setShaderUniform("u_viewMatrix", cam->viewMatrix);
			device->setShaderUniform("u_projMatrix", cam->projMatrix);
			device->setShaderUniform("u_normalMatrix", node->globalTransform.normalMatrix());

			if (meshNode->mesh->hasSkeleton()) {
				auto boneTransforms = meshNode->mesh->getSkeleton()->boneTransforms;
				device->setShaderUniformArray("u_bones", boneTransforms.data(), boneTransforms.size());
			}

			meshNode->mesh->draw(device);
		}
	}
	else if (node->getSceneNodeType() == iris::SceneNodeType::ParticleSystem) {

		renderData->viewMatrix = cam->viewMatrix;
		renderData->projMatrix = cam->projMatrix;
		auto ps = node.staticCast<iris::ParticleSystemNode>();
		//ps->renderParticles(device, renderData, particleShader);
	}

	for (auto childNode : node->children) {
		if (childNode->isVisible())
			renderNode(device, childNode, cam);
	}
}

OutlinerRenderer::~OutlinerRenderer()
{
	delete fsQuad;
	delete renderData;
}