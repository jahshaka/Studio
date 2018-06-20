#ifndef PHYSICS_PROPERTIES
#define PHYSICS_PROPERTIES

namespace iris
{
    enum class PhysicsType : int
{
    None,
    Static,
    RigidBody,
    SoftBody
};

enum class PhysicsCollisionShape : int
{
    None,
    Plane,
    Sphere,
    Cube,
    ConvexHull,
    TriangleMesh
};

enum class PhysicsConstraintType : int
{
    None,
    Ball,
    Dof6
};

struct ConstraintProperty
{
    ConstraintProperty() {
        constraintType = PhysicsConstraintType::None;
    }

    QString constraintFrom;
    QString constraintTo;
    PhysicsConstraintType constraintType;
};

struct PhysicsProperty
{
    // Fairly sane defaults...
    PhysicsProperty() {
        objectMass = 1.f;
        objectRestitution = .1f;
        objectDamping = .1f;
        objectCollisionMargin = .1f;
        isVisible = true;
        isStatic = false;
        shape = PhysicsCollisionShape::None;
        type = PhysicsType::None;
    }

    float objectMass;
    float objectRestitution;
    float objectDamping;
    float objectCollisionMargin;
    bool isVisible;
    bool isStatic;
    PhysicsCollisionShape shape;
    PhysicsType type;
    QVector3D centerOfMass;
    QVector3D pivotPoint;

    QVector<ConstraintProperty> constraints;
};

}

#endif