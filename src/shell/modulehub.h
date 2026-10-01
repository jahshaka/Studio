/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MODULEHUB_H
#define MODULEHUB_H

// ModuleHub — THE SHELL'S ONE LOOP OVER ITS MODULES (D10-SHELL-MODULES).
//
// The shell used to hold five typed module pointers and reach into them: 18
// calls into the materials page, `avatarModule->api()` for an asset open, the
// module chords registered by hand. Everything the shell asks of a module now
// goes through StudioModule v2's hooks, here, in the order studiomodule.h
// documents — so the order is written once and `shell.contract` can drive it
// with a fake module and no window at all.
//
// It also routes the EDIT CHORDS by the active space (EditTarget): the
// space's own target, a module's or one the shell registers for a space it
// owns itself (the editor), or nothing — a page with no document answers no
// chord.

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

#include "modules/studiomodule.h"

class ActionHost;
class PageHost;
class QUndoGroup;

class ModuleHub : public QObject
{
    Q_OBJECT
public:
    explicit ModuleHub(QObject *parent = nullptr);
    ~ModuleHub() override;

    /// Takes ownership of `modules` (deleted by releaseModules / the dtor).
    void setModules(const QVector<StudioModule *> &modules);
    const QVector<StudioModule *> &modules() const { return mModules; }
    /// The module with that id, or null — the shell's only way to name one.
    StudioModule *module(const QString &id) const;

    // ---- boot: initialize -> contribute -> registerApi ------------------
    void initialize(StudioContext &ctx);
    /// Each module's Contributions: its page into `pages` under its id, its
    /// rows and toolbar actions into `actions`, its asset kinds into the hub's
    /// routing table.
    void contribute(PageHost *pages, ActionHost *actions);
    void registerApi(ScriptEngine &engine);

    // ---- the session ----------------------------------------------------
    void projectChanged(Project *project);
    void spaceChanged(const QString &from, const QString &to);
    /// Routes `ref` to the module that contributed `ref.kind`. False when no
    /// module owns the kind or the module declined.
    bool openAsset(const AssetRef &ref);

    // ---- the edit chords ------------------------------------------------
    /// A space the SHELL owns (no module) that still has a document: its
    /// target is asked for each time a chord fires.
    void setSpaceEditTarget(const QString &space, const std::function<EditTarget()> &provider);
    /// The target of `space`: the shell's provider, else the module whose id
    /// is the space, else none.
    EditTarget editTarget(const QString &space) const;
    enum class Edit { Delete, Duplicate, Copy, Cut, Paste, SelectAll };
    /// Runs `edit` on the space's target. False when the space does not
    /// answer it (nothing happens — never a fallback to another space).
    bool runEdit(const QString &space, Edit edit);
    /// Ctrl+Z / Ctrl+Shift+Z: the space's stack becomes the group's ACTIVE
    /// stack, then it moves. A space with no stack has no active stack and the
    /// chord does nothing (audit S4a). False when nothing moved.
    bool undo(const QString &space);
    bool redo(const QString &space);
    QUndoGroup *undoGroup() const { return mUndoGroup; }
    /// The group's active stack for `space`, resolved now (null = none).
    QUndoStack *activateStackFor(const QString &space);

    // ---- teardown -------------------------------------------------------
    /// abortBackgroundWork() on every module (stop, do not join).
    void abortBackgroundWork();
    /// shutdown() on every module, EXACTLY ONCE however many exit paths ask.
    void shutdownModules();
    bool modulesShutDown() const { return mShutDown; }
    /// shutdownModules() if not done, then deletes the modules.
    void releaseModules();

private:
    QVector<StudioModule *> mModules;
    QHash<QString, StudioModule *> mAssetKinds;
    QHash<QString, std::function<EditTarget()>> mShellTargets;
    QUndoGroup *mUndoGroup = nullptr;
    bool mShutDown = false;
};

#endif // MODULEHUB_H
