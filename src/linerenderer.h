/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef LINESTRIP_H
#define LINESTRIP_H

#include <QAttribute>
#include <QBuffer>
#include <QGeometry>
#include <QGeometryRenderer>
#include <QBuffer>
#include <QBufferDataGenerator>
#include <QVector3D>
#include <QColor>

using namespace Qt3DRender;

struct Line3D
{
    QVector3D point1;
    QVector3D point2;
    QColor color;
};


class LineGeometry:public QGeometry
{
public:
    QAttribute* posAttr;
    QAttribute* colAttr;
    QAttribute* indexAttr;
    QBuffer* vertexBuffer;
    QBuffer* indexBuffer;

    std::vector<Line3D> lines;

    explicit LineGeometry(std::vector<Line3D>* lines=nullptr);

    void setLines(std::vector<Line3D>* lines);
};


class LineRenderer:public QGeometryRenderer
{
    QScopedPointer<LineGeometry> geom;
public:
    LineRenderer(std::vector<Line3D>* lines=nullptr):
        geom(new LineGeometry(lines))
    {
        this->setGeometry(geom.data());
        this->setPrimitiveType(QGeometryRenderer::Lines);
    }
};

#endif // LINESTRIP_H
