#ifndef PARTICLE_H
#define PARTICLE_H

#include <QVector3D>
#include <QDebug>

class Particle {

private:
    QVector3D position;
    QVector3D velocity;

    float gravityEffect;
    float lifeLength;
    float rotation;
    float scale;

    float elapsedTime = 0;
    float GRAVITY = -50;

public:
    Particle(QVector3D p, QVector3D v, float g, float ll, float r, float s) {
        position = p;
        velocity = v;
        gravityEffect =  g;
        lifeLength = ll;
        rotation = r;
        scale = s;
    }

    float getRotation() {
        return rotation;
    }

    float getScale() {
        return scale;
    }

    QVector3D getPosition() {
        return position;
    }

    bool update(float delta) {
        // @TODO -- remove if too old
        float cit = GRAVITY * gravityEffect * delta;
        velocity += QVector3D(0, cit, 0);
        QVector3D change = velocity;
        change *= delta;
        position += change;
        elapsedTime += delta;

        qDebug() << change << endl;

        return elapsedTime < lifeLength;
    }
};

#endif // PARTICLE_H
