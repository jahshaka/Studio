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

	float depthScaleBias;
	float depthBias;

    RasterizerState()
    {
        cullMode= CullMode::CullCounterClockwise;
        fillMode = GL_FILL;
		depthScaleBias = 0;
		depthBias = 0;
    }

    RasterizerState(CullMode cullMode, GLenum fillMode):
        cullMode(cullMode),
        fillMode(fillMode)
    {
		depthScaleBias = 0;
		depthBias = 0;
    }

    static RasterizerState CullCounterClockwise;
    static RasterizerState CullClockwise;
    static RasterizerState CullNone;

	static RasterizerState createCullCounterClockwise();
	static RasterizerState createCullClockwise();
	static RasterizerState createCullNone();
};

}

#endif // RASTERIZERSTATE_H
