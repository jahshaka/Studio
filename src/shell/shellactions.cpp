/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/shellactions.h"

#include <QKeySequence>

#include "data/project.h"
#include "data/settingsmanager.h"
#include "irisgl/document/input/inputmap.h"
#include "jahshaka/engine/Engine.h"
#include "player/playerwidget.h"
#include "services/framemonitor.h"
#include "services/projectservice.h"
#include "services/services.h"
#include "shell/actionhost.h"
#include "shell/editordocks.h"
#include "shell/editorpage.h"
#include "shell/editortoolbar.h"
#include "shell/mainwindow.h"
#include "shell/modulehub.h"
#include "shell/viewcontroller.h"
#include "viewport/ieditorviewport.h"

namespace shellactions {

void define(ActionHost &actions, MainWindow &w)
{
    // EDITOR_SHORTCUTS_SPEC §1: every binding lives in the ShortcutRegistry —
    // persisted overrides (jahsettings.ini "shortcut/<id>"), conflict-checked
    // rebinding, and the generated Preferences → Shortcuts page. Inputs the
    // shortcut system cannot express (RMB-held fly keys, held modifiers,
    // Alt+drag) are registered as fixed rows for discoverability; their
    // handling lives in the viewport's event code.
    //
    // Every row goes through the ActionHost (shell/actionhost.h): it is defined
    // once, and its handler is SPACE-SCOPED — `editor` rows run only while the
    // editor is the space, and a module adds the handler for its own space to
    // the same row (the Materials page's Space and F). The registry itself was
    // made in the constructor, where the modules' rows could join it.
        const QString editor = spaces::id(WindowSpaces::EDITOR);
    const QString player = spaces::id(WindowSpaces::PLAYER);
    const QString any;
    auto row = [&actions](const char *id, const char *label, const char *category,
                          const QKeySequence &keys, const QString &space,
                          const std::function<void()> &run) {
        Contributions::Shortcut r;
        r.id = QString::fromLatin1(id);
        r.label = QString::fromUtf8(label);
        r.category = QString::fromLatin1(category);
        r.keys = keys;
        r.space = space;
        r.run = run;
        actions.addRow(r);
    };
    auto fixed = [&actions](const char *id, const char *label, const char *category,
                            const char *text) {
        Contributions::FixedRow r;
        r.id = QString::fromLatin1(id);
        r.label = QString::fromUtf8(label);
        r.category = QString::fromLatin1(category);
        r.text = QString::fromUtf8(text);
        actions.addFixedRow(r);
    };

    // ---- tools (Unreal keys: W/E/R; T kept as the historical translate key.
    // While RMB is held these keys fly the camera — the viewport withholds
    // them from the shortcut system, see EngineSceneViewport::event) ----
    row("tool.translate", "Translate Tool", "Tools", QKeySequence(Qt::Key_W), editor,
            [&w]() { w.editorToolbar()->translateGizmo(); });
    row("tool.translate.alt", "Translate Tool (alias)", "Tools", QKeySequence(Qt::Key_T), editor,
            [&w]() { w.editorToolbar()->translateGizmo(); });
    row("tool.rotate", "Rotate Tool", "Tools", QKeySequence(Qt::Key_E), editor,
            [&w]() { w.editorToolbar()->rotateGizmo(); });
    row("tool.scale", "Scale Tool", "Tools", QKeySequence(Qt::Key_R), editor,
            [&w]() { w.editorToolbar()->scaleGizmo(); });
    // Space is page-scoped, exactly like Ctrl+Z: ONE registry claimant, routed
    // by the active space — the gizmo cycle here, the Materials module's node
    // search on its own space (its contribution to this row).
    row("tool.cycle", "Cycle Gizmo Mode / Node Search", "Tools", QKeySequence(Qt::Key_Space), editor,
        [&w]() { w.editorToolbar()->cycleGizmoMode(); });

    // ---- camera ----
    // F is page-scoped like Space: ONE registry claimant (the graph view's own
    // QShortcut made it ambiguous on the Materials page — STUDIO-CRUD-1 item 7),
    // routed by the active space: the Materials module frames its graph.
    // (graph.resetZoom, H, is the Materials module's own row, listed after this
    // one — its contribution.)
    row("camera.focus", "Focus Selection / Frame Graph Nodes", "Camera",
        QKeySequence(Qt::Key_F), editor, [&w]() { w.viewport()->focusOnSelection(); });
    row("view.orthographic", "Orthographic Projection", "Camera", QKeySequence(Qt::Key_O), any,
            [&w]() { w.views()->changeProjection(false); });
    row("view.perspective", "Perspective Projection", "Camera", QKeySequence(Qt::Key_P), any,
            [&w]() { w.views()->changeProjection(true); });
    // Canonical axis views (historical X/Y/Z keys, moved out of the arcball
    // controller's raw key handling so they are remappable, listed in
    // Preferences -> Shortcuts, and work in the free camera too). Ctrl+Z
    // stays undo — "back" gets Shift+Z instead.
    row("view.top", "Top View", "Camera", QKeySequence(Qt::Key_Y), editor,
            [&w]() { w.views()->applyCameraView("top"); });
    row("view.bottom", "Bottom View", "Camera", QKeySequence(Qt::CTRL | Qt::Key_Y), editor,
            [&w]() { w.views()->applyCameraView("bottom"); });
    row("view.left", "Left View", "Camera", QKeySequence(Qt::Key_X), editor,
            [&w]() { w.views()->applyCameraView("left"); });
    row("view.right", "Right View", "Camera", QKeySequence(Qt::CTRL | Qt::Key_X), editor,
            [&w]() { w.views()->applyCameraView("right"); });
    row("view.front", "Front View", "Camera", QKeySequence(Qt::Key_Z), editor,
            [&w]() { w.views()->applyCameraView("front"); });
    row("view.back", "Back View", "Camera", QKeySequence(Qt::SHIFT | Qt::Key_Z), editor,
            [&w]() { w.views()->applyCameraView("back"); });
    // The ARROW CLUSTER, not W/A/S/D (owner decision 2026-09-09): the editor's
    // fly moved off the letters so tool shortcuts can have them back. The
    // PLAYER still answers to both spellings — its rows are the Gameplay
    // section below, driven by the InputMap.
    fixed("camera.fly", "Fly Camera (free camera)", "Camera",
                 "RMB (hold) + Arrow keys + PageUp/PageDown \xc2\xb7 Shift: 3x");
    fixed("camera.wheel", "Zoom / Dolly", "Camera", "Mouse Wheel");
    // Held-modifier input, like the fly keys: listed read-only, never a
    // QShortcut. Alt ON the gizmo keeps its duplicate-while-dragging meaning
    // (snap.altdrag below) — the gizmo hit-test runs first.
    fixed("camera.orbit", "Orbit Around Selection", "Camera",
                 "Alt + LMB drag (off the gizmo)");

    // ---- view ----
    row("view.gameView", "Game View (hide editor helpers)", "View", QKeySequence(Qt::Key_G), editor,
            [&w]() { w.viewport()->setGameView(!w.viewport()->isGameView()); });
    row("view.grid", "Toggle Ground Grid", "View", QKeySequence(), any,
            [&w]() { if (w.viewport()) w.viewport()->setShowGrid(!w.viewport()->getShowGrid()); });
    // F3 — the games convention (Minecraft, idTech-adjacent), and the only free
    // F-key in this registry besides F11 (STATS_OVERLAY_SPEC D3). Category
    // "View" so it lands beside gameView/grid/fullscreen in the generated
    // Preferences page. Goes through the same verb path as the checkbox and
    // never a separate one — and persists, because a diagnostic you have to
    // switch on again after every restart is a diagnostic nobody uses.
    row("view.stats", "Show Frame Stats", "View", QKeySequence(Qt::Key_F3), any,
            [&w]() { w.editorPage()->setShowFrameStats(!w.viewport()->getShowFps()); });
    // F6 — THE ATOM VIEW, cycled Off -> Triangles -> Levels -> Buckets -> Objects
    // -> Off (the View Options sub-menu picks one directly).
    row("view.atomView", "Cycle Atom View", "View", QKeySequence(Qt::Key_F6), any,
            [&w]() { w.editorPage()->setAtomViewMode((w.editorPage()->atomViewMode() + 1) % 5); });
    // F7 — THE PHOTON VIEW, cycled Off -> Voxels -> ... -> Ray Hits -> Off through
    // the modes that can paint here (the View Options sub-menu picks one directly).
    row("view.photonView", "Cycle Photon View", "View", QKeySequence(Qt::Key_F7), any,
            [&w]() {
                jahshaka::engine::Scene *es = w.viewport() ? w.viewport()->engineScene() : nullptr;
                if (!es) return;
                const int n = jahshaka::engine::kPhotonViewCount;
                int next = w.editorPage()->photonViewMode();
                for (int step = 0; step < n; ++step) {
                    next = (next + 1) % n;
                    if (next == 0 || es->photonViewRefusal(
                                         static_cast<jahshaka::engine::PhotonView>(next)).empty())
                        break;
                }
                w.editorPage()->setPhotonViewMode(next);
            });
    row("window.fullscreen", "Immersive Fullscreen", "View", QKeySequence(Qt::Key_F11), any,
            [&w]() { w.views()->toggleImmersiveFullscreen(); });
    // Ctrl+F4 — THE CAPTURE KEY (owner, 2026-09-12: "I would prefer to activate
    // the monitor Ctrl+F4 and then it captures the next 20 seconds of data for
    // you"). EDITOR ONLY, and deliberately: the monitor's scope is the editor
    // viewport's frame and everything it drives (RENDER_LOOP_MONITOR_SPEC
    // SCOPE). Pressed again while recording it stops early and writes what it
    // has. It goes through perf.capture / perf.stop — the same verbs a script
    // and the MCP tool call — never a second path (SCRIPTING_SPEC §2.3).
    //
    // NOTHING IS DRAWN by this beyond the two toasts wired in
    // connectFrameMonitorToasts(): an on-screen display would itself cost frame
    // time and passes and contaminate what the capture measures.
    row("perf.capture", "Capture Render Monitor Data (20 s)", "View",
        QKeySequence(Qt::CTRL | Qt::Key_F4), editor, [&w]() {
                if (FrameMonitor::instance().isRecording()) { FrameMonitor::instance().stop(); return; }
                FrameMonitor::Request request;
                if (w.currentProject()) request.label = w.currentProject()->getProjectName();
                QString error;
                if (!FrameMonitor::instance().start(request, &error))
                    w.showViewportToast(QObject::tr("Render Monitor"), error);
            });

    // ---- playback (Space is the gizmo cycle now — Unreal PIE puts play on
    // Alt+P; the toolbar Play button is unchanged) ----
    row("play.toggle", "Play / Stop Scene", "Playback",
        QKeySequence(Qt::ALT | Qt::Key_P), editor, [&w]() { w.editorPage()->onPlaySceneButton(); });
    actions.handle(QStringLiteral("play.toggle"), player, [&w]() { w.playerPage()->onPlayScene(); });

    // F8 — EJECT (PLAY-SELECT-1, owner R13). Unreal's key, and free in this
    // registry (the only other F-keys here are F3 and F11). It hands the mouse
    // and the keyboard back to the editor WITHOUT stopping the run; pressed
    // again it gives them back to the run. Editor space only, and only while
    // something is playing — said out loud either way, because an eject that
    // changes nothing visible is indistinguishable from a dead key.
    //
    // ONE PATH with `editor.playEject` (SCRIPTING_SPEC §2.3): both this lambda
    // and the verb set the VIEWPORT's latch, which is the flag its event
    // handlers branch on.
    row("play.eject", "Eject (editor input during play)", "Playback",
        QKeySequence(Qt::Key_F8), editor, [&w]() {
                if (!w.viewport()) return;
                if (!w.viewport()->isPlaying()) return;
                const bool ejected = !w.viewport()->playEjected();
                w.viewport()->setPlayEjected(ejected);
                w.showViewportToast(ejected ? QObject::tr("Ejected") : QObject::tr("Possessed"),
                                  ejected ? QObject::tr("The editor has the input; the scene keeps playing.")
                                          : QObject::tr("Input is back with the running scene."));
            });

    // ---- snapping (SnapSettings, EDITOR_SHORTCUTS_SPEC §4) ----
    row("snap.decrease", "Decrease Snap / Grid Size", "Snapping", QKeySequence(Qt::Key_BracketLeft),
        any, [&w]() { w.editorToolbar()->stepSnapSize(-1); });
    row("snap.increase", "Increase Snap / Grid Size", "Snapping", QKeySequence(Qt::Key_BracketRight),
        any, [&w]() { w.editorToolbar()->stepSnapSize(+1); });
    row("snap.floor", "Snap Selection To Floor", "Snapping", QKeySequence(Qt::Key_End), editor,
            [&w]() { w.viewport()->snapSelectionToFloor(); });
    fixed("snap.relative", "Snap While Dragging", "Snapping", "Ctrl (hold)");
    fixed("snap.altdrag", "Duplicate While Dragging", "Snapping", "Alt + drag gizmo");
    fixed("snap.vertex", "Snap To Vertex", "Snapping", "V (hold) while moving");

    // ---- editing ----
    // Ctrl+Z/Ctrl+Shift+Z had been DEAD since the menubar went away: the .ui's
    // actionEditUndo/actionEditRedo carried the QKeySequence but were attached
    // to no widget, so the shortcut never fired (the toolbar buttons were the
    // only working trigger). Registered here like every other binding.
    // Redo is explicit Ctrl+Shift+Z — QKeySequence::Redo's Ctrl+Y alternate
    // would collide with view.bottom.
    // The ONE claimant for each chord — see undoActiveSpace() for why that
    // matters and which stack each space owns.
    row("edit.undo", "Undo", "Editing", QKeySequence(Qt::CTRL | Qt::Key_Z), any,
            [&w]() { w.undoActiveSpace(); });
    row("edit.redo", "Redo", "Editing", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), any,
            [&w]() { w.redoActiveSpace(); });

