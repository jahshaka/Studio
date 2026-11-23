// THE EDITOR's IMAGE DECODE (NOQT-DOCUMENT-1 ruling 4): the document reads no image file, so the two questions it
// used to answer with Qt are answered here, at the editor's edge, and nowhere else:
//   * IS THIS PATH A READABLE IMAGE? - the readable-image probe Texture2D::load asks (texture2d.h). installProbe() is
//     called ONCE at boot (src/app/main.cpp, right after the QApplication: before any thread that loads exists, on
//     every boot - windowed, --script, --headless, --mcp); Qt-free processes (jahlab, jahengine) run without it.
//   * A CUBE SKY's SIX FACES: decoded, made RGBA and one square size, handed to the document as iris::Image.
// And the two conversions between a QImage and the document's pixels (here, not in app/qtstd.h: that header is
// included by Qt-Core-only targets, and a QImage is QtGui's). The engine decodes every other texture itself.
#pragma once

#include "irisgl/document/assets/image.h"
#include "irisgl/document/assets/texture2d.h"

#include <QImage>
#include <QImageReader>
#include <QString>

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>

namespace imagedecode {

/// A QImage -> the document's pixels (iris::Image: 8-bit RGBA, rows top to bottom, packed), through RGBA8888 -
/// the format every pixel the document holds was kept in. A null QImage gives a null Image.
inline iris::Image toImage(const QImage &image)
{
    iris::Image out;
    if (image.isNull()) return out;
    const QImage rgba = image.format() == QImage::Format_RGBA8888 ? image : image.convertToFormat(QImage::Format_RGBA8888);
    out.width = rgba.width();
    out.height = rgba.height();
    const std::size_t row = std::size_t(out.width) * 4u;
    out.rgba.resize(row * std::size_t(out.height));
    for (int y = 0; y < out.height; ++y)
        std::memcpy(out.rgba.data() + row * std::size_t(y), rgba.constScanLine(y), row);
    return out;
}

/// The document's pixels -> a QImage (RGBA8888, its own copy). A null Image gives a null QImage.
inline QImage toQImage(const iris::Image &image)
{
    if (image.isNull()) return QImage();
    return QImage(image.rgba.data(), image.width, image.height, image.width * 4, QImage::Format_RGBA8888).copy();
}

/// The probe: a HEADER read (QImageReader::canRead), never a decode - a scene open probes every map it binds.
inline bool readable(const std::string &path)
{
    return QImageReader(QString::fromStdString(path)).canRead();
}

/// Installs readable() as the document's probe. True when it is the probe (installed now or before); false when a
/// different probe was installed first (Texture2D refuses a swap by name).
inline bool installProbe()
{
    return iris::Texture2D::installReadableProbe(&readable);
}

/// A cube sky from six image files, in the scene file's order (negZ, posZ, posY, negY, negX, posX = front, back, top,
/// bottom, left, right): each decoded to RGBA8888 and, where its size differs from the +X face's width, scaled to that
/// square (SmoothTransformation). NULL when a face does not decode.
inline std::shared_ptr<iris::Texture2D> loadCubeMap(const QString &negZ, const QString &posZ, const QString &posY,
                                                    const QString &negY, const QString &negX, const QString &posX)
{
    QImage faces[6] = {
        QImage(posX).convertToFormat(QImage::Format_RGBA8888),
        QImage(negX).convertToFormat(QImage::Format_RGBA8888),
        QImage(posY).convertToFormat(QImage::Format_RGBA8888),
        QImage(negY).convertToFormat(QImage::Format_RGBA8888),
        QImage(posZ).convertToFormat(QImage::Format_RGBA8888),
        QImage(negZ).convertToFormat(QImage::Format_RGBA8888)
    };
    for (const QImage &face : faces)
        if (face.isNull()) return nullptr;
    const int size = faces[0].width();
    iris::Image out[6];
    for (int i = 0; i < 6; ++i) {
        if (faces[i].width() != size || faces[i].height() != size)
            faces[i] = faces[i].scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        out[i] = toImage(faces[i]);
    }
    return iris::Texture2D::createCubeMap(out);
}

}   // namespace imagedecode
