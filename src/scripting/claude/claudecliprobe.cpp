/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "claudecliprobe.h"

#include <QProcess>
#include <QRegularExpression>

QString ClaudeCliProbe::program()
{
    const QString override = qEnvironmentVariable("JAHSHAKA_CLAUDE_CLI");
    return override.isEmpty() ? QStringLiteral("claude") : override;
}

ClaudeCliProbe::Result ClaudeCliProbe::probe(int timeoutMs)
{
    Result result;
    result.program = program();

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(result.program, {QStringLiteral("--version")});
    if (!process.waitForStarted(timeoutMs)) {
        result.status = Status::NotFound;
        result.detail = process.errorString();
        return result;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        result.status = Status::Error;
        result.detail = QStringLiteral("timed out");
        return result;
    }

    const QString output = QString::fromUtf8(process.readAll()).trimmed();
    result.detail = output;
    // "2.1.251 (Claude Code)" — accept any leading semver-ish token.
    const QRegularExpression rx(QStringLiteral("(\\d+\\.\\d+[\\.\\d]*)"));
    const auto match = rx.match(output);
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0
        && match.hasMatch()) {
        result.status = Status::Found;
        result.version = match.captured(1);
    } else {
        result.status = Status::Error;
    }
    return result;
}
