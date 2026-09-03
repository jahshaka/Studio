// Decals — pixel-asserted, headless, links JahshakaEngine ONLY (DECALS_SPEC.md).
//
// This suite is also where the phase-0 spike's findings live permanently. It
// PROVES, on the real Vulkan pin, the three things that were only derived from
// reading the Ogre source:
//
//   (a) the reserved-pool recipe works: a decal image resampled into our own
//       fixed-geometry Type2DArray pool renders;
//   (b) the UV convention — decalUV = localPos.xz + 0.5 — puts the image's
//       top-left quadrant at LOCAL (-X, -Z), i.e. upright and unmirrored under
//       an overhead camera, so no default 180 rotation and no upload-time flip
//       are needed;
//   (c) the -Y projection convention: a decal affects surfaces whose normal
//       points back at it and NOTHING when it is turned to face away — the same
//       convention as the document's lights.
//
// (The third phase-0 claim — that flipping decalsPerCell 0 -> 8 moves zero
// pixels in decal-free scenes — is a property of every OTHER suite in the tree,
// and was proven by byte-comparing a reference render built both ways.)
//
// Scene: a big mid-grey floor lit straight down by one directional light, an
// overhead camera looking down -Y. A decal above the floor paints its image on
// it; every assertion is a readback of that painted region.
//
// Runtime requirements are tests/engine's: a reachable DISPLAY and a Vulkan
// driver (lavapipe is fine).
#include "jahshaka/engine/Engine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static const unsigned kSize = 256;      // render target, square
static const float    kCamHeight = 6.0f;
static const float    kFovDeg = 45.0f;

// World half-extent visible at y == 0 for the overhead camera above.
static float visibleHalfExtent()
{
    return kCamHeight * std::tan(kFovDeg * 0.5f * 3.14159265358979f / 180.0f);
}

// World (x, z) on the floor plane -> pixel. The camera looks straight down -Y
// with its up axis on world -Z, so screen right = +X and screen DOWN = +Z
// (readPixels has a top-left origin).
static void worldToPixel(float wx, float wz, unsigned &px, unsigned &py)
{
    const float e = visibleHalfExtent();
    const float fx = (wx / e + 1.0f) * 0.5f * float(kSize);
    const float fy = (wz / e + 1.0f) * 0.5f * float(kSize);
    px = unsigned(std::min(std::max(fx, 0.0f), float(kSize - 1)));
    py = unsigned(std::min(std::max(fy, 0.0f), float(kSize - 1)));
}

static void render(Engine *e, int frames = 3)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static void show(const char *what, const Colour &c)
{
    std::printf("   %s: r=%.3f g=%.3f b=%.3f\n", what, c.r, c.g, c.b);
}

static Colour sampleAt(View *v, float wx, float wz)
{
    Image img;
    if (!v->readPixels(img)) return Colour(0, 0, 0, 0);
    unsigned px = 0, py = 0;
    worldToPixel(wx, wz, px, py);
    return img.at(px, py);
}

// ---------------------------------------------------------------------------
// A 32-bit uncompressed TGA with a top-left origin. Written to disk because
// loadDecalTexture takes a PATH: decal images are ordinary Texture assets, and
// the resample-into-the-atlas step is exactly what is under test.
static bool writeTga(const std::string &path, unsigned w, unsigned h,
                     const std::vector<unsigned char> &rgba)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    unsigned char hdr[18];
    std::memset(hdr, 0, sizeof(hdr));
    hdr[2] = 2;                                  // uncompressed true-colour
    hdr[12] = (unsigned char)(w & 0xFF);  hdr[13] = (unsigned char)(w >> 8);
    hdr[14] = (unsigned char)(h & 0xFF);  hdr[15] = (unsigned char)(h >> 8);
    hdr[16] = 32;                                // bits per pixel
    hdr[17] = 0x28;                              // top-left origin, 8 alpha bits
    std::fwrite(hdr, 1, sizeof(hdr), f);
    std::vector<unsigned char> bgra(size_t(w) * h * 4u);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        bgra[i * 4 + 0] = rgba[i * 4 + 2];
        bgra[i * 4 + 1] = rgba[i * 4 + 1];
        bgra[i * 4 + 2] = rgba[i * 4 + 0];
        bgra[i * 4 + 3] = rgba[i * 4 + 3];
    }
    std::fwrite(bgra.data(), 1, bgra.size(), f);
    std::fclose(f);
    return true;
}

