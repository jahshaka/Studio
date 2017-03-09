#ifndef ANIMFRAMES_H
#define ANIMFRAMES_H

#include "../irisglfwd.h"

namespace iris {


class FloatKeyFrame;
class AnimFrameInfo;

struct PropertyAnimInfo
{
    QString name;
    FloatKeyFrame* keyFrame = nullptr;

    PropertyAnimInfo(QString name, FloatKeyFrame* keyFrame);
};

class PropertyAnim
{
public:
    QString name;
    virtual QList<PropertyAnimInfo> getKeyFrames()
    {
        QList<PropertyAnimInfo> list;
        return list;
    }
};

class FloatPropertyAnim : public PropertyAnim
{
    FloatKeyFrame* keyFrame;
public:
    FloatPropertyAnim();
    ~FloatPropertyAnim();

    float getValue(float time);
    virtual QList<PropertyAnimInfo> getKeyFrames() override;
};

class Vector3DPropertyAnim : public PropertyAnim
{
    FloatKeyFrame* keyFrames[3];
public:
    Vector3DPropertyAnim();
    ~Vector3DPropertyAnim();

    QVector3D getValue(float time);
    virtual QList<PropertyAnimInfo> getKeyFrames() override;
};

class ColorPropertyAnim : public PropertyAnim
{
    FloatKeyFrame* keyFrames[4];
public:
    ColorPropertyAnim();
    ~ColorPropertyAnim();

    QColor getValue(float time);
    virtual QList<PropertyAnimInfo> getKeyFrames() override;
};

}

#endif // ANIMFRAMES_H
