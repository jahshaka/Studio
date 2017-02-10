/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "vrdevice.h"
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLTexture>

#include <QOpenGLContext>
#include "../libovr/Include/OVR_CAPI_GL.h"
#include "../libovr/Include/Extras/OVR_Math.h"

using namespace OVR;

namespace iris
{

struct VrFrameData
{
    ovrEyeRenderDesc eyeRenderDesc[2];
    ovrPosef eyeRenderPose[2];
    ovrVector3f hmdToEyeOffset[2];
    double sensorSampleTime;
};

VrDevice::VrDevice(QOpenGLFunctions_3_2_Core* gl)
{
    this->gl = gl;
    vrSupported = false;
    frameData = new VrFrameData();
}

bool VrDevice::isVrSupported()
{
    return vrSupported;
}

void VrDevice::initialize()
{
    ovrResult result = ovr_Initialize(nullptr);
    if (!OVR_SUCCESS(result)) {
        qDebug()<<"Failed to initialize libOVR.";
        return;
    }

    result = ovr_Create(&session, &luid);
    if (!OVR_SUCCESS(result)) {
        qDebug() << "Could not create libOVR session!";
        return;
    }

    hmdDesc = ovr_GetHmdDesc(session);

    // intialize framebuffers necessary for rendering to the hmd
    for (int eye = 0; eye < 2; ++eye) {
        ovrSizei texSize = ovr_GetFovTextureSize(session,
                                                 ovrEyeType(eye),
                                                 hmdDesc.DefaultEyeFov[eye], 1);
        createTextureChain(session, vr_textureChain[eye], texSize.w, texSize.h );
        vr_depthTexture[eye] = createDepthTexture(texSize.w, texSize.h);

        // should be the same for all
        eyeWidth = texSize.w;
        eyeHeight = texSize.h;
    }
    gl->glGenFramebuffers(2, vr_Fbo);

    createMirrorFbo(800, 600);

    setTrackingOrigin(VrTrackingOrigin::FloorLevel);

    vrSupported = true;
}

void VrDevice::setTrackingOrigin(VrTrackingOrigin trackingOrigin)
{
    this->trackingOrigin = trackingOrigin;

    if(trackingOrigin == VrTrackingOrigin::FloorLevel)
        ovr_SetTrackingOriginType(session, ovrTrackingOrigin_FloorLevel);
    else
        ovr_SetTrackingOriginType(session, ovrTrackingOrigin_EyeLevel);
}

GLuint VrDevice::createDepthTexture(int width,int height)
{
    GLuint texId;
    gl->glGenTextures(1, &texId);
    gl->glBindTexture(GL_TEXTURE_2D, texId);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLenum internalFormat = GL_DEPTH_COMPONENT24;
    GLenum type = GL_UNSIGNED_INT;
    //GLenum internalFormat = GL_DEPTH_COMPONENT32F;
    //GLenum type = GL_FLOAT;

    gl->glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_DEPTH_COMPONENT, type, NULL);

    return texId;
}

ovrTextureSwapChain VrDevice::createTextureChain(ovrSession session,
                                                 ovrTextureSwapChain &swapChain,
                                                 int width,
                                                 int height)
{
    //ovrTextureSwapChain swapChain;

    ovrTextureSwapChainDesc desc = {};
    desc.Type = ovrTexture_2D;
    desc.ArraySize = 1;
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.Format = OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.SampleCount = 1;
    desc.StaticImage = ovrFalse;

    ovrResult result = ovr_CreateTextureSwapChainGL(session, &desc, &swapChain);
    if (!OVR_SUCCESS(result)) {
        //qDebug()<<"could not create swap chain!";
        return nullptr;
    }

    int length = 0;
    ovr_GetTextureSwapChainLength(session, swapChain, &length);

    for (int i = 0; i < length; ++i) {
        GLuint chainTexId;
        ovr_GetTextureSwapChainBufferGL(session, swapChain, i, &chainTexId);
        gl->glBindTexture(GL_TEXTURE_2D, chainTexId);

        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    return swapChain;
}

GLuint VrDevice::createMirrorFbo(int width,int height)
{
    ovrMirrorTexture mirrorTexture = nullptr;

    ovrMirrorTextureDesc desc;
    memset(&desc, 0, sizeof(desc));
    //todo: use actual viewport size
    desc.Width = width;
    desc.Height = height;
    desc.Format = OVR_FORMAT_R8G8B8A8_UNORM_SRGB;

    // Create mirror texture and an FBO used to copy mirror texture to back buffer
    ovrResult result = ovr_CreateMirrorTextureGL(session, &desc, &mirrorTexture);
    if (!OVR_SUCCESS(result))
    {
        qDebug()<< "Failed to create mirror texture.";
        return 0;
    }

    // Configure the mirror read buffer
    // no need to store this texture anywhere
    ovr_GetMirrorTextureBufferGL(session, mirrorTexture, &vr_mirrorTexId);

    gl->glGenFramebuffers(1, &vr_mirrorFbo);
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, vr_mirrorFbo);
    gl->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vr_mirrorTexId, 0);
    gl->glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    return vr_mirrorFbo;
}


