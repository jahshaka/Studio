#pragma once
// Engine-neutral value types. NOTHING here may reference Ogre, Qt or GL.
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
using NativeDisplayHandle = unsigned long long;

/// Opaque handle to something in a Scene. 0 is "none".
using NodeId = unsigned int;

enum class Backend { Vulkan, OpenGL };

/// Render flags — how a View draws its Scene. Lets the editor and the player
/// share one Scene and one window while drawing it differently.
struct RenderFlags {
    bool drawGrid        = true;
    bool drawGizmos      = true;
    bool drawWireframe   = false;
    bool drawSelection   = true;
};

}}  // namespace jahshaka::engine
