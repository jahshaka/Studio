/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/worldshadowpropertywidget.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/shadowmap.h"

#include "ui/controls/comboboxwidget.h"
#include "ui/controls/labelwidget.h"
#include "viewport/ieditorviewport.h"

namespace {
// Combo rows in display order -> scene->shadowResolution values. 0 = Auto.
// The engine accepts up to 8192, but 8192 is a ~940 MB allocation: scripts may
// ask for it (world.setShadowResolution warns), the UI does not offer it.
const int kShadowRows[] = { 0, 1024, 2048, 4096 };
const int kShadowRowCount = int(sizeof(kShadowRows) / sizeof(kShadowRows[0]));
int shadowRowFor(int resolution)
{
    for (int i = 0; i < kShadowRowCount; ++i)
        if (kShadowRows[i] == resolution) return i;
    return 0;   // anything a script set outside the offered set reads as Auto
}
}

WorldShadowPropertyWidget::WorldShadowPropertyWidget()
{
}

int WorldShadowPropertyWidget::atlasMegabytes(int resolution)
{
    if (resolution <= 0) return 0;
    // OgreEngine::createShadowNode builds R x 3.5R at PFG_D32_FLOAT: split 0 and
    // the two focused maps at RxR, splits 1-3 at R/2. 4 bytes per texel.
    return int(qRound(4.0 * 3.5 * double(resolution) * double(resolution) / (1024.0 * 1024.0)));
}

void WorldShadowPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
        rebuild();
    } else {
        this->scene.clear();
    }
}

void WorldShadowPropertyWidget::setSceneView(IEditorViewport *sceneView)
{
    this->sceneView = sceneView;
}

int WorldShadowPropertyWidget::derivedFromLights() const
{
    if (!scene) return 0;
    // SceneMirror's policy, verbatim: area lights cannot cast, "None" does not
    // ask, and the largest remaining request wins.
    int best = 0;
    for (const auto &l : scene->lights) {
        if (l.isNull() || l->lightType == iris::LightType::Area) continue;
        if (!l->shadowMap || l->shadowMap->shadowType == iris::ShadowMapType::None) continue;
        best = std::max(best, l->shadowMap->resolution);
    }
    return best;
}

void WorldShadowPropertyWidget::rebuild()
{
    clearPanel(this->layout());
    if (!scene) return;

    qualitySelector = this->addComboBox("Shadow Quality");
    qualitySelector->addItem("Auto (from lights)");
    qualitySelector->addItem("1024");
    qualitySelector->addItem("2048");
    qualitySelector->addItem("4096");
    qualitySelector->setCurrentIndex(shadowRowFor(scene->shadowResolution));
    qualitySelector->setToolTip(
        QStringLiteral("One shadow atlas serves every light in the scene. Auto sizes it from the "
                       "largest per-light Shadow Size; an explicit choice overrides that."));
    connect(qualitySelector, QOverload<int>::of(&ComboBoxWidget::currentIndexChanged),
            this, &WorldShadowPropertyWidget::onQualityChanged);

    // What is actually in use, and what it costs. Auto has to say what it
    // derived or the row is a mystery; the VRAM figure is the guard-rail.
    const int derived = derivedFromLights();
    int effective = scene->shadowResolution > 0 ? scene->shadowResolution : derived;
    if (sceneView && sceneView->isInitialized()) {
        const int live = sceneView->shadowResolution();
        if (live > 0) effective = live;
    }
    if (scene->shadowResolution == 0) {
        this->addLabel("Auto Resolves To",
                       derived > 0 ? QStringLiteral("%1 (largest light request)").arg(effective)
                                   : QStringLiteral("no shadow-casting light yet"));
    }
    if (effective > 0)
        this->addLabel("Atlas Memory", QStringLiteral("~%1 MB VRAM (%2 x %3)")
                                           .arg(atlasMegabytes(effective))
                                           .arg(effective).arg(qRound(effective * 3.5)));
}

void WorldShadowPropertyWidget::onQualityChanged(int row)
{
    if (!scene || row < 0 || row >= kShadowRowCount) return;
    // The document field is the API (same path as world.setShadowResolution):
    // SceneMirror pushes it to the engine at the next sync. The engine rebuilds
    // its shadow node and every workspace on change, so this is deliberately a
    // combo and not a slider.
    scene->shadowResolution = kShadowRows[row];
    if (sceneView && sceneView->isInitialized())
        sceneView->renderFrames(2);   // apply now so the readback below is the truth
    rebuild();
}
