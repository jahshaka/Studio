/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "playervrcontroller.h"
#include "../irisgl/src/graphics/graphicsdevice.h"
#include "../irisgl/src/graphics/renderitem.h"
#include "../irisgl/src/graphics/material.h"
#include "../irisgl/src/graphics/mesh.h"
#include "../irisgl/src/graphics/model.h"
#include "../irisgl/src/graphics/shader.h"
#include "../irisgl/src/materials/defaultmaterial.h"
#include "../irisgl/src/vr/vrdevice.h"
#include "../irisgl/src/vr/vrmanager.h"
#include "../irisgl/src/scenegraph/scene.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../irisgl/src/scenegraph/meshnode.h"
#include "../irisgl/src/scenegraph/grabnode.h"
#include "../irisgl/src/scenegraph/viewernode.h"
#include "../irisgl/src/core/irisutils.h"
#include "../irisgl/src/math/mathhelper.h"
#include "../irisgl/src/scenegraph/cameranode.h"
#include "../core/keyboardstate.h"
#include "../irisgl/src/graphics/renderlist.h"
#include "../irisgl/src/content/contentmanager.h"
#include "../commands/transfrormscenenodecommand.h"
#include "../uimanager.h"
#include "irisgl/extras/Materials.h"
#include <QtMath>
#include <QSharedPointer>
#include "hand.h"

class FresnelHandMaterial : public iris::Material
{

public:
	QColor color;
	float fresnelPow;

	FresnelHandMaterial()
	{
		auto shader = iris::Shader::load(
			":assets/shaders/default_material.vert",
			IrisUtils::getAbsoluteAssetPath("app/shaders/fresnelhand.frag"));
		this->setShader(shader);

		this->renderLayer = (int)iris::RenderLayer::Transparent;
		this->renderStates.blendState = iris::BlendState::createAlphaBlend();
		//this->renderStates.rasterState.depthBias = 1;// this ensures it's always on top
		//this->renderStates.rasterState.depthScaleBias = 1;// this ensures it's always on top
		this->renderStates.depthState = iris::DepthState(true, false);// no depth write

		this->enableFlag("SKINNING_ENABLED");

		color = QColor(41, 128, 185);
		fresnelPow = 5;
	}

	void begin(iris::GraphicsDevicePtr device, iris::ScenePtr scene) override
	{
		iris::Material::begin(device, scene);

		device->setShaderUniform("u_color", color);
		device->setShaderUniform("u_fresnelPow", fresnelPow);
		device->setShaderUniform("u_baseAlpha", 0.5f);
	}
};


void LeftHand::updateMovement(float dt)
{
	const float linearSpeed = 10.4f * dt;
	auto turnSpeed = 4.0f;
	hoverDist = 1000;

	auto vrDevice = iris::VrManager::getDefaultDevice();
	auto leftTouch = vrDevice->getTouchController(0);
	if (!leftTouch->isTracking())
		return;

	if (!viewer) {
		// lock rot to yaw so user is always right side up
		auto yaw = camera->getLocalRot().toEulerAngles().y();
		auto yawRot = QQuaternion::fromEulerAngles(0, yaw, 0);
		camera->setLocalRot(yawRot);

		// keyboard movement
		const QVector3D upVector(0, 1, 0);
		//not giving proper rotation when not in debug mode
		//apparently i need to normalize the head rotation quaternion
		auto rot = yawRot * vrDevice->getHeadRotation();
		rot.normalize();
		auto forwardVector = rot.rotatedVector(QVector3D(0, 0, -1));
		auto x = QVector3D::crossProduct(forwardVector, upVector).normalized();
		auto z = QVector3D::crossProduct(upVector, x).normalized();

		auto camPos = camera->getLocalPos();

		auto device = iris::VrManager::getDefaultDevice();

		auto dir = leftTouch->GetThumbstick();
		camPos += x * linearSpeed * dir.x() * 2;
		camPos += z * linearSpeed * dir.y() * 2;

		//qDebug()<<dir;
		dir.setY(-dir.y());
		auto newDir = rot.rotatedVector(QVector3D(dir.x(), 0, dir.y()));
		scene->getPhysicsEnvironment()->setDirection(QVector2D(newDir.x(), newDir.z()));
		//auto controller = scene->getPhysicsEnvironment()->getActiveCharacterController();
		//controller->setDirection(dir);


		if (leftTouch->isButtonDown(iris::VrTouchInput::Y))
			camPos += QVector3D(0, linearSpeed, 0);
		if (leftTouch->isButtonDown(iris::VrTouchInput::X))
			camPos += QVector3D(0, -linearSpeed, 0);

		camera->setLocalPos(camPos);
		//camera->setLocalRot(QQuaternion());
		camera->update(0);
	}
	else {
		// lock rot to yaw so user is always right side up
		auto yaw = viewer->getLocalRot().toEulerAngles().y();
		auto yawRot = QQuaternion::fromEulerAngles(0, yaw, 0);
		viewer->setLocalRot(yawRot);

		// keyboard movement
		const QVector3D upVector(0, 1, 0);
		//not giving proper rotation when not in debug mode
		//apparently i need to normalize the head rotation quaternion
		auto rot = yawRot * vrDevice->getHeadRotation();
		rot.normalize();
		auto forwardVector = rot.rotatedVector(QVector3D(0, 0, -1));
		auto x = QVector3D::crossProduct(forwardVector, upVector).normalized();
		auto z = QVector3D::crossProduct(upVector, x).normalized();

		auto camPos = viewer->getLocalPos();

		auto dir = leftTouch->GetThumbstick();
		camPos += x * linearSpeed * dir.x() * 2;
		camPos += z * linearSpeed * dir.y() * 2;

		//qDebug()<<dir;
		dir.setY(-dir.y());
		auto newDir = rot.rotatedVector(QVector3D(dir.x(), 0, dir.y())) * 7;
		scene->getPhysicsEnvironment()->setDirection(QVector2D(newDir.x(), newDir.z()));
		//auto controller = scene->getPhysicsEnvironment()->getActiveCharacterController();
		//controller->setDirection(dir);
	}
	
}

