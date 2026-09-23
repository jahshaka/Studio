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

#include <QKeySequence>
#include <QKeyEvent>
#include <QDockWidget>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPointF>
#include <QSize>
#include <QUndoStack>
#include <QElapsedTimer>

#include <cmath>

#include "scripting/modules/moduleshared.h"
#include "services/shortcutregistry.h"
#include "services/selectioncost.h"
#include "viewport/gizmomode.h"
#include "viewport/ieditorviewport.h"
#include "jahshaka/engine/Engine.h"
#include "services/loadingcover.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/simulationclock.h"
#include "viewport/previewframing.h"
#include "viewport/snapsettings.h"
#include "viewport/cameraspeed.h"
#include "shell/mainwindow.h"
#include "ui/panels/assetwidget.h"
#include "ui/panels/scenehierarchywidget.h"
#include "io/sceneformat.h"
#include "services/editgate.h"
#include "services/services.h"
#include "services/playerservice.h"
#include "services/playbackservice.h"
#include "services/sceneissues.h"
#include "services/sceneeditservice.h"
#include "services/vrworld.h"
#include "services/clipboardservice.h"
#include "services/selectionservice.h"
#include "services/outlinesettings.h"
#include "services/undoservice.h"
#include "bridge/enginehost.h"
#include "data/database/database.h"
#include "data/settingsmanager.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <memory>
#include <QCoreApplication>
#include <QEventLoop>
#include "ui/controls/assetdrag.h"

using namespace scriptmod;

