#include "propertyanim.h"
#include "keyframeanimation.h"

namespace iris{

FloatPropertyAnim::FloatPropertyAnim()
{
    keyFrame = new FloatKeyFrame();
}

FloatPropertyAnim::~FloatPropertyAnim()
{
    delete keyFrame;
}

float FloatPropertyAnim::getValue(float time)
{
    return keyFrame->getValueAt(time);
}

QList<PropertyAnimInfo> FloatPropertyAnim::getKeyFrames()
{
    QList<PropertyAnimInfo> frames;
    frames.append(PropertyAnimInfo(name,keyFrame));
    return frames;
}

Vector3DPropertyAnim::Vector3DPropertyAnim()
{
    keyFrames[0] = new FloatKeyFrame();
    keyFrames[1] = new FloatKeyFrame();
    keyFrames[2] = new FloatKeyFrame();
}

Vector3DPropertyAnim::~Vector3DPropertyAnim()
{
    delete[] keyFrames;
}

QVector3D Vector3DPropertyAnim::getValue(float time)
{
    return QVector3D(
                keyFrames[0]->getValueAt(time),
                keyFrames[1]->getValueAt(time),
                keyFrames[2]->getValueAt(time)
                );
}

QList<PropertyAnimInfo> Vector3DPropertyAnim::getKeyFrames()
{
    QList<PropertyAnimInfo> frames;
    frames.append(PropertyAnimInfo("X",keyFrames[0]));
    frames.append(PropertyAnimInfo("Y",keyFrames[1]));
    frames.append(PropertyAnimInfo("Z",keyFrames[2]));
    return frames;
}

ColorPropertyAnim::ColorPropertyAnim()
{
    keyFrames[0] = new FloatKeyFrame();
    keyFrames[1] = new FloatKeyFrame();
    keyFrames[2] = new FloatKeyFrame();
    keyFrames[3] = new FloatKeyFrame();
}

ColorPropertyAnim::~ColorPropertyAnim()
{
    delete[] keyFrames;
}

QColor ColorPropertyAnim::getValue(float time)
{
    return QColor(
                keyFrames[0]->getValueAt(time),
                keyFrames[1]->getValueAt(time),
                keyFrames[2]->getValueAt(time),
                keyFrames[3]->getValueAt(time)
                );
}

QList<PropertyAnimInfo> ColorPropertyAnim::getKeyFrames()
{
    QList<PropertyAnimInfo> frames;
    frames.append(PropertyAnimInfo("R",keyFrames[0]));
    frames.append(PropertyAnimInfo("G",keyFrames[1]));
    frames.append(PropertyAnimInfo("B",keyFrames[2]));
    frames.append(PropertyAnimInfo("A",keyFrames[3]));
    return frames;
}

PropertyAnimInfo::PropertyAnimInfo(QString name, FloatKeyFrame *keyFrame)
{
    this->name = name;
    this->keyFrame = keyFrame;
}

QString PropertyAnim::getName() const
{
    return name;
}

void PropertyAnim::setName(const QString &value)
{
    name = value;
}


}
