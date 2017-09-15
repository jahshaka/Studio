#include "font.h"
#include "../irisglfwd.h"
#include "texture2d.h"
#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QPixmap>
#include <QPainter>
#include <QMap>
#include <QDebug>

namespace iris
{

// https://github.com/libgdx/libgdx/blob/master/extensions/gdx-freetype/src/com/badlogic/gdx/graphics/g2d/freetype/FreeTypeFontGenerator.java
Font::Font(GraphicsDevicePtr graphics, QString fontName, int fontSize)
{
    this->graphics = graphics;
    font = QFont(fontName, fontSize);
    font.setPixelSize(fontSize); // pixel size is screen independent

    QString chars = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890\"!`?'.,;:()[]{}<>|/@\\^$€-%+=#_&~*";

    for(auto chr : chars) {
        Glyph glyph;
        createGlyph(QChar(chr),glyph);
        glyphs.insert(QChar(chr),glyph);
    }
}

void Font::createGlyph(QChar chr, Glyph &glyph)
{
    // create dummy image in order to get the font metrics
    QImage dummy(100, 100, QImage::Format_ARGB32);
    QPainter dummyPainter(&dummy);
    dummyPainter.setFont(font);
    auto metrics = dummyPainter.fontMetrics();

    int charWidth = metrics.width(chr);
    int charHeight = metrics.height();
    // This adjustment has to be made for high dpi devices
    // where qt automatically adds extra padding around the text
    auto padding = (1 - qApp->devicePixelRatio()) * 2;
    charWidth += padding;
    charHeight += padding;


    QImage image(charWidth, charHeight, QImage::Format_ARGB32);
    image.fill(0);

    QPainter painter;
    painter.begin(&image);
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255, 255));
    painter.setBrush(QBrush(QColor(255, 255, 255, 255)));
    painter.drawText(0, metrics.ascent(), chr);
    painter.end();

    // convert luminance to alpha to make texture blend properly
    // when using GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
    for(int x = 0;x<image.width(); x++) {
        for(int y = 0;y<image.height(); y++) {
            auto pixel = image.pixelColor(x,y);
            if (pixel.alpha()>0) {
                // make pixel white and use luminance as alpha
                // can just use red() since r,g and b are the same
                image.setPixelColor(x,y,QColor(255, 255, 255, pixel.red()));
            }

        }
    }

    glyph.chr = chr;
    glyph.tex = Texture2D::create(image);
    glyph.tex->setWrapMode(QOpenGLTexture::ClampToEdge, QOpenGLTexture::ClampToEdge);
    glyph.tex->setFilters(QOpenGLTexture::Nearest, QOpenGLTexture::Nearest);
    glyph.width = charWidth;
    glyph.height = charHeight;
}

FontPtr Font::create(GraphicsDevicePtr graphics, int size)
{
    return FontPtr(new Font(graphics, "Arial", size));
}

FontPtr Font::create(GraphicsDevicePtr graphics, QString fontName, int size)
{
    return FontPtr(new Font(graphics, fontName, size));
}

}
