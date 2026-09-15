/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTINGSETTINGSWIDGET_H
#define SCRIPTINGSETTINGSWIDGET_H

// The Scripting page of the Preferences dialog (SCRIPTING_LIVE_SPEC §3.1):
// ONE setting, "Live script feedback", over the same state app.scriptPolicy
// reads and writes. It decides whether the render loop keeps drawing while a
// script runs — for the console and for MCP run_script; --script and --headless
// are always off, deliberately, because a deterministic run must not have
// frames happening between its verbs.

#include <QWidget>

class QAbstractButton;
class QShowEvent;
class SettingsManager;
class ScriptEngine;

class ScriptingSettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ScriptingSettingsWidget(SettingsManager *settings, QWidget *parent = nullptr);

    /// Late wiring: the dialog is built before the scripting stack exists.
    void wireScripting(ScriptEngine *engine);

    /// Persists script_feedback_live and pushes it into the live engine.
    void saveSettings();

protected:
    /// Every page's saveSettings() runs on ANY OK, so every page must show the
    /// LIVE state when the dialog opens — a scripted app.scriptPolicy('off')
    /// would otherwise be written back stale.
    void showEvent(QShowEvent *event) override;

private:
    SettingsManager *mSettings;
    ScriptEngine *mEngine = nullptr;
    QAbstractButton *mLive;
};

#endif // SCRIPTINGSETTINGSWIDGET_H
