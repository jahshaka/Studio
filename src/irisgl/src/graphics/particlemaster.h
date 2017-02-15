#ifndef PARTICLEMASTER_H
#define PARTICLEMASTER_H

#include <iostream>
#include "particle.h"
#include "particlerender.h"

#include "renderdata.h"

namespace iris {
class ParticleMaster {
private:
    std::list<Particle*> particles;
    bool dissipate;

public:
    ParticleRenderer renderer;
    ParticleMaster(){

    }

    void setDissipate(bool d) {
        dissipate = d;
    }

    void setBlendMode(bool ubm) {
        renderer.useAdditive = ubm;
    }

    void update(float delta) {
        /*
        std::list<Particle*>::iterator it = particles.begin();

        while (it != particles.end()) {
            bool alive = (*it)->update(delta);

            // in the future we can call behavior management here, add more forces such as wind
            if (dissipate) {
                (*it)->dissipate();
            }

            if (!alive) {
                it = particles.erase(it++);
            }

            ++it;
        }
        */
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

}

#endif // PARTICLEMASTER_H
