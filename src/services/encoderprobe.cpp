#include "services/encoderprobe.h"

#include <QElapsedTimer>
#include <QList>
#include <QMediaFormat>

namespace encoderprobe {
namespace {
bool sWarmed = false;
double sMs = 0.0;
}   // namespace

bool warmed() { return sWarmed; }
double warmMs() { return sMs; }

double warm()
{
    if (sWarmed) return 0.0;
    sWarmed = true;
    QElapsedTimer t;
    t.start();
    (void)QMediaFormat(QMediaFormat::MPEG4).supportedVideoCodecs(QMediaFormat::Encode);
    sMs = double(t.nsecsElapsed()) / 1e6;
    return sMs;
}

}   // namespace encoderprobe
