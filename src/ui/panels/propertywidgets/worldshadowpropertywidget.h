/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef WORLDSHADOWPROPERTYWIDGET_H
#define WORLDSHADOWPROPERTYWIDGET_H

#include <QWidget>
#include "ui/controls/accordionbladewidget.h"
#include "irisgl/irisglfwd.h"

class ComboBoxWidget;
class IEditorViewport;

/**
 * World-panel "Shadows" section (VISUAL_PARITY_SPEC item 2, option A).
 *
 * The renderer keeps ONE shadow atlas for every light in the scene, sized from
 * a single base resolution (PSSM split 0 and the two focused maps at RxR, the
 * remaining splits at R/2 — an RxR*3.5 D32_FLOAT allocation). The Light panel's
 * per-light "Shadow Size" is therefore only a request: whichever light asks for
 * the most wins. This row makes that truth settable and visible.
 *
 * "Auto" (scene->shadowResolution == 0) keeps the historical derive-from-lights
 * behaviour and shows what it derived; an explicit choice overrides it. The
 * VRAM cost of the choice is spelled out beside it, because 4096 is a quarter
 * of a gigabyte and the difference is not otherwise visible until a machine
 * with less memory tries to open the scene.
 *
 * The document field is the API: SceneMirror pushes it to the engine each
 * frame, exactly the path world.setShadowResolution() takes.
 */
class WorldShadowPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    WorldShadowPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);
    /// The live viewport, for the applied-resolution readback. Nullable
    /// (headless): without it the widget shows what the document asked for.
    void setSceneView(IEditorViewport *sceneView);

    /// VRAM the atlas costs at a given base resolution, in MB: the engine
    /// allocates R wide x 3.5R tall at 32-bit depth. Static so the verb docs and
    /// the tests can quote the same number.
    static int atlasMegabytes(int resolution);

protected slots:
    void onQualityChanged(int row);

private:
    void rebuild();
    /// What Auto would derive right now: the largest Shadow Size among the
    /// scene's shadow-casting lights (the mirror's own policy), or 0 if none.
    int derivedFromLights() const;

    QSharedPointer<iris::Scene> scene;
    IEditorViewport *sceneView = nullptr;
    ComboBoxWidget *qualitySelector = nullptr;
};

#endif // WORLDSHADOWPROPERTYWIDGET_H
