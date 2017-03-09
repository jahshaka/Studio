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

QString Animation::getName() const
{
    return name;
}

void Animation::setName(const QString &value)
{
    name = value;
}

void Animation::addPropertyAnim(PropertyAnim *anim)
{
    Q_ASSERT(!keyFrames.contains(name));

    keyFrames.insert(anim->name, anim);
}

PropertyAnim* Animation::getPropertyAnim(QString name)
{
    Q_ASSERT(!keyFrames.contains(name));

    return keyFrames[name];
}

bool Animation::hasPropertyAnim(QString name)
{
    return keyFrames.contains(name);
}

}
