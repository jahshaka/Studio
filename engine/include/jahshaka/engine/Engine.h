#pragma once
// Jahshaka's engine abstraction.
//
// This is THE boundary between the application and the 3D engine. Studio talks
// only to these types. No Ogre type, header or symbol appears here — swapping the
// backend must not touch a single file under src/.
//
// Interface derived from what Studio DOES, not from what any engine offers.
#include <memory>
#include <string>
#include "Types.h"

namespace jahshaka { namespace engine {

class Scene;
class View;

/// A renderable scene. Views draw it; several Views may share one, or each may own one.
class Scene {
public:
    virtual ~Scene() = default;
    virtual void        setAmbient(const Colour &upper, const Colour &lower) = 0;
    virtual NodeId      addDirectionalLight(const Vec3 &direction, float power) = 0;
    /// Unit cube with a PBR metallic-roughness material. Placeholder until the
    /// asset pipeline lands; proves the material path end to end.
    virtual NodeId      addTestCube(const Colour &albedo, float metalness, float roughness) = 0;
    virtual void        setNodePosition(NodeId, const Vec3 &) = 0;
    virtual void        setNodeScale(NodeId, const Vec3 &) = 0;
    virtual void        rotateNode(NodeId, float yawRadians, float pitchRadians, float rollRadians) = 0;
};

/// A view onto a Scene, rendering into a native window supplied by the host.
class View {
public:
    virtual ~View() = default;
    virtual void setCameraPosition(const Vec3 &) = 0;
    virtual void lookAt(const Vec3 &) = 0;
    virtual void setRenderFlags(const RenderFlags &) = 0;
    virtual RenderFlags renderFlags() const = 0;
    virtual void resize(unsigned width, unsigned height) = 0;
};

/// Owns the device and every Scene and View. One per process.
class Engine {
public:
    virtual ~Engine() = default;

    /// Creates the engine. Returns null on failure and fills `error`.
    static std::unique_ptr<Engine> create(Backend, std::string &error);

    virtual Scene *createScene(const std::string &name) = 0;
    virtual View  *createView(const std::string &name, Scene *,
                              NativeWindowHandle, unsigned width, unsigned height,
                              const Colour &background) = 0;

    /// Draws every View once. The host owns the loop and calls this.
    virtual void renderOneFrame() = 0;

    virtual std::string backendName() const = 0;
};

}}  // namespace jahshaka::engine
