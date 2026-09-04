/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "claudelaunchconfig.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

const char *kMcpFile = "jahshaka-mcp.json";
const char *kChatFile = "jahshaka-chat.json";

// Everything Claude Code could otherwise do — denied at launch, never asked
// about (owner decision 2). --tools "Skill" already removes these outright;
// the deny list guards against a future CLI growing new defaults.
const char *kDeniedBuiltins =
    "Bash,Edit,Write,MultiEdit,NotebookEdit,Read,Glob,Grep,WebFetch,"
    "WebSearch,Task,Agent,TodoWrite,KillShell,BashOutput,SlashCommand,"
    "ExitPlanMode";

const char *kSystemPrompt =
    "You are the assistant inside the Jahshaka 3D editor. You are connected "
    "to the LIVE editor through the jahshaka MCP tools; run_script executes "
    "JavaScript against the editor's scripting verbs (api_docs lists them "
    "all) and each run is one undo step. You have no filesystem, shell or "
    "network access — the scripting engine is your entire capability "
    "surface. Prefer one script per user request, verify visually with the "
    "screenshot tool, and keep replies short; this chat renders in a small "
    "popup window.";

QString skillResource(const QString &name)
{
    return QStringLiteral(":/claude/skills/%1.md").arg(name);
}

bool writeFileAtomic(const QString &path, const QByteArray &content, QString *errorOut)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) *errorOut = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.write(content);
    file.close();
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

} // namespace

QString ClaudeLaunchConfig::jahshakaMcpToolPattern()
{
    // Server-anchored on purpose: `mcp__*` alone is ignored with a startup
    // warning (Claude Code permission docs, re-verified at CLI 2.1.258).
    return QStringLiteral("mcp__jahshaka__*");
}

QString ClaudeLaunchConfig::defaultModel()
{
    return QStringLiteral("fable");
}

QStringList ClaudeLaunchConfig::skillNames()
{
    return {QStringLiteral("jahshaka-scene-building"),
            QStringLiteral("jahshaka-materials"),
            QStringLiteral("jahshaka-assets")};
}

QString ClaudeLaunchConfig::mcpConfigPath(const QString &projectFolder)
{
    return QDir(projectFolder).filePath(QStringLiteral(".claude/") + kMcpFile);
}

int ClaudeLaunchConfig::skillVersion(const QString &markdown)
{
    // Frontmatter "version: N" (first 40 lines are plenty).
    static const QRegularExpression rx(QStringLiteral("(?m)^version:\\s*(\\d+)\\s*$"));
    const auto match = rx.match(markdown.left(2000));
    return match.hasMatch() ? match.captured(1).toInt() : -1;
}

bool ClaudeLaunchConfig::writeProjectConfig(const QString &projectFolder, quint16 mcpPort,
                                            const QString &token, QString *errorOut)
{
    if (projectFolder.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("no project folder");
        return false;
    }
    QDir project(projectFolder);
    if (!project.mkpath(QStringLiteral(".claude/skills"))) {
        if (errorOut) *errorOut = QStringLiteral("cannot create .claude in %1").arg(projectFolder);
        return false;
    }
    const QString claudeDir = project.filePath(QStringLiteral(".claude"));

    if (mcpPort > 0) {
        QJsonObject server{
            {"type", "http"},
            {"url", QStringLiteral("http://127.0.0.1:%1/mcp").arg(mcpPort)},
            {"headers", QJsonObject{{"Authorization", QStringLiteral("Bearer %1").arg(token)}}}};
        QJsonObject root{{"mcpServers", QJsonObject{{"jahshaka", server}}}};
        if (!writeFileAtomic(mcpConfigPath(projectFolder),
                             QJsonDocument(root).toJson(QJsonDocument::Indented), errorOut))
            return false;
    } else {
        // MCP off: never leave a stale port/token file behind.
        QFile::remove(mcpConfigPath(projectFolder));
    }

    return installSkills(claudeDir, errorOut);
}

bool ClaudeLaunchConfig::installSkills(const QString &claudeDir, QString *errorOut)
{
    for (const QString &name : skillNames()) {
        QFile resource(skillResource(name));
        if (!resource.open(QIODevice::ReadOnly)) {
            if (errorOut) *errorOut = QStringLiteral("missing embedded skill %1").arg(name);
            return false;
        }
        const QByteArray shipped = resource.readAll();
        const int shippedVersion = skillVersion(QString::fromUtf8(shipped));

        QDir dir(claudeDir);
        dir.mkpath(QStringLiteral("skills/%1").arg(name));
        const QString target =
            dir.filePath(QStringLiteral("skills/%1/SKILL.md").arg(name));

        QFile existing(target);
        if (existing.open(QIODevice::ReadOnly)) {
            const int installedVersion =
                skillVersion(QString::fromUtf8(existing.readAll()));
            existing.close();
            if (installedVersion >= shippedVersion) continue; // user's or current
        }
        if (!writeFileAtomic(target, shipped, errorOut)) return false;
    }
    return true;
}

QStringList ClaudeLaunchConfig::arguments(const QString &projectFolder, bool mcpEnabled,
                                          const QString &resumeSessionId,
                                          const QString &model)
{
    QStringList args{
        QStringLiteral("-p"),
        QStringLiteral("--output-format"), QStringLiteral("stream-json"),
        QStringLiteral("--input-format"), QStringLiteral("stream-json"),
        QStringLiteral("--include-partial-messages"),
        QStringLiteral("--verbose"),
        // Total lockdown: Skill is the ONLY built-in tool left in the session.
        QStringLiteral("--tools"), QStringLiteral("Skill"),
        QStringLiteral("--strict-mcp-config"),
        QStringLiteral("--disallowedTools"), QString::fromLatin1(kDeniedBuiltins),
        QStringLiteral("--append-system-prompt"), QString::fromLatin1(kSystemPrompt),
    };

    QStringList allowed{QStringLiteral("Skill")};
    if (mcpEnabled) {
        args << QStringLiteral("--mcp-config") << mcpConfigPath(projectFolder);
        allowed << jahshakaMcpToolPattern();
    }
    args << QStringLiteral("--allowedTools") << allowed.join(QLatin1Char(','));

    if (!model.isEmpty())
        args << QStringLiteral("--model") << model;
    if (!resumeSessionId.isEmpty())
        args << QStringLiteral("--resume") << resumeSessionId;
    return args;
}

QString ClaudeLaunchConfig::readSessionId(const QString &projectFolder)
{
    QFile file(QDir(projectFolder).filePath(QStringLiteral(".claude/") + kChatFile));
    if (!file.open(QIODevice::ReadOnly)) return QString();
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.object().value("sessionId").toString();
}

bool ClaudeLaunchConfig::writeSessionId(const QString &projectFolder, const QString &sessionId)
{
    QDir project(projectFolder);
    if (!project.mkpath(QStringLiteral(".claude"))) return false;
    const QJsonObject root{{"sessionId", sessionId}};
    return writeFileAtomic(project.filePath(QStringLiteral(".claude/") + kChatFile),
                           QJsonDocument(root).toJson(QJsonDocument::Indented), nullptr);
}

void ClaudeLaunchConfig::clearSessionId(const QString &projectFolder)
{
    QFile::remove(QDir(projectFolder).filePath(QStringLiteral(".claude/") + kChatFile));
}