void LeftHand::update(float dt)
{
	auto device = iris::VrManager::getDefaultDevice();
	auto leftTouch = device->getTouchController(0);

	updateMovement(dt);
	if (leftTouch->isTracking()) {

		rightHandMatrix = calculateHandMatrix(device, 0);

		{
			// Handle picking and movement of picked objects
			iris::PickingResult pick;
			auto beamOffset = getBeamOffset(0);
			if (controller->rayCastToScene(rightHandMatrix * beamOffset, pick)) {
				auto dist = qSqrt(pick.distanceFromStartSqrd);
				hoverDist = dist;

				hoveredNode = controller->getObjectRoot(pick.hitNode);
				// Pick a node if the trigger is down
				if (leftTouch->getHandTrigger() > 0.1f && !grabbedNode)
				{
					grabbedNode = hoveredNode;
					rightPos = grabbedNode->getLocalPos();
					rightRot = grabbedNode->getLocalRot();
					rightScale = grabbedNode->getLocalScale();

					//calculate offset
					rightNodeOffset = rightHandMatrix.inverted() * grabbedNode->getGlobalTransform();

					auto handleNode = controller->findGrabNode(grabbedNode);
					if (!!handleNode) {
						//rightNodeOffset = grabbedNode->getGlobalTransform().inverted() * handleNode->getGlobalTransform();
						rightNodeOffset = handleNode->getGlobalTransform().inverted() * grabbedNode->getGlobalTransform();
					}

					// should accept hit point
					{

						if (grabbedNode->isPhysicsBody) {
							scene->getPhysicsEnvironment()->createPickingConstraint(
								iris::PickingHandleType::LeftHand,
								grabbedNode->getGUID(),
								iris::PhysicsHelper::btVector3FromQVector3D(grabbedNode->getGlobalPosition()),
								QVector3D(),
								QVector3D());
						}
					}
				}

			}
			else
			{
				//rightBeamRenderItem->worldMatrix.scale(1, 1, 100.f * (1.0f / 0.55f));
				hoveredNode.clear();
			}

			// trigger released
			if (leftTouch->getHandTrigger() < 0.1f && !!grabbedNode)
			{
				// release node
				grabbedNode->disablePhysicsTransform = false;
				grabbedNode.clear();
				rightNodeOffset.setToIdentity(); // why bother?

				scene->getPhysicsEnvironment()->cleanupPickingConstraint(iris::PickingHandleType::LeftHand);
			}

			// update picked node
			if (!!grabbedNode) {
				// disable physics visual update
				grabbedNode->disablePhysicsTransform = true;

				// calculate the global position
				auto nodeGlobal = rightHandMatrix * rightNodeOffset;

				// LAZLO's CODE
				if (grabbedNode->isPhysicsBody) {
					scene->getPhysicsEnvironment()->updatePickingConstraint(
						iris::PickingHandleType::LeftHand,
						nodeGlobal);
				}


				// calculate position relative to parent
				auto localTransform = grabbedNode->parent->getGlobalTransform().inverted() * nodeGlobal;

				QVector3D pos, scale;
				QQuaternion rot;
				// decompose matrix to assign pos, rot and scale
				iris::MathHelper::decomposeMatrix(localTransform,
					pos,
					rot,
					scale);

				grabbedNode->setLocalPos(pos);
				rot.normalize();
				grabbedNode->setLocalRot(rot);
				grabbedNode->setLocalScale(scale);

			}

		}

		submitItemsToScene();
	}
}

