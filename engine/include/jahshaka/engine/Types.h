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

/// Rotation as a unit quaternion. Identity by default.
struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    Quat() = default;
    Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

/// Opaque handles to a Scene's meshes and materials. 0 is "none". Per-Scene and
/// monotonic like NodeId. A mesh or material may be shared by any number of nodes.
using MeshId     = unsigned int;
using MaterialId = unsigned int;

/// CPU-side triangle mesh, the shape the assimp importer produces at import time.
/// positions: xyz per vertex (required). normals: xyz per vertex (optional — smooth
/// normals are generated when empty). uvs: uv per vertex (optional). indices: three
/// per triangle (required).
struct MeshData {
    std::vector<float>    positions;
    std::vector<float>    normals;
    std::vector<float>    uvs;
    std::vector<float>    tangents;   // optional, xyzw per vertex (w = handedness);
                                      // generated from uvs when empty — needed for normal maps
    std::vector<unsigned> indices;
    size_t vertexCount() const { return positions.size() / 3; }
    size_t triangleCount() const { return indices.size() / 3; }
};

using TextureId = unsigned int;
enum class SkyMode { NoSky, Equirectangular, Cubemap };   // 'None' collides with X11's macro

/// PBR texture slots. NOTE: there is deliberately NO Occlusion slot — the Ogre
/// backend (HlmsPbs) has no dedicated ambient-occlusion input, so the document's
/// occlusionMap/occlusionFactor are documented as unsupported rather than faked
/// (bake AO into the base colour map at import time if it matters).
enum class PbrTextureSlot { Albedo, Normal, Metalness, Roughness, Emissive };

/// How PbrParams::alpha / alphaCutoff are interpreted (glTF's OPAQUE/MASK/BLEND).
enum class PbrAlphaMode {
    Opaque,   ///< alpha ignored
    Cutout,   ///< pixels whose albedo-texture alpha < alphaCutoff are discarded
    Blend     ///< the whole surface is alpha-blended by `alpha`
};

/// Metallic-roughness PBR parameters — Jahshaka's material model, sized to what
/// the backend's PBR pipeline can honour. Emissive arrives with any intensity
/// already folded in (colour * intensity). Roughness remap bounds are applied by
/// the CALLER as a clamp before filling `roughness` — the backend has no
/// per-texel remap. Texture maps bind separately via setPbrTexture().
struct PbrParams {
    Colour albedo   = Colour(0.8f, 0.8f, 0.8f);
    float  metalness = 0.0f;
    float  roughness = 0.6f;
    Colour emissive = Colour(0.0f, 0.0f, 0.0f);
    PbrAlphaMode alphaMode = PbrAlphaMode::Opaque;
    float  alpha       = 1.0f;   ///< Blend mode: 1 opaque .. 0 invisible
    float  alphaCutoff = 0.5f;   ///< Cutout mode threshold
    bool   twoSided    = false;  ///< draw and light both faces (no back-face culling)
    float  normalMapWeight = 1.0f;   ///< strength of the bound normal map
};

/// One camera-facing textured quad in a node's billboard set (Scene::setBillboards).
/// Positions are WORLD-space: the document simulates particles in world space and
/// the engine draws them as-is.
struct BillboardInstance {
    Vec3   position;                       ///< world-space centre of the quad
    float  size = 1.0f;                    ///< quad edge length in world units
    float  rotationRadians = 0.0f;         ///< spin around the view axis
    Colour colour = Colour(1.0f, 1.0f, 1.0f, 1.0f);   ///< multiplies the texture
};

enum class LightType { Directional, Point, Spot };

/// A light attached to a node. Direction comes from the node's orientation
/// (lights shine down the node's -Z), position from the node's transform.
struct LightDesc {
    LightType type = LightType::Point;
    Colour    colour = Colour(1.0f, 1.0f, 1.0f);
    float     intensity = 1.0f;        // radiometric scale (Jahshaka's "intensity")
    float     range = 10.0f;           // point/spot falloff distance
    float     spotAngleDegrees = 30.0f;    // outer cone
    float     spotSoftness = 0.1f;         // 0..1, inner = outer * (1 - softness)
    bool      castShadows = true;
};

/// A View's camera. Position/orientation are absolute (the document composes them).
struct CameraDesc {
    Vec3  position;
    Quat  orientation;                 // camera looks down its local -Z
    float fovDegrees = 45.0f;          // vertical
    float nearClip = 0.1f, farClip = 1000.0f;
    bool  orthographic = false;
    float orthoSize = 10.0f;           // vertical extent when orthographic
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
