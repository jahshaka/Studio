#include "postprocesspropertywidget.h"
#include "../../irisgl/src/irisglfwd.h"
#include "../../irisgl/src/graphics/postprocess.h"
#include "../propertywidget.h"

#include "../../irisgl/src/core/property.h"

#include <QDebug>

PostProcessPropertyWidget::PostProcessPropertyWidget()
{
    propWidget = this->addPropertyWidget();
    propWidget->setListener(this);
}

void PostProcessPropertyWidget::setPostProcess(iris::PostProcessPtr postProcess)
{
    this->postProcess = postProcess;
    propWidget->setProperties(postProcess->getProperties());
}

void PostProcessPropertyWidget::onPropertyChanged(iris::Property *prop)
{
    postProcess->setProperty(prop);
}
