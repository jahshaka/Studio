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
#include "services/worldmodes.h"

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
    fogColor->setToolTip(QStringLiteral(
        "The fog's colour under a Single Color, Gradient or image sky. Under the Realistic sky "
        "the fog takes the sky's own colour for the direction each surface is seen from — the "
        "distance fades into the sky behind it and follows the sun — so this row is greyed."));

    // GENERATED FROM THE TABLE (worldmodes::fogParams, FOG-ATMO-1) — the same
    // rows, labels, ranges and texts world.fog reads and writes through. (The
    // "Fog Start (unused)" row and the "Sky Colour (Aerial)" switch are gone:
    // the first described nothing exponential fog draws, the second a choice
    // the sky now makes.)
    for (const worldmodes::ParamRow &p : worldmodes::fogParams()) {
        ParamSlider ps;
        ps.id = p.id;
        ps.slider = this->addFloatValueSlider(p.label, float(p.minValue), float(p.maxValue));
        ps.slider->setDecimals(p.decimals);
        ps.slider->setToolTip(p.doc);
        rowundo::bind(ps.slider, rows(worldmodes::fogParamSceneKey(p), p.label));
        paramSliders.append(ps);
    }

    shadowEnabled   = this->addCheckBox("Enable Shadows", true);

    // The rows, bound to their world properties. A row's LIVE write and its
    // undo step come from the same key, so the two can never disagree.
    rowundo::bind(fogColor->getPicker(), rows(QStringLiteral("fogColor"), tr("Fog Colour")));
    {
        // The switch greys the rows it owns the moment it flips — a live write
        // with no undo stack refreshes nothing else.
        rowundo::Binding enabled = rows(QStringLiteral("fogEnabled"), tr("Fog Enabled"));
        auto write = enabled.write;
        enabled.write = [this, write](const QVariant &v) {
            write(v);
            applyEnabledStates();
        };
        rowundo::bind(fogEnabled, enabled);
    }
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
    // than destroying and re-wiring the controls (debt L6's clearLayout half).
    loading = true;
    fogColor->setColorValue(scene->fogColor);
    for (const ParamSlider &ps : paramSliders) {
        const worldmodes::ParamRow *p = worldmodes::fogParam(ps.id);
        if (p && ps.slider) ps.slider->setValue(float(p->get(scene)));
    }
    fogEnabled->setValue(scene->fogEnabled);
    shadowEnabled->setValue(scene->shadowEnabled);
    applyEnabledStates();
    loading = false;
}

void FogPropertyWidget::applyEnabledStates()
{
    if (!scene) return;
    // Dead rows are greyed, not hidden ("why is it grey" beats "where did it
    // go"): every fog row while the fog is off, and the authored colour under
    // the realistic sky, whose own scattering colours the fog.
    fogColor->setEnabled(scene->fogEnabled && worldmodes::fogColourAuthored(scene));
    for (const ParamSlider &ps : paramSliders) {
        const worldmodes::ParamRow *p = worldmodes::fogParam(ps.id);
        if (p && ps.slider) ps.slider->setEnabled(p->enabled ? p->enabled(scene) : true);
    }
}
