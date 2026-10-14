/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// scripting.e2e.arriving_line — A WORLD STREAMS IN ON THE DRIVER'S FRAMES, AND
// THE ARRIVING LINE ENDS WHEN IT HAS ARRIVED (BISTRO-ENGINE-1 stage 5,
// findings 9 and 11).
//
// The owner's "Loading Bistro - shaders 10" stayed on screen, unchanged: the
// line lived for kStreamFramesAfterReveal DRIVER frames whenever the last frame
// had compiled anything, and at 4 fps that is a minute; and "shaders 10" was a
// count, never a queue. The open itself blocked 2.2 s in SceneMirror::toMeshData
// on one frame, building every mesh at once.
//
// THE LAW, IN FRAMES (the driver's own, read as `framesPresented`): an open of
// a world of many distinct meshes (a fixture OBJ this test writes: 120 parts of
// 1,681 vertices) streams them over several frames by the mirror's cost budget
// — the mirror's deferral count moves across the open — and the line is gone within kArrivalSettleSamples + 1 = 3 driver
// frames of the reveal or of the last frame that owed anything (a mesh, a
// texture, a lighting stage), whatever the frame time. The
// shader term, when shown, says what it is ("compiled"). A harness, not a
// --script suite: a script's own frames are never driver frames, and the line
// is the driver's alone.

#include "../support/mcpharness.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

using namespace mcpharness;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

