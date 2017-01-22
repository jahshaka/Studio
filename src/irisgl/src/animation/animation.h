/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ANIMATION_H
#define ANIMATION_H

#include "../irisglfwd.h"

namespace iris
{

class Animation
{
public:
    QString name;
    bool loop;
    float length;

    KeyFrameSetPtr keyFrameSet;

    Animation();

    static AnimationPtr create()
    {
        return AnimationPtr(new Animation());
    }
};

}
#endif // ANIMATION_H
