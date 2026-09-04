/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "claudestreamparser.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QList>
#include <QPair>

namespace {

// A tool_result may carry several images (browse_assets returns one per
// asset). The cap is a rendering budget, not a protocol limit: the dock is a
// small popup and the tool itself already bounds its response.
const int kMaxImagesPerToolResult = 24;

/// "resetsAt" has been seen as epoch seconds and as an ISO string; render
/// whichever arrives as a local time, and nothing at all when neither does.
QString formatResetTime(const QJsonValue &value)
{
    if (value.isDouble()) {
        const qint64 epoch = qint64(value.toDouble());
        if (epoch <= 0) return QString();
        // Seconds or milliseconds — both have appeared in agent tooling.
        const QDateTime when = epoch > 100000000000LL
                                   ? QDateTime::fromMSecsSinceEpoch(epoch)
                                   : QDateTime::fromSecsSinceEpoch(epoch);
        return when.toLocalTime().toString(QStringLiteral("HH:mm"));
    }
    return value.toString();
}

} // namespace

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
        handleResult(obj);
    } else if (type == "rate_limit_event") {
        handleRateLimit(obj);
    }
    // system/status, control_* — deliberately ignored.
}

// The denials come FIRST so the dock explains the refusal above the turn's
// closing line rather than after it.
void ClaudeStreamParser::handleResult(const QJsonObject &obj)
{
    QStringList denied;
    for (const auto &d : obj.value("permission_denials").toArray()) {
        const QString tool = d.toObject().value("tool_name").toString();
        if (!tool.isEmpty() && !denied.contains(tool)) denied << tool;
    }
    if (!denied.isEmpty()) emit permissionsDenied(denied);

    emit turnCompleted(!obj.value("is_error").toBool(),
                       obj.value("result").toString(),
                       obj.value("session_id").toString(),
                       obj.value("total_cost_usd").toDouble());
    mTextBlockOpen = false;
}

void ClaudeStreamParser::handleRateLimit(const QJsonObject &obj)
{
    // Defensive: the payload has been documented both at the top level and
    // nested under "rate_limit" (see the header's shape-provenance note).
    const QJsonObject nested = obj.value("rate_limit").toObject();
    const QJsonObject src = nested.isEmpty() ? obj : nested;
    const QString status = src.value("status").toString();
    if (status.isEmpty()) return;   // not a shape we understand — stay silent
    emit rateLimitEvent(status, formatResetTime(src.value("resetsAt")));
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
        } else if (btype == "thinking" || btype == "redacted_thinking") {
            // The content stays elided (that was the ask); only the fact that
            // there WAS a pause is surfaced.
            emit thinkingBlock();
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
        QList<QPair<QByteArray, QString>> images;   // decoded bytes + mime
        const QJsonValue content = block.value("content");
        if (content.isString()) {
            snippet = content.toString();
        } else if (content.isArray()) {
            for (const auto &part : content.toArray()) {
                const QJsonObject po = part.toObject();
                const QString ptype = po.value("type").toString();
                if (ptype == "text") {
                    // The FIRST text part is the preview; later ones (the
                    // per-image captions browse_assets sends) are not.
                    if (snippet.isEmpty()) snippet = po.value("text").toString();
                } else if (ptype == "image" && images.size() < kMaxImagesPerToolResult) {
                    const QJsonObject source = po.value("source").toObject();
                    if (source.value("type").toString() != QLatin1String("base64"))
                        continue;   // url sources are not something we fetch
                    const QByteArray decoded = QByteArray::fromBase64(
                        source.value("data").toString().toLatin1());
                    if (decoded.isEmpty()) continue;
                    QString mime = source.value("media_type").toString();
                    if (mime.isEmpty()) mime = QStringLiteral("image/png");
                    images.append({decoded, mime});
                }
            }
        }
        snippet.truncate(400);
        emit toolResult(snippet, isError);
        // After the text preview, so the picture lands under its caption.
        for (const auto &image : images) emit toolResultImage(image.first, image.second);
    }
}
