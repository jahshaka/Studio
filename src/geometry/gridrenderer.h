/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef GRID_H
#define GRID_H

#include <QGeometryRenderer>
#include "linerenderer.h"

class GridRenderer:public QGeometryRenderer
{
    QScopedPointer<LineGeometry> geom;
public:
    GridRenderer();
private:
    void buildGrid(float gridSize,float squareSize);
};

#endif // GRID_H
