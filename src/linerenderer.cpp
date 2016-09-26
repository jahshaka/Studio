/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QAttribute>
#include <QBuffer>
#include <QGeometry>
#include <QBuffer>
#include <QBufferDataGenerator>
#include "linerenderer.h"
#include <QVector3D>

using namespace Qt3DRender;


class LineListVertexBufferFunctor : public QBufferDataGenerator
{
    std::vector<Line3D> lines;
public:
    LineListVertexBufferFunctor(std::vector<Line3D> lines)
    {
        this->lines = lines;
    }

    QByteArray operator()() Q_DECL_FINAL
    {
        return createVertexData();
    }

    QByteArray createVertexData()
    {
        int nVerts = lines.size()*2;
        const quint32 elementSize = 6;
        const quint32 stride = elementSize * sizeof(float);
        QByteArray bufferBytes;
        bufferBytes.resize(stride * nVerts);
        float* fptr = reinterpret_cast<float*>(bufferBytes.data());

        auto lineCount = lines.size();
        for(unsigned int i=0;i<lineCount;i++)
        {
            auto line = lines[i];
            *fptr++ = line.point1.x();
            *fptr++ = line.point1.y();
            *fptr++ = line.point1.z();

            *fptr++ = line.color.red()/255.5;
            *fptr++ = line.color.green()/255.5;
            *fptr++ = line.color.blue()/255.5;

            *fptr++ = line.point2.x();
            *fptr++ = line.point2.y();
            *fptr++ = line.point2.z();

            *fptr++ = line.color.red()/255.5;
            *fptr++ = line.color.green()/255.5;
            *fptr++ = line.color.blue()/255.5;
        }

        return bufferBytes;
    }

    bool operator ==(const QBufferDataGenerator &other) const Q_DECL_FINAL
    {
        Q_UNUSED(other);
        return false;
    }

    QT3D_FUNCTOR(LineListVertexBufferFunctor)
};

class LineListIndexBufferFunctor : public QBufferDataGenerator
{
    int numLines;
public:
    LineListIndexBufferFunctor(int numLines)
    {
        this->numLines = numLines;
    }

    virtual QByteArray operator()() Q_DECL_FINAL
    {
        return createIndexData();
    }

    QByteArray createIndexData()
    {
        QByteArray indexBytes;
        auto numVerts = numLines * 2;
        indexBytes.resize(numVerts * sizeof(quint16));
        quint16* indexPtr = reinterpret_cast<quint16*>(indexBytes.data());

        for(int i=0;i<numLines*2;i++)
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

    QT3D_FUNCTOR(LineListIndexBufferFunctor)
};

LineGeometry::LineGeometry(std::vector<Line3D>* lines)
{
    posAttr = new QAttribute(this);
    colAttr = new QAttribute(this);
    indexAttr = new QAttribute(this);
    vertexBuffer = new QBuffer(QBuffer::VertexBuffer, this);
    indexBuffer = new QBuffer(QBuffer::IndexBuffer, this);

    int stride = ( 3 + 3 ) * sizeof(float);//pos(3)+col(3)
    posAttr->setName(QAttribute::defaultPositionAttributeName());
    posAttr->setDataType(QAttribute::Float);
    posAttr->setDataSize(3);
    posAttr->setAttributeType(QAttribute::VertexAttribute);
    posAttr->setBuffer(vertexBuffer);
    posAttr->setByteStride(stride);
    posAttr->setCount(0);

    colAttr->setName(QAttribute::defaultColorAttributeName());
    colAttr->setDataType(QAttribute::Float);
    colAttr->setDataSize(3);
    colAttr->setAttributeType(QAttribute::VertexAttribute);
    colAttr->setBuffer(vertexBuffer);
    colAttr->setByteStride(stride);
    colAttr->setByteOffset(3*sizeof(float));
    colAttr->setCount(0);


    indexAttr->setAttributeType(QAttribute::IndexAttribute);
    indexAttr->setDataType(QAttribute::UnsignedShort);
    indexAttr->setBuffer(indexBuffer);

    this->addAttribute(posAttr);
    this->addAttribute(colAttr);
    this->addAttribute(indexAttr);

    //needs at least some data else it completely crashes
    //ASSERT failure in QVector<T>::at: "index out of range", file c:\Users\qt\work\install\include/QtCore/qvector.h, line 419
    if(lines==nullptr)
    {
        lines = new std::vector<Line3D>();
        lines->push_back(Line3D());
    }

    setLines(lines);
}

//wont update after data is initially set :(
void LineGeometry::setLines(std::vector<Line3D>* lines)
{
    int size = lines->size();
    this->lines = *lines;
    this->posAttr->setCount(size*2);
    this->colAttr->setCount(size*2);
    this->indexAttr->setCount(size*2);

    vertexBuffer->setDataGenerator(QSharedPointer<LineListVertexBufferFunctor>::create(*lines));
    indexBuffer->setDataGenerator(QSharedPointer<LineListIndexBufferFunctor>::create(size));
}
