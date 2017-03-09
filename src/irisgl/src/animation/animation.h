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
class PropertyAnim;

class Animation
{
    QString name;
    bool loop;
    float length;
public:

    //QMap preserves insertion order,QHash doesnt
    QMap<QString,PropertyAnim*> properties;

    Animation(QString name);
    ~Animation();

    void addPropertyAnim(PropertyAnim* anim);

    PropertyAnim* getPropertyAnim(QString name);

    bool hasPropertyAnim(QString name);

    static AnimationPtr create(QString name)
    {
        return AnimationPtr(new Animation(name));
    }

    QString getName() const;
    void setName(const QString &value);
};

}
#endif // ANIMATION_H
