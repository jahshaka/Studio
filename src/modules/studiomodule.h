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

// StudioModule v2 — the module contract (D10-SHELL-MODULES; the 2026-09-30
// architecture audit, area 5 "Next version").
//
// A module is a feature domain: it owns a page, contributes its shortcuts and
// toolbar actions, opens the asset kinds it
// owns, routes the edit chords of its own space, and registers API verbs. The
// shell holds a list of StudioModule* and drives every one through this one
// interface — it never names a module's class and never calls into a module's
// widgets (the shell's `ModuleHub` is where that loop lives).
//
// THE ORDER THE SHELL CALLS THE HOOKS IN (asserted by `shell.contract`):
//
//   boot     initialize(ctx) -> contribute(c) -> registerApi(engine)
//   open     onProjectChanged(project)        (the scene is bound)
//   create   onProjectChanged(project)        (the new scene is bound)
//   close    onProjectChanged(nullptr)        (the scene is torn down)
//   switch   onSpaceChanged(from, to)          (every module, every switch)
//   edit     editTarget()                     (resolved when a chord fires)
//   asset    openAsset(ref)                   (a page asks for a kind it owns)
//   quit     abortBackgroundWork() -> shutdown()   (each exactly once)
//
// Rules a module must obey: no `shell/` include (tests/hygiene guards it); no
// ambient-state reach (everything it needs arrives in StudioContext); verbs
// registered through registerApi are its real interface — the API-first rule
// applies to modules with no exception, and every button a module contributes
// calls a verb's implementation, never a second path.
//
// Registration is static (a compiled-in list, modules/moduleregistry.cpp) by
// design: dynamic plugin loading adds ABI fragility a single-binary product
// never redeems.

#include <QKeySequence>
#include <QStringList>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class Database;
class SettingsManager;
class ScriptEngine;
class EngineHost;
class IEditorViewport;
class IShellView;
class Project;
class QAction;
class QUndoStack;
struct StudioServices;

/// Everything the shell hands a module at initialize() time — the ONE context
/// bag (it replaced ModuleHost). Members are nullable: headless hosts fill in
/// what they have (ScriptHost's pattern, generalized).
struct StudioContext
{
    Database        *db        = nullptr;
    SettingsManager *settings  = nullptr;
    IEditorViewport *viewport  = nullptr;
    EngineHost      *engine    = nullptr;   ///< the running engine host, or null (headless)
    StudioServices  *services  = nullptr;
    Project         *project   = nullptr;   ///< the one live Project instance
    QWidget         *shellWidget = nullptr; ///< parent for pages/dialogs (the shell window as a QWidget)
    IShellView      *shell     = nullptr;   ///< the shell's view surface (spaces, toasts, panels), or null
};

/// What a module adds to the shell. Collected from contribute() and applied by
/// the shell: the page goes into the PageHost under the module's id, the rows
/// into the ActionHost (ShortcutRegistry + toolbar slots).
class Contributions
{
public:
    /// A registry row with a handler. `space` empty = the handler runs on any
    /// space; otherwise only while that space (a module id / space name) is
    /// active. `after` places the row after an existing row id in the
    /// registry's order (the Preferences page lists rows in that order); empty
    /// appends. A row whose id the shell already defines adds a HANDLER to it
    /// instead — one registry claimant per chord, routed by the active space.
    struct Shortcut {
        QString id;
        QString label;
        QString category;
        QKeySequence keys;
        std::function<void()> run;
        QString space;
        QString after;
    };
    /// A read-only registry row (a held or mouse input listed for discovery).
    struct FixedRow {
        QString id;
        QString label;
        QString category;
        QString text;
        QString after;
    };
    /// An action into a named toolbar slot ("editor.camera", "editor.end").
    struct ToolbarAction {
        QString slot;
        QAction *action = nullptr;
    };

    void setPage(QWidget *page) { mPage = page; }
    void addShortcut(const Shortcut &s) { mShortcuts.append(s); }
    void addFixedRow(const FixedRow &r) { mFixedRows.append(r); }
    void addToolbarAction(const QString &slot, QAction *action) { mToolbar.append({ slot, action }); }
    /// An asset KIND this module opens (AssetView / the tray ask by kind).
    void opensAssetKind(const QString &kind) { mAssetKinds.append(kind); }

