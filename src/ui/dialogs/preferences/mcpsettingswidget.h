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

#include <QWidget>

class QCheckBox;
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

private slots:
    void refresh();

private:
    SettingsManager *mSettings;
    McpServer *mServer = nullptr;
    MainWindow *mMainWindow = nullptr;

    QCheckBox *mEnabled;
    QSpinBox *mPort;
    QLineEdit *mToken;
    QLineEdit *mCommand;
    QLabel *mStatus;
};

#endif // MCPSETTINGSWIDGET_H
