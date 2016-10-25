#ifndef MESH_H
#define MESH_H

#include <QString>
#include <QSharedPointer>

class AdvanceMaterial;
class Material;
class aiMesh;
class QOpenGLBuffer;
class QOpenGLFunctions;
class QOpenGLShaderProgram;

namespace jah3d
{

typedef QSharedPointer<Mesh> MeshPtr;

class Mesh
{
    QOpenGLBuffer* vbo;
    int numVerts;
    int numFaces;
public:

    void draw(QOpenGLFunctions* gl,Material* mat);
    void draw(QOpenGLFunctions* gl,QOpenGLShaderProgram* mat);

    static Mesh* loadMesh(QString filePath);
};

}

#endif // MESH_H
