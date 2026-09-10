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
#include "ui/panels/propertywidgets/panelundo.h"

#include <QSignalBlocker>

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
        build();
        refreshRows();
    } else {
        this->scene.clear();
    }
}

void WorldAaPropertyWidget::setSceneView(IEditorViewport *sceneView)
{
    this->sceneView = sceneView;
}

void WorldAaPropertyWidget::build()
{
    if (samplesSelector) return;   // the rows are built once and refilled

    samplesSelector = this->addComboBox("MSAA");
    for (int i = 0; i < kAaRowCount; ++i)
        samplesSelector->addItem(aaName(kAaSamples[i]));
    connect(samplesSelector, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
            this, &WorldAaPropertyWidget::onSamplesChanged);

    // The driver may clamp the request (Vulkan guarantees only 1x and 4x): the
    // row exists from the start and is hidden while the request was honoured,
    // rather than being added and removed — a row that comes and goes is what
    // makes a panel rebuild itself, which is what this lane is removing.
    achievedRow = this->addLabel("Driver Delivers", QString());
    if (achievedRow) achievedRow->hide();
}

void WorldAaPropertyWidget::refreshRows()
{
    if (!scene || !samplesSelector) return;
    loading = true;
    {
        const QSignalBlocker quiet(samplesSelector);
        samplesSelector->setCurrentIndex(aaRowFor(scene->antiAliasing));
    }
    const int achieved = (sceneView && sceneView->isInitialized()) ? sceneView->sampleCount()
                                                                   : scene->antiAliasing;
    if (achievedRow) {
        const bool clamped = achieved != scene->antiAliasing;
        achievedRow->setText(aaName(achieved));
        achievedRow->setVisible(clamped);
    }
    loading = false;
}

void WorldAaPropertyWidget::onSamplesChanged(int row)
{
    if (loading || !scene || row < 0 || row >= kAaRowCount) return;
    const int samples = kAaSamples[row];
    // THROUGH THE REGISTRY: the write AND the pin, as one undo step
    // (POST_CHAIN_SPEC §9.1 — a direct edit of a backing field is a pin, and an
    // undo that dropped the value but kept the pin would lie about the tier).
    panelundo::runWorldModeEdit(services, scene, tr("MSAA"),
        [this, samples]() { worldmodes::setRowValue(scene, QStringLiteral("msaa"), samples); },
        [this]() {
            // Apply now so the achieved count the row reads back is the truth.
            if (sceneView && sceneView->isInitialized()) sceneView->renderFrames(2);
            refreshRows();
        });
}
