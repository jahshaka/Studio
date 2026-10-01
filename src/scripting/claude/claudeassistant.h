/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CLAUDEASSISTANT_H
#define CLAUDEASSISTANT_H

// ClaudeAssistant — THE MCP ENDPOINT AND THE CLAUDE CHAT, ONE OWNER
// (D10-SHELL-MODULES; CLAUDE_EDITOR_SPEC phases 1-2).
//
// The shell used to carry both: the MCP server's creation and start (the
// Preferences toggle, --mcp-port=N, the chat's "enable MCP" button), the
// floating chat popup and its rebind on every project change. They are one
// domain — the chat talks to the editor THROUGH the MCP server — and nothing
// in it is the window's business beyond where the chat's button and chord go,
// which it contributes (Contributions: the Claude toolbar action and the
// claude.toggle row).

#include <QObject>
#include <QPointer>

class ClaudeChatHost;
class ClaudeChatWindow;
class Contributions;
class McpServer;
class Project;
class ProjectService;
class QWidget;
class ScriptConsole;
class ScriptEngine;
class SettingsManager;
class QtAwesome;

class ClaudeAssistant : public QObject
{
    Q_OBJECT
public:
    struct Deps {
        ScriptEngine *engine = nullptr;
        SettingsManager *settings = nullptr;
        ProjectService *projectService = nullptr;
        Project *project = nullptr;
        /// Where the connect line is announced (the script console), or null.
        ScriptConsole *console = nullptr;
        /// The chat popup's parent.
        QWidget *window = nullptr;
    };

    /// Creates the MCP endpoint — OFF: total lockdown, the scripting engine is
    /// the only capability surface.
    ClaudeAssistant(const Deps &deps, QObject *parent = nullptr);
    /// Starts the endpoint only when the Preferences toggle was saved on
    /// (--mcp-port=N starts it from the CLI path instead).
    void startFromSettings();

    /// The chat's toolbar action and its chord.
    void contribute(Contributions &c, QtAwesome *icons);

    McpServer *mcp() const { return mServer; }
    /// Starts the MCP server on 127.0.0.1:port and announces the connect line
    /// in the script console. False (with errorOut) when the bind fails.
    bool startMcpServer(quint16 port, QString *errorOut = nullptr);

    /// The floating chat popup — created lazily; toggled by the toolbar
    /// button and the claude.toggle chord.
    void toggleChat();
    /// The chat popup, or null before it was first opened (app.dialog's
    /// "claudeChat" entry).
    QWidget *chatWindow() const;
    /// Pushes the current project / MCP state into the chat window + host.
    /// Called on every project open, create and close (CLAUDE_EDITOR_SPEC D1).
    void refreshChatContext();

    /// Step 2 of the shutdown order: the endpoint stops accepting requests and
    /// the chat subprocess closes stdin, waits briefly, and is killed.
    void shutdown();

private:
    Deps mDeps;
    McpServer *mServer = nullptr;
    ClaudeChatHost *mChatHost = nullptr;
    QPointer<ClaudeChatWindow> mChatWindow;
};

#endif // CLAUDEASSISTANT_H