QVector<VerbInfo> EditorApi::verbs() const
{
    return {
        { "select", "editor.select(id | [id] | null) -> bool",
          "Selects a node — or a SET of nodes — everywhere (viewport, hierarchy, properties); "
          "null, no argument or an empty array deselects. With an array the FIRST id becomes the "
          "primary: the node the properties panel shows, the gizmo pivots on and every "
          "single-target verb acts on (EDITOR_MULTISELECT_SPEC D2).",
          Needs::Document },
        { "selection", "editor.selection() -> id | null",
          "The PRIMARY selected node's id, or null (JS null — not undefined). Unchanged by multi-selection: "
          "editor.selectionSet() is the whole set.",
          Needs::Document },
        { "selectionSet", "editor.selectionSet() -> [id]",
          "The whole selection: the primary first, then the rest in document pre-order "
          "(an ancestor before its descendants, siblings by index). Empty when nothing is selected.",
          Needs::Document },
        { "outlinerRows", "editor.outlinerRows() -> [{id, name, type, children}]",
          "The OUTLINER's rows, in row order — what the editor's scene tree shows, which since "
          "S9 (2026-09-11) is ONE ROW PER ASSET: a node an import marked `attached` is a PART of "
          "its asset (a Mixamo character is a wrapper, a root, a mesh and five material splits) "
          "and has no row, nor do its descendants. The document is unchanged — scene.nodes() and "
          "node.children() still see every part — so this is the verb that answers \"what does "
          "the user see\". `children` counts the row's CHILD ROWS, not its document children. "
          "With no editor window (a --headless run) it answers from the document by the same "
          "rule, which is what the suites assert on.",
          Needs::Document },
        { "selectAdd", "editor.selectAdd(id | [id]) -> bool",
          "Adds to the selection without replacing it (the viewport's Shift+click, the tree's "
          "Ctrl+click on an unselected row). The LAST id added becomes the primary.",
          Needs::Document },
        { "selectToggle", "editor.selectToggle(id) -> bool",
          "Adds the node if it is not selected, removes it if it is (Ctrl+click), and returns its "
          "membership AFTER the call. Removing the primary promotes the topmost remaining member.",
          Needs::Document },
        { "selectRange", "editor.selectRange(fromId, toId) -> [id]",
          "Selects everything from one node to another INCLUSIVE, replacing the selection — the "
          "tree's Shift+click. In the app the range spans VISIBLE OUTLINER ROWS (collapsed "
          "subtrees and folder rows are not in it); with no window (--headless) it spans document "
          "pre-order instead, which is the same answer for a fully expanded tree.",
          Needs::Document },
        { "selectAll", "editor.selectAll() -> [id]",
          "Selects EVERY node in the scene except the World root (Ctrl+A), and returns the set in "
          "document pre-order with the topmost node as the primary. The root is out by the same "
          "rule that keeps it out of any multi-selection (D6): as a member it would swallow the "
          "set, since every other node is its descendant. The whole tree is selected, not just "
          "the top level — the edit verbs already reduce a set to its roots, so a nested member "
          "cannot be acted on twice. Not an undo entry: selection never is.",
          Needs::Document },
        { "selectNone", "editor.selectNone() -> bool",
          "Clears the selection (the same as editor.select(null)).",
          Needs::Document },
        { "deleteSelection", "editor.deleteSelection() -> {deleted: [id], skipped: [id]}",
          "Deletes the selection as ONE undo step. A member whose ancestor is also selected is "
          "skipped (it goes with its ancestor), and the World root and non-removable nodes are "
          "reported in `skipped` rather than refusing the whole delete.",
          Needs::Document },
        { "duplicateSelection", "editor.duplicateSelection() -> [id]",
          "Duplicates the selection as ONE undo step — each copy lands right after its own "
          "original — and selects the copies. Returns the new ids, the primary's copy first.",
          Needs::Document },
        { "copy", "editor.copy() -> n",
          "DEPRECATED — call clipboard.copy(). An alias kept for scripts written before the "
          "clipboard became one component: it copies the selection onto the SAME system clipboard "
          "clipboard.copy writes to (with the asset closure) and returns how many objects went. Not "
          "an undo entry; copying nothing leaves the previous clipboard alone and returns 0.",
          Needs::Document },
        { "paste", "editor.paste() -> [id]",
          "DEPRECATED — call clipboard.paste(). An alias: pastes the clipboard's scene objects "
          "beside the primary — same parent, sibling index + 1, local transform kept — or at the "
          "scene root when nothing is selected. Fresh guids, one undo step, and the pasted nodes "
          "become the selection. It returns the new ids only; the missing-asset and skipped-item "
          "reports are clipboard.paste's.",
          Needs::Document },
        { "clipboard", "editor.clipboard() -> [{format, version, node, parent, index}]",
          "DEPRECATED — call clipboard.contents() (a description) or clipboard.text() (the "
          "payload). An alias: the clipboard's SCENE-OBJECT items in the shape node.serialize "
          "returns. Empty when the clipboard holds something that is not a Jahshaka payload.",
          Needs::Document },
        { "gizmoMode", "editor.gizmoMode() -> \"translate\" | \"rotate\" | \"scale\"",
          "The active transform gizmo mode (W/E/R in the viewport; Space cycles).",
          Needs::Engine },
        { "setGizmoMode", "editor.setGizmoMode(\"translate\"|\"rotate\"|\"scale\") -> bool",
          "Switches the transform gizmo, exactly like the W/E/R keys and the toolbar buttons.",
          Needs::Engine },
        { "gizmoHitTest", "editor.gizmoHitTest(x, y) -> {handle, distancePx, tolerancePx}",
          "WHICH GIZMO HANDLE A VIEWPORT PIXEL HITS, and how far the cursor is from it in "
          "pixels. `x`/`y` are viewport pixels with the origin top-left, exactly as a mouse "
          "event carries them, and the answer comes from the very call a mouse press takes — "
          "so what the verb reports is what a click would do. `handle` is null when the pixel "
          "grabs nothing.\n\nROTATE: \"x\", \"y\", \"z\" or \"screen\" (the outer grey ring, which "
          "turns the node about the view direction) when the pixel is inside the pick tolerance "
          "of that ring's projected circle; `distancePx` is the distance to the NEAREST ring "
          "either way, and `tolerancePx` the threshold the pick used. The rotation gizmo picks "
          "in SCREEN SPACE (smoke S15) — a ring seen edge-on projects to a line, which is still "
          "clickable, where the old 3D annulus test made whichever ring the camera looked along "
          "unclickable everywhere.\n\nTRANSLATE: \"xy\", \"yz\" or \"xz\" for the three plane "
          "handles, which are picked in pixels too (`distancePx` is 0 inside the square and the "
          "distance to its border outside, and a plane within 10 degrees of edge-on is neither "
          "drawn nor pickable); \"center\", \"x\", \"y\" or \"z\" for the ball and the three "
          "arrows, which are picked against their own geometry in 3D and therefore report no "
          "pixel distance (-1).\n\n`handle` is null (and `distancePx` -1) when the SCALE gizmo "
          "is the active one, when nothing is selected, or when this session's viewport has no "
          "camera.",
          Needs::Engine },
        { "focusSelection", "editor.focusSelection() -> bool",
          "Frames the selection in the editor camera (the F key): bounds-aware distance, current view direction kept. "
          "With more than one node selected it frames the UNION of their world bounds, in one framing, so the whole set "
          "ends up on screen (a member with no meshes contributes its origin). "
          "IN A ROTATION-LOCKED AXIS VIEW it CENTRES instead: the camera keeps its axis orientation, slides along the view axis until the node is centred, and the framing is done by the ortho zoom "
          "(backing off is invisible in an orthographic projection) — turning to face the node there would tilt a \"top\" view off the axis it is named after.",
          Needs::Engine },
        { "gameView", "editor.gameView(enabled) -> bool",
          "Game View (the G key): hides every in-viewport editor helper — grid, light wires, selection outline, gizmo. Docks stay; not persisted.",
          Needs::Engine },
        { "isGameView", "editor.isGameView() -> bool",
          "Whether Game View is active.",
          Needs::Engine },
        { "overlays", "editor.overlays() -> {grid, lightWires, selectionWireframe, stats, physicsDebug, gameView, giVolume, gridPlane, shadowAtlas, outlineWidth, outlineColor, outlinePrimaryColor}",
          "The viewport's editor helpers, as they are right now: `grid` the ground grid, "
          "`lightWires` the light icons and their range wires, `selectionWireframe` the selection "
          "highlight style (true = polygon wireframe, false = silhouette outline), `stats` the "
          "engine-drawn frame-stats readout in the viewport's top-left corner (F3), "
          "`physicsDebug` the Bullet debug drawer (collision shapes and contacts, View → "
          "Wireframes → Physics Debug Overlay — only ever visible while a simulation runs), "
          "`shadowAtlas` a strip of thumbnails along the bottom showing what the renderer "
          "rasterised into each rectangle of its ONE shadow atlas, captioned with the light each "
          "map belongs to and whether the lamp-map cache holds it (\"cached\", with a star while "
          "it re-renders). It is how you see that a "
          "lamp has no shadow map at all, or that a cached map is being re-rendered when it should "
          "not be; world.shadowStatus() is the same information as numbers. Off by default, never "
          "persisted, and never drawn in an offscreen view, so screenshots and pixel tests are "
          "unaffected. Process-wide like the stats readout: with two on-screen views both show the "
          "atlas the primary one rendered. "
          "`giVolume` the wireframe boxes around the GI lit volume and the reflection-probe "
          "region world.giStatus() reports (off by default, drawn only while GI is on — the lit "
          "volume is the one thing in a GI scene a user cannot otherwise see, and an object "
          "outside it gets no bounce), `gridPlane` which plane the grid is drawn in — \"floor\" (the XZ "
          "ground: every perspective view, and top/bottom), \"frontXY\" (front/back) or \"sideYZ\" "
          "(left/right). It follows the canonical VIEW and never the camera's pose, so panning inside "
          "an axis view cannot tip the grid out of the view plane; it is READ-ONLY — editor.setView "
          "moves it and editor.setOverlays refuses it like any other unknown key. `gameView` the "
          "master switch that hides the HELPERS all at once. `stats` is deliberately NOT one of the "
          "things gameView hides: it is a diagnostic, not an editor helper, and \"what is my frame "
          "time in the game view\" is the question people actually ask. Read app.renderStats() for "
          "the numbers themselves — the readout never appears in a screenshot, because screenshots "
          "render through an offscreen view and the overlay is excluded from those by construction. "
          "`outlineWidth`, `outlineColor` and `outlinePrimaryColor` are the selection highlight's "
          "LOOK, READ-ONLY here (they are persisted preferences, written by editor.setOutline): "
          "`outlinePrimaryColor` is the brighter colour the PRIMARY member of a multi-selection is "
          "drawn in, and is unused with a single object selected.",
          Needs::Engine },
        { "setOverlays", "editor.setOverlays({grid, lightWires, selectionWireframe, stats, physicsDebug, gameView, giVolume, shadowAtlas}) -> bool",
          "Turns the viewport's editor helpers on and off — the View Options rows, the G key and "
          "the F3 stats readout, as one verb. Omitted keys keep their value; an unknown key is "
          "REFUSED (a silently ignored overlay key is indistinguishable from a broken renderer). "
          "`gameView` hides the helpers all at once and is not persisted; `grid` is per-scene; "
          "`stats` persists as the `show_fps` preference and survives Game View and fullscreen; the "
          "others are viewport state for this session. `physicsDebug` draws the physics world's "
          "collision shapes, and shows nothing at all until a simulation is running "
          "(editor.simulate / editor.play). NOTE the View Options menu's checkmarks do "
          "not yet follow a script-driven change (same as editor.setCameraMode) — the viewport "
          "does; `physicsDebug` is the exception, its menu checkmark follows.",
          Needs::Engine },
        { "outline", "editor.outline() -> {width, color, primaryColor, primaryColorStored}",
          "The SELECTION OUTLINE's three persisted values (Preferences \u2192 Viewport): `width` in "
          "Preferences units 1..30 (the mirror draws an inverted hull scaled by 1 + width/150), "
          "`color` the colour every selected object is outlined in, and `primaryColor` the colour "
          "the PRIMARY member of a MULTI-selection gets instead \u2014 the last-clicked object, the "
          "one the gizmo pivots on (EDITOR_MULTISELECT_SPEC D4 b, Blender's rule). With a single "
          "object selected there is nothing to contrast against, so the primary colour is not used "
          "and a one-node selection draws exactly as it did before this existed. "
          "`primaryColorStored` is false when no primary colour was ever chosen \u2014 `primaryColor` "
          "is then DERIVED from `color` (lifted halfway to white) and follows it, which is why the "
          "derived value is reported rather than an empty string.",
          Needs::Document },
        { "setOutline", "editor.setOutline({width?, color?, primaryColor?}) -> {width, color, primaryColor, primaryColorStored}",
          "Writes the selection outline's look and returns what resulted \u2014 the same persisted "
          "values the Preferences rows write, and the same push onto the open scene, so the page "
          "and the verb can never disagree. Omitted keys keep their value; an unknown key is "
          "REFUSED. Colours take anything QColor parses (\"#rrggbb\", \"red\"); `width` is clamped "
          "to 1..30. Passing `primaryColor: null` CLEARS the stored choice and puts the primary "
          "back on its derived value (`color` lifted halfway to white).",
          Needs::Document },
        { "setView", "editor.setView(\"top\"|\"bottom\"|\"left\"|\"right\"|\"front\"|\"back\"|\"perspective\") -> bool",
          "Snaps the editor camera to a canonical view (the toolbar Views dropdown / X, Y, Z keys). Each view remembers its camera between visits: \"perspective\" returns to its remembered free/orbit pose, each ortho view to its own pan and zoom (a first visit gets the standard axis framing). Session-only memory; works in both camera modes. "
          "The six AXIS views are ROTATION-LOCKED: they are orthographic measuring views that stay pointed down their axis, so rotation gestures are ignored there until you return to \"perspective\" (editor.camera().rotationLocked reports it).",
          Needs::Engine },
        { "view", "editor.view() -> string",
          "The last canonical view requested via editor.setView (\"perspective\" until one is set). Informational — free orbiting afterwards does not reset it.",
          Needs::Engine },
        { "camera", "editor.camera() -> {position:{x,y,z}, rotation:{x,y,z,scalar}, projection:\"perspective\"|\"orthogonal\", orthoSize, fov, nearClip, farClip, rotationLocked}",
          "The editor camera's current pose: local position, local rotation quaternion, projection mode, ortho zoom and LENS (`fov` is the vertical field of view in degrees — the value editor.setCamera writes, "
          "and the one the scene file persists as the saved camera; `nearClip`/`farClip` complete the lens). Read-only — the pixel-free way to assert camera moves (focus, view switches) and the way a scene's "
          "SAVED camera is checked against the scene-scale convention (samples.cleanstart). A DOCUMENT verb: the editor camera is document state (the scene file's `editor.camera` block), so this answers under "
          "--headless, where the stand-in viewport holds exactly the camera the file loaded. "
          "`rotationLocked` is the AXIS-VIEW LOCK: true while the viewport is in one of the six axis views (editor.view()), which are orthographic measuring views and stay pointed down their axis. "
          "Locked, the rotation GESTURES do nothing — the right-mouse look drag, the Alt+left-mouse orbit and the arcball's own drag are ignored rather than answered by dropping out of the view — "
          "while panning (middle-mouse drag), zooming (the wheel, which moves `orthoSize`) and the fly keys keep working; the fly keys move on the camera's own basis there, so Up/Down pan up and down "
          "the screen and PageUp/PageDown dolly along the view axis. It constrains GESTURES only: editor.setCamera and editor.frameNode still write any pose they are given, and a camera being PILOTED is never "
          "locked. editor.setView(\"perspective\") clears it and restores the remembered perspective pose.",
          Needs::Document },
        { "setCamera", "editor.setCamera({position?, lookAt? | rotation?, fov?}) -> {position, rotation, projection, orthoSize, fov}",
          "Places the editor camera and returns the pose that resulted (the same shape editor.camera() reports, plus `fov`). "
          "`position` is the world-space eye point ({x,y,z} or [x,y,z]); every key is optional, so `{position:…}` alone moves "
          "the camera without turning it. Orientation is EITHER `lookAt` (a world-space point to aim at, up = +Y; straight down "
          "or up it keeps the camera's heading at the top or bottom of the frame — a top view is {position:{x:0,y:h,z:0}, "
          "lookAt:{x:0,y:0,z:0}}) OR `rotation` "
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
          "camera — so `camera` can be null while `enabled` is true. The inset is GRADED like the "
          "viewport (CAMERAS_SPEC §7.2 Route C): it renders into its own target and goes through "
          "the same tonemapper, carrying the previewed camera's own exposure and post overrides — "
          "so what it shows is that camera's shot, not the world's look.",
          Needs::Engine },
        { "setPip", "editor.setPip({enabled?, size?}) -> {enabled, size, camera}",
          "Writes the selection-preview preferences and returns the state that resulted (the same "
          "shape editor.pip() reports). Both keys are optional and both persist; an unknown key is "
          "REFUSED. `size` is clamped to 0.08-0.6. This is the Preferences row's own path — the UI "
          "calls this verb.",
          Needs::Engine },
        { "cameraSpeed", "editor.cameraSpeed([n | \"faster\" | \"slower\"]) -> {n, factor, editorSpeed, playerSpeed, vrSpeed}",
          "THE CAMERA SPEED — one dial for every way a person moves through a scene (owner R15). "
          "`n` is an INTEGER 1..32 and 10 IS TODAY: the factor it applies is n/10, so a fresh "
          "install flies exactly as it always did. That factor rides on each surface's own base — "
          "the editor's RMB fly at 8 units/second (`editorSpeed`, before Shift's 3x boost), the "
          "Player's free camera at 25 (`playerSpeed`), and a VR wearer's PROJECT speed "
          "`world.vr().flySpeed` in metres per second (`vrSpeed`), which stays the project's "
          "number because how fast a world is meant to be walked is authored. Called with no "
          "argument it reads. A NUMBER sets it, clamped to 1..32; a non-integer is REFUSED (the "
          "dial holds no decimals). \"faster\"/\"slower\" step it by one, exactly as the scroll "
          "wheel does while the right mouse button is held (Shift there steps by five). "
          "Editor-global and persisted as the preference `camera/speed` — not a property of a "
          "project. The toolbar's speed button is a view of this value.",
          Needs::Document },
        { "cameraMode", "editor.cameraMode() -> \"free\" | \"orbit\"",
          "The active camera controller: \"free\" (fly camera) or \"orbit\" (arcball).",
          Needs::Engine },
        { "setCameraMode", "editor.setCameraMode(\"free\"|\"orbit\") -> bool",
          "Switches the camera controller, like the toolbar's Free Camera / Arc Ball buttons. (The toolbar buttons do not yet reflect a script-driven switch.)",
          Needs::Engine },
        { "gizmoSpace", "editor.gizmoSpace() -> \"local\" | \"global\"",
          "Which space the transform gizmos drag in: \"global\" moves along the world axes, "
          "\"local\" along the selected object's own. The toolbar's globe/cube pair.",
          Needs::Engine },
        { "setGizmoSpace", "editor.setGizmoSpace(\"local\"|\"global\") -> bool",
          "Switches the gizmos' drag space — the toolbar's Global Space / Local Space buttons, "
          "as a verb, and the buttons follow the switch. Applies to all three gizmos at once "
          "(they have never had separate spaces). Unknown values are refused.",
          Needs::Engine },
        { "lodBias", "editor.lodBias() -> number",
          "ATOM's LOD dial, as this SESSION has it. 1 is the reference: a mesh draws a "
          "coarser baked level once that level's MEASURED deviation from the authored "
          "geometry reaches the view's pixel budget at the live lens and viewport height. "
          "0 means every object is pinned at its finest level.",
          Needs::Engine },
        { "setLodBias", "editor.setLodBias(f) -> number",
          "Sets ATOM's LOD dial for this session and returns the applied value. 1 is the "
          "reference; larger takes coarser levels sooner (2 halves every switch distance); "
          "0 PINS every object at its finest level, which is both how a test asserts one "
          "level at a time and how a person turns automatic LOD off. Applies to the live "
          "scene immediately — every mesh's switch thresholds are re-derived in place, with "
          "no rebuild — and meshes with no baked chain (skinned, too small to simplify, "
          "opened without a bake) are unaffected by any value. "
          "A SESSION DIAL AND NOT A SCENE VALUE (ATOM inventory row AT-A14): it is a "
          "measurement and debugging knob, it is never written to a project, and binding a "
          "new scene returns it to 1. It was `world.setLodBias` until ATOM-BAKE-1, backed by "
          "a document field that no reader or writer on disk ever touched.",
          Needs::Engine },
        { "fullscreen", "editor.fullscreen(on?) -> bool",
          "IMMERSIVE FULLSCREEN — the F11 state (EDITOR_SHORTCUTS_SPEC §3): the window goes "
          "fullscreen and, in the editor space, every dock and the toolbar hide; leaving it "
          "restores exactly what was visible before, including whether the window was "
          "maximized. Called with no argument it READS the state; with a boolean it sets it "
          "and returns the state that resulted — so `editor.fullscreen(true)` then "
          "`editor.fullscreen()` is the round trip. Idempotent (setting the state it is "
          "already in does nothing), and NOT the same thing as a maximized window. The stats "
          "readout deliberately survives it: it is a diagnostic, not an editor helper.",
          Needs::Window },
        { "trayState", "editor.trayState() -> {tab, tabs, consoleVisible, consoleFocused, visible, title, trayRight, rightColumnLeft, rightColumnBottom, areaBottom, trayTop, presetsTop}",
          "THE EDITOR'S BOTTOM AREA: ONE tab bar along the bottom of the editor carrying the "
          "asset browser, the Timeline and — when it is turned on — the script console (owner, "
          "2026-09-14). `tab` is the tab in front (\"assets\", \"timeline\" or \"console\"), "
          "`tabs` the tabs the bar is showing right now (a panel closed from its X leaves the "
          "bar), `consoleVisible` whether the Console tab is in the bar at all, `consoleFocused` "
          "whether the console's INPUT line has the keyboard — the half of Ctrl+` a console "
          "you still have to click does not deliver, `visible` whether the asset browser itself "
          "is on screen, and `title` its dock's own title — the string its tab is drawn from "
          "(\"Assets\").",
          Needs::Window },
        { "trayAssets", "editor.trayAssets() -> [{guid, name, folder}]",
          "WHAT THE ASSET TRAY IS SHOWING, read off the panel itself: its tiles in order — "
          "`guid` and the catalog `name` for an asset, the label for a folder (`folder: true`). "
          "The tray lists through the same function as assets.list({scope: 'project', tray: "
          "true}) (services/assettray.h — every asset the project's scene uses, once), so at "
          "the root the asset tiles and that verb's rows are the same set; this is how a suite "
          "proves it. The tray repopulates on the next event-loop turn after any pin change "
          "(an add, a binding, a remove); a repopulate still queued when this is called is "
          "applied first, so the answer is what the user sees.",
          Needs::Window },
        { "tray", "editor.tray({tab, console, height}) -> (the trayState map)",
          "DRIVES THAT AREA. `tab: \"console\"` shows the Console tab, brings it to the front "
          "and puts the keyboard in the console input — exactly what the Ctrl+` chord does, "
          "through the same function; `tab: \"assets\"` and `tab: \"timeline\"` bring those "
          "forward without closing anything (naming a tab whose panel was CLOSED opens it "
          "again). `console: true|false` adds or removes the Console tab itself. `height: px` resizes the tray (at least 40; a request below the tray's own "
          "minimum content height — ~230 px at 1080 — lands AT that minimum, read the result from "
          "trayTop), and the right column's Presets panel follows so its top stays on the tray's top "
          "line. Called with no argument it reads, "
          "like editor.trayState().",
          Needs::Window },
        { "panel", "editor.panel({name, open, raise}) -> {name, open, current, tabbed}",
          "OPENS OR CLOSES AN EDITOR PANEL — \"hierarchy\", \"properties\", \"presets\", "
          "\"assets\", \"timeline\" or \"console\" — which is exactly what the Toggle "
          "Widgets dialog's buttons and a panel's own title-bar X do, through the same "
          "function (lane SPACE-2): `open: false` CLOSES the dock the way its X does, so the "
          "panel stays closed across a space switch and a restart, and `open: true` brings it "
          "back AND to the front of its tab bar. Called with a name alone it reads. `current` "
          "is whether it is the tab in front of the bottom area's one tab bar (Assets | "
          "Timeline | Console) — an open panel behind another tab is `open: true, current: "
          "false`, which app.docks() reports for every panel at once. `raise: true` brings an "
          "ALREADY-OPEN panel to the FRONT of its tab group — the one gesture a script had "
          "no way to make (a tabbed dock could be opened and closed but not brought forward, "
          "lane SELECT-COST-1's finding). It is separate from `open` because opening already "
          "raises: `{name, raise: true}` is \"show me the one that is open behind another "
          "tab\", `{name, open: true, raise: true}` is \"open it and show it\", and raising a "
          "CLOSED panel does nothing (a closed panel has no front) and reads back `open: "
          "false, current: false` rather than opening it behind the caller's back. Within one "
          "call `open` is applied first, then `raise`, and both are idempotent; `current` in "
          "the answer is the read-back of the raise.",
          Needs::Window },
        { "propertiesTab", "editor.propertiesTab({tab}) -> {tab}",
          "THE RIGHT COLUMN'S TAB — \"world\" or \"selection\" (PROPERTY_FILTER_SPEC §2). The "
          "Properties column is two tabs now, not a panel that changes shape with the "
          "selection: World holds the eight world sections (World, Sky, World Mode, Photon, "
          "Post Process, Anti-Aliasing, Shadows, Fog) and Selection holds the selected "
          "object's. A pick brings Selection to the front and `editor.select(rootId)` brings "
          "World — the SELECTION itself is unchanged by this verb, which moves the tab only. "
          "A deselect keeps the tab it is on. Called with no argument it reads. Same "
          "implementation as the tab bar and the Ctrl+Shift+P toggle.",
          Needs::Window },
        { "propertiesFilter", "editor.propertiesFilter({tab, text}) -> {tab, text, visible, hidden}",
          "THE RIGHT COLUMN'S FILTER BOX — one per tab, filtering that tab's rows only "
          "(PROPERTY_FILTER_SPEC; owner decision 2026-09-15 \"the box belongs to its tab\"). "
          "`text` is matched case-insensitively against each row's NAME, its stable key and its "
          "keywords, plus the titles of the sections it sits in: every whitespace-separated word "
          "must match somewhere, so \"sun disc\" keeps the three Sun Disc rows and \"ssr\" finds "
          "Screen-Space Reflections through its key. A word-start match wins — if anything in the "
          "column matches at the start of a word, mid-word coincidences are dropped. A section "
          "with a match OPENS; a section with none keeps its header, greyed and closed, so the "
          "column still says where its settings are. `tab` is \"world\" or "
          "\"selection\" and defaults to the tab on screen; each tab keeps its own text for the "
          "session (nothing is persisted). `visible` and `hidden` count the rows that tab's last "
          "apply judged — rows the panel itself hides (a spot row on a point light) are in "
          "neither. Called with no argument it reads. Rows are hidden and shown, never rebuilt: "
          "the same call the box makes on every keystroke.",
          Needs::Window },
        { "properties", "editor.properties({tab}) -> [{tab, section, label, key, keywords, "
                        "panelVisible, filteredOut, visible}]",
          "WHAT IS ON THE PROPERTIES COLUMN RIGHT NOW, row by row, in the order the column "
          "holds them (PROPERTY_FILTER_SPEC §3.5) — the answer to \"which rows exist\" and "
          "\"why can I not see that row\" without a screenshot. `section` is the title chain "
          "the row sits in, outermost first (a nested section like Detail Layers adds a "
          "second entry); `label` is the row's NAME even when the header elided it on a "
          "narrow dock; `key` is its stable name where it has one (\"world.override:ssr\", "
          "\"postFx.exposure\", a material property's own name) and `keywords` the synonyms "
          "curated beside it. VISIBILITY HAS TWO INPUTS and both are reported: "
          "`panelVisible` is the panel's own intent (a spot row on a point light is false), "
          "`filteredOut` is the filter box's verdict, and `visible` is the AND of them — the "
          "row on screen. `tab` is \"world\" or \"selection\", defaulting to the tab in "
          "front; only the rows that tab has MOUNTED are listed, because those are the rows "
          "that exist for the current selection.",
          Needs::Window },
        { "propertiesStats", "editor.propertiesStats() -> {mounts, refills, rebuilds, rows, "
                             "pending, deferredHidden, visible, attached}",
          "WHAT THE PROPERTIES COLUMN HAS COST — the numbers behind \"how expensive is a "
          "pick\" and \"how expensive is an add\", so a perf claim about either can be made "
          "from the editor rather than from a stopwatch (ADD-1, 2026-09-15). `mounts` counts "
          "every (re)mount of the column; a selection only ever RAISES a mount, settled once "
          "at the end of the event-loop turn, so repeated selections inside ONE turn (an undo "
          "of a sixty-four-object macro, a multi-drop) move this by one. A scripted add is a "
          "turn of its own, so a script adding sixty-four objects still mounts sixty-four "
          "times — as a ~3 ms refill each, not a 44 ms rebuild. `refills` and `rebuilds` split "
          "the material blade's mesh "
          "picks: a refill points the rows already on screen at the new material (nothing "
          "destroyed, no popup rebuilt), a rebuild is the fallback a SHAPE change forces — a "
          "different material class, a mesh with no material, a node that gained or lost its "
          "Reset row. `rows` is how many rows the mounted tab holds. `pending` is true while a "
          "mount is owed to this turn and `deferredHidden` while one is owed to the moment the "
          "column can be SEEN (a column nobody can see builds nothing). `visible` is that same "
          "reading: the panel shown AND its dock being the tab in FRONT of its group — a dock "
          "tabbed behind another is shown by Qt and parked off-screen, so a Properties panel "
          "arranged that way used to rebuild itself for every selection, for nobody "
          "(TABS-HIDDEN-1). READING THIS BUILDS "
          "NOTHING: unlike `editor.properties`, it never settles an owed mount, because a "
          "measurement must not change what it measures. `attached` is how many blades the "
          "LAST mount actually put on the layout and showed: a mount moves only the blades "
          "that changed, so picking another mesh reports 0 (the same five blades, pointed at "
          "the new node) and picking a light after a mesh reports the tail that differs. It "
          "was five per pick before SELECT-COST-1, and those five re-shows were 4.2 of the "
          "4.9 ms a pick cost.",
          Needs::Window },
        { "selectionCost", "editor.selectionCost({reset?}) -> {selections, primaryChanges, "
                           "mountsCharged, mountsSkipped, totalMs, perSelectionMs, viewport, "
                           "properties, hierarchy, timeline, setViewport, setHierarchy, mount}",
          "WHAT A SELECTION CHANGE COSTS, PER CONSUMER — the number behind \"a controller "
          "trigger press costs more than a frame\" (SELECT-COST-1: `vr.select()` measured "
          "16-17 ms per call, independent of scene size, and a desktop click pays exactly the "
          "same). A selection fans out to SIX consumers, because a replace-select raises both "
          "of the selection service's signals: four on the PRIMARY — the viewport's outline "
          "and gizmo, the Properties column, the outliner's current row, the timeline's "
          "subject — and two on the SET, which every single pick runs as well (`setViewport` "
          "is the viewport's selected set and its gizmo group, `setHierarchy` the outliner's "
          "selected rows). Plus a seventh cost, the Properties column's DEFERRED mount, which "
          "lands in a later turn and is therefore invisible to a stopwatch around the call. "
          "Each is its own "
          "block of {ms, lastMs, maxMs, calls}: `ms` is the total wall time charged to that "
          "consumer since the process started (or since the last reset), `lastMs` the most "
          "recent charge, `maxMs` the worst single one. `selections` counts the fan-outs and "
          "`primaryChanges` how many of them really changed the selected node. A re-selection "
          "is NOT skipped — three callers re-select the node they already have precisely to "
          "refresh the panels after changing the document under them — it is merely cheap: the "
          "column re-points the blades it has instead of showing them again. `mountsCharged` is "
          "how many column mounts the `mount` bucket holds and `mountsSkipped` the owed mounts "
          "a burst never had to pay (sixty-four selections in one event-loop turn are one "
          "mount). EVERY mount is charged to that bucket, including the ones no selection "
          "raised — a scene open, a tab switch, a question that settles an owed mount — so a "
          "`perSelectionMs` read against a whole process history is diluted by them: bracket "
          "the action with `{reset: true}` and the number is your selections' alone. That is "
          "also why this count is a different window from `editor.propertiesStats().mounts`, "
          "which is the column's own count for the life of the process. `perSelectionMs` is totalMs divided "
          "by `selections` — the headline: a selection change must fit inside a frame. "
          "`{reset: true}` zeroes every counter AFTER building the answer, so a script can "
          "bracket an action. READING THIS COSTS NOTHING AND SETTLES NOTHING (no owed mount is "
          "flushed): a measurement must not change what it measures.",
          Needs::Document },
        { "snapSize", "editor.snapSize() -> {translate, rotate, scale}",
          "ALL THREE snap sizes (EDITOR_SHORTCUTS_SPEC §4), editor-global and persisted: "
          "`translate` in world units — which is also the ground grid's spacing — `rotate` in "
          "DEGREES, `scale` as a factor. The gizmos snap to these while Ctrl is held, and "
          "[ / ] step whichever one the active gizmo uses.",
          Needs::Document },
        { "setSnapSize", "editor.setSnapSize(size | {translate, rotate, scale}) -> {translate, rotate, scale}",
          "Sets any of the three snap sizes and returns all three as they ended up. A BARE "
          "NUMBER is the translate alias (`editor.setSnapSize(0.5)` — the spelling this verb "
          "shipped with, and the one that also moves the grid); the object form writes only the "
          "keys it carries, so `{rotate: 15}` leaves translate and scale alone. An unknown key "
          "is REFUSED. Each value must be > 0 and is CLAMPED to its own range — translate "
          "0.01..100, rotate 0.1..180 degrees, scale 0.01..10 — so the returned object is the "
          "truth, not an echo of the request.",
          Needs::Document },
        { "snapToFloor", "editor.snapToFloor() -> bool",
          "Drops the selection straight down onto the first scene surface below its bounds (the End key); y=0 plane when nothing is hit. Undoable.",
          Needs::Engine },
        { "undoState", "editor.undoState() -> {count, index, canUndo, canRedo, macroOpen, pushes, pendingAssetDeletes, dbBatchDepth, dbCommits}",
          "The undo stack, for scripts that need to assert that an action was RECORDED rather "
          "than merely performed. `count`/`index` are the stack's own; `macroOpen` is true inside "
          "a script run. Read `pushes` — the total number of commands ever pushed — to bracket an "
          "action: a script run is ONE open macro, so editor.undo() cannot reach anything the run "
          "did AND `count` does not move while it is open (pushed commands become children of the "
          "macro). `pushes` is the only honest answer to \"did that record an undo step?\" from "
          "inside a script. `pendingAssetDeletes` is the library work the stack still OWES: a "
          "delete command queues its asset row instead of writing it when it dies, and the queue "
          "is applied in one transaction when the stack is cleared (project close, quit), so this "
          "reads non-zero only between those two moments — it is how a test proves the rows were "
          "scrubbed after a close without one fdatasync per command on the UI thread. `dbBatchDepth` is the "
          "database's gesture transaction: a script run opens one (so it reads 1 inside a run), and every "
          "library write the run makes rides it, which is why 300 scene.addPrimitive calls cost one commit "
          "instead of 300. `dbCommits` counts this process's durable write commits so far — read it before "
          "and after an action and the difference is the number of disk syncs that action cost.",
          Needs::Document },
        { "editGate", "editor.editGate() -> {scriptRunning, refusals, notice}",
          "THE EDIT GATE — what a script run does to HAND editing (owner, 2026-09-15). While a "
          "script runs the editor is NON-EDITABLE BUT FULLY NAVIGABLE: the viewport camera, the "
          "panels, the tabs, the page switches and the selection all still work, and every "
          "document write arriving from the UI — a gizmo drag, a property row, a drop, the Delete "
          "key, a menu action that edits — is refused until the run ends. It has to be: the "
          "JavaScript runs on its own thread, so the event loop turns between two verbs, and the "
          "run is ONE open undo entry, so a hand edit would silently become part of the script's "
          "step. The run's OWN writes are not gated — the gate keys on the calling context (a "
          "verb), never on the thread, and every verb arrives on the UI thread like the hand "
          "does. `scriptRunning` is true whenever a run is in flight (so it is ALWAYS true read "
          "from a script — it is the state verb for a test or an agent, and the answer from "
          "inside is the trivial one); `refusals` counts REFUSED WRITES, not gestures: a drag asks the gate on every tick, so one refused slider drag can count a dozen — read it as \"did anything try\", never as a number of user actions; "
          "`notice` says whether the run's one \"Script running\" toast has been raised — once "
          "per run, not once per refused event.",
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
          "Leaves play mode back to editing. Always forces a real stop — safe to call when "
          "already stopped.",
          Needs::Document },
        { "pause", "editor.pause() -> bool",
          "Freezes a playing scene where it stands: the document clock stops, the physics world "
          "and every pre-play transform survive, and editor.play() RESUMES rather than "
          "restarting. Paused counts as still being in play mode — the shot does not cut back "
          "to the explorer and a possessed avatar stays possessed — but editor.playing() reads "
          "false, because it reports whether the scene is RUNNING. A no-op when not playing.",
          Needs::Document },
        { "playing", "editor.playing() -> bool",
          "Whether the editor viewport is running play-in-place RIGHT NOW — read from the "
          "viewport's own flag, the one its input routing branches on. The regression net "
          "for the 2026-09-05 stuck-play defect (input routed to the player controller "
          "after a space round trip).",
          Needs::Document },
        { "playEject", "editor.playEject([on]) -> bool",
          "EJECT (owner R13, Unreal's F8) — hand the mouse and the keyboard back to the EDITOR "
          "without stopping the run. No argument reads the latch; `true`/`false` sets it. "
          "It exists for the one state where a run really consumes input: a POSSESSED avatar "
          "takes every click and key, so without an eject the only way back to the editor is to "
          "stop the simulation. Ejected, the whole widget is the editor's again — the gizmo "
          "shortcuts (W/E/R), the fly arrows, V-hold, the pick — while physics, animation and "
          "the possessed character keep stepping; the possession's follow camera stands down "
          "from the view camera so the editor's fly moves the picture, and takes it back when "
          "you return. REFUSED when nothing is playing: it is a state of the run in flight, not "
          "a setting, and every Play and every Stop clears it. Note the asymmetry that makes it "
          "rarely needed: with NOBODY possessed a plain left click already belongs to the editor "
          "(editor.playInputOwner) and only the keys and the camera gestures are the run's.",
          Needs::Document },
        { "playInputOwner", "editor.playInputOwner() -> \"editor\" | \"controller\"",
          "WHO OWNS A PLAIN LEFT CLICK IN THE VIEWPORT RIGHT NOW. \"controller\" only while a "
          "run is really consuming input — a possessed avatar, and not ejected; \"editor\" "
          "otherwise, including outside play entirely. The viewport's event handlers branch on "
          "the SAME predicate this reports, so a click's destination is readable rather than "
          "guessable (the 2026-09-05 stuck-play defect was exactly a routing flag nobody could "
          "ask about). The right button, the wheel and the gameplay keys are NOT covered by "
          "this: they stay with the run whatever it answers, until editor.playEject(true). "
          "AND MOVING THINGS IN EDITOR PLAY IS THE GIZMO'S JOB, deliberately: the run's own "
          "left-click physics GRAB (the picking constraint that lets you throw a crate around "
          "by hand) belongs to the Player page, where the click is the game's — here the click "
          "is the editor's, and the gizmo is the answer.",
          Needs::Document },
        { "simulate", "editor.simulate(enabled=true) -> bool",
          "Starts/stops the in-place physics simulation without entering play mode.",
          Needs::Document },
        { "frame", "editor.frame(n=1, dt=-1) -> bool",
          "Renders exactly n frames synchronously (document->engine sync + renderOneFrame) — the deterministic stepping the test suites use. With `dt` >= 0 each frame hands the document's ONE simulation clock exactly that many seconds instead of the wall time the frame took; the clock turns it into whole 1/60 s grid steps (a carried remainder, never a partial step), physics, avatars and animation take those steps, and the engine's particle simulation is told to advance by the same amount. The same seconds always produce the same steps, so frame(120, 1/60), frame(60, 1/30) and frame(240, 1/120) leave the document bit-identical (scripting.e2e.fixed_clock). REFUSES a dt above scene.clock().maxAdvance (7 steps = 0.1167 s, one under the clock's 8-step catch-up bound so a carried fraction can never be dropped): a script wanting more simulated time steps more frames.",
          Needs::Engine },
        { "warmUpShaders", "editor.warmUpShaders() -> {built, compiledThisRun, loadedThisRun, ms}",
          "Compiles every shader the OPEN world needs, now, instead of on the first frames the user "
          "sees (SHADER_CACHE_SPEC.md §5). The engine generates a shader per renderable on first "
          "draw, so a freshly-opened world hitches through dozens of compiles unless something does "
          "this first — which the scene-open path now does, behind the loading cover. `built` is how "
          "many this call compiled; on a warm shader cache it is 0 and the call is nearly free. "
          "Synchronous by design: the caller holds its cover up until it returns.",
          Needs::Engine },
        { "toolbar", "editor.toolbar() -> [{id, visible, enabled, tooltip}]",
          "THE EDITOR TOOLBAR'S CONTROLS, as state — one entry per button in the order they sit "
          "there, `id` being the action's name (\"saveScene\", \"export\", \"translate\" …). It "
          "exists because a toolbar had no reading at all: \"the Save button is hidden on every "
          "default install\" was true for as long as it was because nothing could ask. Read-only; "
          "the capabilities behind the buttons are their own verbs.",
          Needs::Window },
        { "viewportState", "editor.viewportState() -> {state, streaming, pending, cover, loadingCover, indicator, coversPresented, blankPresented, nativeMapped, nativeHides, rectChanges, framesPresented, width, height, offscreen, engineScene, windowX, windowY, windowW, windowH, heldKeys, flying}",
          "What the editor viewport is showing right now. `state` is \"presenting\" (the engine's own frames are on screen), \"loading\" (a world is bound but no frame of it has presented yet — the viewport wears its loading cover), \"noscene\" (no world open, the cover says so) or \"offscreen\" (this session's viewport never reaches a window: headless stand-ins and the macOS offscreen fallback). `framesPresented` counts frames actually drawn AND presented since the current world was bound, so a script can wait for real pixels instead of sleeping. `width`/`height` are the LIVE render target (the swapchain for an on-screen viewport), in pixels — not the size anybody requested, so a script can assert that a resize really took; `offscreen` says whether that target is a texture rather than a window. `engineScene` is whether the ONE engine scene the editor and the Player both draw exists yet: it is built when a page that draws it asks (the editor viewport being shown, the Player page being entered, a VR session beginning) and by nothing else, so this is how a caller tells \"the Player built it\" from \"it was already there\". `windowX`/`windowY`/`windowW`/`windowH` are where the viewport WIDGET sits inside its top-level window, in window pixels — the conversion a suite synthesising a real click needs, since every pixel-taking verb here (gizmoHitTest, dropTargetAt, dropPointAt) speaks the viewport's coordinates and an X click speaks the window's; all four are 0 when the viewport has no window. `heldKeys` is what the editor fly believes is held down, by name (\"Left\", \"PageUp\", \"Shift\" …), sorted, and `flying` is true while the fly keys are armed (the right mouse button held). Those two exist because a key STUCK in that set is otherwise invisible: the fly reads the set only while the right button is down, and Left and Right in it together cancel to no movement at all — which reads as \"the arrows are dead\" with nothing in any log to say why (ledger §356). The set is dropped whenever the right button goes down or up, so a stuck key can no longer outlive the gesture that reads it. `streaming` and `pending` are the OTHER question — not \"is a world bound\" but \"is the picture still filling in\" (SPECS/OPEN_COVER_SPEC.md §2.1): with the loading cover off a world appears at once and streams in over the frames that follow, and `pending` is what is still coming — `{shaders, textures, gi}`, with `streaming` true while any of them is non-zero. `gi` is stages of the world's FIRST lighting arm still to run (4 when one is staged, then 3, 2, 1, 0) and `textures` is materials still drawing a fallback because their pixels have not landed; both are read straight off the engine. `shaders` IS NOT A QUEUE and cannot be — a pipeline state object is generated when a renderable is first DRAWN, so how many are left is not a number anything in this process knows — it is how many the last DRIVER frame compiled, which falls to zero the moment a frame draws without compiling anything. Beside them `shadersThisLoad` and `texturesThisLoad` are the progress figures the on-screen indicator line shows; there is deliberately no denominator for the shaders, because the only number available was the PREVIOUS SESSION's whole total (boot included) and it reads as progress through THIS load, which it is not. `cover` is which cover is on screen right now (\"none\", \"loading\", \"noscene\") — 'none' for a load in place with the preference off, which is the designed behaviour: a viewport with no world bound presents its BACKGROUND since STALE-VIEW-1, so the previous world leaves the screen the moment it is torn down and `blankPresented` counts the frames it drew there. Measured, so that nobody reads more into it than is there: in a load IN PLACE that is every frame between the teardown and the bind, and the viewport's window now stays on screen for all of them (VIEW-REBUILD-1 — before it the close half of an open switched the window to the Desktop page, which is a native ancestor of the viewport's own window, and the user watched the app's watermark for 499-2,973 ms while the engine presented into nothing) — and `loadingCover` is the preference behind it (editor.loadingCover), and `indicator` is the line the viewport is drawing at the bottom of the frame while a world streams in (\"Loading Showroom 2 - shaders 12/45  textures 3/17  lighting...\" — ASCII only, because the overlay font has no other glyphs), empty when it is drawing none. `coversPresented` counts the covers this viewport has actually PRESENTED, ever: `cover` is an instant and a warm load is over before a caller outside the process gets a second reading in, so \"was THIS load covered\" is a difference of this across the load — which is the only way to see, after the fact, that a load in place was covered even with the preference off. `blankPresented` is the same shape for the other question: frames this viewport presented with NO world bound — its background, and whatever the HUD drew over it — so \"did the teardown reach the screen\" is a difference of THIS across a load in place. It moves by at least one there because the teardown presents one frame explicitly rather than waiting for a driver tick. Never reset. `nativeMapped` is the third question and the one that governs the other two: is this viewport's native window ON SCREEN right now — an unmapped window shows nothing anybody presents into it, whatever the counters say — and `nativeHides`/`rectChanges` count how many times it has left the screen and how many times its rectangle has changed, ever, so a caller differences them across an operation the way it differences the two above. A load IN PLACE must move NEITHER: the page never changes, so the viewport keeps its window and its rect from the teardown through to the first frame of the new world. A page switch — the Desktop, the Player, a create from the Desktop — moves both, by design. Never reset.",
          Needs::Document },
        { "loadingCover", "editor.loadingCover([on]) -> bool",
          "THE LOADING COVER, as a preference (SPECS/OPEN_COVER_SPEC.md §3). The cover is the "
          "flat grey panel with \"Loading world…\" on it that the engine draws over the "
          "viewport from the moment a world starts loading until its second present. It is "
          "OFF by default: since OPEN-COVER-2a a world no longer arrives in one frame — the "
          "create and the open run one slice per event-loop turn and the first lighting arm is "
          "built one stage per frame of a world the user can already see — so the panel now "
          "hides a world that is there. With it off the world appears at once and streams in "
          "behind one line at the bottom of the viewport (editor.viewportState().pending says "
          "what that line is counting). Switched ON, a load is covered exactly as it was: the "
          "panel from the start of the load until two frames of the new world have presented, "
          "and then the same stream-in. Nothing else about a load changes with this switch — "
          "the slices, the pace rule and the present counting are the same either way, and "
          "editor.viewportState().state reads the same \"loading\"/\"presenting\" in both. "
          "Read ONCE per load, so toggling it while a world is arriving takes effect at the "
          "NEXT one rather than making the panel flicker. The Player has no cover at all and "
          "ignores it, and \"No world open\" is not a load and always shows. ONE THING IT CANNOT DO, said here because it is measured and surprising: a world opened while the editor page is already showing one leaves the PREVIOUS world's last frame on screen for the length of the load whatever this preference says. A viewport with no world bound PRESENTS NOTHING (the engine destroys the view's workspace with its scene), so after the teardown no cover can be put there either — that is also why \"No world open\" is never actually drawn. The fix is engine-side (a scene-less view drawing its own background, STALE-VIEW-1). Every Desktop route — every create, every tile — is unaffected: its viewport is hidden for the load and the frames it reveals are the new world's. With no argument "
          "this reads; with one it writes and returns the new value.",
          Needs::Window },
        { "mirrorStats", "editor.mirrorStats() -> {available, giPushes, giRefreshes, giLightRefreshes, giLightRefreshesAtRest, movableNodes, nodesVisited, materialBuilds, staticNodes, staticRepromotions, dirtyNodes, evictedNodes, verifierVisits, verifierCatches, pushes, walkMode}",
          "What the editor viewport's document->engine mirror has had to do about GLOBAL "
          "ILLUMINATION: `giPushes` counts NEW GI configurations sent to the engine, "
          "`giRefreshes` counts re-solves of the existing one. Both are expensive — a VCT "
          "re-solve tears the voxelizer down and rebuilds it from every item in the scene — and "
          "both are debounced against the last value pushed, so \"a scene nobody is editing "
          "re-solves ZERO times\" is a contract of the mirror rather than an optimisation. It is "
          "invisible in pixels (a re-voxelized scene looks identical; it just costs a frame) and "
          "invisible in the document, which is why the counters are the only way to assert it — "
          "the perf.epic_steady_state gate does exactly that on a real sample scene. "
          "`giLightRefreshes` counts the CHEAP half: while a light is being dragged the mirror "
          "no longer re-solves at all, it re-injects the lights into the voxels that are already "
          "there every few frames and waits for the drag to stop before the real re-solve. So "
          "during a drag this is the counter that moves and `giRefreshes` is the one that must "
          "NOT. `giLightRefreshesAtRest` is the subset of those taken once the motion STOPPED: "
          "the injection is cheap while something moves (one bounce, a coarser ray march) and "
          "must be the scene's full bounce count on the frame the movement ends, so one of these "
          "follows every burst of movement — and for a lamp the author marked Movable it is the "
          "only thing that ever runs the full count again, because that path deliberately arms "
          "no re-solve at all.\n\n"
          "MOBILITY (SPECS/REALTIME_REFLECTIONS_SPEC.md §3.3): `movableNodes` is how many of the "
          "scene's objects THE DOCUMENT resolved as MOVING on the last sync — physics bodies, "
          "characters, socket riders, animated objects, particle emitters, and everything "
          "travelling with one of them (node.mobility(id) explains any single object). What the "
          "RENDERER made of it — movableItems, movableLights, the play-time mobilityMisses and "
          "mobilityRebuilds — is reported by world.giStatus(), beside the probe and rebuild "
          "counters it belongs with; the two counts agreeing is how you know the classification "
          "reached the renderer at all.\n\n"
          "WHAT THE MIRROR ITSELF COSTS. `nodesVisited` is how many document nodes the last sync "
          "walked — the mirror runs this walk every frame, for every node, forever, so it is the "
          "denominator for the other two. `materialBuilds` is how many MATERIAL DESCRIPTIONS that "
          "walk had to build (convert a document material into the renderer's parameters and "
          "texture binds): it must be ZERO on a frame where nobody edited a material, and a scene "
          "where it equals the material count every frame is the defect that made an 8,404-node "
          "lattice cost 52 ms of mirror per STILL frame — the editor gives every primitive its own "
          "material, so \"one description per material\" and \"one per node\" are the same number. "
          "`staticNodes` is how many SCENE GRAPH nodes sit in a SCENE_STATIC memory manager, "
          "i.e. are OUT of the renderer's per-frame transform and bounds passes — graph nodes and "
          "not document ones, so the engine's own helpers (a light's -Y adapter, a decal's "
          "projector box, the helper wires) are in it too and it reads slightly HIGHER than "
          "nodesVisited rather than being a share of it; moving a node "
          "takes its whole subtree out of that set for the duration of the gesture, and "
          "`staticRepromotions` counts the times the mirror has put the scene back after the "
          "document went quiet (half a second with no transform write anywhere). Without that "
          "second number a long editing session drains staticNodes to nothing, one nudged prop at "
          "a time.\n\n"
          "WHAT THE MIRROR LOOKS AT, AND WHY IT IS ALMOST NOTHING "
          "(SPECS/DIRTY_SET_MIRROR_SPEC.md). The mirror does not ask every object in the scene "
          "\"did you change?\" any more — each object SAYS SO when it changes, into a list its "
          "scene keeps, and the mirror handles the list. `dirtyNodes` is how long that list was "
          "on the last sync and `nodesVisited` is how many of them were really looked at (they "
          "differ when one change reaches more than one object — a shading-model switch re-pushes "
          "every object sharing the material). Both are ZERO on a still frame, however big the "
          "scene is: that is the contract, and it is what took an 8,404-node scene from 14 ms of "
          "mirror per still frame to under one. `evictedNodes` is how many objects left the "
          "document on that sync — deletions are an event now, not a sweep over everything that "
          "still exists. `walkMode` says which of the two ran: \"dirty\" for the list, \"full\" "
          "for the whole scene, which happens only at the rare explicit moments (the first sync "
          "after a scene opens or a page switches, the play edge, the verification mode).\n\n"
          "THE SAFETY NET is `verifierVisits` and `verifierCatches`. A design where each object "
          "reports its own changes fails in exactly one way — a change that forgets to report "
          "itself never reaches the screen — so the mirror re-reads a few dozen objects a sync "
          "from a rotating cursor (a full pass over a large scene every couple of seconds) and "
          "COUNTS anything it finds behind. `verifierCatches` MUST BE ZERO. A non-zero value is "
          "not a rendering bug you can see — the verifier pushed the change, so the screen caught "
          "up within a second or two — it is a missing mark in the document, and the mirror names "
          "the object and the field once in the log. `pushes` is how many engine writes the "
          "visits have made in total, which is what the differential test compares: run the "
          "change-list sync, then the whole walk, and the whole walk must push NOTHING.\n\n"
          "`available` "
          "is false when this session's viewport has no mirror (the document-only stand-ins), and "
          "the counts are then meaningless rather than zero.",
          Needs::Document },
        { "issues", "editor.issues() -> [{id, kind, node, nodeName, message, action}]",
          "THE SCENE-ERROR AREA — the things wrong with the OPEN SCENE that the person using the "
          "editor can fix, listed in the viewport beside the frame-rate readout ONE LINE PER "
          "ISSUE, and listed here in the same stable order (by kind, then by the object). "
          "The rule that decides what belongs in it: if you can fix it in your scene it is an "
          "issue; if it exists for us to debug the engine it stays in the log (app.engineErrors, "
          "log.tail, and the monitor's capture bundle). So there are never shader compiles, cache "
          "misses or pass counts here. Every issue NAMES the object it is about (`node` is a guid "
          "you can pass straight to editor.select) and says what to DO about it (`action`), and it "
          "never repeats itself: raising one that is already live changes nothing at all. Nothing "
          "is dismissible: an issue leaves when the condition is gone, which the scanner notices "
          "within a second, and that is the only way it leaves.",
          Needs::Document },
        { "raiseIssue", "editor.raiseIssue({kind, node, message, action, id}) -> id",
          "Raises a scene issue, or does NOTHING and returns the same id when one with that id is "
          "already live. `kind` is required and is the machine-readable class (\"sun.tie\", "
          "\"shadow.leak\", or your own); `message` is required and is what is wrong in plain "
          "words; `node` is the guid of the object it is about, `action` what to do about it, and "
          "`id` defaults to \"<kind>:<node>\", which is what makes repeats free. Use it for "
          "conditions a user can fix — never for engine diagnostics, which belong in the log.",
          Needs::Document },
        { "clearIssue", "editor.clearIssue(id) -> bool",
          "Forgets a scene issue entirely, which is what \"the scene was fixed\" means: a later "
          "raise of the same id is a new event and is shown again. The scanner "
          "(editor.checkScene) does this for its own kinds by itself, which is how a line leaves "
          "the viewport's error area — there is no dismiss. False when there is no such issue.",
          Needs::Document },
        { "checkScene", "editor.checkScene() -> {issues, raised:[id], list:[...]}",
          "Runs the scene checker once against the open scene and returns what is live afterwards "
          "— the same thing the editor does on a timer, exposed so a script or a test can drive "
          "it. It knows two conditions today, both of which used to reach nobody: \"sun.tie\", "
          "two directional lights set to the same Forward Shading Priority, so which one is the "
          "sun comes out of a tie-break the author never chose; and \"shadow.leak\", a light "
          "whose shadows are switched off standing close enough to solid geometry to light "
          "straight through it. Conditions that have been fixed are cleared, so this is safe to "
          "call as often as you like. `raised` names the issues this call raised for the first "
          "time (empty on a second identical call — the never-repeat rule), and `list` is every "
          "live issue in the order the error area lists them.",
          Needs::Document },
        { "issueBar", "editor.issueBar() -> {editorActive, exists, visible, rows, lines, buttons}",
          "WHERE THE SCENE-ERROR AREA IS ON SCREEN. The bar is a frameless, "
          "always-on-top window over the EDITOR's viewport, so it must not be showing while the "
          "user is on the Desktop, Assets, Player, Materials or Publish page — it used to, "
          "complete with a Select button that selected in a viewport nobody was looking at. "
          "This verb runs one scan-and-decide pass and then reports: `editorActive` is whether "
          "the editor is the current space, `visible` whether the bar is on screen, `rows` how "
          "many issues the user can see, `lines` how many lines are actually built (one per "
          "issue, plus a \"+N more\" line past the eighth) and `buttons` how many clickable "
          "controls the bar has — ZERO, always: it shows the errors and the user fixes them. "
          "Meaningless without a window (every field is false/0 "
          "in a --headless run); `editor.issues()` is the model half and works everywhere.",
          Needs::Window },
        { "dropPointAt", "editor.dropPointAt(x, y) -> {x, y, z} | null",
          "WHERE A DROP AT THIS VIEWPORT PIXEL LANDS, in world space: the surface under the "
          "cursor when the ray hits one, else the y=0 ground plane. `x`/`y` are viewport pixels "
          "with the origin top-left, exactly as a mouse event carries them. This is the very "
          "function the viewport's drag-and-drop uses to place what you drop (smoke S2), so "
          "`scene.addPrimitive(name, {position: editor.dropPointAt(x, y)})` puts a cube exactly "
          "where dragging one there would. Null when this session's viewport has no camera (the "
          "document-only stand-ins).",
          Needs::Engine },
        { "dragAsset", "editor.dragAsset(guid, x, y, {action, type}) -> bool",
          "DRAGS AN ASSET OVER THE VIEWPORT, for real: it posts the same "
          "QDragEnter/QDragMove/QDragLeave/QDrop events a person's drag out of an asset view "
          "posts, carrying the same four-slot payload every asset view builds "
          "(ui/controls/assetdrag.h). `action` is what this step of the gesture is — 'move' "
          "(the default: hover at that pixel, which is what shows a MATERIAL's live preview on "
          "the object under it), 'drop' (hover and release, which commits) or 'leave' (the "
          "cursor left the viewport, which puts a previewed material back). The first call of a "
          "gesture sends the enter event for you; 'drop' and 'leave' end it. `type` is the "
          "ModelTypes value, and is worked out from the asset row when omitted — a RESERVED "
          "preset guid names no row and is treated as a material, which is exactly what the "
          "presets tray drags. This is the only way a script or an MCP client can perform the "
          "gesture the owner performs with a mouse; everything it reaches is the viewport's own "
          "handler, so it cannot drift from what a person gets.",
          Needs::Engine },
        { "key", "editor.key(name, action='tap') -> bool",
          "A KEY ON THE EDITOR VIEWPORT, the way the viewport receives one once the window system "
          "has delivered it: its ShortcutOverride, then the KeyPress and/or KeyRelease, sent to "
          "the viewport widget itself — the route a held fly key takes to the camera controller, "
          "and while playing to the run (PLAY-FLY-1's repro). `name` is a Qt key name ('W', 'Up', "
          "'Shift+W'); `action` is 'press' (held until a 'release'), 'release', or 'tap' (both, the "
          "default). A key the viewport does not claim that is an editor SHORTCUT is REFUSED with "
          "the shortcut's id (a real press would fire the shortcut and never reach the viewport: W "
          "is tool.translate while editing, the run's Move while playing). A held key moves the camera on every frame after it (editor.frame). It does "
          "NOT pass through the window system or Qt's shortcut map — whether a REAL key arrives is "
          "app.input_keys' question (xdotool on a private display). Refused with no viewport or "
          "an unknown key name.",
          Needs::Window },
        { "dragAssetToTray", "editor.dragAssetToTray(guidOrGuids, folderGuid, {action}) -> bool",
          "DROPS TILES ON A FOLDER TILE IN THE EDITOR'S ASSET TRAY, for real (DRAWERS-1): it "
          "posts the same QDragEnter/QDragMove/QDrop events the tray's own drag posts, aimed at "
          "the CENTRE of that folder's tile, carrying the one asset payload "
          "(ui/controls/assetdrag.h) with every dragged guid in it — which is the gesture that "
          "files a multi-selection, and the only way a script or an MCP client can perform it. "
          "The move itself is `assets.moveToFolder`, and it is one undo step either way; this "
          "verb exists so the WIDGET's half — the payload, the drop target under the cursor, the "
          "panel's own handler — is driven by a test rather than assumed. `action` is 'drop' (the "
          "default, the whole gesture) or 'move' (hover only, which files nothing). A 'drop' also "
          "spends the turn of the event loop the panel defers the move into (a drop handler runs "
          "inside the drag source's nested loop, so the move cannot happen there), and answers "
          "once it has happened. False when the tray is not showing that folder, when it is not "
          "laid out yet, or when the tile under the cursor is not a folder.",
          Needs::Window },
        { "dropTargetAt", "editor.dropTargetAt(x, y) -> {id, name, locked} | null",
          "WHAT A DROP AT THIS VIEWPORT PIXEL APPLIES TO — the node a dragged MATERIAL or IMAGE "
          "would land on. `locked` is the hierarchy's lock (the node's `pickable` flag, which is "
          "the same thing): a LOCKED node takes no drop and no click, and the drop says so by "
          "name instead of vanishing — the default Ground ships locked, which is why a material "
          "dragged onto it used to do nothing at all and an image spawned a floating plane "
          "instead of retexturing it (owner, 2026-09-14/15). Unlock the node "
          "(node.setProperty(id, \"pickable\", true), or the lock icon in the hierarchy) and both "
          "work like any other object's. Null when the ray hits NOTHING, which is the only case "
          "that spawns an image plane for a dropped picture. Same pixels as editor.dropPointAt, "
          "which answers WHERE the same drop would place a new object.",
          Needs::Engine },
        { "screenshot", "editor.screenshot(path, w=256, h=256, probes=[], grade=\"plain\") -> {path, width, height, grade, encoding, center:{r,g,b}, probes:[{x,y,r,g,b}]}",
          "Offscreen render of the editor scene to a PNG; returns the centre pixel, plus the pixel at each probe point ({x,y} in normalized 0..1 image coordinates), so scripts can assert on colours. Headless-safe. "
          "A SCREENSHOT IS THE PICTURE AT REST: before the editor camera's shot the verb renders frames at dt 0 (the document's clock does not move) until world.giStatus().giAtRest is true - no GI rebuild, settle or irradiance-field refinement still owed - at most 2000 frames, so two shots of a still scene are the same picture. "
          "`grade` says HOW THE SHOT IS DEVELOPED, and the default is deliberately the dullest answer, because this verb is a measuring instrument: "
          "\"plain\" (also spelled \"raw\", or false) is NO POST-PROCESSING AT ALL — 1x MSAA, linear radiance clipped to 8 bits, the same pixels on every machine and in every frame. This is the picture the pixel suites assert and what this verb has always returned. IT CARRIES NO SCREEN-SPACE REFLECTIONS, NO AMBIENT OCCLUSION, NO BLOOM, NO SMAA AND NO TONEMAP, BY DESIGN, and that is worth knowing before using this verb to diagnose a picture: two plain shots taken with reflections on and off are bit-identical, which says nothing about the renderer (a 2026-09-15 diagnosis read exactly that as \"screenshots have lost SSR\"). Ask for \"scene\" when the question is about what the user sees. "
          "\"tonemap\" is the THUMBNAIL picture: the deterministic filmic grade only (the scene's exposure as a constant; no bloom, no ambient occlusion, no SMAA, no reflections), so a bright scene does not clip to white and a sweep of hundreds stays cheap. "
          "\"scene\" is THE EDITOR'S OWN PICTURE and what the Screenshot button in the editor takes: the scene's WHOLE post chain exactly as the world has it — global illumination, screen-space reflections, ambient occlusion, bloom, SMAA, the looks stack, HDR and the tonemap — at this camera's pose and lens, graded at the exposure the on-screen viewport has currently converged on (carried across as a constant, so the shot is repeatable). A world with HDR switched off photographs ungraded, like the viewport. "
          "\"viewport\" (or true) is the same whole chain but with the chain's OWN adaptive exposure re-seeded from the scene's (or the driving camera's) exposure value; an offscreen view lives about two frames and cannot converge, so it grades at that seed. It exists for camera.screenshot, where there is no on-screen view measuring the camera in question — for the editor camera prefer \"scene\". "
          "The editor's own Screenshot action uses \"scene\"; project preview tiles and asset thumbnails use \"tonemap\". "
          "THE TWO COLOUR SPACES, MEASURED (PLAIN-GRADE-1, 2026-09-18): the graded answers (\"tonemap\", \"scene\", \"viewport\") are THE WINDOW'S OWN BYTES — a flat sky picked as #8000C0 reads (50, 0, 114) in a \"scene\" shot and (50, 0, 114) in an xwd grab of the live window, the same bytes — while \"plain\" is LINEAR RADIANCE: the same sky reads (55, 0, 134), which is the sRGB decode of the colour the user picked. So a plain shot is not a dark picture of the scene, it is a MEASUREMENT in a different space, and that — not a missing sRGB encode — is why every offscreen diagnosis in this tree has read \"too dark\" (this engine's window swapchain is not sRGB either; the tonemapper's output IS the display code). The answer reports `grade` and `encoding` (\"linear\" or \"display\") so a reader is told which space it is holding. "
          ,
          Needs::Engine },
        { "beginBatch", "editor.beginBatch() -> bool",
          "Opens a nested undo macro inside the script's run (finer-grained grouping).",
          Needs::Document },
        { "endBatch", "editor.endBatch() -> bool",
          "Closes the macro opened by editor.beginBatch().",
          Needs::Document },
        { "importAssets", "editor.importAssets([paths]) -> bool",
          "Starts the interactive THREADED import of the given files — the same ImportBatchRunner + progress dialog the project panel's Import button and drops use — and returns once the batch has started (it does not wait). It does NOT open the import-settings dialog a person's drop gets (a script cannot answer a modal question): every file imports with the identity record. To import a model at a chosen scale, unit or orientation, pass them: assets.import(path, {scale, units, axes, ...}). assets.importFile is the synchronous, dialog-free verb.",
          Needs::Window },
    };
}

