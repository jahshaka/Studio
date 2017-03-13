/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "texture2d.h"
#include <QDebug>

namespace iris
{

Texture2DPtr Texture2D::load(QString path)
{
    return load(path,true);
}

Texture2DPtr Texture2D::load(QString path,bool flipY)
{
    auto image = QImage(path);
    if(flipY)
        image = image.mirrored(false,true);
    if(image.isNull())
    {
        qDebug()<<"error loading image: "<<path<<endl;
        return Texture2DPtr(nullptr);
    }

    auto tex = create(image);
    tex->source = path;

    return tex;
}

Texture2DPtr Texture2D::create(QImage image)
{
    auto texture = new QOpenGLTexture(image);
    //texture->generateMipMaps();
    texture->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear,QOpenGLTexture::Linear);

    return QSharedPointer<Texture2D>(new Texture2D(texture));
}

Texture2DPtr Texture2D::createCubeMap(QString posX, QString negX,
                                      QString posY, QString negY,
                                      QString posZ, QString negZ,
                                      QImage *info)
{
    int width, height, depth;

    const QImage pos_x = QImage(posX).convertToFormat(QImage::Format_RGBA8888);
    const QImage neg_x = QImage(negX).convertToFormat(QImage::Format_RGBA8888);
    const QImage pos_y = QImage(posY).mirrored(true).convertToFormat(QImage::Format_RGBA8888);
    const QImage neg_y = QImage(negY).mirrored(true).convertToFormat(QImage::Format_RGBA8888);
    const QImage pos_z = QImage(posZ).convertToFormat(QImage::Format_RGBA8888);
    const QImage neg_z = QImage(negZ).convertToFormat(QImage::Format_RGBA8888);

    if (info) {
        width = pos_x.width();
        height = pos_x.height();
        depth = pos_x.depth();
    } else {
        width = pos_x.width();
        height = pos_x.height();
        depth = pos_x.depth();
    }

    auto texture = new QOpenGLTexture(QOpenGLTexture::TargetCubeMap);
    texture->create();
    texture->setSize(width, height, depth);
    texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
    texture->allocateStorage();
    texture->setData(0, 0, QOpenGLTexture::CubeMapPositiveX,
                     QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                     (const void*) pos_x.constBits(), 0);
    texture->setData(0, 0, QOpenGLTexture::CubeMapPositiveY,
                     QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                     (const void*) pos_y.constBits(), 0);
    texture->setData(0, 0, QOpenGLTexture::CubeMapPositiveZ,
                     QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                     (const void*) pos_z.constBits(), 0);
    texture->setData(0, 0, QOpenGLTexture::CubeMapNegativeX,
                     QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                     (const void*) neg_x.constBits(), 0);
    texture->setData(0, 0, QOpenGLTexture::CubeMapNegativeY,
                     QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                     (const void*) neg_y.constBits(), 0);
    texture->setData(0, 0, QOpenGLTexture::CubeMapNegativeZ,
                     QOpenGLTexture::RGBA, QOpenGLTexture::UInt8,
                     (const void*) neg_z.constBits(), 0);

    texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    texture->setMinificationFilter(QOpenGLTexture::Linear);
    texture->setMagnificationFilter(QOpenGLTexture::Linear);

    return QSharedPointer<Texture2D>(new Texture2D(texture));
}
}
