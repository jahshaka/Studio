#ifndef ANIMATIONPROPERTY_H
#define ANIMATIONPROPERTY_H

#include "../irisglfwd.h"

namespace iris
{

enum class AnimablePropertyType
{
    Float,
    Vector3,
    Color
};

struct AnimableProperty
{
    QString name;
    AnimablePropertyType type;

    AnimableProperty(QString name,AnimablePropertyType type)
    {
        this->name = name;
        this->type = type;
    }
};

}

#endif // ANIMATIONPROPERTY_H
