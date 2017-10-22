#ifndef DEPTHSTENCILSTATE_H
#define DEPTHSTENCILSTATE_H

#include <qopengl.h>

namespace iris {

struct DepthState
{
    bool depthBufferEnabled;
    bool depthWriteEnabled;
    GLenum depthCompareFunc;

    DepthState()
    {
        depthBufferEnabled = true;
        depthWriteEnabled = true;
        depthCompareFunc = GL_LEQUAL;
    }

    DepthState(bool bufferEnabled, bool writeEnabled)
    {
        depthBufferEnabled = bufferEnabled;
        depthWriteEnabled = writeEnabled;
        depthCompareFunc = GL_LEQUAL;
    }

    static DepthState Default;
    static DepthState None;
};

}

#endif // DEPTHSTENCILSTATE_H
