#include <QVector>
#include <QVector3D>

class AABB
{
	 QVector3D minPos;
	 QVector3D maxPos;

public:
	 AABB();

	 void setNegativeInfinity();
	 QVector3D getMin(QVector3D a, QVector3D b);
	 QVector3D getMax(QVector3D a, QVector3D b);

	 void merge(QVector3D point);
	 void merge(QVector<QVector3D> points);

	 static AABB fromPoints(QVector<QVector3D> points);
};