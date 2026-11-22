/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/projectapi.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QFileInfo>
#include <QStandardPaths>

#include "export/exportservice.h"
#include "export/previewlauncher.h"
#include "export/exportcontentsource.h"
#include "export/rawexporter.h"
#include "scripting/modules/moduleshared.h"
#include "services/assetmetadata.h"
#include "services/sceneeditservice.h"

#include "data/database/database.h"
#include "data/database/dbqt.h"
#include "data/project.h"
#include "ui/ishellview.h"
#include "services/projectservice.h"
#include "services/loadtimeline.h"
#include "services/ambienceservice.h"
#include "services/services.h"
#include "ui/pages/projectmanager.h"
#include <QPointer>
#include "services/projectarchiver.h"
#include "services/sceneextents.h"
#include "services/scenetemplate.h"
#include "viewport/ieditorviewport.h"

namespace {
/// The Desktop page, through the shell (null in a session with no window).
ProjectManager *desktopPage(const ScriptHost &host)
{
    return host.shell ? host.shell->projectPage() : nullptr;
}
}   // namespace

QVector<VerbInfo> ProjectApi::verbs() const
{
    return {
        { "create", "project.create(name, {template, location, discard}) -> guid",
          "THE WORLD THAT IS OPEN IS CLOSED FIRST, AND NO CLOSE DROPS EDITS SILENTLY (STUDIO-HYGIENE-1): with auto-save on "
          "it is saved, and a save that FAILS refuses the call (the world stays open, the reason in the error); "
          "with auto-save off and unsaved changes (editor.undoState().dirty) the call is REFUSED with a "
          "sentence unless `discard: true` is passed, which drops them. "
          "Creates a project (folder, DB row, default scene saved into the blob) on the current desktop and "
          "opens it in the editor. The scene is in the row when this returns; the row's THUMBNAIL is encoded "
          "on a worker and lands ~100 ms later (the same holds for the world a create closes; project.save "
          "writes its thumbnail synchronously). INSIDE A SCRIPT this ends the run's undo entry first: everything the run "
          "did up to here becomes one undo step of the project being left, whose stack is then cleared with "
          "it, and the rest of the run records into a fresh entry in the new project.\n\n"
          "`template` picks what the new world holds (default \"basic\"): \"basic\" is ONE FLOOR — an "
          "ordinary cube node named Floor, 100 x 1 x 100 m with its top face at y = 0, wearing the default "
          "floor material — the sun (a directional light), a Sky Light and the realistic real-time sky with "
          "the sun following the atmosphere; \"world\" is Basic's sky and lights on a group \"World Floor\" "
          "of 25 of Basic's floor cubes (\"Floor 1\"..\"Floor 25\"), 5 x 5, edge to edge, centred on the "
          "origin (500 m square); \"empty\" is NOTHING — the root node and the Epic world mode, no floor, "
          "no lights and no sky (a black single-colour sky, which lights nothing). Any other value is refused "
          "by name. Every floor (and World's group) is an ordinary node that SHIPS LOCKED — not "
          "pickable, so a click on the empty floor selects nothing and a material drop on it is "
          "refused by name (editor.clickTargetAt / editor.dropTargetAt); unlock it with "
          "node.setProperty(id, \"pickable\", true) or the outliner's lock, then select, move, "
          "delete or re-material it like any node. "
          "`location` is the folder the project's own directory is created under; omitted, it is the user's "
          "projects root (the Jahshaka documents folder, or the run's --data-root). A location that does not "
          "exist, is not a folder or is not writable is REFUSED BY NAME — no project row is written and "
          "nothing is left pointing at a folder that was never made. These are the New Scene dialog's two "
          "controls: its Template drop-down and its Browse button call this verb. "
          "A NAME ANOTHER PROJECT CARRIES (compared case-insensitively) IS REFUSED BY NAME, with the free "
          "name offered — \"a project named 'A' already exists; 'A 2' is free\" (project.nextFreeName); "
          "projects are identified by guid, and a name is a label no two of them share."
          "\n\nTHE WORLD'S LIGHTING IS BUILT INSIDE THE LOAD (SPECS/BUGS_6_SPEC.md stage 1). A "
          "world's FIRST global-illumination arm — the voxel cascades, the reflection-probe grid, the "
          "irradiance field — is the longest thing the engine does on the UI thread; it is built on the "
          "load's own frames once the world is installed (its geometry, sky, world settings and camera "
          "in the renderer), before the shader warm-up and the reveal, because built after the reveal "
          "it landed on the first frame the user sees (298 ms on the default world). It still waits for "
          "what it reads — a camera a drawing view has tracked, the voxel inputs' textures, the sky, "
          "the meshes the mirror still owes — so a world whose inputs land later is lit on the frames "
          "after this returns: `world.giStatus().vctBound` says which.",
          Needs::Document },
        { "createAsync", "project.createAsync(name, {template, location, discard}) -> guid",
          "THE WORLD THAT IS OPEN IS CLOSED FIRST, AND NO CLOSE DROPS EDITS SILENTLY (STUDIO-HYGIENE-1): with auto-save on "
          "it is saved, and a save that FAILS refuses the call (the world stays open, the reason in the error); "
          "with auto-save off and unsaved changes (editor.undoState().dirty) the call is REFUSED with a "
          "sentence unless `discard: true` is passed, which drops them. "
          "THE CREATE, WITHOUT THE WAIT (SPECS/OPEN_COVER_SPEC.md §2 C/§4). Exactly what "
          "project.create does — the same folder, the same DB row, the same default world, the "
          "same slices in the same order — except that it returns as soon as the create is "
          "under way instead of pumping the event loop until the world is installed. Poll "
          "project.openState() for completion (it covers creates and opens alike: one runner "
          "does both), with a frame in the loop, exactly as project.openAsync's doc says.\n\n"
          "The guid is returned immediately and is real the moment this returns — the project "
          "row is written before the first slice runs, which is why a create can be asynchronous "
          "and still hand back an identity. What is NOT there yet is the world: scene.nodes() "
          "reads the old world (or none) until project.openState() says 'idle'.\n\n"
          "project.create keeps its synchronous contract for the scripts and suites written "
          "against it, and the New Scene dialog keeps using it; this exists for a caller that "
          "wants to drive the frames of a create itself — which is the only way to watch a "
          "world stream in (editor.viewportState().pending). `template` and `location` mean exactly "
          "what they mean on project.create, and an open or create already in flight is REFUSED "
          "by name rather than queued.",
          Needs::Window },
        { "open", "project.open(guidOrName, {discard}) -> bool",
          "THE WORLD THAT IS OPEN IS CLOSED FIRST, AND NO CLOSE DROPS EDITS SILENTLY (STUDIO-HYGIENE-1): with auto-save on "
          "it is saved, and a save that FAILS refuses the call (the world stays open, the reason in the error); "
          "with auto-save off and unsaved changes (editor.undoState().dirty) the call is REFUSED with a "
          "sentence unless `discard: true` is passed, which drops them. "
          "Opens a project by guid or exact name: preloads its assets synchronously, reads the scene blob, "
          "switches to the editor. INSIDE A SCRIPT this ends the run's undo entry first (see project.create): "
          "the closed project's undo history goes with it, and the rest of the run records into a fresh entry."
          "\n\nTHE WORLD'S LIGHTING IS BUILT INSIDE THE LOAD (SPECS/BUGS_6_SPEC.md stage 1). A "
          "world's FIRST global-illumination arm — the voxel cascades, the reflection-probe grid, the "
          "irradiance field — is the longest thing the engine does on the UI thread; it is built on the "
          "load's own frames once the world is installed (its geometry, sky, world settings and camera "
          "in the renderer), before the shader warm-up and the reveal, because built after the reveal "
          "it landed on the first frame the user sees (298 ms on the default world). It still waits for "
          "what it reads — a camera a drawing view has tracked, the voxel inputs' textures, the sky, "
          "the meshes the mirror still owes — so a world whose inputs land later is lit on the frames "
          "after this returns: `world.giStatus().vctBound` says which.\n\n"
          "A SCENE THIS BUILD MUST NOT READ IS REFUSED, nothing opened, before anything of the open is torn "
          "down: a scene blob that does not parse, or one ANOTHER build wrote (a formatVersion that is not this "
          "build's — newer, older, or none at all) — the error is \"'<name>' could not be opened: <why>\", the same sentence a desktop tile "
          "or an archive import shows. The world that was open is already closed by then (the close is the "
          "open's first half).",
          Needs::Document },
        { "openAsync", "project.openAsync(guidOrName, {play, discard}) -> bool",
          "THE WORLD THAT IS OPEN IS CLOSED FIRST, AND NO CLOSE DROPS EDITS SILENTLY (STUDIO-HYGIENE-1): with auto-save on "
          "it is saved, and a save that FAILS refuses the call (the world stays open, the reason in the error); "
          "with auto-save off and unsaved changes (editor.undoState().dirty) the call is REFUSED with a "
          "sentence unless `discard: true` is passed, which drops them. "
          "Opens a project WITHOUT blocking the UI thread: the model files parse on a worker thread and the "
          "install runs one slice per event-loop turn (services/sceneopenrunner.h), which is what the desktop "
          "tile and the archive-import open now do. Returns as soon as the open is under way — poll "
          "project.openState() for completion. Needs a window; headless sessions get project.open's "
          "synchronous behaviour. Inside a script it ends the run's undo entry first, like project.open.\n\n"
          "`play` (default false) is WHICH SPACE THE OPEN LANDS IN: false the editor, true the Player — the "
          "desktop tile's Play button, which until SMOKE-FIX-1 was the only route that could say it and said "
          "it by leaving a flag on the page for every other route to read. It is an argument here for the "
          "same reason.",
          Needs::Window },
        { "openSample", "project.openSample(name, {discard}) -> bool",
          "Opens one of the samples this tree ships, by base name (\"Matcaps\", \"Showroom 2\") or by an "
          "Ogre port's display title — THE SAMPLE BROWSER'S OWN ROUTE, which imports the archive and then "
          "opens the imported world. The dialog's tiles take the same import (ProjectManager::openSampleArchive), "
          "so a script and a double-click take the same road; a tile asks a person about unsaved changes. It lands in the EDITOR: a sample is something to look at and edit, and the Player is a "
          "separate statement (app.space('player')). BOTH HALVES ARE THREADED, like the browser's, and "
          "they run in that order: poll project.archiveState() until 'idle' (the import), then "
          "project.openState() until 'idle' (the open), with a frame in the loop. project.samples() "
          "lists the names. A REOPENED SAMPLE IS ALWAYS A FRESH COPY, UNIQUELY NAMED: the first open of "
          "Matcaps makes \"Matcaps\", the next \"Matcaps 2\", then \"Matcaps 3\" (project.nextFreeName's "
          "rule; every archive import names its project the same way). THE OPEN WORLD IS SETTLED FIRST, as "
          "project.open settles it: an unknown name is refused with the world untouched; then unsaved changes "
          "refuse the open with project.open's sentence unless {discard: true} (auto-save on: they are saved, and "
          "a failed save refuses), and only then does the import start.",
          Needs::Window },
        { "samples", "project.samples() -> [name]",
          "The base names of every sample scene this tree ships, both sets (the Jahshaka samples and our "
          "ports of Ogre's), sorted — what project.openSample accepts.",
          Needs::Document },
        { "openState", "project.openState() -> 'idle' | 'opening'",
          "Whether an asynchronous open (project.openAsync, a desktop tile, an archive import) is still in "
          "flight. THE SAME PREDICATE project.openAsync refuses on, so the two can never disagree: while "
          "this reads 'opening' a second openAsync is refused, and while it reads 'idle' one is accepted. "
          "POLL IT WITH A FRAME IN THE LOOP. The install runs one slice per event-loop turn, and a tight "
          "poll is a chain of posted events that outranks the app's render timer in Qt's dispatcher — so a "
          "loop of bare openState() calls starves the very install it is waiting for and needs MANY more "
          "turns to finish (measured: >1500 on a two-world script). Call editor.frame(1) in the poll body, "
          "or sleep. The open itself is safe either way — the runner advances the renderer's resource "
          "bookkeeping at every slice boundary whether or not anything drew (app.renderStats() "
          ".resourceAdvances) — but a poll with no frame is slow and paints nothing.",
          Needs::Window },
        { "openRefusal", "project.openRefusal() -> string",
          "WHY THE LAST OPEN WAS REFUSED, in the user's words — \"\" unless the most recent open (either "
          "route: project.open, project.openAsync, a desktop tile, an archive import's open) was refused "
          "(STUDIO-HYGIENE-1). The sentence is the one project.open fails with and the tile's box shows: "
          "\"'<name>' could not be opened: <why>\" — a scene blob that does not parse, or one another build "
          "wrote. A threaded open refuses at its first slice, so poll project.openState() to 'idle' first.",
          Needs::Document },
        { "save", "project.save() -> bool",
          "Saves the open scene into the project's DB blob. Works headless (blob-only; the thumbnail refreshes only when a viewport can render one).",
          Needs::Document },
        { "close", "project.close({discard}) -> bool",
          "THE WORLD THAT IS OPEN IS CLOSED FIRST, AND NO CLOSE DROPS EDITS SILENTLY (STUDIO-HYGIENE-1): with auto-save on "
          "it is saved, and a save that FAILS refuses the call (the world stays open, the reason in the error); "
          "with auto-save off and unsaved changes (editor.undoState().dirty) the call is REFUSED with a "
          "sentence unless `discard: true` is passed, which drops them. "
          "Closes the open project — INCLUDING one that is still opening: a threaded open in flight is "
          "drained first (its install finishes, then the world closes), so a close can never leave install "
          "slices queued against a project that is gone. "
          "Closes the open project (physics restored, autosave per settings, undo stack reset) and returns to "
          "the desktop. INSIDE A SCRIPT the run's undo entry is ended first, so the stack really is cleared: "
          "the run's edits become one undo step of the project being closed and die with it, and the rest of "
          "the run records into a fresh entry. Nothing on the stack ever names a closed project's nodes.",
          Needs::Document },
        { "rename", "project.rename(guid, newName) -> bool",
          "Renames a project (its row, the open project's caption, its Desktop tile). REFUSED BY NAME: an "
          "empty name, an unknown guid, and a name another project carries (case-insensitive) — the "
          "refusal names the free name (project.nextFreeName). A case-only change of the project's own "
          "name is allowed. The guid never changes.",
          Needs::Document },
        { "nextFreeName", "project.nextFreeName(name) -> string",
          "THE PROJECT NAME RULE: `name` (trimmed) when no project carries it, else the first free "
          "\"<name> N\" from N = 2 (\"Matcaps\" -> \"Matcaps 2\" -> \"Matcaps 3\"). A trailing number is read "
          "as a copy number only when the name is a copy this rule made — its stem and every \"<stem> k\" "
          "below it taken, an unbroken chain — so a taken \"Matcaps 2\" continues at \"Matcaps 3\", while "
          "\"Area 51\" is the user's own name and continues at \"Area 51 2\" even with \"Area\" taken. "
          "What project.create and project.rename offer when they refuse, what the New Scene and Rename "
          "dialogs put in their name box, and what an archive import (a sample reopen) names its project.",
          Needs::Document },
        { "remove", "project.remove(guid) -> bool",
          "Deletes a project: its folder tree, its DB row and its asset/dependency rows — THE ONE DELETE, the Desktop "
          "tile's Delete calls the same function. Refused, by a sentence: an unknown guid, the OPEN project "
          "(project.close() first), and a project whose recorded location is not there. The folder is renamed "
          "aside at once (gone from its place when this returns; a folder that cannot be moved refuses with "
          "the rows kept) and its bytes are removed on a worker, so a large export never freezes the window. "
          "NOT undoable.",
          Needs::Document },
        { "list", "project.list({desktop}) -> [{guid, name, desktop, x, y}]",
          "Lists projects; desktop 1-4 filters, omit for all.",
          Needs::Document },
        { "moveToDesktop", "project.moveToDesktop(guid, desktop) -> bool",
          "Moves a project tile to desktop 1-4.",
          Needs::Document },
        { "setPosition", "project.setPosition(guid, x, y) -> bool",
          "Sets a tile's freeform position (normalized 0..1) on its desktop.",
          Needs::Document },
        { "current", "project.current() -> {guid, name, folder, location} | null",
          "The open project, or null. `location` is the root RECORDED with the project — \"\" when it lives "
          "on the user's projects root (it then follows that root when it moves), the chosen folder when "
          "project.create's `location` (the New Scene dialog's Browse) named somewhere else.",
          Needs::Document },
        { "thumbnail", "project.thumbnail(path, guid=current) -> {guid, path, empty, bytes, width, height}",
          "Writes a project's STORED Desktop tile (the PNG in its row) to `path` — the picture the Desktop "
          "shows. Encodes still in flight are finished and written first, so a save's tile is readable the "
          "moment the save (or the close that saved) returns. The tile is the editor's PRESENTED frame "
          "(CLOSE-SHOT-2): read back from the window, drawn once without the grid, wires, outline, gizmo "
          "and HUD, cropped about its centre to the tile's aspect — never a second render. `empty` is true "
          "(and nothing is written) when the row holds no tile.",
          Needs::Document },
        { "exportWeb", "project.exportWeb(dir) -> {dir, indexHtml, glb, nodes, materials, extensions, warnings, ...}",
          "Exports the open scene for the web (glTF 2.0 + self-contained WebGPU viewer): index.html (double-clickable), "
          "viewer.html + scene.glb (served path), README.txt. dir defaults to <project>/exports/web. Document-only; works headless.",
          Needs::Document },
        { "previewWeb", "project.previewWeb(dir) -> {browser, mode}",
          "Opens an existing web export (see exportWeb) in a Chromium-family browser as a chromeless --app window, "
          "or the default browser when none is found. mode is 'kiosk' or 'browser'.",
          Needs::Document },
        { "exportManifest", "project.exportManifest(dir) -> {dir, manifest, assets, totalBytes}",
          "Writes a manifest v2 (jah.manifest.json) describing the open project's assets — guids, types, dependency "
          "edges, file names, sizes and sha256 content ids — without copying any bytes. dir defaults to "
          "<project>/exports. The catalog half of the unified export (ASSET_PIPELINE_SPEC §3.3); "
          "project.exportArchive materializes the files in the final half.",
          Needs::Document },
        { "exportArchive", "project.exportArchive(path) -> {path, assets, objects}",
          "Exports the open project as a self-contained archive: catalog snapshot + manifest v2 + the "
          "pinned CAS objects. A reference-based project leaves the machine whole.",
          Needs::Document },
        { "importArchive", "project.importArchive(path) -> {guid, name, assets, objects, bakeFailures}",
          "Imports a project archive as a NEW project under a FREE NAME (the archive's, or the next "
          "\"<name> N\" — project.nextFreeName): rows, objects ingested CAS-first, fresh pins, and "
          "EVERY MODEL IT BRINGS BAKED before this returns (an archive carries no bakes); `bakeFailures` "
          "names the model files that could not be baked (they show as missing). "
          "Does not open it; its tile is on the Desktop when this returns (the grid is rebuilt).",
          Needs::Document },
        { "exportArchiveAsync", "project.exportArchiveAsync(path) -> bool",
          "Exports the open project as an archive WITHOUT blocking the UI thread: the catalog reads happen "
          "here, the object copies and the zip on a worker (services/projectarchiver.h). Returns as soon as "
          "the export is under way — poll project.archiveState() for completion and project.archiveResult() "
          "for the outcome. Needs a window; a headless session should use project.exportArchive.",
          Needs::Window },
        { "importArchiveAsync", "project.importArchiveAsync(path) -> bool",
          "Imports a project archive WITHOUT blocking the UI thread: the extraction runs on a worker and the "
          "catalog rows are committed one asset per event-loop turn. Returns as soon as the import is under "
          "way — poll project.archiveState(). Needs a window.",
          Needs::Window },
        { "archiveState", "project.archiveState() -> 'idle' | 'running'",
          "Whether an asynchronous archive export/import is still in flight — ANY of them, this "
          "session's verbs and the desktop page's alike (project.openSample's import is the page's), "
          "so a caller waiting on an archive can always see the one it started.",
          Needs::Window },
        { "waitArchive", "project.waitArchive(maxSeconds?) -> {ok, error, canceled, path, guid, name, assets, objects, waited}",
          "WAITS FOR THE ARCHIVE IN FLIGHT TO FINISH — any of them, project.openSample's import included "
          "(archiveState()'s reach) — and answers its outcome (archiveResult()'s map, plus `waited`: "
          "false when nothing was running). It services the app's queued work while it waits (the "
          "extract's completion, the install slices, the import's bakes), so the archive's OWN "
          "completion ends the wait: an extract on a loaded box takes what it takes, which a "
          "frame-counted poll cannot know (plan row 9cm SAMPLE-OPEN-WAIT-1: a 40,000-frame poll ran out "
          "at 9.4 s under load and failed two pools). `maxSeconds` (1800 by default) is a hang guard, "
          "never a budget: past it the answer is {ok:false, error:'still running…'} and the archive "
          "keeps going. Frames are not drawn while it waits; the open that follows an import is still "
          "polled with project.openState() and a frame in the loop.",
          Needs::Window },
        { "archiveResult", "project.archiveResult() -> {ok, error, canceled, path, guid, name, assets, objects}",
          "The outcome of the most recent archive operation in this PROCESS — the same reach "
          "archiveState() has, so the import project.openSample starts (the desktop page's, not this "
          "module's) reports here too. `ok` false with `error` set is how a failed or incompatible "
          "archive reaches a script: a driven run never gets the message box a person would.",
          Needs::Window },
        { "cancelArchive", "project.cancelArchive() -> bool",
          "Asks an in-flight archive operation to stop. Honoured between zip/extract entries and between "
          "catalog-install slices; a cancelled export deletes its partial archive and a cancelled import "
          "deletes the project it had started building.",
          Needs::Window },
    };
}

