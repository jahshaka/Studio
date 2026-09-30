/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/


#ifndef WORLDHEIGHTFOGPROPERTYWIDGET_H
#define WORLDHEIGHTFOGPROPERTYWIDGET_H

#include <QSharedPointer>
#include <functional>
#include "ui/controls/accordionbladewidget.h"
#include "ui/panels/propertywidgets/panelundo.h"

namespace iris { class Scene; struct HeightFog; }
class LabelWidget;

/**
 * THE WORLD PANEL'S HEIGHT FOG BLADE (SKY-DEFAULTS-1), mounted under Clouds.
 * Every row writes the ONE document field `iris::Scene::heightFog` through the
 * sceneprops key "heightFog" — the key world.heightFog writes, with the same
 * clamp (iris::HeightFog::clamped) — so the panel and the verb are one model,
 * and every gesture is one undo step. The dials are Unreal's numbers.
 */
class WorldHeightFogPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    WorldHeightFogPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);
    void setServices(StudioServices *s) { services = s; }

public slots:
    /// Re-reads every row from the document IN PLACE (selection, undo).
    void refreshRows();

private:
    /// The whole block with ONE field changed, as the "heightFog" key stores it.
    QVariant withField(const std::function<void(iris::HeightFog &)> &edit) const;

    QSharedPointer<iris::Scene> scene;
    StudioServices *services = nullptr;
    bool loading = false;
    panelundo::SceneRows rows;

    CheckBoxWidget *enabled = nullptr;
    HFloatSliderWidget *density = nullptr;
    HFloatSliderWidget *falloff = nullptr;
    HFloatSliderWidget *baseHeight = nullptr;
    HFloatSliderWidget *startDistance = nullptr;
    LabelWidget *note = nullptr;
};

#endif // WORLDHEIGHTFOGPROPERTYWIDGET_H
