#include "material.h"
#include <QOpenGLFunctions_3_2_Core>
#include "texture2d.h"

namespace jah3d
{

void Material::begin(QOpenGLFunctions_3_2_Core* gl)
{
    this->program->bind();
    this->bindTextures(gl);
}


void Material::end(QOpenGLFunctions_3_2_Core* gl)
{
    this->unbindTextures(gl);
}

void Material::addTexture(QString name,Texture2DPtr texture)
{
    //remove texture if it already exist
    if(textures.contains(name))
    {
        auto tex = textures[name];
        textures.remove(name);
        //delete tex;
    }

    textures.insert(name,texture);

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
    int count=0;
    for(auto it = textures.begin();it != textures.end();it++,count++)
    {
        auto tex = it.value();
        gl->glActiveTexture(GL_TEXTURE0+count);

        if(tex->texture!=nullptr)
        {
            tex->texture->bind();
            program->setUniformValue(it.key().toStdString().c_str(), count);
        }
        else
        {
            gl->glBindTexture(GL_TEXTURE_2D,0);
        }
    }

    //bind the rest of the textures to 0
    for(;count<numTextures;count++)
    {
        gl->glActiveTexture(GL_TEXTURE0+count);
        gl->glBindTexture(GL_TEXTURE_2D,0);
    }

}

void Material::unbindTextures(QOpenGLFunctions_3_2_Core* gl)
{
    for(auto i=0;i<numTextures;i++)
    {
        gl->glActiveTexture(GL_TEXTURE0+i);
        gl->glBindTexture(GL_TEXTURE_2D,0);
    }
}

void Material::createProgramFromShaderSource(QString vsFile,QString fsFile)
{
    QOpenGLShader *vshader = new QOpenGLShader(QOpenGLShader::Vertex);
    vshader->compileSourceFile(vsFile);

    QOpenGLShader *fshader = new QOpenGLShader(QOpenGLShader::Fragment);
    fshader->compileSourceFile(fsFile);

    program = new QOpenGLShaderProgram;
    program->addShader(vshader);
    program->addShader(fshader);

    program->link();
}

void Material::setTextureCount(int count)
{
    numTextures = count;
}

}
