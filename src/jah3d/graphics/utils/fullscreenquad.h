#ifndef FULLSCREENQUAD_H
#define FULLSCREENQUAD_H

class QOpenGLFunctions_3_2_Core;
class QOpenGLShaderProgram;

namespace jah3d
{

class Mesh;


class FullScreenQuad
{
public:
    FullScreenQuad();
    ~FullScreenQuad();

    void draw(QOpenGLFunctions_3_2_Core* gl);

    QOpenGLShaderProgram* shader;
    Mesh* mesh;
};
}

#endif // FULLSCREENQUAD_H
