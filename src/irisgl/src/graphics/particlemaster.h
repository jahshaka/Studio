#ifndef PARTICLEMASTER_H
#define PARTICLEMASTER_H

#include "particle.h"
#include "particlerender.h"

#include "renderdata.h"

class ParticleMaster {
private:
    std::vector<Particle*> particles;
    ParticleRenderer renderer;

public:
    ParticleMaster(QOpenGLFunctions_3_2_Core* gl) : renderer(gl) {

    }

    void update(float delta) {
        for (int i = 0; i < particles.size(); i++) {
            particles[i]->update(delta);
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
