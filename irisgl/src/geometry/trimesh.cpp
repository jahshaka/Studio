/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "trimesh.h"

namespace iris
{

/**
 * Adds points for triangle. Assumes points are in a counter-clockwise rotation.
 * @param a
 * @param b
 * @param c
 */
void TriMesh::addTriangle(const QVector3D& a,const QVector3D& b,const QVector3D& c)
{
    Triangle tri = {a,b,c,QVector3D::crossProduct(b-a,c-a)};

    triangles.append(tri);
}

//https://github.com/qt/qt3d/blob/5476bc6b4b6a12c921da502c24c4e078b04dd3b3/src/render/jobs/pickboundingvolumejob.cpp
//realtime rendering page 192
//no need to get uvw, just return true at the first sign of a hit
bool TriMesh::isHitBySegment(const QVector3D& segmentStart,const QVector3D& segmentEnd,QVector3D& hitPoint)
{
    for(auto tri:triangles)
    {
        auto ab = tri.b - tri.a;
        auto ac = tri.c - tri.a;
        auto qp = segmentStart-segmentEnd;

        //auto normal = tri.normal;
        auto normal = QVector3D::crossProduct(ab, ac);
        float d = QVector3D::dotProduct(qp, normal);

        //if (d <= 0)
        //    continue;
        if (d == 0) continue;

        auto ap = segmentStart - tri.a;
        auto t = QVector3D::dotProduct(ap, normal);

        if (t < 0 || t > d)
            continue;

        auto e = QVector3D::crossProduct(qp, ap);
        auto v = QVector3D::dotProduct(ac, e);

        if (v < 0.0f || v > d)
            continue;

        auto w = -QVector3D::dotProduct(ab, e);

        if (w < 0.0f || v + w > d)
            continue;

        t /= d;

        //all conditions have been met
        //todo: fix please. return t instead
        hitPoint = segmentStart + (segmentEnd-segmentStart)*t;//t is in range 0 and 1 and denotes how far along the distance the hit is
        return true;
    }

    return false;
}

/**
 * Does a segment-mesh intersection test
 * Returns number of intersections
 * @return
 */
int TriMesh::getSegmentIntersections(const QVector3D& segmentStart,const QVector3D& segmentEnd,QList<TriangleIntersectionResult>& results)
{
    int hits = 0;
    for(auto i=0;i<triangles.size();i++)
    {
        //auto tri = triangles[i];
        const Triangle& tri = triangles[i];
        auto ab = tri.b - tri.a;
        auto ac = tri.c - tri.a;
        auto qp = segmentStart-segmentEnd;

        //auto normal = tri.normal;
        auto normal = QVector3D::crossProduct(ab, ac);
        float d = QVector3D::dotProduct(qp, normal);

        if (d <= 0)
            continue;

        auto ap = segmentStart - tri.a;
        auto t = QVector3D::dotProduct(ap, normal);

        if (t < 0 || t > d)
            continue;

        auto e = QVector3D::crossProduct(qp, ap);
        auto v = QVector3D::dotProduct(ac, e);

        if (v < 0 || v > d)
            continue;

        auto w = -QVector3D::dotProduct(ab, e);

        if (w < 0.0f || v + w > d)
            continue;

        t /= d;

        //all conditions have been met
        hits++;

        auto dist = segmentStart.distanceToPoint(segmentEnd);
        auto hitPoint = segmentStart + (segmentEnd-segmentStart)*t;

        TriangleIntersectionResult result;
        result.triangleIndex = i;
        result.hitPoint = hitPoint;
        result.t = t;
        //result.hitPoint = tri.a*u + tri.b*v + tri.c*w;
        results.append(result);
        hits++;
    }

    return hits;
}

}
