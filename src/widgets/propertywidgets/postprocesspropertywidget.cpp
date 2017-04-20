#include "postprocesspropertywidget.h"
#include "../../irisgl/src/irisglfwd.h"
#include "../../irisgl/src/graphics/postprocess.h"
#include "../propertywidget.h"

PostProcessPropertyWidget::PostProcessPropertyWidget()
{
    auto prop = this->addPropertyWidget();
    prop->setListener(this);
}

void PostProcessPropertyWidget::setPostProcess(iris::PostProcessPtr postProcess)
{
    //this->setProperties(postProcess->getProperties());
}

void PostProcessPropertyWidget::onPropertyChanged(iris::Property *prop)
{
    postProcess->setProperty(prop);
}
