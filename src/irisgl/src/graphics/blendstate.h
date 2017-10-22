#ifndef BLENDSTATE_H
#define BLENDSTATE_H

#include <qopengl.h>

namespace iris
{

struct BlendState
{
    //bool blendEnabled;
    GLenum colorSourceBlend;
    GLenum alphaSourceBlend;
    GLenum colorDestBlend;
    GLenum alphaDestBlend;

    GLenum colorBlendEquation;
    GLenum alphaBlendEquation;

    BlendState()
    {
        colorSourceBlend = GL_ONE;
        alphaSourceBlend = GL_ONE;
        colorDestBlend = GL_ZERO;
        alphaDestBlend = GL_ZERO;

        colorBlendEquation = GL_FUNC_ADD;
        alphaBlendEquation = GL_FUNC_ADD;
    }

    BlendState(GLenum sourceBlend, GLenum destBlend):
        BlendState()
    {
        colorSourceBlend = sourceBlend;
        alphaSourceBlend = sourceBlend;
        colorDestBlend = destBlend;
        alphaDestBlend = destBlend;
    }

    static BlendState AlphaBlend;
    static BlendState Opaque;
    static BlendState Additive;
};

}
#endif // BLENDSTATE_H
