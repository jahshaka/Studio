/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef FOGPROPERTYWIDGET_H
#define FOGPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include <QVector>
#include "ui/controls/accordionbladewidget.h"
#include "ui/panels/propertywidgets/panelundo.h"

class ColorValueWidget;
class ColorPickerWidget;
class TexturePicker;

namespace iris {
    class Scene;
    class SceneNode;
    class LightNode;
}

/**
 * The World panel's Fog blade. Fog is EXPONENTIAL (see iris::Scene and
 * jahshaka::engine::FogDesc): a density per world unit, optionally a second
 * height-varying layer, plus the brightness "breakthrough" that keeps bright
 * pixels from dissolving. The continuous rows are GENERATED from the fog table
 * (worldmodes::fogParams) that world.fog reads and writes through too; the
 * colour row is greyed under the realistic sky, whose own scattering colours
 * the fog (FOG-ATMO-1).
 */
class FogPropertyWidget: public AccordianBladeWidget
{
    Q_OBJECT

public:
    FogPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);
    /// The undo stack, for the rows (debt L6). Nullable: without it every row
    /// still writes the document, it just records no step — which is what
    /// headless hosts and the panel suites do.
    void setServices(StudioServices *s) { services = s; }

public slots:
    /// Re-reads every row from the document IN PLACE (no rebuild, no rewiring):
    /// what selecting a scene does, what an undo of one of these rows does, and
    /// what a sky change does (the colour row's enabled state follows the sky).
    void refreshRows();

private:
    /// Greys the rows that are dead weight right now (see the definition).
    void applyEnabledStates();

    QSharedPointer<iris::Scene> scene;
    StudioServices *services = nullptr;
    /// Guards the population above: HFloatSliderWidget::setValue EMITS
    /// valueChanged, so filling the rows would otherwise write every value
    /// back into the document (and, now, push undo steps for edits nobody made).
    bool loading = false;
    panelundo::SceneRows rows;

    CheckBoxWidget* fogEnabled = nullptr;
    CheckBoxWidget* shadowEnabled = nullptr;
    ColorValueWidget* fogColor = nullptr;
    /// One slider per fog table row, in the table's order.
    struct ParamSlider {
        QString id;                          ///< the world.fog key
        HFloatSliderWidget *slider = nullptr;
    };
    QVector<ParamSlider> paramSliders;
};

#endif // FOGPROPERTYWIDGET_H
