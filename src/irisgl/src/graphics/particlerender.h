#ifndef PARTICLERENDER_H
#define PARTICLERENDER_H

#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions_3_2_Core>
#include "particle.h"
#include "renderdata.h"

class ParticleRenderer {

private:
    GLuint quadVAO, quadVBO;
    QOpenGLFunctions_3_2_Core* gl;

public:
    ParticleRenderer(QOpenGLFunctions_3_2_Core* gl) {
        this->gl = gl;

        GLfloat quadVertices[] = {
            // Positions        // Texture Coords
            -0.5f,  0.5f, 0.0f,  0.0f, 1.0f,
            -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
             0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
             0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
        };

        // Setup plane VAO
        gl->glGenVertexArrays(1, &quadVAO);
        gl->glGenBuffers(1, &quadVBO);
        gl->glBindVertexArray(quadVAO);
        gl->glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        gl->glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        gl->glEnableVertexAttribArray(0);
        gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), nullptr);
    }

    void prepare() {
        gl->glBindVertexArray(quadVAO);
        gl->glEnable(GL_BLEND);
        gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl->glDepthMask(GL_FALSE);
    }

    void updateModelViewMatrix(QOpenGLShaderProgram *shader,
                               QVector3D pos,
                               float rot,
                               float scale,
                               QMatrix4x4 viewMatrix)
    {
        QMatrix4x4 modelMatrix;
        modelMatrix.translate(pos);

//        modelMatrix.column(0).toVector3D() [0] = viewMatrix.column(0)[0];
//        modelMatrix.column(0)[1] = viewMatrix.column(1)[0];
//        modelMatrix.column(0)[2] = viewMatrix.column(0)[0];

//        modelMatrix[0][0] = viewMatrix[0][0];
//        modelMatrix[0][1] = viewMatrix[1][0];
//        modelMatrix[0][2] = viewMatrix[2][0];

//        modelMatrix[1][0] = viewMatrix[0][1];
//        modelMatrix[1][1] = viewMatrix[1][1];
//        modelMatrix[1][2] = viewMatrix[2][1];

//        modelMatrix[2][0] = viewMatrix[0][2];
//        modelMatrix[2][1] = viewMatrix[1][2];
//        modelMatrix[2][2] = viewMatrix[2][2];

        modelMatrix.rotate((float) rot, QVector3D(0, 0, 1));
        modelMatrix.scale(scale, scale, scale);

        QMatrix4x4 modelViewMatrix = viewMatrix * modelMatrix;
        shader->setUniformValue("modelViewMatrix", modelViewMatrix);
    }

    void render(QOpenGLShaderProgram *shader,
                iris::RenderData* renderData,
                std::vector<Particle*> particles)
    {
        shader->bind();

        shader->setUniformValue("projectionMatrix", renderData->projMatrix);
        QMatrix4x4 viewMatrix = renderData->viewMatrix;

        prepare();

        for (int i = 0; i < particles.size(); i++) {
            updateModelViewMatrix(  shader,
                                    particles[i]->getPosition(),
                                    particles[i]->getRotation(),
                                    particles[i]->getScale(),
                                    viewMatrix
                                );
            gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        // unprepare
        gl->glDepthMask(GL_TRUE);
        gl->glDisable(GL_BLEND);
        gl->glBindVertexArray(0);
    }
};

#endif // PARTICLERENDER_H
