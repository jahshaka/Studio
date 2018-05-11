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
#include "physics/physicshelper.h"

enum class Shapes : int
{
    NONE,
    PLANE,
    SPHERE,
    CUBE,
    CONVEX_HULL,
    TRIANGLE_MESH
};

PhysicsPropertyWidget::PhysicsPropertyWidget()
{
    isPhysicsObject = this->addCheckBox("Physics Object", false);
    isStaticObject = this->addCheckBox("Static", false);
    isVisible = this->addCheckBox("Visible", true);
    massValue = this->addFloatValueSlider("Object Mass", 0.f, 100.f, 1.f);
    bouncinessValue = this->addFloatValueSlider("Bounciness", 0.f, 1.f, .1f);

    shapeSelector = this->addComboBox("Collision Shape");
    shapeSelector->addItem("None", static_cast<int>(Shapes::NONE));
    shapeSelector->addItem("Plane", static_cast<int>(Shapes::PLANE));
    shapeSelector->addItem("Sphere", static_cast<int>(Shapes::SPHERE));
    shapeSelector->addItem("Cube", static_cast<int>(Shapes::CUBE));
    shapeSelector->addItem("Convex Hull (fast, imprecise)", static_cast<int>(Shapes::CONVEX_HULL));
    shapeSelector->addItem("Triangle Mesh (slow, precise)", static_cast<int>(Shapes::TRIANGLE_MESH));

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
            disabledItems.append(static_cast<int>(Shapes::CONVEX_HULL));
            disabledItems.append(static_cast<int>(Shapes::TRIANGLE_MESH));
        }

        for (int index = 0; index < shapeSelector->getWidget()->count(); ++index) {
            model->item(index)->setEnabled(!disabledItems.contains(index));
        }

        isPhysicsObject->setValue(sceneNode->isPhysicsBody);
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

    auto mass = massValue->getValue();
    auto bounciness = bouncinessValue->getValue();

    switch (itemData) {
        case static_cast<int>(Shapes::NONE): {
            transform.setFromOpenGLMatrix(sceneNode->getLocalTransform().constData());
            transform.setOrigin(pos);

            btCollisionShape *shape = new btEmptyShape();
            btMotionState *motion = new btDefaultMotionState(transform);
            
            btVector3 inertia(0, 0, 0);

            if (mass != 0.0) shape->calculateLocalInertia(mass, inertia);
            
            btRigidBody::btRigidBodyConstructionInfo info(mass, motion, shape);
            body = new btRigidBody(info);

            break;
        }

        case static_cast<int>(Shapes::SPHERE) : {

            transform.setOrigin(pos);
            
            float rad = 1.0;
            
            btCollisionShape * sphere = new btSphereShape(rad);
            btMotionState * motion = new btDefaultMotionState(transform);

            btVector3 inertia(0, 0, 0);
            
            if (mass != 0.0) sphere->calculateLocalInertia(mass, inertia);
            
            btRigidBody::btRigidBodyConstructionInfo info(mass, motion, sphere, inertia);
            body = new btRigidBody(info);
            body->setRestitution(bounciness);

            break;
        }

        case static_cast<int>(Shapes::PLANE) : {

            transform.setOrigin(pos);

            btCollisionShape * plane = new btStaticPlaneShape(btVector3(0, 1, 0), 0.f);
            btMotionState * motion = new btDefaultMotionState(transform);

            btRigidBody::btRigidBodyConstructionInfo info(mass, motion, plane);

            body = new btRigidBody(info);
            body->setRestitution(bounciness);

            break;
        }

        case static_cast<int>(Shapes::CUBE) : {

            transform.setOrigin(pos);

            float mass = 0.0;
            float rad = 1.0;

            btCollisionShape *cube = new btBoxShape(btVector3(1, 1, 1));
            btMotionState *motion = new btDefaultMotionState(transform);

            btVector3 inertia;

            if (mass != 0.0) cube->calculateLocalInertia(mass, inertia);

            btRigidBody::btRigidBodyConstructionInfo info(mass, motion, cube, inertia);

            body = new btRigidBody(info);
            body->setRestitution(bounciness);

            break;
        }

        // only show for mesh types!                                       
        case static_cast<int>(Shapes::CONVEX_HULL) : {
            break;
        }

        case static_cast<int>(Shapes::TRIANGLE_MESH) : {

            transform.setOrigin(pos);

            auto meshNode = sceneNode.staticCast<iris::MeshNode>();

            auto rot = meshNode->getGlobalRotation().toVector4D();

            btQuaternion quat;
            quat.setX(rot.x());
            quat.setY(rot.y());
            quat.setZ(rot.z());
            quat.setW(rot.w());
            transform.setRotation(quat);

            // convert triangle mesh into convex shape

            auto triMesh = iris::PhysicsHelper::btTriangleMeshShapeFromMesh(meshNode->getMesh());

            auto shape = new btConvexTriangleMeshShape(triMesh, true);
            btMotionState *motion = new btDefaultMotionState(transform);

            btVector3 inertia(0, 0, 0);

            if (mass != 0.0) shape->calculateLocalInertia(mass, inertia);

            btRigidBody::btRigidBodyConstructionInfo info(mass, motion, shape, inertia);

            body = new btRigidBody(info);
            body->setRestitution(bounciness);

            body->setCenterOfMassTransform(transform);

            break;
        }

        default: break; // can't happen
    }

    sceneView->removeBodyFromWorld(currentBody);
    sceneView->addBodyToWorld(body, sceneNode->getGUID());
}

void PhysicsPropertyWidget::onPhysicsEnabled(bool value)
{
    btTransform t;
    t.setIdentity();
    t.setFromOpenGLMatrix(sceneNode->getLocalTransform().constData());

    float mass = 0.0;

    btCollisionShape *shape = new btEmptyShape();
    btMotionState *motion = new btDefaultMotionState(t);

    btVector3 inertia(0, 0, 0);

    if (mass != 0.0) shape->calculateLocalInertia(mass, inertia);

    btRigidBody::btRigidBodyConstructionInfo info(mass, motion, shape, inertia);
    btRigidBody *body = new btRigidBody(info);
    
    isPhysicsObject->setValue(this->sceneNode->isPhysicsBody = value);
    this->sceneView->addBodyToWorld(body, sceneNode->getGUID());
}

void PhysicsPropertyWidget::onVisibilityChanged(bool value)
{
}

void PhysicsPropertyWidget::onMassChanged(float value)
{
}

void PhysicsPropertyWidget::onBouncinessChanged(float value)
{
}

void PhysicsPropertyWidget::onStaticTypeChecked(bool value)
{
}