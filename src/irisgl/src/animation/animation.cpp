#include "animation.h"
#include "keyframeset.h"

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
