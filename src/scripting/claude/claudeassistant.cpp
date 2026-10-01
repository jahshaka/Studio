/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/claude/claudeassistant.h"

#include <QAction>

#include "data/project.h"
#include "data/settingsmanager.h"
#include "modules/studiomodule.h"
#include "scripting/claude/claudechathost.h"
#include "scripting/claude/claudecliprobe.h"
#include "scripting/mcp/mcpserver.h"
#include "services/projectservice.h"
#include "thirdparty/qtawesome/QtAwesome.h"
#include "ui/panels/scriptconsole.h"
#include "ui/windows/claudechatwindow.h"

ClaudeAssistant::ClaudeAssistant(const Deps &deps, QObject *parent)
    : QObject(parent), mDeps(deps)
{
	// MCP endpoint (CLAUDE_EDITOR_SPEC.md phase 1): OFF by default — total
	// lockdown, the scripting engine is the only capability surface.
	mServer = new McpServer(mDeps.engine, this);
}

void ClaudeAssistant::startFromSettings()
{
	// Started here only when the Preferences toggle was saved on; --mcp-port=N
	// starts it from the CLI path instead.
	if (mDeps.settings && mDeps.settings->get(settingkeys::mcpEnabled)) {
		QString mcpError;
		if (!startMcpServer(quint16(mDeps.settings->get(settingkeys::mcpPort)), &mcpError))
			qWarning("MCP: %s", qPrintable(mcpError));
	}
}

void ClaudeAssistant::contribute(Contributions &c, QtAwesome *icons)
{
	QVariantMap options;
	options.insert("color", QColor(255, 255, 255));
	options.insert("color-active", QColor(255, 255, 255));
	auto *actionClaude = new QAction(this);
	actionClaude->setObjectName(QStringLiteral("actionClaudeChat"));
	actionClaude->setCheckable(false);
	actionClaude->setToolTip("Claude | Chat with Claude inside the editor (Ctrl+Shift+C)");
	if (icons) actionClaude->setIcon(icons->icon(fa::magic, options));
	connect(actionClaude, &QAction::triggered, this, &ClaudeAssistant::toggleChat);
	c.addToolbarAction(QStringLiteral("editor.end"), actionClaude);

	Contributions::Shortcut row;
	row.id = QStringLiteral("claude.toggle");
	row.label = QStringLiteral("Claude Assistant");
	row.category = QStringLiteral("Windows");
	row.keys = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C);
	row.after = QStringLiteral("console.toggle");
	row.run = [this]() { toggleChat(); };
	c.addShortcut(row);
}

bool ClaudeAssistant::startMcpServer(quint16 port, QString *errorOut)
{
    if (!mServer) {
        if (errorOut) *errorOut = QStringLiteral("the MCP server was not created");
        return false;
    }
    QString error;
    if (!mServer->start(port, &error)) {
        if (errorOut) *errorOut = error;
        return false;
    }
    // The console dock shows the copyable connect line (the token lives only
    // in this session — it is never persisted).
    if (mDeps.console) {
        mDeps.console->announce(QStringLiteral("MCP server listening on http://127.0.0.1:%1/mcp")
                                    .arg(mServer->port()));
        mDeps.console->announce(mServer->connectCommand());
    }
    return true;
}

QWidget *ClaudeAssistant::chatWindow() const
{
    return mChatWindow.data();
}

void ClaudeAssistant::toggleChat()
{
    if (mChatWindow && mChatWindow->isVisible()) {
        mChatWindow->close();
        return;
    }
    if (!mChatHost) mChatHost = new ClaudeChatHost(this);
    // The model seam (AI_SURFACE_PROGRAM_SPEC owner decision): the dock pins a
    // model instead of silently inheriting the user's terminal default. The
    // setting is what a header picker will write; absent, the shipped default
    // applies, and an explicit empty string restores "inherit".
    mChatHost->setModel(mDeps.settings->get(settingkeys::claudeModel));
    if (!mChatWindow) {
        mChatWindow = new ClaudeChatWindow(mDeps.settings->settings, mChatHost, mDeps.window);
        connect(mChatWindow, &ClaudeChatWindow::enableMcpRequested, this, [this]() {
            const quint16 port = quint16(mDeps.settings->get(settingkeys::mcpPort));
            QString error;
            if (startMcpServer(port, &error)) {
                mDeps.settings->set(settingkeys::mcpEnabled, true);
            } else if (mDeps.console) {
                mDeps.console->announce(QStringLiteral("MCP enable failed: %1").arg(error));
            }
            refreshChatContext();
        });
        // The one-time CLI probe (~ms when installed; renders the friendly
        // install state when not).
        mChatWindow->setCliState(ClaudeCliProbe::probe());
    }
    refreshChatContext();
    mChatWindow->show();
    mChatWindow->raise();
    mChatWindow->activateWindow();
}

// Called on every project OPEN and CLOSE as well as on toggle/enable-MCP
// (CLAUDE_EDITOR_SPEC D1): ClaudeChatHost::configure is written to rebind on a
// folder change, but nothing used to call it when the project changed, so a
// chat left open across a switch kept the previous project's cwd, MCP config
// file and session. Cheap when the chat was never opened — it returns at the
// first line.
void ClaudeAssistant::refreshChatContext()
{
    if (!mChatWindow || !mChatHost) return;
    const bool sceneOpen = mDeps.projectService && mDeps.projectService->isSceneOpen();
    const bool mcpRunning = mServer && mServer->isRunning();
    mChatWindow->setProjectOpen(sceneOpen);
    mChatWindow->setMcpRunning(mcpRunning);
    const QString folder = (sceneOpen && mDeps.project) ? mDeps.project->getProjectFolder() : QString();
    QString error;
    if (!mChatHost->configure(folder, mcpRunning,
                              mcpRunning ? mServer->port() : 0,
                              mcpRunning ? mServer->token() : QString(), &error)
        && mDeps.console && !error.isEmpty()) {
        mDeps.console->announce(QStringLiteral("Claude chat config: %1").arg(error));
    }
}

void ClaudeAssistant::shutdown()
{
    // The MCP endpoint must not accept requests into a half-torn-down app.
    if (mServer) mServer->stop();
    // The Claude chat subprocess: closes stdin, waits briefly, kills.
    if (mChatHost) mChatHost->shutdown();
}
