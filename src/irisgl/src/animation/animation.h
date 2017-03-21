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
#include <QMap>

namespace iris
{

class PropertyAnim;

class Animation
{
    QString name;
    bool loop;
    float length;

    // sample rate
    int frameRate;
public:

    //QMap preserves insertion order,QHash doesnt
    QMap<QString,PropertyAnim*> properties;

    explicit Animation(QString name = "Animation");
    ~Animation();

    void addPropertyAnim(PropertyAnim* anim);
    void removePropertyAnim(QString name);

    PropertyAnim* getPropertyAnim(QString name);

    FloatPropertyAnim* getFloatPropertyAnim(QString name);

    Vector3DPropertyAnim* getVector3PropertyAnim(QString name);

    ColorPropertyAnim* getColorPropertyAnim(QString name);

    bool hasPropertyAnim(QString name);

    static AnimationPtr create(QString name)
    {
        return AnimationPtr(new Animation(name));
    }

    QString getName() const;
    void setName(const QString &value);

    float getLength() const;
    void setLength(float value);

    bool getLooping() const;
    void setLooping(bool value);

    int getFrameRate() const;
    void setFrameRate(int value);
};

}
#endif // ANIMATION_H
