// THE APP's ONE Qt-CONVERSION HEADER (NOQT-CORE-1, V2 plan 0.12): where the editor converts between Qt's types and the
// standard types of the layers below it (paths, text, bytes, and what the document's own specs add). Nothing below src/app
// includes it - irisgl/ never does (tests/hygiene/no_qt_below.py holds that). Conversions only: no logic lives here.
#pragma once

#include "irisgl/core/files.h"

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace qtstd {

/// A QString path <-> a std::filesystem::path, the same on every platform: through UTF-8 and iris::files' u8path door
/// (a narrow std::string is not UTF-8 on Windows; QString::toStdString IS UTF-8).
inline std::filesystem::path toPath(const QString &p) { return iris::files::path(p.toStdString()); }
inline QString fromPath(const std::filesystem::path &p) { return QString::fromStdString(iris::files::utf8(p)); }

/// UTF-8 text below the editor -> a QString.
inline QString toQString(std::string_view utf8) { return QString::fromUtf8(utf8.data(), static_cast<qsizetype>(utf8.size())); }

/// A QByteArray's bytes as a view (valid while the array lives and is not changed).
inline std::string_view toView(const QByteArray &bytes)
{
    return std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

}   // namespace qtstd
