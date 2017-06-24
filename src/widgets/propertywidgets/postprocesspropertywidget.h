#ifndef POSTPROCESSPROPERTYWIDGET_H
#define POSTPROCESSPROPERTYWIDGET_H

#include "../accordianbladewidget.h"
#include "../../irisgl/src/irisglfwd.h"
#include "../../irisgl/src/core/property.h"

#include "../hfloatsliderwidget.h"


class PropertyWidget;

class PostProcessPropertyWidget :
        public AccordianBladeWidget,
        public iris::PropertyListener
{
    iris::PostProcessPtr postProcess;
public:
    PostProcessPropertyWidget();

    void setPostProcess(iris::PostProcessPtr postProcess);

    // PropertyListener interface
public:
    void onPropertyChanged(iris::Property*) override;
    void onPropertyChangeStart(iris::Property*) override;
    void onPropertyChangeEnd(iris::Property*) override;

    PropertyWidget* propWidget;
    HFloatSliderWidget* slider;
};

#endif // POSTPROCESSPROPERTYWIDGET_H
