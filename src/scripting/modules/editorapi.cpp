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
          "Snaps the editor camera to a canonical view (the toolbar Views dropdown / X, Y, Z keys). Axis views switch to orthographic projection; \"perspective\" restores perspective and keeps the orientation. Works in both camera modes.",
          Needs::Engine },
        { "view", "editor.view() -> string",
          "The last canonical view requested via editor.setView (\"perspective\" until one is set). Informational — free orbiting afterwards does not reset it.",
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
        { "frame", "editor.frame(n=1) -> bool",
          "Renders exactly n frames synchronously (document->engine sync + renderOneFrame) — the deterministic stepping the test suites use.",
          Needs::Engine },
        { "screenshot", "editor.screenshot(path, w=256, h=256) -> {path, width, height, center:{r,g,b}}",
          "Offscreen render of the editor scene to a PNG; returns the centre pixel so scripts can assert on it. Headless-safe.",
          Needs::Engine },
        { "beginBatch", "editor.beginBatch() -> bool",
          "Opens a nested undo macro inside the script's run (finer-grained grouping).",
          Needs::Document },
        { "endBatch", "editor.endBatch() -> bool",
          "Closes the macro opened by editor.beginBatch().",
          Needs::Document },
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

bool EditorApi::frame(int n)
{
    if (!requireEngine()) return false;
    host.viewport->renderFrames(qBound(1, n, 1000));
    return true;
}

QVariantMap EditorApi::screenshot(const QString &path, int width, int height)
{
    QVariantMap out;
    if (!requireEngine()) return out;
    if (path.isEmpty()) { fail("editor.screenshot: a file path is required"); return out; }

    const QImage img = host.viewport->takeScreenshot(qBound(16, width, 4096), qBound(16, height, 4096));
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
