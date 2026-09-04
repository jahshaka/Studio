/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "claudechathost.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "claudecliprobe.h"
#include "claudelaunchconfig.h"

ClaudeChatHost::ClaudeChatHost(QObject *parent)
    : QObject(parent), mModel(ClaudeLaunchConfig::defaultModel())
{
    mKillTimer.setSingleShot(true);
    mKillTimer.setInterval(3000);
    connect(&mKillTimer, &QTimer::timeout, this, [this]() {
        if (mProcess && mProcess->state() != QProcess::NotRunning) {
            mStopping = true;
            mProcess->kill();
            emit turnAborted();
        }
    });

    connect(&mParser, &ClaudeStreamParser::sessionStarted, this,
            [this](const QString &sessionId, const QStringList &, const QStringList &, bool) {
                mSessionId = sessionId;
                // A session that started is a session that can be resumed: the
                // D3 recovery budget resets with it.
                mResumeRecovered = false;
                if (!mProjectFolder.isEmpty())
                    ClaudeLaunchConfig::writeSessionId(mProjectFolder, sessionId);
            });
    connect(&mParser, &ClaudeStreamParser::turnCompleted, this,
            [this](bool, const QString &, const QString &sessionId, double) {
                if (!sessionId.isEmpty()) mSessionId = sessionId;
                mKillTimer.stop();
                setBusy(false);
            });
}

ClaudeChatHost::~ClaudeChatHost()
{
    shutdown();
}

void ClaudeChatHost::setModel(const QString &model)
{
    if (mModel == model) return;
    mModel = model;
    // The argv is built at start; a live process keeps the model it launched
    // with until the next (re)start, exactly like the MCP wiring.
}

bool ClaudeChatHost::configure(const QString &projectFolder, bool mcpEnabled,
                               quint16 mcpPort, const QString &mcpToken, QString *errorOut)
{
    if (projectFolder != mProjectFolder) {
        // D1: the caller now reaches here on project open/close too, so this is
        // where a live conversation is told its world changed.
        const bool hadConversation = !mProjectFolder.isEmpty()
                                     && (isProcessRunning() || !mSessionId.isEmpty());
        shutdown();
        mProjectFolder = projectFolder;
        mLastUserMessage.clear();
        mResumeRecovered = false;
        mSessionId = projectFolder.isEmpty()
                         ? QString()
                         : ClaudeLaunchConfig::readSessionId(projectFolder);
        if (hadConversation) emit projectChanged(projectFolder);
    } else if (mcpEnabled != mMcpEnabled || mcpPort != mMcpPort || mcpToken != mMcpToken) {
        // MCP wiring changed mid-chat: the running process keeps its old
        // connection; the next (re)start picks up the new file.
        shutdown();
    }
    mMcpEnabled = mcpEnabled && mcpPort > 0;
    mMcpPort = mcpPort;
    mMcpToken = mcpToken;

    if (mProjectFolder.isEmpty()) return true; // nothing to write yet
    return ClaudeLaunchConfig::writeProjectConfig(
        mProjectFolder, mMcpEnabled ? mMcpPort : 0, mMcpToken, errorOut);
}

bool ClaudeChatHost::isProcessRunning() const
{
    return mProcess && mProcess->state() != QProcess::NotRunning;
}

void ClaudeChatHost::sendMessage(const QString &text)
{
    if (mBusy || text.trimmed().isEmpty() || mProjectFolder.isEmpty()) return;
    setBusy(true);
    if (!isProcessRunning()) {
        mPendingMessage = text;
        startProcess();
    } else {
        writeUserMessage(text);
    }
}

void ClaudeChatHost::startProcess()
{
    if (mProcess) {
        mProcess->deleteLater();
        mProcess = nullptr;
    }
    mParser.reset();
    mStderr.clear();
    mStopping = false;

    mProcess = new QProcess(this);
    mProcess->setWorkingDirectory(mProjectFolder);
    mProcess->setProgram(ClaudeCliProbe::program());
    mProcess->setArguments(
        ClaudeLaunchConfig::arguments(mProjectFolder, mMcpEnabled, mSessionId, mModel));

    connect(mProcess, &QProcess::readyReadStandardOutput, this,
            [this]() { mParser.feed(mProcess->readAllStandardOutput()); });
    connect(mProcess, &QProcess::readyReadStandardError, this, [this]() {
        mStderr.append(mProcess->readAllStandardError());
        if (mStderr.size() > 16384) mStderr = mStderr.right(16384);
    });
    connect(mProcess, &QProcess::started, this, [this]() {
        if (!mPendingMessage.isEmpty()) {
            writeUserMessage(mPendingMessage);
            mPendingMessage.clear();
        }
    });
    connect(mProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ClaudeChatHost::handleFinished);
    connect(mProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            mKillTimer.stop();
            setBusy(false);
            mPendingMessage.clear();
            emit processFailed(QStringLiteral("could not start %1: %2")
                                   .arg(mProcess->program(), mProcess->errorString()));
        }
    });
    mProcess->start();
}