void LeftHand::loadAssets(iris::ContentManagerPtr content)
{
	//handModel = content->loadModel(IrisUtils::getAbsoluteAssetPath("app/models/LeftHand_anims.fbx"));
	handModel = content->loadModel(IrisUtils::getAbsoluteAssetPath("app/models/left_hand_anims.fbx"));
	auto handMat = QSharedPointer<FresnelHandMaterial>(new FresnelHandMaterial());
	handMaterial = handMat;

	// offset depth material
	auto mat = iris::DefaultMaterial::create();
	mat->enableFlag("SKINNING_ENABLED");
	// set renderstates to render depth only with polygon offset
	iris::RasterizerState raster;
	raster.cullMode = iris::CullMode::None;
	raster.depthBias = 1;
	raster.depthScaleBias = 1;
	mat->setRasterizerState(raster);

	iris::BlendState blend = iris::BlendState::createOpaque();
	//blend.colorMask = 0; // disable writing color
	mat->setBlendState(blend);
	handDepthMaterial = mat;

	beamMesh = content->loadMesh(
		IrisUtils::getAbsoluteAssetPath("app/content/models/beam.obj"));
	sphereMesh = content->loadMesh(
		IrisUtils::getAbsoluteAssetPath("app/content/primitives/sphere.obj"));
	auto colorMat = iris::ColorMaterial::create();
	colorMat->setColor(QColor(255, 100, 100));
	beamMaterial = colorMat;
}

void LeftHand::submitItemsToScene()
{
	QMatrix4x4 scale;
	scale.setToIdentity();

	scale.rotate(180, 0.0, 1.0, 0.0);
	scale.rotate(-90, 0.0, 0.0, 1.0);
	scale.scale(QVector3D(0.001f, 0.001f, 0.001f) * camera->getVrViewScale());
	scene->geometryRenderList->submitModel(handModel, handDepthMaterial, rightHandMatrix * scale);
	scene->geometryRenderList->submitModel(handModel, handMaterial, rightHandMatrix * scale);

#ifdef Q_DEBUG
	// beam
	if (!grabbedNode) {
		QMatrix4x4 beamMatrix = getBeamOffset(0);
		beamMatrix.scale(0.4f, 0.4f, hoverDist);
		scene->geometryRenderList->submitMesh(beamMesh, beamMaterial, rightHandMatrix * beamMatrix);

		QMatrix4x4 sphereMatrix = getBeamOffset(0);
		sphereMatrix.scale(0.02f);
		scene->geometryRenderList->submitMesh(sphereMesh, beamMaterial, rightHandMatrix * sphereMatrix);


		QMatrix4x4 tipMatrix = getBeamOffset(0);
		tipMatrix.translate(0.0, 0.0, -hoverDist);
		tipMatrix.scale(0.02f);
		scene->geometryRenderList->submitMesh(sphereMesh, beamMaterial, rightHandMatrix * tipMatrix);

	}
	else {

	}
#endif
}

QMatrix4x4 LeftHand::getBeamOffset(int handIndex)
{
	QMatrix4x4 beamMatrix;
	beamMatrix.setToIdentity();
	beamMatrix.translate(0.0, -0.08f, 0.0);
	beamMatrix.rotate(QQuaternion::fromEulerAngles(-15, -22, 0));
	return beamMatrix;
}

