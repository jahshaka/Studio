#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include <QVector>
#include <QHash>

#include "bullet3/src/btBulletDynamicsCommon.h"
#include "bullet3/src/LinearMath/btIDebugDraw.h"

#include "../irisgl/src/graphics/utils/linemeshbuilder.h"
#include "../irisgl/src/physics/physicshelper.h"
#include "../irisgl/src/graphics/utils/linemeshbuilder.h"
#include "../irisgl/src/graphics/renderlist.h"
#include "../irisgl/src/graphics/renderitem.h"
#include "../irisgl/src/materials/linecolormaterial.h"

class btTypedConstraint;

namespace iris
{

class GLDebugDrawer : public btIDebugDraw
{
    int m_debugMode;
    iris::RenderList *renderList;
    iris::LineMeshBuilder *builder;

public:
    GLDebugDrawer() {}

    virtual ~GLDebugDrawer() {}

    void setPublicBuilder(iris::LineMeshBuilder *builder) {
        this->builder = builder;
    }

    virtual void   drawLine(const btVector3& from, const btVector3& to, const btVector3& fromColor, const btVector3& toColor) {
        builder->addLine(
            QVector3D(from.x(), from.y(), from.z()),
            QColor(fromColor.getX() * 255.f, fromColor.getY() * 255.f, fromColor.getZ() * 255.f),
            QVector3D(to.x(), to.y(), to.z()),
            QColor(toColor.getX() * 255.f, toColor.getY() * 255.f, toColor.getZ() * 255.f)
        );
    }

    virtual void   drawLine(const btVector3& from, const btVector3& to, const btVector3& color)
    {
        builder->addLine(
            QVector3D(from.x(), from.y(), from.z()),
            QColor(color.getX() * 255.f, color.getY() * 255.f, color.getZ() * 255.f),
            QVector3D(to.x(), to.y(), to.z()),
            QColor(color.getX() * 255.f, color.getY() * 255.f, color.getZ() * 255.f)
        );
    }

    virtual void   drawSphere(const btVector3& p, btScalar radius, const btVector3& color) {}
    virtual void   drawTriangle(const btVector3& a, const btVector3& b, const btVector3& c, const btVector3& color, btScalar alpha)
    {

    }
    virtual void   drawContactPoint(const btVector3& PointOnB, const btVector3& normalOnB, btScalar distance, int lifeTime, const btVector3& color) {}
    virtual void   reportErrorWarning(const char* warningString) {}
    virtual void   draw3dText(const btVector3& location, const char* textString) {

    }
    virtual void   setDebugMode(int debugMode) {
        m_debugMode = debugMode;
    }
    virtual int    getDebugMode() const { return m_debugMode; }
};

class Environment
{
public:
    Environment(iris::RenderList *renderList);
    ~Environment();

    QHash<QString, btRigidBody*> hashBodies;
    QHash<QString, QMatrix4x4> nodeTransforms;

	void addBodyToWorld(btRigidBody *body, const iris::SceneNodePtr &node);
	void removeBodyFromWorld(btRigidBody *body);
	void removeBodyFromWorld(const QString &guid);

    void storeCollisionShape(btCollisionShape *shape);

    void addConstraintToWorld(btTypedConstraint *constraint, bool disableCollisions = true);
    void removeConstraintFromWorld(btTypedConstraint *constraint);

    btDynamicsWorld *getWorld();

    // These are special functions used for creating a constraint to drag bodies
	void simulatePhysics();
	bool isSimulating();
	void stopPhysics();
	void stopSimulation();
	void stepSimulation(float delta);
    void toggleDebugDrawFlags(bool state = false);

    void restartPhysics();
    void createPhysicsWorld();
    void destroyPhysicsWorld();

private:
    btCollisionConfiguration    *collisionConfig;
    btDispatcher                *dispatcher;
    btBroadphaseInterface       *broadphase;
    btConstraintSolver          *solver;
    btDynamicsWorld             *world;

    QVector<btTypedConstraint*> constraints;
    btAlignedObjectArray<btCollisionShape*>	collisionShapes;

    bool simulating;
    bool simulationStarted;

    iris::MaterialPtr lineMat;
    iris::RenderList *debugRenderList;

    GLDebugDrawer *debugDrawer;
};

}

#endif // ENVIRONMENT_H