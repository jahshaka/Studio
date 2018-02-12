#ifndef TRANSFORM_H
#define TRANSFORM_H

#include <QVector3D>
#include <QMatrix4x4>

namespace iris
{

class Transform
{
public:
    QVector3D pos;
    QQuaternion rot;
    QVector3D scale;

    QMatrix4x4 toMatrix()
    {
        QMatrix4x4 matrix;
        matrix.setToIdentity();
        return matrix;
    }
};

}

#endif // TRANSFORM_H
