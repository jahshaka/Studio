/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef VRDEVICE_H
#define VRDEVICE_H

#include <QOpenGLContext>
#include "../libovr/Include/OVR_CAPI_GL.h"


class QOpenGLFunctions_3_2_Core;

namespace iris
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
    VrDevice();
    void initialize();
    void setTrackingOrigin(VrTrackingOrigin trackingOrigin);

    GLuint createMirrorFbo(int width,int height);

    bool isVrSupported();

    void beginFrame();
    void endFrame();

    void beginEye(int eye);
    void endEye(int eye);

    QMatrix4x4 getEyeViewMatrix(int eye,QVector3D pivot, float scale = 1.0f);
    QMatrix4x4 getEyeProjMatrix(int eye,float nearClip,float farClip);

    GLuint bindMirrorTextureId();

    QVector3D getHandPosition(int handIndex);
    QQuaternion getHandRotation(int handIndex);

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

    ovrTrackingState hmdState;
};

}

#endif // VRDEVICE_H
