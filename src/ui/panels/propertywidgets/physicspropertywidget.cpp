/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/physicspropertywidget.h"
#include "viewport/ieditorviewport.h"

#include <QStandardItemModel>

#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/physics/environment.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/comboboxwidget.h"

#include "irisgl/thirdparty/bullet3/src/btBulletDynamicsCommon.h"
#include "BulletCollision/CollisionShapes/btConvexHullShape.h"
#include "BulletCollision/CollisionShapes/btShapeHull.h"
#include "irisgl/document/physics/physicshelper.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "ui/panels/propertywidgets/panelundo.h"
#include "ui/panels/propertywidgets/rowundo.h"

using namespace iris;

PhysicsPropertyWidget::PhysicsPropertyWidget()
{
    physicsTypes.insert(static_cast<int>(PhysicsType::None), "None");
    physicsTypes.insert(static_cast<int>(PhysicsType::Static), "Static (Inanimate)");
    physicsTypes.insert(static_cast<int>(PhysicsType::RigidBody), "Rigid Body (Dynamic)");
    //physicsTypes.insert(static_cast<int>(PhysicsType::SoftBody), "Soft Body");

    physicsShapes.insert(static_cast<int>(PhysicsCollisionShape::None), "None");
    physicsShapes.insert(static_cast<int>(PhysicsCollisionShape::Compound), "Compound");
    physicsShapes.insert(static_cast<int>(PhysicsCollisionShape::Plane), "Plane");
    physicsShapes.insert(static_cast<int>(PhysicsCollisionShape::Sphere), "Sphere");
    physicsShapes.insert(static_cast<int>(PhysicsCollisionShape::Cube), "Cube");
    physicsShapes.insert(static_cast<int>(PhysicsCollisionShape::ConvexHull), "Convex Hull (Recommended)");
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
    frictionValue = this->addFloatValueSlider("Object Friction", 0.f, 1.f, .5f);
    marginValue = this->addFloatValueSlider("Collision Margin", .01f, 1.f, .1f);
    bouncinessValue = this->addFloatValueSlider("Bounciness", 0.f, 1.f, .1f);

    // isVisible is DELIBERATELY not undoable and not a document property: it is
    // never serialized (node.physics refuses the key for the same reason), so
    // an undo step for it would restore a value the next save drops anyway.
    connect(isVisible, &CheckBoxWidget::valueChanged, this, &PhysicsPropertyWidget::onVisibilityChanged);

    // The four body scalars: one undo step per drag, replaying the same struct
    // write node.physics performs (debt L6 — these rows had no undo at all).
    rowundo::bind(massValue, bodyRow(tr("Object Mass"),
        [this]() { return sceneNode->physicsProperty.objectMass; },
        [this](float v) {
            sceneNode->physicsProperty.objectMass = v;
            // A static body has mass 0 in Bullet, and mass 0 IS static — the
            // derived flag the panel and the verb both keep true.
            sceneNode->physicsProperty.isStatic =
                sceneNode->physicsProperty.type == PhysicsType::Static || v == 0.0f;
        }));
    rowundo::bind(marginValue, bodyRow(tr("Collision Margin"),
        [this]() { return sceneNode->physicsProperty.objectCollisionMargin; },
        [this](float v) { sceneNode->physicsProperty.objectCollisionMargin = v; }));
    rowundo::bind(frictionValue, bodyRow(tr("Object Friction"),
        [this]() { return sceneNode->physicsProperty.objectFriction; },
        [this](float v) { sceneNode->physicsProperty.objectFriction = v; }));
    rowundo::bind(bouncinessValue, bodyRow(tr("Bounciness"),
        [this]() { return sceneNode->physicsProperty.objectRestitution; },
        [this](float v) { sceneNode->physicsProperty.objectRestitution = v; }));
    connect(physicsShapeSelector, static_cast<void (ComboBoxWidget::*)(int)>(&ComboBoxWidget::currentIndexChanged),
        this, &PhysicsPropertyWidget::onPhysicsShapeChanged);
    connect(physicsTypeSelector, static_cast<void (ComboBoxWidget::*)(int)>(&ComboBoxWidget::currentIndexChanged),
        this, &PhysicsPropertyWidget::onPhysicsTypeChanged);
}

PhysicsPropertyWidget::~PhysicsPropertyWidget()
{

}

