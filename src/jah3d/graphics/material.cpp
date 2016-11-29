#include "material.h"
#include <QOpenGLFunctions_3_2_Core>
#include "texture2d.h"

namespace jah3d
{

void Material::addTexture(QString name,Texture2DPtr texture)
{
    //remove texture if it already exist
    if(textures.contains(name))
    {
        auto tex = textures[name];
        textures.remove(name);
        //delete tex;
    }

    textures[name] = texture;

}

void Material::removeTexture(QString name)
{
    if(textures.contains(name))
    {
        textures.remove(name);
    }
}

void Material::bindTextures(QOpenGLFunctions_3_2_Core* gl)
{
    int i=0;
    for(auto it = textures.begin();it != textures.end();it++,i++)
    {
        auto tex = it.value();
        gl->glActiveTexture(GL_TEXTURE0+i);

        if(tex->texture!=nullptr)
        {
            tex->texture->bind();
            program->setUniformValue(it.key().toStdString().c_str(), i);
        }
        else
        {
            gl->glBindTexture(GL_TEXTURE_2D,0);
        }
    }
}

void Material::unbindTextures(QOpenGLFunctions_3_2_Core* gl)
{
    for(auto i=0;i<textures.size();i++)
    {
        gl->glActiveTexture(GL_TEXTURE0+i);
        gl->glBindTexture(GL_TEXTURE_2D,0);
    }
}

}
