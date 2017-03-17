/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef PARTICLERENDER_H
#define PARTICLERENDER_H

#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLContext>
#include "particle.h"
#include "renderdata.h"
#include "texture2d.h"
#include "../core/irisutils.h"

namespace iris {

class ParticleRenderer {

private:
    GLuint quadVAO, quadVBO;
    QOpenGLFunctions_3_2_Core* gl;

public:
    bool useAdditive;

    QSharedPointer<iris::Texture2D> icon;
    ParticleRenderer() {
        this->gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();

        GLfloat quadVertices[] = {
            -1.f,  1.f, 0.f,    0.0f, 1.0f,
            -1.f, -1.f, 0.f,    0.0f, 0.0f,
             1.f,  1.f, 0.f,    1.0f, 1.0f,
             1.f, -1.f, 0.f,    1.0f, 0.0f,
        };

        gl->glGenVertexArrays(1, &quadVAO);
        gl->glGenBuffers(1, &quadVBO);
        gl->glBindVertexArray(quadVAO);
        gl->glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        gl->glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        gl->glEnableVertexAttribArray(0);
        gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), nullptr);
        gl->glEnableVertexAttribArray(1);
        gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                                  5 * sizeof(GLfloat),
                                  (GLvoid*) (3 * sizeof(GLfloat)));
        gl->glBindVertexArray(0);

        useAdditive = true;
    }

    void updateModelViewMatrix(QOpenGLShaderProgram *shader,
                               QVector3D pos,
                               float rot,
                               float scale,
                               QMatrix4x4 viewMatrix)
    {
        QMatrix4x4 modelMatrix;
        modelMatrix.translate(pos);

        modelMatrix(0, 0) = viewMatrix(0, 0);
        modelMatrix(0, 1) = viewMatrix(1, 0);
        modelMatrix(0, 2) = viewMatrix(2, 0);

        modelMatrix(1, 0) = viewMatrix(0, 1);
        modelMatrix(1, 1) = viewMatrix(1, 1);
        modelMatrix(1, 2) = viewMatrix(2, 1);

        modelMatrix(2, 0) = viewMatrix(0, 2);
        modelMatrix(2, 1) = viewMatrix(1, 2);
        modelMatrix(2, 2) = viewMatrix(2, 2);

        modelMatrix.rotate((float) rot, QVector3D(0, 0, 1));
        modelMatrix.scale(scale, scale, scale);

        QMatrix4x4 modelViewMatrix = viewMatrix * modelMatrix;
        shader->setUniformValue("modelViewMatrix", modelViewMatrix);
    }

    void setIcon(QSharedPointer<iris::Texture2D> icon) {
        this->icon = icon;
    }

    void render(QOpenGLShaderProgram *shader,
                iris::RenderData* renderData,
                std::list<Particle*>& particles)
    {
        shader->bind();

        shader->setUniformValue("projectionMatrix", renderData->projMatrix);
        QMatrix4x4 viewMatrix = renderData->viewMatrix;

        gl->glBindVertexArray(quadVAO);
        gl->glEnable(GL_BLEND);

        if (useAdditive) {
            gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        } else {
            gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        gl->glDepthMask(GL_FALSE);

        for (auto particle : particles) {
            updateModelViewMatrix(
                        shader,
                        particle->getPosition(),
                        particle->getRotation(),
                        particle->getScale(),
                        viewMatrix
                    );

            if (!!icon) {
                gl->glActiveTexture(GL_TEXTURE0);
                icon->texture->bind();
            }
            gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        gl->glDepthMask(GL_TRUE);
        gl->glDisable(GL_BLEND);
        gl->glBindVertexArray(0);

        shader->release();
    }
};

}
#endif // PARTICLERENDER_H
