#include "font.h"
#include "../irisglfwd.h"
#include "texture2d.h"
#include <QFont>
#include <QFontMetrics>
#include <QPixmap>
#include <QPainter>
#include <QMap>
#include <QDebug>

namespace iris
{

// https://github.com/libgdx/libgdx/blob/master/extensions/gdx-freetype/src/com/badlogic/gdx/graphics/g2d/freetype/FreeTypeFontGenerator.java
Font::Font(GraphicsDevicePtr graphics, QString fontName)
{
    this->graphics = graphics;
    font = QFont("Arial",16);
    metrics = new QFontMetrics(font);

//    Glyph glyph;
//    createGlyph(QChar('a'),glyph);
//    glyphs.insert(QChar('a'),glyph);
    QString chars = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890\"!`?'.,;:()[]{}<>|/@\\^$€-%+=#_&~*";

    for(auto chr : chars) {
        Glyph glyph;
        createGlyph(QChar(chr),glyph);
        glyphs.insert(QChar(chr),glyph);
    }
}

void Font::createGlyph(QChar chr, Glyph &glyph)
{
    int charWidth = metrics->width(chr);
    int charHeight = metrics->height();
    //QPixmap* pixmap = new QPixmap(charWidth, charHeight);

    QImage image(charWidth, charHeight, QImage::Format_ARGB32);
    image.fill(0);

    QPainter painter;
    painter.begin(&image);
    //painter.eraseRect(0, 0, image.width(), image.height());
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255, 255));
    painter.setBrush(QBrush(QColor(255, 255, 255, 255)));
    painter.drawText(0, metrics->ascent(), chr);
    painter.end();

    // convert luminance to alpha to make texture blend properly
    // when using GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
    for(int x = 0;x<image.width(); x++) {
        for(int y = 0;y<image.height(); y++) {
            auto pixel = image.pixelColor(x,y);
            //qDebug()<<pixel.alpha();
            if (pixel.alpha()>0) {
                // make pixel white and use luminance as alpha
                // can just use red() since r,g and b are the same
                image.setPixelColor(x,y,QColor(255, 255, 255, pixel.red()));
            }

        }
    }

    //auto gl = device->getGL();
    //QImage image = pixmap->toImage();
    image.save("/home/nicolas/Desktop/image.png");
    glyph.chr = chr;
    glyph.tex = Texture2D::create(image);
    glyph.tex->setWrapMode(QOpenGLTexture::ClampToEdge, QOpenGLTexture::ClampToEdge);
    glyph.tex->setFilters(QOpenGLTexture::Nearest, QOpenGLTexture::Nearest);
    glyph.width = charWidth;
    glyph.height = charHeight;
    //glyph.pixmap = pixmap;

    //delete pixmap;
}

FontPtr Font::create(GraphicsDevicePtr graphics)
{
    return FontPtr(new Font(graphics, ""));
}


}
