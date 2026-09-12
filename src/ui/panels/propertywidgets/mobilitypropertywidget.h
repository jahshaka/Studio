/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MOBILITYPROPERTYWIDGET_H
#define MOBILITYPROPERTYWIDGET_H

#include <QSharedPointer>
#include <QWidget>

#include "irisgl/irisglfwd.h"
#include "ui/controls/accordionbladewidget.h"
#include "ui/panels/propertywidgets/panelundo.h"

class LabelWidget;

/// MOVEMENT — the per-object mobility row (SPECS/REALTIME_REFLECTIONS_SPEC.md
/// §3.3). One combo over the reflected `mobility` key, plus a line saying what
/// Auto worked out to and why.
///
/// EVERY node kind gets this blade, not just meshes: a lamp on a swinging arm
/// and a character both need it, and the resolution has an answer for all of
/// them.
///
/// It is the UI half of a verb that already exists — node.setProperty(id,
/// 'mobility', ...) and node.mobility(id) — so the combo writes the same
/// reflected key a script does and rides the same undo command (rowundo).
class MobilityPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    MobilityPropertyWidget();

    void setSceneNode(iris::SceneNodePtr sceneNode);
    void setServices(StudioServices *s) { services = s; }

    /// The combo's items, in index order, as the DOCUMENT spells them
    /// (iris::mobilityName). Index IS iris::Mobility, which is what makes the
    /// row's mapping a cast rather than a table.
    static QStringList settingNames();
    /// "Auto (Movable - animation)" / "Movable (you set this)" — the resolved
    /// line under the combo. Static so the test can assert the wording without
    /// standing up a panel.
    static QString resolvedText(const iris::SceneNodePtr &node);

private slots:
    void refreshResolved();

private:
    iris::SceneNodePtr sceneNode;
    StudioServices *services = nullptr;
    bool loading = false;
    panelundo::NodeRows rows;
    ComboBoxWidget *setting = nullptr;
    LabelWidget *resolved = nullptr;
};

#endif // MOBILITYPROPERTYWIDGET_H