    // Delete / Ctrl+D / Ctrl+C / Ctrl+V (EDITOR_MULTISELECT_SPEC §2.6). Same
    // single-claimant rule as Ctrl+Z, for the same measured reason: the
    // Materials graph used to own bare WindowShortcuts on exactly these four
    // chords, and two WindowShortcut claimants make Qt drop the chord entirely.
    // The registry is the one claimant and the ACTIVE SPACE decides what it
    // means — the editor's selection SET here, the node graph there.
    //
    // A text field is safe: QLineEdit/QTextEdit accept the ShortcutOverride for
    // their standard editing keys, so a WindowShortcut never fires while one
    // has focus (the tree's inline rename editor is the case that matters, and
    // app.input_keys probes it on the rig).
    //
    // The ACTIVE SPACE's edit target answers (ModuleHub::runEdit): the editor's
    // selection SET, the Materials graph, or nothing — deliberately NOT a
    // fallback, so a chord never acts on a selection the user cannot see.
    using Edit = ModuleHub::Edit;
    row("edit.delete", "Delete Selection", "Editing", QKeySequence(Qt::Key_Delete), any,
        [&w]() { w.hub()->runEdit(w.currentSpaceName(), Edit::Delete); });
    row("edit.duplicate", "Duplicate Selection", "Editing", QKeySequence(Qt::CTRL | Qt::Key_D), any,
        [&w]() { w.hub()->runEdit(w.currentSpaceName(), Edit::Duplicate); });
    row("edit.copy", "Copy Selection", "Editing", QKeySequence(Qt::CTRL | Qt::Key_C), any,
        [&w]() { w.hub()->runEdit(w.currentSpaceName(), Edit::Copy); });
    // Ctrl+X. The third chord of the set, and the one that was missing: a
    // clipboard whose copy travels to another instance but whose CUT does not
    // exist is half a clipboard. Same single-claimant routing, same text-field
    // rule as the two above (a focused QLineEdit accepts the ShortcutOverride
    // for Cut before a WindowShortcut can fire).
    row("edit.cut", "Cut Selection", "Editing", QKeySequence(Qt::CTRL | Qt::Key_X), any,
        [&w]() { w.hub()->runEdit(w.currentSpaceName(), Edit::Cut); });
    row("edit.paste", "Paste", "Editing", QKeySequence(Qt::CTRL | Qt::Key_V), any,
        [&w]() { w.hub()->runEdit(w.currentSpaceName(), Edit::Paste); });
    // Ctrl+A (EDITOR_MULTISELECT_SPEC §8.7, decided 2026-09-09). Same
    // single-claimant routing as the four chords above — and the same TEXT
    // FIELD rule, made explicit rather than left to Qt: selectAllActiveSpace
    // hands the chord to a focused QLineEdit/QTextEdit/QPlainTextEdit/spin box
    // instead of the scene, so Ctrl+A in the console input, an inline rename or
    // a transform field selects THAT text. Qt's own ShortcutOverride usually
    // gets there first (QWidgetLineControl accepts QKeySequence::SelectAll),
    // but "usually" is not a contract to hang the scene selection on.
    row("edit.selectAll", "Select All", "Editing", QKeySequence(Qt::CTRL | Qt::Key_A), any,
            [&w]() { w.selectAllActiveSpace(); });