QString ProjectApi::resolveGuid(const QString &guidOrName, QString *nameOut)
{
    int hits = 0;
    const QString guid =
        host.services->project->resolveProjectGuid(guidOrName, nameOut, &hits);
    if (hits > 1) {
        fail(QStringLiteral("project: name '%1' matches %2 projects — use the guid").arg(guidOrName).arg(hits));
        return QString();
    }
    return guid;
}

// THE BODY BOTH CREATE VERBS SHARE (OPEN_COVER_SPEC §4). `async` is the only
// thing that differs: whether the shell drains the runner before returning.
// `verb` is what a refusal calls itself, so the message names the verb the
// caller actually used.
namespace {
/// WHAT A SCRIPT'S CLOSE DOES WITH UNSAVED CHANGES (services/closeunsaved.h): refused with a sentence,
/// unless the caller said {discard: true} — no verb drops edits silently.
CloseUnsaved scriptCloseUnsaved(const QVariantMap &options)
{
    return options.value(QStringLiteral("discard"), false).toBool() ? CloseUnsaved::Discard
                                                                     : CloseUnsaved::Refuse;
}
}   // namespace

QString ProjectApi::createInto(const QString &name, const QVariantMap &options,
                               bool async, const QString &verb)
{
    if (!host.shell || !host.services || !host.services->project) { fail("project: not available in this session"); return QString(); }
    if (name.trimmed().isEmpty()) { fail(QStringLiteral("%1: a non-empty name is required").arg(verb)); return QString(); }

    // UNKNOWN KEYS ARE REFUSED, not ignored (the house rule): `{tempalte: "empty"}`
    // silently making the template is exactly the class of bug the option maps
    // exist to prevent.
    static const QStringList known = { QStringLiteral("template"), QStringLiteral("location"),
                                       QStringLiteral("discard") };
    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        if (known.contains(it.key())) continue;
        fail(QStringLiteral("%1: unknown option '%2' (known: %3)")
                 .arg(verb, it.key(), known.join(QStringLiteral(", "))));
        return QString();
    }
    SceneTemplate kind = SceneTemplate::Basic;
    if (options.contains(QStringLiteral("template"))) {
        const QString wanted = options.value(QStringLiteral("template")).toString();
        if (!scenetemplate::fromName(wanted, &kind)) {
            fail(QStringLiteral("%1: unknown template '%2' (known: %3)")
                     .arg(verb, wanted, scenetemplate::names().join(QStringLiteral(", "))));
            return QString();
        }
    }
    const QString location = options.value(QStringLiteral("location")).toString();

    // THE OPEN WORLD CLOSES FIRST, BEFORE A ROW IS MINTED (STUDIO-HYGIENE-1 audit):
    // unsaved changes refuse the create unless {discard: true}, and a failed
    // auto-save refuses it whatever — nothing is created then.
    // ...after the create's OWN refusals (a name, a location): a refused create leaves the open world open.
    if (const QString refusal = host.services->project->createRefusal(name, location); !refusal.isEmpty()) {
        fail(QStringLiteral("%1: %2").arg(verb, refusal));
        return QString();
    }
    // AN OPEN IN FLIGHT IS PART OF THE WORLD THAT CLOSES (the final read's W5): closeProject drains it and then
    // settles the world it installed. Skipped, the runner's drain installed it AFTER the row was minted, its close
    // was refused by the backstop, and the verb returned the guid of an empty row as a success.
    if (host.services->project->isSceneOpen() || host.shell->isOpeningProject()) {
        host.endRunUndoMacro();
        QString closeWhy;
        const bool closed = host.shell->closeProject(false, scriptCloseUnsaved(options), &closeWhy);
        host.beginRunUndoMacro();
        if (!closed) {
            fail(QStringLiteral("%1: %2").arg(verb, closeWhy));
            return QString();
        }
    }

    // The data half (guid, current project, folder, DB row, desktop) is
    // ProjectService's; MainWindow::newProject then builds the default scene
    // and saves it, so the row never carries the empty scene blob (the crash
    // window the census flagged).
    QString why, folder;
    const QString guid = host.services->project->createProjectShell(name, location, &why, &folder);
    if (guid.isEmpty()) {
        // THE SERVICE'S OWN REASON, by name: "the location '/nope' does not
        // exist" is the answer a caller can act on, and it is the same
        // sentence the dialog shows.
        fail(QStringLiteral("%1: %2")
                 .arg(verb, why.isEmpty() ? QStringLiteral("the database rejected the project row")
                                          : why));
        return QString();
    }

    // A PROJECT BOUNDARY ENDS THE RUN'S UNDO ENTRY (CLOSE-2 item 2 — the
    // reasoning is on ScriptHost::endRunUndoMacro). newProject() clears the
    // stack, and that clear is a no-op while the run's macro is open.
    host.endRunUndoMacro();
    if (async) host.shell->newProjectAsync(guid, name.trimmed(), folder, kind);
    else       host.shell->newProject(guid, name.trimmed(), folder, kind);
    host.beginRunUndoMacro();
    return guid;
}

