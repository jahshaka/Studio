#ifndef PARTICLEMASTER_H
#define PARTICLEMASTER_H

#include <iostream>
#include "particle.h"
#include "particlerender.h"

#include "renderdata.h"

class ParticleMaster {
private:
    std::list<Particle*> particles;
    ParticleRenderer renderer;

public:
    ParticleMaster(QOpenGLFunctions_3_2_Core* gl) : renderer(gl) {

    }

    void update(float delta) {
        std::list<Particle*>::iterator it = particles.begin();

        while (it != particles.end()) {
            bool alive = (*it)->update(delta);

            if (!alive) {
                it = particles.erase(it++);
            }

            ++it;
        }
    }

    void renderParticles(QOpenGLFunctions_3_2_Core* gl,
                         QOpenGLShaderProgram *shader,
                         iris::RenderData *renderData)
    {
        renderer.render(shader, renderData, particles);
    }

    void addParticle(Particle *particle) {
        particles.push_back(particle);
    }
};

#endif // PARTICLEMASTER_H
