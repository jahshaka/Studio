#ifndef LINEMESHBUILDER_H
#define LINEMESHBUILDER_H

#include <QVector>
#include <QVector3D>
#include "../irisglfwd.h"

namespace iris {

class LineMeshBuilder
{
    QVector<QVector3D> lineData;

public:
    void addLine(QVector3D a, QVector3D b);
    MeshPtr build();
};

}

#endif // LINEMESHBUILDER_H
