/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SHUTDOWNORDER_H
#define SHUTDOWNORDER_H

#include <QObject>

// THE SHUTDOWN ORDER — one enumeration, one recorder
// (STABILITY_PROGRAM_SPEC.md §1.5 + Lane 3).
//
// Nobody had written the order down, and twice that cost a defect:
//
//   * `740e0155` (2026-09-03) — undo commands write to the database when they
//     die (DeleteSceneNodeCommand finalises an asset row once no undo can
//     reach the delete any more), and the undo stack is parented to the
//     window, so it used to be destroyed AFTER ~MainWindow's body ran
//     closeDatabase(). Every pending asset delete failed against a closed
//     connection, silently: the SQLite driver's only complaint was
//     "Parameter count mismatch" at [info] level. Step WindowBody drains it.
//
//   * The ENGINE TEARDOWN LAW (irisgl/engine/src/OgreEngine.cpp, and
//     CLAUDE.md: "workspaces -> scenes -> drop every MeshPtr -> delete Root";
//     a MeshPtr outliving Root throws inside VaoManager). The Engine is a
//     shared_ptr and the VIEWPORTS hold copies, so EngineHost::shutdown() —
//     despite the name — does NOT destroy it. Before this lane, the last
//     reference dropped when Qt destroyed MainWindow's child widget tree,
//     i.e. AFTER closeDatabase(): exactly the shape of the bug the first
//     incident fixed one level up, waiting for someone to add "save the last
//     camera on teardown". Step EngineViews now destroys those widgets
//     explicitly, so the Engine dies BEFORE the database closes and inside a
//     step with a name.
//
// The macro is a DEBUG-BUILD hook. It costs one function call per step and it
// is what keeps the comment block honest: the recorder warns when a step fires
// twice or out of order, and the app.shutdown_order gate reads the lines it
// prints. In a release build it compiles to nothing.

namespace ShutdownOrder {

/// The participants, in the order they must run. Numbered because the gate
/// asserts the numbers.
enum Step {
    CloseEvent        = 1,  ///< MainWindow::closeEvent — settle an in-flight open,
                            ///  autosave / unsaved-changes prompt, donate dialog,
                            ///  geometry+state to settings
    BackgroundWork    = 2,  ///< MainWindow::shutdownBackgroundWork — the bounded
                            ///  teardown of every worker: 20 s force-exit thread,
                            ///  open runner, import batches, ARCHIVE runners, MCP,
                            ///  Claude chat, thumbnails, the global pool, the
                            ///  main-thread watchdog. Idempotent (closeEvent AND
                            ///  aboutToQuit both land here)
    EngineHostRelease = 3,  ///< finalizeAppExit -> EngineHost::shutdown(): render
                            ///  driver stopped and deleted, shader cache + warm-up
                            ///  set written, the HOST's shared_ptr dropped. Does
                            ///  NOT destroy the Engine — the viewports still hold it
    WindowBody        = 4,  ///< ~MainWindow body: undoStack->clear() (incident 1),
                            ///  then the services and the Ui:: struct
    EngineViews       = 5,  ///< ~MainWindow body: the engine-holding widgets are
                            ///  destroyed HERE, so the last shared_ptr<Engine> drops
                            ///  and ~OgreEngine runs (incident 2) while the database
                            ///  is still open
    DatabaseClosed    = 6,  ///< ~MainWindow body: db->closeDatabase(), last
    WidgetTree        = 7,  ///< ~QWidget(MainWindow): whatever step 5 did not reach.
                            ///  Nothing here may touch the database or the engine
};

/// Records a step: warns if it fires twice or out of order, and prints
/// "[shutdown] step N/7 <name>" so a spawned-process gate can read the
/// sequence out of the app's own output. No-op in release builds.
void record(int step, const char *name);

/// Step 7 has no code of its own — it IS ~QWidget(MainWindow) destroying the
/// child tree, which no destructor body can hook. So the window keeps one of
/// these as a plain QObject child: QWidget's destructor deletes its children,
/// this one records the step on its way out, and because step 5 only deletes
/// child WIDGETS it is still there to do it.
class WidgetTreeMarker : public QObject
{
public:
    explicit WidgetTreeMarker(QObject *parent = nullptr) : QObject(parent) {}
    ~WidgetTreeMarker() override;
};

}   // namespace ShutdownOrder

#ifdef QT_DEBUG
#  define JAH_SHUTDOWN_STEP(step, name) ::ShutdownOrder::record(int(step), name)
#else
#  define JAH_SHUTDOWN_STEP(step, name) do {} while (0)
#endif

#endif   // SHUTDOWNORDER_H
