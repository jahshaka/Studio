// Link stubs for ui.mcp_prefs: the page under test talks to a server and the
// theme, and neither is what is being tested. (The window's start is a
// std::function the test leaves empty: the page falls back to the server's own.)
// (tests/ui house pattern — see test_stubs.cpp for the material panel's.)

#include <QString>

#include "scripting/mcp/mcpserver.h"
#include "scripting/mcp/mcptools.h"
#include "ui/style/thememanager.h"

int gStubServerStops = 0;

McpTools::McpTools(ScriptEngine *engine) : mEngine(engine) {}

McpServer::McpServer(ScriptEngine *engine, QObject *parent)
    : QObject(parent), mEngine(engine), mTools(engine)
{
}
McpServer::~McpServer() = default;

// isRunning() is inline in the header (mTcp != nullptr), so "running" has to
// be that pointer. Nothing in the page dereferences it — it only asks whether
// there is one, and on which port.
bool McpServer::start(quint16 port, QString *)
{
    mPort = port;
    mTcp = reinterpret_cast<QTcpServer *>(this);
    emit stateChanged();
    return true;
}

void McpServer::stop()
{
    ++gStubServerStops;
    mTcp = nullptr;
    emit stateChanged();
}

void McpServer::regenerateToken() { mToken = QStringLiteral("stub-token"); }

QString McpServer::connectCommand() const { return QStringLiteral("claude mcp add … stub"); }

// The theme: Classic, so the page builds plain QCheckBoxes and the test needs
// no qlementine.
bool ThemeManager::classicActive() { return true; }

