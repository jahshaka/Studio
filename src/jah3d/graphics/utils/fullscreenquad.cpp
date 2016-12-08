#include "fullscreenquad.h"
#include <QVector>
#include "../mesh.h"
#include "../graphicshelper.h"
#include "../vertexlayout.h"
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>

namespace jah3d
{

FullScreenQuad::FullScreenQuad()
{
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

    auto layout = new VertexLayout();
    layout->addAttrib("a_pos",GL_FLOAT,3,sizeof(GLfloat)*3);
    layout->addAttrib("a_texCoord",GL_FLOAT,2,sizeof(GLfloat)*2);

    mesh = new jah3d::Mesh(data.data(),data.size()*sizeof(float),6,layout);

    //todo: inline shader in code
    shader = GraphicsHelper::loadShader("app/shaders/fullscreen.vert","app/shaders/fullscreen.frag");
}

FullScreenQuad::~FullScreenQuad()
{
    delete mesh;
}

void FullScreenQuad::draw(QOpenGLFunctions_3_2_Core* gl)
{
    shader->bind();
    mesh->draw(gl,shader);
}

}
