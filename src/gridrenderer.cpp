/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QAttribute>
#include <QBuffer>
#include <QGeometry>
#include <QGeometryRenderer>
#include <QBuffer>
#include <QBufferDataGenerator>
#include <QVector3D>
#include <QColor>
#include "linerenderer.h"
#include "gridrenderer.h"

GridRenderer::GridRenderer()
{
    buildGrid(10000,5);
}

void GridRenderer::buildGrid(float gridSize,float squareSize)
{
    QColor white = QColor::fromRgb(255,255,255);
    std::vector<Line3D> lines;

    float halfGrid = gridSize/2;
    //horizontal row
    for(float x=-halfGrid;x<=halfGrid;x+=squareSize)
    {
        Line3D line;
        line.color = white;
        line.point1 = QVector3D(x,0,-halfGrid);
        line.point2 = QVector3D(x,0,halfGrid);

        lines.push_back(line);
    }

    //vertical rows
    for(float z=-halfGrid;z<=halfGrid;z+=squareSize)
    {
        Line3D line;
        line.color = white;
        line.point1 = QVector3D(-halfGrid,0,z);
        line.point2 = QVector3D(halfGrid,0,z);

        lines.push_back(line);
    }

    auto grid = new LineGeometry(&lines);
    this->setGeometry(grid);
    this->setPrimitiveType(QGeometryRenderer::Lines);
}
