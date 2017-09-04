#ifndef BLENDSTATE_H
#define BLENDSTATE_H

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
};

}
#endif // BLENDSTATE_H
