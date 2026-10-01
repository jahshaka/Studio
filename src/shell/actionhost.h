/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ACTIONHOST_H
#define ACTIONHOST_H

// ActionHost — the ShortcutRegistry EXTENDED TO ACTIONS, MENUS AND TOOLBAR SLOTS
// (D10-SHELL-MODULES; audit S1 "module chords are registered by the shell").
//
// Three things live here:
//
//  * THE ROWS. Every keyboard action is ONE registry row (persisted binding,
//    conflict-checked rebinding, the generated Preferences table) whatever owns
//    it — the shell, a module, the Claude assistant. A row is defined once; its
//    HANDLERS are contributed separately and are SPACE-SCOPED: when the chord
//    fires, the handler registered for the active space runs, else the one
//    registered for every space, else nothing. That is the single-claimant rule
//    the old `if (currentSpace == ...)` chains enforced by hand (Qt drops a
//    chord that has two WindowShortcut claimants), written once.
//
//  * THE ORDER. The registry lists rows in registration order and the
//    Preferences page groups consecutive categories, so a contributed row says
//    where it goes (`after`). Rows are collected first and registered in one
//    pass at commit(), when every anchor exists.
//
//  * THE SLOTS. A toolbar slot is a named insertion point (an invisible marker
//    action); a menu is registered by name. A contribution lands in either
//    without the shell knowing what it is.

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <functional>

#include "modules/studiomodule.h"

class QAction;
class QMenu;
class QToolBar;
class QWidget;
class ShortcutRegistry;

class ActionHost : public QObject
{
    Q_OBJECT
public:
    /// `registry` is borrowed; QShortcuts are parented to `shortcutParent`.
    /// `currentSpace` answers which space is on screen when a chord fires.
    ActionHost(ShortcutRegistry *registry, QWidget *shortcutParent,
               std::function<QString()> currentSpace, QObject *parent = nullptr);

    ShortcutRegistry *registry() const { return mRegistry; }

    /// Defines a row (and, when `row.run` is set, its handler for `row.space`).
    /// A row whose id is already defined — or a row with no label — only adds
    /// the handler.
    void addRow(const Contributions::Shortcut &row);
    void addFixedRow(const Contributions::FixedRow &row);
    /// A handler for an existing row on `space` ("" = every space).
    void handle(const QString &id, const QString &space, const std::function<void()> &run);
    /// Applies a module's contributed rows, toolbar actions and menu rows.
    void apply(const Contributions &contributions);

    /// Registers every row collected so far, in order, anchors resolved. Rows
    /// added afterwards register at once (appended).
    void commit();
    bool committed() const { return mCommitted; }

    /// Fires `id` as its chord would: the active space's handler, else the
    /// every-space one. False when no handler ran.
    bool trigger(const QString &id);
    bool hasRow(const QString &id) const;

    /// A named insertion point at the CURRENT end of `bar`.
    void addToolbarSlot(QToolBar *bar, const QString &slot);
    /// Inserts `action` at the end of `slot` (before its marker). A slot that
    /// does not exist YET (the modules contribute before the toolbar is
    /// built) keeps the action until it does.
    void addToolbarAction(const QString &slot, QAction *action);

    /// A named menu; rows contributed before it existed land in it now.
    void registerMenu(const QString &id, QMenu *menu);
    void addMenuRow(const QString &menu, QAction *action);
    QMenu *menu(const QString &id) const;

private:
    struct Row {
        bool fixed = false;
        Contributions::Shortcut shortcut;
        Contributions::FixedRow fixedRow;
        QString id() const { return fixed ? fixedRow.id : shortcut.id; }
        QString after() const { return fixed ? fixedRow.after : shortcut.after; }
    };
    struct Handler {
        QString space;
        std::function<void()> run;
    };
    void registerRow(const Row &row);
    void dispatch(const QString &id);

    ShortcutRegistry *mRegistry = nullptr;
    QPointer<QWidget> mShortcutParent;
    std::function<QString()> mCurrentSpace;
    QVector<Row> mRows;
    QHash<QString, QVector<Handler>> mHandlers;
    bool mCommitted = false;
    struct Slot { QPointer<QToolBar> bar; QPointer<QAction> marker; };
    QHash<QString, Slot> mSlots;
    QHash<QString, QPointer<QMenu>> mMenus;
    QHash<QString, QVector<QPointer<QAction>>> mPendingToolbar;
    QHash<QString, QVector<QPointer<QAction>>> mPendingMenu;
};

#endif // ACTIONHOST_H