// ---- selection helpers (EDITOR_MULTISELECT_SPEC §2.7) ----------------------

bool EditorApi::resolveNodeArgument(const QVariant &id, const QString &verb,
                                    QList<iris::SceneNodePtr> &out)
{
    out.clear();
    auto scene = host.services->sceneEdit->scene();
    if (!scene) return fail(QStringLiteral("%1: no scene is open").arg(verb));

    const QVariant value = scriptmod::normalizeJs(id);
    QStringList guids;
    if (value.typeId() == QMetaType::QVariantList) {
        for (const QVariant &entry : value.toList()) {
            const QString guid = entry.toString();
            if (!guid.isEmpty()) guids.append(guid);
        }
    } else {
        const QString guid = value.toString();
        if (!guid.isEmpty()) guids.append(guid);
    }

    for (const QString &guid : guids) {
        auto node = findNodeByGuid(scene->getRootNode(), guid);
        if (!node) return fail(QStringLiteral("%1: no node with id '%2'").arg(verb, guid));
        bool seen = false;
        for (const auto &n : out) if (n.data() == node.data()) { seen = true; break; }
        if (!seen) out.append(node);
    }
    // D6: the World root never travels in a multi. On its own it is a normal
    // selection (opening a scene selects it and shows the World panel).
    if (out.size() > 1) {
        QList<iris::SceneNodePtr> filtered;
        for (const auto &n : out) if (!n->isRootNode()) filtered.append(n);
        out = filtered;
    }
    return true;
}

