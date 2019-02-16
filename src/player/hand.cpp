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
#include "hand.h"

RightHand::RightHand()
{
}

void RightHand::update(float dt)
{/*
	// RIGHT CONTROLLER
	auto rightTouch = iris::VrManager::getDefaultDevice()->getTouchController(1);
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
				//rightBeamRenderItem->worldMatrix.scale(1, 1, dist * (1.0f / 0.55f ));// todo: remove magic 0.55
				rightBeamRenderItem->worldMatrix.scale(1, 1, dist);// todo: remove magic 0.55
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

				}
			}

			scene->geometryRenderList->add(rightBeamRenderItem);
			scene->geometryRenderList->add(rightHandRenderItem);
			scene->geometryRenderlist->submitItem(handMesh, handMaterial);
		}

	}
	*/
}

void RightHand::loadAssets()
{
}
