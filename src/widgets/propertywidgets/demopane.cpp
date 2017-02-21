#include "demopane.h"

#include "../checkboxproperty.h"
#include "../combobox.h"
#include "../texturepicker.h"

DemoPane::DemoPane()
{
    demoSlider  = this->addFloatValueSlider("Demo Slider", 1.f, 1000.f);
    demoColor   = this->addColorPicker("Demo Color");
    demoCheck   = this->addCheckBox("Demo Checkbox", true);

    demoCombo   = this->addComboBox("Demo Combo");
    demoCombo->addItem("Circle");
    demoCombo->addItem("Square");

    demoPicker = this->addTexturePicker("Demo Picker");
}
