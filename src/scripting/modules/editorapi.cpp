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

#include <cmath>

#include "scripting/modules/moduleshared.h"
#include "viewport/ieditorviewport.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "viewport/previewframing.h"
#include "viewport/snapsettings.h"
#include "shell/mainwindow.h"
#include "services/services.h"
#include "services/playbackservice.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "services/undoservice.h"
#include "bridge/enginehost.h"
#include "data/settingsmanager.h"

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
        { "overlays", "editor.overlays() -> {grid, lightWires, selectionWireframe, stats, gameView}",
          "The viewport's editor helpers, as they are right now: `grid` the ground grid, "
          "`lightWires` the light icons and their range wires, `selectionWireframe` the selection "
          "highlight style (true = polygon wireframe, false = silhouette outline), `stats` the "
          "engine-drawn frame-stats readout in the viewport's top-left corner (F3), `gameView` the "
          "master switch that hides the HELPERS all at once. `stats` is deliberately NOT one of the "
          "things gameView hides: it is a diagnostic, not an editor helper, and \"what is my frame "
          "time in the game view\" is the question people actually ask. Read app.renderStats() for "
          "the numbers themselves — the readout never appears in a screenshot, because screenshots "
          "render through an offscreen view and the overlay is excluded from those by construction.",
          Needs::Engine },
        { "setOverlays", "editor.setOverlays({grid, lightWires, selectionWireframe, stats, gameView}) -> bool",
          "Turns the viewport's editor helpers on and off — the View Options rows, the G key and "
          "the F3 stats readout, as one verb. Omitted keys keep their value; an unknown key is "
          "REFUSED (a silently ignored overlay key is indistinguishable from a broken renderer). "
          "`gameView` hides the helpers all at once and is not persisted; `grid` is per-scene; "
          "`stats` persists as the `show_fps` preference and survives Game View and fullscreen; the "
          "others are viewport state for this session. NOTE the View Options menu's checkmarks do "
          "not yet follow a script-driven change (same as editor.setCameraMode) — the viewport does.",
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
        { "setCamera", "editor.setCamera({position?, lookAt? | rotation?, fov?}) -> {position, rotation, projection, orthoSize, fov}",
          "Places the editor camera and returns the pose that resulted (the same shape editor.camera() reports, plus `fov`). "
          "`position` is the world-space eye point ({x,y,z} or [x,y,z]); every key is optional, so `{position:…}` alone moves "
          "the camera without turning it. Orientation is EITHER `lookAt` (a world-space point to aim at, up = +Y) OR `rotation` "
          "(a {x,y,z,scalar} quaternion as editor.camera() returns it, or {x,y,z} Euler DEGREES as node.info() returns them) — "
          "passing both is refused rather than silently preferring one. `fov` is the vertical field of view in degrees and is "
          "inert while the camera is orthographic (editor.camera().projection says which it is; editor.setView switches). "
          "A `lookAt` beyond the far clip plane pushes the plane out so the target cannot render as an empty frame. "
          "The active camera controller is resynced, so the next mouse move continues from here instead of snapping back — "
          "but the arcball controller rebuilds the pose from pitch/yaw only, so any ROLL in a `rotation` is dropped while "
          "editor.cameraMode() is \"orbit\".",
          Needs::Engine },
        { "frameNode", "editor.frameNode(id, {yaw?, pitch?, distance?}) -> {position, rotation, projection, orthoSize, fov, target, distance}",
          "Frames a node from a chosen direction: the camera is placed on the sphere around the node's world bounding box "
          "(its origin when it has no meshes) at `yaw`/`pitch` DEGREES and `distance` world units, looking at the centre. "
          "This is editor.focusSelection with the view direction under your control — omit `yaw`/`pitch` to keep the "
          "direction the camera already looks from, omit `distance` for the bounds-derived framing distance that makes the "
          "node fill the view. The angle convention is the viewport's own: yaw 0 / pitch 0 looks down -Z from +Z (the "
          "\"front\" view), yaw turns right-handed about +Y, pitch -90 is straight down (\"top\") and +90 straight up. "
          "`pitch` is CLAMPED to -89..89 (the poles are where the yaw/pitch decomposition the camera controllers run on "
          "degenerates, and where the camera flips under its subject) and `distance` to 0.01..100000. Returns the resulting "
          "pose plus the `target` it framed. Selection is untouched.",
          Needs::Engine },
        { "pilot", "editor.pilot(id | null) -> bool",
          "PILOTS a scene camera (CAMERAS_SPEC D8): the main viewport renders through it and the "
          "viewport's own navigation — RMB fly, orbit, F focus, the gizmos' pick rays — drives THAT "
          "camera's transform instead of the explorer's. Null (or no argument) ejects and returns to "
          "the explorer, leaving the camera wherever it was flown: piloting doubles as placement. The "
          "whole flight lands on the undo stack as ONE step, pushed when piloting ends. While "
          "piloting, a camera that constrains its aspect letterboxes the main view, the selection "
          "preview inset for that same camera is hidden (you are already looking through it), and "
          "editor.setView is refused — canonical views belong to the explorer. Esc ejects in the "
          "editor. Refuses a node that is not a camera of the current scene.",
          Needs::Engine },
        { "piloting", "editor.piloting() -> id | null",
          "The camera being piloted, or null when the viewport is the free explorer.",
          Needs::Engine },
        { "setViewCamera", "editor.setViewCamera(id | \"viewport\") -> bool",
          "The viewport toolbar's camera dropdown, as a verb: \"viewport\" (or null) is the explorer, "
          "a camera id pilots that camera. Exactly editor.pilot with the dropdown's vocabulary — the "
          "two are one mechanism, and this is the name the UI speaks.",
          Needs::Engine },
        { "pip", "editor.pip() -> {enabled, size, camera}",
          "The selection preview inset (CAMERAS_SPEC D3): `enabled` the preference (persisted), "
          "`size` its width as a fraction of the viewport (0.08-0.6, persisted), and `camera` the "
          "camera it is showing RIGHT NOW or null. The inset appears bottom-right while a scene "
          "camera is selected and is hidden in Game View, in play, and while piloting that same "
          "camera — so `camera` can be null while `enabled` is true.",
          Needs::Engine },
        { "setPip", "editor.setPip({enabled?, size?}) -> {enabled, size, camera}",
          "Writes the selection-preview preferences and returns the state that resulted (the same "
          "shape editor.pip() reports). Both keys are optional and both persist; an unknown key is "
          "REFUSED. `size` is clamped to 0.08-0.6. This is the Preferences row's own path — the UI "
          "calls this verb.",
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
        { "undoState", "editor.undoState() -> {count, index, canUndo, canRedo, macroOpen, pushes}",
          "The undo stack, for scripts that need to assert that an action was RECORDED rather "
          "than merely performed. `count`/`index` are the stack's own; `macroOpen` is true inside "
          "a script run. Read `pushes` — the total number of commands ever pushed — to bracket an "
          "action: a script run is ONE open macro, so editor.undo() cannot reach anything the run "
          "did AND `count` does not move while it is open (pushed commands become children of the "
          "macro). `pushes` is the only honest answer to \"did that record an undo step?\" from "
          "inside a script.",
          Needs::Document },
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

// AI_SURFACE_PROGRAM_SPEC lane D #15, plus the fifth key it had to refuse.
//
// This verb used to ship FOUR keys and turn `fps` down IN PROSE, because
// IEditorViewport::setShowFps was an empty override and nothing anywhere drew a
// counter — shipping the key would have been a new silent no-op on the exact
// surface that program existed to clean up. The refusal was a contract, and
// STATS_OVERLAY_SPEC.md is the program that discharged it: the engine now draws
// the readout (irisgl/engine/src/OgreOverlayHud.cpp) and the key is `stats`,
// not `fps`, because what it shows is frame time, draws and triangles — an FPS
// number alone would be the least useful row on it (§4).
//
// `gameView` rides along because it is the master switch over the three
// HELPERS and already has a verb; having it in the read-back object is what
// makes the object honest (grid:true while gameView:true means "on, but
// hidden"). `stats` is outside that switch on purpose (D3).
QVariantMap EditorApi::overlays()
{
    QVariantMap out;
    if (!requireEngine()) return out;
    out["grid"] = host.viewport->getShowGrid();
    out["lightWires"] = host.viewport->getShowLightWires();
    out["selectionWireframe"] = host.viewport->getSelectionWireframe();
    out["stats"] = host.viewport->getShowFps();
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

    static const QStringList known = { "grid", "lightWires", "selectionWireframe",
                                      "stats", "gameView" };
    for (auto it = change.constBegin(); it != change.constEnd(); ++it) {
        if (!known.contains(it.key()))
            return fail(QStringLiteral("editor.setOverlays: unknown overlay '%1' (known: %2). "
                                       "The frame-stats readout is 'stats', not 'fps' — it shows "
                                       "frame time, draws and triangles, not just a frame rate.")
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
    if (change.contains("stats")) {
        const bool on = change.value("stats").toBool();
        host.viewport->setShowFps(on);
        // Persisted, unlike the other rows: `stats` is the Preferences
        // `show_fps` setting, and the checkbox, the F3 key and this verb are one
        // code path with one stored value (STATS_OVERLAY_SPEC §5.3 step 3).
        SettingsManager::getDefaultManager()->setValue("show_fps", on);
    }
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
    if (!ok) {
        // Two ways to fail, and telling them apart is the whole difference
        // between "you typed it wrong" and "you are flying a camera"
        // (CAMERAS_SPEC D8: canonical views belong to the explorer).
        if (host.viewport->pilotedCamera())
            return fail("editor.setView: the viewport is PILOTING a camera — canonical views "
                        "belong to the free explorer. editor.pilot(null) ejects first.");
        return fail("editor.setView: unknown view — use top/bottom/left/right/front/back/perspective");
    }
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

QVariantMap EditorApi::setCamera(const QVariant &poseArg)
{
    QVariantMap out;
    if (!requireEngine()) return out;
    auto cam = host.viewport->editorCamera();
    if (!cam) { fail("editor.setCamera: no editor camera"); return out; }

    const QVariant normalized = scriptmod::normalizeJs(poseArg);
    if (normalized.typeId() != QMetaType::QVariantMap) {
        fail("editor.setCamera: expects an object — {position?, lookAt? | rotation?, fov?}");
        return out;
    }
    const QVariantMap params = normalized.toMap();

    // An unknown key is a typo the model must SEE (the F7/F8 silent-success
    // class): a pose that quietly ignores half of what was asked reads as "the
    // camera verb is broken".
    static const QStringList known{ QStringLiteral("position"), QStringLiteral("lookAt"),
                                    QStringLiteral("rotation"), QStringLiteral("fov") };
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (!known.contains(it.key())) {
            fail(QStringLiteral("editor.setCamera: unknown key '%1' (known: %2)")
                     .arg(it.key(), known.join(QStringLiteral(", "))));
            return out;
        }
    }
    if (params.contains(QStringLiteral("lookAt")) && params.contains(QStringLiteral("rotation"))) {
        fail("editor.setCamera: pass EITHER lookAt or rotation, not both");
        return out;
    }

    EditorCameraPose pose;
    if (params.contains(QStringLiteral("position"))) {
        pose.position = scriptmod::vecFromJs(params.value(QStringLiteral("position")),
                                             cam->getLocalPos());
        pose.hasPosition = true;
    }
    if (params.contains(QStringLiteral("lookAt"))) {
        const iris::Vec3 eye = pose.hasPosition ? pose.position : cam->getLocalPos();
        pose.lookAt = scriptmod::vecFromJs(params.value(QStringLiteral("lookAt")), eye);
        if ((pose.lookAt - eye).length() < 1e-4f) {
            fail("editor.setCamera: lookAt is the camera's own position — there is no direction in that");
            return out;
        }
        pose.hasLookAt = true;
    }
    if (params.contains(QStringLiteral("rotation"))) {
        bool ok = false;
        pose.rotation = scriptmod::quatFromJs(params.value(QStringLiteral("rotation")),
                                              cam->getLocalRot(), &ok);
        if (!ok) {
            fail("editor.setCamera: rotation must be {x,y,z,scalar} (a quaternion, as editor.camera() "
                 "returns) or {x,y,z} Euler degrees");
            return out;
        }
        pose.hasRotation = true;
    }
    if (params.contains(QStringLiteral("fov"))) {
        bool numeric = false;
        const double fov = params.value(QStringLiteral("fov")).toDouble(&numeric);
        if (!numeric || fov <= 0.0 || fov >= 180.0) {
            fail("editor.setCamera: fov must be a field of view in degrees, 0 < fov < 180");
            return out;
        }
        pose.fovDegrees = float(fov);
    }

    if (!host.viewport->setCameraPose(pose)) {
        fail("editor.setCamera: this viewport cannot place the camera");
        return out;
    }
    out = camera();
    out["fov"] = double(cam->angle);
    return out;
}

QVariantMap EditorApi::frameNode(const QString &id, const QVariant &optionsArg)
{
    QVariantMap out;
    if (!requireEngine()) return out;
    auto cam = host.viewport->editorCamera();
    if (!cam) { fail("editor.frameNode: no editor camera"); return out; }
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (!scene) { fail("editor.frameNode: no scene is open"); return out; }
    auto node = findNodeByGuid(scene->getRootNode(), id);
    if (!node) {
        fail(QStringLiteral("editor.frameNode: no node with id '%1'").arg(id));
        return out;
    }

    const QVariant normalized = scriptmod::normalizeJs(optionsArg);
    QVariantMap params;
    if (normalized.isValid() && !normalized.isNull()) {
        if (normalized.typeId() != QMetaType::QVariantMap) {
            fail("editor.frameNode: the second argument is an object — {yaw?, pitch?, distance?}");
            return out;
        }
        params = normalized.toMap();
    }
    static const QStringList known{ QStringLiteral("yaw"), QStringLiteral("pitch"),
                                    QStringLiteral("distance") };
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (!known.contains(it.key())) {
            fail(QStringLiteral("editor.frameNode: unknown key '%1' (known: %2)")
                     .arg(it.key(), known.join(QStringLiteral(", "))));
            return out;
        }
    }

    EditorFraming framing;
    auto readNumber = [&](const char *key, float &into, bool &has, const char *what) -> bool {
        const QString k = QString::fromLatin1(key);
        if (!params.contains(k)) return true;
        bool numeric = false;
        const double v = params.value(k).toDouble(&numeric);
        if (!numeric) {
            fail(QStringLiteral("editor.frameNode: %1 must be a number").arg(QString::fromLatin1(what)));
            return false;
        }
        into = float(v);
        has = true;
        return true;
    };
    if (!readNumber("yaw", framing.yawDegrees, framing.hasYaw, "yaw")) return out;
    if (!readNumber("pitch", framing.pitchDegrees, framing.hasPitch, "pitch")) return out;
    bool hasDistance = false;
    if (!readNumber("distance", framing.distance, hasDistance, "distance")) return out;

    // Clamp what an agent can ask for. The camera controllers carry the pose as
    // pitch/yaw and rebuild it (OrbitalCameraController::updateCameraRot), and
    // the orbital one — unlike the free camera — never clamps its pitch: ±90 is
    // where that decomposition degenerates and where the camera ends up hanging
    // upside down under its subject. distance <= 0 stays the "derive it" signal.
    if (framing.hasPitch) framing.pitchDegrees = qBound(-89.0f, framing.pitchDegrees, 89.0f);
    if (framing.hasYaw) framing.yawDegrees = std::fmod(framing.yawDegrees, 360.0f);
    if (hasDistance) framing.distance = qBound(0.01f, framing.distance, 100000.0f);
    else framing.distance = 0.0f;

    if (!host.viewport->frameNode(node, framing)) {
        fail("editor.frameNode: this viewport cannot place the camera");
        return out;
    }
    out = camera();
    out["fov"] = double(cam->angle);
    // What was actually framed, so a caller can assert on it without redoing
    // the bounds maths: the framed centre and the distance that came out.
    const iris::AABB bounds = preview::worldBoundingBox(node);
    const iris::Vec3 target = (bounds.getMin().x() <= bounds.getMax().x())
                                  ? bounds.getCenter() : node->getGlobalPosition();
    out["target"] = scriptmod::vecToJs(target);
    out["distance"] = double(cam->getLocalPos().distanceToPoint(target));
    return out;
}

// ---------------------------------------------------------------------------
// Pilot mode and the selection preview (CAMERAS_SPEC D3/D8, §6).

bool EditorApi::pilot(const QVariant &id)
{
    if (!requireEngine()) return false;
    const QVariant v = normalizeJs(id);
    const QString guid = v.toString();
    if (!v.isValid() || v.isNull() || guid.isEmpty()) {
        host.viewport->pilotCamera(iris::CameraNodePtr());
        return true;
    }
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (!scene) return fail("editor.pilot: no scene is open");
    auto cam = scene->cameras.value(guid);
    if (!cam)
        return fail(QStringLiteral("editor.pilot: '%1' is not a camera in this scene "
                                   "(scene.cameras() lists them)").arg(guid));
    if (!host.viewport->pilotCamera(cam))
        return fail(QStringLiteral("editor.pilot: this viewport cannot pilot '%1'").arg(guid));
    return true;
}

QVariant EditorApi::piloting()
{
    if (!requireEngine()) return QVariant();
    auto cam = host.viewport->pilotedCamera();
    return cam ? QVariant(cam->getGUID()) : QVariant();
}

bool EditorApi::setViewCamera(const QVariant &id)
{
    if (!requireEngine()) return false;
    const QVariant v = normalizeJs(id);
    const QString name = v.toString();
    // "viewport" is the dropdown's word for the explorer; so is null.
    if (!v.isValid() || v.isNull() || name.isEmpty() || name == QLatin1String("viewport"))
        return pilot(QVariant());
    return pilot(name);
}

QVariantMap EditorApi::pip()
{
    QVariantMap out;
    if (!requireEngine()) return out;
    out["enabled"] = host.viewport->pipEnabled();
    out["size"]    = host.viewport->pipSize();
    // What it is SHOWING, which is not the same question: the inset only
    // appears while a scene camera is selected and not being piloted.
    QVariant showing;
    if (host.viewport->pipEnabled() && !host.viewport->isGameView() &&
        host.services && host.services->selection) {
        auto sel = host.services->selection->selected();
        auto cam = sel ? sel.dynamicCast<iris::CameraNode>() : iris::CameraNodePtr();
        if (cam && cam != host.viewport->pilotedCamera()) showing = cam->getGUID();
    }
    out["camera"] = showing;
    return out;
}

QVariantMap EditorApi::setPip(const QVariantMap &change)
{
    if (!requireEngine()) return QVariantMap();
    static const QStringList known = { "enabled", "size" };
    for (auto it = change.constBegin(); it != change.constEnd(); ++it) {
        if (known.contains(it.key())) continue;
        fail(QStringLiteral("editor.setPip: unknown key '%1' (known: %2)")
                 .arg(it.key(), known.join(", ")));
        return QVariantMap();
    }
    if (change.contains("enabled")) host.viewport->setPipEnabled(change.value("enabled").toBool());
    if (change.contains("size"))    host.viewport->setPipSize(change.value("size").toDouble());
    return pip();
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

QVariantMap EditorApi::undoState()
{
    QVariantMap out;
    if (!host.services || !host.services->undo) return out;
    QUndoStack *stack = host.services->undo->stack();
    if (!stack) return out;
    out["count"]     = stack->count();
    out["index"]     = stack->index();
    out["canUndo"]   = stack->canUndo();
    out["canRedo"]   = stack->canRedo();
    // canUndo() is false while a macro is being composed (QUndoStack refuses to
    // undo into one), and a script run IS a macro — so this is the flag that
    // explains why editor.undo() cannot reach the run's own steps.
    out["macroOpen"] = host.services->undo->isScriptMacroOpen();
    out["pushes"]    = QVariant::fromValue(qulonglong(host.services->undo->pushCount()));
    return out;
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
