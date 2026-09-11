/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.shot_aspect — AN OFFSCREEN SHOT DOES NOT TAKE THE WINDOW'S SHAPE
// (plan item 15 / platform audit C2b.2, lane L11).
//
// Two contracts, both about the camera's ASPECT and the window beside it:
//
//  1. editor.screenshot(w, h) is a function of the camera's POSE and the
//     SHOT's size — never of the window's. The same pose, the same selection
//     (so the gizmo is drawn and re-sized for the shot), photographed before
//     and after the window changes shape, is the SAME picture, pixel for
//     pixel. (The scripting.e2e.particles incident of 2026-09-10 was a pose
//     FRAMED for the window — F backs off until the subject fills the
//     viewport's rendered angle, which depends on the window above 16:9 —
//     and that stays window-relative by design: this suite fixes the pose.)
//
//  2. A piloted camera that CONSTRAINS its aspect keeps its AUTHORED aspect.
//     It used to lose it on the first frame: the gizmo's pixel frame and every
//     pick ray set the camera's aspect from the WIDGET (smoke S15 made that
//     per frame), so a 2.39 camera became the viewport's 1.87 — the on-screen
//     letterbox vanished, a save wrote the viewport's shape into the camera,
//     and a screenshot with a selection came out with no bars at all. The
//     picks now unproject through the letterbox's picture rectangle, and the
//     shot keeps the authored aspect.
//
// The window is re-shaped with app.resizeWindow, and everything is measured a
// request later: layout happens on the event loop, which a --script run holds
// (the ui.column_law harness, for the same reason).
#include "../shutdown/mcpharness.h"

#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QThread>

using namespace shutdownharness;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

QJsonObject readObject(McpClient &mcp, const QString &expression)
{
    const QJsonObject reply = mcp.runScript(QStringLiteral("JSON.stringify(%1)").arg(expression));
    if (!reply.value("ok").toBool()) {
        std::printf("info: script failed: %s\n",
                    qUtf8Printable(reply.value("error").toString()));
        return {};
    }
    return QJsonDocument::fromJson(reply.value("result").toString().toUtf8()).object();
}

void settle(McpClient &mcp)
{
    QThread::msleep(800);
    mcp.runScript(QStringLiteral("editor.frame(3)"));
}

/// Pixels that differ between two same-sized images (-1 = not comparable).
int differingPixels(const QImage &a, const QImage &b)
{
    if (a.isNull() || b.isNull() || a.size() != b.size()) return -1;
    const QImage x = a.convertToFormat(QImage::Format_RGBA8888);
    const QImage y = b.convertToFormat(QImage::Format_RGBA8888);
    int n = 0;
    for (int row = 0; row < x.height(); ++row) {
        const auto *p = reinterpret_cast<const quint32 *>(x.constScanLine(row));
        const auto *q = reinterpret_cast<const quint32 *>(y.constScanLine(row));
        for (int col = 0; col < x.width(); ++col) n += p[col] != q[col] ? 1 : 0;
    }
    return n;
}

