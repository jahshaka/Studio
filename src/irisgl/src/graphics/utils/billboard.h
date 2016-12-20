#ifndef BILLBOARD_H
#define BILLBOARD_H

class QOpenGLFunctions_3_2_Core;
class QOpenGLBuffer;
class QOpenGLShaderProgram;

namespace iris
{

class Billboard
{
    friend class ForwardRenderer;

    QOpenGLBuffer* vbo;
    QOpenGLShaderProgram* program;

public:
    Billboard(QOpenGLFunctions_3_2_Core* gl);
    void draw(QOpenGLFunctions_3_2_Core* gl);
};

}

#endif // BILLBOARD_H
