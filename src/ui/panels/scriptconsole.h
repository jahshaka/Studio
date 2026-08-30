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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void runInput();
    void chooseAndRunFile();

private:
    void appendLine(const QString &text, const QString &color = QString());
    void historyStep(int direction);

    ScriptEngine *mEngine;
    QPlainTextEdit *mLog;
    QPlainTextEdit *mInput;
    QStringList mHistory;
    int mHistoryPos = 0;        // == mHistory.size() when editing a fresh line
    QString mPendingInput;      // the fresh line stashed while browsing history
};

#endif // SCRIPTCONSOLE_H
