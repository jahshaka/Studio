#include "demopane.h"

#include "../checkboxproperty.h"

DemoPane::DemoPane()
{
    demoSlider = this->addFloatValueSlider("Demo Slider", 1.f, 1000.f);
    demoColor = this->addColorPicker("Demo Color");
    demoCheck = this->addCheckBox("Demo Checkbox", true);
}
