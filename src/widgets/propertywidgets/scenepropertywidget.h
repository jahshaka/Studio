/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEPROPERTYWIDGET_H
#define SCENEPROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>
#include "../accordianbladewidget.h"

class ColorValueWidget;
class ColorPickerWidget;
namespace iris
{
    class Scene;
}

class ScenePropertyWidget:public AccordianBladeWidget
{
    Q_OBJECT

public:
    ScenePropertyWidget(QWidget* parent = nullptr);

    void setScene(QSharedPointer<iris::Scene> sceneNode);

private:
    QSharedPointer<iris::Scene> sceneNode;
};

#endif // SCENEPROPERTYWIDGET_H