namespace {

/// Document pre-order, the World root excluded — the headless stand-in for
/// "visible outliner rows" (identical for a fully expanded, folder-free tree).
void preOrderNodes(const iris::SceneNodePtr &node, QList<iris::SceneNodePtr> &out)
{
    if (!node) return;
    if (!node->isRootNode()) out.append(node);
    const int kids = node->childCount();
    for (int i = 0; i < kids; ++i)
        if (iris::SceneNode *child = node->childAt(i))
            preOrderNodes(child->sharedFromThis(), out);
}

} // namespace

QList<iris::SceneNodePtr> EditorApi::rangeInVisibleOrder(const iris::SceneNodePtr &a,
                                                         const iris::SceneNodePtr &b)
{
    QList<iris::SceneNodePtr> out;
    if (!a || !b) return out;

    // THE TREE IS THE AUTHORITY on row order when there is one: outliner
    // folders reorder the root level and a collapsed subtree is not on screen,
    // so "everything between these two rows" is a widget fact, not a document
    // one (EDITOR_MULTISELECT_SPEC §2.2).
    if (host.mainWindow) {
        if (auto *panel = host.mainWindow->hierarchyPanel()) {
            out = panel->nodesInVisibleRange(a, b);
            if (!out.isEmpty()) return out;
        }
    }

    auto scene = host.services->sceneEdit->scene();
    if (!scene) return out;
    QList<iris::SceneNodePtr> order;
    preOrderNodes(scene->getRootNode(), order);
    int i = -1, j = -1;
    for (int k = 0; k < order.size(); ++k) {
        if (order[k].data() == a.data()) i = k;
        if (order[k].data() == b.data()) j = k;
    }
    if (i < 0 || j < 0) return out;
    if (i > j) std::swap(i, j);
    for (int k = i; k <= j; ++k) out.append(order[k]);
    return out;
}

bool EditorApi::select(const QVariant &id)
{
    if (!host.services || !host.services->selection || !host.services->sceneEdit)
        return fail("editor: not available in this session");
    auto scene = host.services->sceneEdit->scene();
    if (!scene) return fail("editor.select: no scene is open");

    QList<iris::SceneNodePtr> nodes;
    if (!resolveNodeArgument(id, QStringLiteral("editor.select"), nodes)) return false;
    if (nodes.isEmpty()) {
        host.services->selection->select(iris::SceneNodePtr());
        return true;
    }
    if (nodes.size() == 1) host.services->selection->select(nodes.first());
    else                   host.services->selection->select(nodes);
    return true;
}

QVariant EditorApi::selection()
{
    // `id | null` means null — an invalid QVariant bridges to `undefined`, and
    // a script comparing `=== null` (as the docs invite it to) got the wrong
    // answer (hygiene lane, 2026-09-09).
    if (!host.services || !host.services->selection) return jsNull();
    auto node = host.services->selection->selected();
    return node ? QVariant(node->getGUID()) : jsNull();
}

QVariantList EditorApi::selectionSet()
{
    QVariantList out;
    if (!host.services || !host.services->selection) return out;
    for (const auto &node : host.services->selection->selectedSet())
        if (node) out.append(node->getGUID());
    return out;
}

