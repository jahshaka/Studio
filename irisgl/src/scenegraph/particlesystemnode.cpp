/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QFileInfo>
#include <QDir>

#include <cmath>
#ifndef M_PI
	#define M_PI 3.14159265358979323846
#endif

#include "../math/mathhelper.h"
#include "meshnode.h"
#include "particlesystemnode.h"
#include "../graphics/mesh.h"

#include "../graphics/vertexlayout.h"
#include "../materials/defaultmaterial.h"
#include "../materials/materialhelper.h"
#include "../graphics/renderitem.h"
#include "../graphics/particle.h"
#include "../graphics/particlerender.h"
#include "../graphics/renderlist.h"

#include "../scenegraph/scene.h"
#include "../scenegraph/scenenode.h"

namespace iris
{

ParticleSystemNode::ParticleSystemNode() {
    //mesh = nullptr;
    sceneNodeType = SceneNodeType::ParticleSystem;

    texture = iris::Texture2D::load(":assets/textures/default_particle.jpg");

    particlesPerSecond = 24;
    speed = 12;

    scaleFactor = 0.0f;
    lifeFactor = 0.0f;
    speedFactor = 0.0f;

    billboardScale = 1.f;

    useAdditive = true;
    randomRotation = true;
    dissipate = true;
    dissipateInv = false;

    gravityComplement = 0.0f;
    particleScale = 1.0f;
    lifeLength = 1.0f;

	exportable = true;

    speedError = lifeError = scaleError = 0;

    renderer = new ParticleRenderer();

    renderItem = new RenderItem();
    renderItem->type = RenderItemType::ParticleSystem;

    boundsRenderItem = new RenderItem();
    boundsRenderItem->mesh = Mesh::loadMesh(":assets/models/cube.obj");

    auto mat = DefaultMaterial::create();
    mat->setDiffuseColor(QColor(255, 255, 255));
    mat->setAmbientColor(QColor(255, 255, 255));
    boundsRenderItem->material = mat;
}

ParticleSystemNode::~ParticleSystemNode()
{
    delete renderItem;
    delete boundsRenderItem;
    delete renderer;
}

void ParticleSystemNode::setBlendMode(bool useAddittive)
{
    renderer->useAdditive = this->useAdditive = useAddittive;
}

void ParticleSystemNode::setBillboardScale(float scale)
{
//    billboardScale = scale;
}

void ParticleSystemNode::submitRenderItems()
{
    renderItem->sceneNode = sharedFromThis().staticCast<SceneNode>();
    renderItem->worldMatrix = this->globalTransform;
    renderItem->renderLayer = (int)RenderLayer::Transparent;

    this->scene->geometryRenderList->add(renderItem);
    this->scene->geometryRenderList->add(boundsRenderItem);
}

void ParticleSystemNode::generateParticles(float delta) {
    float particlesToCreate = particlesPerSecond * delta;
    int count = (int) floor(particlesToCreate);
    float partialParticle = fmod(particlesToCreate, 1);

    for (int i = 0; i < count; i++) {
        emitParticle();
    }

    if (static_cast <float> (rand()) / static_cast <float> (RAND_MAX) < partialParticle) {
        emitParticle();
    }
}

void ParticleSystemNode::emitParticle() {
    QVector4D dir = QVector4D(QVector3D(0, 1, 0), 0);
    QVector4D particleDirection = this->globalTransform * dir;

    // use a volume cube
    QVector3D velocity = QVector3D(particleDirection);
    // QVector3D velocity = generateRandomUnitVector();

    velocity.normalize();
    velocity *= generateValue(speed, speedError);
    float scl = generateValue(particleScale, scaleError);
    float ll = generateValue(lifeLength, lifeError);

    // reuse particles! this is bad.
    // change later after atlases
    boundDimension = QVector3D(1, 1, 1) * this->scale;
    addParticle(new Particle(this->getGlobalPosition() + boundDimension * generateRandomUnitVector(),
                             velocity,
                             gravityComplement,
                             ll,
                             generateRotation(),
                             scl));
}

float ParticleSystemNode::generateValue(float average, float errorMargin) {
    float offset = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.f * errorMargin;
    return average + offset;
}

float ParticleSystemNode::generateRotation() {
    if (randomRotation) {
        return static_cast<float>(rand()) / static_cast<float>(RAND_MAX / 360.f);
    } else {
        return 0;
    }
}

QVector3D ParticleSystemNode::generateRandomUnitVector() {
    float theta = (float) (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.f * M_PI);
    float z = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.f) - 1.f;
    float rootOneMinusZSquared = (float) sqrt(1 - z * z);
    float x = (float) (rootOneMinusZSquared * cos(theta));
    float y = (float) (rootOneMinusZSquared * sin(theta));
    return QVector3D(x, y, z);
}

void ParticleSystemNode::update(float delta) {
    SceneNode::update(delta);

    generateParticles(delta);
    //std::list<Particle*>::iterator it = particles.begin();

	for (int i = 0; i < particles.size(); i++) {
		auto p = particles[i];

        p->velocity += QVector3D(0, p->GRAVITY * p->gravityEffect * delta, 0);
        //p.velocity.setY(p.velocity.y() + GRAVITY * gravityEffect * delta);
        QVector3D change = p->velocity;
        change *= delta;
        p->position += change;
        //p.position += p->velocity * delta;
        p->elapsedTime += delta;

        // in the future we can call behavior management here, add more forces such as wind
        if (dissipate && p->elapsedTime > 0) {
            if (dissipateInv) {
                p->scale = MathHelper::lerp((p->elapsedTime / p->lifeLength), 0, particleScale);
            } else {
                p->scale *= 1.0f - (p->elapsedTime / p->lifeLength);
            }
        }

        if (p->elapsedTime > p->lifeLength) {
			particles.erase(particles.begin() + i);
			i--;
        }
    }
}

void ParticleSystemNode::renderParticles(GraphicsDevicePtr device, RenderData* renderData, QOpenGLShaderProgram* shader)
{
    renderer->icon = texture;
    renderer->render(device, shader, renderData, this->particles);
}

SceneNodePtr ParticleSystemNode::createDuplicate()
{
	auto ps = ParticleSystemNode::create();

	ps->particlesPerSecond  = this->particlesPerSecond;
	ps->speed				= this->speed;
	ps->texture				= this->texture;

	ps->dissipate			= this->dissipate;
	ps->dissipateInv		= this->dissipateInv;
	ps->randomRotation		= this->randomRotation;

	ps->lifeFactor			= this->lifeFactor;
	ps->scaleFactor			= this->scaleFactor;
	ps->speedFactor			= this->speedFactor;
	ps->useAdditive			= this->useAdditive;

	ps->gravityComplement	= this->gravityComplement;
	ps->lifeLength			= this->lifeLength;
	ps->particleScale		= this->particleScale;

	ps->maxParticles		= this->maxParticles;
	ps->billboardScale		= this->billboardScale;

	ps->speedError			= this->speedError;
	ps->lifeError			= this->lifeError;
	ps->scaleError			= this->scaleError;
	ps->direction			= this->direction;

	ps->posDir				= this->posDir;
	ps->boundDimension		= this->boundDimension;

	return ps;
}

}
