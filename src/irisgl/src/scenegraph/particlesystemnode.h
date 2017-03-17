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

class QOpenGLShaderProgram;

namespace iris
{

class RenderItem;
class Particle;
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
    iris::Texture2DPtr texture;

    bool dissipate, dissipateInv;
    bool randomRotation;

    float lifeFactor;
    float scaleFactor;
    float speedFactor;
    bool useAdditive;

    float gravityComplement;
    float lifeLength;
    float particleScale;

    int maxParticles;
    float billboardScale;

    float speedError, lifeError, scaleError;
    QVector3D direction;

    QMatrix4x4 posDir;
    QVector3D boundDimension;

    void setRandomRotation(bool val) {
        randomRotation = val;
    }

    void setBlendMode(bool useAddittive);

    void setDissipation(bool b) {
        this->dissipate = b;
    }

    void setParticleScale(float scale) {
        this->particleScale = scale;
    }

    void setDissipationInv(bool b) {
        this->dissipateInv = b;
    }

    void setVolumeSquare(QVector3D dim) {
        this->boundDimension = dim;
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

    void setGravity(float gravityComplement) {
        this->gravityComplement = gravityComplement;
    }

    float getGravity() {
        return this->gravityComplement;
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

    void setBillboardScale(float scale);

    void update(float delta) override;

    void renderParticles(RenderData* renderData, QOpenGLShaderProgram* shader);

    void addParticle(Particle *particle) {
        particles.push_back(particle);
    }

    ~ParticleSystemNode();

    ParticleRenderer* renderer;

private:
    ParticleSystemNode();

    std::list<Particle*> particles;

    MaterialPtr material;
    RenderItem* renderItem;
    Mesh* boundsMesh;
    RenderItem* boundsRenderItem;
};

}

#endif // PARTICLESYSTEMNODE_H
