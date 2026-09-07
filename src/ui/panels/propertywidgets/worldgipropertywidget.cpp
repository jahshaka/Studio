/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/vec.h"
#include "ui/panels/propertywidgets/worldgipropertywidget.h"

#include "irisgl/document/scenegraph/scene.h"
#include "services/worldmodes.h"
#include "irisgl/document/scenegraph/lightnode.h"

#include "ui/controls/checkboxwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/labelwidget.h"
#include "services/gibounds.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include <QPushButton>

namespace {
// Combo rows in display order -> document modes (rows are NOT the enum values).
const iris::GiMode kGiRows[] = {
    iris::GiMode::OFF, iris::GiMode::INSTANT_RADIOSITY,
    iris::GiMode::VCT, iris::GiMode::VCT_PCC_HYBRID,
};
const int kGiRowCount = int(sizeof(kGiRows) / sizeof(kGiRows[0]));
int giRowFor(iris::GiMode m)
{
    for (int i = 0; i < kGiRowCount; ++i)
        if (kGiRows[i] == m) return i;
    return 0;
}
}

WorldGiPropertyWidget::WorldGiPropertyWidget()
{
}

void WorldGiPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
        rebuild();
    } else {
        this->scene.clear();
    }
}

// Like the sky panel: one dropdown, then only the rows the chosen mode uses.
void WorldGiPropertyWidget::rebuild()
{
    clearPanel(this->layout());

    modeSelector = this->addComboBox("Mode");
    modeSelector->addItem("Off");
    modeSelector->addItem("Bounced Light");
    modeSelector->addItem("Voxel Lighting");
    modeSelector->addItem("Voxel + Reflections");
    modeSelector->setCurrentIndex(giRowFor(scene->giMode));
    connect(modeSelector, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
            this, &WorldGiPropertyWidget::modeChanged);

    switch (scene->giMode) {
    case iris::GiMode::OFF:
        break;

    case iris::GiMode::INSTANT_RADIOSITY: {
        // Bounced Light: sunlight (or any light) bounces once off surfaces and
        // spills its colour into the shadows. Live in the engine viewport.
        quality = this->addComboBox("Quality");
        quality->addItem("Low");
        quality->addItem("Medium");
        quality->addItem("High");
        quality->setCurrentIndex(qBound(0, static_cast<int>(scene->giQuality), 2));
        connect(quality, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
                this, &WorldGiPropertyWidget::onQualityChanged);

        lightSelector = this->addComboBox("Bounce From");
        lightSelector->addItem("Automatic", QString());
        int row = 0, current = 0;
        for (const auto &light : scene->lights) {
            if (light.isNull()) continue;
            lightSelector->addItem(light->getName(), light->getGUID());
            ++row;
            if (!scene->giLightGuid.isEmpty() && light->getGUID() == scene->giLightGuid)
                current = row;
        }
        lightSelector->setCurrentIndex(current);
        connect(lightSelector, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
                this, &WorldGiPropertyWidget::onLightChanged);

        bounces = this->addFloatValueSlider("Light Bounces", 1.0f, 4.0f,
                                            float(scene->giNumBounces));
        connect(bounces, SIGNAL(valueChanged(float)), SLOT(onBouncesChanged(float)));
        break;
    }

    case iris::GiMode::VCT:
    case iris::GiMode::VCT_PCC_HYBRID: {
        // Voxel Lighting: the scene is voxelized over the bounds and every
        // surface cone-traces bounced light and reflections out of the volume.
        // Voxel + Reflections adds a grid of reflection probes blended with the
        // cone-traced reflections by distance. Live in the engine viewport.
        quality = this->addComboBox("Quality");
        quality->addItem("Low");        // 32^3 voxels, 128px probes
        quality->addItem("Medium");     // 64^3, 256px
        quality->addItem("High");       // 128^3, 512px
        quality->setCurrentIndex(qBound(0, static_cast<int>(scene->giQuality), 2));
        connect(quality, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
                this, &WorldGiPropertyWidget::onQualityChanged);

        bounces = this->addFloatValueSlider("Light Bounces", 1.0f, 4.0f,
                                            float(scene->giNumBounces));
        connect(bounces, SIGNAL(valueChanged(float)), SLOT(onBouncesChanged(float)));

        this->addLabel("Bounds", "Corners of the lit area; zeros = fit the scene");
        boundsMin = this->addVector3Widget("", scene->giBoundsMin.x(),
                                           scene->giBoundsMin.y(), scene->giBoundsMin.z());
        boundsMax = this->addVector3Widget("", scene->giBoundsMax.x(),
                                           scene->giBoundsMax.y(), scene->giBoundsMax.z());
        connect(boundsMin, &Widget3D::valueChanged, this, &WorldGiPropertyWidget::onBoundsMinChanged);
        connect(boundsMax, &Widget3D::valueChanged, this, &WorldGiPropertyWidget::onBoundsMaxChanged);

        if (scene->giMode == iris::GiMode::VCT_PCC_HYBRID) {
            this->addLabel("Reflection Probes", "Probe counts along each axis of the bounds");
            pccGrid = this->addVector3Widget("", scene->giPccGrid.x(),
                                             scene->giPccGrid.y(), scene->giPccGrid.z());
            connect(pccGrid, &Widget3D::valueChanged, this, &WorldGiPropertyWidget::onPccGridChanged);
        }

        autoRefresh = this->addCheckBox("Auto Refresh", scene->giAutoRefresh);
        connect(autoRefresh, SIGNAL(valueChanged(bool)), SLOT(onAutoRefreshChanged(bool)));

        // REFLECTIONS_ADOPTION_SPEC.md P1d. Auto Refresh only watches LIGHTS —
        // geometry that moves would re-voxelize every frame of a drag, so it
        // deliberately does not trigger one. That left "I moved something and
        // the bounced light is stale" with no recourse but toggling the mode
        // off and on. This is the recourse. Same verb the script surface has
        // (world.refreshGi).
        refreshButton = new QPushButton(tr("Refresh"));
        refreshButton->setToolTip(tr("Re-solve global illumination against the scene as it is "
                                     "now. Moving objects does not do this automatically — "
                                     "re-voxelizing every frame of a drag would be unusable."));
        connect(refreshButton, &QPushButton::clicked, this, &WorldGiPropertyWidget::onRefreshClicked);
        this->addWidgetToContent(refreshButton);

        // P1a.3, adapted: the spec asked for "fit to SELECTION", but this panel
        // only ever appears while the WORLD is the selection (the properties
        // panel swaps the whole stack, scenenodepropertieswidget.cpp), so a
        // selection-driven button here would be permanently empty. What is
        // useful at this moment is pinning the volume to the scene's contents
        // and then nudging the rows. The selection-driven form lives where it
        // can work: the world.fitGiBounds({nodes}) verb.
        fitBoundsButton = new QPushButton(tr("Fit Bounds To Scene"));
        fitBoundsButton->setToolTip(tr("Pin the bounds above to everything in the scene. Leaving "
                                       "them at zero lets the renderer fit them automatically, "
                                       "which also ignores outsized objects like a ground plane; "
                                       "pin them when you want a volume of your own."));
        connect(fitBoundsButton, &QPushButton::clicked, this, &WorldGiPropertyWidget::onFitBoundsClicked);
        this->addWidgetToContent(fitBoundsButton);
        break;
    }
    }
}

