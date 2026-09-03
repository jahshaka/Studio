/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MESHPROPERTYWIDGET_H
#define MESHPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>

#include "irisgl/irisglfwd.h"
#include "ui/controls/accordionbladewidget.h"

class IEditorViewport;

class MeshPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    MeshPropertyWidget();
    ~MeshPropertyWidget();

    void setSceneNode(iris::SceneNodePtr sceneNode);
    /// The live viewport, needed by the Planar Reflector row: only the renderer
    /// can say whether a mesh is flat enough to BE a reflection plane, so the
    /// row has to be able to ask (and put itself back when the answer is no).
    void setSceneView(IEditorViewport *view) { sceneView = view; }

protected slots:
    void onMeshPathChanged(const QString&);
	void onCullModeChanged(const QString&);
    void onPlanarReflectorChanged(bool);

private:
    QSharedPointer<iris::MeshNode> meshNode;
    FilePickerWidget* meshPicker;
	ComboBoxWidget* faceCullMode;
    CheckBoxWidget* planarReflector = nullptr;
    IEditorViewport* sceneView = nullptr;
};

#endif // MESHPROPERTYWIDGET_H
