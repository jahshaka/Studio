/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/mcp/mcplog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>

#include "data/constants.h"
#include "data/settingsmanager.h"
#include "services/apppaths.h"

namespace {

const char *kSessionKey = "mcp_log_sessions";
const char *kSourceKey  = "mcp_log_script_source";

/// The error file is ALWAYS ON, so it is bounded for ever: one full file plus
/// one previous generation, and nothing else ever accumulates.
constexpr qint64 kErrorLogMaxBytes   = 1024 * 1024;
/// A recording session is a user's deliberate act, but a runaway agent must
/// not fill a disk either.
constexpr qint64 kSessionLogMaxBytes = 8 * 1024 * 1024;
/// Session files kept in the logs directory; the oldest go first.
constexpr int    kKeepSessionFiles   = 20;

/// Argument keys whose VALUE is part of the question being asked and carries
/// no content of the user's: they are enum-shaped, short and fixed by the tool
/// schema. Everything else that is a string is recorded by SIZE only.
bool isEnumKey(const QString &key)
{
    static const QStringList kEnums = {
        QStringLiteral("view"),   QStringLiteral("action"),  QStringLiteral("grade"),
        QStringLiteral("format"), QStringLiteral("type"),    QStringLiteral("direction"),
        QStringLiteral("module"), QStringLiteral("include"),
    };
    return kEnums.contains(key);
}

QString nowIso() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }

QString logsDir()
{
    const QString dir = AppPaths::dataRoot() + QStringLiteral("/logs");
    QDir().mkpath(dir);
    return dir;
}

void rotate(const QString &path, qint64 maxBytes, bool keepPrevious)
{
    const QFileInfo info(path);
    if (!info.exists() || info.size() < maxBytes) return;
    if (!keepPrevious) { QFile::remove(path); return; }
    const QString previous = path + QStringLiteral(".1");
    QFile::remove(previous);
    QFile::rename(path, previous);
}

/// Keep the newest `keep` session files; a machine that has been driven by an
/// agent for months should not carry a year of them.
void pruneSessionFiles(int keep)
{
    QDir dir(logsDir());
    QFileInfoList files = dir.entryInfoList({ QStringLiteral("mcp-session-*.jsonl") },
                                           QDir::Files, QDir::Time);
    for (int i = keep; i < files.size(); ++i) QFile::remove(files[i].absoluteFilePath());
}

} // namespace

McpLog &McpLog::instance()
{
    static McpLog log;
    return log;
}

void McpLog::ensureSwitchesLoaded() const
{
    if (mLoaded) return;
    mLoaded = true;
    if (SettingsManager *s = SettingsManager::getDefaultManager()) {
        mSession = s->getValue(QString::fromLatin1(kSessionKey), false).toBool();
        mSource  = s->getValue(QString::fromLatin1(kSourceKey), false).toBool();
    }
}

bool McpLog::sessionRecording() const   { ensureSwitchesLoaded(); return mSession; }
bool McpLog::recordScriptSource() const { ensureSwitchesLoaded(); return mSource; }

void McpLog::setSessionRecording(bool on)
{
    ensureSwitchesLoaded();
    mSession = on;
    if (SettingsManager *s = SettingsManager::getDefaultManager())
        s->setValue(QString::fromLatin1(kSessionKey), on);
    if (on && mSessionId.isEmpty()) beginSession();
}

void McpLog::setRecordScriptSource(bool on)
{
    ensureSwitchesLoaded();
    mSource = on;
    if (SettingsManager *s = SettingsManager::getDefaultManager())
        s->setValue(QString::fromLatin1(kSourceKey), on);
}

void McpLog::beginSession()
{
    // Readable and unique, and NOT derived from the bearer token — the token
    // must never reach a file name (or a file).
    // UTC, like every `t` in the files — a session id that disagreed with the
    // timestamps beside it would be read as a different session.
    mSessionId = QStringLiteral("%1-%2")
                     .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss")))
                     .arg(QRandomGenerator::system()->generate() & 0xffffu, 4, 16,
                          QLatin1Char('0'));
    if (sessionRecording()) pruneSessionFiles(kKeepSessionFiles);
}

QString McpLog::errorLogPath() const
{
    return logsDir() + QStringLiteral("/mcp-errors.log");
}

QString McpLog::sessionLogPath() const
{
    if (!sessionRecording() || mSessionId.isEmpty()) return QString();
    return logsDir() + QStringLiteral("/mcp-session-%1.jsonl").arg(mSessionId);
}

QJsonObject McpLog::summariseArgs(const QJsonObject &args)
{
    QJsonObject out;
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        const QJsonValue v = it.value();
        QJsonObject entry;
        if (v.isString()) {
            entry["type"] = "string";
            entry["chars"] = v.toString().size();
            if (isEnumKey(it.key()) && v.toString().size() <= 32) entry["value"] = v.toString();
        } else if (v.isBool()) {
            entry["type"] = "bool";
            entry["value"] = v.toBool();
        } else if (v.isDouble()) {
            entry["type"] = "number";
            entry["value"] = v.toDouble();
        } else if (v.isArray()) {
            entry["type"] = "array";
            entry["size"] = v.toArray().size();
            if (isEnumKey(it.key())) entry["value"] = v.toArray();
        } else if (v.isObject()) {
            entry["type"] = "object";
            entry["keys"] = v.toObject().size();
        } else {
            entry["type"] = "null";
        }
        out.insert(it.key(), entry);
    }
    return out;
}

