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
    QMatrix4x4 posDir;
    QVector3D dim;

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

        directionDeviation = speedError = lifeError = scaleError = 0;

        this->scale = scale;

        qDebug() << "particle system created!";
    }

    void setRandomRotation(bool val) {
        randomRotation = val;
    }

    void setBlendMode(bool useAddittive) {
        pm.setBlendMode(useAddittive);
    }

    void dissipate(bool b) {
        pm.setDissipate(b);
    }

    void setVolumeSquare(QVector3D dim) {
        this->dim = dim;
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

    void setDirection(QMatrix4x4 direction) {
        this->posDir = direction;
//        this->direction = QVector3D(direction);
//        this->direction.normalize();
//        this->directionDeviation = (float) (deviation * M_PI);
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
        //float dirX = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.f - 1.f;
        //float dirZ = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.f - 1.f;
//        QVector3D velocity = QVector3D(0, 1, 0);
        QVector4D dir = QVector4D(QVector3D(0, 1, 0), 0);
        QVector4D particleDirection = posDir * dir;

        // use a volume cube
        QVector3D velocity = QVector3D(particleDirection);
//        QVector3D velocity = generateRandomUnitVector();
//        QVector3D velocity = generateRandomUnitVectorWithinCone(direction, directionDeviation);

        velocity.normalize();
        velocity *= generateValue(speed, speedError);
        float scl = generateValue(scale, scaleError);
        float ll = generateValue(lifeLength, lifeError);

        // reuse particles! this is bad.
        // change later after atlases
        pm.renderer.icon = texture;
        pm.addParticle(new Particle(pos + dim * generateRandomUnitVector(),
                                    velocity,
                                    gravityComplient,
                                    ll,
                                    generateRotation(),
                                    scl));
    }

    float generateValue(float average, float errorMargin) {
        float offset = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.f * errorMargin;
        return average + offset;
    }

    float generateRotation() {
        if (randomRotation) {
            return static_cast<float>(rand()) / static_cast<float>(RAND_MAX / 360.f);
        } else {
            return 0;
        }
    }

    QVector3D generateRandomUnitVector() {
        float theta = (float) (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.f * M_PI);
        float z = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.f) - 1.f;
        float rootOneMinusZSquared = (float) sqrt(1 - z * z);
        float x = (float) (rootOneMinusZSquared * cos(theta));
        float y = (float) (rootOneMinusZSquared * sin(theta));
        return QVector3D(x, y, z);
    }

    QVector3D generateRandomUnitVectorWithinCone(QVector3D coneDirection, float angle) {
        float cosAngle = (float) cos(angle);
        float theta = (float) (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.f * M_PI);
        float z = cosAngle + (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * (1.f - cosAngle));
        float rootOneMinusZSquared = (float) sqrt(1 - z * z);
        float x = (float) (rootOneMinusZSquared * cos(theta));
        float y = (float) (rootOneMinusZSquared * sin(theta));

        QVector4D direction = QVector4D(x, y, z, 1);
        if (coneDirection.x() != 0 || coneDirection.y() != 0 || (coneDirection.z() != 1 && coneDirection.z() != -1)) {
            QVector3D rotateAxis = QVector3D::crossProduct(coneDirection, QVector3D(0, 0, 1));
            rotateAxis.normalize();
            float rotateAngle = (float) acos(QVector3D::dotProduct(coneDirection, QVector3D(0, 0, 1)));
            QMatrix4x4 rotationMatrix;
            rotationMatrix.setToIdentity();
            rotationMatrix.rotate(-rotateAngle, rotateAxis);
            direction *= direction * rotationMatrix;
        } else if (coneDirection.z() == -1) {
            direction *= QVector4D(0, 0, -1, 1);
        }


        return QVector3D(direction);
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
