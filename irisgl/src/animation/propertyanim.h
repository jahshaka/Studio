#ifndef ANIMFRAMES_H
#define ANIMFRAMES_H

#include "../irisglfwd.h"

namespace iris {


class FloatKeyFrame;
class AnimFrameInfo;

struct PropertyAnimInfo
{
    QString name;
    // keyFrame isnt owned by this class
    FloatKeyFrame* keyFrame = nullptr;

    PropertyAnimInfo(QString name, FloatKeyFrame* keyFrame);
};

class PropertyAnim
{
protected:
    QString name;
public:
    virtual QList<PropertyAnimInfo> getKeyFrames()
    {
        QList<PropertyAnimInfo> list;
        return list;
    }

    virtual FloatKeyFrame* getKeyFrame(QString name)
    {
        return nullptr;
    }

    virtual FloatKeyFrame* getKeyFrame(int index)
    {
        return nullptr;
    }

    QString getName() const;
    void setName(const QString &value);

    virtual ~PropertyAnim()
    {

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
    virtual FloatKeyFrame* getKeyFrame(QString name) override;
    virtual FloatKeyFrame* getKeyFrame(int index) override;
};

class Vector3DPropertyAnim : public PropertyAnim
{
    FloatKeyFrame* keyFrames[3];
public:
    Vector3DPropertyAnim();
    ~Vector3DPropertyAnim();

    QVector3D getValue(float time);
    virtual QList<PropertyAnimInfo> getKeyFrames() override;
    virtual FloatKeyFrame* getKeyFrame(QString name) override;
    virtual FloatKeyFrame* getKeyFrame(int index) override;
};

class ColorPropertyAnim : public PropertyAnim
{
    FloatKeyFrame* keyFrames[4];
public:
    ColorPropertyAnim();
    ~ColorPropertyAnim();

    QColor getValue(float time);
    virtual QList<PropertyAnimInfo> getKeyFrames() override;
    virtual FloatKeyFrame* getKeyFrame(QString name) override;
    virtual FloatKeyFrame* getKeyFrame(int index) override;
};

}

#endif // ANIMFRAMES_H
