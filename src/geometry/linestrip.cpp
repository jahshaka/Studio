/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QAttribute>
#include <QBuffer>
#include <QGeometry>
#include <QBuffer>
#include <QBufferDataGenerator>
#include "linestrip.h"
#include <QVector3D>

using namespace Qt3DRender;



class LineStripVertexBufferFunctor : public QBufferDataGenerator
{
    std::vector<QVector3D> points;
public:
    LineStripVertexBufferFunctor(std::vector<QVector3D> points)
    {
        this->points = points;
    }

    QByteArray operator()() Q_DECL_FINAL
    {
        return createVertexData();
    }

    QByteArray createVertexData()
    {
        int nVerts = points.size();
        const quint32 elementSize = 3;
        const quint32 stride = elementSize * sizeof(float);
        QByteArray bufferBytes;
        bufferBytes.resize(stride * nVerts);
        float* fptr = reinterpret_cast<float*>(bufferBytes.data());

        for(unsigned int i=0;i<points.size();i++)
        {
            auto point = points[i];
            *fptr++ = point.x();
            *fptr++ = point.y();
            *fptr++ = point.z();
        }

        return bufferBytes;
    }

    bool operator ==(const QBufferDataGenerator &other) const Q_DECL_FINAL
    {
        Q_UNUSED(other);
        return false;
    }

    QT3D_FUNCTOR(LineStripVertexBufferFunctor)
};

class LineStripIndexBufferFunctor : public QBufferDataGenerator
{
    int numPoints;
public:
    LineStripIndexBufferFunctor(int numPoints)
    {
        this->numPoints = numPoints;
    }

    virtual QByteArray operator()() Q_DECL_FINAL
    {
        return createIndexData();
    }

    QByteArray createIndexData()
    {
        QByteArray indexBytes;
        indexBytes.resize(numPoints * sizeof(quint16));
        quint16* indexPtr = reinterpret_cast<quint16*>(indexBytes.data());

        for(int i=0;i<numPoints;i++)
        {
            *indexPtr++ = i;
        }

        return indexBytes;
    }

    bool operator ==(const QBufferDataGenerator &other) const Q_DECL_FINAL
    {
        Q_UNUSED(other);
        return true;
    }

    QT3D_FUNCTOR(LineStripIndexBufferFunctor)
};

LineStripGeometry::LineStripGeometry()
{
    posAttr = new QAttribute(this);
    indexAttr = new QAttribute(this);
    vertexBuffer = new QBuffer(QBuffer::VertexBuffer, this);
    indexBuffer = new QBuffer(QBuffer::IndexBuffer, this);

    int stride = 3 * sizeof(float);//vec3
    posAttr->setName(QAttribute::defaultPositionAttributeName());
    posAttr->setDataType(QAttribute::Float);
    posAttr->setDataSize(3);
    posAttr->setAttributeType(QAttribute::VertexAttribute);
    posAttr->setBuffer(vertexBuffer);
    posAttr->setByteStride(stride);
    posAttr->setCount(0);

    indexAttr->setAttributeType(QAttribute::IndexAttribute);
    indexAttr->setDataType(QAttribute::UnsignedShort);
    indexAttr->setBuffer(indexBuffer);

    this->addAttribute(posAttr);
    this->addAttribute(indexAttr);

    //needs at least some data else it completely crashes
    //ASSERT failure in QVector<T>::at: "index out of range", file c:\Users\qt\work\install\include/QtCore/qvector.h, line 419
    std::vector<QVector3D> points;
    points.push_back(QVector3D(0,0,0));
    setPoints(points);
}

//wont update after data is initially set :(
void LineStripGeometry::setPoints(std::vector<QVector3D> points)
{
    this->points = points;
    this->posAttr->setCount(points.size());
    this->indexAttr->setCount(points.size());

    vertexBuffer->setDataGenerator(QSharedPointer<LineStripVertexBufferFunctor>::create(points));
    indexBuffer->setDataGenerator(QSharedPointer<LineStripIndexBufferFunctor>::create(points.size()));
}