static bool writeSolidTga(const std::string &path, unsigned char r, unsigned char g,
                          unsigned char b, unsigned char a = 255)
{
    const unsigned n = 64;
    std::vector<unsigned char> px(size_t(n) * n * 4u);
    for (size_t i = 0; i < size_t(n) * n; ++i) {
        px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = a;
    }
    return writeTga(path, n, n, px);
}

/// Four solid quadrants: TL red, TR green, BL blue, BR white. The whole point
/// of (b): where each quadrant lands on screen IS the UV convention.
static bool writeQuadrantTga(const std::string &path)
{
    const unsigned n = 64;
    std::vector<unsigned char> px(size_t(n) * n * 4u);
    for (unsigned y = 0; y < n; ++y)
        for (unsigned x = 0; x < n; ++x) {
            unsigned char *p = &px[(size_t(y) * n + x) * 4u];
            const bool left = x < n / 2, top = y < n / 2;
            p[0] = (top && left) ? 255 : (!top && !left ? 255 : 0);   // R: TL + BR
            p[1] = (top && !left) ? 255 : (!top && !left ? 255 : 0);  // G: TR + BR
            p[2] = (!top && left) ? 255 : (!top && !left ? 255 : 0);  // B: BL + BR
            p[3] = 255;
        }
    return writeTga(path, n, n, px);
}

