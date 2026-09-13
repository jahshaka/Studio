// PHOTON-S2 SPIKE — THE LEAK ROOM. Not a gate suite: a measuring instrument.
//
// A closed room with ONE lamp inside and ONE lamp outside, built four times at
// four wall thicknesses. The outside lamp must not light the inside; what this
// prints is how much of it arrives anyway, split into the two ways it can:
//
//   direct   (GI off, outside lamp on)  — the shadow map's own leak
//   indirect (GI on,  outside lamp on)  — the GI leak, which is what the SDF
//                                         visibility arm is supposed to close
//
// Run it twice, JAH_SDF_VIS=0 and JAH_SDF_VIS=1, and diff the tables.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace jahshaka::engine;

static void render(Engine *e, int frames = 8) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

static NodeId addSlab(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale,
                      const Colour &emissive = Colour(0, 0, 0))
{
    const NodeId node = s->createNode();
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    PbrParams p;
    p.albedo = albedo;
    p.metalness = 0.0f;
    p.roughness = 0.9f;
    p.emissive = emissive;
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    s->setNodeTransform(node, pos, Quat(), scale);
    return node;
}

struct Arm { float wall; float directR; float giOffR; float lampOffR; float lampOnR; float lampOnG; };

/// Mean of a centred block, so one texel of noise cannot move a number.
static void meanRG(const Image &img, float &r, float &g)
{
    double sr = 0.0, sg = 0.0; int n = 0;
    for (unsigned y = 40; y < 88; ++y)
        for (unsigned x = 40; x < 88; ++x) { const Colour c = img.at(x, y); sr += c.r; sg += c.g; ++n; }
    r = float(sr / n); g = float(sg / n);
}

