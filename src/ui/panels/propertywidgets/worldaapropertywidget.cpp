/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/worldaapropertywidget.h"

#include "irisgl/document/scenegraph/scene.h"

#include "ui/controls/comboboxwidget.h"
#include "ui/controls/labelwidget.h"
#include "viewport/ieditorviewport.h"
#include "services/worldmodes.h"

namespace {
// Combo rows in display order -> MSAA sample counts.
const int kAaSamples[] = { 1, 2, 4, 8 };
const int kAaRowCount = int(sizeof(kAaSamples) / sizeof(kAaSamples[0]));
int aaRowFor(int samples)
{
    for (int i = 0; i < kAaRowCount; ++i)
        if (kAaSamples[i] == samples) return i;
    return 0;
}
QString aaName(int samples)
{
    return samples <= 1 ? QStringLiteral("Off") : QStringLiteral("%1x").arg(samples);
}
}

WorldAaPropertyWidget::WorldAaPropertyWidget()
{
}

void WorldAaPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
        rebuild();
    } else {
        this->scene.clear();
    }
}

void WorldAaPropertyWidget::setSceneView(IEditorViewport *sceneView)
{
    this->sceneView = sceneView;
}

void WorldAaPropertyWidget::rebuild()
{
    clearPanel(this->layout());

    samplesSelector = this->addComboBox("MSAA");
    for (int i = 0; i < kAaRowCount; ++i)
        samplesSelector->addItem(aaName(kAaSamples[i]));
    samplesSelector->setCurrentIndex(aaRowFor(scene->antiAliasing));
    connect(samplesSelector, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
            this, &WorldAaPropertyWidget::onSamplesChanged);

    // The driver may clamp the request (Vulkan guarantees only 1x and 4x):
    // when the live viewport achieves something else, say so.
    if (sceneView && sceneView->isInitialized()) {
        const int achieved = sceneView->sampleCount();
        if (achieved != scene->antiAliasing)
            this->addLabel("Driver Delivers", aaName(achieved));
    }
}

void WorldAaPropertyWidget::onSamplesChanged(int row)
{
    if (!scene || row < 0 || row >= kAaRowCount) return;
    // The document field is the API (same path as world.setAntiAliasing):
    // SceneMirror pushes it to the engine view at the next sync.
    scene->antiAliasing = kAaSamples[row];
    // A direct edit of a backing field is a World Mode PIN (POST_CHAIN_SPEC §9.1).
    worldmodes::pinRowValue(scene, QStringLiteral("msaa"), scene->antiAliasing);
    if (sceneView && sceneView->isInitialized())
        sceneView->renderFrames(2);   // apply now so the achieved count is readable
    rebuild();                        // refresh (or clear) the achieved row
}
