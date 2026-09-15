/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MCPSETTINGSWIDGET_H
#define MCPSETTINGSWIDGET_H

// The MCP server page of the Preferences dialog (CLAUDE_EDITOR_SPEC.md
// phase 1): enable toggle + port + the per-session token with its copyable
// `claude mcp add ...` connect line. The token is never persisted — only
// mcp_enabled and mcp_port are settings.
//
// Plus the LOGGING switches (owner, 2026-09-15): the error log is always on
// and is not offered as a choice; the session recording (mcp_log_sessions) and
// its script-source sub-option (mcp_log_script_source) are opt-in, default
// OFF, and the page says in plain words what each one writes down and that
// nothing leaves the machine (McpLog::privacyNote).

#include <QWidget>

class QAbstractButton;
class QShowEvent;
class QLabel;
class QLineEdit;
class QSpinBox;
class SettingsManager;
class McpServer;
class MainWindow;

class McpSettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit McpSettingsWidget(SettingsManager *settings, QWidget *parent = nullptr);

    /// Late wiring (the dialog exists before the scripting stack does).
    void wireMcp(McpServer *server, MainWindow *mainWindow);

    /// Persists mcp_enabled/mcp_port and starts/stops/restarts the server to
    /// match. Called from the dialog's Apply.
    void saveSettings();

protected:
    /// Every page's saveSettings() runs on ANY OK, so every page's widgets
    /// must show the LIVE state when the dialog opens — a value that changed
    /// since this widget was built (a scripted app.mcpLogging, a server
    /// started from --mcp-port) would otherwise be written back stale.
    void showEvent(QShowEvent *event) override;

private slots:
    void refresh();
    /// Re-reads every control from the live state (McpLog, the server).
    void reloadFromLiveState();

private:
    SettingsManager *mSettings;
    McpServer *mServer = nullptr;
    MainWindow *mMainWindow = nullptr;

    QAbstractButton *mEnabled;   // QCheckBox (Classic) or qlementine Switch (Qlementine)
    QAbstractButton *mLogSessions;   // the opt-in tool-research record (default OFF)
    QAbstractButton *mLogSource;     // ...and whether it carries the script source
    /// Did the USER touch the enable switch in this dialog session? An OK on a
    /// session whose server came from --mcp-port must neither stop it nor
    /// persist mcp_enabled: the command line is not a preference.
    bool mEnabledTouched = false;
    QSpinBox *mPort;
    QLineEdit *mToken;
    QLineEdit *mCommand;
    QLabel *mStatus;
};

#endif // MCPSETTINGSWIDGET_H
