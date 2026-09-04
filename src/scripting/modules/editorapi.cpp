/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "scripting/modules/editorapi.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSize>
#include <QUndoStack>
#include <QElapsedTimer>

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
#include "bridge/enginehost.h"

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
        { "overlays", "editor.overlays() -> {grid, lightWires, selectionWireframe, gameView}",
          "The viewport's editor helpers, as they are right now: `grid` the ground grid, "
          "`lightWires` the light icons and their range wires, `selectionWireframe` the selection "
          "highlight style (true = polygon wireframe, false = silhouette outline), `gameView` the "
          "master switch that hides all of them at once. There is deliberately no `fps` row: "
          "nothing in the engine viewport draws an FPS counter (setShowFps is an empty override), "
          "so reporting one would be a number that is never true.",
          Needs::Engine },
        { "setOverlays", "editor.setOverlays({grid, lightWires, selectionWireframe, gameView}) -> bool",
          "Turns the viewport's editor helpers on and off — the View Options rows and the G key, "
          "as one verb. Omitted keys keep their value; an unknown key is REFUSED (a silently "
          "ignored overlay key is indistinguishable from a broken renderer). `gameView` hides all "
          "of them at once and is not persisted; `grid` is per-scene; the others are viewport "
          "state for this session. NOTE the View Options menu's checkmarks do not yet follow a "
          "script-driven change (same as editor.setCameraMode) — the viewport does.",
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
        { "warmUpShaders", "editor.warmUpShaders() -> {built, compiledThisRun, loadedThisRun, ms}",
          "Compiles every shader the OPEN world needs, now, instead of on the first frames the user "
          "sees (SHADER_CACHE_SPEC.md §5). The engine generates a shader per renderable on first "
          "draw, so a freshly-opened world hitches through dozens of compiles unless something does "
          "this first — which the scene-open path now does, behind the loading cover. `built` is how "
          "many this call compiled; on a warm shader cache it is 0 and the call is nearly free. "
          "Synchronous by design: the caller holds its cover up until it returns.",
          Needs::Engine },
        { "viewportState", "editor.viewportState() -> {state, framesPresented, width, height, offscreen}",
          "What the editor viewport is showing right now. `state` is \"presenting\" (the engine's own frames are on screen), \"loading\" (a world is bound but no frame of it has presented yet — the viewport wears its loading cover), \"noscene\" (no world open, the cover says so) or \"offscreen\" (this session's viewport never reaches a window: headless stand-ins and the macOS offscreen fallback). `framesPresented` counts frames actually drawn AND presented since the current world was bound, so a script can wait for real pixels instead of sleeping. `width`/`height` are the LIVE render target (the swapchain for an on-screen viewport), in pixels — not the size anybody requested, so a script can assert that a resize really took; `offscreen` says whether that target is a texture rather than a window.",
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

// AI_SURFACE_PROGRAM_SPEC lane D #15. Deliberately four keys, not five: the
// audit asked for `fps` too, but IEditorViewport::setShowFps is an EMPTY
// override in the engine viewport (enginesceneviewport.h) and nothing anywhere
// draws a counter — shipping the key would be a new F7-class silent no-op on
// the exact surface this program exists to clean up. `gameView` rides along
// because it is the master switch over the other three and already has a verb;
// having it in the read-back object is what makes the object honest (grid:true
// while gameView:true means "on, but hidden").
QVariantMap EditorApi::overlays()
{
    QVariantMap out;
    if (!requireEngine()) return out;
    out["grid"] = host.viewport->getShowGrid();
    out["lightWires"] = host.viewport->getShowLightWires();
    out["selectionWireframe"] = host.viewport->getSelectionWireframe();
    out["gameView"] = host.viewport->isGameView();
    return out;
}

bool EditorApi::setOverlays(const QVariantMap &change)
{
    if (!requireEngine()) return false;
    if (change.isEmpty())
        return fail("editor.setOverlays: nothing to change — pass a map "
                    "({grid, lightWires, selectionWireframe, gameView}); "
                    "editor.overlays() reads the current values");

    static const QStringList known = { "grid", "lightWires", "selectionWireframe", "gameView" };
    for (auto it = change.constBegin(); it != change.constEnd(); ++it) {
        if (!known.contains(it.key()))
            return fail(QStringLiteral("editor.setOverlays: unknown overlay '%1' (known: %2). "
                                       "There is no 'fps' overlay — the engine viewport draws no "
                                       "FPS counter.")
                            .arg(it.key(), known.join(", ")));
        // A non-boolean here used to mean "0" everywhere in Qt's variant
        // conversion; on this surface it means the caller guessed the type.
        const QVariant value = scriptmod::normalizeJs(it.value());
        if (value.typeId() != QMetaType::Bool)
            return fail(QStringLiteral("editor.setOverlays: '%1' must be true or false, got '%2'")
                            .arg(it.key(), value.toString()));
    }

    if (change.contains("grid")) host.viewport->setShowGrid(change.value("grid").toBool());
    if (change.contains("lightWires")) host.viewport->setShowLightWires(change.value("lightWires").toBool());
    if (change.contains("selectionWireframe"))
        host.viewport->setSelectionWireframe(change.value("selectionWireframe").toBool());
    if (change.contains("gameView")) host.viewport->setGameView(change.value("gameView").toBool());
    return true;
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
    const iris::Vec3 pos = cam->getLocalPos();
    const iris::Quat rot = cam->getLocalRot();
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

QVariantMap EditorApi::warmUpShaders()
{
    QVariantMap m;
    if (!host.viewport) { fail("editor.warmUpShaders: no viewport in this session"); return m; }
    QElapsedTimer t; t.start();
    const unsigned built = host.viewport->warmUpShaders();
    m["built"] = built;
    m["ms"] = double(t.elapsed());
    if (auto engine = EngineHost::instance().engine()) {
        unsigned compiled = 0, cached = 0, expected = 0;
        engine->shaderBuildProgress(compiled, cached, expected);
        m["compiledThisRun"] = compiled;
        m["loadedThisRun"] = cached;
    }
    return m;
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
        out.insert("width", 0);
        out.insert("height", 0);
        out.insert("offscreen", true);
        return out;
    }
    out.insert("state", host.viewport->presentationState());
    out.insert("framesPresented", QVariant::fromValue(host.viewport->framesPresented()));
    // The ACTUAL render target, straight from the engine — the one number that
    // can prove a resize reached the swapchain (deep audit area 7 F3).
    const QSize target = host.viewport->renderTargetSize();
    out.insert("width", target.width());
    out.insert("height", target.height());
    out.insert("offscreen", host.viewport->isOffscreen());
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
