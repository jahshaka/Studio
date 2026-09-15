// Link stubs for ui.mcp_prefs: the page under test talks to a server, a main
// window and the theme, and none of the three is what is being tested.
// (tests/ui house pattern — see test_stubs.cpp for the material panel's.)

#include <QString>

#include "scripting/mcp/mcpserver.h"
#include "scripting/mcp/mcptools.h"
#include "shell/mainwindow.h"
#include "ui/style/thememanager.h"

int gStubServerStops = 0;
bool McpServerStubStart(quint16 port, QString *errorOut);

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

// The window: the page asks it to (re)start the server so the console dock
// gets the fresh connect line. Nothing here needs either.
bool MainWindow::startMcpServer(quint16 port, QString *errorOut)
{
    return McpServerStubStart(port, errorOut);
}

// The theme: Classic, so the page builds plain QCheckBoxes and the test needs
// no qlementine.
bool ThemeManager::classicActive() { return true; }

bool McpServerStubStart(quint16, QString *) { return true; }