QString ProjectApi::create(const QString &name, const QVariantMap &options)
{
    return createInto(name, options, false, QStringLiteral("project.create"));
}

// THE SAME CREATE, WITHOUT THE DRAIN (§2 C/§4). One body, two verbs: the only
// difference is whether MainWindow pumps the event loop until the runner is
// done, and a second implementation of "make a project" is exactly the class of
// drift the option map and the shell's stage list exist to prevent.
QString ProjectApi::createAsync(const QString &name, const QVariantMap &options)
{
    if (!host.shell) { fail("project.createAsync: this verb needs the editor window"); return QString(); }
    // THE SAME RULE project.openAsync follows, and the same predicate: while
    // project.openState() reads 'opening' a second one is refused, never
    // queued. (project.create cannot refuse — its contract is a loaded world —
    // so its close drains the open in flight first: createInto, the final read's W5.)
    if (openInFlight())
        return (fail("project.createAsync: an open or create is already in flight "
                     "(project.openState() reads 'opening' until it finishes)"), QString());
    return createInto(name, options, true, QStringLiteral("project.createAsync"));
}

bool ProjectApi::open(const QString &guidOrName, const QVariantMap &options)
{
    if (!host.shell || !host.services || !host.services->project)
        return fail("project: not available in this session");

    QString name;
    const QString guid = resolveGuid(guidOrName, &name);
    if (guid.isEmpty()) {
        // resolveGuid already threw for an ambiguous name; add the not-found
        // case. `host.pendingError` is where a fail() waits to be rethrown by
        // the script bridge (SCRIPTING_LIVE_SPEC) — it replaced reading
        // hasError() off the engine that used to wrap this module.
        if (host.pendingError.isEmpty())
            fail(QStringLiteral("project.open: no project named or guid '%1'").arg(guidOrName));
        return false;
    }

    // AN OPEN ALREADY IN FLIGHT FINISHES FIRST, and it has to happen HERE,
    // before a single pointer moves (MainWindow::waitForOpen). The threaded
    // open's remaining slices read the project when they RUN — the document
    // read asks the database for project->getProjectGuid()'s blob — so
    // closing and re-pointing first and draining afterwards would install a
    // hybrid world: the old session's assets, the new blob, and a prewarm for
    // neither, every mesh of it parsed on the UI thread.
    if (!host.shell->waitForOpen())
        return fail("project.open: an open already in flight did not finish");

    if (host.project->getProjectGuid() == guid && host.services->project->isSceneOpen()) {
        host.shell->setSpace(QStringLiteral("editor"));
        return true;
    }
    // A RECORDED LOCATION THAT IS NOT THERE IS A REFUSAL BY NAME (SMALL-UI-A
    // fix round F1). A project created at a chosen location (the New Scene
    // dialog's Browse button) records the root it lives under; if that root is
    // gone — an unplugged drive, a folder the user moved — opening must SAY SO
    // with the path in it, not fall back to the default root (which would open
    // an empty world under the project's own guid and let the user save over
    // it) and not recreate an empty folder.
    {
        QString whyMissing;
        if (host.services->project->projectLocationMissing(guid, &whyMissing))
            return fail(QStringLiteral("%1: %2").arg(QStringLiteral("project.open"), whyMissing));
    }
    // The OLD project's undo history dies with the old project (CLOSE-2
    // item 2): end the run's entry so closeProject's clear() is not a no-op,
    // and open a fresh one for what the script does in the new project.
    host.endRunUndoMacro();
    // The close is the first half of this open: the page the user is on stays
    // (MainWindow::CloseIntent, VIEW-REBUILD-1). Unsaved changes refuse it unless
    // {discard: true}; a failed auto-save refuses it whatever (STUDIO-HYGIENE-1).
    if (host.services->project->isSceneOpen()) {
        QString closeWhy;
        if (!host.shell->closeProject(true, scriptCloseUnsaved(options), &closeWhy)) {
            host.beginRunUndoMacro();
            return fail(QStringLiteral("project.open: %1").arg(closeWhy));
        }
    }

    // The ledger starts HERE, not in MainWindow::openProject: the session
    // registrations are part of what an open costs and they happen inside it.
    LoadTimeline::begin(QStringLiteral("open(script) %1").arg(name.isEmpty() ? guid : name));
    // Point the current project, then the open — which registers the session
    // assets itself, in its slices, with the worker's parsed models in hand
    // (OPEN-ASSIMP-1: the synchronous verb and the threaded open are ONE path
    // now, so the preload that used to run here — and parse on this thread —
    // is gone).
    host.services->project->pointAtProject(guid, name);
    host.shell->openProject(false);
    host.beginRunUndoMacro();
    // A REFUSED SCENE (STUDIO-HYGIENE-1): one that does not parse, or one a
    // newer build wrote — the runner's one sentence, nothing opened.
    if (const QString refusal = host.shell->openRefusal(); !refusal.isEmpty())
        return fail(QStringLiteral("project.open: %1").arg(refusal));
    return true;
}

