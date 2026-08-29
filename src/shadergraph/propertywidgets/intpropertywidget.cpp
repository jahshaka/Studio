/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "intpropertywidget.h"
#include <QLabel>

IntPropertyWidget::IntPropertyWidget() : BasePropertyWidget()
{
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	wid = new WidgetInt;
	setWidget(wid);
	setConnections();

}

IntPropertyWidget
::~IntPropertyWidget()
{
}

void IntPropertyWidget::setProp(IntProperty * prop)
{
	this->prop = prop;
	displayName->setText(prop->displayName);
	wid->spinBox->setValue(prop->value);
	wid->minSpinBox->setValue(prop->minValue);
	wid->maxSpinBox->setValue(prop->maxValue);
	wid->stepSpinBox->setValue(prop->step);
	modelProperty = prop;
	emit nameChanged(displayName->text());

}

int IntPropertyWidget::getValue()
{
	return x;
}

void IntPropertyWidget::setPropValue(int value)
{
	prop->setValue(value);
}


void IntPropertyWidget::setConnections() {
	
	connect(wid, &WidgetInt::valueChanged, [=](int val) {
		x = val;
		setPropValue(val);
		emit valueChanged(val);
	});

	connect(wid, &WidgetInt::minChanged, [=](int val) {
		prop->minValue = val;
	});

	connect(wid, &WidgetInt::maxChanged, [=](int val) {
		prop->maxValue = val;
	});

	connect(wid, &WidgetInt::stepChanged, [=](int val) {
		prop->step = val;
	});
}

