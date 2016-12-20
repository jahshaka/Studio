#ifndef TEXTURE_H
#define TEXTURE_H

#include <QSharedPointer>

class QOpenGLTexture;

namespace iris
{

class Texture;
typedef QSharedPointer<Texture> TexturePtr;

class Texture
{
public:
    QOpenGLTexture* texture;
    QString source;
};

}

#endif // TEXTURE_H
