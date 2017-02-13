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

    bool update(float delta) {
        velocity += QVector3D(0, GRAVITY * gravityEffect * delta, 0);
        QVector3D change = velocity;
        change *= delta;
        position += change;
        elapsedTime += delta;

        return elapsedTime < lifeLength;
    }

    // niche particle properties
    // dissipate particles over half their lifetime
    void dissipate() {
        if (lifeLength - elapsedTime > lifeLength/2) {
            scale *= 1.f - 1/lifeLength;
        }
    }
};

#endif // PARTICLE_H
