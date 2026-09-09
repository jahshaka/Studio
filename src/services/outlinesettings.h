/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef OUTLINESETTINGS_H
#define OUTLINESETTINGS_H

// outlinesettings — THE selection outline's three persisted values, in one
// place: width, the colour every selected node is outlined in, and the colour
// the PRIMARY member of a multi-selection gets instead
// (EDITOR_MULTISELECT_SPEC D4 b — Blender's rule that the active object reads
// brighter than the rest of the selection).
//
// It exists because there were already three copies of "what the outline
// looks like" before the primary colour made it four: the Preferences page
// read and wrote the keys, MainWindow::updateSceneSettings pushed the page's
// two member variables onto the document, and the legacy 6 -> 3 default fold
// lived inside the page's constructor where nothing else could see it. The
// verbs (`editor.outline` / `editor.setOutline`) call THESE functions, the
// page calls these functions, and the document is written by exactly one
// `apply()` — SCRIPTING_SPEC §2.3's "the UI calls the capability".
//
// Free functions over a namespace rather than a class, matching framepacing
// (src/services/framepacing.h): there is no state here beyond the persisted
// keys, which SettingsManager already owns.

#include <QColor>

namespace iris { class Scene; }

namespace outlinesettings {

/// jahsettings.ini keys. Public so a test can assert the exact spellings the
/// shipped installs already carry.
const char *widthKey();
const char *colorKey();
const char *primaryColorKey();

/// Preferences units, 1..30. SceneMirror maps it to the inverted-hull scale as
/// 1 + width/150.
int minWidth();
int maxWidth();
int defaultWidth();

/// The shipped colours. `defaultPrimaryColor()` is `defaultColor()` lifted
/// halfway to white — the same derivation SceneMirror applies when no primary
/// colour was ever stored, so the shipped default and the derived fallback
/// cannot drift.
QColor defaultColor();
QColor lightenForPrimary(const QColor &base);
QColor defaultPrimaryColor();

/// Stored values (clamped / validated; the legacy width 6 is read as today's
/// default 3, see width()).
int width();
QColor color();
QColor primaryColor();

/// Persist. Width is clamped; an INVALID colour clears the key, which puts the
/// value back on its default.
void setWidth(int width);
void setColor(const QColor &color);
void setPrimaryColor(const QColor &color);

/// Pushes all three onto a document. The only writer of Scene::outlineWidth /
/// outlineColor / outlinePrimaryColor in the app.
void apply(iris::Scene *scene);

} // namespace outlinesettings

#endif // OUTLINESETTINGS_H
