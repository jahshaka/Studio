/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "physicspropertywidget.h"

#include <QStandardItemModel>

#include "irisgl/src/scenegraph/meshnode.h"
#include "irisgl/src/scenegraph/scene.h"
#include "irisgl/src/physics/environment.h"
#include "../sceneviewwidget.h"
#include "globals.h"
#include "../checkboxwidget.h"
#include "../hfloatsliderwidget.h"
#include "../comboboxwidget.h"

#include "irisgl/src/bullet3/src/btBulletDynamicsCommon.h"
#include "bullet3/src/BulletCollision/CollisionShapes/btConvexHullShape.h"
#include "bullet3/src/BulletCollision/CollisionShapes/btShapeHull.h"
#include "physics/physicshelper.h"
#include "irisgl/src/scenegraph/scenenode.h"

using namespace iris;

PhysicsPropertyWidget::PhysicsPropertyWidget()
{
    physicsTypes.insert(static_cast<int>(PhysicsType::None), "None");
    physicsTypes.insert(static_cast<int>(PhysicsType::Static), "Static");
    physicsTypes.insert(static_cast<int>(PhysicsType::RigidBody), "Rigid Body");
    physicsTypes.insert(static_cast<int>(PhysicsType::SoftBody), "Soft Body");

    physicsShapes.insert(static_cast<int>(PhysicsCollisionShape::None), "None");
    physicsShapes.insert(static_cast<int>(PhysicsCollisionShape::Plane), "Plane");
    physicsShapes.insert(static_cast<int>(PhysicsCollisionShape::Sphere), "Sphere");
    physicsShapes.insert(static_cast<int>(PhysicsCollisionShape::Cube), "Cube");
    physicsShapes.insert(static_cast<int>(PhysicsCollisionShape::ConvexHull), "Convex Hull");
    physicsShapes.insert(static_cast<int>(PhysicsCollisionShape::TriangleMesh), "Triangle Mesh");

    physicsTypeSelector = this->addComboBox("Physics Type");
    QMap<int, QString>::const_iterator ptIter;
    for (ptIter = physicsTypes.constBegin(); ptIter != physicsTypes.constEnd(); ++ptIter) {
        physicsTypeSelector->addItem(ptIter.value(), ptIter.key());
    }

    physicsShapeSelector = this->addComboBox("Collision Shape");
    QMap<int, QString>::const_iterator psIter;
    for (psIter = physicsShapes.constBegin(); psIter != physicsShapes.constEnd(); ++psIter) {
        physicsShapeSelector->addItem(psIter.value(), psIter.key());
    }

    isVisible = this->addCheckBox("Visible", true);
    massValue = this->addFloatValueSlider("Object Mass", 0.f, 100.f, 1.f);
    marginValue = this->addFloatValueSlider("Collision Margin", .01f, 1.f, .1f);
    bouncinessValue = this->addFloatValueSlider("Bounciness", 0.f, 1.f, .1f);

    connect(isVisible, &CheckBoxWidget::valueChanged, this, &PhysicsPropertyWidget::onVisibilityChanged);
    connect(massValue, &HFloatSliderWidget::valueChanged, this, &PhysicsPropertyWidget::onMassChanged);
    connect(marginValue, &HFloatSliderWidget::valueChanged, this, &PhysicsPropertyWidget::onMarginChanged);
    connect(bouncinessValue, &HFloatSliderWidget::valueChanged, this, &PhysicsPropertyWidget::onBouncinessChanged);
    connect(physicsShapeSelector, static_cast<void (ComboBoxWidget::*)(int)>(&ComboBoxWidget::currentIndexChanged),
        this, &PhysicsPropertyWidget::onPhysicsShapeChanged);
    connect(physicsTypeSelector, static_cast<void (ComboBoxWidget::*)(int)>(&ComboBoxWidget::currentIndexChanged),
        this, &PhysicsPropertyWidget::onPhysicsTypeChanged);
}

PhysicsPropertyWidget::~PhysicsPropertyWidget()
{

}

void PhysicsPropertyWidget::setSceneNode(iris::SceneNodePtr sceneNode)
{
    if (!!sceneNode) {
        this->sceneNode = sceneNode;

        auto disabledItems = QVector<int>();
        QStandardItemModel *model = qobject_cast<QStandardItemModel*>(physicsShapeSelector->getWidget()->model());

        if (sceneNode->getSceneNodeType() == iris::SceneNodeType::Empty) {
            disabledItems.append(static_cast<int>(PhysicsCollisionShape::ConvexHull));
            disabledItems.append(static_cast<int>(PhysicsCollisionShape::TriangleMesh));
        }

        for (int index = 0; index < physicsShapeSelector->getWidget()->count(); ++index) {
            model->item(index)->setEnabled(!disabledItems.contains(index));
        }

        isVisible->setValue(sceneNode->physicsProperty.isVisible);
        massValue->setValue(sceneNode->physicsProperty.objectMass);
        marginValue->setValue(sceneNode->physicsProperty.objectCollisionMargin);
        bouncinessValue->setValue(sceneNode->physicsProperty.objectRestitution);
        
        physicsShapeSelector->setCurrentText(physicsShapes.value(static_cast<int>(sceneNode->physicsProperty.shape)));
        physicsTypeSelector->setCurrentText(physicsTypes.value(static_cast<int>(sceneNode->physicsProperty.type)));

    } else {
        this->sceneNode.clear();
    }
}

