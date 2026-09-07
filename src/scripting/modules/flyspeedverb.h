/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_FLYSPEEDVERB_H
#define SCRIPTING_FLYSPEEDVERB_H

// The shared body of editor.flySpeed/setFlySpeed and player.flySpeed/
// setFlySpeed (owner request 2026-09-07). The two verbs are the SAME verb over
// two FlySpeedSettings surfaces, and the argument grammar — a number, or
// "faster"/"slower" — must not be allowed to drift between them, so it lives
// once, here, rather than twice in the two API modules.

#include <QMetaType>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include "scripting/modules/moduleshared.h"
#include "viewport/flyspeedsettings.h"

namespace flyspeedverb {

/// The map both read verbs return.
inline QVariantMap state(FlySpeedSettings::Surface surface)
{
    QVariantList steps;
    for (float s : FlySpeedSettings::steps()) steps.append(double(s));
    return QVariantMap{
        { QStringLiteral("multiplier"), double(FlySpeedSettings::multiplier(surface)) },
        { QStringLiteral("base"),       double(FlySpeedSettings::baseSpeed(surface)) },
        { QStringLiteral("speed"),      double(FlySpeedSettings::speed(surface)) },
        { QStringLiteral("steps"),      steps },
    };
}

/// Applies a setFlySpeed argument. Returns false with `error` filled — the
/// caller prefixes its own verb name, so the message reads the same either way.
inline bool apply(FlySpeedSettings::Surface surface, const QVariant &arg, QString &error)
{
    const QVariant value = scriptmod::normalizeJs(arg);
    if (value.typeId() == QMetaType::QString) {
        const QString word = value.toString().trimmed().toLower();
        if (word == QLatin1String("faster") || word == QLatin1String("up")) {
            FlySpeedSettings::step(surface, 1);
            return true;
        }
        if (word == QLatin1String("slower") || word == QLatin1String("down")) {
            FlySpeedSettings::step(surface, -1);
            return true;
        }
        error = QStringLiteral("unknown speed '%1' (a multiplier, \"faster\" or \"slower\")")
                    .arg(value.toString());
        return false;
    }
    bool numeric = false;
    const double number = value.toDouble(&numeric);
    if (!numeric || !(number > 0.0)) {
        error = QStringLiteral("'%1' is not a speed multiplier (a positive number, \"faster\" "
                               "or \"slower\")").arg(value.toString());
        return false;
    }
    FlySpeedSettings::setMultiplier(surface, float(number));
    return true;
}

}   // namespace flyspeedverb

#endif // SCRIPTING_FLYSPEEDVERB_H
