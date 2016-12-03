#ifndef TRIMESH_H
#define TRIMESH_H

#include <QVector3D>
#include <QList>

namespace jah3d
{

struct TriangleIntersectionResult
{
    int triangleIndex;
    QVector3D hitPoint;

    TriangleIntersectionResult()
    {
        triangleIndex=-1;
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
    void addTriangle(const QVector3D& a,const QVector3D& b,const QVector3D& c)
    {
        Triangle tri = {a,b,c,QVector3D::crossProduct(b-a,c-a)};
        //Triangle tri = {c,b,a,QVector3D::crossProduct(b-c,a-c)};//clockwise
        //tri.normal = QVector3D::crossProduct(b-a,c-a);

        //Triangle tri = {c,b,a};//clockwise
        //tri.normal = QVector3D::crossProduct(c-a,b-a);


        triangles.append(tri);
    }

    //https://github.com/qt/qt3d/blob/5476bc6b4b6a12c921da502c24c4e078b04dd3b3/src/render/jobs/pickboundingvolumejob.cpp
    //realtime rendering page 192
    //no need to get uvw, just return true at the first sign of a hit
    bool isHitBySegment(const QVector3D& segmentStart,const QVector3D& segmentEnd,QVector3D& hitPoint)
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
            //todo: fix please
            hitPoint = segmentStart + (segmentEnd-segmentStart)*
                    (t*segmentStart.distanceToPoint(segmentEnd));//t is in range 0 and 1 and denotes how far along the distance the hit is
            return true;
        }

        return false;
    }

    /**
     * Does a segment-mesh intersection test
     * Returns number of intersections
     * @return
     */
    int getSegmentIntersections(const QVector3D& segmentStart,const QVector3D& segmentEnd,QList<TriangleIntersectionResult>& results)
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
            auto hitPoint = segmentStart + (segmentEnd-segmentStart)*
                    (t*segmentStart.distanceToPoint(segmentEnd));//t is in range 0 and 1 and denotes how far along the distance the hit is

            TriangleIntersectionResult result;
            result.triangleIndex = i;
            result.hitPoint = hitPoint;
            results.append(result);
            hits++;
        }

        return hits;
    }


};


}

#endif // TRIMESH_H