bool ProjectApi::openAsync(const QString &guidOrName, const QVariantMap &options)
{
    if (!host.shell || !host.services || !host.services->project)
        return fail("project: not available in this session");

    // WHICH SPACE THIS OPEN LANDS IN, said by the caller (SMOKE-FIX-1). The
    // desktop tile's Play button is the UI that means `play: true`, and it had
    // no verb at all until now.
    const bool play = options.value(QStringLiteral("play"), false).toBool();
    QString name;
    const QString guid = resolveGuid(guidOrName, &name);
    if (guid.isEmpty()) {
        if (host.pendingError.isEmpty())
            fail(QStringLiteral("project.openAsync: no project named or guid '%1'").arg(guidOrName));
        return false;
    }
    // THE SAME RULE AS project.open — nothing moves while an open is in
    // flight — enforced here by REFUSING instead of waiting, and that is the
    // whole guard: isOpeningProject() is true for a tile click's open exactly
    // as it is for a scripted one, so this verb can never reach the close
    // below with slices still queued. (project.open cannot refuse — its
    // contract is a loaded world — so it drains through waitForOpen instead.)
    if (openInFlight())
        return fail("project.openAsync: an open is already in flight "
                    "(project.openState() reads 'opening' until it finishes)");

    if (host.project->getProjectGuid() == guid && host.services->project->isSceneOpen()) {
        host.shell->setSpace(play ? QStringLiteral("player") : QStringLiteral("editor"));
        return true;
    }
    // A RECORDED LOCATION THAT IS NOT THERE IS A REFUSAL BY NAME (SMALL-UI-A
    // fix round F1). A project created at a chosen location (the New Scene
    // dialog's Browse button) records the root it lives under; if that root is
    // gone — an unplugged drive, a folder the user moved — opening must SAY SO
    // with the path in it, not fall back to the default root (which would open
    // an empty world under the project's own guid and let the user save over
    // it) and not recreate an empty folder.
    {
        QString whyMissing;
        if (host.services->project->projectLocationMissing(guid, &whyMissing))
            return fail(QStringLiteral("%1: %2").arg(QStringLiteral("project.openAsync"), whyMissing));
    }
    host.endRunUndoMacro();   // CLOSE-2 item 2, as project.open
    // The close is the first half of this open: the page the user is on stays
    // (MainWindow::CloseIntent, VIEW-REBUILD-1) — refused as project.open's is.
    if (host.services->project->isSceneOpen()) {
        QString closeWhy;
        if (!host.shell->closeProject(true, scriptCloseUnsaved(options), &closeWhy)) {
            host.beginRunUndoMacro();
            return fail(QStringLiteral("project.openAsync: %1").arg(closeWhy));
        }
    }

    LoadTimeline::begin(QStringLiteral("open(script-async) %1").arg(name.isEmpty() ? guid : name));
    // The open's first slices do the session registrations themselves, with
    // the worker's parsed models in hand.
    host.services->project->pointAtProject(guid, name);
    host.shell->openProjectAsync(play);
    host.beginRunUndoMacro();
    return true;
}

