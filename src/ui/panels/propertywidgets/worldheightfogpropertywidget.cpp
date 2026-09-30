/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/worldheightfogpropertywidget.h"

#include "commands/scenepropertycommand.h"
#include "irisgl/document/scenegraph/scene.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/labelwidget.h"
#include "ui/panels/propertyrows.h"
#include "ui/panels/propertywidgets/rowundo.h"

WorldHeightFogPropertyWidget::WorldHeightFogPropertyWidget()
    : rows([this]() { return scene; }, [this]() { return services; },
           [this]() { refreshRows(); }, [this]() { return !loading; })
{
    enabled = this->addCheckBox("Height Fog", false);
    enabled->setToolTip(QStringLiteral(
        "Unreal's Exponential Height Fog: a layer of the world's air, densest low down, that the far "
        "world, the horizon and the sky under it are seen through. Nothing nearer than the Start "
        "Distance is fogged, so the scene you build stays itself. Its colour is the sky's own light. "
        "Separate from the Fog section's scene fog."));
    PropertyRows::describe(enabled, { QStringLiteral("height fog"), QStringLiteral("fog"),
                                      QStringLiteral("horizon"), QStringLiteral("haze"),
                                      QStringLiteral("sky") });

    density = this->addFloatValueSlider("Density", 0.f, 0.5f, 0.02f);
    density->setDecimals(3);
    density->setToolTip(QStringLiteral(
        "Unreal's Fog Density (default 0.02): how thick the layer is at its Base Height. Unreal's "
        "units — a tenth of this is lost per metre."));
    falloff = this->addFloatValueSlider("Height Falloff", 0.001f, 2.f, 0.2f);
    falloff->setDecimals(3);
    falloff->setToolTip(QStringLiteral(
        "Unreal's Fog Height Falloff (default 0.2): how fast the layer thins with height. Smaller "
        "values make a deeper, softer layer; it halves every 10 / falloff metres."));
    baseHeight = this->addFloatValueSlider("Base Height", -500.f, 500.f, 0.f);
    baseHeight->setDecimals(1);
    baseHeight->setToolTip(QStringLiteral(
        "The world height, in metres, where the Density applies — Unreal's fog actor height."));
    startDistance = this->addFloatValueSlider("Start Distance", 0.f, 2000.f, 100.f);
    startDistance->setDecimals(0);
    startDistance->setToolTip(QStringLiteral(
        "Metres from the eye before anything is fogged. The default clears the Basic floor; Unreal "
        "ships 0."));
    note = this->addLabel("Note", QString());

    rowundo::bind(enabled, rows(QStringLiteral("heightFog"), tr("Height Fog"), [this](const QVariant &v) {
        return withField([&v](iris::HeightFog &h) { h.enabled = v.toBool(); });
    }));
    const auto bindDial = [this](HFloatSliderWidget *row, const QString &text,
                                 float iris::HeightFog::*field) {
        rowundo::bind(row, rows(QStringLiteral("heightFog"), text, [this, field](const QVariant &v) {
            return withField([&v, field](iris::HeightFog &h) { h.*field = v.toFloat(); });
        }));
    };
    bindDial(density, tr("Height Fog Density"), &iris::HeightFog::density);
    bindDial(falloff, tr("Height Fog Falloff"), &iris::HeightFog::heightFalloff);
    bindDial(baseHeight, tr("Height Fog Base Height"), &iris::HeightFog::baseHeight);
    bindDial(startDistance, tr("Height Fog Start Distance"), &iris::HeightFog::startDistance);
    // EACH ROW ITS OWN KEY (the clouds blade's rule): bind() names all five by
    // the one document key they write; told apart after it.
    PropertyRows::identify(enabled, QStringLiteral("heightFog.enabled"));
    PropertyRows::identify(density, QStringLiteral("heightFog.density"));
    PropertyRows::identify(falloff, QStringLiteral("heightFog.heightFalloff"));
    PropertyRows::identify(baseHeight, QStringLiteral("heightFog.baseHeight"));
    PropertyRows::identify(startDistance, QStringLiteral("heightFog.startDistance"));
}

QVariant WorldHeightFogPropertyWidget::withField(const std::function<void(iris::HeightFog &)> &edit) const
{
    if (!scene) return QVariant();
    iris::HeightFog h = scene->heightFog;
    edit(h);
    return iris::HeightFog::clamped(h).toJson().toVariantMap();
}

void WorldHeightFogPropertyWidget::setScene(QSharedPointer<iris::Scene> s)
{
    scene = s;
    refreshRows();
}

void WorldHeightFogPropertyWidget::refreshRows()
{
    if (!scene) return;
    loading = true;
    const iris::HeightFog &h = scene->heightFog;
    enabled->setValue(h.enabled);
    density->setValue(h.density);
    falloff->setValue(h.heightFalloff);
    baseHeight->setValue(h.baseHeight);
    startDistance->setValue(h.startDistance);
    for (QWidget *w : { static_cast<QWidget *>(density), static_cast<QWidget *>(falloff),
                        static_cast<QWidget *>(baseHeight), static_cast<QWidget *>(startDistance) })
        w->setEnabled(h.enabled);
    note->setText(QStringLiteral("Coloured by the sky. Unreal's units."));
    loading = false;
}
