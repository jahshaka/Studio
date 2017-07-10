#ifndef FLOATCURVE_H
#define FLOATCURVE_H

namespace iris
{


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

class CurveKey
{
public:
    float value;
    float time;

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

bool CurveKeyCompare(const CurveKey * const & a, const CurveKey * const & b)
{
   return a->time < b->time;
}


class FloatCurve
{
public:
    QString name;
    std::vector<CurveKey*> keys;
    float length;

    KeyFrame()
    {
        length = 10;//default value
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

    void setLength(float seconds)
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

    CurveKey* addKey(float value,float time)
    {
        auto key = new CurveKey();
        key->value = value;
        key->time = time;
        keys.push_back(key);

        // if the new key's time isnt greater than the last key's time,
        // do a sort
        if(keys.size() >= 2 &&
           keys[keys.size()-2]->time > keys[keys.size()-1]->time)
        {
            this->sortKeys();
        }

        return key;
    }

    bool hasKeys()
    {
        return keys.size();
    }

    float getValueAt(float time)
    {
        return getValueAt(time, 0);
    }

    float getValueAt(float time, float defaultVal)
    {
        CurveKey* leftKey = Q_NULLPTR;
        CurveKey* rightKey = Q_NULLPTR;

        //why bother with a copy?
        float val = defaultVal;

        this->getKeyFramesAtTime(&leftKey,&rightKey,time);

        if(leftKey!=Q_NULLPTR && rightKey==Q_NULLPTR)
        {
            val = leftKey->value;
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

    float getFirstKeyTime()
    {
        Q_ASSERT(keys.size()>0);

        return keys.begin().time;
    }

    float getLastKeyTime()
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
    float interpolate(float a,float b,float t)
    {
        return a+(b-a)*t;
    }
};

}

#endif // FLOATCURVE_H
