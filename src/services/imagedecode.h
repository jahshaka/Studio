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
#include <QPainter>
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

/// A FRAME INTO THE DOCUMENT's PIXELS IN ONE PASS (NOQT-DOCUMENT-1 stage 5, the owner's bar: a live frame costs no
/// copy of ours): `src` is written into `dst`'s OWN buffer, the conversion to RGBA8888 being that pass (a QImage over
/// dst's bytes, drawn with CompositionMode_Source: Qt's own conversion, no picture change). QVideoFrame::toImage
/// gives RGB32 or ARGB32_Premultiplied, never RGBA8888 (spikes/noqt-document-1/s5/toimage-format.txt), so the
/// convertToFormat pass a frame always paid is the one that now writes into our memory. dst is (re)sized only when
/// its size differs: a producer that keeps one Image and hands it to Texture2D::writeLive (which hands the previous
/// frame back) allocates nothing a frame. False for a null src.
inline bool intoImage(const QImage &src, iris::Image &dst)
{
    if (src.isNull()) return false;
    const int w = src.width();
    const int h = src.height();
    const std::size_t row = std::size_t(w) * 4u;
    if (dst.width != w || dst.height != h || dst.rgba.size() != row * std::size_t(h)) {
        dst.width = w;
        dst.height = h;
        dst.rgba.resize(row * std::size_t(h));
    }
    if (src.format() == QImage::Format_RGBA8888) {
        // ONE PASS MORE THAN THE BASE HERE: before stage 3 an RGBA8888 frame at the texture's size was shared into the
        // texture (0 passes); this copies it. QVideoFrame::toImage never gave RGBA8888 in the probe on this box - but
        // that probe ran with "No RHI backend. Using CPU conversion": the app's RHI path and other platforms are
        // UNVERIFIED (the reader's S2; the STATE's leftovers).
        for (int y = 0; y < h; ++y)
            std::memcpy(dst.rgba.data() + row * std::size_t(y), src.constScanLine(y), row);
        return true;
    }
    QImage target(dst.rgba.data(), w, h, int(row), QImage::Format_RGBA8888);
    QPainter painter(&target);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(0, 0, src);
    return true;
}

/// The document's pixels -> a QImage (RGBA8888, its own copy). A null Image gives a null QImage.
inline QImage toQImage(const iris::Image &image)
{
    if (image.isNull()) return QImage();
    return QImage(image.rgba.data(), image.width, image.height, image.width * 4, QImage::Format_RGBA8888).copy();
}

/// The document's pixels VIEWED as a QImage (RGBA8888, NO copy): valid while `image` lives unchanged; a write to the
/// QImage detaches it (Qt copies then). For a reader only. A null Image gives a null QImage.
inline QImage view(const iris::Image &image)
{
    if (image.isNull()) return QImage();
    return QImage(image.rgba.data(), image.width, image.height, image.width * 4, QImage::Format_RGBA8888);
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
        faces[i] = QImage();   // the decoded face is released as soon as its copy exists: two copies at most, never three
    }
    return iris::Texture2D::createCubeMap(std::move(out));   // the six buffers become the texture's
}

}   // namespace imagedecode
