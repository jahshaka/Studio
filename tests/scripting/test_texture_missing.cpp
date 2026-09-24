/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// scripting.e2e.texture_missing — A MATERIAL'S TEXTURE FILE GONE FROM DISK IS
// A SCENE ISSUE (TEXTURE-FAIL-ISSUE-1, lane SMALL-FIXES-3).
//
// The document keeps a material's path when its file disappears; the mirror
// binds nothing and the surface renders untextured, and before this the only
// trace was one engine error string nobody read. The scene-issue scanner now
// raises `texture.missing:<node>` — one line per object, naming each missing
// slot and its file, with the action "re-import or re-link" — and clears it
// when the file comes back.
//
// Why a harness and not a --script suite: the condition is a file vanishing
// from disk UNDER a running editor, and no verb deletes a file (none should).
// So the app runs under MCP and this process does what the outside world does:
// removes the file, waits, puts it back. It reads the issue through BOTH
// doors: editor.checkScene (one scan per call — "within 2 scans") and the
// editor's own 1 Hz scanner (editor.issues after a wait, with no scan asked).

#include "../support/mcpharness.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>

using namespace shutdownharness;

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

/// The live `texture.missing` issues in a list of issues.
QJsonArray missingIn(const QJsonArray &issues)
{
    QJsonArray out;
    for (const QJsonValue &v : issues)
        if (v.toObject().value("kind").toString() == QLatin1String("texture.missing")) out.append(v);
    return out;
}

/// One scanner pass through the verb; the texture.missing issues after it.
QJsonArray scanOnce(McpClient &mcp)
{
    return missingIn(readJson(mcp, QStringLiteral("editor.checkScene().list")).toArray());
}

/// Scans until `want` texture.missing issues are live, at most `limit` passes.
/// Returns the number of passes it took (limit + 1 = never).
int scansUntil(McpClient &mcp, int want, int limit, QJsonArray *last)
{
    for (int pass = 1; pass <= limit; ++pass) {
        *last = scanOnce(mcp);
        if (last->size() == want) return pass;
    }
    return limit + 1;
}