static MeshData boxMesh(float hx, float hy, float hz)
{
    MeshData d;
    const float fn[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    const float fv[6][4][3] = {
        {{-hx,-hy, hz},{ hx,-hy, hz},{ hx, hy, hz},{-hx, hy, hz}},
        {{ hx,-hy,-hz},{-hx,-hy,-hz},{-hx, hy,-hz},{ hx, hy,-hz}},
        {{ hx,-hy, hz},{ hx,-hy,-hz},{ hx, hy,-hz},{ hx, hy, hz}},
        {{-hx,-hy,-hz},{-hx,-hy, hz},{-hx, hy, hz},{-hx, hy,-hz}},
        {{-hx, hy, hz},{ hx, hy, hz},{ hx, hy,-hz},{-hx, hy,-hz}},
        {{-hx,-hy,-hz},{ hx,-hy,-hz},{ hx,-hy, hz},{-hx,-hy, hz}} };
    const float fuv[4][2] = {{0,1},{1,1},{1,0},{0,0}};
    for (int f = 0; f < 6; ++f) {
        for (int v = 0; v < 4; ++v) {
            d.positions.insert(d.positions.end(), { fv[f][v][0], fv[f][v][1], fv[f][v][2] });
            d.normals.insert(d.normals.end(), { fn[f][0], fn[f][1], fn[f][2] });
            d.uvs.insert(d.uvs.end(), { fuv[v][0], fuv[v][1] });
        }
        const unsigned b = unsigned(f * 4);
        d.indices.insert(d.indices.end(), { b, b + 1, b + 2, b, b + 2, b + 3 });
    }
    return d;
}

int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-decals-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("decals", kSize, kSize, Colour(0, 0, 0));
    Scene *s = engine->createScene("decals");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    // Floor: mid-grey, fully rough, top face exactly at y == 0.
    MeshId floorMesh = s->createMesh(boxMesh(6.0f, 0.1f, 6.0f));
    PbrParams fp;
    fp.albedo = Colour(0.5f, 0.5f, 0.5f);
    fp.roughness = 1.0f;
    fp.metalness = 0.0f;
    MaterialId floorMat = s->createPbrMaterial(fp);
    NodeId floor = s->createNode();
    s->attachMesh(floor, floorMesh, floorMat);
    s->setNodeTransform(floor, Vec3(0, -0.1f, 0), Quat(), Vec3(1, 1, 1));

    // One directional light straight down (identity = shines down -Y).
    {
        NodeId n = s->createNode();
        s->setNodeTransform(n, Vec3(0, 5, 0), Quat(), Vec3(1, 1, 1));
        LightDesc d;
        d.type = LightType::Directional;
        d.colour = Colour(1, 1, 1);
        d.intensity = 1.0f;
        s->setLight(n, d);
    }

    // Camera: straight down from above. -90 degrees about X.
    {
        CameraDesc c;
        c.position = Vec3(0, kCamHeight, 0);
        const float a = -45.0f * 3.14159265358979f / 180.0f;   // half of -90
        c.orientation = Quat{ std::sin(a), 0.0f, 0.0f, std::cos(a) };
        c.fovDegrees = kFovDeg;
        view->setCamera(c);
    }

    render(engine.get(), 4);

    // The clean reference: nothing about the scene changes except decals, so
    // "the decal went away" must restore these pixels EXACTLY.
    Image clean;
    if (!view->readPixels(clean)) { std::printf("FAIL: readPixels: %s\n", engine->lastError().c_str()); return 1; }
    const Colour greyCentre = sampleAt(view, 0.0f, 0.0f);
    show("bare floor centre", greyCentre);
    CHECK(greyCentre.r > 0.05f && std::fabs(greyCentre.r - greyCentre.g) < 0.02f &&
          std::fabs(greyCentre.r - greyCentre.b) < 0.02f,
          "the bare floor is lit and neutral grey");

    // ---- (a) the pool recipe --------------------------------------------
    CHECK(s->decalAtlasCapacity(DecalMap::Diffuse) > 0, "the decal atlas reports a capacity");
    CHECK(s->decalAtlasUsed(DecalMap::Diffuse) == 0, "the decal atlas starts empty");

    if (!writeSolidTga("decal_red.tga", 255, 0, 0)) { std::printf("FAIL: write decal_red.tga\n"); return 1; }
    if (!writeSolidTga("decal_green.tga", 0, 255, 0)) { std::printf("FAIL: write decal_green.tga\n"); return 1; }
    if (!writeQuadrantTga("decal_quad.tga")) { std::printf("FAIL: write decal_quad.tga\n"); return 1; }

    TextureId redTex = s->loadDecalTexture("decal_red.tga", DecalMap::Diffuse);
    CHECK(redTex != 0, "loadDecalTexture accepts an ordinary image file");
    if (!redTex) { std::printf("   lastError: %s\n", engine->lastError().c_str()); return 1; }
    CHECK(s->decalAtlasUsed(DecalMap::Diffuse) == 1, "one image consumes exactly one atlas slice");
    CHECK(s->loadDecalTexture("decal_red.tga", DecalMap::Diffuse) == redTex &&
          s->decalAtlasUsed(DecalMap::Diffuse) == 1,
          "the same path loaded twice reuses one slice");

    // ---- the decal projects ---------------------------------------------
    NodeId decal = s->createNode();
    s->setNodeTransform(decal, Vec3(0, 0.5f, 0), Quat(), Vec3(1, 1, 1));
    DecalDesc dd;
    dd.diffuse = redTex;
    dd.width = 2.0f; dd.height = 2.0f; dd.depth = 2.0f;
    dd.roughness = 1.0f; dd.metalness = 0.0f;
    CHECK(s->setDecal(decal, dd), "setDecal creates the decal");
    render(engine.get(), 3);

    const Colour inside = sampleAt(view, 0.0f, 0.0f);
    const Colour outside = sampleAt(view, 2.0f, 2.0f);
    show("inside the decal box", inside);
    show("outside the decal box", outside);
    CHECK(inside.r > inside.g + 0.05f && inside.r > inside.b + 0.05f,
          "the floor inside the decal box takes the decal's red");
    CHECK(std::fabs(outside.r - outside.g) < 0.02f && std::fabs(outside.r - outside.b) < 0.02f,
          "the floor outside the decal box is untouched");

    // ---- (b) UV orientation ---------------------------------------------
    TextureId quadTex = s->loadDecalTexture("decal_quad.tga", DecalMap::Diffuse);
    CHECK(quadTex != 0 && quadTex != redTex, "a second distinct image gets its own slice");
    dd.diffuse = quadTex;
    CHECK(s->setDecal(decal, dd), "setDecal rebinds the image in place");
    render(engine.get(), 3);
    {
        // Sample the four quadrant centres of the 2x2 decal footprint.
        const Colour tl = sampleAt(view, -0.5f, -0.5f);   // screen top-left
        const Colour tr = sampleAt(view,  0.5f, -0.5f);
        const Colour bl = sampleAt(view, -0.5f,  0.5f);
        const Colour br = sampleAt(view,  0.5f,  0.5f);
        show("footprint top-left", tl);
        show("footprint top-right", tr);
        show("footprint bottom-left", bl);
        show("footprint bottom-right", br);
        // Image quadrants: TL red, TR green, BL blue, BR white. The camera looks
        // down -Y with up on world -Z, so screen top-left is world (-X, -Z),
        // which is decal-local (-X, -Z), which is UV (0, 0) = the image's
        // top-left. Upright and unmirrored: no flip, no default rotation.
        CHECK(tl.r > tl.g + 0.05f && tl.r > tl.b + 0.05f,
              "UV: the image's TOP-LEFT lands at screen top-left (u = localX, v = localZ)");
        CHECK(tr.g > tr.r + 0.05f && tr.g > tr.b + 0.05f,
              "UV: the image's TOP-RIGHT lands at screen top-right");
        CHECK(bl.b > bl.r + 0.05f && bl.b > bl.g + 0.05f,
              "UV: the image's BOTTOM-LEFT lands at screen bottom-left");
        CHECK(br.r > 0.1f && br.g > 0.1f && br.b > 0.1f,
              "UV: the image's white BOTTOM-RIGHT lands at screen bottom-right");
    }

    // ---- (c) the -Y projection convention -------------------------------
    // Turn the decal upside down (180 about X): it now projects UP, away from
    // the floor, and the shader's half-space test must reject every floor pixel.
    dd.diffuse = redTex;
    s->setDecal(decal, dd);
    render(engine.get(), 3);
    CHECK(sampleAt(view, 0.0f, 0.0f).r > greyCentre.r + 0.05f, "red is back before the flip test");
    s->setNodeTransform(decal, Vec3(0, 0.5f, 0), Quat{ 1.0f, 0.0f, 0.0f, 0.0f }, Vec3(1, 1, 1));
    render(engine.get(), 3);
    {
        const Colour flipped = sampleAt(view, 0.0f, 0.0f);
        show("decal rotated 180 about X", flipped);
        CHECK(std::fabs(flipped.r - flipped.g) < 0.02f && std::fabs(flipped.r - flipped.b) < 0.02f,
              "-Y convention: a decal facing away projects NOTHING (half-space mask)");
    }
    s->setNodeTransform(decal, Vec3(0, 0.5f, 0), Quat(), Vec3(1, 1, 1));
    render(engine.get(), 3);

    // ---- the decal moves with its node ----------------------------------
    s->setNodeTransform(decal, Vec3(1.5f, 0.5f, 0), Quat(), Vec3(1, 1, 1));
    render(engine.get(), 3);
    {
        const Colour wasCentre = sampleAt(view, 0.0f, 0.0f);
        const Colour nowThere  = sampleAt(view, 1.5f, 0.0f);
        CHECK(std::fabs(wasCentre.r - wasCentre.g) < 0.02f, "moving the node clears the old footprint");
        CHECK(nowThere.r > nowThere.g + 0.05f, "moving the node paints the new footprint");
    }
    s->setNodeTransform(decal, Vec3(0, 0.5f, 0), Quat(), Vec3(1, 1, 1));
    render(engine.get(), 3);

    // ---- the document node's own scale composes with width/height/depth --
    s->setNodeTransform(decal, Vec3(0, 0.5f, 0), Quat(), Vec3(0.25f, 1, 0.25f));
    render(engine.get(), 3);
    {
        // 2 units wide * 0.25 scale = a 0.5-unit box: (0.4, 0) is inside,
        // (0.8, 0) — inside the unscaled footprint — is now outside.
        const Colour in = sampleAt(view, 0.0f, 0.0f);
        const Colour out = sampleAt(view, 0.8f, 0.0f);
        CHECK(in.r > in.g + 0.05f, "node scale: the shrunken footprint still paints its centre");
        CHECK(std::fabs(out.r - out.g) < 0.02f, "node scale multiplies the numeric box extents");
    }
    s->setNodeTransform(decal, Vec3(0, 0.5f, 0), Quat(), Vec3(1, 1, 1));
    render(engine.get(), 3);

    // ---- visibility ------------------------------------------------------
    s->setNodeVisible(decal, false);
    render(engine.get(), 3);
    {
        const Colour hidden = sampleAt(view, 0.0f, 0.0f);
        show("decal hidden", hidden);
        CHECK(std::fabs(hidden.r - hidden.g) < 0.02f,
              "setNodeVisible(false) hides a decal (LAYER_VISIBILITY, not the PFX2 trap)");
    }
    s->setNodeVisible(decal, true);
    render(engine.get(), 3);
    CHECK(sampleAt(view, 0.0f, 0.0f).r > greyCentre.r + 0.05f, "setNodeVisible(true) brings it back");

    // ---- the aliasing guard: two decals, two images, one pool ------------
    TextureId greenTex = s->loadDecalTexture("decal_green.tga", DecalMap::Diffuse);
    CHECK(greenTex != 0 && greenTex != redTex, "a third image gets its own slice");
    NodeId decal2 = s->createNode();
    s->setNodeTransform(decal2, Vec3(-2.0f, 0.5f, 0), Quat(), Vec3(1, 1, 1));
    DecalDesc dd2 = dd;
    dd2.diffuse = greenTex;
    CHECK(s->setDecal(decal2, dd2), "a second decal is created");
    render(engine.get(), 3);
    {
        const Colour a = sampleAt(view, 0.0f, 0.0f);
        const Colour b = sampleAt(view, -2.0f, 0.0f);
        show("decal 1 (red image)", a);
        show("decal 2 (green image)", b);
        CHECK(a.r > a.g + 0.05f, "decal 1 still samples ITS OWN slice");
        CHECK(b.g > b.r + 0.05f, "decal 2 samples ITS OWN slice (no pool aliasing)");
    }

    // ---- the wrong texture kind is refused, loudly -----------------------
    {
        // An ordinary loadTexture() id: poolId 0, wrong slice space. Binding it
        // is exactly the silent-garbage case the dedicated entry point exists
        // to prevent.
        TextureId plain = s->loadTexture("decal_red.tga", true);
        DecalDesc bad = dd;
        bad.diffuse = plain;
        CHECK(plain != 0 && !s->setDecal(decal, bad),
              "setDecal REFUSES a plain loadTexture() id");
        CHECK(!engine->lastError().empty(), "and says why");
        s->destroyTexture(plain);
        // The refusal must not have disturbed the live decal.
        s->setDecal(decal, dd);
        render(engine.get(), 3);
        CHECK(sampleAt(view, 0.0f, 0.0f).r > greyCentre.r + 0.05f, "the refusal left decal 1 intact");
    }

    // ---- a decal with no image is refused --------------------------------
    {
        DecalDesc empty;
        CHECK(!s->setDecal(decal, empty), "setDecal REFUSES a desc with no diffuse image");
    }

    // ---- the budget is refused LOUDLY, never silently aliased -------------
    {
        const unsigned cap = s->decalAtlasCapacity(DecalMap::Diffuse);
        unsigned made = s->decalAtlasUsed(DecalMap::Diffuse);
        bool allOk = true;
        for (unsigned i = made; i < cap; ++i) {
            char name[64];
            std::snprintf(name, sizeof(name), "decal_fill_%u.tga", i);
            // Distinct content per file so nothing can be deduplicated.
            writeSolidTga(name, (unsigned char)(i * 7 + 1), (unsigned char)(i * 3 + 2),
                          (unsigned char)(i * 5 + 3));
            if (!s->loadDecalTexture(name, DecalMap::Diffuse)) { allOk = false; break; }
        }
        CHECK(allOk && s->decalAtlasUsed(DecalMap::Diffuse) == cap,
              "the atlas accepts exactly its reserved capacity");
        writeSolidTga("decal_overflow.tga", 9, 99, 199);
        const TextureId over = s->loadDecalTexture("decal_overflow.tga", DecalMap::Diffuse);
        CHECK(over == 0, "the image past the budget is REFUSED (never silently aliased)");
        CHECK(engine->lastError().find("full") != std::string::npos,
              "and the error says the atlas is full");
        std::printf("   lastError: %s\n", engine->lastError().c_str());
        // Both live decals must still be correct after the refusal.
        render(engine.get(), 3);
        CHECK(sampleAt(view, 0.0f, 0.0f).r > sampleAt(view, 0.0f, 0.0f).g + 0.05f,
              "decal 1 is unaffected by the overflow refusal");
        CHECK(sampleAt(view, -2.0f, 0.0f).g > sampleAt(view, -2.0f, 0.0f).r + 0.05f,
              "decal 2 is unaffected by the overflow refusal");
    }

    // ---- removeDecal restores the ORIGINAL pixels exactly -----------------
    CHECK(s->removeDecal(decal), "removeDecal removes decal 1");
    CHECK(s->removeDecal(decal2), "removeDecal removes decal 2");
    CHECK(!s->removeDecal(decal), "removeDecal on a node with no decal returns false");
    render(engine.get(), 3);
    {
        Image after;
        view->readPixels(after);
        size_t diff = 0;
        for (size_t i = 0; i < clean.rgba.size() && i < after.rgba.size(); ++i)
            if (clean.rgba[i] != after.rgba[i]) ++diff;
        std::printf("   pixels differing from the pre-decal reference: %zu\n", diff);
        CHECK(diff == 0,
              "with no decals left the scene renders BYTE-IDENTICALLY to before any existed");
    }

    // ---- removeNode also takes the decal down ----------------------------
    {
        NodeId n = s->createNode();
        s->setNodeTransform(n, Vec3(0, 0.5f, 0), Quat(), Vec3(1, 1, 1));
        CHECK(s->setDecal(n, dd), "a decal on a throwaway node");
        render(engine.get(), 2);
        CHECK(sampleAt(view, 0.0f, 0.0f).r > greyCentre.r + 0.05f, "the throwaway decal paints");
        CHECK(s->removeNode(n), "removeNode on a decal node");
        render(engine.get(), 3);
        Image after;
        view->readPixels(after);
        size_t diff = 0;
        for (size_t i = 0; i < clean.rgba.size() && i < after.rgba.size(); ++i)
            if (clean.rgba[i] != after.rgba[i]) ++diff;
        CHECK(diff == 0, "removeNode leaves no trace of the decal");
    }

    engine->destroyView(view);
    engine->destroyScene(s);

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall decal checks passed\n", failures);
    return failures ? 1 : 0;
}
