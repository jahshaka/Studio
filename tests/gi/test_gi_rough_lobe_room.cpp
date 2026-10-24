// gi.rough_lobe_room (PHOTON-7 stage 7's read, F4) - THE NO-RAY ROUTE'S PRODUCER AND ITS PIXEL DECIDE THE ROUGH
// LOBE FROM ONE ROUGHNESS: THE FOOTPRINT'S (the fork's geometric specular anti-aliasing widens a normal-mapped
// surface's alpha; the G-buffer carries the widened one).
//
// The screen integrate (JahScreenIntegrate_cs.glsl) reads the G-buffer's widened alpha; the pixel's read of its products
// (JahScreenCone_piece_ps.any) decided the rough lobe - the lobe wider than tan 1, whose occlusion query gates the sky
// cube (jahGlobalEnvVisibility) - from the material's unwidened roughness. Where the widening carried a lobe over tan 1
// the producer wrote the rough lobe's pair and the pixel read it as a narrow cone's: no occlusion, the sky's cube
// through the walls.
// THE FIXTURE: a closed room 4 x 2.6 x 4 m (walls, floor and ceiling 1 m boxes), a point light inside, a cubemap sky
// outside; the floor a normal-mapped metal (perceptual 0.50: under tan 1's 0.505, its map's detail near the pixel rate -
// the widening carries it over). High, rays off, a 256 x 256 still picture from inside looking down at the floor.
// THE LAW, against the same room's floor WITHOUT its map (its lobe narrow, 0.50 unwidened - the cube's reflection the
// pixel does not occlude at all: jahGlobalEnvVisibility gates the rough lobe only): under a sky ten times brighter the
// normal-mapped floor rises by less than HALF the bare floor's - its widened lobe read as the rough one it is, the cube
// through the voxels' let-through share alone. Before the fix it was read as a narrow cone's and rose by the cube's
// whole reflection (more than the bare floor: the widened lobe reflects more of the cube).
// PRINTED: both rises - the rough lobe's residual in a closed one-metre shell and the narrow lobe's unoccluded cube
// (REFLECT-1 / PCC-1's "occluded sky reflections inside rooms") are the record's, not this row's law.
// A fixture (the engine alone, one offscreen view). Counted in frames.

#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

