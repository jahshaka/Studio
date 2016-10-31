#ifndef MATERIAL_H
#define MATERIAL_H

#include "shaderprogram.h"
#include "texture.h"
#include <QSharedPointer>

class QOpenGLShaderProgram;

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

    void bind();
    void unbind();
};

}

#endif // MATERIAL_H
