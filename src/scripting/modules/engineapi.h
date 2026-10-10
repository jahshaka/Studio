/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_ENGINEAPI_H
#define SCRIPTING_ENGINEAPI_H

// engine.* — the engine's MEASUREMENT ARMS (lane TEST-1, the perf audit's A2).
//
// An arm is a named switch the engine latches once per frame, registered with its
// default (Engine::setArm / Engine::arms, irisgl Types.h ArmInfo). It replaced the
// measuring doors that were environment reads inside per-frame engine code, so two
// arms of an A/B are two verb calls in ONE process — what scripts/perf-ab.py drives.
// Nothing in the product sets an arm: every default is the shipped picture.
//
// engine.validation() — THE VALIDATION LAYER'S PROOF (TESTING-CLEANUP-2 H4; Engine::validation):
// a script that runs under the Khronos validation layer asserts the layer is really live.

#include <QVariantList>
#include <QVariantMap>

#include "scripting/apimodule.h"

class EngineApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("engine"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QVariant arm(const QString &name, const QVariant &value = QVariant());
    Q_INVOKABLE QVariantList arms();
    Q_INVOKABLE QVariantMap validation();
};

#endif // SCRIPTING_ENGINEAPI_H