// A physics row writes a FIELD OF A STRUCT, not a reflected property, so the
// undo step is the shape node.physics uses: capture the whole settings struct
// (plus the isPhysicsBody flag, which the type row moves), apply, and replay.
// Field-wise on the way back, never a struct assign — constraints, centre of
// mass and pivot are outside these rows' scope and a whole-struct copy would
// silently carry or drop them, exactly as nodeapi records.
void PhysicsPropertyWidget::edit(const QString &text, const std::function<void()> &apply)
{
    if (!sceneNode || !apply) return;
    auto node = sceneNode;
    const iris::PhysicsProperty before = node->physicsProperty;
    const bool wasBody = node->isPhysicsBody;
    // The type row pushes the mass slider (a static body has mass 0), and a
    // slider's setValue EMITS: without this the nested row would record a step
    // of its own inside this one.
    const bool wasLoading = loading;
    loading = true;
    apply();
    loading = wasLoading;
    const iris::PhysicsProperty after = node->physicsProperty;
    const bool isBody = node->isPhysicsBody;
    auto write = [](const iris::SceneNodePtr &n, const iris::PhysicsProperty &p, bool body) {
        n->physicsProperty.type = p.type;
        n->physicsProperty.shape = p.shape;
        n->physicsProperty.objectMass = p.objectMass;
        n->physicsProperty.objectRestitution = p.objectRestitution;
        n->physicsProperty.objectFriction = p.objectFriction;
        n->physicsProperty.objectDamping = p.objectDamping;
        n->physicsProperty.objectCollisionMargin = p.objectCollisionMargin;
        n->physicsProperty.isStatic = p.isStatic;
        n->isPhysicsBody = body;
    };
    panelundo::pushEdit(services, text,
                        [node, after, isBody, write]() { write(node, after, isBody); },
                        [node, before, wasBody, write]() { write(node, before, wasBody); });
}

rowundo::Binding PhysicsPropertyWidget::bodyRow(const QString &text, std::function<float()> get,
                                                std::function<void(float)> set)
{
    rowundo::Binding b;
    b.guard = [this]() { return !loading && !!sceneNode; };
    b.read = [this, get]() { return sceneNode ? QVariant(get()) : QVariant(); };
    b.write = [this, set](const QVariant &v) { if (sceneNode) set(v.toFloat()); };
    b.commit = [this, text, get, set](const QVariant &before, const QVariant &after) {
        if (!sceneNode) return;
        // The row has already written `after`; edit() re-applies it so the
        // command carries both halves of the struct.
        set(before.toFloat());
        edit(text, [set, after]() { set(after.toFloat()); });
    };
    return b;
}

void PhysicsPropertyWidget::setSceneNode(iris::SceneNodePtr sceneNode)
{
    if (!!sceneNode) {
        this->sceneNode = sceneNode;
        loading = true;

        auto disabledItems = QVector<int>();
        QStandardItemModel *model = qobject_cast<QStandardItemModel*>(physicsShapeSelector->getWidget()->model());

        if (sceneNode->getSceneNodeType() == iris::SceneNodeType::Empty) {
            disabledItems.append(static_cast<int>(PhysicsCollisionShape::Cube));
            disabledItems.append(static_cast<int>(PhysicsCollisionShape::Sphere));
            disabledItems.append(static_cast<int>(PhysicsCollisionShape::Plane));
            disabledItems.append(static_cast<int>(PhysicsCollisionShape::ConvexHull));
            disabledItems.append(static_cast<int>(PhysicsCollisionShape::TriangleMesh));
		}

        for (int index = 0; index < physicsShapeSelector->getWidget()->count(); ++index) {
            model->item(index)->setEnabled(!disabledItems.contains(index));
        }

        isVisible->setValue(sceneNode->physicsProperty.isVisible);
        massValue->setValue(sceneNode->physicsProperty.objectMass);
		frictionValue->setValue(sceneNode->physicsProperty.objectFriction);
        marginValue->setValue(sceneNode->physicsProperty.objectCollisionMargin);
        bouncinessValue->setValue(sceneNode->physicsProperty.objectRestitution);
        
        physicsShapeSelector->setCurrentText(physicsShapes.value(static_cast<int>(sceneNode->physicsProperty.shape)));
        physicsTypeSelector->setCurrentText(physicsTypes.value(static_cast<int>(sceneNode->physicsProperty.type)));
        loading = false;
    } else {
        this->sceneNode.clear();
    }
}

void PhysicsPropertyWidget::setSceneView(IEditorViewport *sceneView)
{
    this->sceneView = sceneView;
}

void PhysicsPropertyWidget::onPhysicsShapeChanged(int index)
{
    if (loading || !sceneNode) return;
    if (sceneView && sceneView->getScene())
        currentBody = sceneView->getScene()->getPhysicsEnvironment()->hashBodies.value(sceneNode->getGUID());

    int shape = physicsShapeSelector->getItemData(index).toInt();

    edit(tr("Collision Shape"), [this, shape]() {
    this->sceneNode->physicsProperty.shape = static_cast<iris::PhysicsCollisionShape>(shape);

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
    });
}

void PhysicsPropertyWidget::onPhysicsTypeChanged(int index)
{
    if (loading || !sceneNode) return;
    int type = physicsTypeSelector->getItemData(index).toInt();

    edit(tr("Physics Type"), [this, type]() {
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
    });
}

void PhysicsPropertyWidget::onVisibilityChanged(bool value)
{
    // Not serialized, so not undoable — see the note where this is connected.
    if (sceneNode) sceneNode->physicsProperty.isVisible = value;
}