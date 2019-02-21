#include "handgizmo.h"

class HandGizmoMaterial : public iris::Material
{
public:
	QColor color;
	float fresnelPow;

	HandGizmoMaterial()
	{
		auto shader = iris::Shader::load(
			":assets/shaders/default_material.vert",
			IrisUtils::getAbsoluteAssetPath("app/shaders/fresnel.frag")
			);
		this->setShader(shader);

		this->renderLayer = (int)iris::RenderLayer::Transparent;
		this->renderStates.blendState = iris::BlendState::createAlphaBlend();
		//this->renderStates.rasterState.depthBias = 1;// this ensures it's always on top
		//this->renderStates.rasterState.depthScaleBias = 1;// this ensures it's always on top

		color = QColor(41, 128, 185);
		fresnelPow = 2;

		this->enableFlag("SKINNING_ENABLED");
	}

	void begin(iris::GraphicsDevicePtr device, iris::ScenePtr scene) override
	{
		iris::Material::begin(device, scene);

		device->setShaderUniform("u_color", color);
		device->setShaderUniform("u_fresnelPow", fresnelPow);
	}
};

HandGizmoHandler::HandGizmoHandler()
{
	handScale.setToIdentity();
	handScale.scale(0.01f);// scale by 1/100;
}

void HandGizmoHandler::loadAssets(const iris::ContentManagerPtr& content)
{
	handModel = content->loadModel(IrisUtils::getAbsoluteAssetPath("app/models/right_hand_anims.fbx"));
	handMaterial = QSharedPointer<iris::Material>(new HandGizmoMaterial());
}

bool HandGizmoHandler::doHandPicking(const iris::ScenePtr& scene,
	const QVector3D& segStart,
	const QVector3D& segEnd,
	QList<PickingResult>& hitList)
{
	return false;
}

void HandGizmoHandler::submitHandToScene(const iris::ScenePtr & scene)
{
	for (auto grabber : scene->grabbers) {
		// set appropriate animation
		if (grabber->handPose->getPoseType() == iris::HandPoseType::Grab) {
			handModel->setActiveAnimation("RightHand_rig.001|handFistMove");
		}

		// set time based on factor
		handModel->applyAnimation(grabber->poseFactor*3);

		// update animation
		submitHandToScene(scene, grabber, grabber->getGlobalTransform());
	}
}

void HandGizmoHandler::submitHandToScene(const iris::ScenePtr& scene, const iris::GrabNodePtr& grabNode, const QMatrix4x4& transform)
{
	for (auto& modelMesh : handModel->modelMeshes) {
		scene->geometryRenderList->submitMesh(modelMesh.mesh, handMaterial, transform * handScale * modelMesh.transform);
	}
}