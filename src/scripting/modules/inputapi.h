/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_INPUTAPI_H
#define SCRIPTING_INPUTAPI_H

// input.* — the gameplay action map (AVATAR_LOCOMOTION_SPEC §8.2/§10).
//
// FOUR actions and no more: Move, Look, Jump, Sprint. This module reads and
// writes iris::InputSystem's map (bindings — persisted, conflict-checked) and
// reports the live state. It does NOT write the state: driving input from a
// script is `avatar.input(...)`, so the producer verb sits beside the thing it
// drives rather than in the binding namespace.
//
// Not the ShortcutRegistry: that one binds editor COMMANDS to QKeySequences.
// The Preferences → Shortcuts page lists the gameplay rows read-only and
// MainWindow::refreshGameplayShortcutRows re-labels them after every bind here.

#include <QVariantList>
#include <QVariantMap>

#include "scripting/apimodule.h"

class InputApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("input"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QVariantList bindings();
    Q_INVOKABLE bool bind(const QString &action, const QVariant &binding);
    Q_INVOKABLE bool resetBindings();
    Q_INVOKABLE QVariantMap state();

private:
    /// Re-labels the Preferences Gameplay rows after a bind. No-op headless.
    void refreshShortcutRows();
};

#endif // SCRIPTING_INPUTAPI_H
