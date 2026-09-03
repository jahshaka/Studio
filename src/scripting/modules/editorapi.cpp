/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/editorapi.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QUndoStack>

#include "scripting/modules/moduleshared.h"
#include "viewport/ieditorviewport.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "viewport/snapsettings.h"
#include "shell/mainwindow.h"
#include "services/services.h"
#include "services/playbackservice.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "services/undoservice.h"

using namespace scriptmod;

QVector<VerbInfo> EditorApi::verbs() const
{
    return {
        { "select", "editor.select(id | null) -> bool",
          "Selects a node everywhere (viewport, hierarchy, properties); null or no argument deselects.",
          Needs::Document },
        { "selection", "editor.selection() -> id | null",
          "The selected node's id, or null.",
          Needs::Document },
        { "gizmoMode", "editor.gizmoMode() -> \"translate\" | \"rotate\" | \"scale\"",
          "The active transform gizmo mode (W/E/R in the viewport; Space cycles).",
          Needs::Engine },
        { "setGizmoMode", "editor.setGizmoMode(\"translate\"|\"rotate\"|\"scale\") -> bool",
          "Switches the transform gizmo, exactly like the W/E/R keys and the toolbar buttons.",
          Needs::Engine },
        { "focusSelection", "editor.focusSelection() -> bool",
          "Frames the selected node in the editor camera (the F key): bounds-aware distance, current view direction kept.",
          Needs::Engine },
        { "gameView", "editor.gameView(enabled) -> bool",
          "Game View (the G key): hides every in-viewport editor helper — grid, light wires, selection outline, gizmo. Docks stay; not persisted.",
          Needs::Engine },
        { "isGameView", "editor.isGameView() -> bool",
          "Whether Game View is active.",
          Needs::Engine },
        { "setView", "editor.setView(\"top\"|\"bottom\"|\"left\"|\"right\"|\"front\"|\"back\"|\"perspective\") -> bool",
          "Snaps the editor camera to a canonical view (the toolbar Views dropdown / X, Y, Z keys). Each view remembers its camera between visits: \"perspective\" returns to its remembered free/orbit pose, each ortho view to its own pan and zoom (a first visit gets the standard axis framing). Session-only memory; works in both camera modes.",
          Needs::Engine },
        { "view", "editor.view() -> string",
          "The last canonical view requested via editor.setView (\"perspective\" until one is set). Informational — free orbiting afterwards does not reset it.",
          Needs::Engine },
        { "camera", "editor.camera() -> {position:{x,y,z}, rotation:{x,y,z,scalar}, projection:\"perspective\"|\"orthogonal\", orthoSize}",
          "The editor camera's current pose: local position, local rotation quaternion, projection mode and ortho zoom. Read-only — the pixel-free way to assert camera moves (focus, view switches).",
          Needs::Engine },
        { "cameraMode", "editor.cameraMode() -> \"free\" | \"orbit\"",
          "The active camera controller: \"free\" (fly camera) or \"orbit\" (arcball).",
          Needs::Engine },
        { "setCameraMode", "editor.setCameraMode(\"free\"|\"orbit\") -> bool",
          "Switches the camera controller, like the toolbar's Free Camera / Arc Ball buttons. (The toolbar buttons do not yet reflect a script-driven switch.)",
          Needs::Engine },
        { "snapSize", "editor.snapSize() -> number",
          "The translate snap size (world units) — also the ground grid's spacing. Editor-global, persisted.",
          Needs::Document },
        { "setSnapSize", "editor.setSnapSize(size) -> bool",
          "Sets the translate snap / grid size ([ and ] step it in the viewport). Refuses size <= 0; clamped to 0.01..100.",
          Needs::Document },
        { "snapToFloor", "editor.snapToFloor() -> bool",
          "Drops the selection straight down onto the first scene surface below its bounds (the End key); y=0 plane when nothing is hit. Undoable.",
          Needs::Engine },
        { "undo", "editor.undo() -> bool",
          "Undoes the last completed undo step. Inside a script the run's own macro is still open, so this reaches the step before the script.",
          Needs::Document },
        { "redo", "editor.redo() -> bool",
          "Redoes the last undone step.",
          Needs::Document },
        { "play", "editor.play() -> bool",
          "Enters play mode (PlayBack drives physics, animations and controllers in place).",
          Needs::Document },
        { "stop", "editor.stop() -> bool",
          "Leaves play mode back to editing.",
          Needs::Document },
        { "simulate", "editor.simulate(enabled=true) -> bool",
          "Starts/stops the in-place physics simulation without entering play mode.",
          Needs::Document },
        { "frame", "editor.frame(n=1, dt=-1) -> bool",
          "Renders exactly n frames synchronously (document->engine sync + renderOneFrame) — the deterministic stepping the test suites use. With `dt` >= 0 the document's clock AND the engine's particle simulation advance by exactly that many seconds per frame instead of by the wall clock, which is what makes stepping deterministic in play mode and for particles alike (without it, each stepped frame charged the document for however long the previous statement took, and a scripted frame bought a millisecond of fire).",
          Needs::Engine },
        { "viewportState", "editor.viewportState() -> {state, framesPresented}",
          "What the editor viewport is showing right now. `state` is \"presenting\" (the engine's own frames are on screen), \"loading\" (a world is bound but no frame of it has presented yet — the viewport wears its loading cover), \"noscene\" (no world open, the cover says so) or \"offscreen\" (this session's viewport never reaches a window: headless stand-ins and the macOS offscreen fallback). `framesPresented` counts frames actually drawn AND presented since the current world was bound, so a script can wait for real pixels instead of sleeping.",
          Needs::Document },
        { "screenshot", "editor.screenshot(path, w=256, h=256, probes=[], postFx=false) -> {path, width, height, center:{r,g,b}, probes:[{x,y,r,g,b}]}",
          "Offscreen render of the editor scene to a PNG; returns the centre pixel, plus the pixel at each probe point ({x,y} in normalized 0..1 image coordinates), so scripts can assert on colours. Headless-safe. `postFx` true renders it through the scene's post-processing chain (HDR/tonemap, bloom, ambient occlusion, SMAA) so the shot matches what the viewport shows; false (the default) is the neutral, exactly-reproducible readback that pixel assertions want.",
          Needs::Engine },
        { "beginBatch", "editor.beginBatch() -> bool",
          "Opens a nested undo macro inside the script's run (finer-grained grouping).",
          Needs::Document },
        { "endBatch", "editor.endBatch() -> bool",
          "Closes the macro opened by editor.beginBatch().",
          Needs::Document },
        { "importAssets", "editor.importAssets([paths]) -> bool",
          "Starts the interactive THREADED import of the given files — the same ImportBatchRunner + progress dialog the project panel's Import button and drops use — and returns once the batch has started (it does not wait). assets.importFile is the synchronous, dialog-free verb.",
          Needs::Window },
    };
}

