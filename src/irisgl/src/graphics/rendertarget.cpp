#include "rendertarget.h"
#include <QOpenGLFunctions_3_2_Core>
#include "texture2d.h"
#include <QDebug>

namespace iris
{

int RenderTarget::getWidth() const
{
    return width;
}

int RenderTarget::getHeight() const
{
    return height;
}

// https://stackoverflow.com/questions/17347129/opengl-qt-render-to-texture-and-display-it-back
// todo: add options for floating point textures and other internal formats besides GL_RGBA
QImage RenderTarget::toImage()
{
    const int pixelSize = 4;// GL_RGBA
    uchar* pixels = new uchar[this->width * this->height * pixelSize];

    bind();
    gl->glReadPixels( 0,0,  this->width, this->height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    unbind();

    auto image = QImage(pixels, this->width, this->height, QImage::Format_ARGB32);
    image = image.rgbSwapped();
    return image.mirrored(false, true);
}

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

    //checkStatus();

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
    this->width = width;
    this->height = height;

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
    if (textures.count()!=0) {
        // this assertion should only hold true if this isnt the first texture
        Q_ASSERT_X(width==tex->getWidth() && height==tex->getHeight(),
               "RenderTarget",
               "Size of attached texture should be the same as size of render target");
    }

    textures.append(tex);
}

void RenderTarget::setDepthTexture(Texture2DPtr depthTex)
{
    Q_ASSERT_X(width==depthTex->getWidth() && height==depthTex->getHeight(),
               "RenderTarget",
               "Size of attached depth texture should be the same as size of render target");

    clearRenderBuffer();
    depthTexture = depthTex;
}

void RenderTarget::clearTextures()
{
    textures.clear();
}

void RenderTarget::clearDepthTexture()
{
    depthTexture.reset();
}

void RenderTarget::clearRenderBuffer()
{
    gl->glBindFramebuffer(GL_FRAMEBUFFER, fboId);
    gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);

    //checkStatus();

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

    //checkStatus();
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