int main(int argc, char **argv)
{
    const float thicknesses[4] = { 0.5f, 0.2f, 0.1f, 0.05f };
    const int frames = argc > 1 ? std::atoi(argv[1]) : 10;

    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-photon-s2-leak-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("leak", 128, 128, Colour(0, 0, 0));
    // Offscreen views ship with shadows OFF; without them the outside lamp lights
    // the inside straight through the wall and there is no GI measurement to make.
    view->setShadows(true);

    Arm arms[4];
    for (int a = 0; a < 4; ++a) {
        const float T = thicknesses[a];
        Scene *s = engine->createScene("leak" + std::to_string(a));
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

        // Interior: x,z in [-5,5], y in [0,4]. Walls of thickness T sit OUTSIDE
        // that box, so the interior is identical in all four arms.
        const Colour white(0.8f, 0.8f, 0.8f);
        const float ho = 5.0f + T * 0.5f;        // wall centre offset
        const float span = 10.0f + 2.0f * T;
        addSlab(s, white, Vec3(0, -T * 0.5f, 0), Vec3(span, T, span));            // floor
        addSlab(s, white, Vec3(0, 4.0f + T * 0.5f, 0), Vec3(span, T, span));      // ceiling
        addSlab(s, white, Vec3(0, 2, -ho), Vec3(span, 4.0f, T));                  // -Z: THE wall
        addSlab(s, white, Vec3(0, 2,  ho), Vec3(span, 4.0f, T));                  // +Z
        addSlab(s, white, Vec3(-ho, 2, 0), Vec3(T, 4.0f, span));                  // -X
        addSlab(s, white, Vec3( ho, 2, 0), Vec3(T, 4.0f, span));                  // +X

        // THE WITNESS PANEL: a thin slab 0.4 m in front of the leaking wall,
        // facing into the room. Its 8-probe cage lies entirely INSIDE the room
        // (probe spacing is ~0.4 m), so anything red on it cannot be blamed on
        // a cage that straddles the wall — which is what separates "the probes
        // gathered red" from "the shaded point's cage reaches through the wall".
        addSlab(s, white, Vec3(0.0f, 2.0f, -4.6f), Vec3(4.0f, 3.0f, 0.06f));

        // The inside lamp: blue, so the measured wall is visible and nothing
        // inside the room can be mistaken for the outside one's colour.
        const NodeId inside = s->createNode();
        s->setNodeTransform(inside, Vec3(0.0f, 3.0f, 2.5f), Quat(), Vec3(1, 1, 1));
        LightDesc li;
        li.type = LightType::Point;
        li.colour = Colour(0.0f, 1.0f, 0.0f);   // PURE GREEN: the red channel is then 100% outside lamp
        li.intensity = 3.0f;
        li.range = 14.0f;
        li.castShadows = true;
        s->setLight(inside, li);

        // The outside lamp: RED, just beyond the -Z wall, aimed at nothing in
        // particular — a point light one metre off the outer face.
        const NodeId outside = s->createNode();
        s->setNodeTransform(outside, Vec3(0.0f, 2.0f, -(ho + T * 0.5f + 1.0f)), Quat(), Vec3(1, 1, 1));
        LightDesc lo;
        lo.type = LightType::Point;
        lo.colour = Colour(1.0f, 0.0f, 0.0f);
        lo.intensity = 25.0f;
        lo.range = 12.0f;
        lo.castShadows = true;
        s->setLight(outside, lo);

        // The camera looks at the inner face of the -Z wall from inside.
        // Off to +X, so the witness panel (x in [-2,2]) is not in the shot: this
        // camera must see the WALL ITSELF.
        enginetest::testCameraLookAt(view, Vec3(3.6f, 2.0f, 1.0f), Vec3(3.6f, 2.0f, -5.0f));

        Image img;
        // (1) GI OFF, outside lamp ON: whatever arrives is direct — i.e. the
        //     shadow map's leak, the floor under every other number here.
        render(engine.get(), frames);
        view->readPixels(img);
        float directR = 0.0f, directG = 0.0f; meanRG(img, directR, directG);
        const Colour direct = Colour(directR, directG, 0.0f);

        GiParams gi;
        gi.mode = GiMode::Vct;
        const char *q = std::getenv("JAH_S2_QUALITY");
        gi.quality = (q && std::string(q) == "medium") ? GiQuality::Medium : GiQuality::High;
        const char *dd = std::getenv("JAH_S2_DDGI");
        gi.ddgi = (dd && std::atoi(dd) == 0) ? GiToggle::Off : GiToggle::On;
        gi.numBounces = 1;
        gi.boundsMin = Vec3(-6.5f, -1.0f, -6.5f);
        gi.boundsMax = Vec3( 6.5f,  5.5f,  6.5f);
        if (!s->setGlobalIllumination(gi))
            std::printf("   engine error: %s\n", engine->lastError().c_str());
        render(engine.get(), frames);
        view->readPixels(img);
        float onR = 0.0f, onG = 0.0f; meanRG(img, onR, onG);
        const Colour lampOn = Colour(onR, onG, 0.0f);

        // (2) the same picture with the outside lamp DARK: the baseline the
        //     leak is measured against.
        lo.intensity = 0.0f;
        s->setLight(outside, lo);
        s->refreshGlobalIllumination();
        render(engine.get(), frames);
        view->readPixels(img);
        float offR = 0.0f, offG = 0.0f; meanRG(img, offR, offG);
        const Colour lampOff = Colour(offR, offG, 0.0f);

        // A SECOND PROBE POINT: the floor at the room's centre, 5 m from the
        // wall the light leaks through. If the leak lives only at the wall it is
        // the probe CAGE straddling that wall; if it is everywhere, the probes
        // themselves gathered red.
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.6f, 0.6f), Vec3(0.0f, 0.0f, 0.0f));
        lo.intensity = 25.0f; s->setLight(outside, lo); s->refreshGlobalIllumination();
        render(engine.get(), frames);
        view->readPixels(img);
        float floorOnR = 0.0f, floorOnG = 0.0f; meanRG(img, floorOnR, floorOnG);
        lo.intensity = 0.0f; s->setLight(outside, lo); s->refreshGlobalIllumination();
        render(engine.get(), frames);
        view->readPixels(img);
        float floorOffR = 0.0f, floorOffG = 0.0f; meanRG(img, floorOffR, floorOffG);
        std::printf("   floor centre: lampOn r=%.4f g=%.4f  lampOff r=%.4f  LEAK=%.4f\n",
                    double(floorOnR), double(floorOnG), double(floorOffR),
                    double(floorOnR - floorOffR));

        // The witness panel, lamp on then off.
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, -2.0f), Vec3(0.0f, 2.0f, -4.6f));
        lo.intensity = 25.0f; s->setLight(outside, lo); s->refreshGlobalIllumination();
        render(engine.get(), frames);
        view->readPixels(img);
        float panOnR = 0.0f, panOnG = 0.0f; meanRG(img, panOnR, panOnG);
        lo.intensity = 0.0f; s->setLight(outside, lo); s->refreshGlobalIllumination();
        render(engine.get(), frames);
        view->readPixels(img);
        float panOffR = 0.0f, panOffG = 0.0f; meanRG(img, panOffR, panOffG);
        std::printf("   witness panel (0.4 m off the wall): lampOn r=%.4f g=%.4f  LEAK=%.4f\n",
                    double(panOnR), double(panOnG), double(panOnR - panOffR));

        const GiStatus st = s->giStatus();
        std::printf("[wall %.2f m] voxel %.4f m  ifdBound=%s probes=%d  "
                    "direct r=%.4f | GI lampOn r=%.4f g=%.4f b=%.4f | lampOff r=%.4f\n",
                    double(T), double(st.voxelMetres), st.ifdBound ? "y" : "n", st.ifdProbes,
                    double(direct.r), double(lampOn.r), double(lampOn.g), double(lampOn.b),
                    double(lampOff.r));
        arms[a] = Arm{ T, direct.r, 0.0f, lampOff.r, lampOn.r, lampOn.g };
        engine->destroyScene(s);
    }

    std::printf("\n wall(m)  direct_r   lampOff_r  lampOn_r   GI_LEAK(r)  leak/green\n");
    for (int a = 0; a < 4; ++a)
        std::printf("  %5.2f   %8.4f   %8.4f   %8.4f   %8.4f   %8.4f\n", double(arms[a].wall),
                    double(arms[a].directR), double(arms[a].lampOffR), double(arms[a].lampOnR),
                    double(arms[a].lampOnR - arms[a].lampOffR),
                    double((arms[a].lampOnR - arms[a].lampOffR) / std::max(0.001f, arms[a].lampOnG)));
    const char *v = std::getenv("JAH_SDF_VIS");
    std::printf("\nJAH_SDF_VIS=%s\n", v ? v : "(unset)");
    return 0;
}
