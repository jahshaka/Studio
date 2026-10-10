#include "services/mediahardware.h"

#include <QByteArray>
#include <QtGlobal>
#include <algorithm>
#include <cctype>

namespace mediahardware {

std::string pinFor(const std::string &vendorIn)
{
    std::string vendor = vendorIn;
    std::transform(vendor.begin(), vendor.end(), vendor.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (vendor.empty() || vendor == "unknown") return std::string();
    const bool nvidia = vendor.find("nvidia") != std::string::npos;
#if defined(Q_OS_MACOS)
    Q_UNUSED(nvidia);
    const char *type = "videotoolbox";
#elif defined(Q_OS_WIN)
    const char *type = nvidia ? "cuda" : "d3d11va";
#else
    const char *type = nvidia ? "cuda" : "vaapi";
#endif
    for (const char *name : { "QT_FFMPEG_ENCODING_HW_DEVICE_TYPES", "QT_FFMPEG_DECODING_HW_DEVICE_TYPES" })
        if (!qEnvironmentVariableIsSet(name)) qputenv(name, QByteArray(type));
    return type;
}

}   // namespace mediahardware