void PhysicsPropertyWidget::setSceneView(SceneViewWidget *sceneView)
{
    this->sceneView = sceneView;
}

void PhysicsPropertyWidget::onPhysicsShapeChanged(int index)
{
    currentBody = sceneView->getScene()->getPhysicsEnvironment()->hashBodies.value(sceneNode->getGUID());

    int shape = physicsShapeSelector->getItemData(index).toInt();

    iris::PhysicsProperty physicsProperties;
    physicsProperties.isStatic = (massValue->getValue() == 0) ? true : false;
    physicsProperties.objectCollisionMargin = marginValue->getValue();
    physicsProperties.objectMass = massValue->getValue();
    physicsProperties.objectRestitution = bouncinessValue->getValue();
    physicsProperties.shape = static_cast<iris::PhysicsCollisionShape>(shape);

    this->sceneNode->physicsProperty.shape = static_cast<iris::PhysicsCollisionShape>(shape);

    //btRigidBody *body = iris::PhysicsHelper::createPhysicsBody(sceneNode, physicsProperties);
    //if (!body) {
    //    qWarning("Failed to create a rigid body from object");
    //    return;
    //};

    // Can I change shape of a rigid body after it created in Bullet3D?
    // https://gamedev.stackexchange.com/a/11956/16598
    //if (currentBody) {
    //    currentBody->setMotionState(motionState);
    //    currentBody->setMassProps(mass, inertia);
    //    shape->calculateLocalInertia(mass, inertia);
    //    currentBody->setCollisionShape(shape);
    //    currentBody->setRestitution(bounciness);
    //    currentBody->setCenterOfMassTransform(transform);
    //    currentBody->updateInertiaTensor();
    //}

    this->sceneNode->isPhysicsBody = true;

 /*   sceneView->removeBodyFromWorld(sceneNode->getGUID());
    sceneView->addBodyToWorld(body, sceneNode);*/
}

void PhysicsPropertyWidget::onPhysicsTypeChanged(int index)
{
    int type = physicsTypeSelector->getItemData(index).toInt();

    float mass = 0.0;

    iris::PhysicsProperty physicsProperties;

    if (type == static_cast<int>(iris::PhysicsType::None)) {
        this->sceneNode->isPhysicsBody = false;
        this->sceneNode->physicsProperty.type = PhysicsType::None;
        return;
    }

    if (type == static_cast<int>(iris::PhysicsType::Static)) {
        mass = .0f;
        massValue->setValue(mass);
        physicsProperties.objectMass = massValue->getValue();
        this->sceneNode->physicsProperty.type = PhysicsType::Static;
    }

    if (type == static_cast<int>(iris::PhysicsType::RigidBody)) {
        mass = massValue->getValue();
        massValue->setValue(mass);
        physicsProperties.objectMass = mass;
        this->sceneNode->physicsProperty.type = PhysicsType::RigidBody;
    }

    physicsProperties.isStatic = (massValue->getValue() == 0) ? true : false;
    physicsProperties.objectCollisionMargin = marginValue->getValue();
    physicsProperties.objectRestitution = bouncinessValue->getValue();
    physicsProperties.type = static_cast<iris::PhysicsType>(type);

    this->sceneNode->isPhysicsBody = true;

    this->sceneNode->physicsProperty = physicsProperties;

    //btRigidBody *body = iris::PhysicsHelper::createPhysicsBody(sceneNode, physicsProperties);
    //if (!body) {
    //    qWarning("Failed to create a rigid body from object");
    //    return;
    //};

 /*   sceneView->removeBodyFromWorld(sceneNode->getGUID());
    this->sceneView->addBodyToWorld(body, sceneNode);*/
}

void PhysicsPropertyWidget::onVisibilityChanged(bool value)
{
    if (sceneNode) isVisible->setValue(sceneNode->physicsProperty.isVisible = value);
}

void PhysicsPropertyWidget::onMassChanged(float value)
{
    if (sceneNode) massValue->setValue(sceneNode->physicsProperty.objectMass = value);
}

void PhysicsPropertyWidget::onMarginChanged(float value)
{
    if (sceneNode) marginValue->setValue(sceneNode->physicsProperty.objectCollisionMargin = value);
}

void PhysicsPropertyWidget::onBouncinessChanged(float value)
{
    if (sceneNode) bouncinessValue->setValue(sceneNode->physicsProperty.objectRestitution = value);
}