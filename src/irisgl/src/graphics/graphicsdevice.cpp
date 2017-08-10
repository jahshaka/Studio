#include <QColor>

#include "graphicsdevice.h"
#include "rendertarget.h"
#include "texture2d.h"
#include "vertexlayout.h"

namespace iris
{

VertexBuffer::VertexBuffer(GraphicsDevicePtr device, VertexLayout vertexLayout)
{
    this->device = device;
    this->vertexLayout = vertexLayout;
    device->getGL()->glGenBuffers(1, &bufferId);
}

void VertexBuffer::setData(void *data, unsigned int sizeInBytes)
{
    memcpy(this->data, data, sizeInBytes);

    auto gl = device->getGL();
    gl->glBindBuffer(GL_ARRAY_BUFFER, bufferId);
    gl->glBufferData(GL_ARRAY_BUFFER, sizeInBytes, data, GL_STATIC_DRAW);
    gl->glBindBuffer(GL_ARRAY_BUFFER, bufferId);
}

QOpenGLFunctions_3_2_Core *GraphicsDevice::getGL() const
{
    return gl;
}

GraphicsDevice::GraphicsDevice()
{
    context = QOpenGLContext::currentContext();
    gl = context->versionFunctions<QOpenGLFunctions_3_2_Core>();

    // 8 texture units by default
    for(int i =0;i<8;i++)
        textureUnits.append(TexturePtr());
}

void GraphicsDevice::setRenderTarget(RenderTargetPtr renderTarget)
{
    clearRenderTarget();

    activeRT = renderTarget;
    activeRT->bind();
}

// the size of all the textures should be the same
void GraphicsDevice::setRenderTarget(QList<Texture2DPtr> colorTargets, Texture2DPtr depthTarget)
{
    clearRenderTarget();

    // set the initial size
    if(colorTargets.size()!=0) {
        auto tex = colorTargets[0];
        _internalRT->resize(tex->getWidth(), tex->getHeight(), false);
    }
    else if(!!depthTarget) {
        // set the rt size to the depth target size
        _internalRT->resize(depthTarget->getWidth(), depthTarget->getHeight(), false);
    }

    if(colorTargets.size()>0) {
        for(auto tex : colorTargets)
            _internalRT->addTexture(tex);
    }

    if(!!depthTarget)
        _internalRT->setDepthTexture(depthTarget);

    activeRT = _internalRT;
    activeRT->bind();
}

void GraphicsDevice::clearRenderTarget()
{
    // reset to default rt
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!!activeRT) {
        // clear all textures from internal RT
        if (activeRT == _internalRT) {
            _internalRT->clearTextures();
            _internalRT->clearDepthTexture();
        }

        activeRT.reset();
    }
}

void GraphicsDevice::clear(GLuint bits)
{
    gl->glClear(bits);
}

void GraphicsDevice::clear(GLuint bits, QColor color, float depth, int stencil)
{
    glClearColor(color.redF(), color.greenF(), color.blueF(), color.alphaF());
    glClearDepth(depth);
    glClearStencil(stencil);
    gl->glClear(bits);
}

void GraphicsDevice::setTexture(int target, Texture2DPtr texture)
{
    gl->glActiveTexture(GL_TEXTURE0+target);
    gl->glBindTexture(GL_TEXTURE_2D, texture->getTextureId());
    gl->glActiveTexture(GL_TEXTURE0);
}

void GraphicsDevice::clearTexture(int target)
{
    gl->glActiveTexture(GL_TEXTURE0+target);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    gl->glActiveTexture(GL_TEXTURE0);
}





}
