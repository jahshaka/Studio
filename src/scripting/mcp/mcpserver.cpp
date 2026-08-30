/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/mcp/mcpserver.h"

#include <QHostAddress>
#include <QHttpHeaders>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTcpServer>

#include "data/constants.h"
#include "scripting/scriptengine.h"

namespace {

using StatusCode = QHttpServerResponder::StatusCode;

/// The protocol revisions this server knows. The newest is offered when the
/// client asks for one we don't know (per the MCP version-negotiation rule).
const char *kLatestProtocol = "2025-06-18";
bool knownProtocol(const QString &v)
{
    return v == QLatin1String("2024-11-05") || v == QLatin1String("2025-03-26")
        || v == QLatin1String(kLatestProtocol);
}

QJsonObject rpcResult(const QJsonValue &id, const QJsonObject &result)
{
    return { { "jsonrpc", "2.0" }, { "id", id }, { "result", result } };
}

QJsonObject rpcError(const QJsonValue &id, int code, const QString &message)
{
    return { { "jsonrpc", "2.0" }, { "id", id },
             { "error", QJsonObject{ { "code", code }, { "message", message } } } };
}

} // namespace

McpServer::McpServer(ScriptEngine *engine, QObject *parent)
    : QObject(parent), mEngine(engine), mTools(engine)
{
}

McpServer::~McpServer()
{
    stop();
}

bool McpServer::start(quint16 port, QString *errorOut)
{
    stop();
    regenerateToken();   // per-session: a fresh token every start

    mHttp = new QHttpServer(this);
    mHttp->route(QStringLiteral("/mcp"), QHttpServerRequest::Method::Post,
                 [this](const QHttpServerRequest &request, QHttpServerResponder &responder) {
                     handlePost(request, responder);
                 });
    // POST-only subset of Streamable HTTP: no SSE stream is offered.
    mHttp->route(QStringLiteral("/mcp"), QHttpServerRequest::Method::Get,
                 [] { return QHttpServerResponse(StatusCode::MethodNotAllowed); });
    mHttp->route(QStringLiteral("/mcp"), QHttpServerRequest::Method::Delete,
                 [] { return QHttpServerResponse(StatusCode::MethodNotAllowed); });

    // 127.0.0.1 ONLY — never a routable interface (owner decision: localhost
    // lockdown; remote access is a different spec).
    auto *tcp = new QTcpServer(mHttp);
    if (!tcp->listen(QHostAddress::LocalHost, port) || !mHttp->bind(tcp)) {
        if (errorOut)
            *errorOut = QStringLiteral("cannot listen on 127.0.0.1:%1 (%2)")
                            .arg(port).arg(tcp->errorString());
        delete mHttp;
        mHttp = nullptr;
        emit stateChanged();
        return false;
    }
    mTcp = tcp;
    mPort = tcp->serverPort();
    emit stateChanged();
    return true;
}

void McpServer::stop()
{
    if (!mHttp) return;
    // Queued requests die with their connections; nothing must outlive the server.
    mQueue.clear();
    delete mHttp;   // owns the QTcpServer
    mHttp = nullptr;
    mTcp = nullptr;
    mPort = 0;
    emit stateChanged();
}

void McpServer::regenerateToken()
{
    QString t;
    for (int i = 0; i < 4; ++i)
        t += QString::number(QRandomGenerator::system()->generate64(), 16)
                 .rightJustified(16, QLatin1Char('0'));
    mToken = t;
    emit stateChanged();
}

QString McpServer::connectCommand() const
{
    return QStringLiteral("claude mcp add --transport http jahshaka "
                          "http://127.0.0.1:%1/mcp --header \"Authorization: Bearer %2\"")
        .arg(mPort == 0 ? kDefaultPort : mPort)
        .arg(mToken);
}

void McpServer::handlePost(const QHttpServerRequest &request, QHttpServerResponder &responder)
{
    // Bearer auth on every request: another local user must not be able to
    // drive the editor.
    const QByteArray auth = request.value(QByteArrayLiteral("authorization"));
    if (mToken.isEmpty() || auth != QByteArrayLiteral("Bearer ") + mToken.toUtf8()) {
        QHttpHeaders headers;
        headers.append(QHttpHeaders::WellKnownHeader::WWWAuthenticate,
                       QByteArrayLiteral("Bearer realm=\"jahshaka-mcp\""));
        responder.write(headers, StatusCode::Unauthorized);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(request.body(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        responder.write(QJsonDocument(rpcError(QJsonValue(), -32700,
                            QStringLiteral("parse error: %1").arg(parseError.errorString()))),
                        StatusCode::BadRequest);
        return;
    }

    const QJsonObject message = doc.object();
    if (!message.contains(QLatin1String("id"))) {
        // A notification (notifications/initialized, ...): accepted, no body.
        responder.write(StatusCode::Accepted);
        return;
    }

    // FIFO, one at a time (spec): queue, then drain on the main thread. If a
    // running script pumps the event loop, later arrivals simply wait here.
    mQueue.push_back(Pending{ message, std::move(responder) });
    drainQueue();
}

void McpServer::drainQueue()
{
    if (mBusy) return;
    mBusy = true;
    while (!mQueue.empty()) {
        Pending pending = std::move(mQueue.front());
        mQueue.pop_front();
        const QJsonObject response = dispatch(pending.message);   // may re-enter the event loop
        pending.responder.write(QJsonDocument(response), StatusCode::Ok);
    }
    mBusy = false;
}

QJsonObject McpServer::dispatch(const QJsonObject &message)
{
    const QJsonValue id = message.value(QLatin1String("id"));
    const QString method = message.value(QLatin1String("method")).toString();
    const QJsonObject params = message.value(QLatin1String("params")).toObject();

    if (method == QLatin1String("initialize")) {
        const QString asked = params.value(QLatin1String("protocolVersion")).toString();
        return rpcResult(id, QJsonObject{
            { "protocolVersion", knownProtocol(asked) ? asked : QString::fromLatin1(kLatestProtocol) },
            { "capabilities", QJsonObject{ { "tools", QJsonObject{} } } },
            { "serverInfo", QJsonObject{
                { "name", "jahshaka" },
                { "title", "Jahshaka Studio" },
                { "version", Constants::CONTENT_VERSION } } },
            { "instructions",
              "Jahshaka's scripting engine is the whole capability surface: call "
              "api_docs to learn the verbs, run_script to build (each call is one "
              "undo step), describe_scene and screenshot to verify." } });
    }
    if (method == QLatin1String("ping"))
        return rpcResult(id, QJsonObject{});
    if (method == QLatin1String("tools/list"))
        return rpcResult(id, QJsonObject{ { "tools", mTools.listTools() } });
    if (method == QLatin1String("tools/call")) {
        const QString name = params.value(QLatin1String("name")).toString();
        const QJsonObject args = params.value(QLatin1String("arguments")).toObject();
        return rpcResult(id, mTools.call(name, args));
    }
    return rpcError(id, -32601, QStringLiteral("method not found: %1").arg(method));
}
