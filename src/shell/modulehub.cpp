/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/modulehub.h"

#include <QAction>
#include <QUndoGroup>
#include <QUndoStack>

#include "shell/actionhost.h"
#include "shell/pagehost.h"

ModuleHub::ModuleHub(QObject *parent) : QObject(parent), mUndoGroup(new QUndoGroup(this))
{
}

ModuleHub::~ModuleHub()
{
    // The window releases the modules on its own teardown path (in the order
    // shell/shutdownorder.h writes down); this only catches a hub that was
    // never part of a window — the contract test's.
    qDeleteAll(mModules);
    mModules.clear();
}

void ModuleHub::setModules(const QVector<StudioModule *> &modules)
{
    mModules = modules;
}

StudioModule *ModuleHub::module(const QString &id) const
{
    for (StudioModule *m : mModules)
        if (m && m->id() == id) return m;
    return nullptr;
}

void ModuleHub::initialize(StudioContext &ctx)
{
    ctx.editTargetChanged = [this]() { syncActiveStack(); };
    for (StudioModule *m : mModules) m->initialize(ctx);
}

void ModuleHub::contribute(PageHost *pages, ActionHost *actions)
{
    for (StudioModule *m : mModules) {
        Contributions c;
        m->contribute(c);
        if (pages && c.page()) pages->addPage(m->id(), c.page());
        if (actions) actions->apply(c);
        for (const QString &kind : c.assetKinds()) mAssetKinds.insert(kind, m);
    }
}

void ModuleHub::registerApi(ScriptEngine &engine)
{
    for (StudioModule *m : mModules) m->registerApi(engine);
}

void ModuleHub::projectChanged(Project *project)
{
    for (StudioModule *m : mModules) m->onProjectChanged(project);
}

void ModuleHub::spaceChanged(const QString &from, const QString &to)
{
    for (StudioModule *m : mModules) m->onSpaceChanged(from, to);
    mSpace = to;
    syncActiveStack();
}

bool ModuleHub::openAsset(const AssetRef &ref)
{
    StudioModule *m = mAssetKinds.value(ref.kind, nullptr);
    return m && m->openAsset(ref);
}

void ModuleHub::setSpaceEditTarget(const QString &space, const std::function<EditTarget()> &provider)
{
    mShellTargets.insert(space, provider);
}

EditTarget ModuleHub::editTarget(const QString &space) const
{
    const auto it = mShellTargets.constFind(space);
    if (it != mShellTargets.cend() && it.value()) return it.value()();
    if (StudioModule *m = module(space)) return m->editTarget();
    return EditTarget();
}

bool ModuleHub::runEdit(const QString &space, Edit edit)
{
    const EditTarget target = editTarget(space);
    std::function<void()> run;
    switch (edit) {
    case Edit::Delete:    run = target.deleteSelection; break;
    case Edit::Duplicate: run = target.duplicateSelection; break;
    case Edit::Copy:      run = target.copySelection; break;
    case Edit::Cut:       run = target.cutSelection; break;
    case Edit::Paste:     run = target.paste; break;
    case Edit::SelectAll: run = target.selectAll; break;
    }
    if (!run) return false;
    run();
    return true;
}

void ModuleHub::syncActiveStack()
{
    // AT THE SWITCH, AND WHEN THE MODULE SAYS SO: a module's stack can change
    // while its space is up (the Materials page's stack is the open TAB's),
    // so the module calls StudioContext::editTargetChanged and the group
    // follows. A stack deleted while in the group leaves it by itself
    // (~QUndoStack), so the group never points at freed memory.
    QUndoStack *stack = editTarget(mSpace).undoStack;
    if (stack && !mUndoGroup->stacks().contains(stack)) mUndoGroup->addStack(stack);
    mUndoGroup->setActiveStack(stack);
}

bool ModuleHub::undo()
{
    QUndoStack *active = mUndoGroup->activeStack();
    if (!active) return false;   // no document here: Ctrl+Z is a no-op
    const EditTarget target = editTarget(mSpace);
    if (target.undoStack == active && target.undo) target.undo();
    else mUndoGroup->undo();
    return true;
}

bool ModuleHub::redo()
{
    QUndoStack *active = mUndoGroup->activeStack();
    if (!active) return false;
    const EditTarget target = editTarget(mSpace);
    if (target.undoStack == active && target.redo) target.redo();
    else mUndoGroup->redo();
    return true;
}

QAction *ModuleHub::createUndoAction(QObject *parent)
{
    // Qt's action follows the group (enabled = canUndo, text = the command);
    // its trigger is re-pointed from QUndoGroup::undo to undo() so the space
    // target's own path (the edit gate, a repaint) is the one that runs.
    QAction *action = mUndoGroup->createUndoAction(parent);
    QObject::disconnect(action, &QAction::triggered, mUndoGroup, &QUndoGroup::undo);
    connect(action, &QAction::triggered, this, [this]() { undo(); });
    return action;
}

QAction *ModuleHub::createRedoAction(QObject *parent)
{
    QAction *action = mUndoGroup->createRedoAction(parent);
    QObject::disconnect(action, &QAction::triggered, mUndoGroup, &QUndoGroup::redo);
    connect(action, &QAction::triggered, this, [this]() { redo(); });
    return action;
}

void ModuleHub::abortBackgroundWork()
{
    for (StudioModule *m : mModules)
        if (m) m->abortBackgroundWork();
}

void ModuleHub::shutdownModules()
{
    // ONCE. The window close (step 3 of the shutdown order) and the CLI exits
    // (--script, --dump-api-docs: no closeEvent, no aboutToQuit) both reach the
    // modules' teardown; this guard is what lets the contract say "exactly
    // once" instead of every module having to be idempotent (audit S2).
    if (mShutDown) return;
    mShutDown = true;
    for (StudioModule *m : mModules)
        if (m) m->shutdown();
}

void ModuleHub::releaseModules()
{
    shutdownModules();
    // A module's stack may be in the group; the group must not outlive it
    // pointing at freed memory.
    for (QUndoStack *s : mUndoGroup->stacks()) mUndoGroup->removeStack(s);
    qDeleteAll(mModules);
    mModules.clear();
    mAssetKinds.clear();
}
