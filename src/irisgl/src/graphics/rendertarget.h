#ifndef RENDERTARGET_H
#define RENDERTARGET_H

#include "../irisglfwd.h"
#include "qopengl.h"


class QOpenGLFunctions_3_2_Core;
namespace iris
{

class RenderTarget
{
    GLuint fboId;

    // depth render buffer
    GLuint renderBufferId;

    QOpenGLFunctions_3_2_Core* gl;
    int width;
    int height;

    QList<Texture2DPtr> textures;
    Texture2DPtr depthTexture;

    RenderTarget(int width, int height);
    void checkStatus();
public:

    // resized renderbuffer
    // if resizeTextures is true, all attached textures will be resized
    void resize(int width, int height, bool resizeTextures);

    void addTexture(Texture2DPtr tex);
    void setDepthTexture(Texture2DPtr depthTex);

    void clearTextures();
    void clearDepthTexture();
    void clearRenderBuffer();

    void bind();
    void unbind();

    static RenderTargetPtr create(int width, int height);
    int getWidth() const;
    int getHeight() const;

    QImage toImage();
};

}

#endif // RENDERTARGET_H
