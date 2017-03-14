/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "demopane.h"

#include "../checkboxwidget.h"
#include "../comboboxwidget.h"
#include "../texturepickerwidget.h"
#include "../textinputwidget.h"
#include "../labelwidget.h"
#include "../filepickerwidget.h"

#include <QDebug>

DemoPane::DemoPane()
{
    demoSlider  = this->addFloatValueSlider("Demo Slider", 1.f, 1000.f);
    demoCheck   = this->addCheckBox("Demo Checkbox", true);

    demoCombo   = this->addComboBox("Demo Combo");
    demoCombo->addItem("Circle");
    demoCombo->addItem("Square");

    demoPicker  = this->addTexturePicker("Demo Picker");

    demoInput   = this->addTextInput("Demo Text Input");
    demoColor   = this->addColorPicker("Demo Color");

    demoLabel   = this->addLabel("Demo Label", "The fat fox runs!");
    demoFile    = this->addFilePicker("Demo File Picker");
}
