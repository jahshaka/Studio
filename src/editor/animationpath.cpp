#include "irisgl/SceneGraph.h"
#include "irisgl/Animation.h"
#include "irisgl/Graphics.h"

#include "animationpath.h"

AnimationPath::AnimationPath()
{
	//meshPath = iris::Mesh::create();
	iris::VertexLayout layout;
	layout.addAttrib(iris::VertexAttribUsage::Position, GL_FLOAT, 3, sizeof(float) * 3);
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
	if (anim->hasPropertyAnim("Position")) {
		auto posAnim = anim->getVector3PropertyAnim("Position");
		auto end = anim->getLength();
		for (float i = 0.0f; i < end; i += 0.5f) {
			// add point
			pointList.append(posAnim->getValue(i));
		}

		pathMesh.clear();

		iris::VertexLayout layout;
		layout.addAttrib(iris::VertexAttribUsage::Position, GL_FLOAT, 3, sizeof(float) * 3);
		pathMesh = iris::MeshPtr(iris::Mesh::create((void*)pointList.constData(), pointList.size() * sizeof(QVector3D), pointList.size(), &layout));
	}

	
	//pathMesh->addVertexBuffer()
}

void AnimationPath::submit(iris::RenderList* renderList)
{
	//renderList->submitMesh()
	//renderList->submitMesh()
}

void AnimationPath::render(iris::CameraNodePtr cam, iris::GraphicsDevicePtr device)
{
	device->setShader(shader);
	device->setShaderUniform("u_viewMatrix", cam->viewMatrix);
	device->setShaderUniform("u_projMatrix", cam->projMatrix);
	device->setShaderUniform("u_viewMatrix", cam->viewMatrix);
}