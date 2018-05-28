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
    connect(physicsTypeSelector, static_cast<void (ComboBoxWidget::*)(const QString&)>(&ComboBoxWidget::currentIndexChanged),
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
    btRigidBody *currentBody = sceneView->getScene()->getPhysicsEnvironment()->hashBodies.value(sceneNode->getGUID());

    int itemData = physicsShapeSelector->getItemData(index).toInt();

    btVector3 pos(sceneNode->getLocalPos().x(), sceneNode->getLocalPos().y(), sceneNode->getLocalPos().z());
    btRigidBody *body = Q_NULLPTR;

    btTransform transform;
    transform.setIdentity();

    auto meshNode = sceneNode.staticCast<iris::MeshNode>();
    auto rot = meshNode->getGlobalRotation().toVector4D();

    btQuaternion quat;
    quat.setX(rot.x());
    quat.setY(rot.y());
    quat.setZ(rot.z());
    quat.setW(rot.w());

    auto mass = massValue->getValue();
    auto bounciness = bouncinessValue->getValue();
    btCollisionShape *shape = Q_NULLPTR;
    btVector3 inertia(0, 0, 0);
    btMotionState *motionState = Q_NULLPTR;

    switch (itemData) {
        case static_cast<int>(PhysicsCollisionShape::None): {
            transform.setFromOpenGLMatrix(sceneNode->getLocalTransform().constData());
            transform.setOrigin(pos);
            transform.setRotation(quat);

            shape = new btEmptyShape();
            motionState = new btDefaultMotionState(transform);
            
            if (mass != 0.0) shape->calculateLocalInertia(mass, inertia);
            
            btRigidBody::btRigidBodyConstructionInfo info(mass, motionState, shape);
            body = new btRigidBody(info);
            body->setCenterOfMassTransform(transform);

            break;
        }

        case static_cast<int>(PhysicsCollisionShape::Sphere) : {

            transform.setOrigin(pos);
            transform.setRotation(quat);

            float rad = 1.0;
            
            shape = new btSphereShape(rad);
            shape->setMargin(marginValue->getValue());
            motionState = new btDefaultMotionState(transform);

            btVector3 inertia(0, 0, 0);
            
            if (mass != 0.0) shape->calculateLocalInertia(mass, inertia);
            
            btRigidBody::btRigidBodyConstructionInfo info(mass, motionState, shape, inertia);
            body = new btRigidBody(info);
            body->setRestitution(bounciness);
            body->setCenterOfMassTransform(transform);

            break;
        }

        case static_cast<int>(PhysicsCollisionShape::Plane) : {

            transform.setOrigin(pos);
            transform.setRotation(quat);

            shape = new btStaticPlaneShape(btVector3(0, 1, 0), 0.f);
            shape->setMargin(marginValue->getValue());
            motionState = new btDefaultMotionState(transform);

            btRigidBody::btRigidBodyConstructionInfo info(mass, motionState, shape);

            body = new btRigidBody(info);
            body->setRestitution(bounciness);
            body->setCenterOfMassTransform(transform);

            break;
        }

        case static_cast<int>(PhysicsCollisionShape::Cube) : {

            transform.setOrigin(pos);
            transform.setRotation(quat);

            shape = new btBoxShape(btVector3(1, 1, 1));
            shape->setMargin(marginValue->getValue());
            motionState = new btDefaultMotionState(transform);

            if (mass != 0.0) shape->calculateLocalInertia(mass, inertia);

            btRigidBody::btRigidBodyConstructionInfo info(mass, motionState, shape, inertia);

            body = new btRigidBody(info);
            body->setRestitution(bounciness);
            body->setCenterOfMassTransform(transform);

            break;
        }

        // only show for mesh types!                                       
        case static_cast<int>(PhysicsCollisionShape::ConvexHull) : {
            transform.setOrigin(pos);
            transform.setRotation(quat);

            // https://www.gamedev.net/forums/topic/691208-build-a-convex-hull-from-a-given-mesh-in-bullet/
            // https://pybullet.org/Bullet/phpBB3/viewtopic.php?t=11342
            auto tmpShape = iris::PhysicsHelper::btConvexHullShapeFromMesh(meshNode->getMesh());
            tmpShape->setMargin(0); // bullet bug still?
            // tmpShape->setMargin(marginValue->getValue());

            // https://www.gamedev.net/forums/topic/602994-glmmodel-to-bullet-shape-btconvextrianglemeshshape/
            // alternatively instead of building a hull manually with points, use the triangle mesh
            // auto triMesh = iris::PhysicsHelper::btTriangleMeshShapeFromMesh(meshNode->getMesh());
            // btConvexShape *tmpshape = new btConvexTriangleMeshShape(triMesh);
            // btShapeHull *hull = new btShapeHull(tmpshape);
            // btScalar margin = tmpshape->getMargin();

            btShapeHull *hull = new btShapeHull(static_cast<btConvexHullShape*>(tmpShape));
            hull->buildHull(0);

            btConvexHullShape* pConvexHullShape = new btConvexHullShape(
                (const btScalar*) hull->getVertexPointer(), hull->numVertices(), sizeof(btVector3));
            shape = pConvexHullShape;
            delete hull;
            delete tmpShape;

            motionState = new btDefaultMotionState(transform);

            if (mass != 0.0) shape->calculateLocalInertia(mass, inertia);

            btRigidBody::btRigidBodyConstructionInfo info(mass, motionState, shape, inertia);

            body = new btRigidBody(info);
            body->setRestitution(bounciness);
            body->setCenterOfMassTransform(transform);

            break;
        }

        case static_cast<int>(PhysicsCollisionShape::TriangleMesh) : {

            transform.setOrigin(pos);
            transform.setRotation(quat);

            // convert triangle mesh into convex shape

            auto triMesh = iris::PhysicsHelper::btTriangleMeshShapeFromMesh(meshNode->getMesh());

            shape = new btConvexTriangleMeshShape(triMesh, true);
            shape->setMargin(marginValue->getValue());
            motionState = new btDefaultMotionState(transform);

            if (mass != 0.0) shape->calculateLocalInertia(mass, inertia);

            btRigidBody::btRigidBodyConstructionInfo info(mass, motionState, shape, inertia);

            body = new btRigidBody(info);
            body->setRestitution(bounciness);
            body->setCenterOfMassTransform(transform);

            break;
        }

        default: break;
    }

    if (sceneNode) sceneNode->physicsProperty.shape = static_cast<PhysicsCollisionShape>(itemData);
    //physicsShapeSelector->setCurrentIndex(index);

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
    sceneView->removeBodyFromWorld(sceneNode->getGUID());
    sceneView->addBodyToWorld(body, sceneNode->getGUID());
}

