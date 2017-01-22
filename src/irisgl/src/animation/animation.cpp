/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "animation.h"
#include "keyframeset.h"
#include "../irisglfwd.h"

namespace iris
{

Animation::Animation()
{
    name = "Animation";
    loop = false;
    length = 1.0f;

    keyFrameSet = KeyFrameSet::create();
}

}
