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

#include <QJsonArray>
#include <QVector>
#include <QWidget>

#include "ui/controls/accordionbladewidget.h"
#include "irisgl/irisglfwd.h"

class IEditorViewport;
class CheckBoxWidget;
class ComboBoxWidget;
class DragFloatWidget;
class LabelWidget;
struct StudioServices;

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
    /// The undo stack (debt L6): an on/off row is one WorldModeCommand (value
    /// + pin), a parameter scrub is one ScenePropertyCommand, and a looks
    /// gesture is one ScenePropertyCommand over the whole stack. Nullable.
    void setServices(StudioServices *s) { services = s; }

signals:
    /// An on/off row wrote through to a backing field the World Mode section
    /// also displays (and may have pinned); it needs rebuilding.
    void worldSettingsChanged();

private:
    /// The effect rows and their parameters, built ONCE from the registry
    /// (debt L6: an edit refreshes them, it does not rebuild the blade — the
    /// control the user just touched must survive its own signal).
    void build();
    /// Re-reads every row, its pin mark and its enabled state, in place.
    void refreshRows();
    /// The ordered LOOKS stack (POST_LOOKS_SPEC.md §4.1) — the one part of this
    /// section that is not a flat list of rows, because a stack is not one, so
    /// it lives in a sub-section of its own and IS rebuilt when it changes
    /// shape. Scrubbing a look's parameter does not rebuild it.
    void rebuildLooks();
    void applied();
    /// Writes the looks stack as ONE undo step named for the gesture.
    void commitLooks(const QJsonArray &before, const QString &text);

    struct EffectRow
    {
        QString id;
        CheckBoxWidget *box = nullptr;
        ComboBoxWidget *combo = nullptr;
        LabelWidget *unavailable = nullptr;
    };
    struct ParamField
    {
        QString id;
        QString ownerRowId;
        DragFloatWidget *field = nullptr;
    };

    QSharedPointer<iris::Scene> scene;
    IEditorViewport *sceneView = nullptr;
    StudioServices *services = nullptr;
    bool loading = false;
    QVector<EffectRow> effectRows;
    QVector<ParamField> paramFields;
    AccordianBladeWidget *looksSection = nullptr;
    LabelWidget *looksHeading = nullptr;
    /// The stack as it was when the current scrub began (looks parameters have
    /// no start signal of their own — the editor reports the end of one).
    QJsonArray looksBefore;
    bool looksScrubbing = false;
};

#endif // WORLDPOSTFXPROPERTYWIDGET_H
