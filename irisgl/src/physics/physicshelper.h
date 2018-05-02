#ifndef PHYSICS_HELPER
#define PHYSICS_HELPER

#include "bullet3/src/BulletCollision/CollisionShapes/btConvexTriangleMeshShape.h"
#include "bullet3/src/BulletCollision/CollisionShapes/btTriangleMesh.h"

#include "graphics/mesh.h"
#include "irisglfwd.h"

namespace iris
{

class PhysicsHelper
{
public:
    PhysicsHelper() = default;
    static btTriangleMesh *btTriangleMeshShapeFromMesh(iris::MeshPtr mesh);
};

}

#endif // PHYSICS_HELPER