void RightHand::update(float dt)
{
	auto turnSpeed = 2.0f;
	hoverDist = 1000;
	// RIGHT CONTROLLER
	auto rightTouch = iris::VrManager::getDefaultDevice()->getTouchController(1);
	if (rightTouch->isTracking()) {

		auto device = iris::VrManager::getDefaultDevice();

		// rotate camera
		auto dir = rightTouch->GetThumbstick();
		auto yaw = camera->getLocalRot().toEulerAngles().y();
		yaw += -dir.x() * turnSpeed;
		auto yawRot = QQuaternion::fromEulerAngles(0, yaw, 0);
		camera->setLocalRot(yawRot);

		QMatrix4x4 world;
		world.setToIdentity();
		world.translate(device->getHandPosition(1));
		world.rotate(device->getHandRotation(1));
		//camera->setLocalScale(QVector3D(2,2,2));
		//rightHandMatrix = camera->getGlobalTransform() * world;
		rightHandMatrix = calculateHandMatrix(device, 1);

		{
			// Handle picking and movement of picked objects
			iris::PickingResult pick;
			/*
			QMatrix4x4 beamOffset;
			beamOffset.setToIdentity();
			beamOffset.rotate(QQuaternion::fromEulerAngles(-45, 0, 0));
			beamOffset.translate(0.0, -0.05f, 0.0);
			*/
			auto beamOffset = getBeamOffset(0);
			if (controller->rayCastToScene(rightHandMatrix * beamOffset, pick)) {
				auto dist = qSqrt(pick.distanceFromStartSqrd);
				hoverDist = dist;

				//qDebug() << "hit at dist: " << dist;
				//rightBeamRenderItem->worldMatrix.scale(1, 1, dist * (1.0f / 0.55f ));// todo: remove magic 0.55
				//rightBeamRenderItem->worldMatrix.scale(1, 1, dist);// todo: remove magic 0.55
				hoveredNode = controller->getObjectRoot(pick.hitNode);
				// Pick a node if the trigger is down
				if (rightTouch->getHandTrigger() > 0.1f && !grabbedNode)
				{
					grabbedNode = hoveredNode;
					rightPos = grabbedNode->getLocalPos();
					rightRot = grabbedNode->getLocalRot();
					rightScale = grabbedNode->getLocalScale();

					//calculate offset
					rightNodeOffset = rightHandMatrix.inverted() * grabbedNode->getGlobalTransform();

					auto handleNode = controller->findGrabNode(grabbedNode);
					if (!!handleNode) {
						//rightNodeOffset = grabbedNode->getGlobalTransform().inverted() * handleNode->getGlobalTransform();
						rightNodeOffset = handleNode->getGlobalTransform().inverted() * grabbedNode->getGlobalTransform();
					}

					// should accept hit point
					{

						if (grabbedNode->isPhysicsBody) {
							scene->getPhysicsEnvironment()->createPickingConstraint(
								iris::PickingHandleType::RightHand,
								grabbedNode->getGUID(),
								iris::PhysicsHelper::btVector3FromQVector3D(grabbedNode->getGlobalPosition()),
								QVector3D(),
								QVector3D());
						}
					}
				}

			}
			else
			{
				//rightBeamRenderItem->worldMatrix.scale(1, 1, 100.f * (1.0f / 0.55f));
				hoveredNode.clear();
			}

			// trigger released
			if (rightTouch->getHandTrigger() < 0.1f && !!grabbedNode)
			{
				// release node
				grabbedNode->disablePhysicsTransform = false;
				grabbedNode.clear();
				rightNodeOffset.setToIdentity(); // why bother?

				scene->getPhysicsEnvironment()->cleanupPickingConstraint(iris::PickingHandleType::RightHand);
			}

			// update picked node
			if (!!grabbedNode) {
				// disable physics visual update
				grabbedNode->disablePhysicsTransform = true;

				// calculate the global position
				auto nodeGlobal = rightHandMatrix * rightNodeOffset;

				// LAZLO's CODE
				if (grabbedNode->isPhysicsBody) {
					scene->getPhysicsEnvironment()->updatePickingConstraint(iris::PickingHandleType::RightHand, nodeGlobal);
				}
				//else {
					

					// calculate position relative to parent
					auto localTransform = grabbedNode->parent->getGlobalTransform().inverted() * nodeGlobal;

					QVector3D pos, scale;
					QQuaternion rot;
					// decompose matrix to assign pos, rot and scale
					iris::MathHelper::decomposeMatrix(localTransform,
						pos,
						rot,
						scale);

					grabbedNode->setLocalPos(pos);
					rot.normalize();
					grabbedNode->setLocalRot(rot);
					grabbedNode->setLocalScale(scale);

				//}
			}

			//scene->geometryRenderList->add(rightBeamRenderItem);
			//scene->geometryRenderList->add(rightHandRenderItem);
			//scene->geometryRenderList->submitMesh(handMesh, handMaterial, rightHandMatrix);
		}

		submitItemsToScene();
	}
}

