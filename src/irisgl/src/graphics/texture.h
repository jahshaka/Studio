/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TEXTURE_H
#define TEXTURE_H

#include <QSharedPointer>

class QOpenGLTexture;

namespace iris
{

class Texture
{
public:
    QOpenGLTexture* texture;
    QString source;

    void bind();
    void bind(int index);
};

}

#endif // TEXTURE_H
