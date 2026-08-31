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

ClaudeChatHost::ClaudeChatHost(QObject *parent) : QObject(parent)
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

bool ClaudeChatHost::configure(const QString &projectFolder, bool mcpEnabled,
                               quint16 mcpPort, const QString &mcpToken, QString *errorOut)
{
    if (projectFolder != mProjectFolder) {
        shutdown();
        mProjectFolder = projectFolder;
        mSessionId = projectFolder.isEmpty()
                         ? QString()
                         : ClaudeLaunchConfig::readSessionId(projectFolder);
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
        ClaudeLaunchConfig::arguments(mProjectFolder, mMcpEnabled, mSessionId));

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

void ClaudeChatHost::handleFinished(int exitCode, QProcess::ExitStatus status)
{
    mKillTimer.stop();
    const bool failed = !mStopping && (status != QProcess::NormalExit || exitCode != 0);
    if (failed) {
        const QString detail = QString::fromUtf8(mStderr).trimmed();
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