void PhysicsPropertyWidget::onPhysicsTypeChanged(const QString &text)
{
    this->sceneView->removeBodyFromWorld(sceneNode->getGUID());

    float mass = 0.0;

    qDebug() << "test " << text;

    if (text == "None") {
        this->sceneNode->isPhysicsBody = false;
        this->sceneNode->physicsProperty.type = PhysicsType::None;
        return;
    }

    btTransform transform;
    transform.setIdentity();
    transform.setFromOpenGLMatrix(sceneNode->getLocalTransform().constData());
    btVector3 pos(sceneNode->getLocalPos().x(), sceneNode->getLocalPos().y(), sceneNode->getLocalPos().z());
    transform.setOrigin(pos);
   
    if (text == "Static") {
        mass = .0f;
        massValue->setValue(mass);
        this->sceneNode->physicsProperty.type = PhysicsType::Static;
    }

    btCollisionShape *shape = new btEmptyShape();
    btVector3 inertia(0, 0, 0);
    
    if (text == "Rigid Body") {
        mass = massValue->getValue(); // default?
        shape->calculateLocalInertia(mass, inertia);
        this->sceneNode->physicsProperty.type = PhysicsType::RigidBody;
    }

    btMotionState *motion = new btDefaultMotionState(transform);

    btRigidBody::btRigidBodyConstructionInfo info(mass, motion, shape, inertia);
    btRigidBody *body = new btRigidBody(info);
    body->setCenterOfMassTransform(transform);

    this->sceneNode->isPhysicsBody = true;
    this->sceneView->addBodyToWorld(body, sceneNode->getGUID());
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