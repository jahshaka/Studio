/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "viewport/cameraspeed.h"

#include <QSettings>
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
    sValue = clamp(n);
    if (!sStore) return;
    if (sValue == kDefault) sStore->remove(QLatin1String(kKey));
    else                    sStore->setValue(QLatin1String(kKey), sValue);
}

int CameraSpeed::step(int delta)
{
    setValue(sValue + delta);
    return sValue;
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
    sStore = settings;
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
    sValue = kDefault;
}