bool EditorApi::select(const QVariant &id)
{
    if (!host.services || !host.services->selection || !host.services->sceneEdit)
        return fail("editor: not available in this session");
    auto scene = host.services->sceneEdit->scene();
    if (!scene) return fail("editor.select: no scene is open");

    const QString guid = id.toString();
    if (guid.isEmpty()) {
        host.services->selection->select(iris::SceneNodePtr());
        return true;
    }
    auto node = findNodeByGuid(scene->getRootNode(), guid);
    if (!node) return fail(QStringLiteral("editor.select: no node with id '%1'").arg(guid));
    host.services->selection->select(node);
    return true;
}

QVariant EditorApi::selection()
{
    if (!host.services || !host.services->selection) return QVariant();
    auto node = host.services->selection->selected();
    return node ? QVariant(node->getGUID()) : QVariant();
}

QString EditorApi::gizmoMode()
{
    if (!requireEngine()) return QString();
    return host.viewport->gizmoMode();
}

bool EditorApi::setGizmoMode(const QString &mode)
{
    if (!requireEngine()) return false;
    // Through MainWindow's slots when the shell exists so the toolbar's
    // checked state follows (the same path the W/E/R keys take); straight to
    // the viewport otherwise.
    if (mode == "translate") {
        if (host.mainWindow) QMetaObject::invokeMethod(host.mainWindow, "translateGizmo");
        else host.viewport->setGizmoLoc();
    } else if (mode == "rotate") {
        if (host.mainWindow) QMetaObject::invokeMethod(host.mainWindow, "rotateGizmo");
        else host.viewport->setGizmoRot();
    } else if (mode == "scale") {
        if (host.mainWindow) QMetaObject::invokeMethod(host.mainWindow, "scaleGizmo");
        else host.viewport->setGizmoScale();
    } else {
        return fail(QStringLiteral("editor.setGizmoMode: unknown mode '%1' (translate|rotate|scale)").arg(mode));
    }
    return true;
}