QJsonValue readJson(McpClient &mcp, const QString &expression)
{
    const QJsonObject reply = mcp.runScript(QStringLiteral("JSON.stringify(%1)").arg(expression));
    if (!reply.value("ok").toBool()) {
        std::printf("info: script failed: %s\n", qUtf8Printable(reply.value("error").toString()));
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(
        QStringLiteral("[%1]").arg(reply.value("result").toString()).toUtf8());
    return doc.array().isEmpty() ? QJsonValue() : doc.array().at(0);
}

/// `parts` separate objects, each a 40 x 40 grid patch — distinct meshes, so none
/// is shared and every one is a build of its own.
bool writeParts(const QString &path, int parts)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QTextStream out(&f);
    const int n = 40;   // 1,681 vertices a part: 120 parts are ~200k vertices, several frames of the budget
    int base = 1;
    for (int k = 0; k < parts; ++k) {
        const double ox = (k % 12) * 1.2, oz = (k / 12) * 1.2;
        out << "o part_" << k << "\n";
        for (int j = 0; j <= n; ++j)
            for (int i = 0; i <= n; ++i)
                out << "v " << ox + double(i) / n << " " << 0.02 * ((i + j + k) % 3) << " "
                    << oz + double(j) / n << "\n";
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                const int a = base + j * (n + 1) + i, b = a + 1, c = a + (n + 1), d = c + 1;
                out << "f " << a << " " << c << " " << b << "\nf " << b << " " << c << " " << d << "\n";
            }
        base += (n + 1) * (n + 1);
    }
    return true;
}

}   // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    seedSettings(QStringLiteral(JAHSHAKA_BINARY));
    const QString obj = QDir::current().absoluteFilePath(QStringLiteral("arriving_parts.obj"));
    CHECK(writeParts(obj, 120), "the fixture: 120 distinct parts in one OBJ");

    QProcess jahshaka;
    QString token;
    QByteArray log;
    const quint16 port = freePort();
    CHECK(spawn(jahshaka, port, &token, &log), "app booted and printed the MCP token");
    if (token.isEmpty()) return 1;
    McpClient mcp;
    mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    mcp.token = token;
    mcp.attach(jahshaka, log);
    mcp.clientName = QStringLiteral("arriving-line-test");
    mcp.initialize();

    // THE WORLD, built with a script's frames (unbudgeted: the build is not
    // what is measured) and saved; then another world, so the open is a real one.
    const QJsonValue made = readJson(mcp, QStringLiteral(
        "(function () {"
        "  var g = project.create('Arriving ' + Date.now(), {template: 'basic'});"
        "  editor.loadingCover(false);"
        "  var a = assets.importFile('%1');"
        "  assets.addToProject(a);"
        "  assets.addToScene(a, {position: {x: 0, y: 0, z: 0}});"
        "  editor.frame(3);"
        "  project.save();"
        "  project.create('Elsewhere ' + Date.now(), {template: 'empty'});"
        "  editor.frame(3);"
        "  return {guid: g, nodes: scene.nodes().length};"
        "})()").arg(obj));
    const QString guid = made.toObject().value("guid").toString();
    CHECK(!guid.isEmpty(), "the world of 120 parts was made and saved");

    // THE OPEN, watched frame by frame from outside. The deferral COUNT is read
    // across it: a stream between two polls cannot be missed.
    const double deferredBefore = readJson(mcp, QStringLiteral("editor.mirrorStats().attachesDeferred")).toDouble();
    mcp.runScript(QStringLiteral("project.openAsync('%1')").arg(guid));
    struct Sample { double frame; bool idle; bool owed; double meshes; QString line; };
    std::vector<Sample> samples;
    for (int i = 0; i < 6000; ++i) {
        const QJsonObject s = readJson(mcp, QStringLiteral(
            "(function () { var v = editor.viewportState(); return {f: v.framesPresented,"
            " st: project.openState(), c: v.cover, ind: v.indicator, p: v.pending}; })()"))
                                  .toObject();
        const QJsonObject p = s.value("p").toObject();
        Sample x;
        x.frame = s.value("f").toDouble();
        x.idle = s.value("st").toString() == QLatin1String("idle") &&
                 s.value("c").toString() == QLatin1String("none");
        x.meshes = p.value("meshes").toDouble();
        x.owed = x.meshes > 0 || p.value("textures").toDouble() > 0 || p.value("gi").toDouble() > 0;
        x.line = s.value("ind").toString();
        samples.push_back(x);
        if (x.idle && !x.owed && x.line.isEmpty() && samples.size() > 3) break;
    }
    // The stream, and the line's end: the last frame that owed anything (a
    // mesh, a texture, a lighting stage) or the reveal, whichever is later —
    // the settle starts counting at the reveal.
    bool shaderTermHonest = true;
    double settleFrom = -1, gone = -1;
    for (const Sample &x : samples) {
        if (x.line.contains(QLatin1String("shader")) && !x.line.contains(QLatin1String("compiled")))
            shaderTermHonest = false;
        // AFTER THE REVEAL only: framesPresented restarts at the bind, and the
        // settle counts from the reveal (or a later owed frame) anyway.
        if (x.idle && (settleFrom < 0 || x.owed)) { settleFrom = x.frame; gone = -1; }
        if (x.idle && !x.owed && x.line.isEmpty() && gone < 0) gone = x.frame;
    }
    const double deferred =
        readJson(mcp, QStringLiteral("editor.mirrorStats().attachesDeferred")).toDouble() - deferredBefore;
    std::printf("info: %zu samples; %.0f deferral(s) across the open; the settle counts from frame %.0f, "
                "line gone at frame %.0f\n", samples.size(), deferred, settleFrom, gone);
    if (gone < 0 || settleFrom < 0 || gone - settleFrom > 3)
        for (const Sample &x : samples)
            std::printf("info:   frame %.0f idle %d owed %d meshes %.0f line '%s'\n", x.frame, int(x.idle),
                        int(x.owed), x.meshes, qUtf8Printable(x.line));
    CHECK(deferred >= 120 - 40, "the open's meshes streamed over several frames: ~200k vertices at 65,536 a "
                                "frame defer most of the 120 parts at least once");
    CHECK(shaderTermHonest, "a shader term, when shown, is labelled as a count ('compiled')");
    CHECK(gone >= 0, "the line ended");
    CHECK(gone >= 0 && settleFrom >= 0 && gone - settleFrom <= 3,
          "...within 3 driver frames of the reveal or the last frame that owed anything");

    mcp.quit();
    if (!jahshaka.waitForFinished(30000)) { jahshaka.kill(); jahshaka.waitForFinished(5000); }
    std::printf(failures == 0 ? "scripting.e2e.arriving_line: ALL PASS\n"
                              : "scripting.e2e.arriving_line: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
