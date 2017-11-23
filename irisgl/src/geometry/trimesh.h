/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TRIMESH_H
#define TRIMESH_H

#include <QVector3D>
#include <QList>

namespace iris
{

struct TriangleIntersectionResult
{
    int triangleIndex;
    QVector3D hitPoint;
    float t;//distance along length of the segment

    TriangleIntersectionResult()
    {
        triangleIndex = -1;
    }
};

class Triangle
{
public:
    //triangle's points in counter-clockwise order
    QVector3D a,b,c;
    QVector3D normal;
};


/**
 * This class defines a mesh using triangles. It's used for ray-casting and intersection tests
 */
class TriMesh
{
public:
    QList<Triangle> triangles;


    /**
     * Adds points for triangle. Assumes points are in a counter-clockwise rotation.
     * @param a
     * @param b
     * @param c
     */
    void addTriangle(const QVector3D& a, const QVector3D& b, const QVector3D& c);

    //https://github.com/qt/qt3d/blob/5476bc6b4b6a12c921da502c24c4e078b04dd3b3/src/render/jobs/pickboundingvolumejob.cpp
    //realtime rendering page 192
    //no need to get uvw, just return true at the first sign of a hit
    bool isHitBySegment(const QVector3D& segmentStart, const QVector3D& segmentEnd, QVector3D& hitPoint);

    /**
     * Does a segment-mesh intersection test
     * Returns number of intersections
     * @return
     */
    int getSegmentIntersections(const QVector3D& segmentStart, const QVector3D& segmentEnd, QList<TriangleIntersectionResult>& results);


};


}

#endif // TRIMESH_H
