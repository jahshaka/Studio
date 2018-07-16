#include "environment.h"
#include <QDebug>

namespace iris
{

Environment::Environment(iris::RenderList *debugList)
{
    createPhysicsWorld();
 
    simulating = false;

    debugRenderList = debugList;
    lineMat = iris::LineColorMaterial::create();
    lineMat.staticCast<iris::LineColorMaterial>()->setDepthBias(10.f);
}

Environment::~Environment()
{
    destroyPhysicsWorld();
}

void Environment::addBodyToWorld(btRigidBody *body, const iris::SceneNodePtr &node) 
{ 
    world->addRigidBody(body); 

    hashBodies.insert(node->getGUID(), body);
    nodeTransforms.insert(node->getGUID(), node->getGlobalTransform());
} 

void Environment::removeBodyFromWorld(btRigidBody *body)
{
    if (!hashBodies.contains(hashBodies.key(body))) return;

    world->removeRigidBody(body);
    hashBodies.remove(hashBodies.key(body));
    nodeTransforms.remove(hashBodies.key(body));
}

void Environment::removeBodyFromWorld(const QString &guid)
{
    if (!hashBodies.contains(guid)) return;

    world->removeRigidBody(hashBodies.value(guid));
    hashBodies.remove(guid);
    nodeTransforms.remove(guid);
}

void Environment::storeCollisionShape(btCollisionShape *shape)
{
    collisionShapes.push_back(shape);
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
    simulationStarted = true;
}

bool Environment::isSimulating()
{
    return simulationStarted;
}

void Environment::stopPhysics()
{
    // this is the original, we also want to be able to pause as well
    // to "restart" a sim we have to cleanup and recreate it from scratch basically...
	//simulating = false;
	simulating = false;
}

void Environment::stopSimulation()
{
    simulationStarted = false;
}

void Environment::stepSimulation(float delta)
{
    iris::LineMeshBuilder builder; // *must* go out of scope...
    debugDrawer->setPublicBuilder(&builder);

    if (simulating) {
        world->stepSimulation(delta);
    }

    world->debugDrawWorld();

    QMatrix4x4 transform;
    transform.setToIdentity();
    debugRenderList->submitMesh(builder.build(), lineMat, transform);
}

void Environment::toggleDebugDrawFlags(bool state)
{
    if (!state) {
        debugDrawer->setDebugMode(GLDebugDrawer::DBG_NoDebug);
    }
    else {
        debugDrawer->setDebugMode(
            GLDebugDrawer::DBG_DrawAabb |
            GLDebugDrawer::DBG_DrawWireframe |
            GLDebugDrawer::DBG_DrawConstraints |
            GLDebugDrawer::DBG_DrawContactPoints |
            GLDebugDrawer::DBG_DrawConstraintLimits |
            GLDebugDrawer::DBG_DrawFrames
        );
    }
}

void Environment::restartPhysics()
{
    // node transforms are reset inside button caller
    stopPhysics();
    stopSimulation();

    destroyPhysicsWorld();
    createPhysicsWorld();
}

void Environment::createPhysicsWorld()
{
    collisionConfig = new btDefaultCollisionConfiguration();
    dispatcher = new btCollisionDispatcher(collisionConfig);
    broadphase = new btDbvtBroadphase();
    solver = new btSequentialImpulseConstraintSolver();
    world = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, collisionConfig);

    hashBodies.reserve(512);
    nodeTransforms.reserve(512);

    world->setGravity(btVector3(0, -10.f, 0));

    // http://bulletphysics.org/mediawiki-1.5.8/index.php/Bullet_Debug_drawer
    debugDrawer = new GLDebugDrawer;
    debugDrawer->setDebugMode(GLDebugDrawer::DBG_NoDebug);
    world->setDebugDrawer(debugDrawer);
}

void Environment::destroyPhysicsWorld()
{
    //removePickingConstraint();

    // this is rougly verbose the same thing as the exitPhysics() function in the bullet demos
    if (world) {
        int i;
        for (i = world->getNumConstraints() - 1; i >= 0; i--) {
            world->removeConstraint(world->getConstraint(i));
        }

        for (i = world->getNumCollisionObjects() - 1; i >= 0; i--) {
            btCollisionObject* obj = world->getCollisionObjectArray()[i];
            btRigidBody* body = btRigidBody::upcast(obj);
            if (body && body->getMotionState()) {
                delete body->getMotionState();
            }
            world->removeCollisionObject(obj);
            delete obj;
        }

        // https://pybullet.org/Bullet/phpBB3/viewtopic.php?t=8148#p28087
        btOverlappingPairCache* pair_cache = world->getBroadphase()->getOverlappingPairCache();
        btBroadphasePairArray& pair_array = pair_cache->getOverlappingPairArray();
        for (int i = 0; i < pair_array.size(); i++)
            pair_cache->cleanOverlappingPair(pair_array[i], world->getDispatcher());
    }

    // delete collision shapes
    for (int j = 0; j < collisionShapes.size(); j++) {
        btCollisionShape* shape = collisionShapes[j];
        delete shape;
    }

    collisionShapes.clear();

    delete world;
    world = 0;

    delete solver;
    solver = 0;

    delete broadphase;
    broadphase = 0;

    delete dispatcher;
    dispatcher = 0;

    delete collisionConfig;
    collisionConfig = 0;

    hashBodies.squeeze();
    nodeTransforms.squeeze();
}

}