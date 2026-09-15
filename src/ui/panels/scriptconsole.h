/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTCONSOLE_H
#define SCRIPTCONSOLE_H

// The script console dock (SCRIPTING_SPEC §3.1): REPL + log, history,
// Ctrl+Enter / Enter to run, "Run File…", help() from the registry.
// Styled like the other dark docks.

#include <QWidget>

class QPlainTextEdit;
class QPushButton;
class ScriptEngine;

class ScriptConsole : public QWidget
{
    Q_OBJECT
public:
    explicit ScriptConsole(ScriptEngine *engine, QWidget *parent = nullptr);

    /// Runs a script file through the engine, echoing into the log.
    void runFile(const QString &path);

    /// Appends an informational host line to the log (e.g. the MCP server's
    /// copyable connect command).
    void announce(const QString &text);

    /// Puts the keyboard into the INPUT line. The Ctrl+` shortcut calls this
    /// when it shows the dock: a console opened from the keyboard that then
    /// needs a mouse click before it will take a character is a console the
    /// shortcut did not really open (found on the rig, 2026-09-09).
    void focusInput();

    /// Whether the INPUT line has the keyboard right now. The tray verb reports
    /// it (editor.trayState().consoleFocused) so a suite can assert the second
    /// half of what Ctrl+` promises: not just a visible console, a typable one.
    bool inputHasFocus() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void runInput();
    void chooseAndRunFile();
    /// Run while idle, Stop while a script is running — ONE button, because
    /// "the thing that starts it is the thing that stops it" is the only
    /// arrangement that cannot be clicked wrong (and the 2x2 block has no room
    /// for a fifth).
    void runOrStop();

private:
    void appendLine(const QString &text, const QString &color = QString());
    void historyStep(int direction);

    /// Reflects the engine's running state: Run/Stop text, and the file and
    /// input widgets disabled while a run owns the engine (a second run is
    /// refused, so offering it would be a lie).
    void setRunningUi(bool running);

    ScriptEngine *mEngine;
    QPushButton *mRunBtn = nullptr;
    QPushButton *mFileBtn = nullptr;
    QPlainTextEdit *mLog;
    QPlainTextEdit *mInput;
    QStringList mHistory;
    int mHistoryPos = 0;        // == mHistory.size() when editing a fresh line
    QString mPendingInput;      // the fresh line stashed while browsing history
};

#endif // SCRIPTCONSOLE_H
