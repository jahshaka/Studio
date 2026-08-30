/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/shortcutregistry.h"

#include <QSettings>
#include <QShortcut>
#include <QWidget>

namespace {
QString settingsKey(const QString &id) { return QStringLiteral("shortcut/") + id; }
}

ShortcutRegistry::ShortcutRegistry(QSettings *settings, QObject *parent)
    : QObject(parent), mSettings(settings)
{
}

int ShortcutRegistry::indexOf(const QString &id) const
{
    for (int i = 0; i < mEntries.size(); ++i)
        if (mEntries[i].id == id) return i;
    return -1;
}

QShortcut *ShortcutRegistry::add(const QString &id, const QString &label, const QString &category,
                                 const QKeySequence &defaultSequence, QWidget *parent,
                                 const std::function<void()> &activated,
                                 Qt::ShortcutContext context)
{
    if (indexOf(id) >= 0) return nullptr;   // duplicate registration is a bug

    Entry e;
    e.id = id;
    e.label = label;
    e.category = category;
    e.defaultSequence = defaultSequence;
    e.sequence = defaultSequence;
    // A persisted override wins — the empty string is a deliberate "unbound".
    if (mSettings && mSettings->contains(settingsKey(id)))
        e.sequence = QKeySequence(mSettings->value(settingsKey(id)).toString(),
                                  QKeySequence::PortableText);

    e.shortcut = new QShortcut(e.sequence, parent);
    e.shortcut->setContext(context);
    if (activated) QObject::connect(e.shortcut, &QShortcut::activated, this, activated);
    mEntries.append(e);
    return e.shortcut;
}

void ShortcutRegistry::addFixed(const QString &id, const QString &label, const QString &category,
                                const QString &displayText)
{
    if (indexOf(id) >= 0) return;
    Entry e;
    e.id = id;
    e.label = label;
    e.category = category;
    e.fixedText = displayText;
    mEntries.append(e);
}

bool ShortcutRegistry::setBinding(const QString &id, const QKeySequence &sequence,
                                  QString *conflictId)
{
    const int idx = indexOf(id);
    if (idx < 0 || !mEntries[idx].fixedText.isEmpty()) return false;
    if (!sequence.isEmpty()) {
        for (const Entry &other : mEntries) {
            if (other.id == id || !other.fixedText.isEmpty()) continue;
            if (!other.sequence.isEmpty() && other.sequence == sequence) {
                if (conflictId) *conflictId = other.id;
                return false;
            }
        }
    }
    Entry &e = mEntries[idx];
    e.sequence = sequence;
    if (e.shortcut) e.shortcut->setKey(sequence);
    persist(e);
    emit bindingsChanged();
    return true;
}

bool ShortcutRegistry::resetBinding(const QString &id)
{
    const int idx = indexOf(id);
    if (idx < 0 || !mEntries[idx].fixedText.isEmpty()) return false;
    Entry &e = mEntries[idx];
    e.sequence = e.defaultSequence;
    if (e.shortcut) e.shortcut->setKey(e.sequence);
    if (mSettings) mSettings->remove(settingsKey(e.id));
    emit bindingsChanged();
    return true;
}

void ShortcutRegistry::resetAll()
{
    for (Entry &e : mEntries) {
        if (!e.fixedText.isEmpty()) continue;
        e.sequence = e.defaultSequence;
        if (e.shortcut) e.shortcut->setKey(e.sequence);
        if (mSettings) mSettings->remove(settingsKey(e.id));
    }
    emit bindingsChanged();
}

QKeySequence ShortcutRegistry::sequence(const QString &id) const
{
    const int idx = indexOf(id);
    return idx < 0 ? QKeySequence() : mEntries[idx].sequence;
}

bool ShortcutRegistry::contains(const QString &id) const
{
    return indexOf(id) >= 0;
}

void ShortcutRegistry::persist(const Entry &e)
{
    if (!mSettings) return;
    if (e.sequence == e.defaultSequence)
        mSettings->remove(settingsKey(e.id));
    else
        mSettings->setValue(settingsKey(e.id), e.sequence.toString(QKeySequence::PortableText));
}
