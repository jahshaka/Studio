/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ENDLESSPLANE_H
#define ENDLESSPLANE_H

#include <QAttribute>
#include <QBuffer>
#include <QGeometry>
#include <QBuffer>
#include <QBufferDataGenerator>

using namespace Qt3DRender;

//http://stackoverflow.com/questions/12965161/rendering-infinitely-large-plane
QByteArray createPlaneVertexData()
{
    // Populate a buffer with the interleaved per-vertex data with
    // vec4 pos
    int nVerts = 5;
    const quint32 elementSize = 4;
    const quint32 stride = elementSize * sizeof(float);
    QByteArray bufferBytes;
    bufferBytes.resize(stride * nVerts);
    float* fptr = reinterpret_cast<float*>(bufferBytes.data());

    *fptr++ = 0.0f;
    *fptr++ = 0.0f;
    *fptr++ = 0.0f;
    *fptr++ = 0.0f;

    *fptr++ = 1.0f;
    *fptr++ = 0.0f;
    *fptr++ = 0.0f;
    *fptr++ = 0.0f;

    *fptr++ = 0.0f;
    *fptr++ = 0.0f;
    *fptr++ = 1.0f;
    *fptr++ = 0.0f;

    *fptr++ = -1.0f;
    *fptr++ = 0.0f;
    *fptr++ = 0.0f;
    *fptr++ = 0.0f;

    *fptr++ = 0.0f;
    *fptr++ = 0.0f;
    *fptr++ = -1.0f;
    *fptr++ = 0.0f;

    return bufferBytes;
}

QByteArray createPlaneIndexData()
{
    const int indices = 3 * 4;
    Q_ASSERT(indices < std::numeric_limits<quint16>::max());
    QByteArray indexBytes;
    indexBytes.resize(indices * sizeof(quint16));
    quint16* indexPtr = reinterpret_cast<quint16*>(indexBytes.data());

    //rewind vertex order
    *indexPtr++ = 0;
    *indexPtr++ = 2;
    *indexPtr++ = 1;

    *indexPtr++ = 0;
    *indexPtr++ = 3;
    *indexPtr++ = 2;

    *indexPtr++ = 0;
    *indexPtr++ = 4;
    *indexPtr++ = 3;

    *indexPtr++ = 0;
    *indexPtr++ = 1;
    *indexPtr++ = 4;

    return indexBytes;
}

class EndlessPlaneVertexBufferFunctor : public QBufferDataGenerator
{
public:
    QByteArray operator()() Q_DECL_FINAL
    {
        return createPlaneVertexData();
    }

    bool operator ==(const QBufferDataGenerator &other) const Q_DECL_FINAL
    {
        Q_UNUSED(other);
        return false;
    }

    QT3D_FUNCTOR(EndlessPlaneVertexBufferFunctor)
};

class EndlessPlaneIndexBufferFunctor : public QBufferDataGenerator
{
public:
    virtual QByteArray operator()() Q_DECL_FINAL
    {
        return createPlaneIndexData();
    }

    bool operator ==(const QBufferDataGenerator &other) const Q_DECL_FINAL
    {
        Q_UNUSED(other);
        return true;
    }

    QT3D_FUNCTOR(EndlessPlaneIndexBufferFunctor)
};

class EndlessPlane:public QGeometry
{
public:
    QAttribute* posAttr;
    QAttribute* indexAttr;
    QBuffer* vertexBuffer;
    QBuffer* indexBuffer;

    EndlessPlane()
    {
        posAttr = new QAttribute(this);
        indexAttr = new QAttribute(this);
        vertexBuffer = new QBuffer(QBuffer::VertexBuffer, this);
        indexBuffer = new QBuffer(QBuffer::IndexBuffer, this);

        int stride = 4 * sizeof(float);//vec4
        posAttr->setName(QAttribute::defaultPositionAttributeName());
        posAttr->setDataType(QAttribute::Float);
        posAttr->setDataSize(4);
        posAttr->setAttributeType(QAttribute::VertexAttribute);
        posAttr->setBuffer(vertexBuffer);
        posAttr->setByteStride(stride);
        posAttr->setCount(5);

        indexAttr->setAttributeType(QAttribute::IndexAttribute);
        indexAttr->setDataType(QAttribute::UnsignedShort);
        indexAttr->setBuffer(indexBuffer);

        // 4 faces, 3 vertices each
        indexAttr->setCount(4 * 3);

        //vertexBuffer->setDataGenerator(QBufferFunctorPtr(new EndlessPlaneVertexBufferFunctor()));
        vertexBuffer->setDataGenerator(QSharedPointer<EndlessPlaneVertexBufferFunctor>::create());
        indexBuffer->setDataGenerator(QSharedPointer<EndlessPlaneIndexBufferFunctor>::create());

        this->addAttribute(posAttr);
        this->addAttribute(indexAttr);
    }

};

#endif // ENDLESSPLANE_H
