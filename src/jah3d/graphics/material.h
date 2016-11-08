#ifndef MATERIAL_H
#define MATERIAL_H

//#include "shaderprogram.h"
//#include "texture.h"
#include <QSharedPointer>
#include <QOpenGLShaderProgram>

class QOpenGLShaderProgram;
class QOpenGLTexture;
class QOpenGLFunctions;

namespace jah3d
{

class Material;
typedef QSharedPointer<Material> MaterialPtr;

struct MaterialTexture
{
    QOpenGLTexture* texture;
    QString name;
};

class Material
{
public:
    //ShaderProgramPtr program;
    QOpenGLShaderProgram* program;
    //QMap<QString, TexturePtr> textures;
    QList<MaterialTexture*> textures;

    bool acceptsLighting;

    Material()
    {
        acceptsLighting = true;
    }

    //void bind();
    //void unbind();

    virtual void begin(QOpenGLFunctions* gl)
    {

    }

    virtual void end()
    {

    }

    template<typename T>
    void setUniformValue(QString name,T value)
    {
        program->setUniformValue(name.toStdString().c_str(), value);
    }
};

}

#endif // MATERIAL_H
