#ifndef GRAPHICSDEVICE_H
#define GRAPHICSDEVICE_H

#include "../irisglfwd.h"
#include <QOpenGLFunctions_3_2_Core>
#include <QRect>
#include <QStack>
#include "vertexlayout.h"
#include "blendstate.h"
#include "depthstate.h"
#include "rasterizerstate.h"

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
    int dataSize;

    GLuint bufferId;
    VertexLayout vertexLayout;
    GraphicsDevicePtr device;

    bool _isDirty;

    static VertexBufferPtr create(GraphicsDevicePtr device, VertexLayout vertexLayout)
    {
        return VertexBufferPtr(new VertexBuffer(device, vertexLayout));
    }

    template<typename T>
    void setData(T* data, unsigned int sizeInBytes)
    {
        setData((void*) data, sizeInBytes);
    }

    void setData(void* data, unsigned int sizeinBytes);

    bool isDirty()
    {
        return _isDirty;
    }

private:
    VertexBuffer(GraphicsDevicePtr device, VertexLayout vertexLayout);
    void upload();
    void destroy();
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

    QRect viewport;
    RenderTargetPtr _internalRT;
    RenderTargetPtr activeRT;

    QVector<TexturePtr> textureUnits;
    QVector<VertexBufferPtr> vertexBuffers;
    IndexBufferPtr indexBuffer;

    // apparently gl needs at least one to be set
    // before you can render anything
    GLuint defautVAO;

    ShaderPtr activeShader;

    bool lastBlendEnabled;
    BlendState lastBlendState;
    DepthState lastDepthState;
    RasterizerState lastRasterState;

public:
    GraphicsDevice();

    void setViewport(const QRect& vp);
    QRect getViewport();

    void setRenderTarget(RenderTargetPtr renderTarget);
	void setRenderTarget(Texture2DPtr renderTarget);
    void setRenderTarget(QList<Texture2DPtr> colorTargets, Texture2DPtr depthTarget);
    void clearRenderTarget();

    void clear(QColor color);
    void clear(GLuint bits);
    void clear(GLuint bits, QColor color, float depth=1.0f, int stencil=0);

    void setShader(ShaderPtr shader);
    void setTexture(int target, Texture2DPtr texture);
    void clearTexture(int target);
	void compileShader(iris::ShaderPtr shader);

    void setVertexBuffer(VertexBufferPtr vertexBuffer);
    void setVertexBuffers(QList<VertexBufferPtr> vertexBuffers);
    void setIndexBuffer(IndexBufferPtr indexBuffer);
    void clearIndexBuffer();

    void setBlendState(const BlendState& blendState, bool force = false);
    void setDepthState(const DepthState& depthStencil, bool force = false);
    void setRasterizerState(const RasterizerState& rasterState, bool force = false);

    void drawPrimitives(GLenum primitiveType,int start, int count);
    QOpenGLFunctions_3_2_Core *getGL() const;

    static GraphicsDevicePtr create();

private:
	void compileShader();
};

}

#endif // GRAPHICSDEVICE_H
