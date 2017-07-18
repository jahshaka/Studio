#include "shapehelper.h"
#include "linemeshbuilder.h"
#include <qmath.h>

namespace iris
{

MeshPtr ShapeHelper::createWireCube(float size)
{
    auto halfSize = size / 2;
    LineMeshBuilder builder;

    //builder.addLine(QVector3D(halfSize, halfSize, halfSize), QVector3D(halfSize, halfSize, halfSize));
    // build 4 columns
    builder.addLine(QVector3D(-halfSize, halfSize, -halfSize), QVector3D(-halfSize, halfSize, halfSize));
    builder.addLine(QVector3D(-halfSize, -halfSize, -halfSize), QVector3D(-halfSize, -halfSize, halfSize));
    builder.addLine(QVector3D(halfSize, -halfSize, -halfSize), QVector3D(halfSize, -halfSize, halfSize));
    builder.addLine(QVector3D(halfSize, halfSize, -halfSize), QVector3D(halfSize, halfSize, halfSize));

    // top
    builder.addLine(QVector3D(-halfSize, halfSize, halfSize), QVector3D(-halfSize, -halfSize, halfSize));
    builder.addLine(QVector3D(-halfSize, -halfSize, halfSize), QVector3D(halfSize, -halfSize, halfSize));
    builder.addLine(QVector3D(halfSize, -halfSize, halfSize), QVector3D(halfSize, halfSize, halfSize));
    builder.addLine(QVector3D(halfSize, halfSize, halfSize), QVector3D(-halfSize, halfSize, halfSize));

    // bottom
    builder.addLine(QVector3D(-halfSize, halfSize, -halfSize), QVector3D(-halfSize, -halfSize, -halfSize));
    builder.addLine(QVector3D(-halfSize, -halfSize, -halfSize), QVector3D(halfSize, -halfSize, -halfSize));
    builder.addLine(QVector3D(halfSize, -halfSize, -halfSize), QVector3D(halfSize, halfSize, -halfSize));
    builder.addLine(QVector3D(halfSize, halfSize, -halfSize), QVector3D(-halfSize, halfSize, -halfSize));

    return builder.build();
}

MeshPtr ShapeHelper::createWireSphere(float radius)
{
    LineMeshBuilder builder;


    int divisions = 36;
    float arcWidth = 360.0f/divisions;

    // XY plane
    for(int i=0;i<divisions;i++)
    {
        float angle = i * arcWidth;
        QVector3D a = QVector3D(qSin(qDegreesToRadians(angle)), qCos(qDegreesToRadians(angle)), 0) * radius;

        angle = (i+1) * arcWidth;
        QVector3D b = QVector3D(qSin(qDegreesToRadians(angle)), qCos(qDegreesToRadians(angle)), 0) * radius;

        builder.addLine(a, b);
    }

    // XZ plane
    for(int i=0;i<divisions;i++)
    {
        float angle = i * arcWidth;
        QVector3D a = QVector3D(qSin(qDegreesToRadians(angle)), 0, qCos(qDegreesToRadians(angle))) * radius;

        angle = (i+1) * arcWidth;
        QVector3D b = QVector3D(qSin(qDegreesToRadians(angle)), 0, qCos(qDegreesToRadians(angle))) * radius;

        builder.addLine(a, b);
    }

    // YZ plane
    for(int i=0;i<divisions;i++)
    {
        float angle = i * arcWidth;
        QVector3D a = QVector3D(0, qSin(qDegreesToRadians(angle)), qCos(qDegreesToRadians(angle))) * radius;

        angle = (i+1) * arcWidth;
        QVector3D b = QVector3D(0, qSin(qDegreesToRadians(angle)), qCos(qDegreesToRadians(angle))) * radius;

        builder.addLine(a, b);
    }

    return builder.build();
}

}
