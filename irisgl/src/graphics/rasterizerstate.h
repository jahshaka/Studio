#ifndef RASTERIZERSTATE_H
#define RASTERIZERSTATE_H

#include <qopengl.h>

namespace iris
{

enum class CullMode
{
    None,
    CullClockwise,
    CullCounterClockwise
};

struct RasterizerState
{
    CullMode cullMode;
    GLenum fillMode;

    RasterizerState()
    {
        cullMode= CullMode::CullCounterClockwise;
        fillMode = GL_FILL;
    }

    RasterizerState(CullMode cullMode, GLenum fillMode):
        cullMode(cullMode),
        fillMode(fillMode)
    {
    }

    static RasterizerState CullCounterClockwise;
    static RasterizerState CullClockwise;
    static RasterizerState CullNone;
};

}

#endif // RASTERIZERSTATE_H
