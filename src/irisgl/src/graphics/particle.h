#ifndef PARTICLE_H
#define PARTICLE_H

#include <QVector3D>
#include <QDebug>

class Particle {

public:
    QVector3D position;
    QVector3D velocity;

    float gravityEffect;
    float lifeLength;
    float rotation;
    float scale;

    float elapsedTime = 0;
    float GRAVITY = -50;

    Particle(QVector3D pos, QVector3D vel, float gr, float ll, float rot, float scl) {
        position    = pos;
        velocity    = vel;
        gravityEffect = gr;
        lifeLength  = ll;
        rotation    = rot;
        scale       = scl;
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

    float getLife() {
        return lifeLength - elapsedTime;
    }
};

#endif // PARTICLE_H
