#include "physicspropertywidget.h"

#include "irisgl/src/scenegraph/meshnode.h"
#include "irisgl/src/scenegraph/scene.h"
#include "irisgl/src/physics/environment.h"
#include "../sceneviewwidget.h"
#include "globals.h"
#include "../checkboxwidget.h"
#include "../comboboxwidget.h"

#include "irisgl/src/bullet3/src/btBulletDynamicsCommon.h"

enum class Shapes : int
{
    NONE,
    PLANE,
    SPHERE,
    CUBE,
    HULL
};

PhysicsPropertyWidget::PhysicsPropertyWidget()
{
    isPhysicsObject = this->addCheckBox("Physics Object", false);
    isStaticObject = this->addCheckBox("Static", false);

    shapeSelector = this->addComboBox("Collision Shape");
    shapeSelector->addItem("None", static_cast<int>(Shapes::NONE));
    shapeSelector->addItem("Plane", static_cast<int>(Shapes::PLANE));
    shapeSelector->addItem("Sphere", static_cast<int>(Shapes::SPHERE));
    shapeSelector->addItem("Cube", static_cast<int>(Shapes::CUBE));
    shapeSelector->addItem("Object Hull", static_cast<int>(Shapes::HULL));

    connect(isPhysicsObject, SIGNAL(valueChanged(bool)), SLOT(onPhysicsEnabled(bool)));
    connect(isStaticObject, SIGNAL(valueChanged(bool)), SLOT(onStaticTypeChecked(bool)));
    connect(shapeSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(onPhysicsTypeChanged(int)));
}

PhysicsPropertyWidget::~PhysicsPropertyWidget()
{

}

void PhysicsPropertyWidget::setSceneNode(iris::SceneNodePtr sceneNode)
{
    if (!!sceneNode && sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
        this->meshNode = sceneNode.staticCast<iris::MeshNode>();
    } else {
        this->meshNode.clear();
    }
}

void PhysicsPropertyWidget::setSceneView(SceneViewWidget *sceneView)
{
    this->sceneView = sceneView;
}

void PhysicsPropertyWidget::onPhysicsTypeChanged(int index)
{
    btRigidBody *currentBody = sceneView->getScene()->getPhysicsEnvironment()->hashBodies.value(meshNode->getGUID());
    int itemData = shapeSelector->getItemData(index).toInt();

    btVector3 pos(meshNode->getLocalPos().x(), meshNode->getLocalPos().y(), meshNode->getLocalPos().z());
    btRigidBody *body = Q_NULLPTR;

    switch (itemData) {
        case static_cast<int>(Shapes::NONE): {
            btTransform t;
            t.setIdentity();
            t.setFromOpenGLMatrix(meshNode->getLocalTransform().constData());
            t.setOrigin(pos);

            float mass = 0.0;
            float rad = 1.0;

            btCollisionShape *shape = new btEmptyShape();
            btMotionState *motion = new btDefaultMotionState(t);
            
            btVector3 inertia(0, 0, 0);

            if (mass != 0.0) shape->calculateLocalInertia(mass, inertia);
            
            btRigidBody::btRigidBodyConstructionInfo info(mass, motion, shape);
            body = new btRigidBody(info);

            break;
        }

        case static_cast<int>(Shapes::SPHERE) : {
            btTransform t;
            t.setIdentity();
            t.setOrigin(pos);
            
            float mass = 2.0;
            float rad = 1.0;
            
            btCollisionShape * sphere = new btSphereShape(rad);
            btMotionState * motion = new btDefaultMotionState(t);

            btVector3 inertia;
            
            if (mass != 0.0) sphere->calculateLocalInertia(mass, inertia);
            
            btRigidBody::btRigidBodyConstructionInfo info(mass, motion, sphere, inertia);
            body = new btRigidBody(info);
            body->setRestitution(1.f);

            break;
        }

        case static_cast<int>(Shapes::PLANE) : {
            btTransform t;
            t.setIdentity();
            t.setOrigin(pos);

            float mass = 0.0;

            btCollisionShape * plane = new btStaticPlaneShape(btVector3(0, 1, 0), 0.f);
            btMotionState * motion = new btDefaultMotionState(t);

            btRigidBody::btRigidBodyConstructionInfo info(mass, motion, plane);

            body = new btRigidBody(info);
            body->setRestitution(1.f);

            break;
        }

        case static_cast<int>(Shapes::CUBE) : {
            btTransform t;
            t.setIdentity();
            t.setOrigin(pos);

            float mass = 0.0;
            float rad = 1.0;

            btCollisionShape *cube = new btBoxShape(btVector3(1, 1, 1));
            btMotionState *motion = new btDefaultMotionState(t);

            btVector3 inertia;

            if (mass != 0.0) cube->calculateLocalInertia(mass, inertia);

            btRigidBody::btRigidBodyConstructionInfo info(mass, motion, cube, inertia);

            body = new btRigidBody(info);
            body->setRestitution(.2f);

            break;
        }

        case static_cast<int>(Shapes::HULL) : {
            break;
        }

        default: break; // can't happen
    }

    sceneView->removeBodyFromWorld(currentBody);
    sceneView->addBodyToWorld(body, meshNode->getGUID());
}

void PhysicsPropertyWidget::onPhysicsEnabled(bool value)
{
    btTransform t;
    t.setIdentity();
    t.setFromOpenGLMatrix(meshNode->getLocalTransform().constData());

    float mass = 0.0;

    btCollisionShape *shape = new btEmptyShape();
    btMotionState *motion = new btDefaultMotionState(t);

    btVector3 inertia(0, 0, 0);

    if (mass != 0.0) shape->calculateLocalInertia(mass, inertia);

    btRigidBody::btRigidBodyConstructionInfo info(mass, motion, shape, inertia);
    btRigidBody *body = new btRigidBody(info);
    
    this->meshNode->isPhysicsBody = value;
    this->sceneView->addBodyToWorld(body, meshNode->getGUID());
}

void PhysicsPropertyWidget::onStaticTypeChecked(bool value)
{
}