QStringList ProjectApi::samples()
{
    return ProjectManager::sampleNames();
}

bool ProjectApi::openSample(const QString &name, const QVariantMap &options)
{
    if (!host.shell) return fail("project.openSample: this verb needs the editor window");
    ProjectManager *page = host.shell->projectPage();
    if (!page) return fail("project.openSample: this session has no project page");
    if (openInFlight())
        return fail("project.openSample: an open is already in flight "
                    "(project.openState() reads 'opening' until it finishes)");
    // The page's own refusal for this is a MODAL BOX, which a script run cannot
    // answer — refuse before we get there (project.archiveState() reads
    // 'running' until it finishes).
    if (ProjectArchiver::anyRunning())
        return fail("project.openSample: an archive operation is already running "
                    "(project.archiveState() reads 'running' until it finishes)");
    QString why;
    const QString archive = page->sampleArchive(name, &why);
    if (archive.isEmpty()) return fail(QStringLiteral("project.openSample: %1").arg(why));
    // THE OPEN WORLD IS SETTLED BEFORE THE IMPORT (the final read's B5), as project.open settles it: the
    // import-finished close is a person's Ask, which a driven session refuses with nobody told — the verb
    // returned true and opened nothing. The undo entry closes with the world that is leaving.
    host.endRunUndoMacro();
    if (host.services && host.services->project && host.services->project->isSceneOpen()) {
        QString closeWhy;
        if (!host.shell->closeProject(true, scriptCloseUnsaved(options), &closeWhy)) {
            host.beginRunUndoMacro();
            return fail(QStringLiteral("project.openSample: %1").arg(closeWhy));
        }
    }
    const bool started = page->openSampleArchive(archive, &why);
    host.beginRunUndoMacro();
    return started ? true : fail(QStringLiteral("project.openSample: %1").arg(why));
}

