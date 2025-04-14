/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ANIMATIONPATH_H
#define ANIMATIONPATH_H

#include "irisglfwd.h"

class AnimationPath
{
public:
	iris::MeshPtr pathMesh;
	iris::MeshPtr intervalMesh;
	iris::ShaderPtr shader;
	iris::MaterialPtr pathMaterial;
	//bool hasPath;

	AnimationPath();

	void generate(iris::SceneNodePtr sceneNode, iris::AnimationPtr);

	void clearPath();

	void submit(iris::RenderList* renderList);
	void render(iris::CameraNodePtr cam, iris::GraphicsDevicePtr device);
};

#endif // !ANIMATIONPATH_H
