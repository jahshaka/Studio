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

	QVector3D getMin() { return minPos; }
	QVector3D getMax() { return maxPos; }

	QVector3D getCenter() const;
	QVector3D getSize() const;
	QVector3D getHalfSize() const;

	void offset(QVector3D offset);

	void merge(const QVector3D& point);
	void merge(const QVector<QVector3D>& points);
	void merge(const AABB& aabb);

	BoundingSphere getMinimalEnclosingSphere() const;

	static AABB fromPoints(const QVector<QVector3D>& points);

private:
	QVector3D getMin(const QVector3D& a, const QVector3D& b) const;
	QVector3D getMax(const QVector3D& a, const QVector3D& b) const;
};

}