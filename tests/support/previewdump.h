#ifndef TESTS_SUPPORT_PREVIEWDUMP_H
#define TESTS_SUPPORT_PREVIEWDUMP_H

// PIXEL-IDENTITY EVIDENCE for the preview surfaces (ENGINEERING_DEBT_SPEC
// item 6). EngineAssetScene, EngineMaterialPreviewScene, AvatarPreviewScene and
// EngineThumbnailRenderer all render offscreen through the raw path, which is
// deterministic: the same document, camera and size produce the same bytes.
// That makes a refactor of the shared lifecycle provable rather than arguable —
// run the suites with JAH_PREVIEW_DUMP=<dir> before and after, and compare
// sha256 sums.
//
// Unset (the normal case, including every ctest run) this costs one
// QString::isEmpty per picture and writes nothing.
//
// It lives here because five suites wanted the same twelve lines, which is the
// mistake this whole lane exists to stop making.

#include <QImage>
#include <QRegularExpression>
#include <QString>
#include <cstring>

#include "jahshaka/engine/Engine.h"

namespace previewdump {

/// Where the pictures go, or empty for "nowhere". Read once per process.
inline const QString &directory()
{
    static const QString dir = qEnvironmentVariable("JAH_PREVIEW_DUMP");
    return dir;
}

/// `<dir>/<serial>-<tag>.png`, serial per process so the ORDER of a suite's
/// pictures is part of the comparison and two shots of the same subject never
/// overwrite each other. Non-alphanumerics in the tag become underscores.
inline void save(const char *tag, const QImage &img)
{
    static int serial = 0;
    if (directory().isEmpty() || img.isNull()) return;
    QString name = QString::fromLatin1(tag);
    name.replace(QRegularExpression("[^A-Za-z0-9]+"), "_");
    img.save(QString("%1/%2-%3.png").arg(directory()).arg(++serial, 3, 10, QChar('0')).arg(name), "PNG");
}

/// The engine's raw RGBA8 readback, converted the one way the previews convert
/// it (EnginePreviewScene::toQImage) so a dumped picture is the picture the
/// editor would have shown.
inline void save(const char *tag, const jahshaka::engine::Image &img)
{
    if (directory().isEmpty() || !img.width || !img.height) return;
    QImage out(int(img.width), int(img.height), QImage::Format_RGBA8888);
    for (unsigned y = 0; y < img.height; ++y)
        std::memcpy(out.scanLine(int(y)), &img.rgba[size_t(y) * img.width * 4u], img.width * 4u);
    save(tag, out);
}

}   // namespace previewdump

#endif   // TESTS_SUPPORT_PREVIEWDUMP_H