bool writeImage(const QString &path, const QColor &colour)
{
    QImage img(32, 32, QImage::Format_RGB32);
    img.fill(colour);
    return img.save(path, "PNG");
}

}   // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    seedSettings(QStringLiteral(JAHSHAKA_BINARY));
    const QString dir = QDir::current().absoluteFilePath(QStringLiteral("textures"));
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    const QString base = QDir(dir).absoluteFilePath(QStringLiteral("brick_colour.png"));
    const QString normal = QDir(dir).absoluteFilePath(QStringLiteral("brick_normal.png"));
    CHECK(writeImage(base, QColor(180, 60, 40)) && writeImage(normal, QColor(128, 128, 255)),
          "two texture files on disk");

    QProcess jahshaka;
    QString token;
    QByteArray log;
    const quint16 port = freePort();
    CHECK(spawn(jahshaka, port, &token, &log), "app booted and printed the MCP token");
    if (token.isEmpty()) return 1;

    McpClient mcp;
    mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    mcp.token = token;
    mcp.clientName = QStringLiteral("texture-missing-test");
    mcp.initialize();

    // A textured cube (two slots bound to the files above, by path) and a
    // plain cube whose material binds no file at all.
    const QJsonObject built = readJson(mcp, QStringLiteral(
        "(function () {"
        "  project.create('TextureMissing');"
        "  var textured = scene.addPrimitive('cube', {position: {x: -1, y: 0.5, z: 0}});"
        "  node.rename(textured, 'Brick Cube');"
        "  material.set(textured, {baseColorMap: '%1', normalMap: '%2'});"
        "  var plain = scene.addPrimitive('cube', {position: {x: 1, y: 0.5, z: 0}});"
        "  editor.frame(2);"
        "  return {textured: textured, plain: plain,"
        "          bound: material.get(textured).baseColorMap};"
        "})()").arg(base, normal)).toObject();
    const QString textured = built.value("textured").toString();
    const QString plain = built.value("plain").toString();
    std::printf("info: the material holds %s\n", qUtf8Printable(built.value("bound").toString()));
    CHECK(!textured.isEmpty() && !plain.isEmpty(), "a textured cube and a plain one");
    CHECK(built.value("bound").toString() == base,
          "the material holds the file's own path (what the mirror loads)");

    // ---- 1. every file present: no issue --------------------------------------
    QJsonArray live = scanOnce(mcp);
    CHECK(live.isEmpty(), "every texture file present: no texture.missing issue");

    // ---- 2. a file deleted on disk: ONE issue, on the textured cube -------------
    CHECK(QFile::remove(base), "delete the base colour file from disk");
    const int passes = scansUntil(mcp, 1, 2, &live);
    std::printf("info: raised after %d scan(s)\n", passes);
    CHECK(passes <= 2, "texture.missing is raised within 2 scans");
    const QJsonObject issue = live.isEmpty() ? QJsonObject() : live.at(0).toObject();
    CHECK(issue.value("node").toString() == textured, "...naming the textured cube");
    CHECK(issue.value("id").toString() == QStringLiteral("texture.missing:") + textured,
          "...with the id texture.missing:<node>");
    std::printf("info: message: %s\ninfo: action:  %s\n",
                qUtf8Printable(issue.value("message").toString()),
                qUtf8Printable(issue.value("action").toString()));
    CHECK(issue.value("message").toString().contains(QLatin1String("brick_colour.png")) &&
          issue.value("message").toString().contains(QLatin1String("Base Color")),
          "...the message names the slot and the file");
    CHECK(!issue.value("message").toString().contains(QLatin1String("brick_normal.png")),
          "...and only the file that is missing");
    const QString action = issue.value("action").toString().toLower();
    CHECK(action.contains(QLatin1String("re-import")) && action.contains(QLatin1String("re-link")),
          "...and the action says re-import or re-link");
    live = scanOnce(mcp);
    CHECK(live.size() == 1, "a second scan raises nothing new (never repeats)");

    // ---- 3. a second slot missing on the SAME node: still one line ------------
    CHECK(QFile::remove(normal), "delete the normal map too");
    live = scanOnce(mcp);
    CHECK(live.size() == 1, "two missing slots on one object are ONE issue, not one per slot");

    // ---- 4. the editor's own 1 Hz scanner, no scan asked ------------------------
    CHECK(writeImage(base, QColor(180, 60, 40)) && writeImage(normal, QColor(128, 128, 255)),
          "put both files back");
    QThread::msleep(2500);   // two ticks of the editor's scanner, and a margin
    live = missingIn(readJson(mcp, QStringLiteral("editor.issues()")).toArray());
    CHECK(live.isEmpty(), "restoring the files clears it — by the editor's own scanner");
    CHECK(QFile::remove(base), "delete the base colour file again");
    QThread::msleep(2500);
    live = missingIn(readJson(mcp, QStringLiteral("editor.issues()")).toArray());
    CHECK(live.size() == 1 && live.at(0).toObject().value("node").toString() == textured,
          "...and the editor's scanner raises it again when the file goes");

    // ---- 5. restore: cleared within 2 scans ---------------------------------------
    CHECK(writeImage(base, QColor(180, 60, 40)), "restore the file");
    const int clearPasses = scansUntil(mcp, 0, 2, &live);
    std::printf("info: cleared after %d scan(s)\n", clearPasses);
    CHECK(clearPasses <= 2, "restoring the file clears the issue within 2 scans");

    // ---- 6. the plain cube never raised anything ------------------------------------
    bool plainEver = false;
    for (const QJsonValue &v : readJson(mcp, QStringLiteral("editor.issues()")).toArray())
        if (v.toObject().value("node").toString() == plain) plainEver = true;
    CHECK(!plainEver, "a material with no texture files raises nothing");

    mcp.quit();
    if (!jahshaka.waitForFinished(30000)) { jahshaka.kill(); jahshaka.waitForFinished(5000); }
    QDir(dir).removeRecursively();

    std::printf(failures == 0 ? "scripting.e2e.texture_missing: ALL PASS\n"
                              : "scripting.e2e.texture_missing: %d FAILURES\n",
                failures);
    return failures == 0 ? 0 : 1;
}
