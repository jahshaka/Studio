/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/actionhost.h"

#include <QAction>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

#include "services/shortcutregistry.h"

ActionHost::ActionHost(ShortcutRegistry *registry, QWidget *shortcutParent,
                       std::function<QString()> currentSpace, QObject *parent)
    : QObject(parent), mRegistry(registry), mShortcutParent(shortcutParent),
      mCurrentSpace(std::move(currentSpace))
{
}

bool ActionHost::hasRow(const QString &id) const
{
    for (const Row &row : mRows)
        if (row.id() == id) return true;
    return false;
}

void ActionHost::addRow(const Contributions::Shortcut &shortcut)
{
    if (shortcut.run) handle(shortcut.id, shortcut.space, shortcut.run);
    // A row already defined only gains the handler above: ONE claimant per
    // chord, whoever contributes to it. A contribution with no label is a
    // HANDLER for a row somebody else defines (the Materials page's Space and
    // F), and never defines the row itself, whichever is applied first.
    if (shortcut.label.isEmpty() || hasRow(shortcut.id)) return;
    Row row;
    row.shortcut = shortcut;
    mRows.append(row);
    if (mCommitted) registerRow(row);
}

void ActionHost::addFixedRow(const Contributions::FixedRow &fixedRow)
{
    if (hasRow(fixedRow.id)) return;
    Row row;
    row.fixed = true;
    row.fixedRow = fixedRow;
    mRows.append(row);
    if (mCommitted) registerRow(row);
}

void ActionHost::handle(const QString &id, const QString &space, const std::function<void()> &run)
{
    if (!run) return;
    mHandlers[id].append({ space, run });
}

void ActionHost::apply(const Contributions &contributions)
{
    for (const auto &row : contributions.fixedRows()) addFixedRow(row);
    for (const auto &row : contributions.shortcuts()) addRow(row);
    for (const auto &entry : contributions.toolbarActions()) addToolbarAction(entry.slot, entry.action);
}

void ActionHost::commit()
{
    if (mCommitted) return;
    mCommitted = true;
    // THE ORDER: rows without an anchor in the order they were added; a row
    // with an anchor right after it — after any rows already placed after the
    // same anchor, so two contributions to one place keep their own order.
    QVector<Row> ordered;
    QVector<Row> anchored;
    for (const Row &row : mRows) (row.after().isEmpty() ? ordered : anchored).append(row);
    for (const Row &row : anchored) {
        int at = -1;
        for (int i = 0; i < ordered.size(); ++i)
            if (ordered[i].id() == row.after()) { at = i; break; }
        if (at < 0) { ordered.append(row); continue; }   // an anchor nobody defined: last
        int insert = at + 1;
        while (insert < ordered.size() && ordered[insert].after() == row.after()) ++insert;
        ordered.insert(insert, row);
    }
    mRows = ordered;
    for (const Row &row : mRows) registerRow(row);
}

void ActionHost::registerRow(const Row &row)
{
    if (!mRegistry) return;
    if (row.fixed) {
        mRegistry->addFixed(row.fixedRow.id, row.fixedRow.label, row.fixedRow.category,
                            row.fixedRow.text);
        return;
    }
    const QString id = row.shortcut.id;
    mRegistry->add(id, row.shortcut.label, row.shortcut.category, row.shortcut.keys,
                   mShortcutParent, [this, id]() { dispatch(id); });
}

void ActionHost::dispatch(const QString &id)
{
    trigger(id);
}

bool ActionHost::trigger(const QString &id)
{
    const auto it = mHandlers.constFind(id);
    if (it == mHandlers.cend()) return false;
    const QString space = mCurrentSpace ? mCurrentSpace() : QString();
    // The active space's own handler first; the every-space one only when the
    // space has none — never both.
    for (const Handler &h : it.value())
        if (!h.space.isEmpty() && h.space == space) { h.run(); return true; }
    for (const Handler &h : it.value())
        if (h.space.isEmpty()) { h.run(); return true; }
    return false;
}

void ActionHost::addToolbarSlot(QToolBar *bar, const QString &slot)
{
    if (!bar || slot.isEmpty()) return;
    // An invisible marker: never drawn, never listed (editor.toolbar skips
    // actions with no object name), and the place a contribution goes.
    auto *marker = new QAction(bar);
    marker->setVisible(false);
    bar->addAction(marker);
    mSlots.insert(slot, { bar, marker });
    for (const QPointer<QAction> &action : mPendingToolbar.take(slot))
        if (action) bar->insertAction(marker, action);
}

void ActionHost::addButtonSlot(QToolButton *button, const QString &slot)
{
    if (!button || slot.isEmpty()) return;
    mButtonSlots.insert(slot, button);
    const QVector<QPointer<QAction>> pending = mPendingToolbar.take(slot);
    for (const QPointer<QAction> &action : pending)
        if (action) button->setDefaultAction(action);
}

void ActionHost::addToolbarAction(const QString &slot, QAction *action)
{
    if (!action) return;
    if (const auto b = mButtonSlots.constFind(slot); b != mButtonSlots.cend() && *b) {
        (*b)->setDefaultAction(action);
        return;
    }
    const auto it = mSlots.constFind(slot);
    if (it == mSlots.cend() || !it->bar || !it->marker) {
        mPendingToolbar[slot].append(action);
        return;
    }
    it->bar->insertAction(it->marker, action);
}


