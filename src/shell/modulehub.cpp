/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/modulehub.h"

#include <QDockWidget>
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
    for (StudioModule *m : mModules) m->initialize(ctx);
}

void ModuleHub::contribute(PageHost *pages, ActionHost *actions)
{
    for (StudioModule *m : mModules) {
        Contributions c;
        m->contribute(c);
        if (pages && c.page()) pages->addPage(m->id(), c.page());
        for (const auto &dock : c.docks()) {
            if (!pages || !dock.widget) continue;
            auto *d = new QDockWidget(dock.title);
            d->setObjectName(dock.id);
            d->setWidget(dock.widget);
            pages->addDock(m->id(), d, int(dock.area));
        }
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

QUndoStack *ModuleHub::activateStackFor(const QString &space)
{
    // RESOLVED AT THE CHORD, not at the space switch: a module's stack can
    // change while its space is up (the Materials page's stack is the open
    // TAB's), and the group must name the one the user is looking at.
    QUndoStack *stack = editTarget(space).undoStack;
    if (stack) mUndoGroup->addStack(stack);   // a no-op for a stack already in the group
    mUndoGroup->setActiveStack(stack);
    return stack;
}

bool ModuleHub::undo(const QString &space)
{
    const EditTarget target = editTarget(space);
    if (!activateStackFor(space)) return false;   // no document here: Ctrl+Z is a no-op
    if (target.undo) target.undo();
    else mUndoGroup->undo();
    return true;
}

bool ModuleHub::redo(const QString &space)
{
    const EditTarget target = editTarget(space);
    if (!activateStackFor(space)) return false;
    if (target.redo) target.redo();
    else mUndoGroup->redo();
    return true;
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
