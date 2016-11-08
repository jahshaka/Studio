#ifndef BILLBOARD_H
#define BILLBOARD_H

class QOpenGLFunctions;
class QOpenGLBuffer;
class QOpenGLShaderProgram;

namespace jah3d
{

class Billboard
{
    friend class ForwardRenderer;

    QOpenGLBuffer* vbo;
    QOpenGLShaderProgram* program;

public:
    Billboard(QOpenGLFunctions* gl);
    void draw(QOpenGLFunctions* gl);
};

}

#endif // BILLBOARD_H
