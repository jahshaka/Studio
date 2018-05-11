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
#include "../irisgl/src/materials/colormaterial.h"

class btTypedConstraint;

namespace iris
{

class GLDebugDrawer : public btIDebugDraw
{
    int m_debugMode;
    iris::RenderList *renderList;
    iris::MaterialPtr lineMat;

    iris::LineMeshBuilder *builder;

public:
    GLDebugDrawer() {
        lineMat = iris::ColorMaterial::create();
        lineMat.staticCast<iris::ColorMaterial>()->setColor(QColor(10, 255, 20));
    }

    virtual ~GLDebugDrawer() {}

    void setPublicBuilder(iris::LineMeshBuilder *builder) {
        this->builder = builder;
    }

    virtual void   drawLine(const btVector3& from, const btVector3& to, const btVector3& fromColor, const btVector3& toColor) {
        builder->addLine(QVector3D(from.x(), from.y(), from.z()), QVector3D(to.x(), to.y(), to.z()));
    }

    virtual void   drawLine(const btVector3& from, const btVector3& to, const btVector3& color)
    {
        builder->addLine(QVector3D(from.x(), from.y(), from.z()), QVector3D(to.x(), to.y(), to.z()));
    }

    virtual void   drawSphere(const btVector3& p, btScalar radius, const btVector3& color) {}
    virtual void   drawTriangle(const btVector3& a, const btVector3& b, const btVector3& c, const btVector3& color, btScalar alpha) {}
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
    QVector<btTypedConstraint*> constraints;

	void addBodyToWorld(btRigidBody *body, const QString &guid);
	void removeBodyFromWorld(btRigidBody *body);

    void addConstraintToWorld(btTypedConstraint *constraint, bool disableCollisions = true);
    void removeConstraintFromWorld(btTypedConstraint *constraint);

    btDynamicsWorld *getWorld();

    // These are special functions used for creating a constraint to drag bodies
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

    iris::MaterialPtr lineMat;
    iris::RenderList *debugRenderList;

    GLDebugDrawer *debugDrawer;
};

}

#endif // ENVIRONMENT_H