#ifndef SHADERPROGRAM_H
#define SHADERPROGRAM_H

#include <QSharedPointer>

namespace iris
{

class ShaderProgram;
typedef QSharedPointer<ShaderProgram> ShaderProgramPtr;

class ShaderProgram
{
public:
    static ShaderProgramPtr load(QString vertexShaderFile,QString fragmentShaderFile);
    static ShaderProgramPtr create(QString vertexShader,QString fragmentShader);
};

}

#endif // SHADERPROGRAM_H