namespace {

/// The document's answer to "which rows would the outliner draw": pre-order,
/// the World root excluded, an `attached` node's subtree skipped (S9 — the
/// same rule SceneHierarchyWidget::isAssetPart applies to the widget).
void outlinerRowsOf(const iris::SceneNodePtr &node, const iris::SceneNodePtr &root,
                    QList<iris::SceneNodePtr> &out)
{
    if (!node) return;
    if (node != root) out.append(node);
    const int kids = node->childCount();
    for (int i = 0; i < kids; ++i) {
        iris::SceneNode *raw = node->childAt(i);
        if (!raw) continue;
        const iris::SceneNodePtr child = raw->sharedFromThis();
        if (child->isAttached() && node != root) continue;   // an asset's own part
        outlinerRowsOf(child, root, out);
    }
}

}   // namespace

QVariantList EditorApi::outlinerRows()
{
    QVariantList out;
    if (!host.services || !host.services->sceneEdit) {
        fail("editor: not available in this session");
        return out;
    }
    auto scene = host.services->sceneEdit->scene();
    if (!scene) { fail("editor.outlinerRows: no scene is open"); return out; }

    QList<iris::SceneNodePtr> rows;
    // THE WIDGET IS THE AUTHORITY when there is one (folders reorder the root
    // level, a collapsed subtree is not on screen) — the same precedence
    // rangeInVisibleOrder uses.
    if (host.mainWindow)
        if (auto *panel = host.mainWindow->hierarchyPanel())
            rows = panel->visibleNodeRows();
    if (rows.isEmpty()) outlinerRowsOf(scene->getRootNode(), scene->getRootNode(), rows);

    for (const auto &node : rows) {
        if (!node || node->isRootNode()) continue;
        int childRows = 0;
        const int kids = node->childCount();
        for (int i = 0; i < kids; ++i)
            if (iris::SceneNode *child = node->childAt(i))
                if (!child->isAttached()) ++childRows;
        out.append(QVariantMap{ { "id", node->getGUID() },
                                { "name", node->getName() },
                                { "type", scriptmod::nodeTypeName(node->getSceneNodeType()) },
                                { "children", childRows } });
    }
    return out;
}

bool EditorApi::selectAdd(const QVariant &id)
{
    if (!host.services || !host.services->selection || !host.services->sceneEdit)
        return fail("editor: not available in this session");
    auto scene = host.services->sceneEdit->scene();
    if (!scene) return fail("editor.selectAdd: no scene is open");

    QList<iris::SceneNodePtr> nodes;
    if (!resolveNodeArgument(id, QStringLiteral("editor.selectAdd"), nodes)) return false;
    if (nodes.isEmpty()) return fail("editor.selectAdd: no node id given");
    for (const auto &node : nodes) {
        // D6: the World root is never a MEMBER of a multi — adding it would
        // swallow the set (every other member is its descendant, so the D5
        // reduction would drop them all). Adding it is a plain select, exactly
        // as Ctrl+clicking it in the tree is.
        if (node->isRootNode()) host.services->selection->select(node);
        else                    host.services->selection->add(node);
    }
    return true;
}

bool EditorApi::selectToggle(const QString &id)
{
    if (!host.services || !host.services->selection || !host.services->sceneEdit) {
        fail("editor: not available in this session");
        return false;
    }
    auto scene = host.services->sceneEdit->scene();
    if (!scene) { fail("editor.selectToggle: no scene is open"); return false; }
    auto node = findNodeByGuid(scene->getRootNode(), id);
    if (!node) {
        fail(QStringLiteral("editor.selectToggle: no node with id '%1'").arg(id));
        return false;
    }
    // D6: the World root is never a member of a multi-selection — a Ctrl+click
    // on it is a plain click, because as a member it would poison delete
    // (refused), transform (moves the world) and focus.
    if (node->isRootNode()) {
        host.services->selection->select(node);
        return true;
    }
    return host.services->selection->toggle(node);
}

QVariantList EditorApi::selectRange(const QString &fromId, const QString &toId)
{
    QVariantList out;
    if (!host.services || !host.services->selection || !host.services->sceneEdit) {
        fail("editor: not available in this session");
        return out;
    }
    auto scene = host.services->sceneEdit->scene();
    if (!scene) { fail("editor.selectRange: no scene is open"); return out; }
    auto from = findNodeByGuid(scene->getRootNode(), fromId);
    auto to   = findNodeByGuid(scene->getRootNode(), toId);
    if (!from) { fail(QStringLiteral("editor.selectRange: no node with id '%1'").arg(fromId)); return out; }
    if (!to)   { fail(QStringLiteral("editor.selectRange: no node with id '%1'").arg(toId)); return out; }

    QList<iris::SceneNodePtr> range = rangeInVisibleOrder(from, to);
    if (range.isEmpty()) return out;
    // The clicked end is the primary, exactly as it is in the tree (D2).
    QList<iris::SceneNodePtr> ordered;
    ordered.append(to);
    for (const auto &n : range) if (n.data() != to.data()) ordered.append(n);
    host.services->selection->select(ordered);
    for (const auto &n : host.services->selection->selectedSet())
        if (n) out.append(n->getGUID());
    return out;
}

QVariantList EditorApi::selectAll()
{
    QVariantList out;
    if (!host.services || !host.services->sceneEdit) {
        fail("editor: not available in this session");
        return out;
    }
    if (!host.services->sceneEdit->scene()) { fail("editor.selectAll: no scene is open"); return out; }
    // THE SAME CAPABILITY the Ctrl+A shortcut runs (SCRIPTING_SPEC §2.3) —
    // MainWindow::selectAllActiveSpace calls this service method, it does not
    // reimplement the walk.
    for (const auto &n : host.services->sceneEdit->selectAll())
        if (n) out.append(n->getGUID());
    return out;
}

bool EditorApi::selectNone()
{
    if (!host.services || !host.services->selection)
        return fail("editor: not available in this session");
    host.services->selection->clear();
    return true;
}

QVariantMap EditorApi::deleteSelection()
{
    QVariantMap out;
    if (!host.services || !host.services->selection || !host.services->sceneEdit) {
        fail("editor: not available in this session");
        return out;
    }
    const auto set = host.services->selection->selectedSet();
    if (set.isEmpty()) { fail("editor.deleteSelection: nothing is selected"); return out; }
    const auto result = host.services->sceneEdit->deleteNodes(set);
    out["deleted"] = QVariant(result.deleted);
    out["skipped"] = QVariant(result.skipped);
    return out;
}

QVariantList EditorApi::duplicateSelection()
{
    QVariantList out;
    if (!host.services || !host.services->selection || !host.services->sceneEdit) {
        fail("editor: not available in this session");
        return out;
    }
    const auto set = host.services->selection->selectedSet();
    if (set.isEmpty()) { fail("editor.duplicateSelection: nothing is selected"); return out; }
    for (const auto &node : host.services->sceneEdit->duplicateNodes(set))
        if (node) out.append(node->getGUID());
    return out;
}

// ---- the clipboard aliases (CLIPBOARD_SPEC §8) ------------------------------
//
// One clipboard, three older names. These three verbs shipped against the
// in-app QList<SceneFragment> that ClipboardService replaced; they are kept as
// thin delegates so scripts and the MCP tools written against them keep
// working, and they now read and write exactly what clipboard.* does. New code
// calls the clipboard module — the doc strings say so.

int EditorApi::copy()
{
    if (!host.services || !host.services->selection || !host.services->clipboard) {
        fail("editor: not available in this session");
        return 0;
    }
    const auto set = host.services->selection->selectedSet();
    // "How many did I copy?" is the verb's whole contract, and zero is a
    // perfectly good answer — the doc already promised the previous clipboard
    // survives it. Throwing aborted the caller's script over a documented
    // outcome (hygiene lane, 2026-09-09).
    if (set.isEmpty()) { refuse("editor.copy: nothing is selected"); return 0; }
    const auto result = host.services->clipboard->copyNodes(set);
    if (!result.ok()) { refuse(QStringLiteral("editor.copy: %1").arg(result.error)); return 0; }
    return result.items;
}

QVariantList EditorApi::paste()
{
    QVariantList out;
    if (!host.services || !host.services->clipboard) {
        fail("editor: not available in this session");
        return out;
    }
    if (host.services->clipboard->contents().isNull()) {
        fail("editor.paste: the clipboard is empty");
        return out;
    }
    for (const QString &id : host.services->clipboard->paste().pasted) out.append(id);
    return out;
}

QVariantList EditorApi::clipboard()
{
    QVariantList out;
    if (!host.services || !host.services->clipboard) return out;
    const auto envelope = host.services->clipboard->contents();
    for (const auto &item : envelope.items) {
        if (item.kind != QLatin1String(clipboardformat::kind::node())) continue;
        // The same shape node.serialize returns — session node ids deliberately
        // left out for the same reason it leaves them out.
        out.append(QVariantMap{
            { "format", QString::fromLatin1(sceneformat::kFormatId()) },
            { "version", envelope.sceneFormat > 0 ? envelope.sceneFormat : sceneformat::kVersion },
            { "node", item.nodeObject().toVariantMap() },
            { "parent", item.parentGuid() },
            { "index", item.siblingIndex() } });
    }
    return out;
}

QString EditorApi::gizmoMode()
{
    if (!requireEngine()) return QString();
    return host.viewport->gizmoMode();
}

QVariantMap EditorApi::gizmoHitTest(double x, double y)
{
    QVariantMap out;
    // An explicit JS `null`, not an absent key: an invalid QVariant inside a
    // map reaches a script as `undefined`, and `r.handle === null` is how a
    // caller asks "did this pixel miss". (The key was `ring` until GIZMO-1
    // item 3 — the verb answers for the translate gizmo's plane handles and
    // arrows now, and none of those is a ring.)
    out.insert("handle", QVariant::fromValue(nullptr));
    out.insert("distancePx", -1.0);
    out.insert("tolerancePx", 0.0);
    if (!requireEngine()) return out;
    const IEditorViewport::GizmoPickResult pick = host.viewport->gizmoHitTest(QPointF(x, y));
    if (!pick.handle.isEmpty()) out.insert("handle", pick.handle);
    out.insert("distancePx", double(pick.distancePx));
    out.insert("tolerancePx", double(pick.tolerancePx));
    return out;
}

bool EditorApi::setGizmoMode(const QString &mode)
{
    if (!requireEngine()) return false;
    // ONE ROUTE FOR EVERY SURFACE (viewport/gizmomode.h): through MainWindow's
    // slot when the shell exists, so the toolbar's checked state follows
    // exactly as it does for the W/E/R keys, and straight to the viewport
    // otherwise. The wearer's `menu` press takes the same call.
    if (!gizmomode::apply(host.mainWindow, host.viewport, mode))
        return fail(QStringLiteral("editor.setGizmoMode: unknown mode '%1' (translate|rotate|scale)").arg(mode));
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
    // The physics debug drawer (View → Wireframes → Physics Debug Overlay).
    // It had a menu item, an IEditorViewport seam and no verb at all until the
    // 2026-09-06 verb-coverage audit (F11) — the last of the wireframe menu's
    // rows to be script-invisible.
    out["physicsDebug"] = host.viewport->getShowDebugDrawFlags();
    out["gameView"] = host.viewport->isGameView();
    out["giVolume"] = host.viewport->getShowGiVolume();
    // READ-ONLY (it is a consequence of editor.setView, not a setting):
    // setOverlays refuses "gridPlane" like any other unknown key.
    out["gridPlane"] = host.viewport->gridPlane();
    out["shadowAtlas"] = host.viewport->getShowShadowAtlas();
    // The selection highlight's LOOK rides along, read-only like gridPlane:
    // `selectionWireframe` above already answers "which style", and a caller
    // asserting the highlight wanted the colours in the same breath. Writing
    // them is editor.setOutline's job (they are persisted preferences, not
    // session toggles), and setOverlays refuses them like any unknown key.
    out["outlineWidth"] = outlinesettings::width();
    out["outlineColor"] = outlinesettings::color().name();
    out["outlinePrimaryColor"] = outlinesettings::primaryColor().name();
    return out;
}

