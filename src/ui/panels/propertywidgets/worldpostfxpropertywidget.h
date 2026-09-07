/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef WORLDPOSTFXPROPERTYWIDGET_H
#define WORLDPOSTFXPROPERTYWIDGET_H

#include <QVector>
#include <QWidget>

#include "ui/controls/accordionbladewidget.h"
#include "irisgl/irisglfwd.h"

class IEditorViewport;

/**
 * World-panel "Post Process" section (owner request 2026-09-07, fix wave item
 * 8) — every post-processing effect in one place, with its on/off control AND
 * its parameters: Bloom (+threshold), HDR (+exposure and the auto-exposure
 * window), Ambient Occlusion (+power, radius, buffer scale), SMAA, SSR,
 * Refractive Glass.
 *
 * BEFORE THIS, the post chain was scattered and half-invisible: the on/off rows
 * were buried among shadow and reflection rows in the World Mode section (which
 * is a SCALABILITY tier list, so they read as quality settings rather than as
 * effects), and the continuous parameters — exposure, bloom threshold, AO power
 * and radius — had no UI at all. `world.postFx` was the only way to reach them.
 *
 * GENERATED FROM THE REGISTRY, both halves (src/services/worldmodes.h): the
 * on/off controls come from `rows()` filtered by `postFxRowIds()`, the
 * parameter rows from `postFxParams()`, and every parameter row is grouped
 * under the effect that owns it. This file names no effect and no range. Adding
 * a post effect is a table entry.
 *
 * Writes go through the same code paths the verbs take — worldmodes::
 * setRowValue for the on/off rows (so the World Mode pin bookkeeping stays
 * correct) and the ParamRow's own setter for the parameters (the identical
 * function world.postFx calls) — so this panel is a second CONSUMER of the verb
 * layer, never a parallel implementation.
 *
 * Number rows are DragSpinBox rows (ui/controls/dragvaluewidgets.h), the same
 * compact scrubbable shape the transform editor and the GI section use.
 */
class WorldPostFxPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    WorldPostFxPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);
    /// The live viewport, so an edit is visible immediately. Nullable.
    void setSceneView(IEditorViewport *sceneView);

signals:
    /// An on/off row wrote through to a backing field the World Mode section
    /// also displays (and may have pinned); it needs rebuilding.
    void worldSettingsChanged();

private:
    void rebuild();
    void applied(bool rebuildPanel);

    QSharedPointer<iris::Scene> scene;
    IEditorViewport *sceneView = nullptr;
};

#endif // WORLDPOSTFXPROPERTYWIDGET_H
