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


class FresnelMaterial : public iris::Material
{
	
public:
	QColor color;
	float fresnelPow;

	FresnelMaterial()
	{
		auto shader = iris::Shader::load(
			IrisUtils::getAbsoluteAssetPath("app/shaders/fresnel.vert"),
			IrisUtils::getAbsoluteAssetPath("app/shaders/fresnel.frag"));
		this->setShader(shader);

		this->renderLayer = (int)iris::RenderLayer::Transparent;
		this->renderStates.blendState = iris::BlendState::createAlphaBlend();
		//this->renderStates.rasterState.depthBias = 1;// this ensures it's always on top
		//this->renderStates.rasterState.depthScaleBias = 1;// this ensures it's always on top

		color = QColor(41, 128, 185); 
		fresnelPow = 5;
	}

	void begin(iris::GraphicsDevicePtr device, iris::ScenePtr scene) override
	{
		iris::Material::begin(device, scene);
		///device->setBlendState(this->renderStates.blendState);

		device->setShaderUniform("u_color", color);
		device->setShaderUniform("u_fresnelPow", fresnelPow);
	}
};


PlayerVrController::PlayerVrController()
{
	leftHand = new LeftHand(this);
	rightHand = new RightHand(this);
}

void PlayerVrController::setCamera(iris::CameraNodePtr cam)
{
	this->camera = cam;
	//camera->setLocalScale(QVector3D(2, 2, 2));
	leftHand->setCamera(cam);
	rightHand->setCamera(cam);
}

void PlayerVrController::loadAssets(iris::ContentManagerPtr content)
{
	this->content = content;

	//auto cube = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/primitives/cube.obj"));
	auto leftHandMesh = content->loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/models/external_controller01_left.obj"));

	auto mat = iris::DefaultMaterial::create();

	mat->setDiffuseTexture(
		content->loadTexture(
			IrisUtils::getAbsoluteAssetPath("app/content/models/external_controller01_col.png")));

	leftHandRenderItem = new iris::RenderItem();
	leftHandRenderItem->type = iris::RenderItemType::Mesh;
	leftHandRenderItem->material = mat;
	leftHandRenderItem->mesh = leftHandMesh;

	auto rightHandMesh = content->loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/models/external_controller01_right.obj"));
	rightHandRenderItem = new iris::RenderItem();
	rightHandRenderItem->type = iris::RenderItemType::Mesh;
	rightHandRenderItem->material = mat;
	rightHandRenderItem->mesh = rightHandMesh;

	beamMesh = content->loadMesh(
		IrisUtils::getAbsoluteAssetPath("app/content/models/beam.obj"));
	auto beamMat = iris::ColorMaterial::create();
	beamMat->setColor(QColor(255, 100, 100));
	//beamMat->setDiffuseColor(QColor(255,100,100));

	leftBeamRenderItem = new iris::RenderItem();
	leftBeamRenderItem->type = iris::RenderItemType::Mesh;
	leftBeamRenderItem->material = beamMat;
	leftBeamRenderItem->mesh = beamMesh;

	rightBeamRenderItem = new iris::RenderItem();
	rightBeamRenderItem->type = iris::RenderItemType::Mesh;
	rightBeamRenderItem->material = beamMat;
	rightBeamRenderItem->mesh = beamMesh;

	// load models
	leftHandModel = content->loadModel("app/content/models/left_hand.dae");
	rightHandModel = content->loadModel("app/content/models/right_hand.dae");

	fresnelMat = QSharedPointer<FresnelMaterial>(new FresnelMaterial());

	turnSpeed = 4.0f;

	leftHand->loadAssets(content);
	rightHand->loadAssets(content);
}

void PlayerVrController::setScene(iris::ScenePtr scene)
{
    this->scene = scene;
	auto activeViewer = scene->getActiveVrViewer();
	this->leftHand->init(scene, scene->camera, activeViewer);
	this->rightHand->init(scene, scene->camera, activeViewer);
}

