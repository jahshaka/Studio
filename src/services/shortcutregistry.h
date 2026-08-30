/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SHORTCUTREGISTRY_H
#define SHORTCUTREGISTRY_H

// ShortcutRegistry — the editor's remappable keyboard bindings (EDITOR_SHORTCUTS_SPEC §1).
//
// One entry per action: {id, label, category, default QKeySequence, current}.
// The registry owns the QShortcut wiring MainWindow::setupShortcuts used to do
// by hand, persists user overrides in jahsettings.ini under "shortcut/<id>",
// and refuses a rebind that would collide with another entry — the Preferences
// Shortcuts page is generated from entries().
//
// Keys that cannot be plain QShortcuts (the RMB-held fly keys, held-Ctrl snap,
// held-V vertex snap, Alt+drag duplicate, the mouse wheel) are registered as
// FIXED rows: they appear in the table for discoverability but cannot be
// remapped (their handling lives in the viewport's event code).

#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

class QSettings;
class QShortcut;
class QWidget;

class ShortcutRegistry : public QObject
{
    Q_OBJECT
public:
    struct Entry {
        QString id;
        QString label;
        QString category;
        QKeySequence defaultSequence;
        QKeySequence sequence;      // current binding (may be empty = unbound)
        QString fixedText;          // non-empty = read-only row, no QShortcut
        QShortcut *shortcut = nullptr;
    };

    /// `settings` (nullable) is where overrides persist; not owned.
    explicit ShortcutRegistry(QSettings *settings, QObject *parent = nullptr);

    /// Registers a remappable action and creates its QShortcut on `parent`
    /// (Qt::WindowShortcut by default, like the hand-rolled ones before).
    /// A persisted override ("shortcut/<id>") wins over `defaultSequence`.
    /// Returns the created shortcut (null only on a duplicate id).
    QShortcut *add(const QString &id, const QString &label, const QString &category,
                   const QKeySequence &defaultSequence, QWidget *parent,
                   const std::function<void()> &activated,
                   Qt::ShortcutContext context = Qt::WindowShortcut);

    /// A read-only row (input the shortcut system cannot express as a
    /// QKeySequence) — shown in the table, never remappable.
    void addFixed(const QString &id, const QString &label, const QString &category,
                  const QString &displayText);

    /// Rebinds `id` (empty = unbind). Refuses (false) an unknown/fixed id or a
    /// sequence already used by another entry — `conflictId` then names it.
    bool setBinding(const QString &id, const QKeySequence &sequence,
                    QString *conflictId = nullptr);

    /// Back to the default binding (also clears the persisted override).
    bool resetBinding(const QString &id);
    void resetAll();

    QKeySequence sequence(const QString &id) const;
    bool contains(const QString &id) const;
    /// Registration order — the settings page renders in this order.
    const QVector<Entry> &entries() const { return mEntries; }

signals:
    /// A binding changed (rebind, reset, reset-all) — the settings page refreshes.
    void bindingsChanged();

private:
    int indexOf(const QString &id) const;
    void persist(const Entry &e);

    QSettings *mSettings = nullptr;
    QVector<Entry> mEntries;
};

#endif // SHORTCUTREGISTRY_H
