#pragma once

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
	QVector2D value;
	Widget2D *wid;

	void setConnections();

protected:

public slots:
	void setPropValues(QVector2D values);
signals:
	void valueChanged(QVector2D val);
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
	QVector3D value;
	Widget3D *wid;

	void setConnections();

protected:

public slots:
	void setPropValues(QVector3D values);
signals:
	void valueChanged(QVector3D val);
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
	QVector4D value;
	Widget4D *wid;

	void setConnections();
protected:

public slots:
	void setPropValues(QVector4D values);
signals:
	void valueChanged(QVector4D val);	void nameChanged(QString name);

};

