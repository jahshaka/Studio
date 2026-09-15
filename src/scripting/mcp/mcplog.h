/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MCPLOG_H
#define MCPLOG_H

// McpLog — what the MCP surface writes down about itself (owner, 2026-09-15).
//
// TWO FILES, both under `<data root>/logs/`, and they are different promises:
//
//   mcp-errors.log            ALWAYS ON, and it is only failures: a tool call
//                             that came back isError, a run_script that threw
//                             (its message and failing LINE), a timeout, a
//                             refused request (401), a malformed or unknown
//                             JSON-RPC message. NEVER THE SCRIPT TEXT and never
//                             a tool argument's value — but note what the
//                             MESSAGE is: a JS error quotes identifiers
//                             ("ReferenceError: addCubes is not defined") and
//                             carries whatever a script chose to throw, so a
//                             fragment of the author's own naming can appear
//                             there. That is what §361 asked for (the message
//                             and the failing line are the point of the file);
//                             it is stated rather than glossed. This is the file that
//                             answers "what did the agent try that did not
//                             work", and it must cost the user nothing to have
//                             it on, so it is bounded: 1 MiB, one previous
//                             generation kept beside it (mcp-errors.log.1), so
//                             2 MiB total, for ever.
//
//   mcp-session-<id>.jsonl    OPT-IN (Preferences -> Claude/MCP, default OFF),
//                             one line per tool call: the tool, the argument
//                             KEYS with their sizes (never the values, except
//                             numbers, booleans and a handful of enum-shaped
//                             keys like `view` or `action`), the registry
//                             VERBS a run_script actually called, the duration
//                             and the outcome. This is the research file: it
//                             says which tools and verbs an agent reaches for
//                             and where it stalls. A sub-option, also OFF,
//                             adds the script SOURCE.
//
// Both files open with a versioned SCHEMA LINE. That line is the contract with
// a later collector — the "send it to us" half is NOT built here, and nothing
// in this class ever leaves the machine.

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

/// One tool call, as the log sees it. McpTools fills it; the fields a tool
/// does not have stay empty.
struct McpCallRecord
{
    QString     tool;
    QJsonObject args;            ///< summarised: key -> {type, size} (+ value for scalars)
    QStringList verbs;           ///< run_script: the registry verbs the script called
    qint64      durationMs = 0;
    bool        ok = true;
    QString     error;           ///< the message a failed call answered with (a JS
                                 ///< error message may quote identifiers; it is
                                 ///< never the script text)
    int         line = 0;        ///< run_script: the failing line, 0 when unknown
    bool        timedOut = false;
    QJsonObject detail;          ///< per-tool facts (grade, size, bytes, rows)
    QString     scriptSource;    ///< carried ONLY when the source sub-option is on
};

class McpLog
{
public:
    static McpLog &instance();

    // ---- the two switches (persisted; the Preferences page and
    // app.mcpLogging() are the two ways a user sets them) ----
    bool sessionRecording() const;
    bool recordScriptSource() const;
    void setSessionRecording(bool on);
    void setRecordScriptSource(bool on);

    /// A new recording session (McpServer::start). The id names the session
    /// file and appears on every error line; it is generated here and has
    /// NOTHING to do with the bearer token.
    void beginSession();
    QString sessionId() const { return mSessionId; }

    QString errorLogPath() const;
    /// The session file for this session — empty when recording is off.
    QString sessionLogPath() const;

    /// One completed tool call: the error file gets it when it failed, the
    /// session file when recording is on.
    void recordCall(const McpCallRecord &record);
    /// Something that never reached a tool: a 401, a parse error, an unknown
    /// method. Error file only.
    void recordRefusal(const QString &kind, const QString &detail);

    /// Argument summary: keys with type and size, values only for numbers,
    /// booleans and the keys in `enumKeys` — which the caller derives from the
    /// tools' PUBLISHED SCHEMAS (the `enum` arrays in tools/list), so the
    /// allowlist cannot drift away from what the schemas actually offer.
    /// Public because it is the contract.
    static QJsonObject summariseArgs(const QJsonObject &args, const QSet<QString> &enumKeys);

    /// The sentence the Preferences page and api_docs both say.
    static QString privacyNote();
    /// One line for api_docs: what is being written right now, and where.
    QString stateLine() const;

private:
    McpLog() = default;
    void ensureSwitchesLoaded() const;
    void appendLine(const QString &path, const QJsonObject &line, const QJsonObject &schema);

    mutable bool mLoaded = false;
    mutable bool mSession = false;
    mutable bool mSource = false;
    QString mSessionId;
};

#endif // MCPLOG_H
