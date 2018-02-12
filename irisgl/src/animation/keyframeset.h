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
class PropertyAnim;

/**
 * This class stores float keyframes associated with a QString key
 */
class KeyFrameSet
{
public:
    ~KeyFrameSet();

    //QMap preserves insertion order,QHash doesnt
    QMap<QString,PropertyAnim*> keyFrames;

    void addKeyFrame(PropertyAnim* anim);

    PropertyAnim* getOrCreateFrame(QString name);

    PropertyAnim* getKeyFrame(QString name);

    bool hasKeyFrame(QString name);

    static QSharedPointer<KeyFrameSet> create();
};

}
#endif // KEYFRAMESET_H
