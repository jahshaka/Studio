#include "graphicshelper.h"
#include <QOpenGLShaderProgram>

namespace jah3d
{

QOpenGLShaderProgram* GraphicsHelper::loadShader(QString vsPath,QString fsPath)
{
    QOpenGLShader *vshader = new QOpenGLShader(QOpenGLShader::Vertex);
    vshader->compileSourceFile(vsPath);

    QOpenGLShader *fshader = new QOpenGLShader(QOpenGLShader::Fragment);
    fshader->compileSourceFile(fsPath);

    auto program = new QOpenGLShaderProgram;
    program->addShader(vshader);
    program->addShader(fshader);
    program->link();

    //todo: check for errors

    return program;
}

}
