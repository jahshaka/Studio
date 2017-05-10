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

FloatKeyFrame *FloatPropertyAnim::getKeyFrame(QString name)
{
    //returns keyframe regardless of name
    return keyFrame;
}

FloatKeyFrame *FloatPropertyAnim::getKeyFrame(int index)
{
    if (index == 0) return keyFrame;
    return nullptr;
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

FloatKeyFrame *Vector3DPropertyAnim::getKeyFrame(QString name)
{
    if (name=="X") return keyFrames[0];
    if (name=="Y") return keyFrames[1];
    if (name=="Z") return keyFrames[2];

    return nullptr;
}

FloatKeyFrame *Vector3DPropertyAnim::getKeyFrame(int index)
{
    if (index < 0 || index > 3)
        return nullptr;

    return keyFrames[index];
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
                keyFrames[0]->getValueAt(time) * 255,
                keyFrames[1]->getValueAt(time) * 255,
                keyFrames[2]->getValueAt(time) * 255,
                keyFrames[3]->getValueAt(time) * 255
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

FloatKeyFrame *ColorPropertyAnim::getKeyFrame(QString name)
{
    if (name=="R") return keyFrames[0];
    if (name=="G") return keyFrames[1];
    if (name=="B") return keyFrames[2];
    if (name=="A") return keyFrames[3];

    return nullptr;
}

FloatKeyFrame *ColorPropertyAnim::getKeyFrame(int index)
{
    if (index < 0 || index > 4)
        return nullptr;

    return keyFrames[index];
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
