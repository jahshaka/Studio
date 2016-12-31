#ifndef KEYFRAMESET_H
#define KEYFRAMESET_H

#include <QMap>
#include "../irisglfwd.h"
//#include "keyframeset.h"

namespace iris
{

class FloatKeyFrame;

/**
 * This class stores float keyframes associated with a QString key
 */
class KeyFrameSet
{
public:
    //QMap presrves insertion order,QHash doesnt
    QMap<QString,FloatKeyFrame*> keyFrames;

    FloatKeyFrame* getOrCreateFrame(QString name)
    {
        if(keyFrames.contains(name))
        {
            return keyFrames[name];
        }

        auto keyFrame = new FloatKeyFrame();
        keyFrames.insert(name,keyFrame);
        return keyFrame;
    }

    FloatKeyFrame* getKeyFrame(QString name)
    {
        if(keyFrames.contains(name))
        {
            return keyFrames[name];
        }

        return nullptr;
    }

    bool hasKeyFrame(QString name)
    {
        return keyFrames.contains(name);
    }

    static KeyFrameSetPtr create()
    {
        return KeyFrameSetPtr(new KeyFrameSet());
    }
};

}
#endif // KEYFRAMESET_H
