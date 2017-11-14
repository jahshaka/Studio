#ifndef RENDERTEXTURE_H
#define RENDERTEXTURE_H

#include "texture.h"

namespace iris
{

enum AttachmentType
{

};

class RenderTexture : public Texture
{
public:
    RenderTexture();

    void setSize(int width, int height);
    int getTextureId();
};

class DepthRenderTexture : public Texture
{
public:
    DepthRenderTexture();

    void setSize(int width, int height);
    int getTextureId();
};

}


#endif // RENDERTEXTURE_H
