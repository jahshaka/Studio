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

    // Two-way binding to the View Options "Ground Grid" action — the action
    // (and the per-scene EditorData flag behind it) stays the single source
    // of truth; this row is just another face of it.
    void setGridAction(QAction *action);

public slots:
    void onGravityChanged(float value);
    void onAmbientColorChanged(QColor color);
    /// The AVATAR_LOCOMOTION_SPEC §8.5 world setting. Writes through
    /// Scene::setPlayMode — the SAME call `scene.playMode(mode)` makes, never a
    /// parallel implementation of it (the API-first rule).
    void onPlayModeChanged(int index);
    void onBackgroundAmbienceChanged(int index);
	void onAmbientMusicVolumeChanged(float volume);

private:
    QSharedPointer<iris::Scene> scene;
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
