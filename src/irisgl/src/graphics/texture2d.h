/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TEXTURE2D_H
#define TEXTURE2D_H

#include <QSharedPointer>
#include "texture.h"
#include <QOpenGLTexture>
#include <QImage>
#include "../irisglfwd.h"

namespace iris
{

class Texture2D: public Texture
{

public:

    /**
     * Returns a null shared pointer
     * @return
     */
    static Texture2DPtr null()
    {
        return Texture2DPtr(nullptr);
    }

    //todo: move mipmap generation and texture filter responsibilities to Texture2D class's non-static members

    /**
     * Loads a texture. The image is flipped on the y-axis.
     * @param path
     * @return
     */
    static Texture2DPtr load(QString path);

    /**
     * Loads a texture. Setting flipY to true flips the image on the y-axis
     * @param path
     * @return
     */
    static Texture2DPtr load(QString path, bool flipY);

    /**
     * Created texture from QImage
     * @param image
     * @return
     */
    static Texture2DPtr create(QImage image);

    /**
     * Returns the path to the source file of the texture
     * @return
     */
    QString getSource() {
        return source;
    }

    static Texture2DPtr createCubeMap(QString, QString, QString, QString, QString, QString, QImage *i = nullptr);

private:
    Texture2D(QOpenGLTexture* tex)
    {
        this->texture = tex;
    }
};

}

#endif // TEXTURE2D_H
