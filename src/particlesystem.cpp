/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "particlesystem.h"

#include <QAttribute>
#include <QBuffer>
#include <QGeometry>
#include <QBuffer>
#include <QBufferDataGenerator>
#include "billboard.h"
#include <QVector3D>

using namespace Qt3DRender;


//http://www.geeks3d.com/20140807/billboarding-vertex-shader-glsl/
class ParticleVertexBufferFunctor : public QBufferDataGenerator
{
    ParticleSystem* particleSystem;
public:
    ParticleVertexBufferFunctor()
    {
    }

    void setParticleSystem(ParticleSystem* particleSystem)
    {
        this->particleSystem = particleSystem;
    }

    QByteArray operator()() Q_DECL_FINAL
    {
        return createParticleVertexData();
    }

    QByteArray createParticleVertexData()
    {
        auto particles = particleSystem->particles;

        int nVerts = 4;
        const quint32 elementSize = 5;
        QByteArray bufferBytes;

        for(auto p:particles)
        {
            Q_UNUSED(p);

            const quint32 stride = elementSize * sizeof(float);
            bufferBytes.resize(stride * nVerts);
            float* fptr = reinterpret_cast<float*>(bufferBytes.data());

            //TOP-RIGHT
            //pos
            *fptr++ = 1;
            *fptr++ = 1;
            *fptr++ = 0;
            //uv
            *fptr++ = 1;
            *fptr++ = 1;

            //TOP-LEFT
            //pos
            *fptr++ = -1;
            *fptr++ = 1;
            *fptr++ = 0;
            //uv
            *fptr++ = 0;
            *fptr++ = 1;

            //BOTTOM-LEFT
            //pos
            *fptr++ = -1;
            *fptr++ = -1;
            *fptr++ = 0;
            //uv
            *fptr++ = 0;
            *fptr++ = 0;

            //BOTTOM-RIGHT
            //pos
            *fptr++ = 1;
            *fptr++ = -1;
            *fptr++ = 0;
            //uv
            *fptr++ = 1;
            *fptr++ = 0;
        }


        return bufferBytes;
    }

    bool operator ==(const QBufferDataGenerator &other) const Q_DECL_FINAL
    {
        Q_UNUSED(other);
        return false;
    }

    QT3D_FUNCTOR(ParticleVertexBufferFunctor)
};

class ParticleIndexBufferFunctor : public QBufferDataGenerator
{
    ParticleSystem* particleSystem;
public:
    ParticleIndexBufferFunctor()
    {
    }

    void setParticleSystem(ParticleSystem* particleSystem)
    {
        this->particleSystem = particleSystem;
    }

    virtual QByteArray operator()() Q_DECL_FINAL
    {
        return createIndexData();
    }

    QByteArray createIndexData()
    {
        QByteArray indexBytes;
        indexBytes.resize(6 * sizeof(quint16));
        quint16* indexPtr = reinterpret_cast<quint16*>(indexBytes.data());

        *indexPtr++ = 0;
        *indexPtr++ = 1;
        *indexPtr++ = 2;

        *indexPtr++ = 0;
        *indexPtr++ = 2;
        *indexPtr++ = 3;

        return indexBytes;
    }

    bool operator ==(const QBufferDataGenerator &other) const Q_DECL_FINAL
    {
        Q_UNUSED(other);
        return true;
    }

    QT3D_FUNCTOR(ParticleIndexBufferFunctor)
};


ParticleGeometry::ParticleGeometry()
{
    posAttr = new QAttribute(this);
    uvAttr = new QAttribute(this);
    indexAttr = new QAttribute(this);
    vertexBuffer = new QBuffer(QBuffer::VertexBuffer, this);
    indexBuffer = new QBuffer(QBuffer::IndexBuffer, this);

    int stride = 5 * sizeof(float);//vec4
    posAttr->setName(QAttribute::defaultPositionAttributeName());
    posAttr->setDataType(QAttribute::Float);
    posAttr->setDataSize(3);
    posAttr->setAttributeType(QAttribute::VertexAttribute);
    posAttr->setBuffer(vertexBuffer);
    posAttr->setByteStride(stride);
    posAttr->setCount(4);
    //posAttr->setCount(5);//?

    uvAttr->setName(QAttribute::defaultTextureCoordinateAttributeName());
    uvAttr->setDataType(QAttribute::Float);
    uvAttr->setDataSize(2);
    uvAttr->setAttributeType(QAttribute::VertexAttribute);
    uvAttr->setBuffer(vertexBuffer);
    uvAttr->setByteStride(stride);
    uvAttr->setByteOffset(3 * sizeof(float));
    uvAttr->setCount(4);

    indexAttr->setAttributeType(QAttribute::IndexAttribute);
    indexAttr->setDataType(QAttribute::UnsignedShort);
    indexAttr->setBuffer(indexBuffer);
    indexAttr->setCount(6);

    // 4 faces, 3 vertices each
    //indexAttr->setCount(4 * 3);
    this->addAttribute(posAttr);
    this->addAttribute(uvAttr);
    this->addAttribute(indexAttr);

    vertexBuffer->setDataGenerator(QSharedPointer<ParticleVertexBufferFunctor>::create());
    indexBuffer->setDataGenerator(QSharedPointer<ParticleIndexBufferFunctor>::create());

}


ParticleSystem::ParticleSystem()
{

}
