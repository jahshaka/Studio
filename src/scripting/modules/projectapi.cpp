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
#include "data/project.h"
#include "shell/mainwindow.h"
#include "services/projectservice.h"
#include "services/loadtimeline.h"
#include "services/services.h"
#include "ui/pages/projectmanager.h"
#include "services/projectarchiver.h"
#include "services/sceneextents.h"
#include "viewport/ieditorviewport.h"

QVector<VerbInfo> ProjectApi::verbs() const
{
    return {
        { "create", "project.create(name, {empty, location}) -> guid",
          "Creates a project (folder, DB row, default scene saved into the blob) on the current desktop and "
          "opens it in the editor. INSIDE A SCRIPT this ends the run's undo entry first: everything the run "
          "did up to here becomes one undo step of the project being left, whose stack is then cleared with "
          "it, and the rest of the run records into a fresh entry in the new project.\n\n"
          "`empty: true` gives a BLANK WORLD instead of the default template. The template is a ground, the "
          "sun (a directional light), a Sky Light and the realistic real-time sky with the sun following the "
          "atmosphere; the blank world is the root node, the Epic world mode and the document's own defaults "
          "— no ground, no lights at all (so nothing lights it) and no sky beyond the flat default colour. "
          "`location` is the folder the project's own directory is created under; omitted, it is the user's "
          "projects root (the Jahshaka documents folder, or the run's --data-root). A location that does not "
          "exist, is not a folder or is not writable is REFUSED BY NAME — no project row is written and "
          "nothing is left pointing at a folder that was never made. These are the New Scene dialog's two "
          "controls: its Empty scene checkbox and its Browse button call this verb."
          "\n\nTHE WORLD'S LIGHTING ARRIVES ON THE FRAMES AFTER THIS RETURNS "
          "(SPECS/OPEN_COVER_SPEC.md §2 A). A world's FIRST global-illumination arm — the voxel "
          "cascades, the reflection-probe grid, the irradiance field — is the longest thing the "
          "engine does on the UI thread, and it is no longer built inside the load: it would land "
          "on a frame drawn behind the loading cover, which nobody can see. It is built one stage "
          "per frame once the world is on screen, so `world.giStatus()` read immediately after this "
          "reports an arm that is not there yet. A script that asserts on it renders its own frames "
          "first (`editor.frame(n)`); a person never notices, because the frames are the app's own.",
          Needs::Document },
        { "createAsync", "project.createAsync(name, {empty, location}) -> guid",
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
          "world stream in (editor.viewportState().pending). `empty` and `location` mean exactly "
          "what they mean on project.create, and an open or create already in flight is REFUSED "
          "by name rather than queued.",
          Needs::Window },
        { "open", "project.open(guidOrName) -> bool",
          "Opens a project by guid or exact name: preloads its assets synchronously, reads the scene blob, "
          "switches to the editor. INSIDE A SCRIPT this ends the run's undo entry first (see project.create): "
          "the closed project's undo history goes with it, and the rest of the run records into a fresh entry."
          "\n\nTHE WORLD'S LIGHTING ARRIVES ON THE FRAMES AFTER THIS RETURNS "
          "(SPECS/OPEN_COVER_SPEC.md §2 A). A world's FIRST global-illumination arm — the voxel "
          "cascades, the reflection-probe grid, the irradiance field — is the longest thing the "
          "engine does on the UI thread, and it is no longer built inside the load: it would land "
          "on a frame drawn behind the loading cover, which nobody can see. It is built one stage "
          "per frame once the world is on screen, so `world.giStatus()` read immediately after this "
          "reports an arm that is not there yet. A script that asserts on it renders its own frames "
          "first (`editor.frame(n)`); a person never notices, because the frames are the app's own.",
          Needs::Document },
        { "openAsync", "project.openAsync(guidOrName, {play}) -> bool",
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
        { "openSample", "project.openSample(name) -> bool",
          "Opens one of the samples this tree ships, by base name (\"Matcaps\", \"Showroom 2\") or by an "
          "Ogre port's display title — THE SAMPLE BROWSER'S OWN ROUTE, which imports the archive and then "
          "opens the imported world. The dialog's tiles call this, so a script and a double-click take the "
          "same road. It lands in the EDITOR: a sample is something to look at and edit, and the Player is a "
          "separate statement (app.space('player')). BOTH HALVES ARE THREADED, like the browser's, and "
          "they run in that order: poll project.archiveState() until 'idle' (the import), then "
          "project.openState() until 'idle' (the open), with a frame in the loop. project.samples() "
          "lists the names.",
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
        { "save", "project.save() -> bool",
          "Saves the open scene into the project's DB blob. Works headless (blob-only; the thumbnail refreshes only when a viewport can render one).",
          Needs::Document },
        { "close", "project.close() -> bool",
          "Closes the open project — INCLUDING one that is still opening: a threaded open in flight is "
          "drained first (its install finishes, then the world closes), so a close can never leave install "
          "slices queued against a project that is gone. "
          "Closes the open project (physics restored, autosave per settings, undo stack reset) and returns to "
          "the desktop. INSIDE A SCRIPT the run's undo entry is ended first, so the stack really is cleared: "
          "the run's edits become one undo step of the project being closed and die with it, and the rest of "
          "the run records into a fresh entry. Nothing on the stack ever names a closed project's nodes.",
          Needs::Document },
        { "rename", "project.rename(guid, newName) -> bool",
          "Renames a project in the database.",
          Needs::Document },
        { "remove", "project.remove(guid) -> bool",
          "Deletes a project: its folder tree, its DB row and its asset/dependency rows. Refuses to delete the open project. NOT undoable.",
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
        { "current", "project.current() -> {guid, name, folder} | null",
          "The open project, or null.",
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
        { "importArchive", "project.importArchive(path) -> {guid, name, assets, objects}",
          "Imports a project archive as a NEW project: rows, objects ingested CAS-first, fresh pins. "
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
QString ProjectApi::createInto(const QString &name, const QVariantMap &options,
                               bool async, const QString &verb)
{
    if (!host.mainWindow || !host.services || !host.services->project) { fail("project: not available in this session"); return QString(); }
    if (name.trimmed().isEmpty()) { fail(QStringLiteral("%1: a non-empty name is required").arg(verb)); return QString(); }

    // UNKNOWN KEYS ARE REFUSED, not ignored (the house rule): `{emtpy: true}`
    // silently making the template is exactly the class of bug the option maps
    // exist to prevent.
    static const QStringList known = { QStringLiteral("empty"), QStringLiteral("location") };
    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        if (known.contains(it.key())) continue;
        fail(QStringLiteral("%1: unknown option '%2' (known: %3)")
                 .arg(verb, it.key(), known.join(QStringLiteral(", "))));
        return QString();
    }
    const bool empty = options.value(QStringLiteral("empty"), false).toBool();
    const QString location = options.value(QStringLiteral("location")).toString();

    // The data half (guid, current project, folder, DB row, desktop) is
    // ProjectService's; MainWindow::newProject then builds the default scene
    // and saves it, so the row never carries the empty scene blob (the crash
    // window the census flagged).
    QString why;
    const QString guid = host.services->project->createProjectShell(name, location, &why);
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
    if (async) host.mainWindow->newProjectAsync(name.trimmed(), host.project->getProjectFolder(), empty);
    else       host.mainWindow->newProject(name.trimmed(), host.project->getProjectFolder(), empty);
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
    if (!host.mainWindow) { fail("project.createAsync: this verb needs the editor window"); return QString(); }
    // THE SAME RULE project.openAsync follows, and the same predicate: while
    // project.openState() reads 'opening' a second one is refused, never
    // queued. (project.create cannot refuse — its contract is a loaded world —
    // so it drains instead, inside MainWindow::startCreateRun.)
    if (openInFlight())
        return (fail("project.createAsync: an open or create is already in flight "
                     "(project.openState() reads 'opening' until it finishes)"), QString());
    return createInto(name, options, true, QStringLiteral("project.createAsync"));
}

bool ProjectApi::open(const QString &guidOrName)
{
    if (!host.mainWindow || !host.services || !host.services->project)
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
    if (!host.mainWindow->waitForOpen())
        return fail("project.open: an open already in flight did not finish");

    if (host.project->getProjectGuid() == guid && host.services->project->isSceneOpen()) {
        host.mainWindow->switchSpace(WindowSpaces::EDITOR);
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
    // (MainWindow::CloseIntent, VIEW-REBUILD-1).
    if (host.services->project->isSceneOpen())
        host.mainWindow->closeProject(MainWindow::CloseIntent::ReopenInPlace);

    // The ledger starts HERE, not in MainWindow::openProject: the session
    // registrations are part of what an open costs and they happen inside it.
    LoadTimeline::begin(QStringLiteral("open(script) %1").arg(name.isEmpty() ? guid : name));
    // Point the current project, then the open — which registers the session
    // assets itself, in its slices, with the worker's parsed models in hand
    // (OPEN-ASSIMP-1: the synchronous verb and the threaded open are ONE path
    // now, so the preload that used to run here — and parse on this thread —
    // is gone).
    host.services->project->pointAtProject(guid, name);
    host.mainWindow->openProject(false);
    host.beginRunUndoMacro();
    return true;
}

bool ProjectApi::openAsync(const QString &guidOrName, const QVariantMap &options)
{
    if (!host.mainWindow || !host.services || !host.services->project)
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
        host.mainWindow->switchSpace(play ? WindowSpaces::PLAYER : WindowSpaces::EDITOR);
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
    // (MainWindow::CloseIntent, VIEW-REBUILD-1).
    if (host.services->project->isSceneOpen())
        host.mainWindow->closeProject(MainWindow::CloseIntent::ReopenInPlace);

    LoadTimeline::begin(QStringLiteral("open(script-async) %1").arg(name.isEmpty() ? guid : name));
    // The open's first slices do the session registrations themselves, with
    // the worker's parsed models in hand.
    host.services->project->pointAtProject(guid, name);
    host.mainWindow->openProjectAsync(play);
    host.beginRunUndoMacro();
    return true;
}

QStringList ProjectApi::samples()
{
    return ProjectManager::sampleNames();
}

bool ProjectApi::openSample(const QString &name)
{
    if (!host.mainWindow) return fail("project.openSample: this verb needs the editor window");
    ProjectManager *page = host.mainWindow->projectPage();
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
    // The undo entry closes with the world that is leaving, exactly as
    // project.open/openAsync do — the import + open that follows belongs to the
    // sample being opened, not to the project being left.
    QString why;
    if (!page->openSampleByName(name, &why))
        return fail(QStringLiteral("project.openSample: %1").arg(why));
    // Only once the sample is really on its way: a refusal must not end the
    // run's undo entry (project.open/openAsync end theirs on the same edge —
    // the world that is leaving takes the run's edits with it).
    host.endRunUndoMacro();
    host.beginRunUndoMacro();
    return true;
}

bool ProjectApi::openInFlight() const
{
    return host.mainWindow && host.mainWindow->isOpeningProject();
}

QString ProjectApi::openState()
{
    if (!host.mainWindow) { fail("project: not available in this session"); return QStringLiteral("idle"); }
    return openInFlight() ? QStringLiteral("opening") : QStringLiteral("idle");
}

bool ProjectApi::save()
{
    if (!requireProject()) return false;
    if (!host.services || !host.services->project || !host.services->project->saveProjectBlob())
        return fail("project.save: the blob save failed");
    return true;
}

bool ProjectApi::close()
{
    // AN OPEN IN FLIGHT IS SOMETHING TO CLOSE (lane OPEN-FRAMES-1). Until this
    // lane, "is a project open?" meant "has a scene finished being installed?",
    // so the ONE moment a close matters most — a world half-installed, with
    // slices still queued that will mount panels and push geometry — was the
    // one moment this verb refused, and the caller was left with an install it
    // could not stop. MainWindow::closeProject drains the runner first (the
    // window-close and shutdown paths always did), so the close is coherent:
    // the install finishes, then the world closes.
    //
    // The undo macro is ended here too, for the same reason as below — the
    // in-flight project's history dies with it.
    if (openInFlight()) {
        host.endRunUndoMacro();
        host.mainWindow->closeProject();
        host.beginRunUndoMacro();
        return true;
    }
    if (!requireProject()) return false;
    // The run's edits so far become ONE undo step of the project being closed,
    // and the close then clears the stack exactly as a close from the UI does
    // (CLOSE-2 item 2). Without this the whole stack survived the close,
    // holding commands that name a document that no longer exists.
    host.endRunUndoMacro();
    host.mainWindow->closeProject();
    host.beginRunUndoMacro();
    return true;
}

bool ProjectApi::rename(const QString &guid, const QString &newName)
{
    if (!host.db) return fail("project: not available in this session");
    if (newName.trimmed().isEmpty()) return fail("project.rename: a non-empty name is required");
    if (!host.db->renameProject(guid, newName.trimmed()))
        return fail(QStringLiteral("project.rename: no project with guid '%1'").arg(guid));
    if (host.project->getProjectGuid() == guid)
        host.project->setProjectPath(host.project->getProjectFolder(), newName.trimmed());
    return true;
}

bool ProjectApi::remove(const QString &guid)
{
    if (!host.db || !host.services || !host.services->project)
        return fail("project: not available in this session");
    if (host.services->project->isSceneOpen() && host.project->getProjectGuid() == guid)
        return fail("project.remove: this project is open — project.close() first");

    QString name;
    if (resolveGuid(guid, &name) != guid)
        return fail(QStringLiteral("project.remove: no project with guid '%1'").arg(guid));

    // …and a delete cannot reach a folder on a drive that is not there: it must
    // not quietly drop the catalog rows that name files it cannot remove
    // (fix round F1).
    {
        QString whyMissing;
        if (host.services->project->projectLocationMissing(guid, &whyMissing))
            return fail(QStringLiteral("project.remove: %1").arg(whyMissing));
    }

    // Folder first (like the widget), then the DB rows — through the
    // guid-parameterised service: host.project is NOT mutated (§1.6.1).
    if (!host.services->project->removeProject(guid))
        return fail("project.remove: could not remove the project folder");
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
    if (host.projectManager) host.projectManager->populateDesktop(true);
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

    const auto r = ExportService::exportWeb(scene, host.project->getProjectName(), outDir);
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
        QSqlQuery pins(QSqlDatabase::database());
        pins.prepare("SELECT asset_guid FROM project_assets WHERE project_guid = ?");
        pins.addBindValue(projectGuid);
        if (pins.exec())
            while (pins.next()) {
                const QString pinned = pins.value(0).toString();
                if (!pinned.isEmpty() && !guids.contains(pinned)) guids.append(pinned);
            }
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
    CasContentSource source(AssetMetadata::storeRootPath(), projectGuid);
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
    // THE NEW PROJECT IS A TILE NOW, as project.moveToDesktop's move is — the
    // Desktop page's own import adds its tile itself; this verb added none.
    if (host.projectManager) host.projectManager->populateDesktop(true);
    out["guid"] = r.projectGuid;
    out["name"] = r.worldName;
    out["assets"] = r.assets;
    out["objects"] = r.objects;
    return out;
}
