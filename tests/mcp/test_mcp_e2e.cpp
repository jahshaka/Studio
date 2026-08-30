// MCP server end-to-end test (CLAUDE_EDITOR_SPEC.md phase 1).
//
// Spawns the real Jahshaka binary with --mcp-port=<free port> (windowed: the
// engine viewport must be live for the screenshot tool, exactly like
// scripting.e2e.full_surface) and drives http://127.0.0.1:<port>/mcp as an
// MCP client over plain JSON-RPC POSTs:
//   - requests without (or with a wrong) bearer token get 401
//   - initialize handshake: protocol version + tools capability + serverInfo
//   - notifications are accepted with 202 and no body
//   - tools/list exposes EXACTLY the five phase-1 tools
//   - run_script creates a project + primitive, verified via describe_scene
//   - run_script errors surface as isError tool results with console intact
//   - screenshot returns a decodable PNG at the requested size
//   - undo_redo reverts the last run_script call (describe_scene confirms)
//   - api_docs returns the registry reference (whole and per-module)
//
// HOME points at a scratch dir (set by ctest) so the run can never touch the
// user's live library. Framework-free; non-zero exit on failure.

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTcpServer>
#include <QTimer>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

struct HttpResult
{
    int status = 0;
    QByteArray body;
    QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
};

static HttpResult post(QNetworkAccessManager &net, const QUrl &url,
                       const QJsonObject &message, const QString &token)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!token.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());

    QNetworkReply *reply = net.post(request, QJsonDocument(message).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    HttpResult result;
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    reply->deleteLater();
    return result;
}

static QJsonObject rpc(const char *method, int id, const QJsonObject &params = {})
{
    QJsonObject m{ { "jsonrpc", "2.0" }, { "id", id }, { "method", method } };
    if (!params.isEmpty()) m["params"] = params;
    return m;
}

static QJsonObject callTool(QNetworkAccessManager &net, const QUrl &url, const QString &token,
                            int id, const char *name, const QJsonObject &args = {})
{
    const QJsonObject params{ { "name", name }, { "arguments", args } };
    return post(net, url, rpc("tools/call", id, params), token).json()
        .value("result").toObject();
}

/// The first text content item of a tool result, parsed as JSON.
static QJsonObject toolJson(const QJsonObject &result)
{
    const QJsonArray content = result.value("content").toArray();
    if (content.isEmpty()) return {};
    return QJsonDocument::fromJson(
        content.first().toObject().value("text").toString().toUtf8()).object();
}

