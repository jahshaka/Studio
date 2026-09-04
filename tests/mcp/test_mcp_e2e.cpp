// MCP server end-to-end test (CLAUDE_EDITOR_SPEC.md phase 1).
//
// Spawns the real Jahshaka binary with --mcp-port=<free port> (windowed: the
// engine viewport must be live for the screenshot tool, exactly like
// scripting.e2e.full_surface) and drives http://127.0.0.1:<port>/mcp as an
// MCP client over plain JSON-RPC POSTs:
//   - requests without (or with a wrong) bearer token get 401
//   - initialize handshake: protocol version + tools capability + serverInfo
//   - notifications are accepted with 202 and no body
//   - tools/list exposes EXACTLY the six tools
//   - run_script creates a project + primitive, verified via describe_scene
//   - run_script errors surface as isError tool results with console intact
//   - screenshot returns a decodable PNG at the requested size
//   - undo_redo reverts the last run_script call (describe_scene confirms)
//   - api_docs returns the registry reference (whole and per-module)
//   - F5: a scripted node.setProperty is UNDOABLE through undo_redo
//   - F6: run_script timeoutMs interrupts a runaway loop, the app survives,
//     and the NEXT request is served (the setInterrupted reset)
//   - F16: run_script's module list is generated from the live registry
//   - lane C #2: api_docs({verb}) returns one row, api_docs({search}) a
//     bounded set, both orders of magnitude under the full dump
//   - lane C #5: run_script echoes the engineErrors recorded during THAT run,
//     without stealing them from app.engineErrors()
//   - lane C #6: browse_assets returns rows + decodable thumbnail images
//     inside its documented budget
//   - every tool name is reachable through the chat dock's server-anchored
//     allow-list glob (mcp__jahshaka__*)
//
// HOME points at a scratch dir (set by ctest) so the run can never touch the
// user's live library. Framework-free; non-zero exit on failure.

