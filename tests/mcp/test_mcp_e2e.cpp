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
//   - screenshot returns a decodable PNG at the requested size, and always
//     echoes the camera pose in a text block beside it
//   - screenshot camera/frameNode move the camera through the registry verbs:
//     the framed shot is different PIXELS from the default view, the red cube
//     is in the middle of it, and the echoed pose matches editor.camera()
//   - describe_scene is bounded by default (depth 2 + childCount/truncated),
//     escapable via depth/subtree, and enriched via include[]
//   - undo_redo reverts the last run_script call (describe_scene confirms)
//   - api_docs returns the registry reference (whole and per-module)
//   - F5: a scripted node.setProperty is UNDOABLE through undo_redo
//   - F6: run_script timeoutMs interrupts a runaway loop, the app survives,
//     and the NEXT request is served (the setInterrupted reset)
//   - F16: run_script's module list is generated from the live registry
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
    // The image content block is content[0] and stays there: the camera/pose
    // work below only APPENDS a text block, so a client that reads content[0]
    // and nothing else keeps working.
    auto shotImage = [&](const QJsonObject &result, QImage &into) {
        const QJsonArray content = result.value("content").toArray();
        if (content.isEmpty()) return false;
        const QJsonObject item = content.first().toObject();
        if (item.value("type").toString() != QLatin1String("image")) return false;
        return into.loadFromData(
            QByteArray::fromBase64(item.value("data").toString().toLatin1()), "PNG");
    };
    /// The pose echo: the text block beside the image, parsed back to JSON.
    auto shotPose = [&](const QJsonObject &result) {
        const QJsonArray content = result.value("content").toArray();
        for (const QJsonValue &v : content) {
            const QJsonObject item = v.toObject();
            if (item.value("type").toString() != QLatin1String("text")) continue;
            const QString text = item.value("text").toString();
            const int brace = text.indexOf('{');
            if (brace < 0) continue;
            return QJsonDocument::fromJson(text.mid(brace).toUtf8()).object();
        }
        return QJsonObject();
    };
    {
        const QJsonObject result = callTool(net, url, token, ++id, "screenshot",
                                            QJsonObject{ { "width", 320 }, { "height", 240 } });
        const QJsonArray content = result.value("content").toArray();
        CHECK(content.first().toObject().value("type").toString() == "image"
                  && content.first().toObject().value("mimeType").toString() == "image/png",
              "screenshot returns an MCP image content item first");
        QImage img;
        CHECK(shotImage(result, img), "the screenshot decodes as PNG");
        CHECK(img.width() == 320 && img.height() == 240, "the screenshot has the requested size");

        // The pose echo is unconditional: an image with no idea where it was
        // taken from is half an answer.
        const QJsonObject pose = shotPose(result);
        CHECK(pose.contains("position") && pose.contains("rotation"),
              "screenshot echoes the camera pose beside the image");
        CHECK(pose.value("width").toInt() == 320 && pose.value("height").toInt() == 240,
              "…and the size it actually rendered");
    }

    // ---- screenshot camera / frameNode (AI_SURFACE_PROGRAM_SPEC lane B #4a)
    // The tool is only the byte-carrying view of editor.setCamera /
    // editor.frameNode. What has to be true end-to-end: the camera really
    // moved (different PIXELS, not just a different number), and the pose the
    // tool echoed is the pose the editor is actually in.
    {
        // A cube far from the default view, so framing it cannot look like the
        // default frame by accident.
        const QJsonObject built = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script",
                           "var n = scene.addPrimitive('cube', {position:{x:40,y:0,z:-40}});"
                           // emissive so the assertion does not depend on where
                           // the default scene's lights happen to point
                           "material.set(n, {baseColor:'#ff0000', roughness:0.4,"
                           "                 emissiveColor:'#ff0000', emissiveIntensity:1.0});"
                           "editor.setCamera({position:{x:0,y:5,z:14}, lookAt:{x:0,y:0,z:0}}); n" },
                         { "label", "a distant red cube" } }));
        CHECK(built.value("ok").toBool(), "a red cube 40 units away, camera back at the default view");
        const QString cubeId = built.value("result").toString();

        const QJsonObject defaultShot = callTool(net, url, token, ++id, "screenshot",
            QJsonObject{ { "width", 200 }, { "height", 200 }, { "postFx", false } });
        QImage defaultImg;
        CHECK(shotImage(defaultShot, defaultImg), "the default-view shot decodes");
        const QJsonObject defaultPose = shotPose(defaultShot);

        const QJsonObject framedShot = callTool(net, url, token, ++id, "screenshot",
            QJsonObject{ { "width", 200 }, { "height", 200 }, { "postFx", false },
                         { "frameNode", QJsonObject{ { "id", cubeId },
                                                     { "yaw", 0 }, { "pitch", -15 },
                                                     { "distance", 6 } } } });
        QImage framedImg;
        CHECK(shotImage(framedShot, framedImg), "the framed shot decodes");
        CHECK(!framedImg.isNull() && !defaultImg.isNull() && framedImg != defaultImg,
              "the FRAMED screenshot is different pixels from the default view");
        // …and specifically: the red cube is in the middle of the framed one.
        // An 11x11 average, not the single centre pixel — a specular highlight
        // sits exactly on the centre pixel and reads pure white there.
        auto centreAverage = [](const QImage &img) {
            long r = 0, g = 0, b = 0, n = 0;
            for (int dy = -5; dy <= 5; ++dy) {
                for (int dx = -5; dx <= 5; ++dx) {
                    const QColor c = img.pixelColor(img.width() / 2 + dx, img.height() / 2 + dy);
                    r += c.red(); g += c.green(); b += c.blue(); ++n;
                }
            }
            return QColor(int(r / n), int(g / n), int(b / n));
        };
        const QColor middle = centreAverage(framedImg);
        const QColor wasMiddle = centreAverage(defaultImg);
        CHECK(middle.red() > middle.green() + 40 && middle.red() > middle.blue() + 40,
              QString("the red cube is in the centre of the framed shot (%1,%2,%3)")
                  .arg(middle.red()).arg(middle.green()).arg(middle.blue()).toUtf8().constData());
        CHECK(!(wasMiddle.red() > wasMiddle.green() + 40),
              QString("…and it was NOT in the centre of the default view (%1,%2,%3)")
                  .arg(wasMiddle.red()).arg(wasMiddle.green()).arg(wasMiddle.blue()).toUtf8().constData());

        const QJsonObject framedPose = shotPose(framedShot);
        CHECK(framedPose.contains("target") && framedPose.contains("distance"),
              "the frameNode pose echo says what it framed and from how far");
        CHECK(qAbs(framedPose.value("distance").toDouble() - 6.0) < 0.05,
              "…at the distance that was asked for");
        CHECK(framedPose.value("position").toObject() != defaultPose.value("position").toObject(),
              "…and the echoed pose is not the default one");

        // THE echo assertion: what the tool reported IS where the editor is.
        const QJsonObject live = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script", "editor.camera()" } }));
        const QJsonObject livePos = live.value("result").toObject().value("position").toObject();
        const QJsonObject echoPos = framedPose.value("position").toObject();
        CHECK(qAbs(livePos.value("x").toDouble() - echoPos.value("x").toDouble()) < 1e-3
                  && qAbs(livePos.value("y").toDouble() - echoPos.value("y").toDouble()) < 1e-3
                  && qAbs(livePos.value("z").toDouble() - echoPos.value("z").toDouble()) < 1e-3,
              "the echoed pose MATCHES what editor.camera() reports afterwards");

        // camera:{} takes the same route through editor.setCamera.
        const QJsonObject placedShot = callTool(net, url, token, ++id, "screenshot",
            QJsonObject{ { "width", 120 }, { "height", 120 },
                         { "camera", QJsonObject{
                               { "position", QJsonObject{ { "x", 40 }, { "y", 9 }, { "z", -30 } } },
                               { "lookAt", QJsonObject{ { "x", 40 }, { "y", 0 }, { "z", -40 } } },
                               { "fov", 55 } } } });
        const QJsonObject placedPose = shotPose(placedShot);
        CHECK(qAbs(placedPose.value("position").toObject().value("x").toDouble() - 40.0) < 1e-3
                  && qAbs(placedPose.value("fov").toDouble() - 55.0) < 1e-3,
              "screenshot camera:{position, lookAt, fov} placed the camera and echoed it");

        // Refusals travel as tool errors, with the verb's own message.
        const QJsonObject both = callTool(net, url, token, ++id, "screenshot",
            QJsonObject{ { "camera", QJsonObject{} },
                         { "frameNode", QJsonObject{ { "id", cubeId } } } });
        CHECK(both.value("isError").toBool(), "screenshot refuses camera AND frameNode together");
        const QJsonObject noId = callTool(net, url, token, ++id, "screenshot",
            QJsonObject{ { "frameNode", QJsonObject{ { "yaw", 10 } } } });
        CHECK(noId.value("isError").toBool(), "screenshot refuses frameNode with no id");
        const QJsonObject badKey = callTool(net, url, token, ++id, "screenshot",
            QJsonObject{ { "camera", QJsonObject{ { "postion", QJsonObject{} } } } });
        CHECK(badKey.value("isError").toBool()
                  && toolText(badKey).contains(QLatin1String("unknown key")),
              "a misspelled camera key comes back as the VERB's error, not a silent no-op");
        const QJsonObject player = callTool(net, url, token, ++id, "screenshot",
            QJsonObject{ { "view", "player" } });
        CHECK(player.value("isError").toBool(), "view:'player' is refused (editor view only)");
    }

    // ---- describe_scene include / subtree / depth (lane B #8) -------------
    {
        const QJsonObject built = toolJson(callTool(net, url, token, ++id, "run_script",
            QJsonObject{ { "script",
                           "var a = scene.addEmpty();"
                           "var b = scene.addPrimitive('cube', {parent:a});"
                           "var c = scene.addPrimitive('sphere', {parent:b});"
                           "var l = scene.addLight('spot', {position:{x:0,y:6,z:0}});"
                           "node.setProperty(l, 'intensity', 4.5);"
                           "material.set(b, {baseColor:'#00ff00'});"
                           "JSON.stringify({a:a, b:b, c:c, l:l})" },
                         { "label", "a 3-deep chain" } }));
        CHECK(built.value("ok").toBool(), "describe_scene fixture: a 3-deep chain plus a spot light");
        const QJsonObject ids =
            QJsonDocument::fromJson(built.value("result").toString().toUtf8()).object();

        auto rowFor = [](const QJsonObject &scene, const QString &nodeId) {
            for (const QJsonValue &v : scene.value("nodes").toArray())
                if (v.toObject().value("id").toString() == nodeId) return v.toObject();
            return QJsonObject();
        };

        // The DEFAULT is bounded, and says so.
        const QJsonObject shallow = toolJson(callTool(net, url, token, ++id, "describe_scene"));
        CHECK(shallow.value("depth").toInt() == 2, "describe_scene reports the depth it used (2)");
        CHECK(!rowFor(shallow, ids.value("c").toString()).contains("id"),
              "the default depth does NOT reach the 3rd level");
        const QJsonObject cutRow = rowFor(shallow, ids.value("b").toString());
        CHECK(cutRow.value("truncated").toBool() && cutRow.value("childCount").toInt() == 1,
              "…and the node whose children were cut off carries childCount + truncated");

        // …and it is escapable.
        const QJsonObject deep = toolJson(callTool(net, url, token, ++id, "describe_scene",
            QJsonObject{ { "depth", -1 } }));
        CHECK(rowFor(deep, ids.value("c").toString()).contains("id"),
              "depth -1 reaches the whole tree");
        const QJsonObject sub = toolJson(callTool(net, url, token, ++id, "describe_scene",
            QJsonObject{ { "subtree", ids.value("a").toString() }, { "depth", -1 } }));
        CHECK(sub.value("nodes").toArray().size() == 3
                  && rowFor(sub, ids.value("l").toString()).isEmpty(),
              "subtree describes exactly that branch");

        // include: each flag adds its block, and nothing it was not asked for.
        const QJsonObject plain = toolJson(callTool(net, url, token, ++id, "describe_scene",
            QJsonObject{ { "depth", -1 } }));
        CHECK(!rowFor(plain, ids.value("b").toString()).contains("material")
                  && !rowFor(plain, ids.value("l").toString()).contains("light")
                  && !rowFor(plain, ids.value("b").toString()).contains("visible")
                  && !plain.contains("world"),
              "describe_scene without include is the plain node rows (the cheap default)");

        const QJsonObject rich = toolJson(callTool(net, url, token, ++id, "describe_scene",
            QJsonObject{ { "depth", -1 },
                         { "include", QJsonArray{ "materials", "lights", "visibility", "world" } } }));
        const QJsonObject meshRow = rowFor(rich, ids.value("b").toString());
        CHECK(meshRow.value("material").toObject().value("baseColor").toString() == "#00ff00",
              "include materials: the mesh row carries its material summary");
        const QJsonObject lightRow = rowFor(rich, ids.value("l").toString());
        CHECK(lightRow.value("light").toObject().value("lightType").toString() == "spot"
                  && qAbs(lightRow.value("light").toObject().value("intensity").toDouble() - 4.5) < 1e-3,
              "include lights: the light row carries its parameters");
        CHECK(meshRow.value("visible").toBool() && meshRow.value("visibleInScene").toBool(),
              "include visibility: visible + visibleInScene");
        CHECK(rich.value("world").toObject().contains("ambient")
                  && rich.value("world").toObject().contains("fog"),
              "include world: the scene-level settings arrive beside the nodes");

        const QJsonObject unknown = callTool(net, url, token, ++id, "describe_scene",
            QJsonObject{ { "include", QJsonArray{ "textures" } } });
        CHECK(unknown.value("isError").toBool(), "an unknown include is refused, not ignored");
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
