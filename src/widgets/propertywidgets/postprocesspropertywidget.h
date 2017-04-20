#ifndef POSTPROCESSPROPERTYWIDGET_H
#define POSTPROCESSPROPERTYWIDGET_H

#include "../accordianbladewidget.h"
#include "../../irisgl/src/irisglfwd.h"
#include "../../irisgl/src/materials/propertytype.h"

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
    void onPropertyChanged(iris::Property *);
};

#endif // POSTPROCESSPROPERTYWIDGET_H