static QString toolText(const QJsonObject &result)
{
    const QJsonArray content = result.value("content").toArray();
    if (content.isEmpty()) return {};
    return content.first().toObject().value("text").toString();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // A free port, chosen by binding and releasing (a tiny race, accepted).
    quint16 port = 0;
    {
        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, 0)) {
            printf("FAIL: cannot probe for a free port\n");
            return 1;
        }
        port = probe.serverPort();
    }

    // Spawn the real binary. HOME is the scratch dir (set by ctest); stdout
    // carries the token line we must parse.
    QProcess jahshaka;
    jahshaka.setProcessChannelMode(QProcess::MergedChannels);
    jahshaka.start(QStringLiteral(JAHSHAKA_BINARY),
                   { QStringLiteral("--mcp-port=%1").arg(port) });
    if (!jahshaka.waitForStarted(15000)) {
        printf("FAIL: could not start %s\n", JAHSHAKA_BINARY);
        return 1;
    }

    QString token;
    QByteArray bootLog;
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 120000 && token.isEmpty()
               && jahshaka.state() == QProcess::Running) {
            jahshaka.waitForReadyRead(500);
            bootLog += jahshaka.readAll();
            const int at = bootLog.indexOf("MCP: token ");
            if (at >= 0) {
                const int end = bootLog.indexOf('\n', at);
                if (end > at) token = QString::fromUtf8(bootLog.mid(at + 11, end - at - 11)).trimmed();
            }
        }
    }
    CHECK(!token.isEmpty(), "app printed the MCP session token to stdout");
    if (token.isEmpty()) {
        printf("---- app output ----\n%s\n", bootLog.constData());
        jahshaka.kill();
        return 1;
    }

    QNetworkAccessManager net;
    const QUrl url(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    int id = 0;

    // ---- auth ------------------------------------------------------------
    {
        const HttpResult noToken = post(net, url, rpc("ping", ++id), QString());
        CHECK(noToken.status == 401, "request without a token gets 401");
        const HttpResult badToken = post(net, url, rpc("ping", ++id), QStringLiteral("wrong"));
        CHECK(badToken.status == 401, "request with a bad token gets 401");
    }

    // ---- handshake -------------------------------------------------------
    {
        const HttpResult r = post(net, url,
            rpc("initialize", ++id, QJsonObject{
                { "protocolVersion", "2025-06-18" },
                { "capabilities", QJsonObject{} },
                { "clientInfo", QJsonObject{ { "name", "test" }, { "version", "0" } } } }),
            token);
        CHECK(r.status == 200, "initialize answers 200");
        const QJsonObject result = r.json().value("result").toObject();
        CHECK(result.value("protocolVersion").toString() == "2025-06-18",
              "initialize echoes the requested protocol version");
        CHECK(result.value("capabilities").toObject().contains("tools"),
              "initialize advertises the tools capability");
        CHECK(result.value("serverInfo").toObject().value("name").toString() == "jahshaka",
              "serverInfo names the server");

        // The initialized notification has no id: 202, no body.
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        QNetworkReply *reply = net.post(request, QJsonDocument(QJsonObject{
            { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } }).toJson());
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        CHECK(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 202,
              "notifications get 202 Accepted");
        reply->deleteLater();

        const HttpResult ping = post(net, url, rpc("ping", ++id), token);
        CHECK(ping.status == 200 && ping.json().contains("result"), "ping pongs");
    }

    // ---- tools/list ------------------------------------------------------
    {
        const HttpResult r = post(net, url, rpc("tools/list", ++id), token);
        const QJsonArray tools = r.json().value("result").toObject().value("tools").toArray();
        CHECK(tools.size() == 5, "tools/list has exactly 5 tools");
        QStringList names;
        for (const QJsonValue &t : tools) {
            const QJsonObject tool = t.toObject();
            names << tool.value("name").toString();
            if (!tool.contains("inputSchema")) {
                printf("FAIL: tool %s has no inputSchema\n", qPrintable(names.last()));
                ++failures;
            }
        }
        names.sort();
        CHECK(names == QStringList({ "api_docs", "describe_scene", "run_script",
                                     "screenshot", "undo_redo" }),
              "the five tools are run_script/api_docs/describe_scene/screenshot/undo_redo");
    }

    // ---- api_docs --------------------------------------------------------
    {
        const QString whole = toolText(callTool(net, url, token, ++id, "api_docs"));
        CHECK(whole.contains("scene.addPrimitive"), "api_docs returns the verb reference");
        const QString one = toolText(callTool(net, url, token, ++id, "api_docs",
                                              QJsonObject{ { "module", "scene" } }));
        CHECK(one.contains("addPrimitive") && one.size() < whole.size(),
              "api_docs(module) returns just that module");
        const QJsonObject unknown = callTool(net, url, token, ++id, "api_docs",
                                             QJsonObject{ { "module", "nope" } });
        CHECK(unknown.value("isError").toBool(), "api_docs(unknown module) is a tool error");
    }

    // ---- run_script builds; describe_scene verifies ----------------------
    {
        // Unique per run: the scratch HOME (and its project DB) persists
        // between ctest invocations.
        const QString script = QStringLiteral(
            "project.create('McpE2E_%1'); scene.addPrimitive('cube')")
            .arg(QDateTime::currentMSecsSinceEpoch());
        QJsonObject created = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script", script }, { "label", "build a cube" } }));
        CHECK(created.value("ok").toBool(), "run_script creates a project and a cube");
        CHECK(!created.value("result").toString().isEmpty(),
              "run_script returns the completion value (the node id)");

        const QJsonObject scene = toolJson(callTool(net, url, token, ++id, "describe_scene"));
        CHECK(scene.value("projectOpen").toBool(), "describe_scene sees the open project");
        const QJsonArray nodes = scene.value("nodes").toArray();
        bool cubeFound = false;
        for (const QJsonValue &n : nodes)
            if (n.toObject().value("name").toString().contains("Cube", Qt::CaseInsensitive))
                cubeFound = true;
        CHECK(cubeFound, "describe_scene lists the new cube");

        // A script error is a tool error, not a transport failure.
        const QJsonObject bad = callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script", "console.log('before'); nope.such.thing()" } });
        CHECK(bad.value("isError").toBool(), "a script error is an isError tool result");
        const QJsonObject badBody = toolJson(bad);
        CHECK(!badBody.value("error").toString().isEmpty(), "the script error carries a message");
        CHECK(badBody.value("console").toArray().contains(QJsonValue("before")),
              "console output before the error is preserved");
    }

    // ---- screenshot ------------------------------------------------------
    {
        const QJsonObject result = callTool(net, url, token, ++id, "screenshot",
                                            QJsonObject{ { "width", 320 }, { "height", 240 } });
        const QJsonArray content = result.value("content").toArray();
        CHECK(content.size() == 1
                  && content.first().toObject().value("type").toString() == "image"
                  && content.first().toObject().value("mimeType").toString() == "image/png",
              "screenshot returns one MCP image content item");
        const QByteArray png = QByteArray::fromBase64(
            content.first().toObject().value("data").toString().toLatin1());
        QImage img;
        CHECK(img.loadFromData(png, "PNG"), "the screenshot decodes as PNG");
        CHECK(img.width() == 320 && img.height() == 240, "the screenshot has the requested size");
    }

    // ---- undo_redo -------------------------------------------------------
    {
        auto countNodes = [&]() {
            return toolJson(callTool(net, url, token, ++id, "describe_scene"))
                .value("nodes").toArray().size();
        };
        const int before = countNodes();
        const QJsonObject added = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script", "scene.addPrimitive('sphere')" }, { "label", "second shape" } }));
        CHECK(added.value("ok").toBool(), "run_script adds a second primitive");
        CHECK(countNodes() == before + 1, "describe_scene sees the second primitive");

        const QJsonObject undone = toolJson(callTool(net, url, token, ++id, "undo_redo",
                                                     QJsonObject{ { "action", "undo" } }));
        CHECK(undone.value("applied").toBool(), "undo_redo applies the undo");
        CHECK(countNodes() == before, "one undo removes the whole script run");

        const QJsonObject redone = toolJson(callTool(net, url, token, ++id, "undo_redo",
                                                     QJsonObject{ { "action", "redo" } }));
        CHECK(redone.value("applied").toBool(), "undo_redo applies the redo");
        CHECK(countNodes() == before + 1, "redo restores the primitive");
    }

    // ---- unknown method --------------------------------------------------
    {
        const HttpResult r = post(net, url, rpc("resources/list", ++id), token);
        CHECK(r.json().value("error").toObject().value("code").toInt() == -32601,
              "unknown methods get JSON-RPC -32601");
    }

    jahshaka.terminate();
    if (!jahshaka.waitForFinished(15000)) jahshaka.kill();

    if (failures == 0) printf("ALL OK\n");
    else printf("%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