bool EditorApi::focusSelection()
{
    if (!requireEngine()) return false;
    if (!host.services || !host.services->selection || !host.services->selection->selected())
        return fail("editor.focusSelection: nothing is selected");
    host.viewport->focusOnSelection();
    return true;
}

bool EditorApi::gameView(bool enabled)
{
    if (!requireEngine()) return false;
    host.viewport->setGameView(enabled);
    return true;
}

bool EditorApi::isGameView()
{
    if (!requireEngine()) return false;
    return host.viewport->isGameView();
}

bool EditorApi::setView(const QString &view)
{
    if (!requireEngine()) return false;
    // Go through MainWindow when one exists so the projection icon and the
    // Views dropdown checks stay in sync; the viewport alone otherwise
    // (headless --script runs).
    const bool ok = host.mainWindow ? host.mainWindow->applyCameraView(view)
                                    : host.viewport->setCameraView(view);
    if (!ok)
        return fail("editor.setView: unknown view — use top/bottom/left/right/front/back/perspective");
    return true;
}

QString EditorApi::view()
{
    if (!requireEngine()) return QString();
    return host.viewport->cameraView();
}

QVariantMap EditorApi::camera()
{
    QVariantMap out;
    if (!requireEngine()) return out;
    auto cam = host.viewport->editorCamera();
    if (!cam) { fail("editor.camera: no editor camera"); return out; }
    const QVector3D pos = cam->getLocalPos();
    const QQuaternion rot = cam->getLocalRot();
    out["position"] = QVariantMap{ { "x", pos.x() }, { "y", pos.y() }, { "z", pos.z() } };
    out["rotation"] = QVariantMap{ { "x", rot.x() }, { "y", rot.y() }, { "z", rot.z() },
                                   { "scalar", rot.scalar() } };
    out["projection"] = cam->projMode == iris::CameraProjection::Perspective
                            ? QStringLiteral("perspective") : QStringLiteral("orthogonal");
    out["orthoSize"] = cam->orthoSize;
    return out;
}

QString EditorApi::cameraMode()
{
    if (!requireEngine()) return QString();
    return host.viewport->cameraMode();
}

bool EditorApi::setCameraMode(const QString &mode)
{
    if (!requireEngine()) return false;
    // Through MainWindow's slots when the shell exists (the toolbar buttons'
    // path); straight to the viewport otherwise.
    if (mode == QLatin1String("free")) {
        if (host.mainWindow) QMetaObject::invokeMethod(host.mainWindow, "useFreeCamera");
        else host.viewport->setFreeCameraMode();
    } else if (mode == QLatin1String("orbit")) {
        if (host.mainWindow) QMetaObject::invokeMethod(host.mainWindow, "useArcballCam");
        else host.viewport->setArcBallCameraMode();
    } else {
        return fail(QStringLiteral("editor.setCameraMode: unknown mode '%1' (free|orbit)").arg(mode));
    }
    return true;
}

double EditorApi::snapSize()
{
    return double(SnapSettings::translateSize());
}

bool EditorApi::setSnapSize(double size)
{
    if (size <= 0.0)
        return fail("editor.setSnapSize: size must be > 0");
    SnapSettings::setTranslateSize(float(size));
    return true;
}

bool EditorApi::snapToFloor()
{
    if (!requireEngine()) return false;
    if (!host.services || !host.services->selection || !host.services->selection->selected())
        return fail("editor.snapToFloor: nothing is selected");
    return host.viewport->snapSelectionToFloor();
}

bool EditorApi::undo()
{
    if (!host.services || !host.services->undo) return fail("editor: not available in this session");
    host.services->undo->undo();
    return true;
}

bool EditorApi::redo()
{
    if (!host.services || !host.services->undo) return fail("editor: not available in this session");
    host.services->undo->redo();
    return true;
}

bool EditorApi::play()
{
    if (!host.services || !host.services->playback || !host.viewport)
        return fail("editor: not available in this session");
    if (host.services->playback->isPlaying()) return true;
    host.services->playback->enterPlayMode();
    host.viewport->startPlayingScene();
    return true;
}

bool EditorApi::stop()
{
    if (!host.services || !host.services->playback || !host.viewport)
        return fail("editor: not available in this session");
    if (!host.services->playback->isPlaying()) return true;
    host.services->playback->enterEditMode();
    host.viewport->stopPlayingScene();
    return true;
}

bool EditorApi::simulate(bool enabled)
{
    if (!host.services || !host.services->playback || !host.viewport)
        return fail("editor: not available in this session");
    if (enabled) host.services->playback->startSimulation();
    else host.services->playback->stopSimulation();
    return true;
}

