#ifndef ANIMATIONPATH_H
#define ANIMATIONPATH_H

#include "../../irisgl/src/irisglfwd.h"

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
