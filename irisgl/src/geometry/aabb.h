#pragma once
#include <QVector>
#include <QVector3D>
#include "boundingsphere.h"

namespace iris
{

class AABB
{
	QVector3D minPos;
	QVector3D maxPos;

public:
	AABB();

	void setNegativeInfinity();

	QVector3D getCenter();
	QVector3D getSize();
	QVector3D getHalfSize();

	void merge(QVector3D point);
	void merge(QVector<QVector3D> points);

	BoundingSphere getMinimalEnclosingSphere();

	static AABB fromPoints(QVector<QVector3D> points);

private:
	QVector3D getMin(QVector3D a, QVector3D b);
	QVector3D getMax(QVector3D a, QVector3D b);
};

}