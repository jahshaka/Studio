/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "nodekeyframeanimation.h"
#include "nodekeyframe.h"

#include "QVector3D"


//todo: sort keys by frame time
void NodeKeyFrameAnimation::addPosFrame(float x,float y,float z,float time)
{
    NodeKeyFrame* frame = new NodeKeyFrame();
    frame->pos.setX(x);
    frame->pos.setY(y);
    frame->pos.setZ(z);
    frame->time = time;

    frames.push_back(frame);
}

void NodeKeyFrameAnimation::addScaleFrame(float s,float time)
{
    NodeKeyFrame* frame = new NodeKeyFrame();
    frame->scale.setX(s);
    frame->scale.setY(s);
    frame->scale.setZ(s);
    frame->time = time;

    frames.push_back(frame);
}

void NodeKeyFrameAnimation::addScaleFrame(float x,float y,float z,float time)
{
    NodeKeyFrame* frame = new NodeKeyFrame();
    frame->scale.setX(x);
    frame->scale.setY(y);
    frame->scale.setZ(z);
    frame->time = time;

    frames.push_back(frame);
}

void NodeKeyFrameAnimation::addRotFrame(float yaw,float pitch,float roll,float time)
{
    NodeKeyFrame* frame = new NodeKeyFrame();
    frame->rot = QQuaternion::fromEulerAngles(QVector3D(yaw,pitch,roll));
    frame->time = time;

    frames.push_back(frame);
}

void NodeKeyFrameAnimation::addPosRotFrame(float x,float y,float z,
                        float yaw,float pitch,float roll,
                        float time)
{
    NodeKeyFrame* frame = new NodeKeyFrame();
    frame->pos.setX(x);
    frame->pos.setY(y);
    frame->pos.setZ(z);
    frame->time = time;

    frame->rot = QQuaternion::fromEulerAngles(QVector3D(yaw,pitch,roll));
    frame->time = time;

    frames.push_back(frame);
}

bool NodeKeyFrameAnimation::hasFrames()
{
    return frames.size()>0;
}


//obsolete
QVector3D NodeKeyFrameAnimation::getPosAt(float time)
{
    NodeKeyFrame* first = Q_NULLPTR;
    NodeKeyFrame* last = Q_NULLPTR;

    this->getKeyFramesAtTime(&first,&last,time);

    if(first!=Q_NULLPTR && last==Q_NULLPTR)
    {
        return first->pos;
    }

    if(first!=Q_NULLPTR && last!=Q_NULLPTR)
    {
        //linearly interpolate between frames
        float t =0;
        float frameDiff = last->time - first->time;
        // - frameDiff could be 0!!
        if(frameDiff != 0)
        {
            t = (time-first->time)/frameDiff;
        }

        QVector3D pos = lerp(first->pos,last->pos,t);
        return pos;
    }


   return QVector3D();
}

void NodeKeyFrameAnimation::getFrameAt(float time,NodeKeyFrame& frame)
{
    NodeKeyFrame* first = Q_NULLPTR;
    NodeKeyFrame* last = Q_NULLPTR;

    this->getKeyFramesAtTime(&first,&last,time);

    if(first!=Q_NULLPTR && last==Q_NULLPTR)
    {
        frame.pos = first->pos;
        frame.scale = first->scale;
    }

    if(first!=Q_NULLPTR && last!=Q_NULLPTR)
    {
        //linearly interpolate between frames
        float t =0;
        float frameDiff = last->time - first->time;
        // - frameDiff could be 0!!
        if(frameDiff != 0)
        {
            t = (time-first->time)/frameDiff;
        }

        QVector3D pos = lerp(first->pos,last->pos,t);
        QVector3D scale = lerp(first->scale,last->scale,t);
        QQuaternion quat = lerp(first->rot,last->rot,t);

        frame.pos = pos;
        frame.scale = scale;
        frame.rot = quat;
    }
}

// - no wrap arounds at the moment
// - assumes keys are sorted by time
void NodeKeyFrameAnimation::getKeyFramesAtTime(NodeKeyFrame** firstFrame,NodeKeyFrame** lastFrame,float time)
{
    int numFrames = frames.size();

    //todo: do something better than this
    if(numFrames==0)
        return;

    if(numFrames==1)
    {
        *firstFrame = frames[0];
        return;
    }

    //multiple frames from this point on

    //before first frame
    if(time<=frames[0]->time)
    {
        *firstFrame = frames[0];
        return;
    }

    //after last frame
    if(time>=frames[numFrames-1]->time)
    {
        *firstFrame = frames[numFrames-1];
        return;
    }

    //anim is between two keys from here on out
    //NodeKeyFrame* first = Q_NULLPTR;
    //NodeKeyFrame* last = Q_NULLPTR;

    //find first key and last key
    for(unsigned int k = 0;k<frames.size();k++)
    {
        NodeKeyFrame* frame = frames[k];

        //assign frame to the first up until a frame is found whose time is greater than time var
        //next frame is naturally the other key being interpolated with
        if(frame->time<=time)
            *firstFrame = frame;
        else
        {
            *lastFrame = frame;
            break;
        }
    }

}

//t should be between 0 and 1, inclusive
QVector3D NodeKeyFrameAnimation::lerp(QVector3D a,QVector3D b,float t)
{
    return a+(b-a)*t;
}

//t should be between 0 and 1, inclusive
QQuaternion NodeKeyFrameAnimation::lerp(QQuaternion a,QQuaternion b,float t)
{
    return QQuaternion::slerp(a,b,t);
}
