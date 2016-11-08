#ifndef GRAPHICSHELPER_H
#define GRAPHICSHELPER_H

#include <QString>

class QOpenGLShaderProgram;
namespace jah3d
{

class GraphicsHelper
{
public:
    static QOpenGLShaderProgram* loadShader(QString vsPath,QString fsPath);
};

}

#endif // GRAPHICSHELPER_H
