/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef PARTICLESYSTEMNODE_H
#define PARTICLESYSTEMNODE_H

#include "../irisglfwd.h"
#include "../core/scenenode.h"
#include "../core/irisutils.h"
#include "../graphics/texture2d.h"
#include "../graphics/particle.h"

class QOpenGLShaderProgram;

namespace iris
{

class RenderItem;
class ParticleRenderer;

class ParticleSystemNode : public SceneNode
{
public:
    static ParticleSystemNodePtr create() {
        return ParticleSystemNodePtr(new ParticleSystemNode());
    }

    virtual void submitRenderItems() override;

    float particlesPerSecond;
    float speed;
    float gravity;
    iris::Texture2DPtr texture;

    bool dissipate;
    bool randomRotation;

    float lifeFac;
    float scaleFac;
    bool useAdditive;
    float speedFac;

    float gravityComplient;
    float lifeLength;
    float particleScale;

    int maxParticles;

    float speedError, lifeError, scaleError;
    QVector3D direction;
    float directionDeviation;

    QMatrix4x4 posDir;
    QVector3D dim;

    void setRandomRotation(bool val) {
        randomRotation = val;
    }

    void setBlendMode(bool useAddittive);

    void setDissipation(bool b) {
        this->dissipate = b;
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
        scaleError = error * particleScale;
    }

    void setDirection(QMatrix4x4 direction) {
        this->posDir = direction;
    }

    void generateParticles(float delta);
    void emitParticle();

    float generateValue(float average, float errorMargin);
    float generateRotation();

    QVector3D generateRandomUnitVector();

    void setPPS(float pps) {
        this->particlesPerSecond = pps;
    }

    float getPPS() {
        return this->particlesPerSecond;
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

    void update(float delta) override;
    void renderParticles(RenderData* renderData, QOpenGLShaderProgram* shader);

    void addParticle(Particle *particle) {
        particles.push_back(particle);
    }

    ~ParticleSystemNode();

    ParticleRenderer* renderer;

private:
    MaterialPtr material;

    RenderItem* renderItem;

    Mesh* boundsMesh;
    RenderItem* boundsRenderItem;

    std::list<Particle*> particles;


    ParticleSystemNode();

};

}

#endif // PARTICLESYSTEMNODE_H
