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
    isPhysicsObject = this->addCheckBox("Physics Object", false);
    isStaticObject = this->addCheckBox("Static", false);
    isVisible = this->addCheckBox("Visible", true);
    massValue = this->addFloatValueSlider("Object Mass", 0.f, 100.f, 1.f);
    bouncinessValue = this->addFloatValueSlider("Bounciness", 0.f, 1.f, .1f);

    shapeSelector = this->addComboBox("Collision Shape");
    shapeSelector->addItem("None", static_cast<int>(PhysicsCollisionShape::None));
    shapeSelector->addItem("Plane", static_cast<int>(PhysicsCollisionShape::Plane));
    shapeSelector->addItem("Sphere", static_cast<int>(PhysicsCollisionShape::Sphere));
    shapeSelector->addItem("Cube", static_cast<int>(PhysicsCollisionShape::Cube));
    shapeSelector->addItem("Convex Hull", static_cast<int>(PhysicsCollisionShape::ConvexHull));
    shapeSelector->addItem("Triangle Mesh", static_cast<int>(PhysicsCollisionShape::TriangleMesh));

    connect(isPhysicsObject, &CheckBoxWidget::valueChanged, this, &PhysicsPropertyWidget::onPhysicsEnabled);
    connect(isStaticObject, &CheckBoxWidget::valueChanged, this, &PhysicsPropertyWidget::onStaticTypeChecked);
    connect(isVisible, &CheckBoxWidget::valueChanged, this, &PhysicsPropertyWidget::onVisibilityChanged);
    connect(massValue, &HFloatSliderWidget::valueChanged, this, &PhysicsPropertyWidget::onMassChanged);
    connect(bouncinessValue, &HFloatSliderWidget::valueChanged, this, &PhysicsPropertyWidget::onBouncinessChanged);
    connect(shapeSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(onPhysicsTypeChanged(int)));
}

PhysicsPropertyWidget::~PhysicsPropertyWidget()
{

}

void PhysicsPropertyWidget::setSceneNode(iris::SceneNodePtr sceneNode)
{
    if (!!sceneNode) {
        this->sceneNode = sceneNode;

        auto disabledItems = QVector<int>();
        QStandardItemModel *model = qobject_cast<QStandardItemModel*>(shapeSelector->getWidget()->model());

        if (sceneNode->getSceneNodeType() == iris::SceneNodeType::Empty) {
            disabledItems.append(static_cast<int>(PhysicsCollisionShape::ConvexHull));
            disabledItems.append(static_cast<int>(PhysicsCollisionShape::TriangleMesh));
        }

        for (int index = 0; index < shapeSelector->getWidget()->count(); ++index) {
            model->item(index)->setEnabled(!disabledItems.contains(index));
        }

        isPhysicsObject->setValue(sceneNode->isPhysicsBody);
        isStaticObject->setValue(sceneNode->physicsProperty.isStatic);
        isVisible->setValue(sceneNode->physicsProperty.isVisible);
        massValue->setValue(sceneNode->physicsProperty.objectMass);
        bouncinessValue->setValue(sceneNode->physicsProperty.objectRestitution);
        // Note that the index matches the shape's enum value
        shapeSelector->setCurrentIndex(static_cast<int>(sceneNode->physicsProperty.shape));

    } else {
        this->sceneNode.clear();
    }
}

void PhysicsPropertyWidget::setSceneView(SceneViewWidget *sceneView)
{
    this->sceneView = sceneView;
}

void PhysicsPropertyWidget::onPhysicsTypeChanged(int index)
{
    btRigidBody *currentBody = sceneView->getScene()->getPhysicsEnvironment()->hashBodies.value(sceneNode->getGUID());

    int itemData = shapeSelector->getItemData(index).toInt();

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
    shapeSelector->setCurrentIndex(index);

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

void PhysicsPropertyWidget::onPhysicsEnabled(bool value)
{
    isPhysicsObject->setValue(this->sceneNode->isPhysicsBody = value);

    if (!value) {
        this->sceneView->removeBodyFromWorld(sceneNode->getGUID());
        return;
    }

    btTransform transform;
    transform.setIdentity();
    transform.setFromOpenGLMatrix(sceneNode->getLocalTransform().constData());
    btVector3 pos(sceneNode->getLocalPos().x(), sceneNode->getLocalPos().y(), sceneNode->getLocalPos().z());
    transform.setOrigin(pos);

    float mass = 0.0;

    btCollisionShape *shape = new btEmptyShape();
    btMotionState *motion = new btDefaultMotionState(transform);

    btVector3 inertia(0, 0, 0);

    if (mass != 0.0) shape->calculateLocalInertia(mass, inertia);

    btRigidBody::btRigidBodyConstructionInfo info(mass, motion, shape, inertia);
    btRigidBody *body = new btRigidBody(info);
    body->setCenterOfMassTransform(transform);
    
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

void PhysicsPropertyWidget::onBouncinessChanged(float value)
{
    if (sceneNode) bouncinessValue->setValue(sceneNode->physicsProperty.objectRestitution = value);
}

void PhysicsPropertyWidget::onStaticTypeChecked(bool value)
{
    if (sceneNode) isStaticObject->setValue(sceneNode->physicsProperty.isStatic = value);
}