bool EditorApi::setOverlays(const QVariantMap &change)
{
    if (!requireEngine()) return false;
    // The empty-map message used to list FOUR of the keys (F18): a caller who
    // read it and then guessed "fps" got the refusal below, and a caller who
    // trusted it never learned `stats` or `physicsDebug` existed.
    static const QStringList known = { "grid", "lightWires", "selectionWireframe",
                                      "stats", "physicsDebug", "gameView", "giVolume",
                                      "shadowAtlas" };
    if (change.isEmpty())
        return fail(QStringLiteral("editor.setOverlays: nothing to change — pass a map ({%1}); "
                                   "editor.overlays() reads the current values")
                        .arg(known.join(", ")));

    for (auto it = change.constBegin(); it != change.constEnd(); ++it) {
        if (!known.contains(it.key()))
            return fail(QStringLiteral("editor.setOverlays: unknown overlay '%1' (known: %2). "
                                       "The frame-stats readout is 'stats', not 'fps' — it shows "
                                       "frame time, draws and triangles, not just a frame rate; "
                                       "the outline width and colours editor.overlays() reports "
                                       "are written by editor.setOutline, not here.")
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
    if (change.contains("physicsDebug")) {
        const bool on = change.value("physicsDebug").toBool();
        // Through MainWindow when the shell exists so the menu's checkmark
        // follows a scripted change (the same path the menu item takes);
        // straight to the viewport otherwise.
        if (host.mainWindow) host.mainWindow->setPhysicsDebugOverlay(on);
        else host.viewport->setShowDebugDrawFlags(on);
    }
    if (change.contains("gameView")) host.viewport->setGameView(change.value("gameView").toBool());
    // The GI volume boxes (LIGHTING_FIX fix 9). Not persisted: it is a
    // diagnostic you turn on to answer "is that object inside the lit volume?"
    // and the answer stops mattering the moment it is yes.
    if (change.contains("giVolume")) host.viewport->setShowGiVolume(change.value("giVolume").toBool());
    // The shadow-atlas inspector (SHADOW_TOOLING_SPEC.md §4.4): the same
    // "diagnostic, not persisted" contract as giVolume above.
    if (change.contains("shadowAtlas"))
        host.viewport->setShowShadowAtlas(change.value("shadowAtlas").toBool());
    return true;
}

// ---- the selection outline (EDITOR_MULTISELECT_SPEC D4 b) ------------------
//
// NOT on setOverlays: every key there is a boolean session toggle, and these
// three are persisted preferences of a different type. They read and write
// services/outlinesettings.h — the SAME functions the Preferences rows call —
// so there is one definition of the clamps, the defaults and the derivation.

namespace {

/// The read-back shape both verbs return.
QVariantMap outlineState()
{
    QVariantMap out;
    out["width"] = outlinesettings::width();
    out["color"] = outlinesettings::color().name();
    out["primaryColor"] = outlinesettings::primaryColor().name();
    auto *settings = SettingsManager::getDefaultManager();
    const QString stored =
        settings ? settings->getValue(outlinesettings::primaryColorKey(), QString()).toString()
                 : QString();
    out["primaryColorStored"] = QColor(stored).isValid();
    return out;
}

} // namespace

QVariantMap EditorApi::outline()
{
    return outlineState();
}

QVariantMap EditorApi::setOutline(const QVariantMap &change)
{
    static const QStringList known = { "width", "color", "primaryColor" };
    if (change.isEmpty()) {
        fail(QStringLiteral("editor.setOutline: nothing to change — pass a map ({%1}); "
                            "editor.outline() reads the current values").arg(known.join(", ")));
        return QVariantMap();
    }
    for (auto it = change.constBegin(); it != change.constEnd(); ++it) {
        if (!known.contains(it.key())) {
            fail(QStringLiteral("editor.setOutline: unknown key '%1' (known: %2)")
                     .arg(it.key(), known.join(", ")));
            return QVariantMap();
        }
    }
    if (change.contains("width")) {
        const QVariant v = scriptmod::normalizeJs(change.value("width"));
        bool ok = false;
        const int w = v.toInt(&ok);
        if (!ok) {
            fail(QStringLiteral("editor.setOutline: 'width' must be a number (%1..%2), got '%3'")
                     .arg(outlinesettings::minWidth()).arg(outlinesettings::maxWidth())
                     .arg(v.toString()));
            return QVariantMap();
        }
        outlinesettings::setWidth(w);
    }
    // A colour key is validated before ANYTHING is written for it: QColor
    // silently produces an invalid colour from a typo, and an invalid colour
    // means "use the default" one level down — so a misspelled name would
    // quietly reset the row instead of being refused.
    const auto takeColour = [&](const char *key, QColor &out, bool &clear) -> bool {
        const QVariant v = scriptmod::normalizeJs(change.value(QString::fromLatin1(key)));
        clear = !v.isValid() || v.isNull();
        if (clear) return true;
        out = QColor(v.toString());
        if (!out.isValid()) {
            fail(QStringLiteral("editor.setOutline: '%1' is not a colour ('%2') — use \"#rrggbb\" "
                                "or a colour name").arg(QString::fromLatin1(key), v.toString()));
            return false;
        }
        return true;
    };
    if (change.contains("color")) {
        QColor c; bool clear = false;
        if (!takeColour("color", c, clear)) return QVariantMap();
        outlinesettings::setColor(clear ? QColor() : c);
    }
    if (change.contains("primaryColor")) {
        QColor c; bool clear = false;
        if (!takeColour("primaryColor", c, clear)) return QVariantMap();
        // null CLEARS: the primary goes back to being derived from `color`.
        outlinesettings::setPrimaryColor(clear ? QColor() : c);
    }
    // Onto the live document immediately — the mirror reads the scene every
    // frame, so this is what makes the change visible without closing a dialog.
    if (host.services && host.services->sceneEdit)
        outlinesettings::apply(host.services->sceneEdit->scene().data());
    return outlineState();
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
    // NOT requireEngine (lane-samplescale 2026-09-09): the editor camera is
    // DOCUMENT state — the scene file's `editor.camera` block, restored into
    // whatever viewport is in play — and the headless stand-in
    // (HeadlessEditorViewport) holds the very camera the file loaded. Gating
    // the read on a render device made "what lens did this scene save?"
    // answerable only with a GPU, which is why the samples' saved cameras went
    // unchecked. The SETTER still needs the engine; only this read moved.
    if (!host.viewport) { fail("editor.camera: no viewport"); return out; }
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
    // THE LENS. setCamera has taken `fov` since it landed and camera() never
    // reported it back — a setter/getter asymmetry that made the saved lens
    // (the number the fixed-95-degree-cap incident was about) unreadable from a
    // script. angle is the VERTICAL field of view in degrees; the free-camera
    // framing policy holds the authored vertical angle at or below 16:9 and
    // holds the 16:9 HORIZONTAL extent beyond it.
    out["fov"] = cam->angle;
    out["nearClip"] = cam->nearClip;
    out["farClip"] = cam->farClip;
    // THE AXIS-VIEW LOCK, as a readback (owner report 2026-09-08). It is the
    // only way a script — or a user asking "why will my drag not turn the
    // camera" — can tell a locked view from a broken mouse.
    out["rotationLocked"] = host.viewport->cameraRotationLocked();
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
                 "returns) or {x,y,z} Euler degrees — a fourth number under any other key "
                 "(\"w\" is the one alias accepted) is refused rather than read as Euler, "
                 "because a quaternion silently understood as degrees is a pose that collapses");
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

// THE CAMERA SPEED (owner R15, lane FLYSPEED-1). ONE verb, ONE value, and
// every other way to change it — the toolbar's speed button, the scroll wheel
// while flying — lands here, so there is exactly one clamp and one place the
// preference is written. It replaced editor.flySpeed/setFlySpeed and
// player.flySpeed/setFlySpeed, which were two multipliers on two surfaces and
// could disagree about how fast "fast" is.
//
// Needs::Document deliberately — the setting is editor-global state, not a
// property of a live engine, so a headless run can set it and a suite can
// assert it with no display.
QVariantMap EditorApi::cameraSpeedState() const
{
    // THE VR NUMBER IS THE PROJECT'S BASE TIMES THE FACTOR, read from the open
    // document exactly as a session would read it (vrworld::resolve answers the
    // documented default for a null scene, so a headless run still gets a
    // number rather than a zero).
    const iris::ScenePtr scene = (host.services && host.services->sceneEdit)
                                     ? host.services->sceneEdit->scene() : iris::ScenePtr();
    return QVariantMap{
        { QStringLiteral("n"),           CameraSpeed::value() },
        { QStringLiteral("factor"),      double(CameraSpeed::factor()) },
        { QStringLiteral("editorSpeed"), double(CameraSpeed::editorSpeed()) },
        { QStringLiteral("playerSpeed"), double(CameraSpeed::playerSpeed()) },
        { QStringLiteral("vrSpeed"),
          double(CameraSpeed::applyTo(vrworld::resolve(scene).flySpeed)) },
    };
}

QVariantMap EditorApi::cameraSpeed(const QVariant &speed)
{
    const QVariant value = scriptmod::normalizeJs(speed);
    if (!value.isValid() || value.isNull()) return cameraSpeedState();

    if (value.typeId() == QMetaType::QString) {
        const QString word = value.toString().trimmed().toLower();
        // TWO WORDS, THE TWO THE DOCUMENTATION NAMES. "up"/"down" rode along
        // undocumented from the retired setFlySpeed and are deleted with it
        // (the CRUD law): a grammar nobody can read from the docs is a grammar
        // nobody can rely on.
        if (word == QLatin1String("faster"))      CameraSpeed::step(+1);
        else if (word == QLatin1String("slower")) CameraSpeed::step(-1);
        else {
            fail(QStringLiteral("editor.cameraSpeed: unknown speed '%1' (an integer 1..32, "
                                "\"faster\" or \"slower\")").arg(value.toString()));
            return QVariantMap();
        }
    } else if (value.typeId() == QMetaType::Bool) {
        // A BOOLEAN IS NOT A SPEED, and Qt would hand us 1 for `true` (the
        // world.vr defect of VR-WORLD-1, in the one other place a number is
        // read from a script).
        fail(QStringLiteral("editor.cameraSpeed: a true/false is not a camera speed "
                            "(an integer 1..32, \"faster\" or \"slower\")"));
        return QVariantMap();
    } else {
        bool numeric = false;
        const double number = value.toDouble(&numeric);
        if (!numeric || !std::isfinite(number)) {
            fail(QStringLiteral("editor.cameraSpeed: '%1' is not a camera speed (an integer "
                                "1..32, \"faster\" or \"slower\")").arg(value.toString()));
            return QVariantMap();
        }
        // AN INTEGER, NOT A ROUNDED ONE: the dial holds no decimals, so 12.5 is
        // a caller who thinks it does and would silently get 12 or 13.
        if (number != std::floor(number)) {
            fail(QStringLiteral("editor.cameraSpeed: %1 is not a whole number — the camera "
                                "speed is an integer 1..32").arg(value.toString()));
            return QVariantMap();
        }
        // CLAMPED AS A DOUBLE, THEN NARROWED: `int(1e10)` is undefined
        // behaviour, and on this compiler it lands on INT_MIN — so an absurdly
        // large number clamped to 1 instead of 32, which is the opposite of
        // what the caller asked for (fix round item 3).
        const double clamped = std::min(std::max(number, double(CameraSpeed::kMin)),
                                        double(CameraSpeed::kMax));
        CameraSpeed::setValue(int(clamped));
    }
    // NOTHING TO TELL THE SHELL: the toolbar's speed button follows the dial
    // itself (CameraSpeed::setOnChanged), which is what makes the Player's
    // wheel move it too — and nothing to write, either. The store write is
    // deferred (CameraSpeed::flush's note) precisely so a caller in a loop —
    // a script here, a mouse-move on the slider there — cannot turn a setting
    // into one durable rewrite of jahsettings.ini each; the value reaches the
    // file half a second later, or at the latest when the window closes.
    return cameraSpeedState();
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

QString EditorApi::gizmoSpace()
{
    if (!requireEngine()) return QString();
    return host.viewport->gizmoTransformSpace();
}

bool EditorApi::setGizmoSpace(const QString &space)
{
    if (!requireEngine()) return false;
    const QString wanted = space.trimmed().toLower();
    if (wanted != QLatin1String("local") && wanted != QLatin1String("global"))
        return fail(QStringLiteral("editor.setGizmoSpace: unknown space '%1' (local|global)")
                        .arg(space));
    // Through MainWindow when the shell exists so the toolbar's Global/Local
    // buttons follow the switch (the same path the buttons themselves take);
    // straight to the viewport otherwise.
    if (host.mainWindow) host.mainWindow->applyGizmoTransformSpace(wanted);
    else if (wanted == QLatin1String("local")) host.viewport->setGizmoTransformToLocal();
    else host.viewport->setGizmoTransformToGlobal();
    return true;
}

// ATOM's LOD dial (AT-A14). THE ENGINE SCENE IS WHERE IT LIVES, because the
// engine scene is where it is SPENT: `Scene::setLodBias` re-derives every mesh's
// switch thresholds in place, and the Items hold a pointer to the array it
// rewrites, so one call moves every instance with no rebuild. There is no
// document field behind this and no mirror push in front of it — that shape was
// the defect (a measurement knob in the document, with no reader or writer on
// disk in either direction).
double EditorApi::lodBias()
{
    if (!requireEngine()) return 0.0;
    jahshaka::engine::Scene *scene = host.viewport->engineScene();
    if (!scene) return fail(QStringLiteral("editor.lodBias: no engine scene")), 0.0;
    return double(scene->lodBias());
}

double EditorApi::setLodBias(double bias)
{
    if (!requireEngine()) return 0.0;
    jahshaka::engine::Scene *scene = host.viewport->engineScene();
    if (!scene) return fail(QStringLiteral("editor.setLodBias: no engine scene")), 0.0;
    if (!(bias >= 0.0) || bias > 1.0e6) {
        fail(QStringLiteral("editor.setLodBias: the bias must be between 0 (pin the finest "
                            "level) and 1e6"));
        return double(scene->lodBias());
    }
    scene->setLodBias(float(bias));
    // Step a frame so a script that sets the dial and then reads
    // app.renderStats() sees the level it asked for.
    host.viewport->renderFrames(1);
    return double(scene->lodBias());
}

bool EditorApi::fullscreen(const QVariant &on)
{
    if (!host.mainWindow)
        return fail("editor.fullscreen: this verb needs the editor window (a --script/--headless "
                    "run has no window to make fullscreen)");
    const QVariant value = scriptmod::normalizeJs(on);
    if (value.isValid() && !value.isNull()) {
        if (value.typeId() != QMetaType::Bool)
            return fail(QStringLiteral("editor.fullscreen: '%1' is not true or false — call it "
                                       "with no argument to READ the state")
                            .arg(value.toString()));
        host.mainWindow->setImmersiveFullscreen(value.toBool());
    }
    return host.mainWindow->isImmersiveFullscreen();
}

// THE BOTTOM TRAY (smoke S1). The verb and the Ctrl+` chord call the SAME
// MainWindow functions — the chord's ShortcutRegistry entry is one line calling
// toggleScriptConsole() — so what a suite asserts through these verbs is what
// the key really did, not a parallel implementation of it.
QVariantMap EditorApi::trayState()
{
    QVariantMap out;
    if (!host.mainWindow) {
        fail("editor.trayState: this verb needs the editor window (a --script/--headless run "
             "has no tray)");
        return out;
    }
    const QString tab = host.mainWindow->trayTab();
    if (tab.isEmpty()) {
        fail("editor.trayState: this window has no bottom tray");
        return out;
    }
    QVariantList tabs;
    for (const QString &name : host.mainWindow->trayTabs()) tabs << name;
    out["tab"] = tab;
    out["tabs"] = tabs;
    out["consoleVisible"] = host.mainWindow->isConsoleTabVisible();
    out["consoleFocused"] = host.mainWindow->isConsoleInputFocused();
    out["visible"] = host.mainWindow->isTrayVisible();
    // The asset browser dock's title, read off the dock itself (objectName
    // "assetDock", the DockState key): the string its tab is drawn from.
    const auto *assets = host.mainWindow->findChild<QDockWidget *>(QStringLiteral("assetDock"));
    out["title"] = assets ? assets->windowTitle() : QString();
    // …and the GEOMETRY belongs to whichever tab is in front: the three docks
    // down there share one rectangle and Qt parks the ones behind off-screen
    // (lane SPACE-2), so measuring the asset browser while the Timeline is up
    // answers with the parking spot.
    const auto *dock = host.mainWindow->bottomFrontDock();
    // THE CORNER (owner, 2026-09-12): the right column owns the bottom-right
    // corner, so it runs to the bottom of the editor and the tray stops at its
    // edge. Reported in the docks' shared parent (the editor's nested window)
    // so a test can assert it: trayRight <= rightColumnLeft and
    // rightColumnBottom == areaBottom.
    const auto *props = host.mainWindow->findChild<QDockWidget *>(QStringLiteral("sceneNodePropertiesDock"));
    const auto *presets = host.mainWindow->findChild<QDockWidget *>(QStringLiteral("presetsDock"));
    if (dock && props && dock->parentWidget() && dock->isVisible() && props->isVisible()) {
        const QWidget *area = dock->parentWidget();
        out["trayRight"] = dock->geometry().right();
        out["rightColumnLeft"] = props->geometry().left();
        int bottom = props->geometry().bottom();
        if (presets && presets->isVisible() && presets->parentWidget() == area)
            bottom = std::max(bottom, presets->geometry().bottom());
        out["rightColumnBottom"] = bottom;
        out["areaBottom"] = area->height() - 1;
        // THE PRESETS LINE (owner 2026-09-12): Presets starts where the Tray does.
        out["trayTop"] = host.mainWindow->bottomAreaTop();
        if (presets && presets->isVisible() && presets->parentWidget() == area)
            out["presetsTop"] = presets->geometry().top();
    }
    return out;
}

QVariantList EditorApi::trayAssets()
{
    if (!host.mainWindow || !host.mainWindow->assetTray()) {
        fail("editor.trayAssets: this verb needs the editor window (a --script/--headless run "
             "has no tray) — assets.list({scope: 'project', tray: true}) is the same listing "
             "as data");
        return QVariantList();
    }
    return host.mainWindow->assetTray()->shownTiles();
}

QVariantMap EditorApi::tray(const QVariantMap &change)
{
    if (!host.mainWindow) {
        fail("editor.tray: this verb needs the editor window (a --script/--headless run has no "
             "tray)");
        return QVariantMap();
    }
    if (host.mainWindow->trayTab().isEmpty()) {
        fail("editor.tray: this window has no bottom tray");
        return QVariantMap();
    }
    static const QStringList known = { "tab", "console", "height" };
    for (auto it = change.constBegin(); it != change.constEnd(); ++it) {
        if (known.contains(it.key())) continue;
        fail(QStringLiteral("editor.tray: unknown key '%1' (known: %2)")
                 .arg(it.key(), known.join(", ")));
        return QVariantMap();
    }
    // `console` first: turning the tab off and then asking for a tab is a
    // contradiction the caller should see resolved in the order they wrote it,
    // and `tab` is the more specific request.
    if (change.contains("console"))
        host.mainWindow->setConsoleTabVisible(change.value("console").toBool());
    if (change.contains("height")) {
        const int h = change.value("height").toInt();
        if (!host.mainWindow->setTrayHeight(h)) {
            fail(QStringLiteral("editor.tray: height %1 refused (at least 40 px, and the editor tray must exist)").arg(h));
            return QVariantMap();
        }
    }
    if (change.contains("tab")) {
        const QString tab = change.value("tab").toString();
        if (!host.mainWindow->setTrayTab(tab)) {
            fail(QStringLiteral("editor.tray: unknown tab '%1' (assets|timeline|console)").arg(tab));
            return QVariantMap();
        }
    }
    return trayState();
}

QVariantMap EditorApi::panel(const QVariantMap &change)
{
    if (!host.mainWindow) {
        fail("editor.panel: this verb needs the editor window (a --script/--headless run has "
             "no panels)");
        return QVariantMap();
    }
    static const QStringList known = { "name", "open", "raise" };
    for (auto it = change.constBegin(); it != change.constEnd(); ++it) {
        if (known.contains(it.key())) continue;
        fail(QStringLiteral("editor.panel: unknown key '%1' (known: %2)")
                 .arg(it.key(), known.join(", ")));
        return QVariantMap();
    }
    const QString name = change.value("name").toString().trimmed().toLower();
    if (!host.mainWindow->panelDock(name)) {
        fail(QStringLiteral("editor.panel: unknown panel '%1' (hierarchy|properties|presets|"
                            "assets|timeline|console)").arg(change.value("name").toString()));
        return QVariantMap();
    }
    if (change.contains("open"))
        host.mainWindow->setPanelOpen(name, change.value("open").toBool());
    // ...then the raise, so `{open: true, raise: true}` in one call means what
    // it reads like. `open: true` raises by itself; this is for the panel that
    // is already open behind another tab.
    if (change.value("raise").toBool())
        host.mainWindow->raisePanel(name);
    QVariantMap out;
    out["name"] = name;
    out["open"] = host.mainWindow->isPanelOpen(name);
    out["current"] = MainWindow::isFrontTab(host.mainWindow->panelDock(name));
    out["tabbed"] = host.mainWindow->trayTabs().contains(name);
    return out;
}

namespace {

QVariantMap selectionCostBucket(selcost::Stage stage)
{
    const selcost::Bucket &b = selcost::bucket(stage);
    QVariantMap out;
    out["ms"]     = b.ms;
    out["lastMs"] = b.lastMs;
    out["maxMs"]  = b.maxMs;
    out["calls"]  = QVariant::fromValue(qulonglong(b.calls));
    return out;
}

} // namespace

QVariantMap EditorApi::selectionCost(const QVariantMap &options)
{
    for (auto it = options.cbegin(); it != options.cend(); ++it) {
        if (it.key() != QLatin1String("reset")) {
            fail(QStringLiteral("editor.selectionCost: unknown key '%1' — the only key is "
                                "'reset'").arg(it.key()));
            return QVariantMap();
        }
    }

    QVariantMap out;
    const quint64 selections = selcost::selections();
    out["selections"]     = QVariant::fromValue(qulonglong(selections));
    out["primaryChanges"] = QVariant::fromValue(qulonglong(selcost::primaryChanges()));
    // `mountsCharged`, not `mounts`: this is the mounts the bucket holds SINCE
    // THE LAST RESET, a different window from `editor.propertiesStats().mounts`
    // (the column's own count for the life of the process). Two keys called
    // `mounts` that disagree after a reset would be a trap.
    out["mountsCharged"]  = QVariant::fromValue(qulonglong(selcost::bucket(selcost::Mount).calls));
    out["mountsSkipped"]  = QVariant::fromValue(qulonglong(selcost::mountsSkipped()));
    const double total = selcost::totalMs();
    out["totalMs"]        = total;
    out["perSelectionMs"] = selections ? total / double(selections) : 0.0;
    out["viewport"]     = selectionCostBucket(selcost::Viewport);
    out["properties"]   = selectionCostBucket(selcost::Properties);
    out["hierarchy"]    = selectionCostBucket(selcost::Hierarchy);
    out["timeline"]     = selectionCostBucket(selcost::Timeline);
    out["setViewport"]  = selectionCostBucket(selcost::SetViewport);
    out["setHierarchy"] = selectionCostBucket(selcost::SetHierarchy);
    out["mount"]        = selectionCostBucket(selcost::Mount);

    if (options.value(QStringLiteral("reset"), false).toBool()) selcost::reset();
    return out;
}

QVariantMap EditorApi::propertiesStats()
{
    if (!host.mainWindow) {
        fail("editor.propertiesStats: this verb needs the editor window (a --script/--headless "
             "run has no panels)");
        return QVariantMap();
    }
    return host.mainWindow->propertiesStats();
}

QVariantMap EditorApi::propertiesTab(const QVariantMap &change)
{
    if (!host.mainWindow) {
        fail("editor.propertiesTab: this verb needs the editor window (a --script/--headless "
             "run has no panels)");
        return QVariantMap();
    }
    static const QStringList known = { QStringLiteral("tab") };
    const QString refusal = scriptmod::refuseUnknownKeys(QStringLiteral("editor.propertiesTab"),
                                                         change, known);
    if (!refusal.isEmpty()) { fail(refusal); return QVariantMap(); }

    if (change.contains(QStringLiteral("tab"))) {
        const QString wanted = change.value(QStringLiteral("tab")).toString();
        if (!host.mainWindow->setPropertiesTab(wanted)) {
            fail(QStringLiteral("editor.propertiesTab: unknown tab '%1' (world|selection)")
                     .arg(wanted));
            return QVariantMap();
        }
    }
    QVariantMap out;
    out[QStringLiteral("tab")] = host.mainWindow->propertiesTab();
    return out;
}

QVariantMap EditorApi::propertiesFilter(const QVariantMap &change)
{
    if (!host.mainWindow) {
        fail("editor.propertiesFilter: this verb needs the editor window (a --script/--headless "
             "run has no panels)");
        return QVariantMap();
    }
    static const QStringList known = { QStringLiteral("tab"), QStringLiteral("text") };
    const QString refusal = scriptmod::refuseUnknownKeys(QStringLiteral("editor.propertiesFilter"),
                                                         change, known);
    if (!refusal.isEmpty()) { fail(refusal); return QVariantMap(); }

    const QString tab = change.value(QStringLiteral("tab")).toString();
    if (!host.mainWindow->isPropertiesTab(tab)) {
        fail(QStringLiteral("editor.propertiesFilter: unknown tab '%1' (world|selection)").arg(tab));
        return QVariantMap();
    }
    if (change.contains(QStringLiteral("text")))
        host.mainWindow->setPropertiesFilter(tab, change.value(QStringLiteral("text")).toString());
    const QPair<int, int> counts = host.mainWindow->propertiesFilterCounts(tab);
    QVariantMap out;
    out[QStringLiteral("tab")] = tab.isEmpty() ? host.mainWindow->propertiesTab() : tab.trimmed().toLower();
    out[QStringLiteral("text")] = host.mainWindow->propertiesFilter(tab);
    out[QStringLiteral("visible")] = counts.first;
    out[QStringLiteral("hidden")] = counts.second;
    return out;
}

QVariantList EditorApi::properties(const QVariantMap &args)
{
    if (!host.mainWindow) {
        fail("editor.properties: this verb needs the editor window (a --script/--headless "
             "run has no panels)");
        return QVariantList();
    }
    static const QStringList known = { QStringLiteral("tab") };
    const QString refusal = scriptmod::refuseUnknownKeys(QStringLiteral("editor.properties"),
                                                         args, known);
    if (!refusal.isEmpty()) { fail(refusal); return QVariantList(); }

    const QString tab = args.value(QStringLiteral("tab")).toString();
    if (!host.mainWindow->isPropertiesTab(tab)) {
        fail(QStringLiteral("editor.properties: unknown tab '%1' (world|selection)").arg(tab));
        return QVariantList();
    }
    return host.mainWindow->propertyRows(tab);
}

QVariantMap EditorApi::snapSize()
{
    return QVariantMap{ { QStringLiteral("translate"), double(SnapSettings::translateSize()) },
                        { QStringLiteral("rotate"), double(SnapSettings::rotateSize()) },
                        { QStringLiteral("scale"), double(SnapSettings::scaleSize()) } };
}

QVariantMap EditorApi::setSnapSize(const QVariant &sizeArg)
{
    // Two of the three snap sizes had no verb at all (2026-09-06 verb-coverage
    // audit F10): the rotate and scale gizmos snap to SnapSettings just as the
    // translate one does, and only [ / ] in the viewport could change them.
    // The bare number keeps meaning "translate" — it is the spelling this verb
    // shipped with, and the one that moves the grid.
    const QVariant value = scriptmod::normalizeJs(sizeArg);
    QVariantMap change;
    if (value.typeId() == QMetaType::QVariantMap) {
        change = value.toMap();
        if (change.isEmpty()) {
            fail("editor.setSnapSize: nothing to change — pass a number (the translate size) "
                 "or a map ({translate, rotate, scale}); editor.snapSize() reads all three");
            return QVariantMap();
        }
    } else {
        bool numeric = false;
        const double number = value.toDouble(&numeric);
        if (!numeric) {
            fail(QStringLiteral("editor.setSnapSize: '%1' is neither a size nor a map — pass a "
                                "number (the translate size) or {translate, rotate, scale}")
                     .arg(value.toString()));
            return QVariantMap();
        }
        change.insert(QStringLiteral("translate"), number);
    }

    static const QStringList known = { QStringLiteral("translate"), QStringLiteral("rotate"),
                                       QStringLiteral("scale") };
    const QString refusal = scriptmod::refuseUnknownKeys(QStringLiteral("editor.setSnapSize"),
                                                         change, known);
    if (!refusal.isEmpty()) { fail(refusal); return QVariantMap(); }

    // Validated in full before anything is written: a refused call leaves all
    // three sizes exactly as they were.
    for (auto it = change.constBegin(); it != change.constEnd(); ++it) {
        bool numeric = false;
        const double size = scriptmod::normalizeJs(it.value()).toDouble(&numeric);
        if (!numeric || size <= 0.0) {
            fail(QStringLiteral("editor.setSnapSize: '%1' must be a size > 0, got '%2'")
                     .arg(it.key(), scriptmod::normalizeJs(it.value()).toString()));
            return QVariantMap();
        }
    }
    if (change.contains(QStringLiteral("translate")))
        SnapSettings::setTranslateSize(float(change.value(QStringLiteral("translate")).toDouble()));
    if (change.contains(QStringLiteral("rotate")))
        SnapSettings::setRotateSize(float(change.value(QStringLiteral("rotate")).toDouble()));
    if (change.contains(QStringLiteral("scale")))
        SnapSettings::setScaleSize(float(change.value(QStringLiteral("scale")).toDouble()));
    return snapSize();
}

bool EditorApi::snapToFloor()
{
    if (!requireEngine()) return false;
    if (!host.services || !host.services->selection || !host.services->selection->selected())
        return fail("editor.snapToFloor: nothing is selected");
    return host.viewport->snapSelectionToFloor();
}

QVariantMap EditorApi::editGate()
{
    // Straight off the gate (services/editgate.h) rather than through the undo
    // service: the gate is what the panels, the viewport and the undo spine all
    // ask, so a verb that read a copy of it somewhere else could disagree with
    // the answer the app is actually acting on.
    QVariantMap out;
    out["scriptRunning"] = editgate::runActive();
    out["refusals"]      = QVariant::fromValue(qulonglong(editgate::refusals()));
    out["notice"]        = editgate::noticeShown();
    return out;
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
    // The deferred library work the dying commands queued (CLOSE-1). Zero
    // after every clear; a database-less host reports zero too.
    out["pendingAssetDeletes"] = host.db ? host.db->pendingAssetDeleteCount() : 0;
    // ONE GESTURE, ONE COMMIT (CLOSE-2). `dbBatchDepth` is the counted
    // transaction scope the run holds; `dbCommits` is the process's durable
    // write commits so far, so a script can bracket an action and prove the
    // rows cost one commit instead of one per row.
    out["dbBatchDepth"] = host.db ? host.db->batchDepth() : 0;
    out["dbCommits"]    = QVariant::fromValue(qulonglong(Database::durableCommits()));
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
    // No isPlaying() early-out: a script must ALWAYS be able to force a real
    // stop. The old guard read a flag that could desync from the viewport's
    // own (2026-09-05), making the stuck state unrecoverable from here — and
    // both calls below are no-ops when already stopped anyway.
    host.services->playback->enterEditMode();
    host.viewport->stopPlayingScene();
    return true;
}

bool EditorApi::pause()
{
    if (!host.services || !host.services->playback || !host.viewport)
        return fail("editor: not available in this session");
    // pauseScene() is guarded all the way down (PlayBack::pause early-returns
    // unless it is playing and not already paused), so this is idempotent for
    // the same reason editor.stop() is.
    host.services->playback->pauseScene();
    return true;
}

bool EditorApi::playing()
{
    if (!host.services || !host.services->playback) return false;
    return host.services->playback->isPlaying();
}

bool EditorApi::playEject(const QVariant &on)
{
    if (!host.services || !host.services->playback || !host.viewport)
        return fail("editor: not available in this session");
    // The VIEWPORT's flag, not the service's: it is the one the event handlers
    // branch on (the reason editor.playing() reads it too).
    if (!on.isValid()) return host.viewport->playEjected();
    // A RUN EXISTS, not "a run is stepping" (fix round F5): a PAUSED run is
    // still a run — its physics world, its snapshot and its possession all
    // live on — and ejecting from one is exactly what somebody who paused to
    // look around is asking for.
    if (!host.viewport->playRunLive())
        return fail("editor.playEject: nothing is playing — eject is a state of a run in "
                    "flight, not a setting");
    host.viewport->setPlayEjected(on.toBool());
    return host.viewport->playEjected();
}

QString EditorApi::playInputOwner()
{
    if (!host.viewport) return QStringLiteral("editor");
    return host.viewport->playInputOwner();
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
    // Declared Needs::Engine — enforce it (a headless HeadlessEditorViewport
    // satisfies the null-check but has no shaders to warm; the generated
    // matrix must not lie about this one row).
    if (!requireEngine()) return m;
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


// THE ACTIVE SPACE'S VIEW (audit F4, lane PLAYER-1). The editor and the Player
// are two VIEWS on one scene now, and only one of them is enabled at a time:
// whichever page is up is the one that syncs the document and the one the
// engine renders. `editor.frame` and `editor.screenshot` used to act on the
// editor's view regardless — so a script that entered the player space got
// frames nobody had stepped and screenshots taken through the editor's camera,
// twice over, and called them the player.
//
// The rule is stated once, here: while the PLAYER page is the visible space,
// these two verbs act on the player. `player.frame` / `player.screenshot` are
// still the explicit spelling and nothing about them changes.
bool EditorApi::playerHasTheScreen() const
{
    return host.services && host.services->player && host.services->player->isActive();
}

bool EditorApi::frame(int n, double dt)
{
    if (!requireEngine()) return false;
    if (dt > iris::SimulationClock::kMaxAdvanceSeconds + 1e-9)
        return fail(QStringLiteral("editor.frame: dt %1 s is above the clock's per-frame bound of "
                                   "%2 s (%3 steps of 1/%4); step more frames instead")
                        .arg(dt).arg(iris::SimulationClock::kMaxAdvanceSeconds)
                        .arg(iris::SimulationClock::kMaxStepsPerAdvance - 1)
                        .arg(iris::SimulationClock::kStepHz));
    // The player page owns the screen: step IT (see playerHasTheScreen).
    if (playerHasTheScreen())
        return host.services->player->stepFrames(qBound(1, n, 1000), float(dt)) ||
               fail(QStringLiteral("editor.frame: the player space is active but its view is not "
                                   "ready to render; show the page or use player.frame"));
    host.viewport->renderFrames(qBound(1, n, 1000), float(dt));
    return true;
}

bool EditorApi::key(const QString &name, const QString &action)
{
    QWidget *target = host.viewport ? host.viewport->asWidget() : nullptr;
    if (!target) return fail("editor.key: no editor viewport in this session");
    const QKeySequence seq = QKeySequence::fromString(name, QKeySequence::PortableText);
    if (seq.isEmpty() || seq[0].key() == Qt::Key_unknown)
        return fail(QStringLiteral("editor.key: unknown key '%1'").arg(name));
    const QString act = action.isEmpty() ? QStringLiteral("tap") : action.toLower();
    if (act != QLatin1String("press") && act != QLatin1String("release")
        && act != QLatin1String("tap"))
        return fail(QStringLiteral("editor.key: action must be 'press', 'release' or 'tap' "
                                   "(got '%1')").arg(action));
    const Qt::Key k = seq[0].key();
    const Qt::KeyboardModifiers mods = seq[0].keyboardModifiers();
    if (act != QLatin1String("release")) {
        // THE OVERRIDE FIRST, as Qt's shortcut map asks it of the focus widget:
        // it is where the viewport claims a key for the run while playing.
        QKeyEvent over(QEvent::ShortcutOverride, k, mods);
        over.ignore();
        QApplication::sendEvent(target, &over);
        // A KEY THE VIEWPORT DID NOT CLAIM, THAT IS AN EDITOR SHORTCUT, NEVER
        // REACHES IT: Qt's shortcut map fires the shortcut instead (W is
        // tool.translate while editing). Sending the KeyPress anyway would
        // drive a path no user can take — refuse, and name the shortcut.
        if (!over.isAccepted() && host.mainWindow) {
            const QKeySequence chord(seq[0]);
            if (const auto *reg = host.mainWindow->findChild<ShortcutRegistry *>()) {
                for (const ShortcutRegistry::Entry &e : reg->entries()) {
                    if (!e.shortcut || e.sequence.isEmpty() || e.sequence != chord) continue;
                    return fail(QStringLiteral("editor.key: '%1' is the shortcut %2 here, not a "
                                               "viewport key").arg(name, e.id));
                }
            }
        }
        QKeyEvent press(QEvent::KeyPress, k, mods);
        QApplication::sendEvent(target, &press);
    }
    if (act != QLatin1String("press")) {
        QKeyEvent release(QEvent::KeyRelease, k, mods);
        QApplication::sendEvent(target, &release);
    }
    return true;
}

QVariant EditorApi::dropPointAt(double x, double y)
{
    if (!requireEngine()) return QVariant();
    iris::Vec3 point;
    if (!host.viewport->dropPointAt(QPointF(x, y), &point)) return QVariant();
    QVariantMap out;
    out.insert("x", point.x());
    out.insert("y", point.y());
    out.insert("z", point.z());
    return out;
}

QVariant EditorApi::dropTargetAt(double x, double y)
{
    if (!requireEngine()) return QVariant();
    bool locked = false;
    const iris::SceneNodePtr node = host.viewport->dropTargetAt(QPointF(x, y), &locked);
    if (!node) return QVariant();
    QVariantMap out;
    out.insert("id", node->getGUID());
    out.insert("name", node->getName());
    // A LOCKED node is reported, not hidden (owner correction, 2026-09-15):
    // "there is something here and it will refuse you" is a different answer
    // from "there is nothing here", and only one of them spawns an image plane.
    out.insert("locked", locked);
    return out;
}

bool EditorApi::dragAssetToTray(const QVariant &guidOrGuids, const QString &folderGuid,
                                const QVariantMap &options)
{
    if (!host.mainWindow || !host.mainWindow->assetTray())
        return fail("editor.dragAssetToTray: this verb needs the editor window (a "
                    "--script/--headless run has no tray)");
    AssetWidget *tray = host.mainWindow->assetTray();

    static const QStringList kActions{ QStringLiteral("move"), QStringLiteral("drop") };
    const QString action = options.value(QStringLiteral("action"),
                                         QStringLiteral("drop")).toString().toLower();
    if (!kActions.contains(action))
        return fail(QStringLiteral("editor.dragAssetToTray: action must be one of %1")
                        .arg(kActions.join(QStringLiteral(", "))));

    QStringList guids;
    const QVariant picked = scriptmod::normalizeJs(guidOrGuids);
    if (picked.typeId() == QMetaType::QVariantList) {
        for (const QVariant &value : picked.toList()) guids << value.toString();
    } else {
        guids << picked.toString();
    }
    guids.removeAll(QString());
    if (guids.isEmpty())
        return fail("editor.dragAssetToTray: a guid (or an array of guids) is required");

    const QPoint centre = tray->tileCentre(folderGuid);
    if (centre.isNull())
        return fail(QStringLiteral("editor.dragAssetToTray: the tray is not showing a tile for "
                                   "'%1' (open the folder that holds it, and make sure the "
                                   "Assets tab is in front)").arg(folderGuid));

    QWidget *target = tray->dropTarget();
    if (!target) return fail("editor.dragAssetToTray: the tray has no list viewport");

    // ONE payload builder, the one the tray's own drag uses — a synthesised
    // drag that built its own map would be testing itself.
    const AssetRecord row = host.db ? host.db->fetchAsset(guids.first()) : AssetRecord();
    auto mimeData = [&] {
        return AssetDrag::mimeForMany(row.type, row.name, QString(), guids.first(), guids);
    };

    {
        std::unique_ptr<QMimeData> mime(mimeData());
        QDragEnterEvent event(centre, Qt::MoveAction, mime.get(),
                              Qt::LeftButton, Qt::NoModifier);
        event.ignore();
        QApplication::sendEvent(target, &event);
        // Qt never delivers a move or a drop to a widget that ignored the
        // enter, and neither may this.
        if (!event.isAccepted())
            return refuse(QStringLiteral("editor.dragAssetToTray: the tray refused the drag "
                                         "(the panel takes no asset drag at all)"));
    }
    {
        std::unique_ptr<QMimeData> mime(mimeData());
        QDragMoveEvent event(centre, Qt::MoveAction, mime.get(),
                             Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &event);
        // THE ANSWER THE CURSOR GIVES A PERSON: only a FOLDER tile takes a
        // move, and a drop that the panel would refuse must be refused here
        // too rather than reported as done.
        if (!event.isAccepted())
            return refuse(QStringLiteral("editor.dragAssetToTray: the tray takes no drop on "
                                         "'%1' — only a folder tile does").arg(folderGuid));
    }
    if (action == QLatin1String("move")) return true;
    {
        std::unique_ptr<QMimeData> mime(mimeData());
        QDropEvent event(QPointF(centre), Qt::MoveAction, mime.get(),
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &event);
        if (!event.isAccepted())
            return refuse(QStringLiteral("editor.dragAssetToTray: the tray refused the drop on "
                                         "'%1'").arg(folderGuid));
    }
    // AND THE TURN OF THE LOOP THE DROP DEFERS INTO. The panel does the move
    // on the next event-loop turn on purpose — a drop handler runs inside the
    // drag SOURCE's nested loop, and repopulating the list the source is
    // dragging from is the shape of the sticky-drag defect
    // (ui/panels/singledragowner.h) — so the gesture is not over when the
    // event returns. A person's drop gets that turn for free; a script holds
    // the loop for its whole run, so this verb spends it here and answers
    // once the move has actually happened.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    return true;
}

bool EditorApi::dragAsset(const QString &guid, double x, double y, const QVariantMap &options)
{
    if (!requireEngine()) return false;
    QWidget *target = host.viewport->asWidget();
    if (!target) return fail("editor.dragAsset: the viewport has no widget");

    static const QStringList kActions{ QStringLiteral("move"), QStringLiteral("drop"),
                                       QStringLiteral("leave") };
    const QString action = options.value(QStringLiteral("action"),
                                         QStringLiteral("move")).toString().toLower();
    if (!kActions.contains(action))
        return fail(QStringLiteral("editor.dragAsset: action must be one of %1")
                        .arg(kActions.join(QStringLiteral(", "))));

    const AssetRecord row = host.db ? host.db->fetchAsset(guid) : AssetRecord();
    // A RESERVED PRESET GUID names no row, and is exactly what the presets tray
    // drags — so an absent row is a MATERIAL drag, not a refusal.
    int type = options.value(QStringLiteral("type"), -1).toInt();
    if (type < 0)
        type = row.guid.isEmpty() ? static_cast<int>(ModelTypes::Material) : row.type;

    const QPointF pos(x, y);
    // ONE payload builder, the one every asset view uses (ui/controls/assetdrag.h):
    // a synthesised drag that built its own map would be testing itself.
    // Returns what the widget ANSWERED: Qt never delivers a move or a drop to
    // a widget that ignored the enter, and neither may this (code review, F7).
    auto sendEvent = [&](QEvent::Type kind) -> bool {
        std::unique_ptr<QMimeData> mime(
            AssetDrag::mimeFor(type, row.name, QString(), guid));
        if (kind == QEvent::DragEnter) {
            QDragEnterEvent event(pos.toPoint(), Qt::CopyAction, mime.get(),
                                  Qt::LeftButton, Qt::NoModifier);
            event.ignore();
            QApplication::sendEvent(target, &event);
            return event.isAccepted();
        } else if (kind == QEvent::DragMove) {
            QDragMoveEvent event(pos.toPoint(), Qt::CopyAction, mime.get(),
                                 Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(target, &event);
        } else if (kind == QEvent::Drop) {
            QDropEvent event(pos, Qt::CopyAction, mime.get(),
                             Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(target, &event);
        } else {
            QDragLeaveEvent event;
            QApplication::sendEvent(target, &event);
        }
        return true;
    };

    // A DRAG IS A SEQUENCE, and the handlers depend on having seen its start:
    // dragEnter is what decides whether the payload is a material at all and
    // remembers it for the moves that follow. So a call carrying a DIFFERENT
    // asset is a different gesture — it ends the open one and enters afresh,
    // rather than moving the old payload to a new pixel.
    if (mDragOpen && mDragGuid != guid) {
        sendEvent(QEvent::DragLeave);
        mDragOpen = false;
    }
    if (!mDragOpen) {
        if (!sendEvent(QEvent::DragEnter)) {
            // REFUSED AT THE DOOR, as a person's drag would be: nothing follows,
            // and the answer says so.
            mDragGuid.clear();
            return refuse(QStringLiteral("editor.dragAsset: the viewport refused '%1' (not something it can take a drop of)").arg(guid));
        }
        mDragOpen = true;
        mDragGuid = guid;
    }
    if (action == QStringLiteral("leave")) {
        sendEvent(QEvent::DragLeave);
        mDragOpen = false;
        mDragGuid.clear();
        return true;
    }
    sendEvent(QEvent::DragMove);
    if (action == QStringLiteral("drop")) {
        sendEvent(QEvent::Drop);
        mDragOpen = false;
        mDragGuid.clear();
    }
    return true;
}

QVariantList EditorApi::toolbar()
{
    if (!host.mainWindow) {
        fail("editor.toolbar: this verb needs the editor window");
        return QVariantList();
    }
    return host.mainWindow->toolbarActions();
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
        out.insert("engineScene", false);
        // THE SHAPE IS THE CONTRACT: a caller reading `.pending.gi` must not
        // get an undefined in the session that has no viewport at all.
        out.insert("streaming", false);
        out.insert("pending", QVariantMap{ { QStringLiteral("shaders"), 0u },
                                           { QStringLiteral("textures"), 0u },
                                           { QStringLiteral("gi"), 0u },
                                           { QStringLiteral("shadersThisLoad"), 0u },
                                           { QStringLiteral("texturesThisLoad"), 0u } });
        out.insert("cover", QStringLiteral("none"));
        out.insert("loadingCover", loadingcover::enabled());
        out.insert("indicator", QString());
        out.insert("coversPresented", 0);
        out.insert("blankPresented", 0);
        out.insert("nativeMapped", false);
        out.insert("nativeHides", 0);
        out.insert("rectChanges", 0);
        return out;
    }
    // DOES THE ONE ENGINE SCENE EXIST YET? (SMOKE-FIX-1's fix round.) It is
    // built when a page that DRAWS it asks — the editor viewport's show event,
    // the Player as it is entered, a VR session beginning — and never by a
    // getter, so "has anything asked yet" is a real, observable thing and the
    // only reading that can tell "the Player built it" from "it was already
    // there". A PURE read: asking this question must not answer it.
    out.insert("engineScene", host.viewport->engineScene() != nullptr);
    out.insert("state", host.viewport->presentationState());
    out.insert("framesPresented", QVariant::fromValue(host.viewport->framesPresented()));
    // The ACTUAL render target, straight from the engine — the one number that
    // can prove a resize reached the swapchain (deep audit area 7 F3).
    const QSize target = host.viewport->renderTargetSize();
    // A viewport with no target yet reports QSize() = (-1,-1); the verb's doc
    // says "in pixels" and the no-viewport branch says 0 — agree with them.
    out.insert("width", qMax(0, target.width()));
    out.insert("height", qMax(0, target.height()));
    out.insert("offscreen", host.viewport->isOffscreen());
    // THE FLY'S STATE (ledger §356). A key stuck in the held set moves nothing
    // and logs nothing — Left and Right in it together cancel — so "the arrows
    // are dead" had no reading anywhere. These two make it one.
    out.insert("heldKeys", host.viewport->heldFlyKeys());
    out.insert("flying", host.viewport->flying());
    // WHERE THE VIEWPORT IS INSIDE THE WINDOW (PLAY-SELECT-1). A suite that
    // synthesises a real click has the WINDOW's pixels to aim with (xdotool
    // mousemove --window) and every verb that speaks pixels — gizmoHitTest,
    // dropTargetAt, dropPointAt, editor.screenshot's probes — speaks the
    // VIEWPORT's. Without this the difference is a guess, and a guess at a
    // pixel is how a drag lands on a dock's tab bar instead of the scene
    // (hygiene lane, 2026-09-09). Zero-sized with no window (the offscreen and
    // stand-in viewports), which is honest: there is no window to be inside of.
    const QRect inWindow = host.viewport->widgetRectInWindow();
    out.insert("windowX", inWindow.x());
    out.insert("windowY", inWindow.y());
    out.insert("windowW", inWindow.width());
    out.insert("windowH", inWindow.height());
    // ---- what the world still owes while it streams in ------------------
    // (SPECS/OPEN_COVER_SPEC.md §2.1/§4, lane OPEN-COVER-2b.) `state` above is
    // deliberately UNCHANGED — it is the presentation state machine and its
    // contract is scripting.e2e.presentation_state's — and these are the
    // separate question "is the picture still filling in".
    //
    // `pending.shaders` IS NOT A QUEUE and cannot be: a pipeline state object
    // is generated when a renderable is first DRAWN, so how many are left is
    // not a number anything in this process knows. It is how many the last
    // DRIVER frame compiled — a rate that falls to zero the moment a frame
    // draws without compiling anything, which is the moment the picture stops
    // changing for that reason. The two `*ThisLoad` figures beside it are the
    // progress pair the indicator line shows.
    const IEditorViewport::StreamingPending pending = host.viewport->streamingPending();
    out.insert("streaming", pending.any());
    out.insert("pending", QVariantMap{
                              { QStringLiteral("shaders"), pending.shaders },
                              { QStringLiteral("textures"), pending.textures },
                              { QStringLiteral("gi"), pending.gi },
                              { QStringLiteral("shadersThisLoad"), pending.shadersThisLoad },
                              { QStringLiteral("texturesThisLoad"), pending.texturesThisLoad },
                          });
    // WHICH COVER IS ON SCREEN. The loading cover is a preference and a
    // preference whose whole job is drawing a panel needs a reading, or its
    // test has to photograph the screen.
    out.insert("cover", host.viewport->coverState());
    out.insert("loadingCover", loadingcover::enabled());
    out.insert("indicator", host.viewport->loadingIndicator());
    out.insert("coversPresented", QVariant::fromValue(host.viewport->coversPresented()));
    // THE FRAMES THIS VIEWPORT PRESENTED WITH NO WORLD BOUND (STALE-VIEW-1).
    // The same shape and the same reason as `coversPresented`: a load in place
    // is over before a caller outside the process gets a second reading in, so
    // "did the teardown reach the screen, or did the previous world stay there"
    // is a difference of this across the load.
    out.insert("blankPresented", QVariant::fromValue(host.viewport->blankFramesPresented()));
    // AND THE WINDOW ITSELF (VIEW-REBUILD-1): neither of the two counters above
    // means anything while the viewport's native window is off screen, because
    // nothing presented into it can be seen. `nativeMapped` is the instant;
    // `nativeHides` and `rectChanges` are differenced across an operation.
    out.insert("nativeMapped", host.viewport->nativeMapped());
    out.insert("nativeHides", QVariant::fromValue(host.viewport->nativeHides()));
    out.insert("rectChanges", QVariant::fromValue(host.viewport->rectChanges()));
    return out;
}

bool EditorApi::loadingCover(const QVariant &on)
{
    if (!host.mainWindow) {
        fail("editor.loadingCover: this verb needs the editor window");
        return false;
    }
    // ONE CAPABILITY (services/loadingcover.h): this verb, the Preferences row
    // and the viewport all read and write the same key through the same two
    // functions — the page does not reimplement the default and the viewport
    // does not read QSettings itself.
    if (on.isValid()) loadingcover::setEnabled(on.toBool());
    return loadingcover::enabled();
}

QVariantMap EditorApi::mirrorStats()
{
    // Needs::Document, like viewportState: "this session has no mirror" is one
    // of the answers the verb exists to give, so it must be readable without an
    // engine. That is what `available` is for.
    QVariantMap out;
    const IEditorViewport::MirrorStats s =
        host.viewport ? host.viewport->mirrorStats() : IEditorViewport::MirrorStats{};
    out.insert("available", s.available);
    out.insert("giPushes", QVariant::fromValue(s.giPushes));
    out.insert("giRefreshes", QVariant::fromValue(s.giRefreshes));
    out.insert("giLightRefreshes", QVariant::fromValue(s.giLightRefreshes));
    out.insert("giLightRefreshesAtRest", QVariant::fromValue(s.giLightRefreshesAtRest));
    // MOBILITY (REALTIME_REFLECTIONS_SPEC §3.3): "how many things in this scene
    // does the DOCUMENT say move?". The renderer's side of it is on
    // world.giStatus() (§3.3.4), where R2's counters live.
    out.insert("movableNodes", QVariant::fromValue(s.movableNodes));
    // WHAT THE MIRROR COSTS (MIRROR_SCALE lane): how big the walk is, whether
    // it is rebuilding material descriptions it did not have to, and how much
    // of the scene is out of the renderer's per-frame transform pass.
    out.insert("nodesVisited", QVariant::fromValue(s.nodesVisited));
    out.insert("materialBuilds", QVariant::fromValue(s.materialBuilds));
    out.insert("staticNodes", QVariant::fromValue(s.staticNodes));
    out.insert("staticRepromotions", QVariant::fromValue(s.staticRepromotions));
    // THE DIRTY SET (SPECS/DIRTY_SET_MIRROR_SPEC.md): what the document said
    // changed, what the verifier caught behind it, and which mode ran.
    out.insert("dirtyNodes", QVariant::fromValue(s.dirtyNodes));
    out.insert("evictedNodes", QVariant::fromValue(s.evictedNodes));
    out.insert("verifierVisits", QVariant::fromValue(s.verifierVisits));
    out.insert("verifierCatches", QVariant::fromValue(s.verifierCatches));
    out.insert("pushes", QVariant::fromValue(s.pushes));
    out.insert("walkMode", s.walkMode.isEmpty() ? QStringLiteral("none") : s.walkMode);
    return out;
}

// ---------------------------------------------------------------------------
// THE SCENE-ERROR AREA (services/sceneissues.h) — API FIRST
// ---------------------------------------------------------------------------
// The verbs are the model's whole surface; the viewport's error bar reads the
// same store and adds nothing. All Needs::Document: an issue is a statement
// about the document, and a headless run must be able to make and read one (it
// is how the suite proves the never-repeat rule without a window).
//
// There is no dismiss verb, deliberately (owner, 2026-09-13): the bar shows
// what is wrong and the user fixes it; `clearIssue` — "the condition is gone" —
// is the only way a line ever leaves, and the scanner calls it itself.
QVariantList EditorApi::issues()
{
    return SceneIssues::instance().toVariant();
}

QString EditorApi::raiseIssue(const QVariantMap &issue)
{
    SceneIssue out;
    out.kind = issue.value(QStringLiteral("kind")).toString().trimmed();
    out.message = issue.value(QStringLiteral("message")).toString().trimmed();
    if (out.kind.isEmpty()) {
        fail(QStringLiteral("editor.raiseIssue: 'kind' is required (the issue's class, e.g. "
                            "'sun.tie')"));
        return QString();
    }
    if (out.message.isEmpty()) {
        fail(QStringLiteral("editor.raiseIssue: 'message' is required — an issue with nothing to "
                            "say cannot help anyone"));
        return QString();
    }
    out.node = issue.value(QStringLiteral("node")).toString();
    out.action = issue.value(QStringLiteral("action")).toString();
    out.id = issue.value(QStringLiteral("id")).toString();
    // The name is resolved HERE and stored, so a message keeps naming the thing
    // it was raised about even after the node is renamed or deleted.
    if (!out.node.isEmpty()) {
        if (auto scene = (host.services && host.services->sceneEdit)
                              ? host.services->sceneEdit->scene() : iris::ScenePtr()) {
            auto node = scene->nodes.value(out.node);
            if (node) out.nodeName = node->getName();
        }
    }
    return SceneIssues::instance().raise(out);
}

bool EditorApi::clearIssue(const QString &id) { return SceneIssues::instance().clear(id); }

QVariantMap EditorApi::checkScene()
{
    auto &store = SceneIssues::instance();
    QStringList before;
    for (const auto &i : store.issues()) before << i.id;
    store.scan((host.services && host.services->sceneEdit)
                   ? host.services->sceneEdit->scene() : iris::ScenePtr());
    QVariantList raised;
    for (const auto &i : store.issues())
        if (!before.contains(i.id)) raised.append(i.id);
    return QVariantMap{ { QStringLiteral("issues"), store.count() },
                        { QStringLiteral("raised"), raised },
                        { QStringLiteral("list"), store.toVariant() } };
}

QVariantMap EditorApi::issueBar()
{
    if (!host.mainWindow) {
        // Not a failure: a headless session simply has no bar, and the store
        // verbs are the half that works everywhere.
        return QVariantMap{ { QStringLiteral("editorActive"), false },
                            { QStringLiteral("exists"), false },
                            { QStringLiteral("visible"), false },
                            { QStringLiteral("rows"), 0 },
                            { QStringLiteral("lines"), 0 },
                            { QStringLiteral("buttons"), 0 } };
    }
    // SETTLED, NOT RACED: the shell decides this on a 1 Hz timer, and a script
    // that asked a moment after switching pages would otherwise read the old
    // answer. One pass, then report.
    host.mainWindow->updateSceneIssues();
    return host.mainWindow->sceneIssueBarState();
}

QVariantMap EditorApi::screenshot(const QString &path, int width, int height,
                                  const QVariantList &probes, const QVariant &grade)
{
    QVariantMap out;
    if (!requireEngine()) return out;
    if (path.isEmpty()) { fail("editor.screenshot: a file path is required"); return out; }

    // THE GRADE (IEditorViewport::ScreenshotGrade, where each answer is
    // documented). This argument was a BOOLEAN (`postFx`) and it stays
    // compatible with one — false is Plain, true is Viewport — because the
    // whole pixel-suite corpus passes it that way, and because PLAIN MUST
    // REMAIN THE DEFAULT: this verb is the tree's measuring instrument and its
    // exact colours are what dozens of assertions pin. "scene" is the picture
    // the editor's own Screenshot button takes (SS1).
    IEditorViewport::ScreenshotGrade mode = IEditorViewport::ScreenshotGrade::Plain;
    if (!grade.isNull() && grade.isValid()) {
        const QVariant g = scriptmod::normalizeJs(grade);
        if (g.typeId() == QMetaType::Bool) {
            mode = g.toBool() ? IEditorViewport::ScreenshotGrade::Viewport
                              : IEditorViewport::ScreenshotGrade::Plain;
        } else if (!IEditorViewport::gradeFromString(g.toString(), &mode)) {
            // ONE PARSER for all four verbs (item 5) — a verb whose spellings
            // are narrower than its documentation is how player.screenshot's
            // `scene` branch once shipped unreachable.
            fail(QStringLiteral("editor.screenshot: unknown grade '%1' (%2, or a boolean)")
                     .arg(g.toString(), IEditorViewport::gradeWords()));
            return out;
        }
    }

    // A SCREENSHOT IS THE EDITOR'S PICTURE AT REST (PHOTON-FIELD-ROTATE-1; the
    // product rule): GI that still owes work - a rebuild, a settle injection, the
    // irradiance field's refinement passes - would photograph a picture that is
    // still moving, and two shots of one still scene would differ. Frames are
    // stepped at dt 0 (the document's clock does not move: animation, physics and
    // particles stay at the instant the script asked for) until the ONE settle
    // predicate, world.giStatus().giAtRest, holds - bounded, because a scene whose
    // lights move every frame never comes to rest.
    if (!playerHasTheScreen()) {
        static const int kScreenshotSettleFrames = 2000;
        for (int i = 0; i < kScreenshotSettleFrames && !host.viewport->giStatus().giAtRest; ++i)
            host.viewport->renderFrames(1, 0.0f);
    }

    // The player page owns the screen: photograph IT, through the player's own
    // camera and with the editor's furniture masked out (playerHasTheScreen).
    const QImage img =
        playerHasTheScreen()
            ? host.services->player->screenshot(qBound(16, width, 4096),
                                                qBound(16, height, 4096), int(mode))
            : host.viewport->takeScreenshot(qBound(16, width, 4096),
                                            qBound(16, height, 4096), mode);
    if (img.isNull()) { fail("editor.screenshot: the viewport returned no image"); return out; }

    QFileInfo info(path);
    if (!info.dir().exists()) info.dir().mkpath(".");
    if (!img.save(path, "PNG")) {
        fail(QStringLiteral("editor.screenshot: could not save '%1'").arg(path));
        return out;
    }

    const QColor center = img.pixelColor(img.width() / 2, img.height() / 2);
    // WHICH PICTURE THESE PIXELS ARE, AND IN WHICH COLOUR SPACE
    // (PLAIN-GRADE-1). The plain grade's bytes are LINEAR RADIANCE and the
    // three graded answers are the window's own bytes, measured — a number
    // read in the wrong space is the reading nobody notices is wrong, so the
    // answer says which space it is in rather than leaving it to the caller
    // to remember what the default grade was.
    out["grade"] = IEditorViewport::gradeName(mode);
    out["encoding"] = IEditorViewport::gradeEncoding(mode);
    out["path"] = info.absoluteFilePath();
    out["width"] = img.width();
    out["height"] = img.height();
    out["center"] = QVariantMap{ { "r", center.red() }, { "g", center.green() }, { "b", center.blue() } };

    // Optional probe points in normalized 0..1 image coordinates: the pixel
    // gate for the shipped samples (samples.cleanstart.*) asserts material
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
    // The RUN's macro is lazy (UndoService::beginScriptMacro) — open it first,
    // or this batch would become the outer macro and the run's entry would
    // nest inside the batch instead of the other way round.
    if (host.services && host.services->undo) host.services->undo->ensureScriptMacroOpen();
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