bool ProjectApi::openInFlight() const
{
    return host.shell && host.shell->isOpeningProject();
}

QString ProjectApi::openState()
{
    if (!host.shell) { fail("project: not available in this session"); return QStringLiteral("idle"); }
    return openInFlight() ? QStringLiteral("opening") : QStringLiteral("idle");
}

QString ProjectApi::openRefusal()
{
    if (!host.shell) { fail("project: not available in this session"); return QString(); }
    return host.shell->openRefusal();
}

QVariantMap ProjectApi::thumbnail(const QString &path, const QString &guid)
{
    QVariantMap out;
    if (!host.db || !host.services || !host.services->project) {
        fail("project.thumbnail: not available in this session");
        return out;
    }
    if (path.isEmpty()) { fail("project.thumbnail: a file path is required"); return out; }
    const QString id = guid.isEmpty() ? host.services->project->current()->getProjectGuid() : guid;
    if (id.isEmpty()) { fail("project.thumbnail: no project is open and no guid was given"); return out; }
    // A save's encode runs on a worker; the stored tile is the one after it.
    host.services->project->drainThumbnailEncodes();
    const QByteArray png = host.db->fetchProjectThumbnailBytes({ id }).value(id);
    out["guid"] = id;
    out["bytes"] = png.size();
    out["empty"] = png.isEmpty();
    out["width"] = 0;
    out["height"] = 0;
    if (png.isEmpty()) return out;
    QImage img;
    if (!img.loadFromData(png, "PNG")) { fail("project.thumbnail: the stored tile is not a PNG"); return out; }
    QFileInfo info(path);
    if (!info.dir().exists()) info.dir().mkpath(".");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(png) != png.size()) {
        fail(QStringLiteral("project.thumbnail: could not write '%1'").arg(path));
        return out;
    }
    out["path"] = info.absoluteFilePath();
    out["width"] = img.width();
    out["height"] = img.height();
    return out;
}

bool ProjectApi::save()
{
    if (!requireProject()) return false;
    if (!host.services || !host.services->project) return fail("project: not available in this session");
    // THE ONE SAVE DOOR (STUDIO-HYGIENE-1): true only when the row was written.
    QString why;
    if (!host.services->project->save(host.project->getProjectGuid(), ProjectService::SaveReason::Script, &why))
        return fail(QStringLiteral("project.save: the scene could not be saved: %1").arg(why));
    return true;
}

bool ProjectApi::close(const QVariantMap &options)
{
    // AN OPEN IN FLIGHT IS SOMETHING TO CLOSE (lane OPEN-FRAMES-1). Until this
    // lane, "is a project open?" meant "has a scene finished being installed?",
    // so the ONE moment a close matters most — a world half-installed, with
    // slices still queued that will mount panels and push geometry — was the
    // one moment this verb refused, and the caller was left with an install it
    // could not stop. The shell's closeProject (MainWindow::closeWorld) drains the runner first (the
    // window-close and shutdown paths always did), so the close is coherent:
    // the install finishes, then the world closes.
    //
    // The undo macro is ended here too, for the same reason as below — the
    // in-flight project's history dies with it.
    if (openInFlight()) {
        host.endRunUndoMacro();
        QString closeWhy;
        const bool closed = host.shell->closeProject(false, scriptCloseUnsaved(options), &closeWhy);
        host.beginRunUndoMacro();
        return closed ? true : fail(QStringLiteral("project.close: %1").arg(closeWhy));
    }
    if (!requireProject()) return false;
    // The run's edits so far become ONE undo step of the project being closed,
    // and the close then clears the stack exactly as a close from the UI does
    // (CLOSE-2 item 2). Without this the whole stack survived the close,
    // holding commands that name a document that no longer exists.
    host.endRunUndoMacro();
    QString closeWhy;
    const bool closed = host.shell->closeProject(false, scriptCloseUnsaved(options), &closeWhy);
    host.beginRunUndoMacro();
    // REFUSED, NOT DISCARDED (STUDIO-HYGIENE-1 audit): unsaved changes with
    // auto-save off, or an auto-save that failed — the world stays open.
    return closed ? true : fail(QStringLiteral("project.close: %1").arg(closeWhy));
}

QString ProjectApi::nextFreeName(const QString &name)
{
    if (!host.services || !host.services->project) {
        fail("project: not available in this session");
        return QString();
    }
    return host.services->project->nextFreeName(name);
}

bool ProjectApi::rename(const QString &guid, const QString &newName)
{
    if (!host.db || !host.services || !host.services->project)
        return fail("project: not available in this session");
    // THE ONE RENAME (PROJECT-NAMES-1): the service refuses an empty name, an
    // unknown guid and a taken name, each by a sentence — the desktop tile's
    // Rename dialog goes through the same call.
    QString why;
    if (!host.services->project->renameProject(guid, newName, &why))
        return fail(QStringLiteral("project.rename: %1").arg(why));
    if (host.project->getProjectGuid() == guid)
        host.project->setProjectPath(host.project->getProjectFolder(), newName.trimmed());
    // The caption follows (the Desktop used to learn it at its next rebuild).
    if (desktopPage(host)) desktopPage(host)->renameTile(guid, newName.trimmed());
    return true;
}

bool ProjectApi::remove(const QString &guid)
{
    if (!host.db || !host.services || !host.services->project)
        return fail("project: not available in this session");
    // A PROJECT WHOSE OPEN IS IN FLIGHT is not deleted under it (the audit's W3):
    // the door reads sceneOpen, which a threaded open sets only at its bind — the
    // install would then build a world for a row that is gone. (Whether the
    // Desktop's Delete can be clicked while an open runs is unverified: the
    // open's progress dialog is up — a leftover line for the door itself.)
    if (openInFlight() && host.project->getProjectGuid() == guid)
        return fail(QStringLiteral("project.remove: '%1' is opening — wait until project.openState() reads "
                                   "'idle'").arg(guid));
    // THE ONE DELETE (STUDIO-HYGIENE-1): the Desktop tile's Delete calls the
    // same function, and it owns every refusal (an unknown guid, the open
    // project, a location that is not there) — host.project is NOT mutated
    // (SCRIPTING_SPEC §1.6.1).
    QString why;
    if (!host.services->project->removeProject(guid, &why))
        return fail(QStringLiteral("project.remove: %1").arg(why));
    return true;
}

