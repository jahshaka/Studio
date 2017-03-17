/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MESHPROPERTYWIDGET_H
#define MESHPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>

#include "../../irisgl/src/irisglfwd.h"
#include "../accordianbladewidget.h"

class MeshPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    MeshPropertyWidget();
    ~MeshPropertyWidget();

    void setSceneNode(iris::SceneNodePtr sceneNode);

protected slots:
    void onMeshPathChanged(const QString&);

private:
    QSharedPointer<iris::MeshNode> meshNode;
    FilePickerWidget* meshPicker;
};

#endif // MESHPROPERTYWIDGET_H
