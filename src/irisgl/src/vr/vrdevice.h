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

#include <QMatrix4x4>
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

enum class VrTouchInput : unsigned int
{
    A                   = ovrButton_A,
    B                   = ovrButton_B,
    RightThumb          = ovrButton_RThumb,
    RightIndexTrigger   = 0x00000010,
    RightShoulder       = ovrButton_RShoulder,

    X                   = ovrButton_X,
    Y                   = ovrButton_Y,
    LeftThumb           = ovrButton_LThumb,
    LeftIndexTrigger    = 0x00001000,
    LeftShoulder        = ovrButton_LShoulder,

    RightIndexPointing  = 0x00000020,
    RightThumbUp        = 0x00000040,

    LeftIndexPointing   = 0x00002000,
    LeftThumbUp         = 0x00004000
};

class VrTouchController
{
    friend class VrDevice;
    ovrInputState inputState;
    ovrInputState prevInputState;

    int index;
    bool isBeingTracked;

public:
    VrTouchController(int index);

    bool isButtonDown(VrTouchInput btn);
    bool isButtonUp(VrTouchInput btn);

    bool isButtonPressed(VrTouchInput btn);
    bool isButtonReleased(VrTouchInput btn);

    QVector2D GetThumbstick();

    bool isTracking();

    float getIndexTrigger();
    float getHandTrigger();
private:
    bool isButtonDown(const ovrInputState& state, VrTouchInput btn);
    void setTrackingState(bool state);
};

struct VrFrameData;

/**
 * This class provides an interface for the OVR sdk
 */
class VrDevice
{
    friend class VrManager;
    VrDevice();
public:
    void initialize();
    void setTrackingOrigin(VrTrackingOrigin trackingOrigin);

    GLuint createMirrorFbo(int width,int height);

    bool isVrSupported();

    void beginFrame();
    void endFrame();

    void beginEye(int eye);
    void endEye(int eye);

    /*
     * Returns whether or not the headset is being tracked
     * If orientation is being track the the headset is on the persons head
     */
    bool isHeadMounted();

    QMatrix4x4 getEyeViewMatrix(int eye,QVector3D pivot,QMatrix4x4 transform = QMatrix4x4());
    QMatrix4x4 getEyeProjMatrix(int eye,float nearClip,float farClip);

    GLuint bindMirrorTextureId();

    QVector3D getHandPosition(int handIndex);
    QQuaternion getHandRotation(int handIndex);

    VrTouchController* getTouchController(int index);
    QQuaternion getHeadRotation();
    QVector3D getHeadPos();

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

    VrTouchController* touchControllers[2];
};

}

#endif // VRDEVICE_H
