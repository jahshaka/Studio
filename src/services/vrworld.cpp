/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/vrworld.h"

#include <cmath>

namespace vrworld {

namespace {

/// THE SESSION'S STATE — the adopted defaults and the overrides on top of
/// them. See the header for why it is file scope: one session per process, and
/// three per-frame consumers with no services plumbed through.
struct SessionState
{
    Settings defaults;                  ///< the project's, latched at adopt()
    bool latched = false;               ///< is a session running on them?
    QHash<QString, double> overrides;   ///< by row id; only what somebody set
};

SessionState &session()
{
    static SessionState state;
    return state;
}

QVector<Row> buildRows()
{
    QVector<Row> r;

    // ---- THE FLY --------------------------------------------------------
    {
        Row row;
        row.id = QStringLiteral("flySpeed");
        row.label = QStringLiteral("Fly Speed");
        row.kind = RowKind::Number;
        row.minValue = 0.1;
        row.maxValue = 100.0;
        row.perPixelStep = 0.25;
        row.decimals = 2;
        row.unit = QStringLiteral("m/s");
        row.doc = QStringLiteral(
            "How fast the thumbstick and the fly keys walk the wearer, in METRES PER SECOND "
            "(the document's own unit; the stick's boost multiplies it). 15 is the default: a "
            "brisk walk is 1.5 and a car in town is 14, so it crosses an authored set without "
            "being the speed people report as unpleasant in a headset. It replaced the desktop "
            "editor's 8 u/s camera speed, which the headset borrowed only because nothing else "
            "existed.");
        row.get = [](const iris::ScenePtr &s) { return double(s->vrFlySpeed); };
        row.set = [](const iris::ScenePtr &s, double v) { s->vrFlySpeed = float(v); };
        row.read = [](const Settings &s) { return double(s.flySpeed); };
        row.write = [](Settings &s, double v) { s.flySpeed = float(v); };
        r.append(row);
    }
    {
        Row row;
        row.id = QStringLiteral("fly");
        row.label = QStringLiteral("Fly Direction");
        row.kind = RowKind::Enum;
        row.options = { { QStringLiteral("aim"), QStringLiteral("Aim"),
                          int(iris::VrFlyMode::Aim) },
                        { QStringLiteral("gaze"), QStringLiteral("Gaze"),
                          int(iris::VrFlyMode::Gaze) },
                        { QStringLiteral("level"), QStringLiteral("Level"),
                          int(iris::VrFlyMode::Level) } };
        row.doc = QStringLiteral(
            "WHERE THE STICK FLIES. \"aim\" follows the stick hand's own aim ray (Unreal's VR "
            "editor, and the default); \"gaze\" follows where the wearer is looking; \"level\" "
            "walks along the head's heading with the pitch thrown away — the comfort option, "
            "and the one a wearer cannot accidentally fly through their own floor with.");
        row.get = [](const iris::ScenePtr &s) { return double(int(s->vrFlyMode)); };
        row.set = [](const iris::ScenePtr &s, double v) {
            s->vrFlyMode = iris::VrFlyMode(int(std::lround(v)));
        };
        row.read = [](const Settings &s) { return double(int(s.fly)); };
        row.write = [](Settings &s, double v) { s.fly = iris::VrFlyMode(int(std::lround(v))); };
        r.append(row);
    }

    // ---- THE TURN -------------------------------------------------------
    {
        Row row;
        row.id = QStringLiteral("turn");
        row.label = QStringLiteral("Turning");
        row.kind = RowKind::Enum;
        row.options = { { QStringLiteral("snap"), QStringLiteral("Snap"),
                          int(iris::VrTurnMode::Snap) },
                        { QStringLiteral("smooth"), QStringLiteral("Smooth"),
                          int(iris::VrTurnMode::Smooth) } };
        row.doc = QStringLiteral(
            "HOW A FLICK OF THE STICK TURNS THE WEARER. \"snap\" steps by whole degrees per "
            "flick — the default, and what nearly every shipping VR tool does, because a "
            "continuous turn makes a proportion of people sick. \"smooth\" turns continuously "
            "while the stick is held. Either way the wearer turns about their OWN HEAD and not "
            "about the middle of their room.");
        row.get = [](const iris::ScenePtr &s) { return double(int(s->vrTurnMode)); };
        row.set = [](const iris::ScenePtr &s, double v) {
            s->vrTurnMode = iris::VrTurnMode(int(std::lround(v)));
        };
        row.read = [](const Settings &s) { return double(int(s.turn)); };
        row.write = [](Settings &s, double v) { s.turn = iris::VrTurnMode(int(std::lround(v))); };
        r.append(row);
    }
    {
        Row row;
        row.id = QStringLiteral("snapTurnDegrees");
        row.label = QStringLiteral("Snap Turn");
        row.kind = RowKind::Number;
        row.minValue = 1.0;
        row.maxValue = 180.0;
        row.perPixelStep = 1.0;
        row.decimals = 0;
        row.unit = QStringLiteral("degrees");
        row.doc = QStringLiteral(
            "Degrees per flick while the turn is SNAP. 30 is the default — twelve of them is a "
            "full circle, small enough to aim with and large enough to be worth the gesture.");
        row.enabled = [](const iris::ScenePtr &s) {
            return s->vrTurnMode == iris::VrTurnMode::Snap;
        };
        row.get = [](const iris::ScenePtr &s) { return double(s->vrSnapTurnDegrees); };
        row.set = [](const iris::ScenePtr &s, double v) { s->vrSnapTurnDegrees = float(v); };
        row.read = [](const Settings &s) { return double(s.snapTurnDegrees); };
        row.write = [](Settings &s, double v) { s.snapTurnDegrees = float(v); };
        r.append(row);
    }
    {
        Row row;
        row.id = QStringLiteral("smoothTurnDegreesPerSecond");
        row.label = QStringLiteral("Smooth Turn Rate");
        row.kind = RowKind::Number;
        row.minValue = 1.0;
        row.maxValue = 720.0;
        row.perPixelStep = 2.0;
        row.decimals = 0;
        row.unit = QStringLiteral("degrees/second");
        row.doc = QStringLiteral(
            "Degrees per second at full stick while the turn is SMOOTH. 90 is the default.");
        row.enabled = [](const iris::ScenePtr &s) {
            return s->vrTurnMode == iris::VrTurnMode::Smooth;
        };
        row.get = [](const iris::ScenePtr &s) {
            return double(s->vrSmoothTurnDegreesPerSecond);
        };
        row.set = [](const iris::ScenePtr &s, double v) {
            s->vrSmoothTurnDegreesPerSecond = float(v);
        };
        row.read = [](const Settings &s) { return double(s.smoothTurnDegreesPerSecond); };
        row.write = [](Settings &s, double v) { s.smoothTurnDegreesPerSecond = float(v); };
        r.append(row);
    }

    // ---- THE HANDS ------------------------------------------------------
    {
        Row row;
        row.id = QStringLiteral("dominant");
        row.label = QStringLiteral("Dominant Hand");
        row.kind = RowKind::Enum;
        row.options = { { QStringLiteral("right"), QStringLiteral("Right"), 1 },
                        { QStringLiteral("left"), QStringLiteral("Left"), 0 } };
        row.doc = QStringLiteral(
            "WHICH HAND MANIPULATES. \"right\" (the default) points, selects and grabs with "
            "the right hand while the LEFT stick walks and turns; \"left\" swaps BOTH roles at "
            "once. One setting, because two would eventually disagree.");
        row.get = [](const iris::ScenePtr &s) { return s->vrDominantRight ? 1.0 : 0.0; };
        row.set = [](const iris::ScenePtr &s, double v) {
            s->vrDominantRight = std::lround(v) != 0;
        };
        row.read = [](const Settings &s) { return s.dominantRight ? 1.0 : 0.0; };
        row.write = [](Settings &s, double v) { s.dominantRight = std::lround(v) != 0; };
        r.append(row);
    }

    return r;
}

}   // namespace

const QVector<Row> &rows()
{
    static const QVector<Row> table = buildRows();
    return table;
}

const Row *row(const QString &id)
{
    for (const Row &r : rows())
        if (r.id == id) return &r;
    return nullptr;
}

QStringList ids()
{
    QStringList out;
    for (const Row &r : rows()) out << r.id;
    return out;
}

QString propsKey(const QString &rowId) { return QStringLiteral("vr.") + rowId; }

bool validate(const Row &r, const QVariant &value, double &out, QString &error)
{
    if (r.kind == RowKind::Enum) {
        const QString name = value.toString().trimmed().toLower();
        for (const EnumOption &o : r.options)
            if (o.id == name) { out = double(o.value); return true; }
        QStringList known;
        for (const EnumOption &o : r.options) known << QStringLiteral("\"%1\"").arg(o.id);
        error = QStringLiteral("%1 must be %2, not '%3'")
                    .arg(r.id, known.join(QStringLiteral(" or ")), value.toString());
        return false;
    }
    bool ok = false;
    const double v = value.toDouble(&ok);
    if (!ok || !std::isfinite(v)) {
        error = QStringLiteral("%1 must be a finite number, not '%2'")
                    .arg(r.id, value.toString());
        return false;
    }
    if (v <= 0.0) {
        // A SPEED OR A RATE OF ZERO IS NOT A SETTING, it is a wearer who cannot
        // move, and a negative one is a wearer who walks backwards for ever:
        // refused rather than clamped, because either is a typo and not a
        // preference. Every row in this table is a positive quantity.
        error = QStringLiteral("%1 must be greater than zero, not %2").arg(r.id).arg(v);
        return false;
    }
    // MERELY OUT OF RANGE IS CLAMPED, not refused: these are continuous dials
    // and the panel's own drag row cannot leave the range either, so clamping
    // is the only rule the two consumers can share.
    out = qBound(r.minValue, v, r.maxValue);
    return true;
}

QString valueId(const Row &r, double value)
{
    if (r.kind == RowKind::Enum) {
        const int v = int(std::lround(value));
        for (const EnumOption &o : r.options)
            if (o.value == v) return o.id;
    }
    return QString::number(value);
}

QVariant valueOf(const Row &r, const Settings &s)
{
    const double v = r.read ? r.read(s) : 0.0;
    return r.kind == RowKind::Enum ? QVariant(valueId(r, v)) : QVariant(v);
}

Settings fromScene(const iris::ScenePtr &scene)
{
    Settings s;
    if (!scene) return s;
    for (const Row &r : rows())
        if (r.get && r.write) r.write(s, r.get(scene));
    return s;
}

void adopt(const iris::ScenePtr &scene)
{
    session().defaults = fromScene(scene);
    session().latched = true;
    session().overrides.clear();
}

void release()
{
    session().latched = false;
    session().defaults = Settings();
    session().overrides.clear();
}

bool adopted() { return session().latched; }

Settings resolve(const iris::ScenePtr &scene)
{
    Settings s = session().latched ? session().defaults : fromScene(scene);
    for (const Row &r : rows()) {
        const auto it = session().overrides.constFind(r.id);
        if (it != session().overrides.constEnd() && r.write) r.write(s, it.value());
    }
    return s;
}

void override(const QString &rowId, double value)
{
    if (!row(rowId)) return;
    session().overrides.insert(rowId, value);
}

QStringList overridden()
{
    QStringList out;
    for (const Row &r : rows())
        if (session().overrides.contains(r.id)) out << r.id;
    return out;
}

void clearOverrides() { session().overrides.clear(); }

}   // namespace vrworld