namespace {
NodeId block(Scene *s, const Vec3 &at, const Vec3 &size, const PbrParams &p, MaterialId *matOut = nullptr) {
    const NodeId node = s->createNode();
    MeshData d = enginetest::unitCubeMesh();
    d.cards = enginetest::boxCards(0.5f);
    // ...WITH UVS, one 0..1 square a face (unitCubeMesh carries none: a normal map would read one texel everywhere and
    // its tangents be undefined) - the tangents follow from them
    for (int f = 0; f < 6; ++f) d.uvs.insert(d.uvs.end(), { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f });
    const MeshId mesh = s->createMesh(d);
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    if (matOut) *matOut = mat;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    enginetest::setNodePosition(s, node, at);
    enginetest::setNodeScale(s, node, size);
    return node;
}
PbrParams matte(float r, float g, float b) {
    PbrParams p;
    p.albedo = Colour(r, g, b);
    p.roughness = 0.85f;
    return p;
}
bool setSkyLevel(Scene *s, unsigned char r, unsigned char g, unsigned char b) {
    SkyDesc sky;
    sky.mode = SkyMode::Cubemap;
    std::vector<unsigned char> face(8 * 8 * 4);
    for (size_t k = 0; k < face.size(); k += 4) { face[k] = r; face[k + 1] = g; face[k + 2] = b; face[k + 3] = 255; }
    for (int f = 0; f < 6; ++f) sky.faces[f] = s->createTexture(8, 8, face.data(), true, false);
    return s->setSky(sky);
}
}   // namespace

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-rough-lobe-room-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->setRayTracing(false);
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    const auto run = [&](int n) { enginetest::frames(e, n); frames += n; };

    // the floor's tangent-space map: a texel-rate noise near the pixel rate, unmipped - the widening's kernel at its
    // cap nearly everywhere (a smooth pattern leaves its crests narrow)
    const unsigned kN = 256;
    std::vector<unsigned char> nm(kN * kN * 4);
    unsigned seed = 12345u;
    const auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / float(1u << 24) * 2.0f - 1.0f;
    };
    for (unsigned y = 0; y < kN; ++y)
        for (unsigned x = 0; x < kN; ++x) {
            const float fx = rnd() * 0.6f, fy = rnd() * 0.6f;
            const float nz = std::sqrt(std::max(0.0f, 1.0f - fx * fx - fy * fy));
            unsigned char *q = &nm[(y * kN + x) * 4];
            q[0] = (unsigned char)((fx * 0.5f + 0.5f) * 255.0f);
            q[1] = (unsigned char)((fy * 0.5f + 0.5f) * 255.0f);
            q[2] = (unsigned char)((nz * 0.5f + 0.5f) * 255.0f);
            q[3] = 255;
        }

    View *v = e->createOffscreenView("rough_lobe_room", 256, 256, Colour(0, 0, 0));
    if (!v) { std::printf("FAIL: the view\n"); return 1; }
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 1;
        fx.hdrReadback = true;
        v->setPostFx(fx);
    }
    v->setOffscreenContract(OffscreenContract::StillPicture);

    // ONE ROOM A RUN: mapped (the law) and bare (printed)
    double rise[2] = { -1.0, -1.0 }, base[2] = { -1.0, -1.0 };
    int rest[2] = { -1, -1 };
    for (int mapped = 1; mapped >= 0; --mapped) {
        Scene *s = e->createScene(mapped ? "rough_lobe_room" : "rough_lobe_room_bare");
        if (!s) { std::printf("FAIL: the scene\n"); return 1; }
        v->setScene(s);
        s->setRayTracing(RayTracingMode::Off);
        PbrParams metal;
        metal.albedo = Colour(0.9f, 0.9f, 0.9f);
        metal.metalness = 1.0f;
        metal.roughness = 0.50f;
        metal.uvScale[0] = 3.0f;
        metal.uvScale[1] = 3.0f;
        MaterialId floorMat = 0;
        // (walls, floor and ceiling ONE METRE thick - a dozen cells of cascade 0: the march's thin-wall leak,
        // VOXEL-LEAK-1, is not this row's subject)
        block(s, Vec3(0.0f, -0.5f, 0.0f), Vec3(6.0f, 1.0f, 6.0f), metal, &floorMat);              // the floor
        block(s, Vec3(0.0f, 3.1f, 0.0f), Vec3(6.0f, 1.0f, 6.0f), matte(0.7f, 0.7f, 0.7f));       // the ceiling
        block(s, Vec3(-2.5f, 1.3f, 0.0f), Vec3(1.0f, 2.6f, 6.0f), matte(0.7f, 0.6f, 0.5f));      // the walls
        block(s, Vec3(2.5f, 1.3f, 0.0f), Vec3(1.0f, 2.6f, 6.0f), matte(0.5f, 0.6f, 0.7f));
        block(s, Vec3(0.0f, 1.3f, -2.5f), Vec3(6.0f, 2.6f, 1.0f), matte(0.6f, 0.7f, 0.6f));
        block(s, Vec3(0.0f, 1.3f, 2.5f), Vec3(6.0f, 2.6f, 1.0f), matte(0.7f, 0.7f, 0.6f));
        if (mapped) {
            const TextureId normalTex = s->createTexture(kN, kN, nm.data(), false, false);
            if (!normalTex || !s->setPbrTexture(floorMat, PbrTextureSlot::Normal, normalTex)) {
                std::printf("FAIL: the floor's map\n");
                return 1;
            }
        }
        {
            const NodeId lamp = s->createNode();
            LightDesc l;
            l.type = LightType::Point;
            l.colour = Colour(1.0f, 0.95f, 0.9f);
            l.intensity = 4.0f;
            l.range = 12.0f;
            s->setNodeTransform(lamp, Vec3(0.6f, 2.0f, -0.8f), Quat(), Vec3(1, 1, 1));
            s->setLight(lamp, l);
        }
        if (!setSkyLevel(s, 20, 23, 30)) { std::printf("FAIL: the sky\n"); return 1; }
        enginetest::testCameraLookAt(v, Vec3(-1.2f, 1.9f, 1.4f), Vec3(0.3f, 0.0f, -0.6f));
        GiParams gi;
        gi.mode = GiMode::Vct;
        gi.quality = GiQuality::High;
        gi.bounces = true;
        if (!s->setGlobalIllumination(gi)) { std::printf("FAIL: the GI builds\n"); return 1; }
        e->waitForAsyncShaders();
        const auto settle = [&]() {
            int quiet = 0, f = 0;
            for (; f < 600 && quiet < 30; ++f) {
                run(1);
                const GiStatus g = s->published().gi;
                quiet = !g.chainOwed && !g.feedbackOwed && !g.cascades.empty() && g.giAtRest ? quiet + 1 : 0;
            }
            return quiet >= 30 ? f - quiet : -1;
        };
        const auto floorMean = [&]() {
            ImageF im;
            double sum = 0.0;
            unsigned n = 0;
            if (v->readPixelsHdr(im))
                for (unsigned y = 150; y < 240 && y < im.height; ++y)
                    for (unsigned x = 70; x < 190 && x < im.width; ++x) {
                        const Colour c = im.at(x, y);
                        sum += c.r + c.g + c.b;
                        ++n;
                    }
            return n ? sum / n : -1.0;
        };
        rest[mapped] = settle();
        base[mapped] = floorMean();
        if (!setSkyLevel(s, 200, 230, 255)) { std::printf("FAIL: the bright sky\n"); return 1; }
        e->waitForAsyncShaders();
        if (settle() < 0) rest[mapped] = -1;
        rise[mapped] = base[mapped] > 0.0 ? floorMean() / base[mapped] - 1.0 : -1.0;
        v->setScene(nullptr);
        e->destroyScene(s);
        run(2);
    }
    std::printf("   the normal-mapped floor (rest %d): %.5f, under a sky ten times brighter %+.2f %%; without its map (rest "
                "%d, the narrow lobe): %.5f, %+.2f %%\n", rest[1], base[1], 100.0 * rise[1], rest[0], base[0],
                100.0 * rise[0]);
    CHECK(rest[1] >= 0 && base[1] > 0.0, "premise: the mapped room at rest, its floor lit");
    CHECK(rest[0] >= 0 && rise[0] > 0.0 && rise[1] < 0.5 * rise[0],
          "under a sky ten times brighter the normal-mapped floor rises by less than half its bare twin's: the widened "
          "lobe is the rough one in the producer AND the pixel (occluded), never a narrow cone's (unoccluded)");
    e->destroyView(v);
    run(2);
    std::printf("\ngi.rough_lobe_room: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
