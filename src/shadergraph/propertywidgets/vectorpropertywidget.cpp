/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

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

	connect(wid, &Widget2D::valueChanged, [=](QVector2D val) {
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

void Vector2DPropertyWidget::setPropValues(QVector2D values) {
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
	connect(wid, &Widget3D::valueChanged, [=](QVector3D val) {
		x = val.x();
		y = val.y();
		z = val.z();
		setPropValues(val);
		emit valueChanged(val);
	});
}

void Vector3DPropertyWidget::setPropValues(QVector3D values) {
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
	connect(wid, &Widget4D::valueChanged, [=](QVector4D val) {
		x = val.x();
		y = val.y();
		z = val.z();
		w = val.w();
		setPropValues(val);
		emit valueChanged(val);

	});
}


void Vector4DPropertyWidget::setPropValues(QVector4D values) 
{
	value = values;
	prop->value.setX(x);
	prop->value.setY(y);
	prop->value.setZ(z);
	prop->value.setW(w);
}





