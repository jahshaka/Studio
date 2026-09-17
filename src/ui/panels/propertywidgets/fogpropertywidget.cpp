/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/fogpropertywidget.h"
#include "ui/controls/texturepickerwidget.h"
#include "irisgl/document/scenegraph/scene.h"

#include "ui/controls/colorvaluewidget.h"
#include "ui/controls/colorpickerwidget.h"
#include "ui/controls/hfloatsliderwidget.h"

#include "ui/controls/checkboxwidget.h"
#include "ui/panels/propertywidgets/rowundo.h"

// Every row here writes ONE world property and is undoable through
// ScenePropertyCommand — a slider drag is one step (rowundo brackets it), a
// checkbox is one step, and populating the rows for a newly selected scene is
// none (the `loading` guard). The keys are the sceneprops table's, which is
// also what world.fog / world.shadows write, so the panel and the verbs stay
// one model (API-first: no new verb was needed for any row on this blade).
FogPropertyWidget::FogPropertyWidget()
    : rows([this]() { return scene; }, [this]() { return services; },
           [this]() { refreshRows(); }, [this]() { return !loading; })
{
    fogEnabled      = this->addCheckBox("Fog Enabled", false);
    fogColor        = this->addColorPicker("Fog Color");

    // Density is small by nature (the default 100..180 linear fog maps to 0.0071),
    // so the row needs real decimals — Qt's spinbox default of 2 would round it
    // to a two-step control.
    fogDensity      = this->addFloatValueSlider("Fog Density", 0.f, 0.5f);
    fogDensity->setDecimals(4);
    fogDensity->setToolTip(QStringLiteral(
        "How much of a surface's colour is lost per world unit. A surface 1/density units away "
        "keeps half its colour, and by 4.32/density it has all but disappeared. 0.01 = half gone "
        "at 100 units."));

    // (THE "Fog Start (unused)" ROW IS GONE — render audit I-6 / A5's dead
    // rows, CRUD law. It was shown DISABLED, with a label and a tooltip that
    // said it did nothing, for a document field nothing rendered; the field is
    // deleted and so is the row. Exponential fog begins at the camera and
    // never stops: Fog Density above is the whole setting.)

    fogHeightDensity = this->addFloatValueSlider("Height Fog Density", 0.f, 0.5f);
    fogHeightDensity->setDecimals(4);
    fogHeightDensity->setToolTip(QStringLiteral(
        "A second layer of fog, the same colour, whose density falls off with height — ground "
        "mist and valley fog. 0 turns it off entirely."));
    fogHeightFalloff = this->addFloatValueSlider("Height Falloff", 0.f, 2.f);
    fogHeightFalloff->setDecimals(3);
    fogHeightFalloff->setToolTip(QStringLiteral(
        "How fast the height layer thins out as you rise. Larger = a shallower, sharper-edged "
        "layer; the density halves every 1/falloff units above the level below."));
    fogHeightLevel   = this->addFloatValueSlider("Height Level", -100.f, 100.f);
    fogHeightLevel->setDecimals(2);
    fogHeightLevel->setToolTip(QStringLiteral(
        "The world height at which the height layer has its full density — the surface of the "
        "mist, usually the ground."));

    fogBreakBrightness = this->addFloatValueSlider("Breakthrough Brightness", 0.f, 4.f);
    fogBreakBrightness->setDecimals(3);
    fogBreakBrightness->setToolTip(QStringLiteral(
        "How bright a pixel has to be before it starts cutting through the fog instead of "
        "dissolving into it — the sun, a lamp, an emissive sign."));
    fogBreakFalloff    = this->addFloatValueSlider("Breakthrough Falloff", 0.f, 2.f);
    fogBreakFalloff->setDecimals(3);
    fogBreakFalloff->setToolTip(QStringLiteral(
        "How sharply bright pixels break through. 0 switches breakthrough off, leaving plain "
        "exponential fog."));

    // AERIAL PERSPECTIVE (SKY-GPU). Off by default and off in every scene
    // authored before it existed: the fog colour above is a colour a person
    // picked, and no scene's look changes unasked.
    fogAtmosphere = this->addCheckBox("Sky Colour (Aerial)", false);
    fogAtmosphere->setToolTip(QStringLiteral(
        "Takes the distance fog's colour from the REALISTIC sky itself — the scattering in the "
        "direction each surface is seen from — so far things fade into the sky behind them and "
        "the haze follows the sun, with nothing to author. Needs the Realistic sky; with any "
        "other sky the Fog Colour above is used. The height layer always uses Fog Colour."));

    shadowEnabled   = this->addCheckBox("Enable Shadows", true);

    // The rows, bound to their world properties. A row's LIVE write and its
    // undo step come from the same key, so the two can never disagree.
    rowundo::bind(fogColor->getPicker(), rows(QStringLiteral("fogColor"), tr("Fog Colour")));
    rowundo::bind(fogDensity,        rows(QStringLiteral("fogDensity"), tr("Fog Density")));
    rowundo::bind(fogHeightDensity,  rows(QStringLiteral("fogHeightDensity"), tr("Height Fog Density")));
    rowundo::bind(fogHeightFalloff,  rows(QStringLiteral("fogHeightFalloff"), tr("Height Falloff")));
    rowundo::bind(fogHeightLevel,    rows(QStringLiteral("fogHeightLevel"), tr("Height Level")));
    rowundo::bind(fogBreakBrightness,rows(QStringLiteral("fogBreakMinBrightness"), tr("Breakthrough Brightness")));
    rowundo::bind(fogBreakFalloff,   rows(QStringLiteral("fogBreakFalloff"), tr("Breakthrough Falloff")));
    rowundo::bind(fogEnabled,        rows(QStringLiteral("fogEnabled"), tr("Fog Enabled")));
    rowundo::bind(fogAtmosphere,     rows(QStringLiteral("fogAtmosphere"), tr("Sky Colour (Aerial)")));
    rowundo::bind(shadowEnabled,     rows(QStringLiteral("shadowEnabled"), tr("Enable Shadows")));
}

void FogPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
        refreshRows();
    } else {
        this->scene.clear();
    }
}

void FogPropertyWidget::refreshRows()
{
    if (!scene) return;
    // IN PLACE, never a rebuild: the rows are the same widgets from the panel's
    // construction to the window's close, so an undo repaints numbers rather
    // than destroying and re-wiring nine controls (debt L6's clearLayout half).
    loading = true;
    fogColor->setColorValue(scene->fogColor);
    fogDensity->setValue(scene->fogDensity);
    fogHeightDensity->setValue(scene->fogHeightDensity);
    fogHeightFalloff->setValue(scene->fogHeightFalloff);
    fogHeightLevel->setValue(scene->fogHeightLevel);
    fogBreakBrightness->setValue(scene->fogBreakMinBrightness);
    fogBreakFalloff->setValue(scene->fogBreakFalloff);
    fogEnabled->setValue(scene->fogEnabled);
    fogAtmosphere->setValue(scene->fogAtmosphere);
    shadowEnabled->setValue(scene->shadowEnabled);
    loading = false;
}
