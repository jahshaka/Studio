#include <QColor>

#include "graphicsdevice.h"
#include "rendertarget.h"
#include "texture2d.h"
#include "vertexlayout.h"
#include "shader.h"

#include <QOpenGLShaderProgram>

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
    //memcpy(this->data, data, sizeInBytes);

    auto gl = device->getGL();
    gl->glBindBuffer(GL_ARRAY_BUFFER, bufferId);
    gl->glBufferData(GL_ARRAY_BUFFER, sizeInBytes, data, GL_STATIC_DRAW);
    gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
}

QOpenGLFunctions_3_2_Core *GraphicsDevice::getGL() const
{
    return gl;
}

GraphicsDevicePtr GraphicsDevice::create()
{
    return GraphicsDevicePtr(new GraphicsDevice());
}

GraphicsDevice::GraphicsDevice()
{
    context = QOpenGLContext::currentContext();
    gl = context->versionFunctions<QOpenGLFunctions_3_2_Core>();

    // 8 texture units by default
    for(int i =0;i<8;i++)
        textureUnits.append(TexturePtr());

    gl->glGenVertexArrays(1, &defautVAO);

    //set default blend and depth state
    this->setBlendState(BlendState::Opaque);
    this->setDepthState(DepthState::Default);
    this->setRasterizerState(RasterizerState::CullCounterClockwise);
}

void GraphicsDevice::setViewport(const QRect& vp)
{
    viewport = vp;
    gl->glViewport(vp.x(), vp.y(), vp.width(),vp.height());
}

QRect GraphicsDevice::getViewport()
{
    return viewport;
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

void GraphicsDevice::clear(QColor color)
{
    clear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT, color);
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

void GraphicsDevice::setShader(ShaderPtr shader)
{
    activeShader = shader;
    shader->program->bind();
}

void GraphicsDevice::setTexture(int target, Texture2DPtr texture)
{
    gl->glActiveTexture(GL_TEXTURE0+target);
    if (!!texture)
        gl->glBindTexture(GL_TEXTURE_2D, texture->getTextureId());
    else
        gl->glBindTexture(GL_TEXTURE_2D, 0);
    gl->glActiveTexture(GL_TEXTURE0);
}

void GraphicsDevice::clearTexture(int target)
{
    gl->glActiveTexture(GL_TEXTURE0+target);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    gl->glActiveTexture(GL_TEXTURE0);
}

void GraphicsDevice::setVertexBuffer(VertexBufferPtr vertexBuffer)
{
    vertexBuffers.clear();
    vertexBuffers.append(vertexBuffer);
}

void GraphicsDevice::setBlendState(const BlendState &blendState)
{
    bool blendEnabled = true;
    if(blendState.colorSourceBlend == GL_ONE &&
       blendState.colorDestBlend == GL_ZERO &&
       blendState.alphaSourceBlend == GL_ONE &&
       blendState.alphaDestBlend == GL_ZERO) {
        blendEnabled = false;
    }

    this->lastBlendEnabled = blendEnabled;
    if (blendEnabled)
        gl->glEnable(GL_BLEND);
    else
        gl->glDisable(GL_BLEND);

    // update blend equations
    if (lastBlendState.alphaBlendEquation != blendState.alphaBlendEquation ||
        lastBlendState.colorBlendEquation != blendState.colorBlendEquation) {

        gl->glBlendEquationSeparate(blendState.colorBlendEquation, blendState.alphaBlendEquation);
        lastBlendState.alphaBlendEquation = blendState.alphaBlendEquation;
        lastBlendState.colorBlendEquation = blendState.colorBlendEquation;
    }

    // update blend functions
    if(lastBlendState.colorSourceBlend != blendState.colorSourceBlend ||
       lastBlendState.colorDestBlend != blendState.colorDestBlend ||
       lastBlendState.alphaSourceBlend != blendState.alphaSourceBlend ||
       lastBlendState.alphaDestBlend != blendState.alphaDestBlend)
    {
        gl->glBlendFuncSeparate(blendState.colorSourceBlend,
                                blendState.colorDestBlend,
                                blendState.alphaSourceBlend,
                                blendState.alphaDestBlend);

        lastBlendState.colorSourceBlend = blendState.colorSourceBlend;
        lastBlendState.colorDestBlend = blendState.colorDestBlend;
        lastBlendState.alphaSourceBlend = blendState.alphaSourceBlend;
        lastBlendState.alphaDestBlend = blendState.alphaDestBlend;
    }
}

void GraphicsDevice::setDepthState(const DepthState &depthStencil)
{
    // depth test
    if (lastDepthState.depthBufferEnabled != depthStencil.depthBufferEnabled)
    {
        if (depthStencil.depthBufferEnabled)
            gl->glEnable(GL_DEPTH_TEST);
        else
            gl->glDisable(GL_DEPTH_TEST);

        lastDepthState.depthBufferEnabled = depthStencil.depthBufferEnabled;
    }

    // depth write
    if (lastDepthState.depthWriteEnabled != depthStencil.depthWriteEnabled)
    {
        gl->glDepthMask(depthStencil.depthWriteEnabled);
        lastDepthState.depthWriteEnabled = depthStencil.depthWriteEnabled;
    }

    // depth func
    if (lastDepthState.depthCompareFunc != depthStencil.depthCompareFunc)
    {
        gl->glDepthFunc(depthStencil.depthCompareFunc);
        lastDepthState.depthCompareFunc = depthStencil.depthCompareFunc;
    }
}

void GraphicsDevice::setRasterizerState(const RasterizerState &rasterState)
{
    // culling
    if (lastRasterState.cullMode != rasterState.cullMode) {
        if (rasterState.cullMode == CullMode::None)
            gl->glDisable(GL_CULL_FACE);
        else {
            gl->glEnable(GL_CULL_FACE);

            if (rasterState.cullMode == CullMode::CullClockwise)
                gl->glFrontFace(GL_CW);
            else
                gl->glFrontFace(GL_CCW);
        }

        lastRasterState.cullMode = rasterState.cullMode;
    }

    // polygon fill
    if (lastRasterState.fillMode != rasterState.fillMode) {
        gl->glPolygonMode(GL_FRONT_AND_BACK, rasterState.fillMode);
        lastRasterState.fillMode = rasterState.fillMode;
    }
}

void GraphicsDevice::drawPrimitives(GLenum primitiveType, int start, int count)
{
    gl->glBindVertexArray(defautVAO);
    for(auto buffer : vertexBuffers) {
        gl->glBindBuffer(GL_ARRAY_BUFFER, buffer->bufferId);
        buffer->vertexLayout.bind();
    }

    gl->glDrawArrays(primitiveType, start, count);
    gl->glBindVertexArray(0);
}





}
