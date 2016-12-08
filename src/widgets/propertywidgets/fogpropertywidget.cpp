#include "fogpropertywidget.h"
#include "../texturepicker.h"
#include "../../jah3d/core/scene.h"
#include "../../jah3d/materials/defaultskymaterial.h"

#include "../colorvaluewidget.h"
#include "../colorpickerwidget.h"
#include "../hfloatslider.h"

#include "../checkboxproperty.h"

FogPropertyWidget::FogPropertyWidget()
{
    fogEnabled = this->addCheckBox("Enabled",false);
    fogColor = this->addColorPicker("Fog Color");
    fogStart = this->addFloatValueSlider("Fog Start",0,1000);
    fogEnd = this->addFloatValueSlider("Fog End",0,1000);


    connect(fogColor->getPicker(),SIGNAL(onColorChanged(QColor)),SLOT(onFogColorChanged(QColor)));
    connect(fogStart,SIGNAL(valueChanged(float)),SLOT(onFogStartChanged(float)));
    connect(fogEnd,SIGNAL(valueChanged(float)),SLOT(onFogEndChanged(float)));
    connect(fogEnabled,SIGNAL(valueChanged(bool)),SLOT(onFogEnabledChanged(bool)));
}

void FogPropertyWidget::setScene(QSharedPointer<jah3d::Scene> scene)
{
    if(!!scene)
    {
        this->scene = scene;

        fogColor->setColorValue(scene->fogColor);
        fogStart->setValue(scene->fogStart);
        fogEnd->setValue(scene->fogEnd);
        fogEnabled->setValue(scene->fogEnabled);

    }
    else
    {
        this->scene.clear();
        //return;
        //todo: clear ui
    }


}

void FogPropertyWidget::onFogColorChanged(QColor color)
{
    if(!!scene)
        scene->fogColor = color;
}

void FogPropertyWidget::onFogStartChanged(float val)
{
    if(!!scene)
        scene->fogStart = val;
}

void FogPropertyWidget::onFogEndChanged(float val)
{
    if(!!scene)
        scene->fogEnd = val;
}

void FogPropertyWidget::onFogEnabledChanged(bool val)
{
    if(!!scene)
        scene->fogEnabled = val;
}
