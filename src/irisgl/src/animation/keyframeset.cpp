/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "keyframeset.h"
#include "keyframeanimation.h"

namespace iris
{

FloatKeyFrame* KeyFrameSet::getOrCreateFrame(QString name)
{
    if(keyFrames.contains(name))
    {
        return keyFrames[name];
    }

    auto keyFrame = new FloatKeyFrame();
    keyFrames.insert(name,keyFrame);
    return keyFrame;
}

FloatKeyFrame* KeyFrameSet::getKeyFrame(QString name)
{
    if(keyFrames.contains(name))
    {
        return keyFrames[name];
    }

    return nullptr;
}

bool KeyFrameSet::hasKeyFrame(QString name)
{
    return keyFrames.contains(name);
}

KeyFrameSetPtr KeyFrameSet::create()
{
    return KeyFrameSetPtr(new KeyFrameSet());
}

}
