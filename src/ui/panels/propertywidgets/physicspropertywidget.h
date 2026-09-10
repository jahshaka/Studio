/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PHYSICSPROPERTYWIDGET_H
#define PHYSICSPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>

#include "irisgl/irisglfwd.h"
#include "ui/controls/accordionbladewidget.h"
#include "ui/panels/propertywidgets/panelundo.h"

class IEditorViewport;
class btRigidBody;

class PhysicsPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    PhysicsPropertyWidget();
    ~PhysicsPropertyWidget();

    void setSceneNode(iris::SceneNodePtr sceneNode);
    void setSceneView(IEditorViewport *sceneView);
    /// The undo stack (debt L6). A physics row is not a reflected property —
    /// the body settings live in a struct — so an edit records the same shape
    /// node.physics does: apply, then a command that replays the struct.
    void setServices(StudioServices *s) { services = s; }

protected slots:
    void onPhysicsTypeChanged(int);
    void onPhysicsShapeChanged(int);
    void onVisibilityChanged(bool);

private:
    /// One physics gesture: the struct before, the struct after, one step.
    /// `text` names it; `apply` is what the row just did.
    void edit(const QString &text, const std::function<void()> &apply);
    /// A scalar row bound to one field of the body settings.
    rowundo::Binding bodyRow(const QString &text, std::function<float()> get,
                             std::function<void(float)> set);

    btRigidBody *currentBody;
    StudioServices *services = nullptr;
    /// Populating the rows for a newly selected node (rowundo's guard).
    bool loading = false;
    iris::SceneNodePtr sceneNode;
    IEditorViewport *sceneView;

    CheckBoxWidget* isVisible;
    HFloatSliderWidget *massValue;
    HFloatSliderWidget *frictionValue;
    HFloatSliderWidget *bouncinessValue;
    HFloatSliderWidget *marginValue;
    ComboBoxWidget *physicsTypeSelector;
    ComboBoxWidget *physicsShapeSelector;

    QMap<int, QString> physicsTypes;
    QMap<int, QString> physicsShapes;
};

#endif // PHYSICSPROPERTYWIDGET_HPP