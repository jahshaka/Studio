#include "aabb.h"
#include <limits>

AABB::AABB()
{
	setNegativeInfinity();
}


void AABB::setNegativeInfinity()
{
	minPos = QVector3D(std::numeric_limits<float>::max(),
					   std::numeric_limits<float>::max(),
					   std::numeric_limits<float>::max());

	maxPos = QVector3D(-std::numeric_limits<float>::max(),
					   -std::numeric_limits<float>::max(),
					   -std::numeric_limits<float>::max());
}

QVector3D AABB::getMin(QVector3D a, QVector3D b)
{
	QVector3D result;

	result.setX(a.x() < b.x() ? a.x() : b.x());
	result.setY(a.y() < b.y() ? a.y() : b.y());
	result.setZ(a.z() < b.z() ? a.z() : b.z());

	return result;
}

QVector3D AABB::getMax(QVector3D a, QVector3D b)
{
	QVector3D result;

	result.setX(a.x() > b.x() ? a.x() : b.x());
	result.setY(a.y() > b.y() ? a.y() : b.y());
	result.setZ(a.z() > b.z() ? a.z() : b.z());

	return result;
}

void AABB::merge(QVector3D point)
{
	minPos = getMin(minPos, point);
	maxPos = getMin(maxPos, point);
}

void AABB::merge(QVector<QVector3D> points)
{
	for (auto &p : points) {
		merge(p);
	}
}