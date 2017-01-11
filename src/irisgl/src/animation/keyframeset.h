/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

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
