#include "aabb.h"
#include "boundingsphere.h"
#include <limits>

namespace iris
{

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

QVector3D AABB::getCenter()
{
	return (minPos + maxPos) * 0.5f;
}

QVector3D AABB::getSize()
{
	return QVector3D(maxPos.x() - minPos.x(),
		maxPos.y() - minPos.y(),
		maxPos.z() - minPos.z());
}

QVector3D AABB::getHalfSize()
{
	return getSize() * 0.5f;
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

BoundingSphere AABB::getMinimalEnclosingSphere()
{
	return { getCenter(), getSize().length() * 0.5f };
}

}