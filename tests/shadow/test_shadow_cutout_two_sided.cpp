// shadow.cutout_two_sided — A THIN CUTOUT CARD STOPS THE SUN FROM BOTH SIDES (BISTRO-FIXES-3 stage 2).
//
// A leaf card is a one-sided, alpha-tested quad. Its COLOUR pass culls the back face (the
// card is seen from its front only), but a card has no inside: whichever face the sun
// sees, the card is between the sun and what lies behind it. The shadow caster's
// macroblock copies the material's (OgreHlmsDatablock::setMacroblock), so a card whose
// front faces AWAY from the sun was culled in the caster pass and cast nothing — the sun
// leaked through it onto the leaves behind (the Bistro's partly lit foliage).
//
// The fixture: a sun travelling (0, -0.6, -0.8); a matte receiver wall at z = -2 facing the
// sun; a 2 x 2 m card at z = 3 (its shadow centred on the wall at y 1) whose albedo map is
// opaque on its left half and fully transparent on its right (Cutout, cutoff 0.5); a camera
// between the card and the wall at z = 1, looking at the wall (the card is behind it, never
// in the picture). No sky, no ambient, no GI: the wall is the sun's
// direct light and its shadow only. Scene radiance, float readback.
//   NO CARD      — the wall fully lit: L(px).
//   FACING       — the card's front toward the sun: the stock caster casts; the shadow's
//                  texels (below half of L) are the MASK; its lit texels the cut half.
//   AWAY         — the same card turned to face away from the sun. THE BAR: 0 texels of
//                  the mask lit (above half of L) - the card stops the sun from both sides -
//                  and the cut half's texels lit as with the card facing (the cutout cuts).
//   OPAQUE AWAY  — an OPAQUE one-sided card facing away: a closed shape's back face, which
//                  this stage leaves as it is - it casts nothing (its texels in the mask lit).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