void VrDevice::beginFrame()
{
    frameData->eyeRenderDesc[0] = ovr_GetRenderDesc(session, ovrEye_Left, hmdDesc.DefaultEyeFov[0]);
    frameData->eyeRenderDesc[1] = ovr_GetRenderDesc(session, ovrEye_Right, hmdDesc.DefaultEyeFov[1]);

    frameData->hmdToEyeOffset[0] = frameData->eyeRenderDesc[0].HmdToEyeOffset;
    frameData->hmdToEyeOffset[1] = frameData->eyeRenderDesc[1].HmdToEyeOffset;

    ovr_GetEyePoses(session, frameIndex, ovrTrue, frameData->hmdToEyeOffset, frameData->eyeRenderPose, &frameData->sensorSampleTime);
}

void VrDevice::endFrame()
{
    ovrLayerEyeFov ld;
    ld.Header.Type  = ovrLayerType_EyeFov;
    ld.Header.Flags = ovrLayerFlag_TextureOriginAtBottomLeft;

    for (int eye = 0; eye < 2; ++eye)
    {
        ld.ColorTexture[eye] = vr_textureChain[eye];
        ld.Viewport[eye]     = Recti(Sizei(eyeWidth,eyeHeight));
        ld.Fov[eye]          = hmdDesc.DefaultEyeFov[eye];
        ld.RenderPose[eye]   = frameData->eyeRenderPose[eye];
        ld.SensorSampleTime  = frameData->sensorSampleTime;
    }

    ovrLayerHeader* layers = &ld.Header;
    ovrResult result = ovr_SubmitFrame(session, frameIndex, nullptr, &layers, 1);

    if (!OVR_SUCCESS(result))
    {
        qDebug()<<"error submitting frame"<<endl;
    }

    frameIndex++;
}

void VrDevice::beginEye(int eye)
{
    GLuint curTexId;
    int curIndex;

    ovr_GetTextureSwapChainCurrentIndex(session, vr_textureChain[eye], &curIndex);
    ovr_GetTextureSwapChainBufferGL(session, vr_textureChain[eye], curIndex, &curTexId);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, vr_Fbo[eye]);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D,
                               curTexId,
                               0);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D,
                               vr_depthTexture[eye],
                               0);

    gl->glViewport(0, 0, eyeWidth, eyeHeight);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl->glEnable(GL_FRAMEBUFFER_SRGB);
}

void VrDevice::endEye(int eye)
{
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);

    ovr_CommitTextureSwapChain(session, vr_textureChain[eye]);
}

QMatrix4x4 VrDevice::getEyeViewMatrix(int eye,QVector3D pivot, float scale)
{
    Vector3f origin = Vector3f(pivot.x(),pivot.y(),pivot.z());

    Matrix4f rollPitchYaw = Matrix4f::RotationY(0);
    Matrix4f finalRollPitchYaw = rollPitchYaw * Matrix4f(frameData->eyeRenderPose[eye].Orientation);
    Vector3f finalUp = finalRollPitchYaw.Transform(Vector3f(0, 1, 0));
    Vector3f finalForward = finalRollPitchYaw.Transform(Vector3f(0, 0, -1));

    auto fd = frameData->eyeRenderPose[eye].Position;
    auto framePos = Vector3f(fd.x, fd.y, fd.z);
    Vector3f shiftedEyePos = origin + rollPitchYaw.Transform(framePos * scale);

    Vector3f forward = shiftedEyePos + finalForward;

    QMatrix4x4 view;
    view.setToIdentity();
    view.lookAt(QVector3D(shiftedEyePos.x,shiftedEyePos.y,shiftedEyePos.z),
                QVector3D(forward.x,forward.y,forward.z),
                QVector3D(finalUp.x,finalUp.y,finalUp.z));

    return view;

}

QMatrix4x4 VrDevice::getEyeProjMatrix(int eye,float nearClip,float farClip)
{
    QMatrix4x4 proj;
    proj.setToIdentity();//not needed
    Matrix4f eyeProj = ovrMatrix4f_Projection(hmdDesc.DefaultEyeFov[eye],
                                              nearClip,
                                              farClip,
                                              ovrProjection_None);

    //todo: put in
    for (int r = 0; r < 4; r++) {
        for (int c = 0;c < 4; c++) {
            proj(r, c) = eyeProj.M[r][c];
        }
    }

    return proj;
}

GLuint VrDevice::bindMirrorTextureId()
{
    gl->glBindTexture(GL_TEXTURE_2D, vr_mirrorTexId);
    return vr_mirrorTexId;
}

}
