/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/engineapi.h"

#include "bridge/enginehost.h"
#include "jahshaka/engine/Engine.h"

QVector<VerbInfo> EngineApi::verbs() const
{
    return {
        { "arm", "engine.arm(name, value?) -> number | null",
          "A MEASUREMENT ARM: a named switch the engine reads once per frame, for paired A/B "
          "measurements in one process (scripts/perf-ab.py drives these). With `value` it SETS the "
          "arm and answers the value set — the engine LATCHES it at the top of the next frame, so a "
          "frame never sees two values of one arm and the change shows from the next "
          "editor.frame(); an unknown name or a value outside the arm's range is refused (null, "
          "the reason in app.lastError()) and changes nothing. Without `value` it READS the value "
          "the current frame uses. engine.arms() lists every arm with its default, range and what "
          "it changes. Nothing in the product sets an arm: every default is the shipped picture, "
          "and an arm left set is a measurement left running.",
          Needs::Engine },
        { "arms", "engine.arms() -> [{name, value, default, min, max, what}]",
          "Every registered measurement arm: its `name` (engine.arm's key), the `value` the "
          "current frame reads, its `default` (the shipped picture), its `min`/`max` and `what` it "
          "changes, with the suite that measures it.",
          Needs::Engine },
    };
}

QVariant EngineApi::arm(const QString &name, const QVariant &value)
{
    auto engine = EngineHost::instance().engine();
    if (!engine) { fail(QStringLiteral("engine.arm: no engine in this session")); return jsNull(); }
    const std::string key = name.toStdString();
    if (value.isValid() && !value.isNull()) {
        bool ok = false;
        const double v = value.toDouble(&ok);
        if (!ok) { refuse(QStringLiteral("engine.arm: %1 needs a number").arg(name)); return jsNull(); }
        if (!engine->setArm(key, v)) {
            refuse(QStringLiteral("engine.arm: %1").arg(QString::fromStdString(engine->takeLastError())));
            return jsNull();
        }
        return v;
    }
    for (const jahshaka::engine::ArmInfo &a : engine->arms())
        if (a.name == key) return a.value;
    refuse(QStringLiteral("engine.arm: no arm named '%1' (engine.arms() lists them)").arg(name));
    return jsNull();
}

QVariantList EngineApi::arms()
{
    QVariantList out;
    auto engine = EngineHost::instance().engine();
    if (!engine) { fail(QStringLiteral("engine.arms: no engine in this session")); return out; }
    for (const jahshaka::engine::ArmInfo &a : engine->arms())
        out.push_back(QVariantMap{ { QStringLiteral("name"), QString::fromStdString(a.name) },
                                   { QStringLiteral("value"), a.value },
                                   { QStringLiteral("default"), a.defaultValue },
                                   { QStringLiteral("min"), a.minValue },
                                   { QStringLiteral("max"), a.maxValue },
                                   { QStringLiteral("what"), QString::fromStdString(a.what) } });
    return out;
}
