#ifndef LINEMESHBUILDER_H
#define LINEMESHBUILDER_H

#include <QVector>
#include <QVector3D>
#include <QVector4D>
#include "irisglfwd.h"

namespace iris {

class LineMeshBuilder
{
	struct LineData
	{
		QVector3D pos;
		QVector4D color;
	};

    QVector<LineData> lineData;

public:
    void addLine(QVector3D a, QVector3D b);
	void addLine(QVector3D a, QColor aCol, QVector3D b, QColor bCol);
    MeshPtr build();
};

}

#endif // LINEMESHBUILDER_H
