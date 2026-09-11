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
#include "viewport/ieditorviewport.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/simulationclock.h"
#include "viewport/previewframing.h"
#include "viewport/snapsettings.h"
#include "viewport/flyspeedsettings.h"
#include "scripting/modules/flyspeedverb.h"
#include "shell/mainwindow.h"
#include "ui/panels/assetwidget.h"
#include "ui/panels/scenehierarchywidget.h"
#include "io/sceneformat.h"
#include "services/services.h"
#include "services/playbackservice.h"
#include "services/sceneeditservice.h"
#include "services/clipboardservice.h"
#include "services/selectionservice.h"
#include "services/outlinesettings.h"
#include "services/undoservice.h"
#include "bridge/enginehost.h"
#include "data/settingsmanager.h"

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
        { "gizmoHitTest", "editor.gizmoHitTest(x, y) -> {ring, distancePx, tolerancePx}",
          "WHICH ROTATION RING A VIEWPORT PIXEL HITS, and how far the cursor is from it in "
          "pixels. `x`/`y` are viewport pixels with the origin top-left, exactly as a mouse "
          "event carries them. `ring` is \"x\", \"y\" or \"z\" when the pixel is inside the pick "
          "tolerance of that ring's projected circle, and null when it is not; `distancePx` is "
          "the distance to the NEAREST ring either way, and `tolerancePx` the threshold the "
          "pick used. The rotation gizmo picks in SCREEN SPACE (smoke S15) — a ring seen "
          "edge-on projects to a line, which is still clickable, where the old 3D annulus test "
          "made whichever ring the camera looked along unclickable everywhere. This is the very "
          "function a click takes, so what the verb reports is what the mouse would do. `ring` "
          "is null (and `distancePx` -1) when the rotate gizmo is not the active one, when "
          "nothing is selected, or when this session's viewport has no camera.",
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
        { "flySpeed", "editor.flySpeed() -> {multiplier, base, speed, steps:[...]}",
          "THE EDITOR FLY SPEED, Unreal's model: a `multiplier` on a fixed `base` of 8 world "
          "units per second, so `speed` = base * multiplier is what the RMB fly actually moves "
          "at (before Shift's 3x boost). `steps` is the ladder the toolbar dropdown offers and "
          "the scroll wheel walks while flying. Editor-global and persisted (camera/flySpeedEditor); "
          "the player has its own, player.flySpeed().",
          Needs::Document },
        { "setFlySpeed", "editor.setFlySpeed(multiplier | \"faster\" | \"slower\") -> {multiplier, base, speed, steps:[...]}",
          "Sets the editor fly-speed multiplier and returns the state that resulted (the same "
          "shape editor.flySpeed() reads). A NUMBER is the multiplier itself, clamped to "
          "0.05..32 — it need not be one of the `steps`, which are only what the UI offers. "
          "\"faster\"/\"slower\" step one entry along that ladder, exactly as the scroll wheel "
          "does while the right mouse button is held. The toolbar dropdown follows either way.",
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
          "THE EDITOR'S BOTTOM TRAY, the one widget that carries the asset browser and the "
          "script console as TABS at its top (owner, 2026-09-11: turning the console on adds a "
          "Console tab beside Assets and the two share that widget). `tab` is the tab in front "
          "(\"assets\" or \"console\"), `tabs` the tabs the tab bar is showing, "
          "`consoleVisible` whether the Console tab is in the bar at all, `consoleFocused` "
          "whether the console's INPUT line has the keyboard — the half of Ctrl+` a console "
          "you still have to click does not deliver, `visible` whether the tray widget "
          "itself is on screen, and `title` the tray DOCK's own title — what the bottom tab bar "
          "shows beside \"Timeline\" when the two docks share the bottom area (\"Tray\").",
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
          "DRIVES THAT TRAY. `tab: \"console\"` shows the Console tab, raises the tray and "
          "puts the keyboard in the console input — exactly what the Ctrl+` chord does, "
          "through the same function; `tab: \"assets\"` brings the asset browser forward "
          "without closing the console tab. `console: true|false` adds or removes the Console "
          "tab itself (false returns the tray to Assets). `height: px` resizes the tray (at least 40; a request below the tray's own "
          "minimum content height — ~230 px at 1080 — lands AT that minimum, read the result from "
          "trayTop), and the right column's Presets panel follows so its top stays on the tray's top "
          "line. Called with no argument it reads, "
          "like editor.trayState().",
          Needs::Window },
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
        { "viewportState", "editor.viewportState() -> {state, framesPresented, width, height, offscreen}",
          "What the editor viewport is showing right now. `state` is \"presenting\" (the engine's own frames are on screen), \"loading\" (a world is bound but no frame of it has presented yet — the viewport wears its loading cover), \"noscene\" (no world open, the cover says so) or \"offscreen\" (this session's viewport never reaches a window: headless stand-ins and the macOS offscreen fallback). `framesPresented` counts frames actually drawn AND presented since the current world was bound, so a script can wait for real pixels instead of sleeping. `width`/`height` are the LIVE render target (the swapchain for an on-screen viewport), in pixels — not the size anybody requested, so a script can assert that a resize really took; `offscreen` says whether that target is a texture rather than a window.",
          Needs::Document },
        { "mirrorStats", "editor.mirrorStats() -> {available, giPushes, giRefreshes, giLightRefreshes}",
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
          "NOT. `available` "
          "is false when this session's viewport has no mirror (the document-only stand-ins), and "
          "the counts are then meaningless rather than zero.",
          Needs::Document },
        { "dropPointAt", "editor.dropPointAt(x, y) -> {x, y, z} | null",
          "WHERE A DROP AT THIS VIEWPORT PIXEL LANDS, in world space: the surface under the "
          "cursor when the ray hits one, else the y=0 ground plane. `x`/`y` are viewport pixels "
          "with the origin top-left, exactly as a mouse event carries them. This is the very "
          "function the viewport's drag-and-drop uses to place what you drop (smoke S2), so "
          "`scene.addPrimitive(name, {position: editor.dropPointAt(x, y)})` puts a cube exactly "
          "where dragging one there would. Null when this session's viewport has no camera (the "
          "document-only stand-ins).",
          Needs::Engine },
        { "screenshot", "editor.screenshot(path, w=256, h=256, probes=[], grade=\"raw\") -> {path, width, height, center:{r,g,b}, probes:[{x,y,r,g,b}]}",
          "Offscreen render of the editor scene to a PNG; returns the centre pixel, plus the pixel at each probe point ({x,y} in normalized 0..1 image coordinates), so scripts can assert on colours. Headless-safe. "
          "`grade` says how the shot is DEVELOPED, and the default is deliberately the dullest answer: "
          "\"raw\" (or false) is no post-processing at all — the neutral, exactly-reproducible readback pixel assertions want, and what this verb has always returned. "
          "\"tonemap\" applies the deterministic filmic grade ONLY (fixed exposure, no bloom, no ambient occlusion, no SMAA): a picture of the CONTENT that no longer clips to white wherever the scene is bright, and still the same picture every time. "
          "\"viewport\" (or true) renders the scene's whole post-processing chain so the shot matches what the viewport shows, at the cost of the scene's ADAPTIVE exposure making it depend on how many frames it rendered. "
          "The editor's own Screenshot action uses \"tonemap\".",
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
    // map reaches a script as `undefined`, and `r.ring === null` is how a
    // caller asks "did this pixel miss".
    out.insert("ring", QVariant::fromValue(nullptr));
    out.insert("distancePx", -1.0);
    out.insert("tolerancePx", 0.0);
    if (!requireEngine()) return out;
    const IEditorViewport::GizmoPickResult pick = host.viewport->gizmoHitTest(QPointF(x, y));
    if (!pick.handle.isEmpty()) out.insert("ring", pick.handle);
    out.insert("distancePx", double(pick.distancePx));
    out.insert("tolerancePx", double(pick.tolerancePx));
    return out;
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

// FLY SPEED (owner request 2026-09-07). Verb-first, per the API-first rule: the
// toolbar dropdown and the scroll-wheel gesture both land here, on
// FlySpeedSettings, so there is exactly one value and one clamp. Needs::Document
// deliberately — the setting is editor-global state, not a property of a live
// engine, so a headless run can set it and a suite can assert it with no display.
// The argument grammar is shared with player.setFlySpeed (flyspeedverb.h).
QVariantMap EditorApi::flySpeed()
{
    return flyspeedverb::state(FlySpeedSettings::Editor);
}

QVariantMap EditorApi::setFlySpeed(const QVariant &multiplier)
{
    QString error;
    if (!flyspeedverb::apply(FlySpeedSettings::Editor, multiplier, error)) {
        fail(QStringLiteral("editor.setFlySpeed: %1").arg(error));
        return QVariantMap();
    }
    // The toolbar dropdown is a VIEW of this value; tell the shell so a
    // scripted change moves it, exactly as the wheel gesture does.
    if (host.mainWindow) QMetaObject::invokeMethod(host.mainWindow, "syncFlySpeedUi");
    return flyspeedverb::state(FlySpeedSettings::Editor);
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
    QVariantList tabs{ QStringLiteral("assets") };
    if (host.mainWindow->isConsoleTabVisible()) tabs << QStringLiteral("console");
    out["tab"] = tab;
    out["tabs"] = tabs;
    out["consoleVisible"] = host.mainWindow->isConsoleTabVisible();
    out["consoleFocused"] = host.mainWindow->isConsoleInputFocused();
    out["visible"] = host.mainWindow->isTrayVisible();
    // The dock's title, read off the dock itself (objectName "assetDock", the
    // DockState key): the one string the bottom tab bar is drawn from.
    const auto *dock = host.mainWindow->findChild<QDockWidget *>(QStringLiteral("assetDock"));
    out["title"] = dock ? dock->windowTitle() : QString();
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
        out["trayTop"] = dock->geometry().top();
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
            fail(QStringLiteral("editor.tray: unknown tab '%1' (assets|console)").arg(tab));
            return QVariantMap();
        }
    }
    return trayState();
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

bool EditorApi::frame(int n, double dt)
{
    if (!requireEngine()) return false;
    if (dt > iris::SimulationClock::kMaxAdvanceSeconds + 1e-9)
        return fail(QStringLiteral("editor.frame: dt %1 s is above the clock's per-frame bound of "
                                   "%2 s (%3 steps of 1/%4); step more frames instead")
                        .arg(dt).arg(iris::SimulationClock::kMaxAdvanceSeconds)
                        .arg(iris::SimulationClock::kMaxStepsPerAdvance - 1)
                        .arg(iris::SimulationClock::kStepHz));
    host.viewport->renderFrames(qBound(1, n, 1000), float(dt));
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
    // A viewport with no target yet reports QSize() = (-1,-1); the verb's doc
    // says "in pixels" and the no-viewport branch says 0 — agree with them.
    out.insert("width", qMax(0, target.width()));
    out.insert("height", qMax(0, target.height()));
    out.insert("offscreen", host.viewport->isOffscreen());
    return out;
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
    return out;
}

QVariantMap EditorApi::screenshot(const QString &path, int width, int height,
                                  const QVariantList &probes, const QVariant &grade)
{
    QVariantMap out;
    if (!requireEngine()) return out;
    if (path.isEmpty()) { fail("editor.screenshot: a file path is required"); return out; }

    // THE GRADE (fix wave 2026-09-07 item 6). This argument was a BOOLEAN
    // (`postFx`) and it stays compatible with one — false is Raw, true is
    // Viewport — because the whole pixel-suite corpus passes it that way, and
    // because Raw MUST remain the default: this verb is the tree's measuring
    // instrument and its exact colours are what dozens of assertions pin.
    // "tonemap" is the new third answer: the deterministic filmic grade alone,
    // for a shot that should look like the editor rather than like a readback.
    IEditorViewport::ScreenshotGrade mode = IEditorViewport::ScreenshotGrade::Raw;
    if (!grade.isNull() && grade.isValid()) {
        const QVariant g = scriptmod::normalizeJs(grade);
        if (g.typeId() == QMetaType::Bool) {
            mode = g.toBool() ? IEditorViewport::ScreenshotGrade::Viewport
                              : IEditorViewport::ScreenshotGrade::Raw;
        } else {
            const QString word = g.toString().trimmed().toLower();
            if (word == QLatin1String("raw"))           mode = IEditorViewport::ScreenshotGrade::Raw;
            else if (word == QLatin1String("tonemap"))  mode = IEditorViewport::ScreenshotGrade::Tonemap;
            else if (word == QLatin1String("viewport")) mode = IEditorViewport::ScreenshotGrade::Viewport;
            else {
                fail(QStringLiteral("editor.screenshot: unknown grade '%1' "
                                    "(raw | tonemap | viewport, or a boolean)").arg(g.toString()));
                return out;
            }
        }
    }

    const QImage img = host.viewport->takeScreenshot(qBound(16, width, 4096),
                                                     qBound(16, height, 4096), mode);
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
