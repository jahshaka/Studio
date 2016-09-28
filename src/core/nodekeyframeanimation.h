/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef KEYFRAMEANIMATION_H
#define KEYFRAMEANIMATION_H

#include <vector>

class NodeKeyFrame;
class QVector3D;
class QQuaternion;

class NodeKeyFrameAnimation
{
public:
    std::vector<NodeKeyFrame*> frames;

    void addPosFrame(float x,float y,float z,float time);
    void addScaleFrame(float s,float time);
    void addScaleFrame(float x,float y,float z,float time);
    void addRotFrame(float yaw,float pitch,float roll,float time);
    void addPosRotFrame(float x,float y,float z,
                        float yaw,float pitch,float roll,
                        float time);

    bool hasFrames();

    QVector3D getPosAt(float time);
    void getFrameAt(float time,NodeKeyFrame& frame);

    void getKeyFramesAtTime(NodeKeyFrame** firstFrame,NodeKeyFrame** lastFrame,float time);
    QVector3D lerp(QVector3D a,QVector3D b,float t);

    //todo: slerp for quaternion
    QQuaternion lerp(QQuaternion a,QQuaternion b,float t);

};

#endif // KEYFRAMEANIMATION_H
