#ifndef SHADERPROGRAM_H
#define SHADERPROGRAM_H

#include <QSharedPointer>

namespace jah3d
{

typedef QSharedPointer<ShaderProgram> ShaderProgramPtr;

class ShaderProgram
{
public:
    ShaderProgramPtr load(QString vertexShaderFile,QString fragmentShaderFile);
    ShaderProgramPtr create(QString vertexShader,QString fragmentShader);
};

}

#endif // SHADERPROGRAM_H
