#ifndef GRAPHICSHELPER_H
#define GRAPHICSHELPER_H

#include <QString>
#include <QList>

class QOpenGLShaderProgram;


namespace jah3d
{
class Mesh;
class GraphicsHelper
{
public:
    static QOpenGLShaderProgram* loadShader(QString vsPath,QString fsPath);

    /**
     * Loads all meshes from mesh file
     * Useful for loading a mesh file containing multiple meshes
     * Caller is responsible for releasing returned Mesh pointers
     * @param filePath
     * @return
     */
    static QList<Mesh*> loadAllMeshesFromFile(QString filePath);
};

}

#endif // GRAPHICSHELPER_H
