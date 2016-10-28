#ifndef MATERIAL_H
#define MATERIAL_H

#include "shaderprogram.h"
#include "texture.h"

namespace jah3d
{

typedef QSharedPointer<Material> MaterialPtr;

struct MaterialTexture
{
    QOpenGLTexture* texture;
    QString name;
};

class Material
{
public:
    ShaderProgramPtr program;
    //QMap<QString, TexturePtr> textures;
    QList<MaterialTexture*> textures;

    void bind();
    void unbind();
};

}

#endif // MATERIAL_H
