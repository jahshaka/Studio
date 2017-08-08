#ifndef GRAPHICSDEVICE_H
#define GRAPHICSDEVICE_H

#include "../irisglfwd.h"
#include <QOpenGLFunctions_3_2_Core>

class QOpenGLContext;

namespace iris
{

class VertexBuffer
{
public:
    void* data;
    GLuint bufferId;
};

/*
 * This class is intended to wrap all calls to opengl with simpler
 * and easier-to-use functions
*/
class GraphicsDevice
{
    QOpenGLFunctions_3_2_Core* gl;
    QOpenGLContext* context;

    QStack<QRect> viewportStack;
    RenderTargetPtr _internalRT;
    RenderTargetPtr activeRT;

    QList<TexturePtr> textureUnits;
public:
    GraphicsDevice();

    void setRenderTarget(RenderTargetPtr renderTarget);
    void setRenderTarget(QList<Texture2DPtr> colorTargets, Texture2DPtr depthTarget);
    void clearRenderTarget();

    void clear(GLuint bits);
    void clear(GLuint bits, QColor color, float depth=1.0f, int stencil=0);

    void setShader(ShaderPtr shader);
    void setTexture(int target, Texture2DPtr texture);
    void clearTexture(int target);

    void setVertexBuffer(VertexBufferPtr vertexBuffer);
    void setVertexBuffers(QList<VertexBufferPtr> vertexBuffers);
    void setIndexBuffer(IndexBufferPtr indexBuffer);

    void drawPrimitives(GLenum primitiveType,int start, int count);
};

}

#endif // GRAPHICSDEVICE_H
