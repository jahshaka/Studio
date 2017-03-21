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
#include "propertyanim.h"
#include "../irisglfwd.h"
#include <QDebug>

namespace iris
{

Animation::Animation(QString name)
{
    this->name = name;
    loop = false;
    length = 1.0f;
    frameRate = 60;
}

Animation::~Animation()
{
}

QString Animation::getName() const
{
    return name;
}

void Animation::setName(const QString &value)
{
    name = value;
}

float Animation::getLength() const
{
    return length;
}

void Animation::setLength(float value)
{
    length = value;
}

bool Animation::getLooping() const
{
    return loop;
}

void Animation::setLooping(bool value)
{
    loop = value;
}

void Animation::addPropertyAnim(PropertyAnim *anim)
{
    //Q_ASSERT(!properties.contains(name));
    
    properties.insert(anim->getName(), anim);
}

void Animation::removePropertyAnim(QString name)
{
    auto prop = properties[name];
    delete prop;

    if (properties.remove(name) == 0)
        qDebug() << "Animation property "<<name<<" doesnt exist";
}

PropertyAnim* Animation::getPropertyAnim(QString name)
{
    //Q_ASSERT(properties.contains(name));

    return properties[name];
}

FloatPropertyAnim *Animation::getFloatPropertyAnim(QString name)
{
    return (FloatPropertyAnim*)properties[name];
}

Vector3DPropertyAnim *Animation::getVector3PropertyAnim(QString name)
{
    return (Vector3DPropertyAnim*)properties[name];
}

ColorPropertyAnim *Animation::getColorPropertyAnim(QString name)
{
    return (ColorPropertyAnim*)properties[name];
}

bool Animation::hasPropertyAnim(QString name)
{
    return properties.contains(name);
}

int Animation::getFrameRate() const
{
    return frameRate;
}

void Animation::setFrameRate(int value)
{
    frameRate = value;
}

}
