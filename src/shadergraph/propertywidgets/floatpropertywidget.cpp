/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "floatpropertywidget.h"
#include <QLabel>

FloatPropertyWidget::FloatPropertyWidget() : BasePropertyWidget()
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	wid = new WidgetFloat;
	setWidget(wid);
	setConnections();
}

FloatPropertyWidget::~FloatPropertyWidget()
{
}

void FloatPropertyWidget::setProp(FloatProperty * prop)
{
	this->prop = prop;
	displayName->setText(prop->displayName);
	wid->floatSpinBox->setValue(prop->value);
	wid->minSpinBox->setValue(prop->minValue);
	wid->maxSpinBox->setValue(prop->maxValue);
	wid->stepSpinBox->setValue(prop->step);
	modelProperty = prop;
	emit nameChanged(displayName->text());

}

float FloatPropertyWidget::getValue()
{
	return x;
}

void FloatPropertyWidget::setPropValue(double value)
{
	prop->setValue(value);
}


void FloatPropertyWidget::setConnections() {

	connect(wid, &WidgetFloat::valueChanged, [=](double val) {
		x = val;
		setPropValue(val);
		emit valueChanged(val);
	});

	connect(wid, &WidgetFloat::minChanged, [=](double val) {
		prop->minValue = val;
	});

	connect(wid, &WidgetFloat::maxChanged, [=](double val) {
		prop->maxValue = val;
	});

	connect(wid, &WidgetFloat::stepChanged, [=](double val) {
		prop->step = val;
	});

}



