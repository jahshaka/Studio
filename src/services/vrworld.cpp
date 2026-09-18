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

#include <QtGlobal>

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
        row.jsonKey = QStringLiteral("vrFlySpeed");
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
        row.jsonKey = QStringLiteral("vrFlyMode");
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
        row.jsonKey = QStringLiteral("vrTurnMode");
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
        row.jsonKey = QStringLiteral("vrSnapTurnDegrees");
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
        row.jsonKey = QStringLiteral("vrSmoothTurnDegreesPerSecond");
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
        row.jsonKey = QStringLiteral("vrDominantHand");
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
    {
        Row row;
        row.id = QStringLiteral("hands");
        row.jsonKey = QStringLiteral("vrHands");
        row.label = QStringLiteral("Hands");
        row.kind = RowKind::Flag;
        // A SESSION LATCHES IT AT CREATION and cannot be told otherwise: the
        // suggested bindings are attached to the session's action sets before
        // its first frame. So `vr.locomotion({hands:...})` is refused by name
        // instead of quietly doing nothing for the life of a session.
        row.sessionFixed = true;
        row.doc = QStringLiteral(
            "BARE-HAND TRACKING. Off by default: a session binds the wearer's own hands only "
            "when this is on, and the controllers are unaffected either way. With it off a "
            "wearer who puts a controller down is left holding nothing — which is what "
            "\"controllers only\" has to mean — instead of being handed to bare hands "
            "mid-session by the runtime. A session reads it when it BEGINS, so turning it on "
            "reaches the next session and not the one in the headset.");
        row.get = [](const iris::ScenePtr &s) { return s->vrHands ? 1.0 : 0.0; };
        row.set = [](const iris::ScenePtr &s, double v) { s->vrHands = std::lround(v) != 0; };
        row.read = [](const Settings &s) { return s.hands ? 1.0 : 0.0; };
        row.write = [](Settings &s, double v) { s.hands = std::lround(v) != 0; };
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

void write(const iris::ScenePtr &scene, QJsonObject &sceneObj)
{
    if (!scene) return;
    for (const Row &r : rows()) {
        if (!r.get || r.jsonKey.isEmpty()) continue;
        const double v = r.get(scene);
        // An Enum rides as its stable option id, a Flag as a JSON boolean, a
        // Number as a number — each as the thing it is, so a file reads as what
        // the panel shows.
        if (r.kind == RowKind::Enum)      sceneObj[r.jsonKey] = valueId(r, v);
        else if (r.kind == RowKind::Flag) sceneObj[r.jsonKey] = (v != 0.0);
        else                              sceneObj[r.jsonKey] = v;
    }
}

void read(const iris::ScenePtr &scene, const QJsonObject &sceneObj)
{
    if (!scene) return;
    for (const Row &r : rows()) {
        if (!r.set || r.jsonKey.isEmpty() || !sceneObj.contains(r.jsonKey)) continue;
        const QJsonValue value = sceneObj.value(r.jsonKey);
        if (r.kind == RowKind::Enum) {
            // BY NAME, and a name this build does not know leaves the
            // constructor's value standing — the tolerance `rayTracing` and the
            // play mode carry, for the same reason: a file from a later build
            // must open, at the default, rather than at zero.
            const QString name = value.toString().trimmed().toLower();
            for (const EnumOption &o : r.options)
                if (o.id == name) { r.set(scene, double(o.value)); break; }
            continue;
        }
        if (r.kind == RowKind::Flag) {
            // A BOOLEAN, AND NOTHING ELSE: a key carrying a number or a word
            // leaves the constructor's value standing, exactly as an unknown
            // enum name does. THE READER-DEFAULTS LAW is the same here — an
            // absent key is never read as false, it is simply not read.
            if (value.isBool()) r.set(scene, value.toBool() ? 1.0 : 0.0);
            continue;
        }
        if (!value.isDouble()) continue;          // not a number: the default stands
        const double v = value.toDouble();
        if (!std::isfinite(v)) continue;
        r.set(scene, qBound(r.minValue, v, r.maxValue));
    }
}

bool validate(const Row &r, const QVariant &value, double &out, QString &error)
{
    if (r.kind == RowKind::Flag) {
        // TRUE OR FALSE, BY TYPE. `QVariant("no").toBool()` is true and
        // `QVariant(0.3).toBool()` is true as well, so coercion here would
        // accept almost anything and mean almost nothing.
        if (value.metaType().id() != QMetaType::Bool) {
            error = QStringLiteral("%1 must be true or false, not '%2'")
                        .arg(r.id, value.toString());
            return false;
        }
        out = value.toBool() ? 1.0 : 0.0;
        return true;
    }
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
    // A BOOLEAN IS NOT A NUMBER (the Fable read of VR-WORLD-1, item 4).
    // `QVariant(true).toDouble()` succeeds and yields 1, so
    // `world.vr({flySpeed:true})` used to set a wearer walking at 1 m/s — a
    // typo taken as a setting. Refused by name, like every other wrong type.
    if (value.metaType().id() == QMetaType::Bool) {
        error = QStringLiteral("%1 must be a number, not a true/false").arg(r.id);
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
    if (r.kind == RowKind::Flag) return value != 0.0 ? QStringLiteral("true")
                                                     : QStringLiteral("false");
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
    if (r.kind == RowKind::Enum) return QVariant(valueId(r, v));
    if (r.kind == RowKind::Flag) return QVariant(v != 0.0);
    return QVariant(v);
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
    // A SESSION BEGINS ON THE PROJECT IT FINDS — and on any override the
    // caller has ALREADY asked for. Clearing them here (which this did until
    // the Fable read of VR-WORLD-1, item 3) killed a `vr.locomotion({...})`
    // issued in the breath before `vr.begin`, silently: the script asked for a
    // slower fly and got the project's. The overrides are dropped by
    // `release()` instead, which is the moment they stop meaning anything —
    // one rule, at the end of the session that made them.
    //
    // A null scene is not reachable (both hosts have the document they began
    // on) and would mean "adopt the shipped defaults", which is never what a
    // caller means; it is worth a line in the log rather than a silent one.
    if (!scene)
        qWarning("vrworld::adopt: no scene — the session will run on the shipped defaults");
    session().defaults = fromScene(scene);
    session().latched = true;
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
