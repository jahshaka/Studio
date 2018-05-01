#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include <QVector>

#include "../bullet3/src/btBulletDynamicsCommon.h"

namespace iris
{

class Environment
{
public:
    Environment();
    ~Environment();

	void addBodyToWorld(btRigidBody *body);
	void removeBodyFromWorld(btRigidBody *body);
	void simulatePhysics();
	void stopPhysics();
	void stepSimulation(float delta);

private:
    btCollisionConfiguration    *collisionConfig;
    btDispatcher                *dispatcher;
    btBroadphaseInterface       *broadphase;
    btConstraintSolver          *solver;
    btDynamicsWorld             *world;

    QVector<btRigidBody*> bodies;
    bool simulating;
};

}

#endif // ENVIRONMENT_H