#ifndef MESH_H
#define MESH_H

#include <QString>
#include <QSharedPointer>

class aiMesh;
class QOpenGLBuffer;
class QOpenGLFunctions;
class QOpenGLShaderProgram;


namespace jah3d
{

//class DefaultMaterial;
class Material;
class VertexLayout;

class Mesh
{
public:
    QOpenGLBuffer* vbo;
    VertexLayout* vertexLayout;
    int numVerts;
    int numFaces;



    void draw(QOpenGLFunctions* gl,Material* mat);
    void draw(QOpenGLFunctions* gl,QOpenGLShaderProgram* mat);

    static Mesh* loadMesh(QString filePath);

private:
    Mesh(aiMesh* mesh,VertexLayout* vertexLayout);
};

typedef QSharedPointer<Mesh> MeshPtr;

}

#endif // MESH_H