#include <QCoreApplication>
#include <QRegularExpression>
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
        // Deliberate, not incidental: the tool set is the AI surface's entire
        // shape, so adding one is a decision that has to be made HERE too.
        // browse_assets is the sixth (AI_SURFACE_PROGRAM_SPEC lane C #6): the
        // byte-carrying view of assets.list, which is where its capability is.
        CHECK(tools.size() == 6, "tools/list has exactly 6 tools");
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
        CHECK(names == QStringList({ "api_docs", "browse_assets", "describe_scene",
                                     "run_script", "screenshot", "undo_redo" }),
              "the six tools are run_script/api_docs/describe_scene/screenshot/"
              "browse_assets/undo_redo");

        // The chat dock allows the whole server with a GLOB
        // (ClaudeLaunchConfig::jahshakaMcpToolPattern, asserted in the argv by
        // tests/claudechat), so a new tool reaches the dock with no code
        // change — as long as its NAME is a plain identifier the pattern can
        // match. A tool called "browse assets" would silently never be allowed.
        const QRegularExpression glob(
            QRegularExpression::wildcardToRegularExpression("mcp__jahshaka__*"));
        bool allMatched = true;
        for (const QString &n : names) {
            if (!QRegularExpression("^[a-z][a-z0-9_]*$").match(n).hasMatch()
                || !glob.match("mcp__jahshaka__" + n).hasMatch()) {
                printf("FAIL: tool name '%s' is not reachable through mcp__jahshaka__*\n",
                       qPrintable(n));
                allMatched = false;
            }
        }
        CHECK(allMatched, "every tool name is reachable through the dock's "
                          "mcp__jahshaka__* allow-list glob");

        QString browseDoc;
        for (const QJsonValue &t : tools)
            if (t.toObject().value("name").toString() == QLatin1String("browse_assets"))
                browseDoc = t.toObject().value("description").toString();
        CHECK(browseDoc.contains("assets.list"),
              "browse_assets names the verb its capability lives on");

        // F16: run_script's module list is GENERATED from the registry, so it
        // cannot go stale again. It used to be a hand-typed ten of thirteen —
        // three whole domains invisible to anything reading only the tool list.
        QString runScriptDoc;
        for (const QJsonValue &t : tools)
            if (t.toObject().value("name").toString() == QLatin1String("run_script"))
                runScriptDoc = t.toObject().value("description").toString();
        CHECK(runScriptDoc.contains("anim") && runScriptDoc.contains("particles")
                  && runScriptDoc.contains("avatar"),
              "run_script's description lists the modules the audit found missing "
              "(anim/particles/avatar) — it is generated, not typed");
        CHECK(runScriptDoc.contains("timeoutMs"),
              "run_script's description documents timeoutMs");
        CHECK(runScriptDoc.contains("editor.frame") || runScriptDoc.contains("native verb"),
              "and states the interrupt's bytecode-boundary limit");
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

        // ---- lane C #2: verb + search ------------------------------------
        // The whole reference is tens of kilobytes and grows with every
        // feature; a reader that knows a verb's name must not have to pay for
        // all of it. These two assertions are about SIZE as much as content.
        const QString one2 = toolText(callTool(net, url, token, ++id, "api_docs",
                                               QJsonObject{ { "verb", "scene.addPrimitive" } }));
        CHECK(one2.contains("scene.addPrimitive") && one2.contains("primitive"),
              "api_docs(verb) returns that verb's signature and doc");
        CHECK(one2.size() < 2000 && one2.size() * 20 < whole.size(),
              "api_docs(verb) is orders of magnitude smaller than the full dump");
        printf("      (full dump %lld bytes; one verb %lld bytes)\n",
               (long long)whole.size(), (long long)one2.size());
        CHECK(!one2.contains("scene.addLight"),
              "api_docs(verb) returns ONE verb, not the module");

        const QString bare = toolText(callTool(net, url, token, ++id, "api_docs",
                                               QJsonObject{ { "verb", "addPrimitive" } }));
        CHECK(bare.contains("scene.addPrimitive"),
              "api_docs(verb) accepts a bare verb name too");

        const QJsonObject noSuchVerb = callTool(net, url, token, ++id, "api_docs",
                                                QJsonObject{ { "verb", "scene.nope" } });
        CHECK(noSuchVerb.value("isError").toBool(),
              "api_docs(unknown verb) is a tool error, not an empty success");

        const QString found = toolText(callTool(net, url, token, ++id, "api_docs",
                                                QJsonObject{ { "search", "light" } }));
        CHECK(found.contains("scene.addLight"), "api_docs(search) matches verb names");
        CHECK(found.contains("match"), "api_docs(search) reports how many matched");
        CHECK(found.size() < whole.size() / 2,
              "api_docs(search) is a bounded slice of the reference");
        printf("      (search \"light\" %lld bytes)\n", (long long)found.size());

        const QString nothing = toolText(callTool(net, url, token, ++id, "api_docs",
            QJsonObject{ { "search", "zzzznosuchthing" } }));
        CHECK(nothing.contains("no verb matches"),
              "api_docs(search) with no matches says so instead of returning nothing");
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

    // ---- F5: node.setProperty is UNDOABLE --------------------------------
    // The audit's F5: node.setProperty was a direct document write documented
    // "not undoable yet", while every skill and this very tool promise that one
    // script run is one undo step — and it is the only path to light parameters
    // and every particle scalar.
    {
        const QJsonObject lit = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script",
                           "var l = scene.addLight('point', {position:{x:0,y:3,z:0}});"
                           "node.setProperty(l, 'intensity', 7.5);"
                           "JSON.stringify({id:l, v:node.property(l,'intensity')})" },
                         { "label", "add a light" } }));
        CHECK(lit.value("ok").toBool(), "run_script adds a light and sets its intensity");
        const QJsonObject litInfo =
            QJsonDocument::fromJson(lit.value("result").toString().toUtf8()).object();
        const QString lightId = litInfo.value("id").toString();
        CHECK(qAbs(litInfo.value("v").toDouble() - 7.5) < 0.001,
              "the light property took (7.5)");

        // Second run, so the first run's macro is closed: change it again.
        const QJsonObject changed = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script", QStringLiteral(
                               "node.setProperty('%1', 'intensity', 42);"
                               "node.property('%1','intensity')").arg(lightId) },
                         { "label", "brighten it" } }));
        CHECK(changed.value("ok").toBool()
                  && qAbs(changed.value("result").toDouble() - 42) < 0.001,
              "a second run raises the intensity to 42");

        const QJsonObject undone = toolJson(callTool(net, url, token, ++id, "undo_redo",
                                                     QJsonObject{ { "action", "undo" } }));
        CHECK(undone.value("applied").toBool(), "undo_redo applies");
        const QJsonObject read = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script", QStringLiteral("node.property('%1','intensity')").arg(lightId) } }));
        CHECK(qAbs(read.value("result").toDouble() - 7.5) < 0.001,
              "F5: undo_redo REVERTED the scripted property write (7.5 again)");

        // …and it comes back. NOTE the redo must be issued with NO run_script
        // in between: every script run opens an undo macro, which clears the
        // redo branch even when the script only reads (a pre-existing property
        // of ScriptEngine::evaluate, reported with this wave — it is why the
        // read above cannot sit between the undo and the redo).
        const QJsonObject again = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script", QStringLiteral("node.setProperty('%1','intensity', 99)").arg(lightId) },
                         { "label", "brighten again" } }));
        CHECK(again.value("ok").toBool(), "a third run sets 99");
        callTool(net, url, token, ++id, "undo_redo", QJsonObject{ { "action", "undo" } });
        const QJsonObject redone2 = toolJson(callTool(net, url, token, ++id, "undo_redo",
                                                      QJsonObject{ { "action", "redo" } }));
        CHECK(redone2.value("applied").toBool(), "undo_redo applies the redo");
        const QJsonObject reread = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script", QStringLiteral("node.property('%1','intensity')").arg(lightId) } }));
        CHECK(qAbs(reread.value("result").toDouble() - 99) < 0.001,
              "F5: and redo restores the scripted property write");
    }

    // ---- F6: timeoutMs interrupts a runaway script ------------------------
    // A runaway loop used to wedge the editor with no way out: the transport is
    // POST-only and serves one request at a time, so a cancel TOOL can never be
    // delivered mid-run. The budget has to travel WITH the script.
    {
        QElapsedTimer clock;
        clock.start();
        const QJsonObject spun = callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script", "var i = 0; while (true) { i++; }" },
                         { "timeoutMs", 1500 } });
        const qint64 elapsed = clock.elapsed();
        CHECK(spun.value("isError").toBool(), "an infinite loop returns a tool ERROR");
        const QJsonObject body = toolJson(spun);
        CHECK(body.value("timedOut").toBool(), "and it is flagged as a timeout");
        printf("      (the runaway script returned after %lld ms)\n", (long long)elapsed);
        CHECK(elapsed < 20000, "the watchdog actually fired well inside the request timeout");

        // THE assertion that proves setInterrupted was reset: the NEXT script
        // must run normally. Without the reset every later script dies instantly.
        const QJsonObject after = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script", "1 + 1" } }));
        CHECK(after.value("ok").toBool() && after.value("result").toInt() == 2,
              "the server serves the NEXT request normally (the interrupt flag was reset)");

        // The app is still alive and still serving other tools.
        const QJsonObject alive = toolJson(callTool(net, url, token, ++id, "describe_scene"));
        CHECK(alive.value("projectOpen").toBool(), "the editor survived the runaway script");
    }

    // ---- lane C #6: browse_assets ----------------------------------------
    // The library the model can SEE. A script result is JSON, so the pixels
    // need a tool; everything else (the filters, the ordering, the scope) is
    // the assets.list VERB, and this case asserts both halves agree.
    {
        // Three store assets with real, differently sized thumbnails: two at
        // import size (72 px) and one re-rendered at 256 — the one that proves
        // the downscale, because an over-budget thumbnail is the case the
        // budget exists for.
        const QJsonObject imported = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script", QStringLiteral(
                "var d = assets.createDrawer('BrowseLane');"
                "var wide = assets.importFile('%1/implane_wide.png');"
                "var tall = assets.importFile('%1/implane_tall.png');"
                "var alpha = assets.importFile('%1/implane_alpha.png', d);"
                "assets.refreshThumbnail(tall);"
                "JSON.stringify({drawer:d, wide:wide, tall:tall, alpha:alpha})")
                .arg(QStringLiteral(JAHSHAKA_FIXTURES)) },
                         { "label", "browse fixtures" } }));
        CHECK(imported.value("ok").toBool(), "fixtures imported for browse_assets");
        const QJsonObject ids =
            QJsonDocument::fromJson(imported.value("result").toString().toUtf8()).object();
        const int drawerId = ids.value("drawer").toInt();

        const QJsonObject browsed = callTool(net, url, token, ++id, "browse_assets");
        const QJsonArray content = browsed.value("content").toArray();
        CHECK(!content.isEmpty() && content.first().toObject().value("type").toString() == "text",
              "browse_assets leads with a text summary");
        const QJsonObject summary = QJsonDocument::fromJson(
            content.first().toObject().value("text").toString().toUtf8()).object();
        const QJsonArray rows = summary.value("assets").toArray();
        CHECK(rows.size() >= 3, "browse_assets lists the imported assets");
        CHECK(summary.value("limit").toInt() == 12, "the default row limit is 12");

        // The image blocks: each decodes to a real QImage, each is preceded by
        // a caption naming its guid, and every long edge is inside the cap.
        int imageBlocks = 0, totalBase64 = 0, maxEdge = 0;
        bool captionsLineUp = true;
        for (int i = 1; i < content.size(); ++i) {
            const QJsonObject item = content.at(i).toObject();
            if (item.value("type").toString() != QLatin1String("image")) continue;
            ++imageBlocks;
            const QString data = item.value("data").toString();
            totalBase64 += data.size();
            QImage img;
            if (!img.loadFromData(QByteArray::fromBase64(data.toLatin1()), "PNG")) {
                printf("FAIL: image content block %d does not decode as PNG\n", imageBlocks);
                ++failures;
                continue;
            }
            maxEdge = qMax(maxEdge, qMax(img.width(), img.height()));
            CHECK(item.value("mimeType").toString() == "image/png",
                  "the image block declares image/png");
            const QJsonObject caption = content.at(i - 1).toObject();
            if (caption.value("type").toString() != QLatin1String("text")
                || !caption.value("text").toString().contains(QLatin1Char('-')))
                captionsLineUp = false;
        }
        CHECK(imageBlocks >= 3, "browse_assets returns a thumbnail image per asset");
        CHECK(captionsLineUp, "every image block is captioned with its asset");
        CHECK(maxEdge <= 128 && maxEdge > 0,
              "every thumbnail is downscaled to the 128 px long edge");
        printf("      (%d image blocks, %d base64 chars, longest edge %d px)\n",
               imageBlocks, totalBase64, maxEdge);
        CHECK(summary.value("images").toInt() == imageBlocks,
              "the summary's image count matches the blocks actually sent");
        CHECK(summary.value("imageBytes").toInt() <= summary.value("imageBudgetBytes").toInt()
                  && summary.value("imageBudgetBytes").toInt() > 0,
              "browse_assets stays inside its declared image budget");

        // The re-rendered 256 px thumbnail is the one that HAD to be scaled:
        // without the downscale it would arrive at 128x256.
        bool sawScaled = false;
        for (const QJsonValue &r : rows) {
            const QJsonObject row = r.toObject();
            if (row.value("name").toString().contains("implane_tall")
                && row.value("image").toBool()
                && qMax(row.value("imageWidth").toInt(), row.value("imageHeight").toInt()) == 128)
                sawScaled = true;
        }
        CHECK(sawScaled, "a 256 px stored thumbnail arrives downscaled to 128 px");

        // The filters are the verb's, and the tool must not reinterpret them.
        const QJsonObject q = QJsonDocument::fromJson(
            callTool(net, url, token, ++id, "browse_assets",
                     QJsonObject{ { "query", "implane_wide" } })
                .value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        // >= 1, not == 1: the scratch HOME persists between ctest runs, so an
        // earlier invocation's import of the same fixture is still in the store.
        bool queryFiltered = !q.value("assets").toArray().isEmpty();
        for (const QJsonValue &r : q.value("assets").toArray())
            if (!r.toObject().value("name").toString().contains("implane_wide"))
                queryFiltered = false;
        CHECK(queryFiltered, "browse_assets({query}) filters by name");

        const QJsonObject inDrawer = QJsonDocument::fromJson(
            callTool(net, url, token, ++id, "browse_assets",
                     QJsonObject{ { "drawer", drawerId } })
                .value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        CHECK(inDrawer.value("assets").toArray().size() == 1
                  && inDrawer.value("assets").toArray().first().toObject()
                         .value("name").toString().contains("implane_alpha"),
              "browse_assets({drawer}) filters by drawer");

        const QJsonObject limited = QJsonDocument::fromJson(
            callTool(net, url, token, ++id, "browse_assets", QJsonObject{ { "limit", 1 } })
                .value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        CHECK(limited.value("assets").toArray().size() == 1, "browse_assets({limit}) caps the rows");
        CHECK(limited.value("more").toBool(),
              "and says there are more rather than looking complete");

        const QJsonObject clamped = QJsonDocument::fromJson(
            callTool(net, url, token, ++id, "browse_assets", QJsonObject{ { "limit", 9999 } })
                .value("content").toArray().first().toObject().value("text").toString().toUtf8()).object();
        CHECK(clamped.value("limit").toInt() == 24,
              "an absurd limit is clamped to the documented maximum (24)");

        // A bad type name is the VERB's refusal, surfaced, not swallowed.
        const QJsonObject badType = callTool(net, url, token, ++id, "browse_assets",
                                             QJsonObject{ { "type", "sandwich" } });
        CHECK(badType.value("isError").toBool()
                  && toolText(badType).contains("unknown type"),
              "browse_assets surfaces assets.list's refusal of an unknown type");

        // The same filters through the verb: what a script asks for and what
        // the tool shows must be the same listing.
        const QJsonObject viaVerb = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script", "assets.list({scope:'store', query:'implane', limit:2}).length" } }));
        CHECK(viaVerb.value("result").toInt() == 2,
              "assets.list({query, limit}) is a verb a script can call for itself");
    }

    // ---- lane C #5: run_script echoes the engine errors of THAT run --------
    // The renderer refuses rather than throws, so a texture that will not
    // decode produces a wrong picture and a perfectly successful script. The
    // fixture is a TRUNCATED png (header, no pixels): the document's header
    // probe accepts it, the engine's decoder does not. See tests/mcp/CMakeLists
    // for why it has to be that and not simply a text file.
    {
        const QJsonObject before = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script", "JSON.stringify(app.engineErrors())" } }));
        const int recordedBefore = QJsonDocument::fromJson(
            before.value("result").toString().toUtf8()).object().value("recorded").toInt();
        CHECK(!before.contains("engineErrors"),
              "a clean run carries no engineErrors field at all");

        const QJsonObject broke = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script", QStringLiteral(
                "var c = scene.addPrimitive('cube');"
                "material.set(c, {baseColorMap: '%1'});"
                "editor.frame(3);"
                "'done'").arg(QStringLiteral(JAHSHAKA_BAD_TEXTURE)) },
                         { "label", "break a texture" } }));
        CHECK(broke.value("ok").toBool(),
              "the script SUCCEEDS — the engine refuses, it does not throw");
        const QJsonArray errors = broke.value("engineErrors").toArray();
        CHECK(!errors.isEmpty(),
              "run_script reports the engine refusal recorded during that run");
        for (const QJsonValue &e : errors)
            printf("      engineError: %s (x%d)\n",
                   qPrintable(e.toObject().value("message").toString()),
                   e.toObject().value("count").toInt());

        // DRAIN-NEUTRAL: the echo is a diff of two const reports, so the pump's
        // own record still has everything. A reset here would make the failure
        // vanish for every other reader.
        const QJsonObject after = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script", "JSON.stringify(app.engineErrors())" } }));
        const QJsonObject pump = QJsonDocument::fromJson(
            after.value("result").toString().toUtf8()).object();
        CHECK(pump.value("recorded").toInt() > recordedBefore,
              "the pump's cumulative record grew (the echo did not steal it)");
        bool stillThere = false;
        for (const QJsonValue &e : pump.value("entries").toArray())
            for (const QJsonValue &mine : errors)
                if (e.toObject().value("message").toString()
                    == mine.toObject().value("message").toString())
                    stillThere = true;
        CHECK(stillThere, "app.engineErrors() still lists what run_script echoed");
        CHECK(!after.contains("engineErrors"),
              "and the NEXT run reports nothing — the echo is per-run, not cumulative");
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