void PlayerVrController::start()
{
	auto activeViewer = scene->getActiveVrViewer();
	this->leftHand->init(scene, scene->camera, activeViewer);
	this->rightHand->init(scene, scene->camera, activeViewer);

	if (!!activeViewer) {
		// plant viewer to any surface below it
		auto rayStart = activeViewer->getGlobalPosition();
		auto rayEnd = rayStart + QVector3D(0, -1000, 0);
		QList<iris::PickingResult> results;
		scene->rayCast(rayStart, rayEnd, results, 0, true);

		// closest point
		QVector3D closestPoint = rayEnd;
		float closestDist = 1000;
		if (results.size() > 0) {
			// find closest one
			for (const auto result : results) {
				auto dist = result.hitPoint.distanceToPoint(closestPoint);
				if (dist < closestDist)
				{
					closestDist = dist;
					closestPoint = result.hitPoint;
				}
			}

			// todo: should limit snapping distance?
			//5.75
			//activeViewer->setGlobalPos(closestPoint + QVector3D(0, 1.73736, 0));
			activeViewer->setGlobalPos(closestPoint + QVector3D(0, 5.75f * 0.5f, 0));

		}
	}
}

void PlayerVrController::update(float dt)
{
	if (!_isPlaying)
		return;

	vrDevice = iris::VrManager::getDefaultDevice();
    const float linearSpeed = 10.4f * dt;

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
    auto x = QVector3D::crossProduct(forwardVector,upVector).normalized();
    auto z = QVector3D::crossProduct(upVector,x).normalized();

    auto camPos = camera->getLocalPos();

	/*
	// LEFT CONTROLLER
    auto leftTouch = vrDevice->getTouchController(0);
    if (leftTouch->isTracking()) {

		auto dir = leftTouch->GetThumbstick();
		camPos += x * linearSpeed * dir.x() * 2;
		camPos += z * linearSpeed * dir.y() * 2;


		if (leftTouch->isButtonDown(iris::VrTouchInput::Y))
			camPos += QVector3D(0, linearSpeed, 0);
		if (leftTouch->isButtonDown(iris::VrTouchInput::X))
			camPos += QVector3D(0, -linearSpeed, 0);

		camera->setLocalPos(camPos);
		//camera->setLocalRot(QQuaternion());
		camera->update(0);


		// Submit items to renderer
		auto device = iris::VrManager::getDefaultDevice();

		QMatrix4x4 world;
		world.setToIdentity();
		world.translate(device->getHandPosition(0));
		world.rotate(device->getHandRotation(0));
		//world.scale(0.55f);
		leftHandRenderItem->worldMatrix = camera->globalTransform * world;
		leftBeamRenderItem->worldMatrix = leftHandRenderItem->worldMatrix;


		if (UiManager::sceneMode == SceneMode::EditMode) {

			// Handle picking and movement of picked objects
			iris::PickingResult pick;
			if (rayCastToScene(leftHandRenderItem->worldMatrix, pick)) {
				auto dist = qSqrt(pick.distanceFromStartSqrd);
				//qDebug() << "hit at dist: " << dist;
				leftBeamRenderItem->worldMatrix.scale(1, 1, dist);// todo: remove magic 0.55
				leftHoveredNode = getObjectRoot(pick.hitNode);

				// Pick a node if the trigger is down
				if (leftTouch->getIndexTrigger() > 0.1f && !leftPickedNode)
				{
					leftPickedNode = leftHoveredNode;
					leftPos = leftPickedNode->getLocalPos();
					leftRot = leftPickedNode->getLocalRot();
					leftScale = leftPickedNode->getLocalScale();

					//calculate offset
					//leftNodeOffset = leftPickedNode->getGlobalTransform() * leftHandRenderItem->worldMatrix.inverted();
					leftNodeOffset = leftHandRenderItem->worldMatrix.inverted() * leftPickedNode->getGlobalTransform();
				}

			}
			else
			{
				leftBeamRenderItem->worldMatrix.scale(1, 1, 100.f * (1.0f / 0.55f));
				leftHoveredNode.clear();
			}

			if (leftTouch->getIndexTrigger() < 0.1f && !!leftPickedNode)
			{
				// add to undo
				auto newPos = leftPickedNode->getLocalPos();
				auto cmd = new TransformSceneNodeCommand(leftPickedNode,
					leftPos, leftRot, leftScale,
					leftPickedNode->getLocalPos(), leftPickedNode->getLocalRot(), leftPickedNode->getLocalScale());
				UiManager::pushUndoStack(cmd);

				// release node
				leftPickedNode.clear();
				leftNodeOffset.setToIdentity(); // why bother?
			}

			// update picked node
			if (!!leftPickedNode) {
				// calculate the global position
				auto nodeGlobal = leftHandRenderItem->worldMatrix * leftNodeOffset;

				// calculate position relative to parent
				auto localTransform = leftPickedNode->parent->getGlobalTransform().inverted() * nodeGlobal;

				QVector3D pos, scale;
				QQuaternion rot;
				// decompose matrix to assign pos, rot and scale
				iris::MathHelper::decomposeMatrix(localTransform,
					pos,
					rot,
					scale);

				leftPickedNode->setLocalPos(pos);
				rot.normalize();
				leftPickedNode->setLocalRot(rot);
				leftPickedNode->setLocalScale(scale);


				// @todo: force recalculatioin of global transform
				// leftPickedNode->update(0);// bad!
				// it wil be updated a frame later, no need to stress over this
			}

			if (rayCastToScene(rightBeamRenderItem->worldMatrix, pick)) {
				auto dist = qSqrt(pick.distanceFromStartSqrd);
				rightBeamRenderItem->worldMatrix.scale(1, 1, (dist ));// todo: remove magic 0.55
			}

			scene->geometryRenderList->add(leftHandRenderItem);
			scene->geometryRenderList->add(leftBeamRenderItem);
		}
		else
		{
			// submit only the right hand in render mode
			scene->geometryRenderList->add(leftHandRenderItem);
		}
    }
	*/

	leftHand->update(dt);
	rightHand->update(dt);
	/*
	// RIGHT CONTROLLER
	auto rightTouch = vrDevice->getTouchController(1);
	if (rightTouch->isTracking()) {

		// Submit items to renderer
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
		//world.scale(0.55f);
		rightHandRenderItem->worldMatrix = camera->globalTransform * world;
		rightBeamRenderItem->worldMatrix = rightHandRenderItem->worldMatrix;

		//if (UiManager::sceneMode == SceneMode::EditMode)
		{
			// Handle picking and movement of picked objects
			iris::PickingResult pick;
			if (rayCastToScene(rightHandRenderItem->worldMatrix, pick)) {
				auto dist = qSqrt(pick.distanceFromStartSqrd);
				//qDebug() << "hit at dist: " << dist;
				rightBeamRenderItem->worldMatrix.scale(1, 1, dist);// todo: remove magic 0.55
				rightBeamRenderItem->worldMatrix.scale(1, 1, dist * (1.0f / 0.55f ));// todo: remove magic 0.55
				rightHoveredNode = getObjectRoot(pick.hitNode);
				// Pick a node if the trigger is down
				if (rightTouch->getIndexTrigger() > 0.1f && !rightPickedNode)
				{
					rightPickedNode = rightHoveredNode;
					rightPos = rightPickedNode->getLocalPos();
					rightRot = rightPickedNode->getLocalRot();
					rightScale = rightPickedNode->getLocalScale();

					//calculate offset
					rightNodeOffset = rightHandRenderItem->worldMatrix.inverted() * rightPickedNode->getGlobalTransform();

					// should accept hit point
					{
						auto pickedNode = rightPickedNode;

						if (pickedNode->isPhysicsBody && UiManager::isSimulationRunning) {
							// Fetch our rigid body from the list stored in the world by guid
							activeRigidBody = scene->getPhysicsEnvironment()->hashBodies.value(pickedNode->getGUID());
							// prevent the picked object from falling asleep
							activeRigidBody->setActivationState(DISABLE_DEACTIVATION);
							// get the hit position relative to the body we hit 
							// constraints MUST be defined in local space coords
							btVector3 localPivot = activeRigidBody->getCenterOfMassTransform().inverse()
								* iris::PhysicsHelper::btVector3FromQVector3D(pick.hitPoint);

							// create a transform for the pivot point
							btTransform pivot;
							pivot.setIdentity();
							pivot.setOrigin(localPivot);

							// create our constraint object
							auto dof6 = new btGeneric6DofConstraint(*activeRigidBody, pivot, true);
							bool bLimitAngularMotion = true;
							if (bLimitAngularMotion) {
								dof6->setAngularLowerLimit(btVector3(0, 0, 0));
								dof6->setAngularUpperLimit(btVector3(0, 0, 0));
							}

							// add the constraint to the world
							scene->getPhysicsEnvironment()->addConstraintToWorld(dof6, false);

							// store a pointer to our constraint
							m_pickedConstraint = dof6;

							// define the 'strength' of our constraint (each axis)
							float cfm = 0.9f;
							dof6->setParam(BT_CONSTRAINT_STOP_CFM, cfm, 0);
							dof6->setParam(BT_CONSTRAINT_STOP_CFM, cfm, 1);
							dof6->setParam(BT_CONSTRAINT_STOP_CFM, cfm, 2);
							dof6->setParam(BT_CONSTRAINT_STOP_CFM, cfm, 3);
							dof6->setParam(BT_CONSTRAINT_STOP_CFM, cfm, 4);
							dof6->setParam(BT_CONSTRAINT_STOP_CFM, cfm, 5);

							// define the 'error reduction' of our constraint (each axis)
							float erp = 0.5f;
							dof6->setParam(BT_CONSTRAINT_STOP_ERP, erp, 0);
							dof6->setParam(BT_CONSTRAINT_STOP_ERP, erp, 1);
							dof6->setParam(BT_CONSTRAINT_STOP_ERP, erp, 2);
							dof6->setParam(BT_CONSTRAINT_STOP_ERP, erp, 3);
							dof6->setParam(BT_CONSTRAINT_STOP_ERP, erp, 4);
							dof6->setParam(BT_CONSTRAINT_STOP_ERP, erp, 5);
						}

						// save this data for future reference
						//btVector3 rayFromWorld = iris::PhysicsHelper::btVector3FromQVector3D(editorCam->getGlobalPosition());
						//btVector3 rayToWorld = iris::PhysicsHelper::btVector3FromQVector3D(calculateMouseRay(point) * 1024);

						auto segStart = rightHandRenderItem->worldMatrix * QVector3D(0, 0, 0);
						auto segEnd = rightHandRenderItem->worldMatrix * QVector3D(0, 0, -100);
						btVector3 rayFromWorld = iris::PhysicsHelper::btVector3FromQVector3D(segStart);
						btVector3 rayToWorld = iris::PhysicsHelper::btVector3FromQVector3D(segEnd);

						m_oldPickingPos = rayToWorld;
						m_hitPos = iris::PhysicsHelper::btVector3FromQVector3D(pick.hitPoint);
						m_oldPickingDist = (iris::PhysicsHelper::btVector3FromQVector3D(pick.hitPoint) - rayFromWorld).length();

						rightHandPickOffset = rightHandRenderItem->worldMatrix.inverted() * pick.hitPoint;
					}
				}

			}
			else
			{
				rightBeamRenderItem->worldMatrix.scale(1, 1, 100.f * (1.0f / 0.55f));
				rightHoveredNode.clear();
			}

			// trigger released
			if (rightTouch->getIndexTrigger() < 0.1f && !!rightPickedNode)
			{
				// add to undo
				auto newPos = rightPickedNode->getLocalPos();
				auto cmd = new TransformSceneNodeCommand(rightPickedNode,
					leftPos, leftRot, leftScale,
					rightPickedNode->getLocalPos(), rightPickedNode->getLocalRot(), rightPickedNode->getLocalScale());
				UiManager::pushUndoStack(cmd);

				// release node
				rightPickedNode.clear();
				rightNodeOffset.setToIdentity(); // why bother?

				// clear physics
				if (m_pickedConstraint) {
					activeRigidBody->forceActivationState(m_savedState);
					activeRigidBody->activate();
					scene->getPhysicsEnvironment()->removeConstraintFromWorld(m_pickedConstraint);
					delete m_pickedConstraint;
					m_pickedConstraint = 0;
					activeRigidBody = 0;
				}
			}

			// update picked node
			if (!!rightPickedNode) {

				// LAZLO's CODE
				if (rightPickedNode->isPhysicsBody) {
					if (activeRigidBody && m_pickedConstraint) {
						btGeneric6DofConstraint* pickCon = static_cast<btGeneric6DofConstraint*>(m_pickedConstraint);
						if (pickCon)
						{
							QVector3D newPivotVec = rightHandRenderItem->worldMatrix * rightHandPickOffset;
							btVector3 newPivot = iris::PhysicsHelper::btVector3FromQVector3D(newPivotVec);
							pickCon->getFrameOffsetA().setOrigin(newPivot);
						}
					}
				}
				else {
					// calculate the global position
					auto nodeGlobal = rightHandRenderItem->worldMatrix * rightNodeOffset;

					// calculate position relative to parent
					auto localTransform = rightPickedNode->parent->getGlobalTransform().inverted() * nodeGlobal;

					QVector3D pos, scale;
					QQuaternion rot;
					// decompose matrix to assign pos, rot and scale
					iris::MathHelper::decomposeMatrix(localTransform,
						pos,
						rot,
						scale);

					rightPickedNode->setLocalPos(pos);
					rot.normalize();
					rightPickedNode->setLocalRot(rot);
					rightPickedNode->setLocalScale(scale);


					// @todo: force recalculatioin of global transform
					// leftPickedNode->update(0);// bad!
					// it wil be updated a frame later, no need to stress over this
				}
			}

			scene->geometryRenderList->add(rightBeamRenderItem);
			scene->geometryRenderList->add(rightHandRenderItem);
		}

	}
	*/
	submitHoveredNodes();
}

