/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef WORLDMODESPROPERTYWIDGET_H
#define WORLDMODESPROPERTYWIDGET_H

#include <QVector>
#include <QWidget>

#include "ui/controls/accordionbladewidget.h"
#include "irisgl/irisglfwd.h"

class ComboBoxWidget;
class CheckBoxWidget;
class IEditorViewport;

/**
 * World-panel "World Mode" section (POST_CHAIN_SPEC.md §9.6) — the per-scene
 * scalability tier (Low / Medium / High / Epic) and every quality row it
 * resolves, with per-row pins that survive mode switches.
 *
 * GENERATED FROM THE REGISTRY (src/services/worldmodes.h), the way the
 * Preferences shortcuts page is generated from ShortcutRegistry: this file
 * knows no row by name, so a new quality row is a table entry and nothing here
 * changes. Rows whose renderer support has not landed render disabled.
 *
 * Everything it writes goes through worldmodes::setRowValue / setMode, i.e.
 * through the same code path world.override() / world.mode() take — the panel
 * is a second consumer of the verb layer, never a parallel implementation.
 */
class WorldModesPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    WorldModesPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);
    /// The live viewport, so an edit is applied immediately. Nullable.
    void setSceneView(IEditorViewport *sceneView);

signals:
    /// A World Mode edit wrote through to the backing fields the sibling World
    /// sections display; they need rebuilding.
    void worldSettingsChanged();

protected slots:
    void onModeChanged(int row);
    void onRowChanged(int rowIndex);

private:
    void rebuild();
    void applied();

    QSharedPointer<iris::Scene> scene;
    IEditorViewport *sceneView = nullptr;
    ComboBoxWidget *modeSelector = nullptr;
    /// Parallel to the registry order: the control for each row, or null when
    /// the row was rendered as a plain label (unavailable rows).
    QVector<QWidget *> rowControls;
};

#endif // WORLDMODESPROPERTYWIDGET_H
