/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "viewport/cameraspeed.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTimer>
#include <algorithm>

namespace {
const char *kKey = "camera/speed";

// THE TWO KEYS THIS ONE REPLACED (lane FLYSPEED-1, the CRUD law): a per-surface
// float multiplier each, on a ladder that no longer exists. They are REMOVED on
// bind rather than read — nothing is owed to old settings, and a stale row that
// nothing writes is a row somebody will one day try to explain.
const char *kRetiredEditorKey = "camera/flySpeedEditor";
const char *kRetiredPlayerKey = "camera/flySpeedPlayer";

int sValue = CameraSpeed::kDefault;
QSettings *sStore = nullptr;
std::function<void()> sOnChanged;

// THE DEFERRED STORE WRITE (fix round item 2, measured on Qt 6.10.2: a
// QSettings::setValue makes the NEXT pass of the event loop rewrite
// jahsettings.ini through a QSaveFile — two fdatasyncs and a rename, on the UI
// THREAD). One notch of the wheel mid-fly, or one mouse-move of a slider drag,
// must not be a durable write: the house law since FSYNC-2 is that nothing
// that draws also waits for a disk.
//
// So a set moves `sValue` at once — every reader, every fly step and the
// toolbar see it immediately — and only ARMS the write. The arm is not
// restarted by later sets (a continuous drag would postpone it forever): the
// first set of a burst starts the clock, the write at the end of it stores
// whatever the dial ended on, once.
bool sPending = false;
bool sArmed = false;
int  sWrites = 0;
constexpr int kFlushDelayMs = 500;

void writeNow()
{
    if (!sStore || !sPending) return;
    sPending = false;
    ++sWrites;
    if (sValue == CameraSpeed::kDefault) sStore->remove(QLatin1String(kKey));
    else                                 sStore->setValue(QLatin1String(kKey), sValue);
}

void armWrite()
{
    if (sArmed) return;
    // NO EVENT LOOP, NO TIMER: a unit test (and --dump-api-docs) may never
    // reach one, and a value that is never written is worse than a write on a
    // thread that is not drawing anything.
    QCoreApplication *app = QCoreApplication::instance();
    if (!app) { writeNow(); return; }
    sArmed = true;
    QTimer::singleShot(kFlushDelayMs, app, [] { sArmed = false; writeNow(); });
}

void announce()
{
    if (sOnChanged) sOnChanged();
}
}

int CameraSpeed::clamp(int n)
{
    return std::min(std::max(n, kMin), kMax);
}

int CameraSpeed::value()
{
    return sValue;
}

void CameraSpeed::setValue(int n)
{
    const int was = sValue;
    sValue = clamp(n);
    if (sValue == was) return;   // a set that changes nothing is not a gesture
    if (sStore) { sPending = true; armWrite(); }
    announce();
}

int CameraSpeed::step(int delta)
{
    setValue(sValue + delta);
    return sValue;
}

void CameraSpeed::flush()
{
    writeNow();
}

int CameraSpeed::storeWrites()
{
    return sWrites;
}

void CameraSpeed::setOnChanged(std::function<void()> handler)
{
    sOnChanged = std::move(handler);
}

float CameraSpeed::factor()
{
    return float(sValue) / float(kDefault);
}

float CameraSpeed::applyTo(float base)
{
    return base * factor();
}

float CameraSpeed::editorSpeed()
{
    return applyTo(kEditorBase);
}

float CameraSpeed::playerSpeed()
{
    return applyTo(kPlayerBase);
}

void CameraSpeed::bindSettings(QSettings *settings)
{
    writeNow();                  // what the OLD store was owed goes to it
    sStore = settings;
    sPending = false;
    if (!sStore) return;
    sStore->remove(QLatin1String(kRetiredEditorKey));
    sStore->remove(QLatin1String(kRetiredPlayerKey));
    // A key that is absent, or holds something that is not a whole number,
    // leaves the default standing (the reader-defaults law): an unreadable
    // preference is a preference nobody set.
    bool ok = false;
    const int stored = sStore->value(QLatin1String(kKey), kDefault).toInt(&ok);
    sValue = ok ? clamp(stored) : kDefault;
}

void CameraSpeed::reset()
{
    if (sStore) sStore->remove(QLatin1String(kKey));
    sPending = false;
    const bool moved = sValue != kDefault;
    sValue = kDefault;
    if (moved) announce();
}
