#ifndef TEXTURE2D_H
#define TEXTURE2D_H

#include <QSharedPointer>
#include "texture.h"
#include <QOpenGLTexture>
#include <QImage>

namespace jah3d
{

//class Texture;
typedef QSharedPointer<Texture2D> Texture2DPtr;

class Texture2D: public Texture
{

public:
    //todo: move mipmap generation and texture filter responsibilities to Texture2D class's non-static members
    static Texture2DPtr load(QString path)
    {
        auto image = QImage(path);
        image = image.mirrored(false,true);
        if(image.isNull())
        {
            qDebug()<<"error loading image: "<<path<<endl;
            return nullptr;
        }

        return create(image);
    }

    static Texture2DPtr create(QImage image)
    {
        auto texture = new QOpenGLTexture(image);
        texture->generateMipMaps();
        texture->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear,QOpenGLTexture::Linear);

        return QSharedPointer<Texture2D>(new Texture2D(texture));
    }

private:
    Texture2D(QOpenGLTexture* tex)
    {
        this->texture = tex;
    }
};

}

#endif // TEXTURE2D_H
