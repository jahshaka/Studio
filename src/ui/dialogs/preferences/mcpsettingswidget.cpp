/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/dialogs/preferences/mcpsettingswidget.h"

#include <QCheckBox>
#include "ui/style/thememanager.h"
#include <oclero/qlementine/widgets/Switch.hpp>
#include <QClipboard>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include "data/settingsmanager.h"
#include "scripting/mcp/mcplog.h"
#include "scripting/mcp/mcpserver.h"
#include "shell/mainwindow.h"
#include "ui/style/stylesheet.h"

McpSettingsWidget::McpSettingsWidget(SettingsManager *settings, QWidget *parent)
    : QWidget(parent), mSettings(settings)
{
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        "The MCP server lets Claude Code drive the editor through the scripting "
        "engine — and nothing else. It listens on 127.0.0.1 only and requires "
        "the per-session token below.", this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // Qlementine mode gets the real Switch widget (first adoption of the
    // qlementine utility widgets); Classic keeps its QCheckBox.
    if (ThemeManager::classicActive())
        mEnabled = new QCheckBox("Enable MCP server", this);
    else {
        auto *sw = new oclero::qlementine::Switch(this);
        sw->setText("Enable MCP server");
        mEnabled = sw;
    }
    mEnabled->setObjectName(QStringLiteral("mcpEnabled"));
    mEnabled->setChecked(mSettings->getValue("mcp_enabled", false).toBool());
    connect(mEnabled, &QAbstractButton::toggled, this,
            [this]() { mEnabledTouched = true; });
    layout->addWidget(mEnabled);

    auto *form = new QFormLayout;
    mPort = new QSpinBox(this);
    mPort->setRange(1024, 65535);
    mPort->setValue(mSettings->getValue("mcp_port", McpServer::kDefaultPort).toInt());
    form->addRow("Port", mPort);

    mToken = new QLineEdit(this);
    mToken->setReadOnly(true);
    auto *regenerate = new QPushButton("Regenerate", this);
    auto *tokenRow = new QHBoxLayout;
    tokenRow->addWidget(mToken, 1);
    tokenRow->addWidget(regenerate);
    form->addRow("Session token", tokenRow);

    mCommand = new QLineEdit(this);
    mCommand->setReadOnly(true);
    auto *copy = new QPushButton("Copy", this);
    auto *commandRow = new QHBoxLayout;
    commandRow->addWidget(mCommand, 1);
    commandRow->addWidget(copy);
    form->addRow("Connect", commandRow);

    mStatus = new QLabel(this);
    form->addRow("Status", mStatus);
    layout->addLayout(form);

    // ---- logging (ledger §361) -------------------------------------------
    // The error log is not a choice: it is always on, bounded, and holds only
    // failures. What IS a choice is the research record, so it is offered as
    // one — off, with the sentence that says what it would write.
    auto *logTitle = new QLabel("Logging", this);
    logTitle->setStyleSheet(StyleSheet::MutedInfoText());
    layout->addWidget(logTitle);

    auto makeToggle = [this](const QString &text) -> QAbstractButton * {
        if (ThemeManager::classicActive()) return new QCheckBox(text, this);
        auto *sw = new oclero::qlementine::Switch(this);
        sw->setText(text);
        return sw;
    };
    mLogSessions = makeToggle("Record MCP sessions for tool research");
    mLogSessions->setObjectName(QStringLiteral("mcpLogSessions"));
    mLogSessions->setChecked(McpLog::instance().sessionRecording());
    layout->addWidget(mLogSessions);

    mLogSource = makeToggle("…and include the script source");
    mLogSource->setObjectName(QStringLiteral("mcpLogSource"));
    mLogSource->setChecked(McpLog::instance().recordScriptSource());
    mLogSource->setEnabled(mLogSessions->isChecked());
    auto *sourceRow = new QHBoxLayout;
    sourceRow->addSpacing(24);
    sourceRow->addWidget(mLogSource, 1);
    layout->addLayout(sourceRow);
    connect(mLogSessions, &QAbstractButton::toggled, mLogSource, &QWidget::setEnabled);

    auto *privacy = new QLabel(McpLog::privacyNote(), this);
    privacy->setWordWrap(true);
    privacy->setStyleSheet(StyleSheet::MutedInfoText());
    layout->addWidget(privacy);

    // What to actually DO with the connect line (owner ask 2026-08-31).
    auto *howTo = new QLabel(
        "Run the line above once in a normal terminal (your shell, not inside a "
        "Claude chat) \xe2\x80\x94 it registers Jahshaka with Claude Code. Then start "
        "claude in any terminal, or resume an existing session, and ask it to build "
        "in the editor \xe2\x80\x94 it will use these tools automatically. The token "
        "changes each time Jahshaka starts, so re-copy the line after restarting "
        "the app.", this);
    howTo->setWordWrap(true);
    howTo->setStyleSheet(StyleSheet::MutedInfoText());
    layout->addWidget(howTo);
    layout->addStretch(1);

    // Dark-theme text/inputs (the tab sheet covers defaults; the spinbox needs
    // its dedicated dark style).
    StyleSheet::setStyle({ intro, mEnabled, mPort, mStatus, mLogSessions, mLogSource });

    reloadFromLiveState();

    connect(regenerate, &QPushButton::clicked, this, [this] {
        if (mServer) mServer->regenerateToken();
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(mCommand->text());
    });

    refresh();
}

