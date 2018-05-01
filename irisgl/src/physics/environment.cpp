#include "environment.h"
#include <QDebug>

namespace iris
{

Environment::Environment()
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
    world->setGravity(btVector3(0, -10, 0)); 
}

Environment::~Environment()
{

}

void Environment::addBodyToWorld(btRigidBody *body, const QString &guid) 
{ 
    world->addRigidBody(body); 
    //bodies.push_back(body); 
    hashBodies.insert(guid, body);

    qDebug() << "BODY ADDED TO WORLD " << guid;
} 

void Environment::removeBodyFromWorld(btRigidBody *body)
{
     world->removeRigidBody(body);
     //bodies.removeOne(body);
     hashBodies.remove(hashBodies.key(body)); // ???
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
	if (simulating) {
		world->stepSimulation(delta);
		qDebug() << delta;
	}
}

}