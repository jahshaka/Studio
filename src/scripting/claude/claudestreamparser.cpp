/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "claudestreamparser.h"

#include <QJsonArray>
#include <QJsonDocument>

ClaudeStreamParser::ClaudeStreamParser(QObject *parent) : QObject(parent) {}

void ClaudeStreamParser::feed(const QByteArray &data)
{
    mBuffer.append(data);
    for (;;) {
        int nl = mBuffer.indexOf('\n');
        if (nl < 0) break;
        QByteArray line = mBuffer.left(nl).trimmed();
        mBuffer.remove(0, nl + 1);
        if (!line.isEmpty()) parseLine(line);
    }
}

void ClaudeStreamParser::reset()
{
    mBuffer.clear();
    mTextBlockOpen = false;
}

void ClaudeStreamParser::parseLine(const QByteArray &line)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit parseError(QString::fromUtf8(line.left(200)));
        return;
    }
    const QJsonObject obj = doc.object();
    const QString type = obj.value("type").toString();

    if (type == "system") {
        if (obj.value("subtype").toString() == "init") {
            QStringList tools, servers;
            bool connected = false;
            for (const auto &t : obj.value("tools").toArray())
                tools << t.toString();
            for (const auto &s : obj.value("mcp_servers").toArray()) {
                const QJsonObject so = s.toObject();
                const QString status = so.value("status").toString();
                servers << QString("%1 (%2)").arg(so.value("name").toString(), status);
                if (so.value("name").toString() == "jahshaka" && status == "connected")
                    connected = true;
            }
            emit sessionStarted(obj.value("session_id").toString(), tools,
                                servers, connected);
        }
    } else if (type == "stream_event") {
        handleStreamEvent(obj.value("event").toObject());
    } else if (type == "assistant") {
        handleAssistant(obj.value("message").toObject());
    } else if (type == "user") {
        handleUser(obj.value("message").toObject());
    } else if (type == "result") {
        emit turnCompleted(!obj.value("is_error").toBool(),
                           obj.value("result").toString(),
                           obj.value("session_id").toString(),
                           obj.value("total_cost_usd").toDouble());
        mTextBlockOpen = false;
    }
    // system/status, rate_limit_event, control_* — deliberately ignored.
}

void ClaudeStreamParser::handleStreamEvent(const QJsonObject &event)
{
    const QString etype = event.value("type").toString();
    if (etype == "content_block_start") {
        const QJsonObject block = event.value("content_block").toObject();
        if (block.value("type").toString() == "text") {
            mTextBlockOpen = true;
            emit textBlockStarted();
            const QString initial = block.value("text").toString();
            if (!initial.isEmpty()) emit textDelta(initial);
        }
        // tool_use blocks surface from the complete assistant message
        // (there the input is whole, not a stream of json fragments).
    } else if (etype == "content_block_delta") {
        const QJsonObject delta = event.value("delta").toObject();
        if (delta.value("type").toString() == "text_delta" && mTextBlockOpen)
            emit textDelta(delta.value("text").toString());
    } else if (etype == "content_block_stop" || etype == "message_stop") {
        mTextBlockOpen = mTextBlockOpen && etype != "message_stop";
    }
}

void ClaudeStreamParser::handleAssistant(const QJsonObject &message)
{
    QString text;
    for (const auto &c : message.value("content").toArray()) {
        const QJsonObject block = c.toObject();
        const QString btype = block.value("type").toString();
        if (btype == "text") {
            text += block.value("text").toString();
        } else if (btype == "tool_use") {
            const QJsonDocument input(block.value("input").toObject());
            emit toolUseStarted(block.value("name").toString(),
                                QString::fromUtf8(input.toJson(QJsonDocument::Indented)));
        }
    }
    if (!text.isEmpty()) emit assistantText(text);
    mTextBlockOpen = false;
}

void ClaudeStreamParser::handleUser(const QJsonObject &message)
{
    for (const auto &c : message.value("content").toArray()) {
        const QJsonObject block = c.toObject();
        if (block.value("type").toString() != "tool_result") continue;
        const bool isError = block.value("is_error").toBool();
        QString snippet;
        const QJsonValue content = block.value("content");
        if (content.isString()) {
            snippet = content.toString();
        } else if (content.isArray()) {
            for (const auto &part : content.toArray()) {
                const QJsonObject po = part.toObject();
                if (po.value("type").toString() == "text") {
                    snippet = po.value("text").toString();
                    break;
                }
                if (po.value("type").toString() == "image") snippet = "(image)";
            }
        }
        snippet.truncate(400);
        emit toolResult(snippet, isError);
    }
}
