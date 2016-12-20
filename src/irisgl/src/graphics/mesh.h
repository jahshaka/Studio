#ifndef MESH_H
#define MESH_H

#include <QString>
#include <QSharedPointer>
#include <qopengl.h>

class aiMesh;
class QOpenGLBuffer;
class QOpenGLFunctions_3_2_Core;
class QOpenGLShaderProgram;


namespace iris
{

//class DefaultMaterial;
class Material;
class VertexLayout;
class TriMesh;

class Mesh
{
    //friend class iris::MeshNode;
public:
    QOpenGLBuffer* vbo;
    VertexLayout* vertexLayout;
    int numVerts;
    int numFaces;

    TriMesh* triMesh;
    TriMesh* getTriMesh()
    {
        return triMesh;
    }

    void draw(QOpenGLFunctions_3_2_Core* gl,Material* mat);
    void draw(QOpenGLFunctions_3_2_Core* gl,QOpenGLShaderProgram* mat);
    void draw(QOpenGLFunctions_3_2_Core* gl,QOpenGLShaderProgram* mat,GLenum primitiveMode);

    static Mesh* loadMesh(QString filePath);

    //assumes mesh is triangulated
    //makes copy of data
    //assumed ownership of vertexLayout
    static Mesh* create(void* data,int dataSize,int numElements,VertexLayout* vertexLayout);

    Mesh(aiMesh* mesh,VertexLayout* vertexLayout);

    /**
     *
     * @param data
     * @param dataSize
     * @param numElements number of vertices
     * @param vertexLayout
     */
    Mesh(void* data,int dataSize,int numElements,VertexLayout* vertexLayout);

    ~Mesh();
};

typedef QSharedPointer<Mesh> MeshPtr;

}

#endif // MESH_H
