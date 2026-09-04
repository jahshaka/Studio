/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/vec.h"
#include "vectorpropertywidget.h"
#include <QLabel>
#include <QDebug>

VectorPropertyWidget::VectorPropertyWidget()
{
}


VectorPropertyWidget::~VectorPropertyWidget()
{
}


Vector2DPropertyWidget::Vector2DPropertyWidget() : BasePropertyWidget()
{
	x = y = 0;
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	wid = new Widget2D(); 
	setWidget(wid);
	setConnections();
}

void Vector2DPropertyWidget::setConnections()
{

	connect(wid, &Widget2D::valueChanged, [=](iris::Vec2 val) {
		x = val.x();
		y = val.y();
		setPropValues(val);
		emit valueChanged(val);
	});
}

Vector2DPropertyWidget::~Vector2DPropertyWidget()
{
}

void Vector2DPropertyWidget::setProp(Vec2Property *prop)
{
	this->prop = prop;
	displayName->setText(prop->displayName);
	wid->xSpinBox->setValue(prop->value.x());
	wid->ySpinBox->setValue(prop->value.y());
	modelProperty = prop;
	emit nameChanged(displayName->text());
}

void Vector2DPropertyWidget::setPropValues(iris::Vec2 values) {
	value = values;
	prop->value.setX(x);
	prop->value.setY(y);
}


/////////////////////////////


Vector3DPropertyWidget::Vector3DPropertyWidget() : BasePropertyWidget()
{
	x = y = z = 0;
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);	
	wid = new Widget3D;
	setWidget(wid);
	setConnections();
}


Vector3DPropertyWidget::~Vector3DPropertyWidget()
{
}

void Vector3DPropertyWidget::setProp(Vec3Property *prop)
{
	this->prop = prop;
	displayName->setText(prop->displayName);
	wid->xSpinBox->setValue(prop->value.x());
	wid->ySpinBox->setValue(prop->value.y());
	wid->zSpinBox->setValue(prop->value.z());
	modelProperty = prop;
	emit nameChanged(displayName->text());
}


void Vector3DPropertyWidget::setConnections()
{
	connect(wid, &Widget3D::valueChanged, [=](iris::Vec3 val) {
		x = val.x();
		y = val.y();
		z = val.z();
		setPropValues(val);
		emit valueChanged(val);
	});
}

void Vector3DPropertyWidget::setPropValues(iris::Vec3 values) {
	value = values;
	prop->value.setX(x);
	prop->value.setY(y);
	prop->value.setZ(z);
}


/////////////////////////////


Vector4DPropertyWidget::Vector4DPropertyWidget() : BasePropertyWidget()
{
	x = y = z = 0;
	w = 1;

	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	wid = new Widget4D;
	setWidget(wid);
	setConnections();
}


Vector4DPropertyWidget::~Vector4DPropertyWidget()
{
}

void Vector4DPropertyWidget::setProp(Vec4Property *prop)
{
	this->prop = prop;
	displayName->setText(prop->displayName);
	wid->xSpinBox->setValue(prop->value.x());
	wid->ySpinBox->setValue(prop->value.y());
	wid->zSpinBox->setValue(prop->value.z());
	wid->wSpinBox->setValue(prop->value.w());
	modelProperty = prop;
	emit nameChanged(displayName->text());
}

void Vector4DPropertyWidget::setConnections()
{
	connect(wid, &Widget4D::valueChanged, [=](iris::Vec4 val) {
		x = val.x();
		y = val.y();
		z = val.z();
		w = val.w();
		setPropValues(val);
		emit valueChanged(val);

	});
}


void Vector4DPropertyWidget::setPropValues(iris::Vec4 values) 
{
	value = values;
	prop->value.setX(x);
	prop->value.setY(y);
	prop->value.setZ(z);
	prop->value.setW(w);
}