    // ---- file / windows ----
    row("file.save", "Save Scene", "File", QKeySequence(Qt::CTRL | Qt::Key_S), any,
            [&w]() { w.saveScene(); });
    // Ctrl+` = the Console TAB of the bottom tray (smoke S1). One function for
    // the chord and for `editor.tray`, so the verb the suites drive is the code
    // path the key takes: show the tab, raise the tray, AND put the keyboard in
    // the input line (Ctrl+` used to open a console that still needed a mouse
    // click before it would take a character, which also meant the chord rules
    // the console is the natural place to exercise — Ctrl+A belongs to a
    // focused text field — could not be reached from the keyboard at all).
    row("console.toggle", "Script Console", "Windows",
            QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft), any,
            [&w]() { w.editorDocks()->toggleScriptConsole(); });
    // (claude.toggle, Ctrl+Shift+C, is the Claude assistant's own row, listed
    // after this one — its contribution.)
    // THE RIGHT COLUMN'S TWO TABS (PROPERTY_FILTER_SPEC D2): one toggle, not two
    // keys. Ctrl+Tab is taken by space.previous, so Ctrl+Shift+P — verified
    // free against the 50 rows already registered here, and remappable in
    // Preferences → Shortcuts like every other row. (ShortcutRegistry's
    // conflict check runs on a USER rebinding, not on these defaults: two
    // defaults claiming one chord would simply both be registered, so the
    // default above was checked by hand.)
    row("properties.tab", "Properties: World / Selection Tab", "Windows",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), any, [&w]() { w.editorDocks()->togglePropertiesTab(); });
    // THE PROPERTY FILTER'S BOX (PROPERTY_FILTER_SPEC D1): Ctrl+F, which is the
    // universal find key and was free in the registry — the only "Ctrl+F" in
    // src/ is the Ctrl+F4 render-capture tooltip, and plain F (camera.focus) is
    // a different chord. It focuses the box of the tab ON SCREEN, since each
    // tab has its own filter. (A QLineEdit accepts the ShortcutOverride for
    // unmodified printable keys, so typing "f" into the box does not fire
    // camera.focus.)
    row("properties.filter", "Properties: Filter Rows", "Windows",
            QKeySequence(Qt::CTRL | Qt::Key_F), any, [&w]() { w.editorDocks()->focusPropertiesFilter(); });
    // Esc is a widget-level key inside the box, not a registry binding — the
    // row exists so the Preferences table says so.
    fixed("properties.filter.clear", "Properties: Clear the Filter", "Windows",
                 "Esc (while the filter box has focus)");
    // (vr.toggle, Ctrl+Shift+V, is the VR module's own row, listed after this
    // one — its contribution.)
    row("space.desktop", "Desktop Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_1), any,
            [&w]() { w.switchSpace(WindowSpaces::DESKTOP); });
    row("space.player", "Player Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_2), any,
            [&w]() { if (w.studioServices()->project->isSceneOpen()) w.switchSpace(WindowSpaces::PLAYER); });
    row("space.editor", "Editor Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_3), any,
            [&w]() { if (w.studioServices()->project->isSceneOpen()) w.switchSpace(WindowSpaces::EDITOR); });
    row("space.effects", "Effects Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_4), any,
            [&w]() { w.switchSpace(WindowSpaces::EFFECT); });
    row("space.assets", "Assets Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_5), any,
            [&w]() { w.switchSpace(WindowSpaces::ASSETS); });
    row("space.previous", "Previous Space", "Windows", QKeySequence(Qt::CTRL | Qt::Key_Tab), any,
            [&w]() {
                if ((w.previousWindowSpace() == WindowSpaces::PLAYER || w.previousWindowSpace() == WindowSpaces::EDITOR) &&
                    !w.studioServices()->project->isSceneOpen())
                    return;
                w.switchSpace(w.previousWindowSpace());
            });

    // ---- gameplay (AVATAR_LOCOMOTION_SPEC §8.2) ----
    // FIXED rows on purpose. These four are not QShortcuts and must never
    // become any: they are HELD, combined and polled (W+A is a diagonal, Shift
    // is a modifier held for seconds), they only exist while the scene is
    // playing, and a QShortcut on W is exactly what stops W from reaching play
    // mode today. The rebindable half lives in the InputMap — `input.bind`
    // writes it and refreshGameplayShortcutRows() re-labels these rows.
    iris::InputSystem::instance().setSettings(w.getSettingsManager()->settings);
    fixed("gameplay.move",   "Move (play mode)",   "Gameplay", "W / S / A / D");
    fixed("gameplay.look",   "Look (play mode)",   "Gameplay", "Mouse");
    fixed("gameplay.jump",   "Jump (play mode)",   "Gameplay", "Space");
    fixed("gameplay.sprint", "Sprint (play mode)", "Gameplay", "Shift");
}

}   // namespace shellactions
