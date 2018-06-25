/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "editorvrcontroller.h"
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

		color = QColor(255, 0, 0); 
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


EditorVrController::EditorVrController(iris::ContentManagerPtr content)
{
	this->content = content;

    //auto cube = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/primitives/cube.obj"));
    auto leftHandMesh = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/models/external_controller01_left.obj"));

	auto mat = iris::DefaultMaterial::create();

    mat->setDiffuseTexture(
                iris::Texture2D::load(
                    IrisUtils::getAbsoluteAssetPath("app/content/models/external_controller01_col.png")));

    leftHandRenderItem = new iris::RenderItem();
    leftHandRenderItem->type = iris::RenderItemType::Mesh;
    leftHandRenderItem->material = mat;
    leftHandRenderItem->mesh = leftHandMesh;

    auto rightHandMesh = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/models/external_controller01_right.obj"));
    rightHandRenderItem = new iris::RenderItem();
    rightHandRenderItem->type = iris::RenderItemType::Mesh;
    rightHandRenderItem->material = mat;
    rightHandRenderItem->mesh = rightHandMesh;

    beamMesh = iris::Mesh::loadMesh(
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
}

void EditorVrController::setScene(iris::ScenePtr scene)
{
    this->scene = scene;
}

void EditorVrController::update(float dt)
{
    vrDevice = iris::VrManager::getDefaultDevice();
    const float linearSpeed = 10.4f * dt;

    // keyboard movement
    const QVector3D upVector(0, 1, 0);
    //not giving proper rotation when not in debug mode
    //apparently i need to normalize the head rotation quaternion
    auto rot = vrDevice->getHeadRotation();
    rot.normalize();
    auto forwardVector = rot.rotatedVector(QVector3D(0, 0, -1));
    auto x = QVector3D::crossProduct(forwardVector,upVector).normalized();
    auto z = QVector3D::crossProduct(upVector,x).normalized();

    auto camPos = camera->getLocalPos();
    // left
    if(KeyboardState::isKeyDown(Qt::Key_Left))
        camPos -= x * linearSpeed;

    // right
    if(KeyboardState::isKeyDown(Qt::Key_Right))
        camPos += x * linearSpeed;

    // up
    if(KeyboardState::isKeyDown(Qt::Key_Up))
        camPos += z * linearSpeed;

    // down
    if(KeyboardState::isKeyDown(Qt::Key_Down))
        camPos -= z * linearSpeed;

    camera->setLocalPos(camPos);

    // touch controls

	// LEFT CONTROLLER
    auto leftTouch = vrDevice->getTouchController(0);
    if (leftTouch->isTracking()) {
        auto dir = leftTouch->GetThumbstick();
        camPos += x * linearSpeed * dir.x() * 2;
        camPos += z * linearSpeed * dir.y() * 2;


        if(leftTouch->isButtonDown(iris::VrTouchInput::Y))
            camPos += QVector3D(0, linearSpeed, 0);
        if(leftTouch->isButtonDown(iris::VrTouchInput::X))
            camPos += QVector3D(0, -linearSpeed, 0);

        camera->setLocalPos(camPos);
        camera->setLocalRot(QQuaternion());
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


        world.setToIdentity();
        world.translate(device->getHandPosition(1));
        world.rotate(device->getHandRotation(1));
        //world.scale(0.55f);
        rightHandRenderItem->worldMatrix = camera->globalTransform * world;
        rightBeamRenderItem->worldMatrix = rightHandRenderItem->worldMatrix;


        // Handle picking and movement of picked objects
        iris::PickingResult pick;
        if (rayCastToScene(leftHandRenderItem->worldMatrix, pick)) {
            auto dist = qSqrt(pick.distanceFromStartSqrd);
            //qDebug() << "hit at dist: " << dist;
            leftBeamRenderItem->worldMatrix.scale(1, 1, dist /* * (1.0f / 0.55f )*/);// todo: remove magic 0.55
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
                leftNodeOffset =  leftHandRenderItem->worldMatrix.inverted() * leftPickedNode->getGlobalTransform();
            }

        }
        else
        {
            leftBeamRenderItem->worldMatrix.scale(1, 1, 100.f * (1.0f / 0.55f ));
			leftHoveredNode.clear();
        }

        if(leftTouch->getIndexTrigger() < 0.1f && !!leftPickedNode)
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
            rightBeamRenderItem->worldMatrix.scale(1, 1,(dist /*  * (1.0f / 0.55f )*/));// todo: remove magic 0.55
        }

        scene->geometryRenderList->add(leftHandRenderItem);
        scene->geometryRenderList->add(leftBeamRenderItem);
    }


	// RIGHT CONTROLLER
	auto rightTouch = vrDevice->getTouchController(1);
	if (rightTouch->isTracking()) {
		// Submit items to renderer
		auto device = iris::VrManager::getDefaultDevice();

		QMatrix4x4 world;
		world.setToIdentity();
		world.translate(device->getHandPosition(1));
		world.rotate(device->getHandRotation(1));
		//world.scale(0.55f);
		rightHandRenderItem->worldMatrix = camera->globalTransform * world;
		rightBeamRenderItem->worldMatrix = rightHandRenderItem->worldMatrix;


		world.setToIdentity();
		world.translate(device->getHandPosition(1));
		world.rotate(device->getHandRotation(1));
		//world.scale(0.55f);
		rightHandRenderItem->worldMatrix = camera->globalTransform * world;
		rightBeamRenderItem->worldMatrix = rightHandRenderItem->worldMatrix;


		// Handle picking and movement of picked objects
		iris::PickingResult pick;
		if (rayCastToScene(rightHandRenderItem->worldMatrix, pick)) {
			auto dist = qSqrt(pick.distanceFromStartSqrd);
			//qDebug() << "hit at dist: " << dist;
			rightBeamRenderItem->worldMatrix.scale(1, 1, dist /* * (1.0f / 0.55f )*/);// todo: remove magic 0.55
			rightHoveredNode = getObjectRoot(pick.hitNode);
																					 // Pick a node if the trigger is down
			if (rightTouch->getIndexTrigger() > 0.1f && !rightPickedNode)
			{
				rightPickedNode = rightHoveredNode;
				rightPos = rightPickedNode->getLocalPos();
				rightRot = rightPickedNode->getLocalRot();
				rightScale = rightPickedNode->getLocalScale();

				//calculate offset
				//leftNodeOffset = leftPickedNode->getGlobalTransform() * leftHandRenderItem->worldMatrix.inverted();
				rightNodeOffset = rightHandRenderItem->worldMatrix.inverted() * rightPickedNode->getGlobalTransform();
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
		}

		// update picked node
		if (!!rightPickedNode) {
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
		/*
		if (rayCastToScene(rightBeamRenderItem->worldMatrix, pick)) {
			auto dist = qSqrt(pick.distanceFromStartSqrd);
			rightBeamRenderItem->worldMatrix.scale(1, 1, (dist);// todo: remove magic 0.55
		}
		*/

		scene->geometryRenderList->add(rightBeamRenderItem);
		scene->geometryRenderList->add(rightHandRenderItem);
	}
	/*
    auto rightTouch = vrDevice->getTouchController(1);
    if (rightTouch->isTracking()) {
        scene->geometryRenderList->add(leftBeamRenderItem);
        scene->geometryRenderList->add(rightBeamRenderItem);
    }
	*/

	submitHoveredNodes();
}

bool EditorVrController::rayCastToScene(QMatrix4x4 handMatrix, iris::PickingResult& result)
{
    QList<iris::PickingResult> hits;

    scene->rayCast(handMatrix * QVector3D(0,0,0),
                   handMatrix * QVector3D(0,0,-100),
                   hits);

    if(hits.size() == 0)
        return false;

    qSort(hits.begin(), hits.end(), [](const iris::PickingResult& a, const iris::PickingResult& b){
        return a.distanceFromStartSqrd < b.distanceFromStartSqrd;
    });

    result = hits.first();
    return true;
}

iris::SceneNodePtr EditorVrController::getObjectRoot(iris::SceneNodePtr node)
{
	if (node->isAttached() && scene->getRootNode() != node->getParent())
		return getObjectRoot(node->getParent());

	return node;
}

void EditorVrController::submitHoveredNodes()
{
	if (!rightPickedNode && !!rightHoveredNode) {
		submitHoveredNode(rightHoveredNode);
	}

	if (!leftPickedNode && !!leftHoveredNode) {
		submitHoveredNode(leftHoveredNode);
	}
}

void EditorVrController::submitHoveredNode(iris::SceneNodePtr node)
{
	if (node->sceneNodeType == iris::SceneNodeType::Mesh) {
		auto mesh = node.staticCast<iris::MeshNode>()->getMesh();

		scene->geometryRenderList->submitMesh(mesh, fresnelMat, node->getGlobalTransform());
	}

	for (auto child : node->children) {
		submitHoveredNode(child);
	}
}

iris::GrabNodePtr EditorVrController::findGrabNode(iris::SceneNodePtr node)
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
