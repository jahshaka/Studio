/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef WORLDCLOUDSPROPERTYWIDGET_H
#define WORLDCLOUDSPROPERTYWIDGET_H

#include <QSharedPointer>
#include "ui/controls/accordionbladewidget.h"
#include "ui/panels/propertywidgets/panelundo.h"

namespace iris { class Scene; struct CloudLayer; }
#include <functional>
class Database;
class Project;
class LabelWidget;
class TexturePickerWidget;

/**
 * THE WORLD PANEL'S CLOUDS BLADE (CLOUDS-2D-1), mounted under the Sky blade.
 * Every row writes the ONE document field `iris::Scene::clouds` through the
 * sceneprops key "clouds" — the key world.clouds writes, with the same clamp
 * (iris::CloudLayer::clamped) — so the panel and the verb are one model, and
 * every gesture is one undo step. The rows are DISABLED over an image sky
 * (equirect or cubemap), which carries its own clouds and is never drawn over.
 */
class WorldCloudsPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    WorldCloudsPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);
    void setServices(StudioServices *s) { services = s; }
    /// The weather-map picker resolves an asset guid to its pinned bytes.
    void setDatabase(Database *d) { db = d; }
    void setProject(Project *p) { project = p; }

public slots:
    /// Re-reads every row from the document IN PLACE — selection, undo, and a
    /// sky-type change in the Sky blade above (which decides the enabled state).
    void refreshRows();

private:
    /// The whole block with ONE field changed, as the "clouds" key stores it.
    QVariant withField(const std::function<void(iris::CloudLayer &)> &edit) const;
    void onWeatherPicked(const QString &value, const QString &guid);

    QSharedPointer<iris::Scene> scene;
    StudioServices *services = nullptr;
    Database *db = nullptr;
    Project *project = nullptr;
    bool loading = false;
    panelundo::SceneRows rows;

    CheckBoxWidget *enabled = nullptr;
    HFloatSliderWidget *coverage = nullptr;
    HFloatSliderWidget *density = nullptr;
    HFloatSliderWidget *speed = nullptr;
    HFloatSliderWidget *direction = nullptr;
    HFloatSliderWidget *altitude = nullptr;
    HFloatSliderWidget *shadow = nullptr;
    TexturePickerWidget *weather = nullptr;
    LabelWidget *note = nullptr;
};

#endif // WORLDCLOUDSPROPERTYWIDGET_H
