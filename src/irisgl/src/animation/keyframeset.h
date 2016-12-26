#ifndef KEYFRAMESET_H
#define KEYFRAMESET_H

#include <QHash>
//#include "keyframeset.h"

class FloatKeyFrame;

/**
 * This class stores float keyframes associated with a QString key
 */
class KeyFrameSet
{
public:
    QHash<QString,FloatKeyFrame*> keyFrames;
};

#endif // KEYFRAMESET_H
