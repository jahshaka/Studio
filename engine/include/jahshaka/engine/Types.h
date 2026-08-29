#pragma once
// Engine-neutral value types. NOTHING here may reference Ogre, Qt or GL.
#include <cstddef>
#include <string>
#include <vector>

namespace jahshaka { namespace engine {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

struct Colour {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    Colour() = default;
    Colour(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
};

/// Native window handle a View renders into (X11 Window / HWND / NSView).
using NativeWindowHandle = unsigned long long;

/// Native display connection (X11 Display*). MUST be the host's own connection —
/// opening a second connection to the same windows causes flicker and cross-bleed
/// between windows. 0 where the platform has no such concept.
///
/// KNOWN LEAK (audit): this is an X11 concept in a supposedly platform-neutral
/// boundary. Left as-is for now; it is only consumed by on-screen Views on
/// Linux/Vulkan and is redesigned when the macOS/Windows hosts arrive.
using NativeDisplayHandle = unsigned long long;

/// Opaque handle to something in a Scene. 0 is "none". Ids are per-Scene and
/// monotonic: a removed node's id is NEVER reused, so a stale id is harmless.
using NodeId = unsigned int;

enum class Backend { Vulkan, OpenGL };

/// Everything the engine needs to start. All paths are resolved by the HOST at
/// runtime (next to the executable, an env override, or a compile-time default).
/// Nothing in the engine is baked to a build-machine path.
struct EngineConfig {
    Backend     backend = Backend::Vulkan;
    /// Directory holding the render-system plugins (RenderSystem_Vulkan.so ...).
    std::string pluginDir;
    /// Directory that CONTAINS the `Hlms/` folder (Hlms/Common, Hlms/Pbs, Hlms/Unlit).
    /// These shader templates are required at runtime, not optional sample data.
    std::string hlmsMediaDir;
    /// Log file path; empty means the backend's default name in the working directory.
    std::string logFile = "jahshaka-ogre.log";
    /// Host's display connection; required only for on-screen Views (see above).
    NativeDisplayHandle display = 0;
};

/// A CPU-side RGBA8 image, used to read back an offscreen View.
struct Image {
    unsigned width = 0, height = 0;
    std::vector<unsigned char> rgba;   // width*height*4, row-major, top-left origin
    /// Pixel accessor; returns {0,0,0,0} if out of range.
    Colour at(unsigned x, unsigned y) const {
        if (x >= width || y >= height) return Colour(0, 0, 0, 0);
        const size_t i = (static_cast<size_t>(y) * width + x) * 4u;
        return Colour(rgba[i] / 255.0f, rgba[i+1] / 255.0f, rgba[i+2] / 255.0f, rgba[i+3] / 255.0f);
    }
};

}}  // namespace jahshaka::engine
