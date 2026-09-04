#pragma once

#include "irisgl/core/math/vec.h"
#include <QWidget>
#include <QGridLayout>
#include <QSpinBox>
#include <QLineEdit>
#include <QPainter>
#include "../models/properties.h"
#include "basepropertywidget.h"
#include "propertywidgetbase.h"

class VectorPropertyWidget : public BasePropertyWidget
{
public:
	VectorPropertyWidget();
	~VectorPropertyWidget();
};

class Vector2DPropertyWidget : public BasePropertyWidget
{
	Q_OBJECT
public:
	Vector2DPropertyWidget();
	~Vector2DPropertyWidget();
	void setProp(Vec2Property *prop);
	


private:
	Vec2Property *prop;
	double x;
	double y;
	iris::Vec2 value;
	Widget2D *wid;

	void setConnections();

protected:

public slots:
	void setPropValues(iris::Vec2 values);
signals:
	void valueChanged(iris::Vec2 val);
	void nameChanged(QString name);


};

class Vector3DPropertyWidget : public BasePropertyWidget
{
	Q_OBJECT
public:
	Vector3DPropertyWidget();
	~Vector3DPropertyWidget();
	void setProp(Vec3Property *prop);


private:
	Vec3Property *prop;
	double x;
	double y;
	double z;
	iris::Vec3 value;
	Widget3D *wid;

	void setConnections();

protected:

public slots:
	void setPropValues(iris::Vec3 values);
signals:
	void valueChanged(iris::Vec3 val);
	void nameChanged(QString name);
};


class Vector4DPropertyWidget : public BasePropertyWidget
{
	Q_OBJECT
public:
	Vector4DPropertyWidget();
	~Vector4DPropertyWidget();
	void setProp(Vec4Property *prop);

private:
	Vec4Property *prop;
	double x;
	double y;
	double z;
	double w;
	iris::Vec4 value;
	Widget4D *wid;

	void setConnections();
protected:

public slots:
	void setPropValues(iris::Vec4 values);
signals:
	void valueChanged(iris::Vec4 val);	void nameChanged(QString name);

};

