/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.material_drag — THE GESTURE ITSELF (MATERIAL-PREVIEW-1).
//
// scripting.e2e.material_preview drives the VERBS; this drives the DRAG. Every
// step here posts the real QDragEnter / QDragMove / QDragLeave / QDropEvent a
// person's drag out of an asset view posts, carrying the payload every asset
// view builds (ui/controls/assetdrag.h), straight at the editor viewport's own
// handlers — through `editor.dragAsset`, which exists so that this gesture is
// reachable by a script and an MCP client at all.
//
// WHAT IT PROVES, from EACH of the three material sources the owner named:
//
//   1. the live preview appears on the object UNDER THE CURSOR and follows the
//      cursor to the next one (the owner's "very very cool" behaviour, which
//      before this lane worked for exactly one of the three sources);
//   2. a drag that LEAVES the viewport restores the object exactly;
//   3. a DROP commits as EXACTLY ONE undo step, and undoing it restores the
//      node's TRUE original material — not the borrowed preview one, which is
//      the failure the "restore before the apply" dance in the old drop
//      handler existed to avoid and which nothing tested;
//   4. the drop applies to the node under the CURSOR, never to the selection.
//
// It is an app-driving suite (the MCP harness): the whole wiring — the drag
// handlers, the preview service, the resolver, the apply and the undo spine —
// has to be live for any of it to mean anything.
#include "../support/mcpharness.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QProcess>
#include <QThread>

using namespace shutdownharness;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

QString runValue(McpClient &mcp, const QString &expression)
{
    const QJsonObject reply = mcp.runScript(QStringLiteral("String(%1)").arg(expression));
    if (!reply.value("ok").toBool()) {
        std::printf("info: script failed: %s\n",
                    qUtf8Printable(reply.value("error").toString()));
        return QString();
    }
    return reply.value("result").toString();
}

bool runOk(McpClient &mcp, const QString &script)
{
    const QJsonObject reply = mcp.runScript(script);
    if (!reply.value("ok").toBool())
        std::printf("info: script failed: %s\n",
                    qUtf8Printable(reply.value("error").toString()));
    return reply.value("ok").toBool();
}

