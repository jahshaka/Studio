/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef WORLDPROPERTYWIDGET_H
#define WORLDPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "irisgl/irisglfwd.h"
#include "ui/controls/accordionbladewidget.h"
#include "ui/panels/propertywidgets/panelundo.h"

namespace iris {
    class Scene;
    class SceneNode;
    class LightNode;
}

class Database;

/**
 * This widget displays the properties of the scene.
 */
class WorldPropertyWidget: public AccordianBladeWidget
{
    Q_OBJECT

public:
    WorldPropertyWidget();

    void setScene(QSharedPointer<iris::Scene> scene);
	void setDatabase(Database*);
    /// The undo stack, for the rows (debt L6). Nullable — the rows still write
    /// the document without it.
    void setServices(StudioServices *s) { services = s; }

    // Two-way binding to the View Options "Ground Grid" action — the action
    // (and the per-scene EditorData flag behind it) stays the single source
    // of truth; this row is just another face of it.
    void setGridAction(QAction *action);

public slots:
    void onBackgroundAmbienceChanged(int index);

private:
    /// Re-reads every row from the document in place (selection, and undo).
    void refreshRows();
    /// Binds the scene's ambient music to `guid` and starts (or stops) it. The
    /// one place that resolves the clip — both the row and its undo step call
    /// it, so they cannot diverge.
    void applyAmbientMusic(const QString &guid);

    QSharedPointer<iris::Scene> scene;
    StudioServices *services = nullptr;
    /// Populating the rows, not showing an edit (see rowundo::Binding::guard).
    bool loading = false;
    panelundo::SceneRows rows;
    CheckBoxWidget *flipView;
    CheckBoxWidget *showGridToggle = nullptr;
    QAction *gridAction = nullptr;
    ColorValueWidget *ambientColor;
    HFloatSliderWidget *worldGravity;
    /// The AVATAR_LOCOMOTION_SPEC §8.5 world setting, as a row.
    ComboBoxWidget *playModeSelector = nullptr;
	ComboBoxWidget *ambientMusicSelector;
	HFloatSliderWidget *ambientMusicVolume;

	Database *db;
};

#endif // WORLDPROPERTYWIDGET_H
