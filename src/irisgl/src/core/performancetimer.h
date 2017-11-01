#ifndef PERFORMANCETIMER_H
#define PERFORMANCETIMER_H

#include <QElapsedTimer>
#include <QMap>

namespace iris {

class PerformanceTimer
{
public:
    QElapsedTimer timer;
    QMap<QString, qint64> startTimes;
    QMap<QString, qint64> endTimes;

    PerformanceTimer()
    {
        reset();
    }

    void start(QString name)
    {
        startTimes.insert(name, timer.nsecsElapsed());
    }

    void end(QString name)
    {
        endTimes.insert(name, timer.nsecsElapsed());
    }

    void reset()
    {
        startTimes.clear();
        endTimes.clear();
        timer.restart();
    }

    void report()
    {
        qDebug() << "=======================";
        for( auto key : endTimes.keys()) {
            auto diff = endTimes[key] - startTimes[key];
            auto time = diff /(1000.0f * 1000.0f * 1000.0f);
            qDebug() << key << ": "<<time<<"s";
        }
        qDebug() << "=======================";
    }
};

}
#endif // PERFORMANCETIMER_H
