#ifndef MESH_H
#define MESH_H

#include <QString>
#include <QSharedPointer>
#include <qopengl.h>

class aiMesh;
class QOpenGLBuffer;
class QOpenGLFunctions_3_2_Core;
class QOpenGLShaderProgram;


namespace jah3d
{

//class DefaultMaterial;
class Material;
class VertexLayout;

class Mesh
{
    //friend class jah3d::MeshNode;
public:
    QOpenGLBuffer* vbo;
    VertexLayout* vertexLayout;
    int numVerts;
    int numFaces;

    void draw(QOpenGLFunctions_3_2_Core* gl,Material* mat);
    void draw(QOpenGLFunctions_3_2_Core* gl,QOpenGLShaderProgram* mat);
    void draw(QOpenGLFunctions_3_2_Core* gl,QOpenGLShaderProgram* mat,GLenum primitiveMode);

    static Mesh* loadMesh(QString filePath);

    //assumes mesh is triangulated
    //makes copy of data
    //assumed ownership of vertexLayout
    static Mesh* create(void* data,int dataSize,int numElements,VertexLayout* vertexLayout);

    Mesh(aiMesh* mesh,VertexLayout* vertexLayout);
    Mesh(void* data,int dataSize,int numElements,VertexLayout* vertexLayout);
};

typedef QSharedPointer<Mesh> MeshPtr;

}

#endif // MESH_H