    QWidget *page() const { return mPage; }
    const QVector<Shortcut> &shortcuts() const { return mShortcuts; }
    const QVector<FixedRow> &fixedRows() const { return mFixedRows; }
    const QVector<ToolbarAction> &toolbarActions() const { return mToolbar; }
    const QStringList &assetKinds() const { return mAssetKinds; }

private:
    QWidget *mPage = nullptr;
    QVector<Shortcut> mShortcuts;
    QVector<FixedRow> mFixedRows;
    QVector<ToolbarAction> mToolbar;
    QStringList mAssetKinds;
};

/// WHAT THE EDIT CHORDS MEAN ON A MODULE'S SPACE. `undoStack` joins the shell's
/// QUndoGroup and is the ACTIVE stack while the space is up; null = the space
/// has no document, and Ctrl+Z there is a no-op (audit S4a: it used to undo
/// the scene, invisibly). `undo`/`redo` replace the plain stack call when the
/// module must do more than move the stack (a repaint, an edit gate). A null
/// handler is a chord this space does not answer.
struct EditTarget
{
    QUndoStack *undoStack = nullptr;
    std::function<void()> undo;
    std::function<void()> redo;
    std::function<void()> deleteSelection;
    std::function<void()> duplicateSelection;
    std::function<void()> copySelection;
    std::function<void()> cutSelection;
    std::function<void()> paste;
    std::function<void()> selectAll;
};

/// A request to open/use a library asset in the module that owns its kind.
struct AssetRef
{
    enum class Intent {
        Open,     ///< edit it in the module's page (switches to the module's space)
        Spawn,    ///< instantiate it into the open scene
        Assign    ///< put it on the scene node `targetGuid` (a clip on a body)
    };
    QString guid;
    QString kind;
    QString scope;                ///< "library" | "project" (Open)
    Intent intent = Intent::Open;
    bool hasPosition = false;     ///< Spawn: at `position`, else in front of the camera
    float position[3] = { 0.0f, 0.0f, 0.0f };
    QString targetGuid;           ///< Assign: the node; empty = nothing under the cursor
    QString targetName;
};

class StudioModule
{
public:
    virtual ~StudioModule() = default;

    /// Stable identifier: "materials", "publish", ... — also the id of the
    /// module's space and of its page in the PageHost.
    virtual QString id() const = 0;

    /// Build the module's internals from the context. Called once, after the
    /// services exist and before contribute().
    virtual void initialize(StudioContext &ctx) = 0;

    /// Everything the module adds to the shell: its page, shortcuts, toolbar
    /// actions and asset kinds. Called once, after initialize().
    /// OWNERSHIP: the PAGE becomes the shell's (reparented into the PageHost's
    /// stack). A toolbar ACTION stays the module's — the shell only places it
    /// (QWidget::insertAction takes no ownership), so the module keeps it
    /// alive while placed and deletes it in its own teardown (VrModule holds
    /// its action in a unique_ptr; a QAction removes itself from the bar).
    virtual void contribute(Contributions &) {}

    /// The verbs — the module's real interface. Called once, after contribute().
    virtual void registerApi(ScriptEngine &) {}

    /// The open project changed: a scene was bound (open or create — the
    /// project), or torn down (close — nullptr).
    virtual void onProjectChanged(Project *) {}

    /// The shell switched spaces; `from`/`to` are space ids ("desktop",
    /// "editor", "materials", ...). Every module hears every switch.
    virtual void onSpaceChanged(const QString &from, const QString &to) { Q_UNUSED(from); Q_UNUSED(to); }

    /// What the edit chords do while this module's space is active. Resolved
    /// each time a chord fires, so a stack that changes with the module's own
    /// state (a material tab) is always the live one. Default: no document.
    virtual EditTarget editTarget() { return EditTarget(); }

    /// Open/spawn/assign an asset of a kind this module contributed. Returns
    /// false when the module does not handle the request.
    virtual bool openAsset(const AssetRef &) { return false; }

    /// STOP, DO NOT JOIN — the first thing the shell asks of every module when
    /// the window closes (CLEANUP-1 item 2, ShellLifecycle::stopBackgroundWork).
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

    /// The ordered teardown, while the engine is still up. The shell calls it
    /// EXACTLY ONCE per module, on every exit path (ShellLifecycle runs it
    /// behind a run-once guard — the window close and the CLI exits share it),
    /// so a module need not make it idempotent.
    virtual void shutdown() = 0;
};

#endif // STUDIOMODULE_H
