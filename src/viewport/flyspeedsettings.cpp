/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "viewport/flyspeedsettings.h"

#include <QSettings>
#include <algorithm>

namespace {
constexpr float kDefaultMultiplier = 1.0f;
constexpr float kMinMultiplier = 0.05f;
constexpr float kMaxMultiplier = 32.0f;

// The bases. Editor: EditorCameraController's linearSpeed since
// EDITOR_SHORTCUTS_SPEC §2. Player: PlayerMouseController::movementSpeed.
constexpr float kEditorBase = 8.0f;
constexpr float kPlayerBase = 25.0f;

float sMultiplier[2] = { kDefaultMultiplier, kDefaultMultiplier };
QSettings *sStore = nullptr;

const char *keyFor(FlySpeedSettings::Surface s)
{
    return s == FlySpeedSettings::Player ? "camera/flySpeedPlayer" : "camera/flySpeedEditor";
}

float clampMultiplier(float v)
{
    if (!(v > 0.0f)) return kDefaultMultiplier;   // NaN and <= 0 are not speeds
    return std::min(std::max(v, kMinMultiplier), kMaxMultiplier);
}

int index(FlySpeedSettings::Surface s) { return s == FlySpeedSettings::Player ? 1 : 0; }
}

const QVector<float> &FlySpeedSettings::steps()
{
    static const QVector<float> list { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
    return list;
}

float FlySpeedSettings::baseSpeed(Surface surface)
{
    return surface == Player ? kPlayerBase : kEditorBase;
}

float FlySpeedSettings::multiplier(Surface surface)
{
    return sMultiplier[index(surface)];
}

void FlySpeedSettings::setMultiplier(Surface surface, float multiplier)
{
    const float v = clampMultiplier(multiplier);
    sMultiplier[index(surface)] = v;
    if (!sStore) return;
    if (v == kDefaultMultiplier) sStore->remove(keyFor(surface));
    else                         sStore->setValue(keyFor(surface), double(v));
}

float FlySpeedSettings::speed(Surface surface)
{
    return baseSpeed(surface) * multiplier(surface);
}

float FlySpeedSettings::step(Surface surface, int direction)
{
    const QVector<float> &list = steps();
    const float current = multiplier(surface);
    float next = current;
    if (direction > 0) {
        next = list.last();
        for (float s : list)
            if (s > current + 1e-4f) { next = s; break; }
    } else if (direction < 0) {
        next = list.first();
        for (int i = list.size() - 1; i >= 0; --i)
            if (list[i] < current - 1e-4f) { next = list[i]; break; }
    }
    setMultiplier(surface, next);
    return multiplier(surface);
}

void FlySpeedSettings::bindSettings(QSettings *settings)
{
    sStore = settings;
    if (!sStore) return;
    sMultiplier[0] = clampMultiplier(
        float(sStore->value(keyFor(Editor), double(kDefaultMultiplier)).toDouble()));
    sMultiplier[1] = clampMultiplier(
        float(sStore->value(keyFor(Player), double(kDefaultMultiplier)).toDouble()));
}

void FlySpeedSettings::reset()
{
    if (sStore) {
        sStore->remove(keyFor(Editor));
        sStore->remove(keyFor(Player));
    }
    sMultiplier[0] = sMultiplier[1] = kDefaultMultiplier;
}
