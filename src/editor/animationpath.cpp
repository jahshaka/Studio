/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/SceneGraph.h"
#include "irisgl/Animation.h"
#include "irisgl/Graphics.h"
#include "irisgl/extras/Materials.h"

#include "animationpath.h"

AnimationPath::AnimationPath()
{
	//meshPath = iris::Mesh::create();
	iris::VertexLayout layout;
	layout.addAttrib(iris::VertexAttribUsage::Position, GL_FLOAT, 3, sizeof(float) * 3);

	auto mat = iris::ColorMaterial::create();
	mat->setColor(Qt::blue);
	pathMaterial = mat;
}

void AnimationPath::clearPath()
{
	if (!!pathMesh) {
		pathMesh->clearVertexBuffers();
		pathMesh.clear();
	}
}

void AnimationPath::generate(iris::SceneNodePtr sceneNode, iris::AnimationPtr anim)
{
	QVector<QVector3D> pointList;
	if (anim->hasPropertyAnim("position")) {
		auto posAnim = anim->getVector3PropertyAnim("position");
		auto end = anim->getLength();
		for (float i = 0.0f; i < end; i += 0.01f) {
			// add point
			pointList.append(posAnim->getValue(i));
		}

		pathMesh.clear();

		iris::VertexLayout layout;
		layout.addAttrib(iris::VertexAttribUsage::Position, GL_FLOAT, 3, sizeof(float) * 3);
		pathMesh = iris::MeshPtr(iris::Mesh::create((void*)pointList.constData(), pointList.size() * sizeof(QVector3D), pointList.size(), &layout));
		pathMesh->setPrimitiveMode(iris::PrimitiveMode::LineStrip);
	}

	
	//pathMesh->addVertexBuffer()
}

void AnimationPath::submit(iris::RenderList* renderList)
{
	if (!!pathMesh) {
		QMatrix4x4 world;
		world.setToIdentity();
		renderList->submitMesh(pathMesh, pathMaterial, world);
	}
}

void AnimationPath::render(iris::CameraNodePtr cam, iris::GraphicsDevicePtr device)
{
	device->setShader(shader);
	device->setShaderUniform("u_viewMatrix", cam->viewMatrix);
	device->setShaderUniform("u_projMatrix", cam->projMatrix);
	device->setShaderUniform("u_viewMatrix", cam->viewMatrix);
}