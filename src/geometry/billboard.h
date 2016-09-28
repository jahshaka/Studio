/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef BILLBOARD_H
#define BILLBOARD_H

#include <QAttribute>
#include <QBuffer>
#include <QGeometry>
#include <QGeometryRenderer>
#include <QBuffer>
#include <QBufferDataGenerator>

using namespace Qt3DRender;

class BillboardGeometry:public QGeometry
{
public:
    QAttribute* posAttr;
    QAttribute* uvAttr;
    QAttribute* indexAttr;
    QBuffer* vertexBuffer;
    QBuffer* indexBuffer;

    //std::vector<QVector3D> points;

    BillboardGeometry();
};

class Billboard:public QGeometryRenderer
{
    BillboardGeometry* geom;
public:
    Billboard()
    {
        geom = new BillboardGeometry();
        this->setGeometry(geom);
        this->setPrimitiveType(QGeometryRenderer::Triangles);
    }
};

#endif // BILLBOARD_H