void shutdown(McpClient &mcp, QProcess &jahshaka)
{
    mcp.quit();
    jahshaka.waitForFinished(15000);
    if (jahshaka.state() != QProcess::NotRunning) jahshaka.kill();
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication qtApp(argc, argv);
    seedSettings(QStringLiteral(JAHSHAKA_BINARY));

    QProcess jahshaka;
    QString token;
    QByteArray log;
    const quint16 port = freePort();
    CHECK(spawn(jahshaka, port, &token, &log), "app booted and printed the MCP token");
    if (token.isEmpty()) return 1;

    McpClient mcp;
    mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    mcp.token = token;
    mcp.clientName = QStringLiteral("material-drag-test");
    mcp.initialize();

    // ---- a scene with two cubes far enough apart to hover one at a time ----
    if (!runOk(mcp, QStringLiteral(R"JS(
        project.create("Material Drag " + Date.now());
        cubeA = scene.addPrimitive("cube", {position: {x: -1.2, y: 1, z: 0}});
        cubeB = scene.addPrimitive("cube", {position: {x:  1.2, y: 1, z: 0}});
        editor.frame(2);
        true;
    )JS"))) {
        std::printf("FAIL: could not build the scene\n");
        return 1;
    }

    // Where each cube IS on screen, asked of the viewport rather than guessed:
    // editor.dropTargetAt is the same pick the drop takes.
    const QString probe = runValue(mcp, QStringLiteral(R"JS((function () {
        var found = {};
        for (var y = 200; y < 900 && (!found.a || !found.b); y += 20) {
            for (var x = 200; x < 1700; x += 20) {
                var t = editor.dropTargetAt(x, y);
                if (!t) continue;
                if (t.id === cubeA && !found.a) found.a = [x, y];
                if (t.id === cubeB && !found.b) found.b = [x, y];
            }
        }
        pa = found.a; pb = found.b;
        return JSON.stringify(found);
    })())JS"));
    std::printf("info: cube pixels %s\n", qUtf8Printable(probe));
    CHECK(probe.contains("\"a\"") && probe.contains("\"b\""),
          "both cubes are on screen and pickable at known pixels");
    if (!probe.contains("\"a\"") || !probe.contains("\"b\"")) {
        shutdown(mcp, jahshaka);
        return 1;
    }

    // ---- the three sources, as the three tiles a user would drag -----------
    const QString sources = runValue(mcp, QStringLiteral(R"JS((function () {
        var tex = assets.list({scope: "store", type: "texture"})[0];
        SOURCES = [
            ["a TRAY PRESET",            materials.presets()[0].guid],
            ["a MATERIAL made from an image", materials.createFromImage(tex.guid)],
            ["a MATERIALS-MODULE graph", materials.createFromImage(tex.guid, {graph: true})]
        ];
        return SOURCES.length;
    })())JS"));
    CHECK(sources == QStringLiteral("3"), "three material sources to drag");

    for (int i = 0; i < 3; ++i) {
        const QString what = runValue(mcp, QStringLiteral("SOURCES[%1][0]").arg(i));
        const QString setup = QStringLiteral(
            "SRC = SOURCES[%1][1];"
            "fp = function (id) { var m = material.get(id);"
            "  return [m.baseColor, m.roughness, m.metallic, m.baseColorMap || \"\"].join(\"|\"); };"
            "plainA = fp(cubeA);"
            "plainB = fp(cubeB); true;").arg(i);
        runOk(mcp, setup);

        // 1. HOVER A -> the preview appears on the object under the cursor.
        runOk(mcp, QStringLiteral(
            "editor.dragAsset(SRC, pa[0], pa[1]); editor.frame(1); true;"));
        const QString hoveredA = runValue(mcp, QStringLiteral(
            "fp(cubeA) !== plainA"));
        CHECK(hoveredA == QStringLiteral("true"),
              qUtf8Printable(QStringLiteral("%1: a real drag over A shows it live").arg(what)));

        // ...and FOLLOWS THE CURSOR to B, putting A back exactly.
        runOk(mcp, QStringLiteral(
            "editor.dragAsset(SRC, pb[0], pb[1]); editor.frame(1); true;"));
        const QString movedOn = runValue(mcp, QStringLiteral(
            "(fp(cubeA) === plainA) && "
            "(fp(cubeB) !== plainB)"));
        CHECK(movedOn == QStringLiteral("true"),
              qUtf8Printable(QStringLiteral("%1: the preview follows the cursor to B and restores A exactly")
                                 .arg(what)));

        // 2. LEAVING the viewport restores B exactly.
        runOk(mcp, QStringLiteral(
            "editor.dragAsset(SRC, pb[0], pb[1], {action: 'leave'});"
            "editor.frame(1); true;"));
        const QString left = runValue(mcp, QStringLiteral(
            "fp(cubeB) === plainB"));
        CHECK(left == QStringLiteral("true"),
              qUtf8Printable(QStringLiteral("%1: a drag that LEAVES restores B exactly").arg(what)));

        // 3 + 4. A DROP on A: one undo STEP, applied to the node under the
        // CURSOR (B is deliberately selected first — the old drop applied to
        // the SELECTION and the leaked preview made it look right anyway), and
        // the undo comes back to the TRUE original.
        //
        // ONE STEP IS ONE STACK ENTRY, NOT ONE PUSH. This used to
        // read `pushes`, which counts COMMANDS — and since phase 3 an apply
        // of a bundle the project does not hold yet pushes TWO of them: the
        // PIN (commands/pinassetcommand.h, so that undoing the apply takes
        // back the membership it created — the materials audit's F5) and the
        // material change, composed into ONE macro. What the user presses
        // Ctrl+Z on is the macro; that is the claim this line is making, and
        // `count` is the number that states it. `pushes` is asserted beside
        // it, because "one step made of exactly the commands we expect" is
        // stronger than either number alone.
        //
        // The number read is the stack's INDEX, not its count: each arm here
        // UNDOES its drop before the next one, so the next push truncates the
        // redo tail and `count` can come back unchanged. The index moves by
        // exactly one entry per macro either way.
        runOk(mcp, QStringLiteral(
            "editor.select(cubeB);"
            "pushes0 = editor.undoState().pushes;"
            "index0 = editor.undoState().index;"
            "held0 = assets.list({scope: 'project'}).filter("
            "    function (a) { return a.guid === SRC; }).length; true;"));
        runOk(mcp, QStringLiteral(
            "editor.dragAsset(SRC, pa[0], pa[1], {action: 'drop'});"
            "editor.frame(2); true;"));
        const QString dropped = runValue(mcp, QStringLiteral(
            "JSON.stringify({steps: editor.undoState().index - index0,"
            " commands: editor.undoState().pushes - pushes0,"
            " expected: held0 ? 1 : 2,"
            " a: fp(cubeA) !== plainA,"
            " b: fp(cubeB) === plainB})"));
        std::printf("info: %s drop -> %s\n", qUtf8Printable(what), qUtf8Printable(dropped));
        CHECK(dropped.contains("\"steps\":1"),
              qUtf8Printable(QStringLiteral("%1: the drop is EXACTLY ONE undo step").arg(what)));
        {
            const QJsonObject drop = QJsonDocument::fromJson(dropped.toUtf8()).object();
            CHECK(drop.value("commands").toInt() == drop.value("expected").toInt(),
                  qUtf8Printable(QStringLiteral(
                      "%1: ...made of the commands it should be — the material, plus the PIN "
                      "when the project did not hold the bundle yet (%2 of %3)")
                          .arg(what).arg(drop.value("commands").toInt())
                          .arg(drop.value("expected").toInt())));
        }
        CHECK(dropped.contains("\"a\":true"),
              qUtf8Printable(QStringLiteral("%1: the drop applied to the node under the CURSOR").arg(what)));
        CHECK(dropped.contains("\"b\":true"),
              qUtf8Printable(QStringLiteral("%1: ...and NOT to the selected node").arg(what)));

        runOk(mcp, QStringLiteral("editor.undo(); editor.frame(1); true;"));
        const QString undone = runValue(mcp, QStringLiteral(
            "fp(cubeA) === plainA"));
        CHECK(undone == QStringLiteral("true"),
              qUtf8Printable(QStringLiteral("%1: undo restores the node's TRUE original material")
                                 .arg(what)));
    }

    shutdown(mcp, jahshaka);
    if (failures) std::printf("\nRESULT: %d FAILURE(S)\n", failures);
    else std::printf("\nRESULT: all ok\n");
    return failures ? 1 : 0;
}
