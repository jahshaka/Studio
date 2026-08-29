/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "assets.h"
#include "core/materialhelper.h"

namespace shadergraph
{


	bool Assets::isLoaded = false;

	iris::MeshPtr Assets::sphereMesh = iris::MeshPtr();
	iris::MeshPtr Assets::cubeMesh = iris::MeshPtr();
	iris::MeshPtr Assets::planeMesh = iris::MeshPtr();
	iris::MeshPtr Assets::cylinderMesh = iris::MeshPtr();
	iris::MeshPtr Assets::capsuleMesh = iris::MeshPtr();
	iris::MeshPtr Assets::torusMesh = iris::MeshPtr();


	void Assets::load()
	{
		sphereMesh = iris::Mesh::loadMesh(MaterialHelper::assetPath("lowpoly_sphere.obj"));
		cubeMesh = iris::Mesh::loadMesh(MaterialHelper::assetPath("cube.obj"));
		planeMesh = iris::Mesh::loadMesh(MaterialHelper::assetPath("plane.obj"));
		cylinderMesh = iris::Mesh::loadMesh(MaterialHelper::assetPath("cylinder.obj"));
		capsuleMesh = iris::Mesh::loadMesh(MaterialHelper::assetPath("capsule.obj"));
		torusMesh = iris::Mesh::loadMesh(MaterialHelper::assetPath("torus.obj"));
	}

	void Assets::clear()
	{
		sphereMesh.reset();
		cubeMesh.reset();
		planeMesh.reset();
		cylinderMesh.reset();
		capsuleMesh.reset();
		torusMesh.reset();
	}

}