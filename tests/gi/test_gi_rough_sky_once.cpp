// gi.rough_sky_once — THE SKY IS COUNTED ONCE ON A ROUGH LOBE (BISTRO-FIXES-3 stage 1).
//
// A lobe wider than tan 1 (perceptual roughness above ~0.515) is not marched as a
// cone: its march is an OCCLUSION QUERY along the reflection (PhotonVct_piece_ps.any,
// computeVctProbe: vctRoughOcclusion = the share the voxels stopped) and the
// stopped share is lit by what the query stopped on - the march's own colour,
// vctRoughVoxel, added by applyVctRoughSpecular (BISTRO-FIXES-3; the diffuse set's
// voxel light stood for it before). Where the material carries the sky's environment cube
// (the bound-cube path: use_envprobe_map without parallax-corrected cubemaps)
// HlmsPbs's CubemapGlobal reads that cube; it must read it only for the share the
// query let through (Lagarde & de Rousiers 2014's specular occlusion: at roughness
// 0.74 the visibility IS the occlusion's complement), or the occluded share is lit
// twice — the full sky AND the voxels' light (the Bistro's blue-white specks on
// shaded leaves).
//
// The fixture: a white rough (0.74) METAL plate (no diffuse: the pixel is the
// specular environment alone), a camera straight above it, a UNIFORM sky of
// radiance L (a 1x1 equirect: its cube is the material's environment cube), the
// cones (Low, one pinned cascade) and the field, no light, scene radiance read back
// as float.
//   OPEN — the plate under the open sky. Nothing above it: the query stops nothing
//     and the picture is the base's bit for bit (printed as hex floats; the A/B is
//     the stage's evidence, the row asserts the physics below).
//   THE UNIFORM ENCLOSURE — the same plate in a closed 9 m box with one 3 x 3 m
//     opening in its ceiling, every inner face EMITTING L (black albedo: no
//     bounce). Every direction the lobe can take carries radiance L, through the
//     opening or off a wall, so the plate reads EXACTLY what it reads under the
//     open sky (the furnace's closed form, gi.env_cone's sealed-room arm). THE
//     ENERGY BAR: the sky's share plus the voxels' share never exceeds the open
//     value. The base reads the full sky PLUS the voxels' share (red).
//     Tolerance 3 %: gi.env_cone's furnace bar and its reasons (the four cones'
//     coverage of the box's corners at Low's cells, the half-float store).
//   THE SHADED PLATE — the same box, its faces black and dark. The plate sees the
//     sky through the opening only: the reading is the open value times the
//     query's visibility, strictly inside (0, open), and the share the query
//     stopped is dark (the uniform enclosure's reading minus this one is the
//     voxels' share, which the walls' L supplies).
//   OPEN, GRAZING — the open plate seen 10 degrees above its surface: the query's cone
//     reaches below the surface, a share the BRDF's masking already holds (no light
//     arrives from below the horizon) - not the sky's occlusion: the GI-off value.
// Both the cones and the field are asserted.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-rough-sky-once-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();

    View *rv = e->createOffscreenView("roughsky", 64, 64, Colour(0, 0, 0));
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;
    fx.hdr = false;
    fx.hdrReadback = true;
    rv->setPostFx(fx);
    // The centre 8 x 8 of the plate, four frames: scene radiance (float readback).
    const auto readHdr = [&](View *v) {
        double acc = 0.0;
        for (int f = 0; f < 4; ++f) {
            enginetest::frames(e, 1);
            ImageF img;
            if (!v->readPixelsHdr(img)) return -1.0;
            double s2 = 0.0; int n = 0;
            for (unsigned y = 28; y < 36; ++y)
                for (unsigned x = 28; x < 36; ++x) {
                    const Colour c = img.at(x, y);
                    s2 += 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; ++n;
                }
            acc += s2 / n;
        }
        return acc / 4.0;
    };
    const unsigned char kSkyByte = 128;
    const double L = std::pow((kSkyByte / 255.0 + 0.055) / 1.055, 2.4);   // the byte's linear radiance
    PbrParams metal; metal.albedo = Colour(1, 1, 1); metal.metalness = 1.0f; metal.roughness = 0.74f;

    GiParams cones;
    cones.mode = GiMode::Vct;
    cones.quality = GiQuality::Low;
    cones.numBounces = 1;
    cones.ddgi = GiToggle::Off;
    cones.gather = GiToggle::Off;
    // ONE PINNED CASCADE, 9 m at 64 (gi.env_cone's furnace cell): it holds the whole box.
    cones.cascadeCount = 1;
    cones.cascadeSet[0] = GiParams::GiCascadeDesc{ 4.5f, 64, 0.0f };
    GiParams field = cones;
    field.ddgi = GiToggle::On;
    GiParams off; off.mode = GiMode::Off;

    // One scene: the uniform sky, the plate, and (when `box`) the box with its opening.
    struct Arm { double off = -1.0, cones = -1.0, field = -1.0; };
    const auto runScene = [&](const char *name, int box /* 0 open, 1 emitting L, 2 dark */,
                              bool grazing) {
        Arm a;
        Scene *s = e->createScene(name);
        char m[160];
        std::snprintf(m, sizeof m, "the view shows the %s scene", name);
        CHECK(rv->setScene(s), m);
        const unsigned char skyPx[4] = { kSkyByte, kSkyByte, kSkyByte, 255 };
        SkyDesc sky;
        sky.mode = SkyMode::Equirectangular;
        sky.equirect = s->createTexture(1, 1, skyPx, true);
        CHECK(sky.equirect && s->setSky(sky), "the uniform sky binds");
        float sh[27] = { 0 };
        for (int f = 0; f < 20 && !s->published().copySkyAmbientSh(sh); ++f) enginetest::frames(e, 1);
        s->setAmbientSh(sh);
        s->setEnvironmentLight(Colour(1, 1, 1, 1));
        const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
        const auto slab = [&](const Vec3 &pos, const Vec3 &scale, const PbrParams &p) {
            const NodeId nd = s->createNode();
            s->attachMesh(nd, cube, s->createPbrMaterial(p));
            s->setNodeTransform(nd, pos, Quat(), scale);
        };
        if (box) {
            PbrParams wall; wall.albedo = Colour(0, 0, 0); wall.roughness = 1.0f;
            if (box == 1) wall.emissive = Colour(float(L), float(L), float(L));
            slab(Vec3(0, -0.25f, 0), Vec3(9.0f, 0.5f, 9.0f), wall);
            slab(Vec3(-4.25f, 2.5f, 0), Vec3(0.5f, 5.0f, 9.0f), wall);
            slab(Vec3(4.25f, 2.5f, 0), Vec3(0.5f, 5.0f, 9.0f), wall);
            slab(Vec3(0, 2.5f, -4.25f), Vec3(9.0f, 5.0f, 0.5f), wall);
            slab(Vec3(0, 2.5f, 4.25f), Vec3(9.0f, 5.0f, 0.5f), wall);
            // the ceiling at y 5-5.5 with a 3 x 3 m opening over the plate's centre:
            // two 9 x 3 m strips (z beyond +-1.5) and two 3 x 3 m pieces (x beyond +-1.5)
            slab(Vec3(0, 5.25f, -3.0f), Vec3(9.0f, 0.5f, 3.0f), wall);
            slab(Vec3(0, 5.25f, 3.0f), Vec3(9.0f, 0.5f, 3.0f), wall);
            slab(Vec3(-3.0f, 5.25f, 0), Vec3(3.0f, 0.5f, 3.0f), wall);
            slab(Vec3(3.0f, 5.25f, 0), Vec3(3.0f, 0.5f, 3.0f), wall);
        }
        slab(Vec3(0.0f, 0.05f, 0.0f), Vec3(4.0f, 0.1f, 4.0f), metal);
        if (grazing)   // 0.45 m above the plate's top, 2.5 m off: the view 10 degrees above its surface
            enginetest::testCameraLookAt(rv, Vec3(0.0f, 0.55f, -2.5f), Vec3(0.0f, 0.1f, 0.0f));
        else
            enginetest::testCameraLookAt(rv, Vec3(0.02f, 3.0f, 0.02f), Vec3(0.0f, 0.0f, 0.0f));
        s->setGlobalIllumination(off);
        enginetest::frames(e, 10);
        a.off = readHdr(rv);
        s->setGlobalIllumination(cones);
        enginetest::frames(e, 20);
        for (int f = 0; f < 4000 && !s->published().gi.giAtRest; ++f) enginetest::frames(e, 1);
        a.cones = readHdr(rv);
        s->setGlobalIllumination(field);
        enginetest::frames(e, 20);
        for (int f = 0; f < 4000 && !s->published().gi.giAtRest; ++f) enginetest::frames(e, 1);
        a.field = readHdr(rv);
        s->setGlobalIllumination(off);
        rv->setScene(nullptr);
        e->destroyScene(s);
        std::printf("   %-9s GI off %.6f (%a)  cones %.6f (%a)  field %.6f (%a)\n", name, a.off, a.off,
                    a.cones, a.cones, a.field, a.field);
        return a;
    };

    const Arm open = runScene("open", 0, false);
    const Arm graze = runScene("grazing", 0, true);
    const Arm lit = runScene("enclosure", 1, false);
    const Arm dark = runScene("shaded", 2, false);
    std::printf("   ROUGH METAL (0.74), a uniform sky of L = %.4f: open %.4f / %.4f (cones / field); the uniform "
                "enclosure %.4f / %.4f (%.3fx / %.3fx); the shaded plate %.4f / %.4f (visibility %.3f / %.3f)\n", L,
                open.cones, open.field, lit.cones, lit.field, lit.cones / open.cones, lit.field / open.field,
                dark.cones, dark.field, dark.cones / open.cones, dark.field / open.field);

    char msg[260];
    CHECK(open.off > 0.01, "the open plate reads the uniform sky (GI off)");
    std::snprintf(msg, sizeof msg, "OPEN: nothing above the plate, the volume's plate reads what PBS reads outside it "
                  "within 1 %% (cones %.5f, field %.5f / %.5f)", open.cones, open.field, open.off);
    CHECK(std::fabs(open.cones / open.off - 1.0) < 0.01 && std::fabs(open.field / open.off - 1.0) < 0.01, msg);
    // A GRAZING VIEW OF THE OPEN PLATE: the lobe's share below the surface is no occlusion (the
    // split-sum's masking holds it), so the open sky stays whole (read as occlusion it took half).
    std::snprintf(msg, sizeof msg, "OPEN, GRAZING (10 degrees): the share of the lobe below the surface is not the "
                  "sky's occlusion - the volume's plate reads what PBS reads within 1 %% (cones %.5f, field %.5f / %.5f)",
                  graze.cones, graze.field, graze.off);
    CHECK(graze.off > 0.01 && std::fabs(graze.cones / graze.off - 1.0) < 0.01 &&
          std::fabs(graze.field / graze.off - 1.0) < 0.01, msg);
    // (ONE WALL IN THE REFLECTION - a wall of the sky's radiance filling a grazing lobe - is
    // PHOTON-MARCH-1's acceptance, tests/gi/test_gi_march_underread.cpp: it under-reads as the
    // cascade's cells shrink, the cone march's opacity uncorrected for its step.)
    // THE VISIBILITY ITSELF (the read's W2): the query is a 45-degree cone up the plate's
    // normal; the 3 x 3 m opening 4.9 m above the plate's top subtends 4 atan(ab / (d sqrt(a^2
    // + b^2 + d^2))) = 0.343 sr of the cone's 2 pi (1 - cos 45) = 1.840 sr: 0.187. THE BAR,
    // 0.06: at the ceiling the cone's footprint is about the cascade's whole extent (9.8 m
    // across, 9 m at 64 cells: its last mips), so the march reads the opening as a density
    // averaged over one or two texels - a third of the estimate.
    const double a = 1.5, d = 4.9;
    const double omegaOpening = 4.0 * std::atan(a * a / (d * std::sqrt(2.0 * a * a + d * d)));
    const double omegaCone = 2.0 * 3.14159265358979 * (1.0 - std::sqrt(0.5));
    const double visGeom = omegaOpening / omegaCone;
    for (int k = 0; k < 2; ++k) {
        const char *tag = k ? "the field" : "the cones";
        const double o = k ? open.field : open.cones;
        const double u = k ? lit.field : lit.cones;
        const double d = k ? dark.field : dark.cones;
        std::snprintf(msg, sizeof msg, "THE ENERGY BAR, %s: in the uniform enclosure the sky's share plus the voxels' "
                      "share is the open value, never more (%.4f / %.4f; bar 3 %%)", tag, u, o);
        CHECK(u <= o * 1.03 && u >= o * 0.97, msg);
        std::snprintf(msg, sizeof msg, "THE SHADED PLATE, %s: the sky only through the opening - the open value times "
                      "the query's visibility, strictly inside (0, open) (%.4f / %.4f)", tag, d, o);
        CHECK(d > 0.0 && d < 0.97 * o, msg);
        std::snprintf(msg, sizeof msg, "THE SHADED PLATE, %s: the visibility is the opening's share of the 45-degree "
                      "query (%.3f vs the solid angle's %.3f; bar 0.06)", tag, d / o, visGeom);
        CHECK(std::fabs(d / o - visGeom) <= 0.06, msg);
        std::snprintf(msg, sizeof msg, "THE SHADED PLATE, %s: the share the query stopped is the dark walls' (the "
                      "enclosure's reading above the shaded one, %.4f > 0)", tag, u - d);
        CHECK(u - d > 0.03 * o, msg);
    }

    e->destroyView(rv);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
