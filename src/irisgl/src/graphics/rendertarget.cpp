#include "rendertarget.h"
#include <QOpenGLFunctions_3_2_Core>
#include "texture2d.h"
#include <QDebug>

namespace iris
{

RenderTarget::RenderTarget(int width, int height):
    width(width),
    height(height)
{
    gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
    gl->glGenFramebuffers(1, &fboId);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fboId);

    gl->glGenRenderbuffers(1, &renderBufferId);
    gl->glBindRenderbuffer(GL_RENDERBUFFER, renderBufferId);
    gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);

    gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderBufferId);

    checkStatus();

    gl->glBindRenderbuffer(GL_RENDERBUFFER, 0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);


}

void RenderTarget::checkStatus()
{
    auto status = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE) {

        switch(status)
        {
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            qDebug() << "Framebuffer incomplete: Attachment is NOT complete.";
        break;

        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            qDebug() << "Framebuffer incomplete: No image is attached to FBO.";
        break;

        case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
            qDebug() << "Framebuffer incomplete: Draw buffer.";
        break;

        case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
            qDebug() << "Framebuffer incomplete: Read buffer.";
        break;

        case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            qDebug() << "Framebuffer incomplete: Multisample.";
        break;

        case GL_FRAMEBUFFER_UNSUPPORTED:
            qDebug() << "Framebuffer incomplete: Unsupported by FBO implementation.";
        break;

        default:
            qDebug() << "Framebuffer incomplete: Unknown error.";
        break;
        }
    }
}

void RenderTarget::resize(int width, int height, bool resizeTextures)
{
    gl->glBindRenderbuffer(GL_RENDERBUFFER, renderBufferId);
    gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
    gl->glBindRenderbuffer(GL_RENDERBUFFER, 0);

    if(resizeTextures) {
        for( auto texture : textures) {
            texture->resize(width, height);
        }

        if (!!depthTexture)
            depthTexture->resize(width, height);
    }
}

void RenderTarget::addTexture(Texture2DPtr tex)
{
    textures.append(tex);
}

void RenderTarget::setDepthTexture(Texture2DPtr depthTex)
{
    clearRenderBuffer();
    depthTexture = depthTex;
}

void RenderTarget::clearTextures()
{
    textures.clear();
}

void RenderTarget::clearRenderBuffer()
{
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fboId);
    gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);

    checkStatus();

    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::bind()
{
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fboId);

    auto i = 0;
    for(auto texture : textures)
    {
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+i, GL_TEXTURE_2D, texture->getTextureId(), 0);
        i++;
    }

    if (!!depthTexture)
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture->getTextureId(), 0);

    checkStatus();
}

void RenderTarget::unbind()
{
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

RenderTargetPtr RenderTarget::create(int width, int height)
{
    return RenderTargetPtr(new RenderTarget(width, height));
}


}
