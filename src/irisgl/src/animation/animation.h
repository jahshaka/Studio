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
