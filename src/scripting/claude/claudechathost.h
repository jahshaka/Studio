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
//
// Stale resume (CLAUDE_EDITOR_SPEC D3): `--resume <unknown-id>` makes the CLI
// exit 1 with "No conversation found with session ID: …". The id used to
// survive that, so every later send rebuilt the same doomed argv and the chat
// was bricked until the user found the Clear button — which any ~/.claude
// prune, machine move or copied project folder could trigger. handleFinished
// now recognises that failure, DROPS the id, and restarts once with the user's
// message re-queued.
//
// Orphan hardening (CLAUDE_EDITOR_SPEC D2). A NORMAL quit already kills the
// child in the right place — shutdown() runs from MainWindow::shutdown-
// BackgroundWork, step 2 `BackgroundWork` of src/shell/shutdownorder.h
// (mainwindow.cpp, before modules, engine and database), bounded 1.5 s + 1 s.
// What was not covered is a CRASHING app: the child then only notices stdin
// EOF, which was measured at ~4 s while idle and is unmeasured mid-turn, and
// the zombie-process incident class is on the record
// (tests/importasync/CMakeLists.txt). Two belts:
//   * Linux: PR_SET_PDEATHSIG/SIGKILL through QProcess::setChildProcessModifier
//     — the kernel reaps the child the instant this process dies, however it
//     dies. `__linux__` only: there is no macOS or Windows equivalent.
//   * Everywhere: the child's pid is recorded in <project>/.claude/
//     jahshaka-chat.pid, and the NEXT configure() for that project kills a
//     recorded pid that is still alive AND still carries our launch signature
//     in its command line (pids are recycled — a number alone is never enough).

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

    /// The model dock sessions run on. Defaults to
    /// ClaudeLaunchConfig::defaultModel(); an empty string means "inherit the
    /// user's own default" (the pre-2026-09-04 behaviour). This is the seam a
    /// chat-header picker drives — the launch argv reads it on every start.
    void setModel(const QString &model);
    QString model() const { return mModel; }

    QString projectFolder() const { return mProjectFolder; }
    bool mcpEnabled() const { return mMcpEnabled; }
    bool isProcessRunning() const;
    bool isBusy() const { return mBusy; }
    /// Non-empty once a session exists (persisted per project).
    QString sessionId() const { return mSessionId; }

    ClaudeStreamParser *parser() { return &mParser; }

    /// The pid of the live child, or 0 (test/diagnostic seam).
    qint64 childProcessId() const;

    /// Kills a child recorded for this project that outlived the app that
    /// spawned it (D2, above). Returns the pid it reaped, or 0. Safe to call
    /// when there is nothing to reap; NEVER kills a pid whose command line
    /// does not carry ClaudeLaunchConfig::launchSignature().
    static qint64 reapStaleChild(const QString &projectFolder);

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
    /// The persisted session could not be resumed; it has been dropped and a
    /// fresh conversation started (D3). The window shows this as an info row.
    void sessionResumeFailed();
    /// The configured project folder changed while a conversation was live:
    /// that session is gone and the next turn starts a new one in the new
    /// project's folder (D1).
    void projectChanged(const QString &projectFolder);

private:
    void startProcess();
    void writeUserMessage(const QString &text);
    void setBusy(bool busy);
    void handleFinished(int exitCode, QProcess::ExitStatus status);
    bool isStaleResumeFailure(const QString &stderrText) const;
    void clearPidRecord();

    QString mProjectFolder;
    bool mMcpEnabled = false;
    quint16 mMcpPort = 0;
    QString mMcpToken;

    QProcess *mProcess = nullptr;
    ClaudeStreamParser mParser;
    QString mSessionId;
    QString mModel;
    QString mPendingMessage;   // queued until the process is up
    QString mLastUserMessage;  // re-queued across ONE stale-resume restart
    QByteArray mStderr;
    QTimer mKillTimer;
    bool mBusy = false;
    bool mStopping = false;    // deliberate shutdown — don't report failure
    bool mResumeRecovered = false;  // D3: one recovery attempt per session id
    int mRequestCounter = 0;
    QString mPidProject;       // the project the live pid file belongs to
};

#endif // CLAUDECHATHOST_H
