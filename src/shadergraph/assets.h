#pragma once

#include <QtGlobal>

#include <irisgl/IrisGL.h>

namespace shadergraph
{

class Assets
{
	static bool isLoaded;

public:
	static iris::MeshPtr sphereMesh;
	static iris::MeshPtr cubeMesh;
	static iris::MeshPtr planeMesh;
	static iris::MeshPtr cylinderMesh;
	static iris::MeshPtr capsuleMesh;
	static iris::MeshPtr torusMesh;


	static void load();
	static void clear();
};

}