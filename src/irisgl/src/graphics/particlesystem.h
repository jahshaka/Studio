#ifndef PARTICLESYSTEM_H
#define PARTICLESYSTEM_H

#include <QVector3D>

#include "particlemaster.h"

class ParticleSystem {

    float pps;
    float speed;
    float gravityComplient;
    float lifeLength;

public:
    ParticleMaster pm;

    int randInRange(int min, int max)
    {
      return min + (int) (rand() / (double) (RAND_MAX + 1) * (max - min + 1));
    }

    ParticleSystem(QOpenGLFunctions_3_2_Core* gl,
                   float pps,
                   float speed,
                   float gravityComplient,
                   float lifeLength)
        : pm(gl)
    {
        this->pps = pps;
        this->speed = speed;
        this->gravityComplient = gravityComplient;
        this->lifeLength = lifeLength;
    }

    void generateParticles(QVector3D systemCenter, float delta){
        float particlesToCreate = pps * delta;
        int count = (int) floor(particlesToCreate);
        float partialParticle = (int) particlesToCreate % 1;

        for(int i=0;i<count;i++){
            emitParticle(systemCenter);
        }

        float rand_ = randInRange(-1, 2);
        if (rand_ < partialParticle){
            emitParticle(systemCenter);
        }
    }

    void emitParticle(QVector3D center){
        float dirX = (float) randInRange(-1, 2) * 2.f - 1.f;
        float dirZ = (float) randInRange(-1, 2) * 2.f - 1.f;
        QVector3D velocity = QVector3D(dirX, 1, dirZ);
        velocity.normalize();
        velocity *= speed;
        //new Particles(QVector3D(center), velocity, gravityComplient, lifeLength, 0, 1);
        pm.addParticle(new Particle(QVector3D(center), velocity, gravityComplient, lifeLength, 0, 1));
    }
};

#endif // PARTICLESYSTEM_H
