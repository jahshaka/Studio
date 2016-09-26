/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef PARTICLESYSTEM_H
#define PARTICLESYSTEM_H

#include <QAttribute>
#include <QBuffer>
#include <QGeometry>
#include <QGeometryRenderer>
#include <QBuffer>
#include <QBufferDataGenerator>
#include <Qt3DRender/QAbstractTexture>
#include <QVector3D>

using namespace Qt3DRender;

class ParticleGeometry:public QGeometry
{
public:
    QAttribute* posAttr;
    QAttribute* uvAttr;
    QAttribute* indexAttr;
    QBuffer* vertexBuffer;
    QBuffer* indexBuffer;

    ParticleGeometry();
};


class ParticleRenderer:public QGeometryRenderer
{
    ParticleGeometry* geom;
public:
    ParticleRenderer()
    {
        geom = new ParticleGeometry();
        this->setGeometry(geom);
        this->setPrimitiveType(QGeometryRenderer::Triangles);
    }
};

class Particle
{
public:
    QVector3D pos;
};

class ParticleSystem
{
public:
    Qt3DRender::QAbstractTexture* texture;
    QVector<Particle*> particles;

    ParticleRenderer* particleRenderer;
    ParticleGeometry* particleGeometry;

    ParticleSystem();

    void update(float dt);
    void updateGeometry();
};

#endif // PARTICLESYSTEM_H
