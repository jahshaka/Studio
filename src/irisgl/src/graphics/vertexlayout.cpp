#include "vertexlayout.h"
#include "shader.h"

namespace iris
{

VertexLayout::VertexLayout()
{
    stride = 0;
}

void VertexLayout::addAttrib(QString name,int type,int count,int sizeInBytes)
{
    VertexAttribute attrib = {attribs.size(),name,type,count,sizeInBytes};
    attribs.append(attrib);

    stride += sizeInBytes;
}

//todo: make this more efficient
void VertexLayout::bind(QOpenGLShaderProgram* program)
{
    int offset = 0;
    for(auto attrib: attribs)
    {
        attrib.loc = program->attributeLocation(attrib.name);//todo: do this once per shader
        program->enableAttributeArray(attrib.loc);
        //program->bindAttributeLocation(attrib.name,loc);// slow! causes shader relinking!

        program->setAttributeBuffer(attrib.loc, attrib.type, offset, attrib.count,stride);
        offset += attrib.sizeInBytes;
    }
}

void VertexLayout::unbind(QOpenGLShaderProgram* program)
{
    for(auto attrib: attribs)
    {
        program->disableAttributeArray(attrib.loc);
    }
}

void VertexLayout::bind(ShaderPtr shader)
{
    auto program = shader->program;
    int offset = 0;
    for(auto attrib: attribs)
    {
        if(!shader->attribs.contains(attrib.name))
        {
            offset += attrib.sizeInBytes;
            continue;
        }

        auto shaderAttrib = shader->attribs[attrib.name];

        attrib.loc = shaderAttrib->location;//todo: do this once per shader
        program->enableAttributeArray(attrib.loc);

        program->setAttributeBuffer(attrib.loc, attrib.type, offset, attrib.count,this->stride);
        offset += attrib.sizeInBytes;
    }
}

void VertexLayout::unbind(ShaderPtr shader)
{
    auto program = shader->program;
    for(auto attrib: attribs)
    {
        program->disableAttributeArray(attrib.loc);
    }
}

//default vertex layout for meshes
VertexLayout* VertexLayout::createMeshDefault()
{
    auto layout = new VertexLayout();

    layout->addAttrib("a_pos",GL_FLOAT,3,sizeof(GLfloat)*3);
    layout->addAttrib("a_texCoord",GL_FLOAT,2,sizeof(GLfloat)*2);
    layout->addAttrib("a_normal",GL_FLOAT,3,sizeof(GLfloat)*3);
    layout->addAttrib("a_tangent",GL_FLOAT,3,sizeof(GLfloat)*3);

    return layout;
}

}
