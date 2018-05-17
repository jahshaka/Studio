#include "environment.h"
#include <QDebug>

namespace iris
{

Environment::Environment(iris::RenderList *debugList)
{
    collisionConfig = new btDefaultCollisionConfiguration(); 
    dispatcher      = new btCollisionDispatcher(collisionConfig); 
    broadphase      = new btDbvtBroadphase(); 
    solver          = new btSequentialImpulseConstraintSolver(); 
    world           = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collisionConfig); 
 
    simulating = false;

    bodies.reserve(512); // for now
    hashBodies.reserve(512); // also for now

    // TODO - use constants file and make this changeable
    world->setGravity(btVector3(0, -10.f, 0)); 

    debugRenderList = debugList;
    lineMat = iris::LineColorMaterial::create();
    lineMat.staticCast<iris::LineColorMaterial>()->setDepthBias(10.f);

    debugDrawer = new GLDebugDrawer;
    debugDrawer->setDebugMode(
        GLDebugDrawer::DBG_DrawAabb |
        GLDebugDrawer::DBG_DrawWireframe |
        GLDebugDrawer::DBG_DrawConstraints |
        GLDebugDrawer::DBG_DrawContactPoints |
        GLDebugDrawer::DBG_DrawConstraintLimits |
        GLDebugDrawer::DBG_DrawFrames
    );
    world->setDebugDrawer(debugDrawer);
}

Environment::~Environment()
{
    delete world;
    delete solver;
    delete broadphase;
    delete dispatcher;
    delete collisionConfig;
}

void Environment::addBodyToWorld(btRigidBody *body, const QString &guid) 
{ 
    world->addRigidBody(body); 
    //bodies.push_back(body); 
    hashBodies.insert(guid, body);

    qDebug() << "BODY ADDED TO WORLD " << guid;
    qDebug() << "There are " << hashBodies.size() << " bodies ";
} 

void Environment::removeBodyFromWorld(btRigidBody *body)
{
    if (!hashBodies.contains(hashBodies.key(body))) return;

    world->removeRigidBody(body);
    hashBodies.remove(hashBodies.key(body)); // ???

    qDebug() << "BODY REMOVED TO WORLD " ;
    qDebug() << "There are " << hashBodies.size() << " bodies ";
}

void Environment::removeBodyFromWorld(const QString &guid)
{
    if (!hashBodies.contains(guid)) return;

    world->removeRigidBody(hashBodies.value(guid));
    hashBodies.remove(guid);

    qDebug() << "BODY REMOVED TO WORLD ";
    qDebug() << "There are " << hashBodies.size() << " bodies ";
}

void Environment::addConstraintToWorld(btTypedConstraint *constraint, bool disableCollisions)
{
    world->addConstraint(constraint, disableCollisions);
    constraints.append(constraint);
}

void Environment::removeConstraintFromWorld(btTypedConstraint *constraint)
{
    for (int i = 0; i < constraints.size(); ++i) {
        if (constraints[i] == constraint) {
            constraints.erase(constraints.begin() + i);
            world->removeConstraint(constraint);
            break;
        }
    }
}

btDynamicsWorld *Environment::getWorld()
{
    return world;
}

void Environment::simulatePhysics()
{
    simulating = true;
}

void Environment::stopPhysics()
{
	simulating = false;
}

void Environment::stepSimulation(float delta)
{
    iris::LineMeshBuilder builder; // *must* go out of scope...
    debugDrawer->setPublicBuilder(&builder);

    if (simulating) {
        world->stepSimulation(delta);
        world->debugDrawWorld();
    }

    QMatrix4x4 transform;
    transform.setToIdentity();
    debugRenderList->submitMesh(builder.build(), lineMat, transform);
}

}