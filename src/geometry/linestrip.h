/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

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

using namespace Qt3DRender;

class LineStripGeometry:public QGeometry
{
public:
    QAttribute* posAttr;
    QAttribute* indexAttr;
    QBuffer* vertexBuffer;
    QBuffer* indexBuffer;

    std::vector<QVector3D> points;

    LineStripGeometry();

    void setPoints(std::vector<QVector3D> points);
};

class LineStrip:public QGeometryRenderer
{
    QScopedPointer<LineStripGeometry> geom;
public:
    LineStrip():
        geom(new LineStripGeometry())
    {
        this->setGeometry(geom.data());
        this->setPrimitiveType(QGeometryRenderer::LineStrip);
    }

    void setPoints(std::vector<QVector3D> points)
    {
        geom->setPoints(points);


    }
};

#endif // LINESTRIP_H
