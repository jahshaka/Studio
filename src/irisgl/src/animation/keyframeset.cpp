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
