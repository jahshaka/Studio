/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/outlinesettings.h"

#include "data/settingsmanager.h"
#include "irisgl/document/scenegraph/scene.h"

namespace outlinesettings {

const char *widthKey()        { return "outline_width"; }
const char *colorKey()        { return "outline_color"; }
const char *primaryColorKey() { return "outline_primary_color"; }

int minWidth()     { return 1; }
int maxWidth()     { return 30; }
int defaultWidth() { return 3; }

QColor defaultColor() { return QColor(QStringLiteral("#3498db")); }

QColor lightenForPrimary(const QColor &base)
{
    if (!base.isValid()) return lightenForPrimary(defaultColor());
    // HALFWAY TO WHITE, per channel. QColor::lighter() multiplies the HSV
    // VALUE, which does nothing at all once the value is already 255 — and the
    // shipped #3498db is at value 219, so lighter(150) clips to the same
    // saturated blue and the primary would have been indistinguishable.
    return QColor::fromRgbF(base.redF()   + (1.0 - base.redF())   * 0.5,
                            base.greenF() + (1.0 - base.greenF()) * 0.5,
                            base.blueF()  + (1.0 - base.blueF())  * 0.5);
}

QColor defaultPrimaryColor() { return lightenForPrimary(defaultColor()); }

int width()
{
    auto *settings = SettingsManager::getDefaultManager();
    if (!settings) return defaultWidth();
    // Default halved 6 -> 3 (2026-08-30). A stored 6 is indistinguishable from
    // the old default, so treat it AS the new default; any other stored value
    // is a deliberate user choice and is kept. (Was inline in the Preferences
    // page, where the verbs could not see it.)
    int stored = settings->getValue(widthKey(), defaultWidth()).toInt();
    if (stored == 6) stored = defaultWidth();
    return qBound(minWidth(), stored, maxWidth());
}

QColor color()
{
    auto *settings = SettingsManager::getDefaultManager();
    if (!settings) return defaultColor();
    const QColor stored(settings->getValue(colorKey(), defaultColor().name()).toString());
    return stored.isValid() ? stored : defaultColor();
}

QColor primaryColor()
{
    auto *settings = SettingsManager::getDefaultManager();
    if (!settings) return lightenForPrimary(defaultColor());
    const QString stored = settings->getValue(primaryColorKey(), QString()).toString();
    const QColor parsed(stored);
    // NO STORED VALUE = DERIVED, not "the shipped default": a user who changed
    // the outline colour and never touched this row should get a primary that
    // is lighter than THEIR colour, not lighter than ours.
    return parsed.isValid() ? parsed : lightenForPrimary(color());
}

void setWidth(int w)
{
    if (auto *settings = SettingsManager::getDefaultManager())
        settings->setValue(widthKey(), qBound(minWidth(), w, maxWidth()));
}

void setColor(const QColor &c)
{
    auto *settings = SettingsManager::getDefaultManager();
    if (!settings) return;
    if (c.isValid()) settings->setValue(colorKey(), c.name());
    else             settings->setValue(colorKey(), defaultColor().name());
}

void setPrimaryColor(const QColor &c)
{
    auto *settings = SettingsManager::getDefaultManager();
    if (!settings) return;
    // An invalid colour clears the key rather than writing a name: cleared
    // means "derive from the outline colour", which is a state the row can
    // return to.
    settings->setValue(primaryColorKey(), c.isValid() ? c.name() : QString());
}

void apply(iris::Scene *scene)
{
    if (!scene) return;
    scene->setOutlineWidth(width());
    scene->setOutlineColor(color());
    scene->setOutlinePrimaryColor(primaryColor());
}

} // namespace outlinesettings
