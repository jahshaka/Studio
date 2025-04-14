/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

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

void PostProcessPropertyWidget::onPropertyChangeStart(iris::Property* prop)
{
}

void PostProcessPropertyWidget::onPropertyChangeEnd(iris::Property* prop)
{
}
