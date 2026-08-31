/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CLAUDECHATHOST_H
#define CLAUDECHATHOST_H

// ClaudeChatHost — hosts Claude Code itself as a subprocess
// (CLAUDE_EDITOR_SPEC phase 2): `claude -p --output-format stream-json
// --input-format stream-json ...` with cwd = the open project folder and the
// ClaudeLaunchConfig lockdown argv. One long-lived process serves the whole
// chat; user turns are stream-json lines on its stdin. The session resumes
// per project (--resume with the persisted id); clearSession() forgets it.
//
// Stop: a control_request/interrupt line on stdin (the CLI's own interrupt
// protocol); if the turn hasn't ended shortly after, the process is killed —
// the NEXT send restarts it with --resume, so nothing is lost but the
// interrupted turn.

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

#include "claudestreamparser.h"

class ClaudeChatHost : public QObject
{
    Q_OBJECT
public:
    explicit ClaudeChatHost(QObject *parent = nullptr);
    ~ClaudeChatHost() override;

    /// Project + live MCP wiring. Writes the .claude config (skills + MCP
    /// file). Call again whenever the project or MCP state changes — a
    /// running process is stopped when the project folder changes.
    bool configure(const QString &projectFolder, bool mcpEnabled,
                   quint16 mcpPort, const QString &mcpToken,
                   QString *errorOut = nullptr);

    QString projectFolder() const { return mProjectFolder; }
    bool mcpEnabled() const { return mMcpEnabled; }
    bool isProcessRunning() const;
    bool isBusy() const { return mBusy; }
    /// Non-empty once a session exists (persisted per project).
    QString sessionId() const { return mSessionId; }

    ClaudeStreamParser *parser() { return &mParser; }

    /// Sends one user turn (starts/restarts the subprocess as needed).
    void sendMessage(const QString &text);
    /// Interrupts the current turn (control_request, then kill fallback).
    void stopTurn();
    /// Ends the process and FORGETS the project's session (Clear button).
    void clearSession();
    /// Ends the process, keeping the session for --resume next time.
    void shutdown();

signals:
    void busyChanged(bool busy);
    /// The subprocess failed to start / died unexpectedly. `detail` includes
    /// captured stderr — this is where "not logged in" surfaces.
    void processFailed(const QString &detail);
    /// The stop fallback killed the process (turn aborted).
    void turnAborted();

private:
    void startProcess();
    void writeUserMessage(const QString &text);
    void setBusy(bool busy);
    void handleFinished(int exitCode, QProcess::ExitStatus status);

    QString mProjectFolder;
    bool mMcpEnabled = false;
    quint16 mMcpPort = 0;
    QString mMcpToken;

    QProcess *mProcess = nullptr;
    ClaudeStreamParser mParser;
    QString mSessionId;
    QString mPendingMessage;   // queued until the process is up
    QByteArray mStderr;
    QTimer mKillTimer;
    bool mBusy = false;
    bool mStopping = false;    // deliberate shutdown — don't report failure
    int mRequestCounter = 0;
};

#endif // CLAUDECHATHOST_H
