/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "viewport/snapsettings.h"

#include <QSettings>
#include <algorithm>

namespace {
constexpr float kTranslateDefault = 1.0f;
constexpr float kRotateDefault = 10.0f;
constexpr float kScaleDefault = 0.25f;

float sTranslate = kTranslateDefault;
float sRotate = kRotateDefault;
float sScale = kScaleDefault;
QSettings *sStore = nullptr;

float clampTranslate(float v) { return std::min(std::max(v, 0.01f), 100.0f); }
float clampRotate(float v)    { return std::min(std::max(v, 0.1f), 180.0f); }
float clampScale(float v)     { return std::min(std::max(v, 0.01f), 10.0f); }

void persist(const char *key, float value, float def)
{
    if (!sStore) return;
    if (value == def) sStore->remove(key);
    else sStore->setValue(key, double(value));
}
}

float SnapSettings::translateSize() { return sTranslate; }
float SnapSettings::rotateSize()    { return sRotate; }
float SnapSettings::scaleSize()     { return sScale; }

void SnapSettings::setTranslateSize(float size)
{
    sTranslate = clampTranslate(size);
    persist("snap/translate", sTranslate, kTranslateDefault);
}

void SnapSettings::setRotateSize(float size)
{
    sRotate = clampRotate(size);
    persist("snap/rotate", sRotate, kRotateDefault);
}

void SnapSettings::setScaleSize(float size)
{
    sScale = clampScale(size);
    persist("snap/scale", sScale, kScaleDefault);
}

const QVector<float> &SnapSettings::translateSteps()
{
    static const QVector<float> steps { 0.1f, 0.25f, 0.5f, 1.0f, 5.0f, 10.0f };
    return steps;
}

const QVector<float> &SnapSettings::rotateSteps()
{
    static const QVector<float> steps { 1.0f, 5.0f, 10.0f, 15.0f, 30.0f, 45.0f, 90.0f };
    return steps;
}

const QVector<float> &SnapSettings::scaleSteps()
{
    static const QVector<float> steps { 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    return steps;
}

float SnapSettings::stepped(const QVector<float> &steps, float current, int direction)
{
    if (steps.isEmpty()) return current;
    if (direction > 0) {
        for (float s : steps)
            if (s > current + 1e-6f) return s;
        return steps.last();
    }
    for (int i = steps.size() - 1; i >= 0; --i)
        if (steps[i] < current - 1e-6f) return steps[i];
    return steps.first();
}

void SnapSettings::bindSettings(QSettings *settings)
{
    sStore = settings;
    if (!sStore) return;
    sTranslate = clampTranslate(float(sStore->value("snap/translate", double(kTranslateDefault)).toDouble()));
    sRotate    = clampRotate(float(sStore->value("snap/rotate", double(kRotateDefault)).toDouble()));
    sScale     = clampScale(float(sStore->value("snap/scale", double(kScaleDefault)).toDouble()));
}

void SnapSettings::reset()
{
    if (sStore) {
        sStore->remove("snap/translate");
        sStore->remove("snap/rotate");
        sStore->remove("snap/scale");
    }
    sTranslate = kTranslateDefault;
    sRotate = kRotateDefault;
    sScale = kScaleDefault;
}
