/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef WORLDAAPROPERTYWIDGET_H
#define WORLDAAPROPERTYWIDGET_H

#include <QWidget>
#include "ui/controls/accordionbladewidget.h"
#include "irisgl/irisglfwd.h"

class ComboBoxWidget;
class IEditorViewport;

/**
 * World-panel "Anti-Aliasing" section (WORLD_AA_SPEC.md phase 1): hardware
 * MSAA for the scene's viewport, Off / 2x / 4x / 8x. The choice lives on the
 * document (scene->antiAliasing, saved with the scene) and SceneMirror pushes
 * it to the engine each frame — the exact path world.setAntiAliasing() takes.
 * The driver may clamp the request (Vulkan guarantees only 1x and 4x), so when
 * the engine viewport reports a different ACHIEVED count, a row shows it.
 */
class WorldAaPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    WorldAaPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);
    /// The live viewport, for the achieved-count readback. Nullable (legacy
    /// viewport / headless): without it the widget simply shows the request.
    void setSceneView(IEditorViewport *sceneView);

protected slots:
    void onSamplesChanged(int row);

private:
    void rebuild();

    QSharedPointer<iris::Scene> scene;
    IEditorViewport *sceneView = nullptr;
    ComboBoxWidget *samplesSelector = nullptr;
};

#endif // WORLDAAPROPERTYWIDGET_H
