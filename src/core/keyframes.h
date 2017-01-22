/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALKEYFRAME_H
#define MATERIALKEYFRAME_H

#include "keyframeanimation.h"


//base class for animation frames for different animation objects:
//Scene Node Transformation
//Material Properties
//Other Properties

class KeyFrameAnimation
{
public:

    std::vector<QString> getChannels();
};

//holds key frames
//needs a more description  name
class MaterialKeyFrameAnimation
{
public:
    Vector3DKeyFrame* diffuse;
    Vector3DKeyFrame* specular;
    FloatKeyFrame* shininess;

    FloatKeyFrame* alpha;

    StringKeyFrame* diffuseTextureName;

    float length;
    MaterialKeyFrameAnimation()
    {
        length = 15;

        diffuse = new Vector3DKeyFrame();
        specular = new Vector3DKeyFrame();
        shininess = new FloatKeyFrame();

        alpha = new FloatKeyFrame();
        diffuseTextureName = new StringKeyFrame();
    }

    ~MaterialKeyFrameAnimation()
    {
        delete diffuse;
        delete specular;
        delete shininess;
        delete alpha;
        delete diffuseTextureName;
    }
};

class TransformKeyFrameAnimation
{
public:
    Vector3DKeyFrame* pos;
    Vector3DKeyFrame* scale;
    //QuaternionKeyFrame* rot;
    Vector3DKeyFrame* rot;

    TransformKeyFrameAnimation()
    {
        pos = new Vector3DKeyFrame();
        scale = new Vector3DKeyFrame();
        rot = new Vector3DKeyFrame();
    }

    ~TransformKeyFrameAnimation()
    {
        delete pos;
        delete scale;
        delete rot;
    }
};

class CameraKeyFrameAnimation
{
public:
    FloatKeyFrame aspectRatio;
    FloatKeyFrame nearClip;
    FloatKeyFrame farClip;
    FloatKeyFrame angle;
};

class SceneKeyFrameAnimation
{
public:
    Vector3DKeyFrame backgroundColor;
    StringKeyFrame activeCamera;
};

class LightKeyFrameAnimation
{
public:
    ColorKeyFrame* color;
    FloatKeyFrame* intensity;

    LightKeyFrameAnimation()
    {
        color = new ColorKeyFrame();
        intensity = new FloatKeyFrame();
    }
};

#endif // MATERIALKEYFRAME_H