// A 2 x 2 m quad in the XY plane facing +z (front = counter-clockwise seen from +z),
// or -z when `faceNegZ`. UVs 0..1 left to right as seen from its FRONT is not needed:
// the albedo's opaque half is the u < 0.5 half, u running with +x.
static MeshData quad(bool faceNegZ)
{
    MeshData d;
    const float h = 1.0f;
    const float nz = faceNegZ ? -1.0f : 1.0f;
    d.positions = { -h, -h, 0.0f,  h, -h, 0.0f,  h, h, 0.0f,  -h, h, 0.0f };
    d.normals = { 0, 0, nz,  0, 0, nz,  0, 0, nz,  0, 0, nz };
    d.uvs = { 0, 1,  1, 1,  1, 0,  0, 0 };
    if (faceNegZ) d.indices = { 0, 2, 1, 0, 3, 2 };
    else          d.indices = { 0, 1, 2, 0, 2, 3 };
    return d;
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-shadow-cutout-two-sided-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    const unsigned W = 128, H = 128;
    View *v = e->createOffscreenView("cutout2s", W, H, Colour(0, 0, 0));
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;
    fx.hdr = false;
    fx.hdrReadback = true;
    v->setPostFx(fx);
    v->setShadows(true);

    const auto shoot = [&](int card /* 0 none, 1 cutout facing, 2 cutout away, 3 opaque away */) {
        std::vector<float> lum(W * H, 0.0f);
        Scene *s = e->createScene("cutout2s");
        if (!v->setScene(s)) { std::printf("FAIL: setScene\n"); ++failures; return lum; }
        SkyDesc none; none.mode = SkyMode::NoSky;
        s->setSky(none);
        s->setEnvironmentLight(Colour(0, 0, 0, 1));
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        GiParams off; off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        enginetest::addDirectionalLight(s, Vec3(0.0f, -0.6f, -0.8f), 3.14159265f);
        PbrParams wallP; wallP.albedo = Colour(0.5f, 0.5f, 0.5f); wallP.roughness = 1.0f;
        const NodeId wall = s->createNode();
        s->attachMesh(wall, s->createMesh(enginetest::unitCubeMesh()), s->createPbrMaterial(wallP));
        s->setNodeTransform(wall, Vec3(0.0f, 1.0f, -2.05f), Quat(), Vec3(8.0f, 8.0f, 0.1f));
        if (card) {
            // 16 x 16: alpha 255 on u < 0.5, 0 beyond.
            std::vector<unsigned char> px(16 * 16 * 4);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    unsigned char *p = &px[(y * 16 + x) * 4];
                    p[0] = 200; p[1] = 200; p[2] = 200; p[3] = x < 8 ? 255 : 0;
                }
            PbrParams cp; cp.albedo = Colour(1, 1, 1); cp.roughness = 1.0f;
            cp.alphaMode = card == 3 ? PbrAlphaMode::Opaque : PbrAlphaMode::Cutout;
            cp.alphaCutoff = 0.5f;
            const MaterialId m = s->createPbrMaterial(cp);
            if (card != 3) s->setPbrTexture(m, PbrTextureSlot::Albedo, s->createTexture(16, 16, px.data(), true));
            const NodeId n = s->createNode();
            s->attachMesh(n, s->createMesh(quad(card != 1)), m);
            s->setNodeTransform(n, Vec3(0.0f, 4.75f, 3.0f), Quat(), Vec3(1, 1, 1));
        }
        enginetest::testCameraLookAt(v, Vec3(0.0f, 1.0f, 8.0f), Vec3(0.0f, 1.0f, -2.0f));
        for (int f = 0; f < 30; ++f) e->renderOneFrame();
        ImageF img;
        if (!v->readPixelsHdr(img)) { std::printf("FAIL: readPixelsHdr\n"); ++failures; }
        else
            for (unsigned y = 0; y < H; ++y)
                for (unsigned x = 0; x < W; ++x) {
                    const Colour c = img.at(x, y);
                    lum[y * W + x] = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
                }
        v->setScene(nullptr);
        e->destroyScene(s);
        return lum;
    };

    const std::vector<float> lit = shoot(0);
    const std::vector<float> facing = shoot(1);
    const std::vector<float> away = shoot(2);
    const std::vector<float> opaqueAway = shoot(3);

    // THE MASK: the texels the facing card shadows (below half the lit value), ONE TEXEL IN
    // from the mask's edge (the PCF filter's ramp is the same either way the card faces).
    // THE CUT: texels the facing card leaves lit that lie beside the mask (the transparent
    // half's shadow footprint is unknowable without the card; the lit texels of the facing
    // arm stand for it).
    const auto in = [&](const std::vector<float> &a, unsigned x, unsigned y) {
        return a[y * W + x] < 0.5f * lit[y * W + x];
    };
    unsigned mask = 0, leakAway = 0, leakOpaque = 0, litTexels = 0, cutLostAway = 0;
    for (unsigned y = 1; y + 1 < H; ++y)
        for (unsigned x = 1; x + 1 < W; ++x) {
            if (!(lit[y * W + x] > 1e-3f)) continue;
            ++litTexels;
            bool core = true;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    core = core && in(facing, x + dx, y + dy);
            if (core) {
                ++mask;
                if (!in(away, x, y)) ++leakAway;
                if (!in(opaqueAway, x, y)) ++leakOpaque;
            } else if (!in(facing, x, y) && in(away, x, y)) {
                ++cutLostAway;   // lit with the card facing, shadowed with it turned
            }
        }
    {
        unsigned nf = 0, na = 0, no = 0;
        float lmax = 0.0f;
        for (unsigned i = 0; i < W * H; ++i) {
            lmax = std::max(lmax, lit[i]);
            if (lit[i] > 1e-3f) {
                nf += facing[i] < 0.5f * lit[i];
                na += away[i] < 0.5f * lit[i];
                no += opaqueAway[i] < 0.5f * lit[i];
            }
        }
        std::printf("   shadowed texels (below half the lit wall): the cutout facing %u, facing away %u, the opaque "
                    "card facing away %u (the lit wall's peak %.4f)\n", nf, na, no, lmax);
    }
    std::printf("   wall texels lit %u; the facing cutout's shadow core %u texels; lit inside it: the cutout "
                "facing away %u, the opaque card facing away %u; texels lit facing but shadowed away %u\n",
                litTexels, mask, leakAway, leakOpaque, cutLostAway);
    char msg[260];
    CHECK(mask > 200, "the facing cutout casts a shadow core on the wall (> 200 texels)");
    std::snprintf(msg, sizeof msg, "A CUTOUT CARD FACING AWAY FROM THE SUN casts the same shadow: 0 of the core's %u "
                  "texels lit (%u)", mask, leakAway);
    CHECK(leakAway == 0, msg);
    std::snprintf(msg, sizeof msg, "...and its cut half stays cut: no texel the facing card leaves lit is shadowed by "
                  "it turned (%u; the PCF edge allows 2 %% of the core, %u)", cutLostAway, mask / 50);
    CHECK(cutLostAway <= mask / 50, msg);
    std::snprintf(msg, sizeof msg, "AN OPAQUE ONE-SIDED CARD facing away keeps the stock rule (a closed shape's back "
                  "face casts nothing): %u of %u core texels lit", leakOpaque, mask);
    CHECK(leakOpaque * 10 >= mask * 9, msg);

    e->destroyView(v);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
