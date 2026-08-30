/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MCPSERVER_H
#define MCPSERVER_H

// McpServer — the MCP endpoint inside the running app (CLAUDE_EDITOR_SPEC.md
// phase 1). Serves MCP over Streamable HTTP at http://127.0.0.1:<port>/mcp
// using Qt's QHttpServer. POST-only (stateless JSON responses) — a conforming
// subset of MCP Streamable HTTP; no SSE stream is offered (GET returns 405),
// which every compliant client, Claude Code included, handles.
//
// Security (owner decisions, binding):
//   - OFF by default; started from the Preferences toggle or --mcp-port=N.
//   - binds 127.0.0.1 ONLY.
//   - a per-session bearer token: requests without it get 401. The token is
//     regenerated per start (and on demand) and never persisted.
//   - one request at a time, FIFO: requests are queued and processed strictly
//     in order on the main thread — scripts touch the document there, same
//     rule as the console dock. Re-entrant arrivals (a script that pumps the
//     event loop, e.g. project.open) wait in the queue.

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <deque>

#include <QtHttpServer/QHttpServer>
#include <QtHttpServer/QHttpServerResponder>

#include "scripting/mcp/mcptools.h"

class QTcpServer;
class ScriptEngine;

class McpServer : public QObject
{
    Q_OBJECT
public:
    static constexpr quint16 kDefaultPort = 8639;

    explicit McpServer(ScriptEngine *engine, QObject *parent = nullptr);
    ~McpServer() override;

    /// Starts listening on 127.0.0.1:port. Generates a fresh session token.
    /// Returns false (with errorOut filled) when the port cannot be bound.
    bool start(quint16 port, QString *errorOut = nullptr);
    void stop();
    bool isRunning() const { return mTcp != nullptr; }
    quint16 port() const { return mPort; }

    /// The per-session bearer token clients must send as
    /// "Authorization: Bearer <token>".
    QString token() const { return mToken; }
    /// Replaces the token; connected clients must re-add with the new one.
    void regenerateToken();

    /// The exact line a user pastes to connect Claude Code.
    QString connectCommand() const;

signals:
    /// start/stop/regenerateToken — the settings UI refreshes off this.
    void stateChanged();

private:
    struct Pending
    {
        QJsonObject message;
        QHttpServerResponder responder;
    };

    void handlePost(const QHttpServerRequest &request, QHttpServerResponder &responder);
    void drainQueue();
    /// Dispatches one JSON-RPC request; returns the response object.
    QJsonObject dispatch(const QJsonObject &message);

    ScriptEngine *mEngine;
    McpTools mTools;
    QHttpServer *mHttp = nullptr;    // recreated per start (QHttpServer has no unbind)
    QTcpServer *mTcp = nullptr;      // owned by mHttp once bound
    quint16 mPort = 0;
    QString mToken;
    std::deque<Pending> mQueue;
    bool mBusy = false;
};

#endif // MCPSERVER_H
