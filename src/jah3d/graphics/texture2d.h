#ifndef TEXTURE2D_H
#define TEXTURE2D_H

#include <QSharedPointer>
#include "texture.h"
#include <QOpenGLTexture>
#include <QImage>

namespace jah3d
{

//class Texture;
class Texture2D;
typedef QSharedPointer<Texture2D> Texture2DPtr;

class Texture2D: public Texture
{

public:

    /**
     * Returns a null shared pointer
     * @return
     */
    static QSharedPointer<Texture2D> null()
    {
        return QSharedPointer<Texture2D>(nullptr);
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
    static Texture2DPtr load(QString path,bool flipY);

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
    QString getSource()
    {
        return source;
    }

private:
    Texture2D(QOpenGLTexture* tex)
    {
        this->texture = tex;
    }
};

}

#endif // TEXTURE2D_H
