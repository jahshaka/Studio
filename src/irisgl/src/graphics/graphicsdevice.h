#ifndef GRAPHICSDEVICE_H
#define GRAPHICSDEVICE_H

#include "../irisglfwd.h"
#include <QOpenGLFunctions_3_2_Core>
#include <QRect>
#include <QStack>
#include "vertexlayout.h"

class QOpenGLContext;

namespace iris
{

class GraphicsDevice;
typedef QSharedPointer<GraphicsDevice> GraphicsDevicePtr;
class VertexBuffer;
typedef QSharedPointer<VertexBuffer> VertexBufferPtr;
class IndexBuffer;
typedef QSharedPointer<IndexBuffer> IndexBufferPtr;

class VertexBuffer
{
    friend class GraphicsDevice;
public:
    void* data;
    GLuint bufferId;
    VertexLayout vertexLayout;
    GraphicsDevicePtr device;

    VertexBuffer(GraphicsDevicePtr device, VertexLayout vertexLayout);

    template<typename T>
    void setData(T* data, unsigned int sizeInBytes)
    {
        setData((void*) data, sizeInBytes);
    }

    void setData(void* data, unsigned int sizeinBytes);
};

class IndexBuffer
{
public:
    void* data;
    GLuint bufferId;

    template<typename T>
    void setData(T* data, unsigned int sizeInBytes)
    {
        memcpy(this->data, data, sizeInBytes);
    }
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

    void clear(QColor color);
    void clear(GLuint bits);
    void clear(GLuint bits, QColor color, float depth=1.0f, int stencil=0);

    void setShader(ShaderPtr shader);
    void setTexture(int target, Texture2DPtr texture);
    void clearTexture(int target);

    void setVertexBuffer(VertexBufferPtr vertexBuffer);
    void setVertexBuffers(QList<VertexBufferPtr> vertexBuffers);
    void setIndexBuffer(IndexBufferPtr indexBuffer);

    void drawPrimitives(GLenum primitiveType,int start, int count);
    QOpenGLFunctions_3_2_Core *getGL() const;

    static GraphicsDevicePtr create();
};

}

#endif // GRAPHICSDEVICE_H
