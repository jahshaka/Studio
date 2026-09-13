/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef STUDIOMODULE_H
#define STUDIOMODULE_H

// StudioModule — the module pattern (APP_ARCHITECTURE_AUDIT §6.2).
//
// A module is a feature domain that owns a page and/or docks, contributes API
// verbs, and could in principle live in its own repo. The shell owns a
// QVector<StudioModule*> and drives every module through this one interface:
// page/menu/dock wiring is a loop, not five ad-hoc calls.
//
// Rules a module must obey (§6.2): no mainwindow.h include; no ambient-state
// reach (everything it needs arrives in ModuleHost); no class named
// MainWindow; verbs registered through registerApi are its real interface
// (the API-first rule applies to modules with no exception).
//
// Registration is static (a compiled-in vector) by design — §10.4: dynamic
// plugin loading adds ABI fragility a single-binary product never redeems.

#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class Database;
class SettingsManager;
class ScriptEngine;
class EngineHost;
class IEditorViewport;
class Project;
struct StudioServices;

/// Everything the shell hands a module at initialize() time. Members are
/// nullable — headless hosts fill in what they have (ScriptHost's pattern,
/// generalized).
struct ModuleHost
{
    Database        *db        = nullptr;
    SettingsManager *settings  = nullptr;
    IEditorViewport *viewport  = nullptr;
    EngineHost      *engine    = nullptr;   ///< the running engine host, or null (headless)
    StudioServices  *services  = nullptr;
    Project         *project   = nullptr;   ///< the one live Project instance
    QWidget         *shellWidget = nullptr; ///< parent for pages/dialogs (the shell window as a QWidget)
};

/// A dock panel a module contributes to the shell's window.
struct DockContribution
{
    QString  title;
    QWidget *widget = nullptr;
    Qt::DockWidgetArea area = Qt::RightDockWidgetArea;
};

class StudioModule
{
public:
    virtual ~StudioModule() = default;

    /// Stable identifier: "materials", "publish", ...
    virtual QString id() const = 0;

    /// Build the module's internals from the host context. Called once by the
    /// shell, after services exist and before the page is requested.
    virtual void initialize(ModuleHost &host) = 0;

    /// The module's stacked page, if it has one (owned by the caller once
    /// added to the stack). Called after initialize().
    virtual QWidget *createPage() { return nullptr; }

    /// Dock panels the module contributes (none by default).
    virtual QVector<DockContribution> docks() { return {}; }

    /// The module's verbs — its real interface. Modules register ApiModules
    /// on the scripting engine exactly as the built-in domains do.
    virtual void registerApi(ScriptEngine &) {}

    /// STOP, DO NOT JOIN — the first thing the shell asks of every module when
    /// the window closes (CLEANUP-1 item 2, MainWindow::shutdownBackgroundWork).
    ///
    /// A module's workers run on the SAME global thread pool the shell waits
    /// on, and that wait is bounded at 3 s with `std::_Exit(0)` behind it. A
    /// module whose abort lives in shutdown() — which runs AFTER that wait — is
    /// therefore never asked to stop before the process can be taken away from
    /// it: the avatar module's import runner kept parsing for twelve seconds
    /// while the shell counted to three, and a definition edit still inside its
    /// 250 ms write-coalescing window was simply lost.
    ///
    /// So this hook must (a) FLUSH anything unsaved, right now, synchronously;
    /// (b) ask every worker it owns to stop; and (c) RETURN — the joining is
    /// the shell's pool wait, a few lines later, and a module that blocks here
    /// has only moved the problem. shutdown() still does the ordered teardown.
    /// Default: nothing, for a module with no background work.
    virtual void abortBackgroundWork() {}

    virtual void shutdown() = 0;
};

#endif // STUDIOMODULE_H
