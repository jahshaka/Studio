/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef KEYFRAMEANIMATION_H
#define KEYFRAMEANIMATION_H

#include <QString>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QQuaternion>
#include <QColor>

#include "../math/bezierhelper.h"

namespace iris
{

enum class KeyFrameType
{
    Float,
    Int,
    String
};

enum class TangentType
{
    Free,
    Linear,
    Constant
};

enum class HandleMode
{
    Joined,
    Broken
};

template<typename T>
class Key
{
public:
    T value;
    double time;

    TangentType leftTangent;
    TangentType rightTangent;

    float leftSlope;
    float rightSlope;

    HandleMode handleMode;

    inline bool operator< ( const T& rhs)
    {
        return this->time < rhs.time;
    }
};

template <typename T> bool KeyCompare(const Key<T> * const & a, const Key<T> * const & b)
{
   return a->time < b->time;
}


template<typename T>
class KeyFrame
{
public:
    QString name;
    std::vector<Key<T>*> keys;
    float length;//in seconds

    KeyFrame()
    {
        length = 15;//for now
    }

    void clear()
    {
        for (size_t i=0;i<keys.size();i++)
        {
            delete keys[i];
        }
        keys.clear();
    }

    float getLength()
    {
        return length;
    }

    void setLenth(float seconds)
    {
        length = seconds;
    }

    void autoAdjustLength()
    {
        if(keys.size()==0)
            return;

        //sort keys
        this->sortKeys();
        //get last key and use that to determine length
        auto last = keys.end().c;
        length = last->time;
    }

    Key<T>* addKey(T value,double time)
    {
        auto key = new Key<T>();
        key->value = value;
        key->time = time;
        keys.push_back(key);

        //todo: make this faster
        this->sortKeys();
        return key;
    }

    bool hasKeys()
    {
        return keys.size();
    }

    T getValueAt(double time)
    {
        return getValueAt(time,T());
    }

    T getValueAt(double time,T defaultVal)
    {
        Key<T>* leftKey = Q_NULLPTR;
        Key<T>* rightKey = Q_NULLPTR;

        //why bother with a copy?
        T val = defaultVal;

        this->getKeyFramesAtTime(&leftKey,&rightKey,time);

        if(leftKey!=Q_NULLPTR && rightKey==Q_NULLPTR)
        {
            val = leftKey->value;
        }

        if(leftKey!=Q_NULLPTR && rightKey!=Q_NULLPTR)
        {
            //linearly interpolate between frames
            float t =0;
            float timeDiff = rightKey->time - leftKey->time;

            //frameDiff could be 0!!
            if(timeDiff != 0)
            {
                t = (time-leftKey->time)/timeDiff;
            }

            if(leftKey->rightTangent == TangentType::Constant ||
               rightKey->leftTangent == TangentType::Constant) {
                val = leftKey->value;
            } else {

                // 1D beziers are a third of the distance apart
                float third = timeDiff * 0.333333f;

                val = BezierHelper::calculateBezier(leftKey->value,
                                                    leftKey->value + (leftKey->rightSlope * third * timeDiff),
                                                    rightKey->value - (rightKey->leftSlope * third * timeDiff),
                                                    rightKey->value,
                                                    t);
                //val = interpolate(leftKey->value,rightKey->value,t2);
            }
        }

        return val;
    }

    void getKeyFramesAtTime(Key<T>** firstKey,Key<T>** lastKey,float time)
    {
        int numKeys = keys.size();

        if(numKeys==0)
            return;

        if(numKeys==1)
        {
            *firstKey = keys[0];
            return;
        }

        //before first key
        //todo: wrap around
        if(time<=keys[0]->time)
        {
            *firstKey = keys[0];
            return;
        }

        //after last key
        //todo: wrap around
        if(time>=keys[numKeys-1]->time)
        {
            *firstKey = keys[numKeys-1];
            return;
        }

        //find first key and last key
        for(size_t k = 0;k<keys.size();k++)
        {
            Key<T>* key = keys[k];

            if(key->time<=time)
                *firstKey = key;
            else
            {
                *lastKey = key;
                break;
            }
        }

    }

    void sortKeys()
    {
        std::sort(keys.begin(),keys.end(),KeyCompare<T>);
    }

    double getFirstKeyTime()
    {
        Q_ASSERT(keys.size()>0);

        return keys.begin().time;
    }

    double getLastKeyTime()
    {
        Q_ASSERT(keys.size()>0);

        return keys.end().time;
    }

    virtual ~KeyFrame()
    {
        for(auto iter = keys.begin();iter!=keys.end();iter++)
            delete *iter;
    }

protected:
    virtual T interpolate(T a,T b,float t)=0;
};

//typedef Key<float> FloatKey;

class FloatKeyFrame:public KeyFrame<float>
{
public:
    float interpolate(float a,float b,float t)
    {
        return a+(b-a)*t;
    }
};

class IntKeyFrame:public KeyFrame<int>
{
public:
    int interpolate(int a,int b,float t)
    {
        return a+(b-a)*t;
    }
};

class BoolKeyFrame:public KeyFrame<bool>
{
public:
    bool interpolate(bool a,bool b,float t)
    {
        Q_UNUSED(b);
        Q_UNUSED(t);
        //booleans favor first value
        return a;
    }
};

class StringKeyFrame:public KeyFrame<QString>
{
public:
    QString interpolate(QString a,QString b,float t)
    {
        Q_UNUSED(b);
        Q_UNUSED(t);
        return a;
    }
};

class Vector2DKeyFrame:public KeyFrame<QVector2D>
{
public:
    QVector2D interpolate(QVector2D a,QVector2D b,float t)
    {
        return a+(b-a)*t;
    }
};

class Vector3DKeyFrame:public KeyFrame<QVector3D>
{
public:
    QVector3D interpolate(QVector3D a,QVector3D b,float t)
    {
        return a+((b-a)*t);
    }
};

class Vector4DKeyFrame:public KeyFrame<QVector4D>
{
public:
    QVector4D interpolate(QVector4D a,QVector4D b,float t)
    {
        return a+(b-a)*t;
    }
};

class QuaternionKeyFrame:public KeyFrame<QQuaternion>
{
public:
    QQuaternion interpolate(QQuaternion a,QQuaternion b,float t)
    {
        //slerp instead of lerp
        return QQuaternion::slerp(a,b,t);
    }
};

class ColorKeyFrame:public KeyFrame<QColor>
{
public:
    QColor interpolate(QColor a,QColor b,float t)
    {
        //return a+(b-a)*t;
        return QColor(
                    a.red()+(int)((b.red()-a.red())*t),
                    a.green()+(int)((b.green()-a.green())*t),
                    a.blue()+(int)((b.blue()-a.blue())*t),
                    a.alpha()+(int)((b.alpha()-a.alpha())*t)
                    );
    }
};

}
#endif // KEYFRAMEANIMATION_H
