#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include <QVector>
#include <QHash>

#include "../bullet3/src/btBulletDynamicsCommon.h"

namespace iris
{

class Environment
{
public:
    Environment();
    ~Environment();

    QHash<QString, btRigidBody*> hashBodies;

	void addBodyToWorld(btRigidBody *body, const QString &guid);
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