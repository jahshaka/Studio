#ifndef FONT_H
#define FONT_H

#include "../irisglfwd.h"
#include <QFont>
#include <QChar>
#include <QPixmap>
#include <QMap>

class QFont;
class QFontMetrics;

namespace iris
{

struct Glyph
{
    Texture2DPtr tex;
    QPixmap pixmap;
    QChar chr;

    int width;
    int height;
};

class Font
{
public:
    QFont font;
    QFontMetrics* metrics;
    QMap<QChar, Glyph> glyphs;
    GraphicsDevicePtr graphics;

    Font(GraphicsDevicePtr graphics, QString fontName);

    void createGlyph(QChar chr, Glyph& glyph);

    static FontPtr create(GraphicsDevicePtr graphics);
};

}

#endif // FONT_H
