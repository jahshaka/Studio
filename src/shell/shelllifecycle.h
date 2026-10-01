/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SHELLLIFECYCLE_H
#define SHELLLIFECYCLE_H

// ShellLifecycle — THE SHELL'S BIRTH AND DEATH, IN ONE PLACE (D10-SHELL-MODULES).
//
// The library's opening and the whole shutdown order (shell/shutdownorder.h)
// live here, so there is ONE teardown path: the window close (closeEvent ->
// stopBackgroundWork -> the modules) and the CLI exits (--script,
// --dump-api-docs, --engine-selftest, which never close the window) both end
// in teardownWindow(), and the modules are shut down by the hub's run-once
// guard on whichever path gets there first. The shell used to carry two loops
// over its modules for the two paths and rely on every module's shutdown()
// being idempotent (audit S2).
//
//   1 CloseEvent        closeRequested — settle an in-flight open, autosave /
//                       unsaved-changes prompt, geometry + state to settings
//   2 BackgroundWork    stopBackgroundWork — the bounded teardown of every
//                       worker the window owns. Idempotent: closeEvent AND
//                       aboutToQuit land here
//   3 Modules           stopBackgroundWork's tail — StudioModule::shutdown()
//                       on every module, while the engine is still alive
//   4 EngineHostRelease finalizeAppExit (app/cli/scriptrunner.cpp) ->
//                       EngineHost::shutdown(): the render driver stops, the
//                       shader cache and warm-up set are written, the HOST's
//                       shared_ptr is dropped. It does NOT destroy the Engine
//   5 WindowBody        teardownWindow: the undo stack drained first
//                       (incident 1), then the modules, the services and the
//                       Ui:: struct
//   6 EngineViews       destroyEngineViews() — the widgets holding the last
//                       shared_ptr<Engine> are deleted HERE (incident 2), so
//                       ~OgreEngine runs with the database still open
//   7 DatabaseClosed    db->closeDatabase(), last
//   8 WidgetTree        ~QWidget(MainWindow): whatever step 6 did not reach.
//                       Nothing here may touch the database or the engine
//
//  If you add a participant, add it to shutdownorder.h's enum and to this
//  block. The app.shutdown_order gate reads the steps out of the process's
//  own output and fails when they fire twice or out of order.

#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

class Database;
class ModuleHub;
class ProjectService;
class QCloseEvent;
class QMainWindow;
class QUndoStack;
class ScriptEngine;
class SettingsManager;
class UndoService;
struct ScriptHost;
namespace jahshaka { namespace engine { class Engine; } }

class ShellLifecycle : public QObject
{
    Q_OBJECT
public:
    /// What the order needs from the window. Pointers are borrowed; a hook
    /// left unset is a participant this session does not have.
    struct Parts {
        QMainWindow *window = nullptr;
        SettingsManager *settings = nullptr;
        ModuleHub *modules = nullptr;
        ScriptEngine *scriptEngine = nullptr;
        ScriptHost *scriptHost = nullptr;
        ProjectService *projectService = nullptr;
        UndoService *undoService = nullptr;
        QUndoStack *undoStack = nullptr;
        /// An open (or create) in flight, and its settle: finish within the
        /// budget, then abandon whatever is left.
        std::function<bool()> openInFlight;
        std::function<void(int budgetMs)> settleOpen;
        /// Step 2's join of the open runner: abort, then wait. False = did not stop.
        std::function<bool(int budgetMs)> stopOpen;
        /// The force save (autosave / "save before closing?").
        std::function<void()> saveScene;
        /// Writes the EDITOR's dock layout (not the page's) to settings.
        std::function<void()> storeEditorLayout;
        /// The import batches (the tray's and the Assets page's). False = did not stop.
        std::function<bool(int budgetMs)> stopImports;
        /// The MCP endpoint and the Claude chat subprocess.
        std::function<void()> stopAssistant;
        /// Step 5: the services and the Ui:: struct, in that order.
        std::function<void()> deleteServices;
        /// Step 6's epilogue: every pointer into the destroyed widget tree nulled.
        std::function<void()> forgetViews;
    };

    explicit ShellLifecycle(QObject *parent = nullptr);

    /// THE LIBRARY, OPENED: the lock, the database, the tile cache's sources,
    /// the shipped seeds and the background bake rebuild. Returns the database.
    Database *openLibrary();

    void setParts(const Parts &parts);
    /// A non-owning watch on the process's Engine (step 6 proves it died).
    void watchEngine(const std::weak_ptr<jahshaka::engine::Engine> &engine) { mEngineWatch = engine; }

    /// STEP 1: the close event. Accepts or ignores `event`; on accept the
    /// background work is stopped before the window disappears.
    void closeRequested(QCloseEvent *event);
    /// STEP 2 (+3): bounded, run-once. Also wired to aboutToQuit.
    void stopBackgroundWork();
    /// STEPS 5-7: the window body's ordered teardown (~MainWindow). Runs the
    /// modules' shutdown here too when no close ever did (the CLI exits).
    void teardownWindow();

private:
    void destroyEngineViews();

    Parts mParts;
    Database *mDb = nullptr;
    bool mBackgroundStopped = false;
    std::weak_ptr<jahshaka::engine::Engine> mEngineWatch;
};

#endif // SHELLLIFECYCLE_H
