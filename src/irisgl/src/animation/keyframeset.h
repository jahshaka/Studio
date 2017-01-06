#ifndef KEYFRAMESET_H
#define KEYFRAMESET_H

#include <QMap>
#include "../irisglfwd.h"

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

    FloatKeyFrame* getOrCreateFrame(QString name);

    FloatKeyFrame* getKeyFrame(QString name);

    bool hasKeyFrame(QString name);

    static QSharedPointer<KeyFrameSet> create();
};

}
#endif // KEYFRAMESET_H
