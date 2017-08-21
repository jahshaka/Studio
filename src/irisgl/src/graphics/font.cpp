#include "font.h"
#include "../irisglfwd.h"
#include "texture2d.h"
#include <QFont>
#include <QFontMetrics>
#include <QPixmap>
#include <QPainter>
#include <QMap>

namespace iris
{

Font::Font(GraphicsDevicePtr graphics, QString fontName)
{
    this->graphics = graphics;
    font = QFont("Arial",8);
    metrics = new QFontMetrics(font);

//    Glyph glyph;
//    createGlyph(QChar('a'),glyph);
//    glyphs.insert(QChar('a'),glyph);
    QString chars = " abcdefghijklmnopqrstuvwxyzBCDEFGHIJKLMNOPQRSTUVWXZY1234567890._-/!A";

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
    painter.drawText(0, charHeight, chr);
    painter.end();

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