void McpSettingsWidget::wireMcp(McpServer *server, MainWindow *mainWindow)
{
    mServer = server;
    mMainWindow = mainWindow;
    if (mServer) connect(mServer, &McpServer::stateChanged, this, &McpSettingsWidget::refresh);
    reloadFromLiveState();
    refresh();
}

// The dialog is built ONCE and shown many times, and OK saves EVERY page — so
// a control that still shows what it was built with writes that stale value
// back over whatever changed in between. Two things change behind this page's
// back: a script (app.mcpLogging) and the command line (--mcp-port).
void McpSettingsWidget::showEvent(QShowEvent *event)
{
    reloadFromLiveState();
    QWidget::showEvent(event);
}

void McpSettingsWidget::reloadFromLiveState()
{
    QSignalBlocker blockEnabled(mEnabled);
    QSignalBlocker blockSessions(mLogSessions);
    QSignalBlocker blockSource(mLogSource);
    // The RUNNING server is the truth when there is one: a session started
    // with --mcp-port has no mcp_enabled setting and the switch showed "off"
    // while the server was serving.
    mEnabled->setChecked(mServer ? mServer->isRunning()
                                 : mSettings->getValue("mcp_enabled", false).toBool());
    mEnabledTouched = false;
    mLogSessions->setChecked(McpLog::instance().sessionRecording());
    mLogSource->setChecked(McpLog::instance().recordScriptSource());
    mLogSource->setEnabled(mLogSessions->isChecked());
}

void McpSettingsWidget::refresh()
{
    if (!mServer) {
        mToken->setText(QString());
        mCommand->setText(QString());
        mStatus->setText("not available");
        return;
    }
    mToken->setText(mServer->token());
    mCommand->setText(mServer->connectCommand());
    mStatus->setText(mServer->isRunning()
                         ? QStringLiteral("running on http://127.0.0.1:%1/mcp").arg(mServer->port())
                         : QStringLiteral("stopped"));
}

void McpSettingsWidget::saveSettings()
{
    const bool enabled = mEnabled->isChecked();
    const quint16 port = quint16(mPort->value());
    // `mcp_enabled` is persisted ONLY when the user worked the switch here.
    // A session whose server came from --mcp-port would otherwise have its
    // command-line choice written into the preferences by any OK on any page,
    // and (before this) an untouched OK STOPPED that server outright.
    if (mEnabledTouched) mSettings->setValue("mcp_enabled", enabled);
    mSettings->setValue("mcp_port", int(port));
    // McpLog owns these two (it persists them itself and the scripting verb
    // writes the same pair) — the page only tells it what the user chose.
    McpLog::instance().setSessionRecording(mLogSessions->isChecked());
    McpLog::instance().setRecordScriptSource(mLogSource->isChecked());

    if (!mServer) return;
    if (enabled && (!mServer->isRunning() || mServer->port() != port)) {
        // Restart on the (possibly new) port; the console dock gets the
        // fresh connect line via MainWindow.
        QString error;
        if (mMainWindow) mMainWindow->startMcpServer(port, &error);
        else mServer->start(port, &error);
        if (!error.isEmpty()) mStatus->setText(error);
    } else if (!enabled && mServer->isRunning() && mEnabledTouched) {
        mServer->stop();
    }
}