void WorldGiPropertyWidget::modeChanged(int row)
{
    if (!scene || row < 0 || row >= kGiRowCount) return;
    scene->giMode = kGiRows[row];
    // A direct edit of a backing field is a World Mode PIN (POST_CHAIN_SPEC §9.1).
    worldmodes::pinRowValue(scene, QStringLiteral("giMode"), int(scene->giMode));
    rebuild();
}

void WorldGiPropertyWidget::onQualityChanged(int row)
{
    if (!scene) return;
    scene->giQuality = static_cast<iris::GiQuality>(qBound(0, row, 2));
    worldmodes::pinRowValue(scene, QStringLiteral("giQuality"), int(scene->giQuality));
}

void WorldGiPropertyWidget::onLightChanged(int row)
{
    if (!scene || !lightSelector) return;
    scene->giLightGuid = lightSelector->getItemData(row).toString();
}

void WorldGiPropertyWidget::onBouncesChanged(float value)
{
    if (!!scene) scene->giNumBounces = qBound(1, qRound(value), 4);
}

void WorldGiPropertyWidget::onBoundsMinChanged(iris::Vec3 value)
{
    if (!!scene) scene->giBoundsMin = value;
}

void WorldGiPropertyWidget::onBoundsMaxChanged(iris::Vec3 value)
{
    if (!!scene) scene->giBoundsMax = value;
}

void WorldGiPropertyWidget::onPccGridChanged(iris::Vec3 value)
{
    if (!!scene)
        scene->giPccGrid = iris::Vec3(qBound(1, qRound(value.x()), 8),
                                     qBound(1, qRound(value.y()), 8),
                                     qBound(1, qRound(value.z()), 8));
}

void WorldGiPropertyWidget::onAutoRefreshChanged(bool value)
{
    if (!!scene) scene->giAutoRefresh = value;
}

void WorldGiPropertyWidget::onRefreshClicked()
{
    // The document-side serial IS the verb (world.refreshGi): the mirror is what
    // owns "push this to the engine", and a panel that called the renderer
    // directly would be a second route to keep in step forever.
    if (!!scene) ++scene->giRefreshSerial;
}

void WorldGiPropertyWidget::onFitBoundsClicked()
{
    if (!scene || scene->getRootNode().isNull()) return;
    iris::Vec3 mn, mx;
    if (!gibounds::fit(scene->getRootNode()->children(), 0.0f, mn, mx)) return;
    scene->giBoundsMin = mn;
    scene->giBoundsMax = mx;
    // The rows are spin boxes holding the OLD numbers; rebuild so the panel
    // shows what it just wrote.
    rebuild();
}
