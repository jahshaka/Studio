#include <QOpenGLFunctions_3_2_Core>
#include "billboard.h"
#include <qopengl.h>
#include <QOpenGLShader>
#include <QOpenGLBuffer>

namespace jah3d
{

Billboard::Billboard(QOpenGLFunctions_3_2_Core* gl)
{
    //todo: write shaders in script
    QOpenGLShader *vshader = new QOpenGLShader(QOpenGLShader::Vertex);
    vshader->compileSourceFile("app/shaders/billboard.vert");

    QOpenGLShader *fshader = new QOpenGLShader(QOpenGLShader::Fragment);
    fshader->compileSourceFile("app/shaders/billboard.frag");

    program = new QOpenGLShaderProgram;
    program->addShader(vshader);
    program->addShader(fshader);
    program->bindAttributeLocation("a_pos", 0);
    program->bindAttributeLocation("a_texCoord", 1);
    program->link();

    program->bind();

    //build fbo
    QVector<float> data;

    //TRIANGLE 1
    data.append(-1);data.append(-1);data.append(0);
    data.append(0);data.append(0);

    data.append(1);data.append(-1);data.append(0);
    data.append(1);data.append(0);

    data.append(-1);data.append(1);data.append(0);
    data.append(0);data.append(1);

    //TRIANGLE 2
    data.append(-1);data.append(1);data.append(0);
    data.append(0);data.append(1);

    data.append(1);data.append(-1);data.append(0);
    data.append(1);data.append(0);

    data.append(1);data.append(1);data.append(0);
    data.append(1);data.append(1);

    vbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    vbo->create();
    vbo->bind();
    vbo->allocate(data.constData(), data.count() * sizeof(GLfloat));

}

void Billboard::draw(QOpenGLFunctions_3_2_Core* gl)
{
    vbo->bind();

    auto stride = (3+2) * sizeof(GLfloat);

    program->enableAttributeArray(0);
    program->setAttributeBuffer(0, GL_FLOAT, 0, 3, stride);

    program->enableAttributeArray(1);
    program->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(GLfloat), 2, stride);

    gl->glDrawArrays(GL_TRIANGLES,0,6);\

}

}