void McpLog::appendLine(const QString &path, const QJsonObject &line, const QJsonObject &schema)
{
    if (path.isEmpty()) return;
    const bool fresh = !QFileInfo::exists(path);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    if (fresh && !schema.isEmpty())
        file.write(QJsonDocument(schema).toJson(QJsonDocument::Compact) + '\n');
    file.write(QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n');
    file.close();
}

void McpLog::recordCall(const McpCallRecord &record)
{
    const QString when = nowIso();

    // ---- the error file: failures only, and never the source --------------
    if (!record.ok) {
        rotate(errorLogPath(), kErrorLogMaxBytes, true);
        QJsonObject line{
            { "t", when },
            { "session", mSessionId.isEmpty() ? QStringLiteral("-") : mSessionId },
            { "kind", record.timedOut ? "timeout" : "tool_error" },
            { "tool", record.tool },
            { "error", record.error },
            { "durationMs", record.durationMs },
        };
        if (record.line > 0) line["line"] = record.line;
        appendLine(errorLogPath(), line, QJsonObject{
            { "schema", "jahshaka.mcp.errors/1" },
            { "app", Constants::CONTENT_VERSION },
            { "note", "One object per FAILED MCP tool call or refused request. "
                      "Carries the JS error MESSAGE, which may quote an identifier or "
                      "whatever the script threw; never the script text, never an "
                      "argument's value." } });
    }

    // ---- the session file: every call, opt-in -----------------------------
    const QString sessionPath = sessionLogPath();
    if (sessionPath.isEmpty()) return;
    rotate(sessionPath, kSessionLogMaxBytes, false);
    QJsonObject line{
        { "t", when },
        { "tool", record.tool },
        { "args", record.args },
        { "durationMs", record.durationMs },
        { "ok", record.ok },
    };
    if (!record.verbs.isEmpty()) {
        QJsonArray verbs;
        for (const QString &v : record.verbs) verbs.append(v);
        line["verbs"] = verbs;
    }
    if (!record.detail.isEmpty()) line["detail"] = record.detail;
    if (!record.ok) {
        line["error"] = record.error;
        if (record.line > 0) line["line"] = record.line;
        if (record.timedOut) line["timedOut"] = true;
    }
    if (!record.scriptSource.isEmpty()) line["script"] = record.scriptSource;
    appendLine(sessionPath, line, QJsonObject{
        { "schema", "jahshaka.mcp.session/1" },
        { "app", Constants::CONTENT_VERSION },
        { "session", mSessionId },
        { "started", when },
        { "scriptSource", recordScriptSource() },
        { "note", "One object per MCP tool call: the tool, the argument KEYS with their "
                  "sizes (values only for numbers, booleans and enum-shaped keys), the "
                  "registry verbs a script called, the duration and the outcome. "
                  "Recorded because the user opted in; nothing is sent anywhere." } });
}

void McpLog::recordRefusal(const QString &kind, const QString &detail)
{
    rotate(errorLogPath(), kErrorLogMaxBytes, true);
    appendLine(errorLogPath(), QJsonObject{
        { "t", nowIso() },
        { "session", mSessionId.isEmpty() ? QStringLiteral("-") : mSessionId },
        { "kind", kind },
        { "error", detail },
    }, QJsonObject{
        { "schema", "jahshaka.mcp.errors/1" },
        { "app", Constants::CONTENT_VERSION },
        { "note", "One object per FAILED MCP tool call or refused request. "
                  "Carries the JS error MESSAGE, which may quote an identifier or "
                  "whatever the script threw; never the script text, never an "
                  "argument's value." } });
}

QString McpLog::privacyNote()
{
    return QStringLiteral(
        "Nothing is sent anywhere: both files stay on this machine, under the "
        "app's logs folder, and you can read or delete them at any time. The "
        "error log is always on and records only failures: what a call refused "
        "with, including the JavaScript error message, which may quote an "
        "identifier or whatever the script threw — never the script text itself. "
        "The session recording adds one line per tool call for tool "
        "research: which tools and verbs were used, argument names and sizes, "
        "how long each took. Argument VALUES are not recorded unless you also "
        "turn on \"Include the script source\".");
}

QString McpLog::stateLine() const
{
    if (!sessionRecording()) {
        return QStringLiteral(
            "MCP logging: failures only (%1). Session recording is off; turn it on in "
            "Preferences > Claude/MCP, or with app.mcpLogging({session: true}).")
            .arg(errorLogPath());
    }
    return QStringLiteral("MCP logging: failures (%1) and this session's tool calls (%2)%3.")
        .arg(errorLogPath(), sessionLogPath(),
             recordScriptSource() ? QStringLiteral(", script source included")
                                  : QStringLiteral(", without script source"));
}
