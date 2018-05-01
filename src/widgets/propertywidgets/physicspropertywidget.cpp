#include "physicspropertywidget.h"

#include "irisgl/src/scenegraph/meshnode.h"
#include "../sceneviewwidget.h"
#include "globals.h"
#include "../checkboxwidget.h"
#include "../comboboxwidget.h"

#include "irisgl/src/bullet3/src/btBulletDynamicsCommon.h"

PhysicsPropertyWidget::PhysicsPropertyWidget()
{
    isPhysicsObject = this->addCheckBox("Physics Object", false);
    isStaticObject = this->addCheckBox("Static", false);

    shapeSelector = this->addComboBox("Collision Shape");
    shapeSelector->addItem("None");
    shapeSelector->addItem("Plane");
    shapeSelector->addItem("Sphere");
    shapeSelector->addItem("Cube");
    shapeSelector->addItem("Object Hull");

    connect(isPhysicsObject, SIGNAL(valueChanged(bool)), SLOT(onPhysicsEnabled(bool)));
    connect(isStaticObject, SIGNAL(valueChanged(bool)), SLOT(onStaticTypeChecked(bool)));
    connect(shapeSelector, SIGNAL(currentIndexChanged(QString)), this, SLOT(onPhysicsTypeChanged(QString)));
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

void PhysicsPropertyWidget::onPhysicsTypeChanged(const QString &type)
{
}

void PhysicsPropertyWidget::onPhysicsEnabled(bool value)
{
    // Initialize a body using the object's current transform
    btTransform t;
    t.setIdentity();
    t.setFromOpenGLMatrix(meshNode->getLocalTransform().constData());
    //t.setOrigin(_pos);

    // Set the properties
    float mass = 2.0;
    float rad = 1.0;

    // Create a collision shape ... 
    btSphereShape *sphere = new btSphereShape(rad);
    btMotionState *motion = new btDefaultMotionState(t);
    btVector3 inertia(0, 0, 0);

    // If mass is valid, calculate local inertia
    if (mass != 0.0) sphere->calculateLocalInertia(mass, inertia);

    btRigidBody::btRigidBodyConstructionInfo info(mass, motion, sphere, inertia);
    btRigidBody *body = new btRigidBody(info);

    body->setMotionState(motion);
    
    this->meshNode->isPhysicsBody = value;
    this->sceneView->addBodyToWorld(body, meshNode->getGUID());
}

void PhysicsPropertyWidget::onStaticTypeChecked(bool value)
{
}