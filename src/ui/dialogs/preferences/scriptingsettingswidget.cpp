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
    mLive->setChecked(mSettings->getValue("script_feedback_live", true).toBool());
    layout->addWidget(mLive);

    auto *note = new QLabel(
        "On, the viewport keeps drawing while the script runs, so you watch it build the "
        "scene. Off, the picture holds still until the run ends — faster, and the only way "
        "to get a run where nothing at all happens between two verbs.\n\n"
        "Applies to the script console and to Claude's run_script. Scripts run from the "
        "command line (--script, --headless) are always off, so their frame counts stay "
        "exact.\n\n"
        "While a live run is going, an edit you make by hand joins the script's undo step — "
        "the run is the open undo entry until it ends.", this);
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
    else mLive->setChecked(mSettings->getValue("script_feedback_live", true).toBool());
}

void ScriptingSettingsWidget::saveSettings()
{
    const bool live = mLive->isChecked();
    mSettings->setValue("script_feedback_live", live);
    if (mEngine)
        mEngine->setInteractivePolicy(live ? ScriptRunPolicy::Live : ScriptRunPolicy::Off);
}
