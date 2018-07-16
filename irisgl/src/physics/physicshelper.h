#ifndef PHYSICS_HELPER
#define PHYSICS_HELPER

#include "bullet3/src/btBulletDynamicsCommon.h"
#include "bullet3/src/BulletCollision/CollisionShapes/btConvexHullShape.h"
#include "bullet3/src/BulletCollision/CollisionShapes/btShapeHull.h"
#include "bullet3/src/BulletCollision/CollisionShapes/btConvexTriangleMeshShape.h"
#include "bullet3/src/BulletCollision/CollisionShapes/btConvexHullShape.h"
#include "bullet3/src/BulletCollision/CollisionShapes/btTriangleMesh.h"

#include "graphics/mesh.h"
#include "physics/physicsproperties.h"
#include "scenegraph/scenenode.h"
#include "scenegraph/meshnode.h"
#include "irisglfwd.h"

namespace iris
{

class Environment;

class PhysicsHelper
{
public:
    PhysicsHelper() = default;
    static btTriangleMesh *btTriangleMeshShapeFromMesh(iris::MeshPtr mesh);
    static btConvexHullShape *btConvexHullShapeFromMesh(iris::MeshPtr mesh);
    static btVector3 btVector3FromQVector3D(QVector3D vector);
    static btRigidBody *createPhysicsBody(const iris::SceneNodePtr sceneNode, const iris::PhysicsProperty &props);
    static btTypedConstraint *createConstraintFromProperty(QSharedPointer<Environment> environment, const iris::ConstraintProperty &prop);
};

}

#endif // PHYSICS_HELPER