bool EditorApi::frame(int n, double dt)
{
    if (!requireEngine()) return false;
    host.viewport->renderFrames(qBound(1, n, 1000), float(dt));
    return true;
}

QVariantMap EditorApi::viewportState()
{
    QVariantMap out;
    if (!host.viewport) {
        out.insert("state", QStringLiteral("offscreen"));
        out.insert("framesPresented", 0);
        return out;
    }
    out.insert("state", host.viewport->presentationState());
    out.insert("framesPresented", QVariant::fromValue(host.viewport->framesPresented()));
    return out;
}

QVariantMap EditorApi::screenshot(const QString &path, int width, int height,
                                  const QVariantList &probes, bool postFx)
{
    QVariantMap out;
    if (!requireEngine()) return out;
    if (path.isEmpty()) { fail("editor.screenshot: a file path is required"); return out; }

    const QImage img = host.viewport->takeScreenshot(qBound(16, width, 4096),
                                                     qBound(16, height, 4096), postFx);
    if (img.isNull()) { fail("editor.screenshot: the viewport returned no image"); return out; }

    QFileInfo info(path);
    if (!info.dir().exists()) info.dir().mkpath(".");
    if (!img.save(path, "PNG")) {
        fail(QStringLiteral("editor.screenshot: could not save '%1'").arg(path));
        return out;
    }

    const QColor center = img.pixelColor(img.width() / 2, img.height() / 2);
    out["path"] = info.absoluteFilePath();
    out["width"] = img.width();
    out["height"] = img.height();
    out["center"] = QVariantMap{ { "r", center.red() }, { "g", center.green() }, { "b", center.blue() } };

    // Optional probe points in normalized 0..1 image coordinates: the pixel
    // gate for the shipped samples (scripting.e2e.samples) asserts material
    // fidelity through these — the gold dragon must be gold, not fallback grey.
    // Each probe returns the average of the 5x5 pixel block around the point
    // so the assertions are stable across drivers and minor framing drift.
    QVariantList probeResults;
    for (const QVariant &p : probes) {
        const QVariantMap pm = p.toMap();
        const double px = qBound(0.0, pm.value("x").toDouble(), 1.0);
        const double py = qBound(0.0, pm.value("y").toDouble(), 1.0);
        const int ix = qMin(int(px * img.width()), img.width() - 1);
        const int iy = qMin(int(py * img.height()), img.height() - 1);
        int r = 0, g = 0, b = 0, n = 0;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                const int x = ix + dx, y = iy + dy;
                if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) continue;
                const QColor c = img.pixelColor(x, y);
                r += c.red(); g += c.green(); b += c.blue(); ++n;
            }
        }
        if (n > 0) { r /= n; g /= n; b /= n; }
        probeResults.append(QVariantMap{ { "x", px }, { "y", py },
                                         { "r", r }, { "g", g }, { "b", b } });
    }
    if (!probeResults.isEmpty()) out["probes"] = probeResults;
    return out;
}

bool EditorApi::beginBatch()
{
    if (!host.undoStack) return fail("editor.beginBatch: no undo stack in this session");
    host.undoStack->beginMacro(QStringLiteral("script batch"));
    ++mBatchDepth;
    return true;
}

bool EditorApi::endBatch()
{
    if (!host.undoStack) return fail("editor.endBatch: no undo stack in this session");
    if (mBatchDepth <= 0) return fail("editor.endBatch: no batch is open");
    host.undoStack->endMacro();
    --mBatchDepth;
    return true;
}

bool EditorApi::importAssets(const QVariant &paths)
{
    if (!requireProject()) return false;
    if (!host.mainWindow) return fail("editor.importAssets: no window in this session");

    QStringList files;
    const QVariant normalized = scriptmod::normalizeJs(paths);
    if (normalized.typeId() == QMetaType::QVariantList) {
        for (const QVariant &v : normalized.toList()) files.append(v.toString());
    } else if (!normalized.toString().isEmpty()) {
        files.append(normalized.toString());
    }
    if (files.isEmpty()) return fail("editor.importAssets: no files given");
    for (const QString &file : files) {
        if (!QFileInfo::exists(file))
            return fail(QStringLiteral("editor.importAssets: no such file: %1").arg(file));
    }
    if (!host.mainWindow->startInteractiveImport(files))
        return fail("editor.importAssets: an interactive import is already running");
    return true;
}
