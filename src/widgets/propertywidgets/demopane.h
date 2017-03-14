/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef DEMOPANE_H
#define DEMOPANE_H

#include <QWidget>
#include <QSharedPointer>
#include "../../irisgl/src/irisglfwd.h"

#include "../accordianbladewidget.h"

/*
 *  DO NOT REMOVE THIS FILE!
 *
 *  This is a testbed of sorts for every pane and custom widget to make sure they
 *  conform and are consistent. If you add a new widget type, add it here as well.
 *
 */

class DemoPane : public AccordianBladeWidget
{
    Q_OBJECT

public:
    DemoPane();

    void setSceneNode(iris::SceneNodePtr sceneNode) {

    }

protected slots:
    // ---

private:
    HFloatSliderWidget* demoSlider;
    ColorValueWidget* demoColor;
    CheckBoxWidget* demoCheck;
    ComboBoxWidget* demoCombo;
    TexturePickerWidget* demoPicker;
    TextInputWidget* demoInput;
    LabelWidget* demoLabel;
    FilePickerWidget* demoFile;
};

#endif // DEMOPANE_H