QVariantList ProjectApi::list(const QVariantMap &options)
{
    QVariantList out;
    if (!host.db) { fail("project: not available in this session"); return out; }
    const int desktop = options.value("desktop", 0).toInt();
    for (const auto &p : host.db->fetchProjects(desktop)) {
        QVariantMap m;
        m["guid"] = p.guid;
        m["name"] = p.name;
        m["desktop"] = p.desktop;
        if (p.hasPosition) { m["x"] = p.posX; m["y"] = p.posY; }
        out.append(m);
    }
    return out;
}

bool ProjectApi::moveToDesktop(const QString &guid, int desktop)
{
    if (!host.db) return fail("project: not available in this session");
    if (desktop < 1 || desktop > 4) return fail("project.moveToDesktop: desktop must be 1-4");
    if (!host.db->updateProjectDesktop(guid, desktop))
        return fail(QStringLiteral("project.moveToDesktop: no project with guid '%1'").arg(guid));
    // ONE TILE MOVES (CREATE-GAP-1): off this desktop's grid, or onto it.
    if (desktopPage(host)) desktopPage(host)->moveTile(guid, desktop);
    return true;
}

bool ProjectApi::setPosition(const QString &guid, double x, double y)
{
    if (!host.db) return fail("project: not available in this session");
    if (!host.db->updateProjectPosition(guid, float(x), float(y)))
        return fail(QStringLiteral("project.setPosition: no project with guid '%1'").arg(guid));
    return true;
}

QVariantMap ProjectApi::exportWeb(const QString &dir)
{
    QVariantMap out;
    if (!requireProject()) return out;
    if (!host.services || !host.services->sceneEdit) { fail("project.exportWeb: no scene service"); return out; }
    auto scene = host.services->sceneEdit->scene();
    if (!scene) { fail("project.exportWeb: no scene is open"); return out; }

    QString outDir = dir.trimmed();
    if (outDir.isEmpty())
        outDir = QDir(host.project->getProjectFolder()).filePath(QStringLiteral("exports/web"));

    // The live renderer, when there is one, bakes the sky's cloud layer into
    // the exported sky image (CLOUDS-2D-1); headless exports go without it.
    jahshaka::engine::Scene *renderer =
        (host.isEngineReady() && host.viewport) ? host.viewport->engineScene() : nullptr;
    const QString musicPath = (host.services && host.services->ambience)
                                  ? host.services->ambience->fileFor(scene->ambientMusicGuid)
                                  : QString();
    const auto r = ExportService::exportWeb(scene, host.project->getProjectName(), outDir, renderer,
                                            musicPath);
    if (!r.ok) { fail(QStringLiteral("project.exportWeb: %1").arg(r.error)); return out; }

    out["dir"] = r.dir;
    out["indexHtml"] = r.indexHtml;
    out["viewerHtml"] = r.viewerHtml;
    out["glb"] = r.glbPath;
    out["glbSize"] = r.glbSize;
    out["indexSize"] = r.indexSize;
    out["inlined"] = r.inlined;
    out["nodes"] = r.nodeCount;
    out["meshes"] = r.meshCount;
    out["materials"] = r.materialCount;
    out["lights"] = r.lightCount;
    out["cameras"] = r.cameraCount;
    out["animations"] = r.animationCount;
    out["extensions"] = QVariant(r.extensionsUsed);
    out["warnings"] = QVariant(r.warnings);
    return out;
}

QVariantMap ProjectApi::previewWeb(const QString &dir)
{
    QVariantMap out;
    if (!requireProject()) return out;

    QString outDir = dir.trimmed();
    if (outDir.isEmpty())
        outDir = QDir(host.project->getProjectFolder()).filePath(QStringLiteral("exports/web"));
    const QString indexHtml = QDir(outDir).filePath(QStringLiteral("index.html"));
    if (!QFileInfo::exists(indexHtml)) {
        fail(QStringLiteral("project.previewWeb: no export at %1 — project.exportWeb() first").arg(indexHtml));
        return out;
    }

    // mode:"kiosk" now means the browser REALLY started: launchKiosk waits for
    // the start and returns null when it dies immediately (PUBLISH_AUDIT #11 —
    // this verb used to report a window that never existed), so the fall-
    // through below is a real fallback rather than a formality.
    const QString browser = PreviewLauncher::findChromiumBrowser();
    if (!browser.isEmpty() && PreviewLauncher::launchKiosk(indexHtml, this)) {
        out["browser"] = browser;
        out["mode"] = "kiosk";
        return out;
    }
    if (!PreviewLauncher::openInBrowser(indexHtml)) {
        fail("project.previewWeb: no browser could be launched");
        return out;
    }
    out["browser"] = QStringLiteral("default");
    out["mode"] = "browser";
    return out;
}

QVariant ProjectApi::current()
{
    if (!host.isProjectOpen()) return QVariant();
    QVariantMap m;
    m["guid"] = host.project->getProjectGuid();
    m["name"] = host.project->getProjectName();
    m["folder"] = host.project->getProjectFolder();
    m["location"] = host.db ? host.db->projectLocation(host.project->getProjectGuid()) : QString();
    return m;
}

QVariantMap ProjectApi::exportManifest(const QString &dir)
{
    QVariantMap out;
    if (!requireProject()) return out;
    if (!host.db) { fail("project.exportManifest: no database in this session"); return out; }

    QString outDir = dir.trimmed();
    if (outDir.isEmpty())
        outDir = QDir(host.project->getProjectFolder()).filePath(QStringLiteral("exports"));

    const QString projectGuid = host.project->getProjectGuid();

    // The same sweep assets.list({scope:'project'}) shows, closed over each
    // asset's outgoing dependency edges so the manifest is self-describing.
    // Reference-with-pin (phase 4): pinned LIBRARY assets are members too.
    QStringList guids;
    for (const auto &record :
         host.db->fetchChildAssets(projectGuid, projectGuid, -1))
        if (!record.guid.isEmpty() && !guids.contains(record.guid)) guids.append(record.guid);
    {
        db::Statement pins(host.db->connection(), "SELECT asset_guid FROM project_assets WHERE project_guid = ?");
        dbqt::bindText(pins, 1, projectGuid);
        db::Error error;
        while (pins.step(&error)) {
            const QString pinned = dbqt::text(pins, 0);
            if (!pinned.isEmpty() && !guids.contains(pinned)) guids.append(pinned);
        }
        if (error) qWarning("project: the project's pinned assets: %s", qUtf8Printable(dbqt::describe(error)));
    }
    for (int i = 0; i < guids.size(); ++i)   // grows while iterating: transitive closure
        for (const QString &dep : host.db->fetchAssetGUIDAndDependencies(guids.at(i), false))
            if (!dep.isEmpty() && !guids.contains(dep)) guids.append(dep);

    QVector<RawExporter::AssetInfo> infos;
    for (const QString &g : guids) {
        const auto rec = host.db->fetchAsset(g);
        if (rec.guid.isEmpty()) continue;
        RawExporter::AssetInfo info;
        info.guid = rec.guid;
        info.name = rec.name;
        info.typeId = rec.type;
        info.type = scriptmod::assetTypeName(rec.type);
        info.dependencies = host.db->fetchAssetGUIDAndDependencies(rec.guid, false);
        infos.append(info);
    }
    if (infos.isEmpty()) { fail("project.exportManifest: the project has no assets"); return out; }

    // The resolver-backed source (final half): entries by oid through the
    // catalog, source role at the project's pinned content.
    CasContentSource source(&host.db->connection(), AssetMetadata::storeRootPath(), projectGuid);
    const auto r = RawExporter::exportAssets(infos, source, outDir,
                                             QStringLiteral("project"), /*copyFiles=*/false);
    if (!r.ok) { fail(QStringLiteral("project.exportManifest: %1").arg(r.error)); return out; }

    out["dir"] = r.dir;
    out["manifest"] = r.manifestPath;
    out["assets"] = r.assetCount;
    out["totalBytes"] = r.totalBytes;
    out["warnings"] = QVariant(r.warnings);
    return out;
}