/// The first and last rows, down the image's middle column, that are not the
/// letterbox's black.
void pictureRows(const QImage &img, int &top, int &bottom)
{
    top = bottom = -1;
    const int x = img.width() / 2;
    for (int y = 0; y < img.height(); ++y)
        if ((img.pixel(x, y) & 0x00FFFFFFu) != 0u) { if (top < 0) top = y; bottom = y; }
}

}   // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    seedSettings(QStringLiteral(JAHSHAKA_BINARY));
    const QString shots = QDir::current().absoluteFilePath(QStringLiteral("shots"));
    QDir().mkpath(shots);
    auto shotPath = [&](const char *name) {
        return QDir(shots).absoluteFilePath(QString::fromLatin1(name));
    };

    QProcess jahshaka;
    QString token;
    QByteArray log;
    const quint16 port = freePort();
    CHECK(spawn(jahshaka, port, &token, &log), "app booted and printed the MCP token");
    if (token.isEmpty()) return 1;

    McpClient mcp;
    mcp.url = QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port));
    mcp.token = token;
    mcp.initialize();

    // A deterministic frame: no GI (a probe field converges over frames, which
    // would be a difference this suite did not cause), a cube and the
    // default scene's lights, a FIXED pose, and the cube selected so the gizmo
    // is drawn — and re-sized for the shot — in both pictures.
    const QJsonObject built = readObject(mcp, QStringLiteral(
        "(function () {"
        "  project.create('ShotAspect');"
        "  world.rayon({enabled: false});"
        "  var cube = scene.addPrimitive('cube', {position: {x: 0, y: 0.5, z: 0}});"
        "  editor.select(cube);"
        "  editor.setCamera({position: {x: 3, y: 2.5, z: 6}, lookAt: {x: 0, y: 0.5, z: 0}});"
        "  return {cube: cube};"
        "})()"));
    const QString cube = built.value("cube").toString();
    CHECK(!cube.isEmpty(), "a project with a selected cube and a fixed camera");
    settle(mcp);

    // ---- 1. the same pose, two window shapes, one picture -------------------
    // Both shapes are SET, not inherited: the window's geometry is saved on
    // quit, so a second run of this suite would otherwise start where the
    // first one left it. 1900 wide puts the viewport well above the 16:9
    // framing hold, 1300 well below it — the two sides of the one policy that
    // makes the window's shape matter to a free camera at all.
    mcp.runScript(QStringLiteral("app.resizeWindow(1900, 1060)"));
    settle(mcp);
    const QJsonObject before = readObject(mcp, QStringLiteral("editor.viewportState()"));
    mcp.runScript(QStringLiteral("editor.screenshot('%1', 640, 480)").arg(shotPath("wide.png")));

    mcp.runScript(QStringLiteral("app.resizeWindow(1300, 1060)"));
    settle(mcp);
    const QJsonObject after = readObject(mcp, QStringLiteral("editor.viewportState()"));
    const double aspectBefore = before.value("height").toDouble() > 0
        ? before.value("width").toDouble() / before.value("height").toDouble() : 0.0;
    const double aspectAfter = after.value("height").toDouble() > 0
        ? after.value("width").toDouble() / after.value("height").toDouble() : 0.0;
    std::printf("info: viewport %dx%d (%.3f) -> %dx%d (%.3f)\n",
                before.value("width").toInt(), before.value("height").toInt(), aspectBefore,
                after.value("width").toInt(), after.value("height").toInt(), aspectAfter);
    CHECK((aspectBefore > 16.0 / 9.0) != (aspectAfter > 16.0 / 9.0),
          "the window really moved the viewport across the 16:9 hold (else this proves nothing)");
    mcp.runScript(QStringLiteral("editor.screenshot('%1', 640, 480)").arg(shotPath("narrow.png")));

    const int diff = differingPixels(QImage(shotPath("wide.png")), QImage(shotPath("narrow.png")));
    std::printf("info: %d pixels differ between the two shots\n", diff);
    CHECK(diff == 0, "the same pose photographs the same at either window shape (gizmo drawn)");

    // ---- 2. a piloted, CONSTRAINED camera keeps its authored aspect ---------
    const QJsonObject cam = readObject(mcp, QStringLiteral(
        "(function () {"
        "  var c = scene.addCamera({position: {x: 0, y: 1, z: 6}});"
        "  camera.settings(c, {aspectRatio: 2.39, constrainAspect: true});"
        "  editor.pilot(c);"
        "  editor.select('%1');"
        "  return {cam: c};"
        "})()").arg(cube));
    const QString camId = cam.value("cam").toString();
    CHECK(!camId.isEmpty(), "a 2.39 camera that constrains its aspect, piloted, cube selected");
    settle(mcp);   // frames: the gizmo's pixel frame is set on each
    // ...and the two pick paths a click takes, through their verbs: a pick ray
    // (the drop point is pickAt's) and the gizmo's pixel test. Both used to
    // set the camera's aspect from the widget; a letterboxed picture is not a
    // whole number of pixels tall, so even the picture rect had to go in
    // fractional or it would round 2.39 to 2.3887.
    mcp.runScript(QStringLiteral("editor.dropPointAt(200, 150); editor.gizmoHitTest(300, 200);"));
    const double authored = readObject(mcp, QStringLiteral("camera.settings('%1')").arg(camId))
                                .value("aspectRatio").toDouble();
    std::printf("info: piloted camera aspect after frames and picks: %.6f\n", authored);
    CHECK(qAbs(authored - 2.39) < 1e-5,
          "piloting and picking did not overwrite the camera's AUTHORED aspect");

    mcp.runScript(QStringLiteral("editor.screenshot('%1', 640, 480)").arg(shotPath("letterbox.png")));
    int top = -1, bottom = -1;
    pictureRows(QImage(shotPath("letterbox.png")), top, bottom);
    const int rows = (top >= 0) ? bottom - top + 1 : 0;
    std::printf("info: letterboxed picture rows %d..%d (%d rows; 640/2.39 = 267.8)\n",
                top, bottom, rows);
    CHECK(qAbs(rows - 268) <= 2,
          "the shot is letterboxed at the AUTHORED 2.39, selection and gizmo notwithstanding");

    mcp.runScript(QStringLiteral("app.quit()"));
    if (!jahshaka.waitForFinished(30000)) { jahshaka.kill(); jahshaka.waitForFinished(5000); }

    std::printf(failures == 0 ? "ui.shot_aspect: ALL PASS\n" : "ui.shot_aspect: %d FAILURES\n",
                failures);
    return failures == 0 ? 0 : 1;
}
