#ifndef MEDIAHARDWARE_H
#define MEDIAHARDWARE_H

#include <string>

// QT MULTIMEDIA'S HARDWARE BACKEND, NAMED FROM THE ENGINE'S ADAPTER (SC1-NEWPROJECT-STALL fix round,
// lead 2026-10-10).
//
// Qt's FFmpeg backend, when it is first constructed, CHECKS every hardware-context type it was built
// with — measured on the reference box: vdpau, cuda, vaapi, qsv and vulkan contexts opened and closed,
// 731 ms ("Device types checked"), plus a second Vulkan device beside ours. That check was the
// session's first-record-click hitch (VIDEO-REC-1's 585 ms) and, while a project open/create made the
// probe, most of threading.newproject_stall's 1,040 ms. Given the types by name
// (QT_FFMPEG_ENCODING_HW_DEVICE_TYPES / QT_FFMPEG_DECODING_HW_DEVICE_TYPES) it trusts them and checks
// nothing: 20-34 ms.
//
// So the APP names the one backend the machine has, derived from the adapter the engine actually runs
// on, the moment that adapter is known (EngineHost, after the device block) and always before Qt
// Multimedia is first constructed (nothing media-related runs at boot: app.startup_quiet):
//   NVIDIA -> cuda (NVENC / NVDEC); any other adapter -> vaapi; macOS -> videotoolbox;
//   Windows: NVIDIA -> cuda, else d3d11va.
// Never a user setting. A variable already present in the environment is left alone (a developer's
// explicit override); an unknown adapter (the NULL render system) names nothing.

namespace mediahardware {

/// Names the hardware backend for `vendor` (Ogre's RenderSystemCapabilities vendor string, as
/// jahshaka::engine::DeviceInfo carries it). Returns the type named, or empty when none was.
std::string pinFor(const std::string &vendor);

}   // namespace mediahardware

#endif // MEDIAHARDWARE_H
