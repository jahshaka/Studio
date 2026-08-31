/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CLAUDESTREAMPARSER_H
#define CLAUDESTREAMPARSER_H

// ClaudeStreamParser — turns the Claude Code CLI's stream-json stdout
// (`claude -p --output-format stream-json --include-partial-messages`)
// into typed signals the chat window renders (CLAUDE_EDITOR_SPEC phase 2).
//
// Event shapes (verified against claude CLI 2.1.251):
//   {"type":"system","subtype":"init","session_id","tools":[...],
//    "mcp_servers":[{"name","status"}], "model", ...}
//   {"type":"stream_event","event":{API stream event: message_start,
//    content_block_start/delta/stop, message_delta, message_stop}, ...}
//   {"type":"assistant","message":{full API message so far}, ...}
//   {"type":"user","message":{tool_result blocks}, ...}
//   {"type":"result","subtype":"success"|"error_*","is_error","result",
//    "session_id","num_turns","total_cost_usd", ...}
// Unknown/extra event types (system/status, rate_limit_event, ...) are
// ignored; a non-JSON line raises parseError but never stops the stream.

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class ClaudeStreamParser : public QObject
{
    Q_OBJECT
public:
    explicit ClaudeStreamParser(QObject *parent = nullptr);

    /// Feed raw stdout bytes; complete lines are parsed, the tail buffered.
    void feed(const QByteArray &data);
    /// Drops any buffered partial line and per-message state (process died).
    void reset();

signals:
    /// system/init: the session is live. mcpServers lists "name (status)".
    void sessionStarted(const QString &sessionId, const QStringList &tools,
                        const QStringList &mcpServers, bool mcpConnected);
    /// A new assistant text block began streaming.
    void textBlockStarted();
    /// Streamed text (content_block_delta / text_delta).
    void textDelta(const QString &text);
    /// A complete assistant message: the final text (all text blocks joined)
    /// — replaces whatever streamed, so dropped deltas can't corrupt the UI.
    void assistantText(const QString &fullText);
    /// The assistant invoked a tool (from the complete assistant message, so
    /// `inputJson` is the full input). Rendered as the collapsible line.
    void toolUseStarted(const QString &name, const QString &inputJson);
    /// A tool finished (user/tool_result). `snippet` is a short preview.
    void toolResult(const QString &snippet, bool isError);
    /// result event: the turn is over.
    void turnCompleted(bool ok, const QString &resultText,
                       const QString &sessionId, double costUsd);
    /// A line that wasn't valid JSON (never fatal).
    void parseError(const QString &line);

private:
    void parseLine(const QByteArray &line);
    void handleStreamEvent(const QJsonObject &event);
    void handleAssistant(const QJsonObject &message);
    void handleUser(const QJsonObject &message);

    QByteArray mBuffer;
    bool mTextBlockOpen = false;
};

#endif // CLAUDESTREAMPARSER_H