void PlayerVrController::postUpdate(float dt)
{

}

bool PlayerVrController::rayCastToScene(QMatrix4x4 handMatrix, iris::PickingResult& result)
{
    QList<iris::PickingResult> hits;

    scene->rayCast(handMatrix * QVector3D(0,0,0),
                   handMatrix * QVector3D(0,0,-100),
                   hits);

    if(hits.size() == 0)
        return false;

    std::sort(hits.begin(), hits.end(), [](const iris::PickingResult& a, const iris::PickingResult& b){
        return a.distanceFromStartSqrd < b.distanceFromStartSqrd;
    });

    result = hits.first();
    return true;
}

iris::SceneNodePtr PlayerVrController::getObjectRoot(iris::SceneNodePtr node)
{
	if (node->isAttached() && scene->getRootNode() != node->getParent())
		return getObjectRoot(node->getParent());

	return node;
}

void PlayerVrController::submitHoveredNodes()
{
	// only do this in edit mode
	if (UiManager::sceneMode == SceneMode::EditMode) {
		if (!rightPickedNode && !!rightHoveredNode) {
			submitHoveredNode(rightHoveredNode);
		}

		if (!leftPickedNode && !!leftHoveredNode) {
			submitHoveredNode(leftHoveredNode);
		}
	}
}

void PlayerVrController::submitHoveredNode(iris::SceneNodePtr node)
{
	if (node->sceneNodeType == iris::SceneNodeType::Mesh) {
		auto mesh = node.staticCast<iris::MeshNode>()->getMesh();

		scene->geometryRenderList->submitMesh(mesh, fresnelMat, node->getGlobalTransform());
	}

	for (auto child : node->children) {
		submitHoveredNode(child);
	}
}

iris::GrabNodePtr PlayerVrController::findGrabNode(iris::SceneNodePtr node)
{
	if (node->sceneNodeType == iris::SceneNodeType::Grab)
		return node.staticCast<iris::GrabNode>();

	for (auto child : node->children) {
		auto result = findGrabNode(child);
		if (result)
			return result;
	}

	// none found
	return iris::GrabNodePtr();
}