void ClaudeChatHost::writeUserMessage(const QString &text)
{
    mLastUserMessage = text;   // D3: re-queued if a stale --resume kills the process
    const QJsonObject message{
        {"type", "user"},
        {"message",
         QJsonObject{{"role", "user"},
                     {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}}}}}};
    mProcess->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + "\n");
}

void ClaudeChatHost::stopTurn()
{
    if (!mBusy || !isProcessRunning()) return;
    const QJsonObject interrupt{
        {"type", "control_request"},
        {"request_id", QStringLiteral("stop_%1").arg(++mRequestCounter)},
        {"request", QJsonObject{{"subtype", "interrupt"}}}};
    mProcess->write(QJsonDocument(interrupt).toJson(QJsonDocument::Compact) + "\n");
    mKillTimer.start(); // fallback: kill if the interrupt goes unanswered
}

void ClaudeChatHost::clearSession()
{
    shutdown();
    mSessionId.clear();
    mLastUserMessage.clear();
    mResumeRecovered = false;
    if (!mProjectFolder.isEmpty()) ClaudeLaunchConfig::clearSessionId(mProjectFolder);
}

void ClaudeChatHost::shutdown()
{
    mKillTimer.stop();
    mPendingMessage.clear();
    if (mProcess) {
        mStopping = true;
        disconnect(mProcess, nullptr, this, nullptr);
        if (mProcess->state() != QProcess::NotRunning) {
            mProcess->closeWriteChannel(); // stream-json EOF = clean exit
            if (!mProcess->waitForFinished(1500)) {
                mProcess->kill();
                mProcess->waitForFinished(1000);
            }
        }
        mProcess->deleteLater();
        mProcess = nullptr;
    }
    setBusy(false);
}

// D3: `--resume <id>` against an id the CLI no longer knows is a hard exit 1.
// Matched on the CLI's own sentence, with a looser fallback so a reworded
// message still recovers rather than bricking the chat.
bool ClaudeChatHost::isStaleResumeFailure(const QString &stderrText) const
{
    if (mSessionId.isEmpty()) return false;
    const QString text = stderrText.toLower();
    return text.contains(QLatin1String("no conversation found with session id"))
           || (text.contains(QLatin1String("session id")) && text.contains(QLatin1String("not found")))
           || text.contains(QLatin1String("resume failed"));
}

void ClaudeChatHost::handleFinished(int exitCode, QProcess::ExitStatus status)
{
    mKillTimer.stop();
    const bool failed = !mStopping && (status != QProcess::NormalExit || exitCode != 0);
    const QString detail = QString::fromUtf8(mStderr).trimmed();

    if (failed && !mResumeRecovered && isStaleResumeFailure(detail)) {
        // Drop the id (from memory AND from disk — otherwise the next app run
        // reads it straight back) and restart once, carrying the user's turn.
        mResumeRecovered = true;
        mSessionId.clear();
        if (!mProjectFolder.isEmpty()) ClaudeLaunchConfig::clearSessionId(mProjectFolder);
        emit sessionResumeFailed();
        mStopping = false;
        if (!mLastUserMessage.isEmpty()) {
            mPendingMessage = mLastUserMessage;
            startProcess();          // stays busy: the turn is being retried
            return;
        }
        if (mBusy) setBusy(false);
        return;
    }

    if (failed) {
        emit processFailed(detail.isEmpty()
                               ? QStringLiteral("claude exited with code %1").arg(exitCode)
                               : detail);
    }
    if (mBusy) setBusy(false);
    mStopping = false;
}

void ClaudeChatHost::setBusy(bool busy)
{
    if (mBusy == busy) return;
    mBusy = busy;
    emit busyChanged(busy);
}
