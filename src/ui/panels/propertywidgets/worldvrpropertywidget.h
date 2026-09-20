/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef WORLDVRPROPERTYWIDGET_H
#define WORLDVRPROPERTYWIDGET_H

#include <QVector>
#include <QWidget>

#include "irisgl/irisglfwd.h"
#include "services/vrworld.h"
#include "ui/controls/accordionbladewidget.h"
#include "ui/controls/bladerow.h"

class ComboBoxWidget;
class DragFloatWidget;
struct StudioServices;

/**
 * World-panel "VR" section (owner request 2026-09-18: "default fly speed — use
 * what you like — and add a VR World settings section where we can set that").
 *
 * WHAT A WEARER'S MOVEMENT IS, per PROJECT: the fly speed in metres per second,
 * which direction the stick flies, how it turns and by how much, and which hand
 * manipulates. Every session this project opens adopts them
 * (EditorVrPreview::begin, PlayerVr::begin) and `vr.locomotion` overrides them
 * for one session without touching the document.
 *
 * GENERATED FROM THE TABLE (src/services/vrworld.h), the way the Post Process
 * section is generated from the post-fx registry: this file names no setting,
 * no range and no mode. Adding one is a table entry. Writes go through the same
 * sceneprops keys `world.vr` writes, so the panel is a second CONSUMER of the
 * verb layer and never a parallel implementation — and each gesture is one undo
 * step (panelundo/rowundo, debt L6).
 */
class WorldVrPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    WorldVrPropertyWidget();
    void setScene(QSharedPointer<iris::Scene> scene);
    /// The undo stack. Nullable (headless hosts and the panel suites).
    void setServices(StudioServices *s) { services = s; }

private:
    /// The rows, ONCE, from the table (debt L6: an edit refreshes them, it does
    /// not rebuild the blade — the control the user just touched must survive
    /// its own signal).
    void build();
    /// Re-reads every row's value and whether it is live, in place.
    void refreshRows();

    struct RowWidgets
    {
        QString id;
        RowPtr<ComboBoxWidget> combo;    ///< Enum rows
        RowPtr<DragFloatWidget> field;   ///< Number rows
        RowPtr<CheckBoxWidget> flag;     ///< Flag rows (a switch)
    };

    QSharedPointer<iris::Scene> scene;
    StudioServices *services = nullptr;
    bool loading = false;
    QVector<RowWidgets> rows;
};

#endif // WORLDVRPROPERTYWIDGET_H
