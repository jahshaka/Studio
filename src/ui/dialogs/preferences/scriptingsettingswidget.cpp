/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/dialogs/preferences/scriptingsettingswidget.h"

#include <QCheckBox>
#include <QLabel>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <oclero/qlementine/widgets/Switch.hpp>

#include "data/settingsmanager.h"
#include "scripting/scriptengine.h"
#include "ui/style/stylesheet.h"
#include "ui/style/thememanager.h"

ScriptingSettingsWidget::ScriptingSettingsWidget(SettingsManager *settings, QWidget *parent)
    : QWidget(parent), mSettings(settings)
{
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        "Scripts run on their own thread: the editor keeps answering while one works, "
        "and the console's Run button becomes Stop.", this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    if (ThemeManager::classicActive())
        mLive = new QCheckBox("Live script feedback", this);
    else {
        auto *sw = new oclero::qlementine::Switch(this);
        sw->setText("Live script feedback");
        mLive = sw;
    }
    mLive->setObjectName(QStringLiteral("scriptFeedbackLive"));
    mLive->setChecked(mSettings->get(settingkeys::scriptFeedbackLive));
    layout->addWidget(mLive);

    auto *note = new QLabel(
        "On, the viewport keeps drawing while the script runs, so you watch it build the "
        "scene. Off, the picture holds still until the run ends — faster, and the only way "
        "to get a run where nothing at all happens between two verbs.\n\n"
        "Applies to the script console and to Claude's run_script. Scripts run from the "
        "command line (--script, --headless) are always off, so their frame counts stay "
        "exact.\n\n"
        "While a script runs the editor is read-only: you can look around, select things and "
        "switch pages, but an edit made by hand is refused until the run finishes. A toast "
        "says so the first time you try.", this);
    note->setWordWrap(true);
    note->setStyleSheet(StyleSheet::MutedInfoText());
    layout->addWidget(note);
    layout->addStretch(1);
}

void ScriptingSettingsWidget::wireScripting(ScriptEngine *engine)
{
    mEngine = engine;
    if (mEngine) {
        const QSignalBlocker block(mLive);
        mLive->setChecked(mEngine->interactivePolicy() == ScriptRunPolicy::Live);
    }
}

void ScriptingSettingsWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    const QSignalBlocker block(mLive);
    if (mEngine) mLive->setChecked(mEngine->interactivePolicy() == ScriptRunPolicy::Live);
    else mLive->setChecked(mSettings->get(settingkeys::scriptFeedbackLive));
}

void ScriptingSettingsWidget::saveSettings()
{
    const bool live = mLive->isChecked();
    mSettings->set(settingkeys::scriptFeedbackLive, live);
    if (mEngine)
        mEngine->setInteractivePolicy(live ? ScriptRunPolicy::Live : ScriptRunPolicy::Off);
}
