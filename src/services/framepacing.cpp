#include "services/framepacing.h"

#include <cmath>

namespace framepacing {

QString modeName(Mode m)
{
    return m == Mode::Unlimited ? QStringLiteral("unlimited") : QStringLiteral("display");
}

QString modeLabel(Mode m)
{
    return m == Mode::Unlimited ? QStringLiteral("Unlimited (no vsync)")
                                : QStringLiteral("Display refresh rate");
}

Mode modeFromName(const QString &name, bool *ok)
{
    const QString n = name.trimmed().toLower();
    if (ok) *ok = true;
    if (n == QLatin1String("display")) return Mode::Display;
    if (n == QLatin1String("unlimited")) return Mode::Unlimited;
    if (ok) *ok = false;
    return Mode::Display;
}

QStringList modeNames()
{
    return { QStringLiteral("display"), QStringLiteral("unlimited") };
}

int intervalMsFor(Mode m, double refreshHz)
{
    // Unlimited is a 0 ms timer: Qt fires it whenever the event loop has
    // nothing else pending, which is what makes it uncapped without starving
    // input (window-system and posted events are still processed first).
    if (m == Mode::Unlimited) return 0;
    if (!std::isfinite(refreshHz) || refreshHz <= 0.0) return kFallbackIntervalMs;
    const int ms = int(std::floor(1000.0 / refreshHz));
    if (ms < kMinIntervalMs) return kMinIntervalMs;
    if (ms > kMaxIntervalMs) return kMaxIntervalMs;
    return ms;
}

}   // namespace framepacing
