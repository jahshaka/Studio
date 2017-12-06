#include "translationgizmo.h"
#include "irisgl/src/math/intersectionhelper.h"
#include "irisgl/src/math/mathhelper.h"

bool TranslationHandle::isHit(QVector3D rayPos, QVector3D rayDir)
{
    auto gizmoTrans = gizmo->getTransform();

    // calculate world space position of the segment representing the handle
    auto p1 = gizmoTrans * QVector3D(0,0,0);
    auto q1 = gizmoTrans * (handleExtent * handleLength * gizmo->getGizmoScale() * handleScale);

    auto p2 = rayPos;
    auto q2 = rayPos + rayDir * 1000;

    float s,t;
    QVector3D c1, c2;
    auto dist = iris::MathHelper::closestPointBetweenSegments(p1, q1, p2, q2, s,t,c1,c2);
    if (dist < handleScale * gizmo->getGizmoScale() * handleScale) {
        return true;
    }

    return false;
}

//todo: transform rayPos and rayDir to gizmo space first then back to world space
// The rayPos and rayDir are in world space, convert them to gizmo space to do calculations then calculate them back
// to world space when we're done
QVector3D TranslationHandle::getHitPos(QVector3D rayPos, QVector3D rayDir)
{
	bool hit = false;
	QVector3D finalHitPos;
	float closestDist = 10000000;

	// transform rayPos and rayDir to gizmo space
	auto gizmoTransform = gizmo->getTransform();
	auto worldToGizmo = gizmoTransform.inverted();
	rayPos = worldToGizmo * rayPos;
	rayDir = QQuaternion::fromRotationMatrix(worldToGizmo.normalMatrix()).rotatedVector(rayDir);

	// loop thru planes
	for (auto normal : planes) {
		float t;
		QVector3D hitPos;

		// flip normal so its facing the ray source
		if (QVector3D::dotProduct(normal, rayPos) < 0)
			normal = -normal;

		if (iris::IntersectionHelper::intersectSegmentPlane(rayPos, rayPos + rayDir * 10000000, iris::Plane(normal, 0), t, hitPos)) {
			// ignore planes at grazing angles
			if (qAbs(QVector3D::dotProduct(rayDir, normal)) < 0.1f)
				continue;
			
			auto hitResult = handleExtent * hitPos;

			// this isnt the first hit, but if it's the closest one then use it
			if (hit) {
				if (rayPos.distanceToPoint(hitResult) < closestDist) {
					finalHitPos = hitResult;
					closestDist = rayPos.distanceToPoint(hitResult);
				}
			}
			else {
				// first hit, just assign it
				finalHitPos = hitResult;
				closestDist = rayPos.distanceToPoint(hitResult);
			}

			hit = true;
		}
	}

	if (hit) {
		qDebug() << "hit pos: " << finalHitPos;
	}
	else {
		qDebug() << "no hit";
		// no hit so move to max distance in view direction
		float dominantExtent = iris::MathHelper::sign( QVector3D::dotProduct(rayDir.normalized(), handleExtent));// results in -1 or 1
		finalHitPos = dominantExtent * handleExtent * 10000;
	}

	// now convert it back to world space
	finalHitPos = gizmoTransform * finalHitPos;

	return finalHitPos;
}

/*
bool TranslationHandle::isHit(QVector3D rayPos, QVector3D rayDir)
{
	auto gizmoTrans = gizmo->getTransform();

	// calculate world space position of the segment representing the handle
	auto p1 = gizmoTrans * QVector3D(0, 0, 0);
	auto q1 = gizmoTrans * handleExtent;

	auto p2 = rayPos;
	auto q2 = rayPos + rayDir * 1000;

	float s, t;
	QVector3D c1, c2;
	auto dist = iris::MathHelper::closestPointBetweenSegments(p1, q1, p2, q2, s, t, c1, c2);
	if (dist < 0.1f) {
		return true;
	}

	return false;
}

QVector3D TranslationHandle::getHitPos(QVector3D rayPos, QVector3D rayDir)
{
	auto gizmoTrans = gizmo->getTransform();

	// calculate world space position of the segment representing the handle
	auto p1 = gizmoTrans * (-handleExtent * 10000);
	// extend the handle a lot further
	auto q1 = gizmoTrans * (handleExtent * 10000);

	auto p2 = rayPos;
	auto q2 = rayPos + rayDir * 1000;

	float s, t;
	QVector3D c1, c2;
	auto dist = iris::MathHelper::closestPointBetweenSegments(p1, q1, p2, q2, s, t, c1, c2);

	return c1;
}

*/