void RightHand::loadAssets(iris::ContentManagerPtr content)
{
	handModel = content->loadModel(IrisUtils::getAbsoluteAssetPath("app/models/right_hand_anims.fbx"));
	auto handMat = QSharedPointer<FresnelHandMaterial>(new FresnelHandMaterial());
	handMaterial = handMat;

	// offset depth material
	auto mat = iris::DefaultMaterial::create();
	mat->enableFlag("SKINNING_ENABLED");
	// set renderstates to render depth only with polygon offset
	iris::RasterizerState raster;
	raster.cullMode = iris::CullMode::None;
	raster.depthBias = 1;
	raster.depthScaleBias = 1;
	mat->setRasterizerState(raster);

	iris::BlendState blend = iris::BlendState::createOpaque();
	//blend.colorMask = 0; // disable writing color
	mat->setBlendState(blend);
	handDepthMaterial = mat;


	beamMesh = content->loadMesh(
		IrisUtils::getAbsoluteAssetPath("app/content/models/beam.obj"));
	sphereMesh = content->loadMesh(
		IrisUtils::getAbsoluteAssetPath("app/content/primitives/sphere.obj"));
	auto colorMat = iris::ColorMaterial::create();
	colorMat->setColor(QColor(255, 100, 100));
	beamMaterial = colorMat;
}

void RightHand::submitItemsToScene()
{
	handModel->setActiveAnimation("RightHand_rig.001|handFistMove");
	auto rightTouch = iris::VrManager::getDefaultDevice()->getTouchController(1);
	handModel->applyAnimation(rightTouch->getHandTrigger() * 24);

	QMatrix4x4 scale;
	scale.setToIdentity();
	
	scale.rotate(180, 0.0, 1.0, 0.0);
	scale.rotate(90, 0.0, 0.0, 1.0);
	scale.scale(QVector3D(0.1f, 0.1f, 0.1f) * camera->getVrViewScale());
	scene->geometryRenderList->submitModel(handModel, handDepthMaterial, rightHandMatrix * scale);
	scene->geometryRenderList->submitModel(handModel, handMaterial, rightHandMatrix * scale);

#ifdef Q_DEBUG
	// beam
	if (!grabbedNode) {
		QMatrix4x4 beamMatrix = getBeamOffset(0);
		beamMatrix.scale(0.4f, 0.4f, hoverDist);
		scene->geometryRenderList->submitMesh(beamMesh, beamMaterial, rightHandMatrix * beamMatrix);

		QMatrix4x4 sphereMatrix = getBeamOffset(0);
		sphereMatrix.scale(0.02f);
		scene->geometryRenderList->submitMesh(sphereMesh, beamMaterial, rightHandMatrix * sphereMatrix);


		QMatrix4x4 tipMatrix = getBeamOffset(0);
		tipMatrix.translate(0.0, 0.0, -hoverDist);
		tipMatrix.scale(0.02f);
		scene->geometryRenderList->submitMesh(sphereMesh, beamMaterial, rightHandMatrix * tipMatrix);

	}
	else {

	}
#endif
}

QMatrix4x4 RightHand::getBeamOffset(int handIndex)
{
	QMatrix4x4 beamMatrix;
	beamMatrix.setToIdentity();
	beamMatrix.translate(0.0, -0.08f, 0.0);
	beamMatrix.rotate(QQuaternion::fromEulerAngles(-15, 22, 0));
	return beamMatrix;
}

void Hand::init(iris::ScenePtr scene, iris::CameraNodePtr cam, iris::ViewerNodePtr viewer)
{
	this->scene = scene;
	this->camera = cam;
	this->viewer = viewer;
}

void Hand::setCamera(iris::CameraNodePtr camera)
{
	this->camera = camera;
}

QMatrix4x4 Hand::calculateHandMatrix(iris::VrDevice* device, int handIndex)
{
	if (!!viewer) {
		// cameras arent parented to anything
		// so local transform == global transform
		auto trans = viewer->getLocalTransform();
		trans.scale(viewer->getViewScale());
		auto globalPos = trans * device->getHandPosition(handIndex);
		auto globalRot = viewer->getLocalRot() * device->getHandRotation(handIndex);

		QMatrix4x4 world;
		world.setToIdentity();
		world.translate(globalPos);
		world.rotate(globalRot);
		return world;
	}
	else {
		// cameras arent parented to anything
		// so local transform == global transform
		auto trans = camera->getLocalTransform();
		trans.scale(camera->getVrViewScale());
		auto globalPos = trans * device->getHandPosition(handIndex);
		auto globalRot = camera->getLocalRot() * device->getHandRotation(handIndex);

		QMatrix4x4 world;
		world.setToIdentity();
		world.translate(globalPos);
		world.rotate(globalRot);
		return world;
	}
	
}