QVariantMap ProjectApi::exportArchive(const QString &path)
{
    QVariantMap out;
    if (!requireProject()) return out;
    if (!host.db) { fail("project.exportArchive: no database in this session"); return out; }
    if (path.trimmed().isEmpty()) { fail("project.exportArchive: a destination path is required"); return out; }

    // The SYNCHRONOUS path: the same three phases inline (scripts and headless
    // runs have no event loop to slice against). The threaded twin is
    // project.exportArchiveAsync.
    ProjectArchiver archiver(host.db, host.project);
    // The manifest's scene-scale block: what one unit means and how big the
    // scene is, measured from the LIVE document (the archiver only ever sees
    // the database). This is what lets a sample tile say "12 x 3.3 x 12 m"
    // without opening the project.
    if (host.viewport)
        archiver.setSceneMetadata(sceneextents::describe(host.viewport->getScene(),
                                                         host.viewport->editorCamera()));
    const auto r = archiver.exportArchive(path);
    if (!r.ok()) { fail(QStringLiteral("project.exportArchive: %1").arg(r.error)); return out; }
    out["path"] = r.path;
    out["assets"] = r.assets;
    out["objects"] = r.objects;
    out["bakeFailures"] = r.bakeFailures;
    return out;
}

namespace {
// One archiver per SESSION for the async verbs: the state the poll verbs read
// has to outlive the call that started it. Parented to nothing and never
// deleted while a run is in flight (its own destructor joins the worker).
ProjectArchiver *&sessionArchiver()
{
    static ProjectArchiver *a = nullptr;
    return a;
}
}   // namespace

bool ProjectApi::exportArchiveAsync(const QString &path)
{
    if (!requireProject()) return false;
    if (!host.db) return fail("project.exportArchiveAsync: no database in this session");
    if (path.trimmed().isEmpty())
        return fail("project.exportArchiveAsync: a destination path is required");
    ProjectArchiver *&a = sessionArchiver();
    if (a && a->isRunning()) return fail("project.exportArchiveAsync: an archive operation is already running");
    if (!a) a = new ProjectArchiver(host.db, host.project);
    if (host.viewport)
        a->setSceneMetadata(sceneextents::describe(host.viewport->getScene(),
                                                   host.viewport->editorCamera()));
    return a->startExport(path);
}

bool ProjectApi::importArchiveAsync(const QString &path)
{
    if (!host.db) return fail("project.importArchiveAsync: no database in this session");
    if (path.trimmed().isEmpty())
        return fail("project.importArchiveAsync: an archive path is required");
    ProjectArchiver *&a = sessionArchiver();
    if (a && a->isRunning()) return fail("project.importArchiveAsync: an archive operation is already running");
    if (!a) a = new ProjectArchiver(host.db, host.project);
    // THE IMPORTED PROJECT IS A TILE when it lands (CREATE-GAP-1) — the
    // Desktop no longer rebuilds on entry to find it. One connection per
    // session archiver, made with the import that needs it.
    if (desktopPage(host) && !a->property("jahTileHook").toBool()) {
        a->setProperty("jahTileHook", true);
        QPointer<ProjectManager> page = desktopPage(host);
        QObject::connect(a, &ProjectArchiver::finished, a, [a, page](bool canceled) {
            if (canceled || !page) return;
            const ProjectArchiver::Result &r = a->result();
            if (r.ok() && !r.projectGuid.isEmpty()) page->addTile(r.projectGuid);
        });
    }
    return a->startImport(path);
}

QString ProjectApi::archiveState()
{
    // ANY archive in flight, not just this module's own (SMOKE-FIX-1) — the
    // same rule openState() follows for opens: a caller waiting for the import
    // project.openSample started (the desktop PAGE's archiver, not the script's)
    // has to be able to see it, or it polls an idle counter and walks straight
    // past the work it asked for.
    return ProjectArchiver::anyRunning() ? QStringLiteral("running") : QStringLiteral("idle");
}

QVariantMap ProjectApi::archiveResult()
{
    QVariantMap out;
    // THE PROCESS-WIDE RECORD, not this module's own archiver (SMOKE-FIX-1's
    // fix round, F5). archiveState() already answers for every archiver in the
    // process — it has to, because project.openSample starts the desktop PAGE's
    // import — and an outcome nobody can read is not a failure channel: a
    // failed or incompatible import ended in a message box and silence here.
    if (!ProjectArchiver::haveLastResult()) {
        out["ok"] = false;
        out["error"] = QStringLiteral("no archive operation has run");
        return out;
    }
    const ProjectArchiver::Result &r = ProjectArchiver::lastResult();
    out["ok"] = r.ok();
    out["error"] = r.error;
    out["canceled"] = r.canceled;
    out["path"] = r.path;
    out["guid"] = r.projectGuid;
    out["name"] = r.worldName;
    out["assets"] = r.assets;
    out["objects"] = r.objects;
    return out;
}

QVariantMap ProjectApi::waitArchive(double maxSeconds)
{
    const bool running = ProjectArchiver::anyRunning();
    if (running && !ProjectArchiver::waitForAll(int(qBound(1.0, maxSeconds, 86400.0) * 1000.0))) {
        QVariantMap out;
        out["ok"] = false;
        out["error"] = QStringLiteral("still running after %1 s (the hang guard; the archive goes on)")
                           .arg(maxSeconds);
        out["waited"] = true;
        refuse(QStringLiteral("project.waitArchive: %1").arg(out["error"].toString()));
        return out;
    }
    QVariantMap out = archiveResult();
    out["waited"] = running;
    return out;
}

bool ProjectApi::cancelArchive()
{
    ProjectArchiver *a = sessionArchiver();
    if (!a || !a->isRunning()) return false;
    a->requestCancel();
    return true;
}

QVariantMap ProjectApi::importArchive(const QString &path)
{
    QVariantMap out;
    if (!host.db) { fail("project.importArchive: no database in this session"); return out; }
    if (path.trimmed().isEmpty()) { fail("project.importArchive: an archive path is required"); return out; }

    ProjectArchiver archiver(host.db, nullptr);
    const auto r = archiver.importArchive(path);
    if (!r.ok()) { fail(QStringLiteral("project.importArchive: %1").arg(r.error)); return out; }
    // THE NEW PROJECT IS A TILE NOW — its one tile, as the Desktop page's own
    // import adds it (CREATE-GAP-1: this rebuilt the whole grid for it).
    if (desktopPage(host)) desktopPage(host)->addTile(r.projectGuid);
    out["guid"] = r.projectGuid;
    out["name"] = r.worldName;
    out["assets"] = r.assets;
    out["objects"] = r.objects;
    return out;
}
