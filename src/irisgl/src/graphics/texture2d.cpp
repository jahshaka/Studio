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
    texture->generateMipMaps();
    texture->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear,QOpenGLTexture::Linear);

    return QSharedPointer<Texture2D>(new Texture2D(texture));
}
}
