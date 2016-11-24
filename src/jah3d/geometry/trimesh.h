#ifndef TRIMESH_H
#define TRIMESH_H

#include <QVector3D>

namespace jah3d
{

class Triangle
{
public:
    QVector3D a,b,c;
    QVector3D normal;
};

class TriMesh
{
public:
    QList<Triangle> triangles;
};


}

#endif // TRIMESH_H
