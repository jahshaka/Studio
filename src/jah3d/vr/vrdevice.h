#ifndef VRDEVICE_H
#define VRDEVICE_H

#include <QOpenGLContext>
#include "../libovr/Include/OVR_CAPI_GL.h"


class QOpenGLFunctions_3_2_Core;

namespace jah3d
{

enum class VrTrackingOrigin
{
    EyeLevel,
    FloorLevel
};

struct VrFrameData;

/**
 * This class provides an interface for the OVR sdk
 */
class VrDevice
{
public:
    VrDevice(QOpenGLFunctions_3_2_Core* gl);
    void initialize();
    void setTrackingOrigin(VrTrackingOrigin trackingOrigin);

    GLuint createMirrorFbo(int width,int height);

    bool isVrSupported();

    void beginFrame();
    void endFrame();

    void beginEye(int eye);
    void endEye(int eye);

    QMatrix4x4 getEyeViewMatrix(int eye,QVector3D pivot);
    QMatrix4x4 getEyeProjMatrix(int eye,float nearClip,float farClip);

    GLuint bindMirrorTextureId();

private:
    GLuint createDepthTexture(int width,int height);
    ovrTextureSwapChain createTextureChain(ovrSession session,ovrTextureSwapChain &swapChain,int width,int height);

    GLuint vr_depthTexture[2];
    ovrTextureSwapChain vr_textureChain[2];
    GLuint vr_Fbo[2];

    int eyeWidth;
    int eyeHeight;
    long long frameIndex;

    GLuint vr_mirrorFbo;
    GLuint vr_mirrorTexId;

    //quick bool to enable/disable vr rendering
    bool vrSupported;

    ovrSession session;
    ovrGraphicsLuid luid;
    ovrHmdDesc hmdDesc;

    VrTrackingOrigin trackingOrigin;

    QOpenGLFunctions_3_2_Core* gl;
    VrFrameData* frameData;
};

}

#endif // VRDEVICE_H
