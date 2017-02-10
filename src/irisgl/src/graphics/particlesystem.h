#ifndef PARTICLESYSTEM_H
#define PARTICLESYSTEM_H

#include <QVector3D>
#include "../graphics/texture2d.h"
#include "particlemaster.h"

class ParticleSystem {
    QVector3D pos;
    float pps;
    float speed;
    float gravityComplient;
    float lifeLength;
    float scale;

    bool randomRotation;
    float speedError, lifeError, scaleError;
    QVector3D direction;
    float directionDeviation;

    QSharedPointer<iris::Texture2D> texture;

public:
    ParticleMaster pm;

    ParticleSystem();
    ParticleSystem(QOpenGLFunctions_3_2_Core* gl,
                   QVector3D pos,
                   float pps,
                   float speed,
                   float gravityComplient,
                   float lifeLength,
                   float scale)
        : pm(gl)
    {
        this->pos = pos;
        this->pps = pps;
        this->speed = speed;
        this->gravityComplient = gravityComplient;
        this->lifeLength = lifeLength;

        randomRotation = true;
        directionDeviation = speedError = lifeError = scaleError = 0;

        this->scale = scale;
    }

    void setSpeedError(float error) {
        speedError = error * speed;
    }

    void setLifeError(float error) {
        lifeError = error * lifeLength;
    }

    void setScaleError(float error) {
        scaleError = error * scale;
    }

    void setDirection(QVector3D direction, float deviation) {
        this->direction = QVector3D(direction);
        this->directionDeviation = (float) (deviation * M_PI);
    }

    void generateParticles(float delta) {
        float particlesToCreate = pps * delta;
        int count = (int) floor(particlesToCreate);
        float partialParticle = fmod(particlesToCreate, 1);

        for (int i = 0; i < count; i++) {
            emitParticle();
        }

        if (static_cast <float> (rand()) / static_cast <float> (RAND_MAX) < partialParticle) {
            emitParticle();
        }
    }

    void emitParticle() {
        float dirX = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.f - 1.f;
        float dirZ = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.f - 1.f;
        QVector3D velocity = QVector3D(dirX, 1, dirZ);
        velocity.normalize();
        velocity *= speed;

        // reuse particles! this is bad.
        // change later after atlases
        pm.renderer.icon = texture;
        pm.addParticle(new Particle(pos,
                                    velocity,
                                    gravityComplient,
                                    lifeLength,
                                    generateRotation(),
                                    scale));
    }

    float generateRotation() {
        if (randomRotation) {
            return static_cast<float>(rand()) / static_cast<float>(RAND_MAX / 360.f);
        } else {
            return 0;
        }
    }

    void setPPS(float pps) {
        this->pps = pps;
    }

    float getPPS() {
        return this->pps;
    }

    void setGravity(float gravityComplient) {
        this->gravityComplient = gravityComplient;
    }

    float getGravity() {
        return this->gravityComplient;
    }

    void setLife(float ll) {
        this->lifeLength = ll;
    }

    float getLife() {
        return this->lifeLength;
    }

    void setSpeed(float speed) {
        this->speed = speed;
    }

    float getSpeed() {
        return this->speed;
    }

    QVector3D getPos() {
        return this->pos;
    }

    void setPos(QVector3D p) {
        this->pos = p;
    }

    void setTexture(QSharedPointer<iris::Texture2D> tex) {
        this->texture = tex;
    }
};

#endif // PARTICLESYSTEM_H
