// THE OWNER'S TEAPOT TEARING AND ITS THREE NEIGHBOURS (REFLECT-FIX-1) — four rows,
// one binary, one fixture: a chrome sphere on a matte floor, the shape of the Mirror
// Room's sphere and the green teapot dragged past it (spikes/reflect-tear-audit).
//
//   gi.reflect_uncover[_norays]  (no argument)
//     THE STALE HISTORY. The march's colour is the previous frame's, and a floor
//     point a moving object uncovered this frame still holds that object in it: the
//     sphere drew a crescent of the object's colour where the floor belongs (the
//     audit: ~225 px a frame of teapot green, moving ~5x the true reflection). A
//     green box (a STILL, moved as a script or a drag moves it) crosses in front of
//     the sphere; at three checkpoints of the run the MOVING frame is read, the box
//     is held there kSettle frames and the SETTLED frame is read. FALSE = green in
//     the sphere's disk in the moving frame where the settled one has none (the
//     audit's metric: object-coloured pixels outside the true reflected footprint);
//     MISSED = the settled frame's reflected green the moving frame lacks (the true
//     reflection must still track within a frame). With the ray tier and without it
//     (the march alone hands a declined tap to the probe/sky: it must not smear
//     either).
//   gi.reflect_no_helpers  (--helpers)
//     THE EDITOR'S HELPERS NEVER REACH A REFLECTION. A selection outline shell
//     (Scene::createOutlineMaterial on a helper node, the mirror's own shape) and
//     the grid around the box: none of their colour in the SSR history, none in the
//     reflection texture, none in the sphere's disk — and both still drawn on
//     screen.
//   gi.cards_move_keep  (--cards)
//     A MOVED STILL KEEPS ITS CARDS. The atlas filled past the 7/8 gate by a field
//     of crates; one of them moved: its cards come back (the residency counts
//     unchanged) and are captured again within kRecapture frames.
//   gi.reflect_metal_hit  (--metal)
//     A METAL SEEN IN A REFLECTION IS ITS COLOUR. A gold crate off screen behind the
//     camera, seen only in the sphere (the rays answer it, not the march): the
//     caches hold a surface's diffuse response, a metal's is ~0, and its card made
//     it black. Gold against the same crate in matte gold (whose card is right).
//
// Every number is frames-counted (the engine has no wall clock).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

static const unsigned kWidth = 640;
static const unsigned kHeight = 360;
static const float kFovDeg = 45.0f;
static const int kWarmFrames = 120;
static const int kSettle = 60;
static const float kSphereR = 0.6f;
static const Vec3 kSphereC(0.0f, 0.6f, 0.0f);
static const Vec3 kCamPos(0.0f, 2.6f, 4.0f);
static const Vec3 kCamTarget(0.0f, 0.4f, 0.0f);
// THE BOX: 0.5 m, between the camera and the sphere, crossing at kStep a frame (2.4
// m/s — a brisk drag) from +kHalfRun to -kHalfRun.
static const float kBox = 0.4f;
static const float kBoxZ = 0.95f;
static const float kStep = 0.1f;
static const float kHalfRun = 1.6f;

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

static MeshData sphereMesh(int rings = 48, int segments = 96)
{
    MeshData d;
    const float kPi = 3.14159265358979f;
    for (int r = 0; r <= rings; ++r) {
        const float th = float(r) / float(rings) * kPi;
        for (int sg = 0; sg < segments; ++sg) {
            const float ph = float(sg) / float(segments) * 2.0f * kPi;
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = std::sin(th) * std::sin(ph);
            d.positions.insert(d.positions.end(), { 0.5f * x, 0.5f * y, 0.5f * z });
            d.normals.insert(d.normals.end(), { x, y, z });
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int sg = 0; sg < segments; ++sg) {
            const unsigned a = unsigned(r * segments + sg);
            const unsigned b = unsigned(r * segments + (sg + 1) % segments);
            const unsigned c = a + unsigned(segments), e = b + unsigned(segments);
            d.indices.insert(d.indices.end(), { a, b, c, b, e, c });
        }
    return d;
}

/// A world point's pixel through the fixture camera (testCameraDescLookAt's basis).
static bool project(const Vec3 &p, float &px, float &py, float &z)
{
    Vec3 f(kCamTarget.x - kCamPos.x, kCamTarget.y - kCamPos.y, kCamTarget.z - kCamPos.z);
    float l = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    f = Vec3(f.x / l, f.y / l, f.z / l);
    Vec3 r(-f.z, 0.0f, f.x);
    l = std::sqrt(r.x * r.x + r.z * r.z);
    r = Vec3(r.x / l, 0.0f, r.z / l);
    const Vec3 u(r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x);
    const Vec3 d(p.x - kCamPos.x, p.y - kCamPos.y, p.z - kCamPos.z);
    z = d.x * f.x + d.y * f.y + d.z * f.z;
    if (z <= 0.0f) return false;
    const float t = std::tan(kFovDeg * 0.5f * 3.14159265f / 180.0f);
    const float aspect = float(kWidth) / float(kHeight);
    const float nx = (d.x * r.x + d.y * r.y + d.z * r.z) / (z * t * aspect);
    const float ny = (d.x * u.x + d.y * u.y + d.z * u.z) / (z * t);
    px = (nx + 1.0f) * 0.5f * float(kWidth);
    py = (1.0f - ny) * 0.5f * float(kHeight);
    return true;
}

/// The sphere's screen disk (centre, radius in pixels).
static void sphereDisk(float &cx, float &cy, float &pr)
{
    float z = 1.0f;
    project(kSphereC, cx, cy, z);
    const float t = std::tan(kFovDeg * 0.5f * 3.14159265f / 180.0f);
    pr = kSphereR / (z * t) * 0.5f * float(kHeight);
}

/// The box's direct screen rectangle (its eight corners' bounds), dilated by `pad`.
struct Rect { float x0, y0, x1, y1; bool in(float x, float y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; } };
static Rect boxRect(const Vec3 &c, float half, float pad)
{
    Rect r{ 1e9f, 1e9f, -1e9f, -1e9f };
    for (int k = 0; k < 8; ++k) {
        const Vec3 p(c.x + ((k & 1) ? half : -half), c.y + ((k & 2) ? half : -half), c.z + ((k & 4) ? half : -half));
        float x, y, z;
        if (!project(p, x, y, z)) continue;
        r.x0 = std::min(r.x0, x); r.y0 = std::min(r.y0, y);
        r.x1 = std::max(r.x1, x); r.y1 = std::max(r.y1, y);
    }
    r.x0 -= pad; r.y0 -= pad; r.x1 += pad; r.y1 += pad;
    return r;
}

/// The pixels of the sphere's disk, eroded off its silhouette by `erode` px.
static std::vector<unsigned> diskPixels(float erode)
{
    float cx, cy, pr;
    sphereDisk(cx, cy, pr);
    std::vector<unsigned> px;
    for (unsigned y = 0; y < kHeight; ++y)
        for (unsigned x = 0; x < kWidth; ++x)
            if (std::hypot(float(x) + 0.5f - cx, float(y) + 0.5f - cy) < pr - erode) px.push_back(y * kWidth + x);
    return px;
}

// The audit's classifiers (spikes/reflect-tear-audit/metrics/an.py), on display codes.
static bool green(const Image &img, unsigned i)
{
    const int r = img.rgba[size_t(i) * 4u], g = img.rgba[size_t(i) * 4u + 1], b = img.rgba[size_t(i) * 4u + 2];
    return g > r + 40 && g > b + 15;
}
/// The helpers' colour: magenta outline and grid (no scene colour is magenta here).
static bool magenta(const Image &img, unsigned i)
{
    const int r = img.rgba[size_t(i) * 4u], g = img.rgba[size_t(i) * 4u + 1], b = img.rgba[size_t(i) * 4u + 2];
    return r > g + 50 && b > g + 50;
}
static bool magentaF(const ImageF &img, size_t i, bool weighted)
{
    const float r = img.rgba[i * 4u], g = img.rgba[i * 4u + 1], b = img.rgba[i * 4u + 2], w = img.rgba[i * 4u + 3];
    if (weighted && w <= 0.0f) return false;
    return r > 0.02f && b > 0.02f && r > 3.0f * g && b > 3.0f * g;
}

static void writePpm(const Image &img, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (size_t i = 0; i < size_t(img.width) * img.height; ++i) std::fwrite(&img.rgba[i * 4u], 1, 3, f);
    std::fclose(f);
}

struct Room {
    NodeId floor = 0, sphere = 0, box = 0;
    MeshId cube = 0, cardedCube = 0;
};

static Room buildRoom(Engine *e, Scene *s, View *view, bool cards)
{
    Room r;
    s->setAmbient(Colour(0.45f, 0.55f, 0.70f), Colour(0.30f, 0.30f, 0.32f));
    r.cube = s->createMesh(enginetest::unitCubeMesh());
    MeshData cd = enginetest::unitCubeMesh();
    cd.cards = enginetest::boxCards(0.5f);
    r.cardedCube = s->createMesh(cd);
    r.floor = s->createNode();
    PbrParams fp; fp.albedo = Colour(0.62f, 0.62f, 0.62f); fp.roughness = 0.9f;
    s->attachMesh(r.floor, r.cube, s->createPbrMaterial(fp));
    s->setNodeTransform(r.floor, Vec3(0.0f, -0.1f, 0.0f), Quat(), Vec3(60.0f, 0.2f, 60.0f));
    r.sphere = s->createNode();
    PbrParams sp; sp.albedo = Colour(0.95f, 0.95f, 0.95f); sp.metalness = 1.0f; sp.roughness = 0.02f;
    s->attachMesh(r.sphere, s->createMesh(sphereMesh()), s->createPbrMaterial(sp));
    s->setNodeTransform(r.sphere, kSphereC, Quat(), Vec3(2.0f * kSphereR, 2.0f * kSphereR, 2.0f * kSphereR));
    enginetest::addDirectionalLight(s, Vec3(-0.4f, -1.0f, -0.5f), 2.5f);
    GiParams gi; gi.mode = GiMode::Vct; gi.quality = GiQuality::High; gi.numBounces = 1;
    gi.cascadeCount = 1;
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 20.0f, 128, 0.0f };
    gi.cards = cards;
    gi.dragMoverChannel = true;   // the editor's: a gesture moves a still onto the drag channel
    s->setGlobalIllumination(gi);
    PostFxDesc fx; fx.allowOffscreen = true; fx.ssr = 2;
    view->setPostFx(fx);
    enginetest::testCameraLookAt(view, kCamPos, kCamTarget);
    (void)e;
    return r;
}

static Vec3 boxAt(int f) { return Vec3(kHalfRun - kStep * float(f), 0.5f * kBox, kBoxZ); }

// ---------------------------------------------------------------------------
// gi.reflect_uncover
// ---------------------------------------------------------------------------
static int uncoverMain(Engine *e, Scene *s, View *view, const char *dumpDir)
{
    const bool rays = !getenv("JAHSHAKA_NO_RAY_QUERY");
    Room room = buildRoom(e, s, view, true);
    room.box = s->createNode();
    PbrParams bp; bp.albedo = Colour(0.08f, 0.75f, 0.10f); bp.roughness = 0.7f;
    s->attachMesh(room.box, room.cardedCube, s->createPbrMaterial(bp));
    s->setNodeTransform(room.box, boxAt(0), Quat(), Vec3(kBox, kBox, kBox));
    render(e, kWarmFrames);

    const std::vector<unsigned> disk = diskPixels(2.0f);
    const int kCheck[3] = { 10, 16, 22 };   // box x = 0.6, 0.0, -0.6
    int frame = 0;
    long sumFalse = 0, sumTrue = 0, sumMissed = 0;
    int worstFalse = 0;
    for (int k = 0; k < 3; ++k) {
        while (frame < kCheck[k]) {
            ++frame;
            s->setNodeTransform(room.box, boxAt(frame), Quat(), Vec3(kBox, kBox, kBox));
            e->renderOneFrame();
        }
        Image moving, settled;
        if (!view->readPixels(moving)) { std::printf("FAIL: readPixels: %s\n", e->lastError().c_str()); return 1; }
        if (dumpDir) {
            // the screen answer as the composite takes it: rgb, and its weight as grey
            ImageF refl;
            if (view->readReflectionHdr(refl)) {
                Image c, w;
                c.width = w.width = refl.width; c.height = w.height = refl.height;
                c.rgba.resize(size_t(refl.width) * refl.height * 4u); w.rgba = c.rgba;
                for (size_t i = 0; i < size_t(refl.width) * refl.height; ++i)
                    for (int ch = 0; ch < 4; ++ch) {
                        const float v = refl.rgba[i * 4u + size_t(ch)];
                        c.rgba[i * 4u + size_t(ch)] = (unsigned char)std::min(255.0f, std::max(0.0f, std::sqrt(v) * 255.0f));
                        w.rgba[i * 4u + size_t(ch)] = (unsigned char)std::min(255.0f, std::max(0.0f, refl.rgba[i * 4u + 3u] * 255.0f));
                    }
                const std::string base = std::string(dumpDir) + "/uncover-" + (rays ? "rays" : "norays") + "-" +
                                         std::to_string(k);
                writePpm(c, base + "-reflrgb.ppm");
                writePpm(w, base + "-reflw.ppm");
            }
        }
        render(e, kSettle);
        if (!view->readPixels(settled)) { std::printf("FAIL: readPixels\n"); return 1; }
        const Rect direct = boxRect(boxAt(frame), 0.5f * kBox, 2.0f);
        int falsePx = 0, truePx = 0, missed = 0;
        // THE TRUE FOOTPRINT: every pixel of the settled frame the box's colour reaches
        // at all (a green tint of 8 codes — the converged ray mean draws the reflection's
        // edges translucent), dilated by 2 px for a frame-to-frame antialiasing edge.
        std::vector<unsigned char> foot(size_t(kWidth) * kHeight, 0);
        for (unsigned y = 0; y < kHeight; ++y)
            for (unsigned x = 0; x < kWidth; ++x) {
                const unsigned char *p = &settled.rgba[(size_t(y) * kWidth + x) * 4u];
                if (int(p[1]) - std::max(int(p[0]), int(p[2])) <= 8) continue;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx) {
                        const int xx = int(x) + dx, yy = int(y) + dy;
                        if (xx >= 0 && yy >= 0 && xx < int(kWidth) && yy < int(kHeight))
                            foot[size_t(yy) * kWidth + size_t(xx)] = 1;
                    }
            }
        // THE DIRECT BOX AGREES: the moving frame shows the box where this frame put it.
        // (A first version of this lane drew the frame's Atom items a frame late — a
        // depth-format history texture read by the resolve — and this is the guard.)
        int directDiff = 0;
        for (unsigned i = 0; i < kWidth * kHeight; ++i)
            if (green(moving, i) != green(settled, i) && float(i / kWidth) > direct.y0 + 6.0f) ++directDiff;
        for (unsigned i : disk) {
            const bool gm = green(moving, i), gs = green(settled, i);
            if (gm && !foot[i]) ++falsePx;
            const bool isDirect = direct.in(float(i % kWidth) + 0.5f, float(i / kWidth) + 0.5f);
            if (gs && !isDirect) {
                ++truePx;
                if (!gm) ++missed;
            }
        }
        CHECK_MSG(directDiff == 0, "checkpoint %d: the box is drawn where this frame put it (%d px of its direct "
                  "image differ from the settled frame at the same pose)", k, directDiff);
        std::printf("   checkpoint %d (box x %+.2f): FALSE %d px (moving green outside the settled frame's footprint), the true "
                    "reflection %d px of which the moving frame misses %d\n",
                    k, boxAt(frame).x, falsePx, truePx, missed);
        sumFalse += falsePx; sumTrue += truePx; sumMissed += missed;
        worstFalse = std::max(worstFalse, falsePx);
        if (dumpDir) {
            const std::string base = std::string(dumpDir) + "/uncover-" + (rays ? "rays" : "norays") + "-" +
                                     std::to_string(k);
            writePpm(moving, base + "-moving.ppm");
            writePpm(settled, base + "-settled.ppm");
        }
    }
    std::printf("   %s: false %ld px over 3 checkpoints (worst %d), true reflection %ld px, missed %ld\n",
                rays ? "rays + march" : "march alone", sumFalse, worstFalse, sumTrue, sumMissed);
    CHECK_MSG(sumTrue > 150, "the sphere reflects the box at all (%ld px of true reflection)", sumTrue);
    // THE BAR. The audit measured ~225 px a frame of false crescent on the Mirror Room;
    // this fixture's base reads it below. ~0 is the brief's: a few pixels of the
    // reflected silhouette's antialiased edge may differ between a moving and a
    // settled frame, never a crescent.
    CHECK_MSG(worstFalse <= 12,
              "NO FALSE CRESCENT: no checkpoint shows more than 12 px of the box's colour outside its true "
              "reflection (worst %d)", worstFalse);
    // ...AND THE TRUE REFLECTION STILL TRACKS. With the ray tier a hit on the moving box
    // follows its motion (jahSsrVelocity) and the rays answer the rest: at most a
    // sliver may be missing. The MARCH ALONE has no motion vectors: where the box
    // moved INTO, the last picture held floor, the texel is declined and the probe —
    // which holds no mover — answers; that leading strip is a frame of motion (kStep
    // of a kBox box, ~25 %) and is the march-only tier's honest limit, not a smear.
    const double missBar = rays ? 0.35 : 0.5;
    CHECK_MSG(sumTrue > 0 && double(sumMissed) <= missBar * double(sumTrue),
              "THE TRUE REFLECTION TRACKS within a frame: the moving frame misses %ld of the settled frame's %ld "
              "reflected px (bar %.0f %%)", sumMissed, sumTrue, 100.0 * missBar);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// gi.reflect_no_helpers
// ---------------------------------------------------------------------------
static int helpersMain(Engine *e, Scene *s, View *view, const char *dumpDir)
{
    Room room = buildRoom(e, s, view, true);
    const Vec3 at(0.35f, 0.5f * kBox, kBoxZ - 0.3f);
    room.box = s->createNode();
    PbrParams bp; bp.albedo = Colour(0.08f, 0.75f, 0.10f); bp.roughness = 0.7f;
    s->attachMesh(room.box, room.cardedCube, s->createPbrMaterial(bp));
    s->setNodeTransform(room.box, at, Quat(), Vec3(kBox, kBox, kBox));
    // THE SELECTION SHELL, the mirror's shape (scenemirror.cpp: a helper node at birth,
    // the inverted hull on a slightly larger copy of the mesh).
    const NodeId shell = s->createNode();
    s->setNodeHelper(shell, true);
    const MaterialId outline = s->createOutlineMaterial(Colour(1.0f, 0.0f, 1.0f));
    CHECK_MSG(shell && outline && s->attachMesh(shell, room.cube, outline), "the outline shell exists");
    s->setNodeTransform(shell, at, Quat(), Vec3(kBox * 1.12f, kBox * 1.12f, kBox * 1.12f));
    GridDesc grid;
    grid.enabled = true;
    grid.spacing = 0.5f;
    grid.minorColour = Colour(1.0f, 0.0f, 1.0f, 0.9f);
    grid.majorColour = Colour(1.0f, 0.0f, 1.0f, 0.9f);
    grid.thicknessPx = 2.0f;
    CHECK_MSG(s->setGrid(grid), "the grid is on");
    render(e, kWarmFrames);

    Image shown;
    ImageF history, reflection;
    if (!view->readPixels(shown)) { std::printf("FAIL: readPixels\n"); return 1; }
    const bool haveHistory = view->readSsrHistoryHdr(history);
    const bool haveReflection = view->readReflectionHdr(reflection);
    CHECK_MSG(haveHistory, "the SSR history reads back (%s)", haveHistory ? "" : e->lastError().c_str());
    CHECK_MSG(haveReflection, "the reflection texture reads back");

    const Rect direct = boxRect(at, 0.5f * kBox * 1.12f, 4.0f);
    int onScreen = 0, inSphere = 0;
    for (unsigned i = 0; i < kWidth * kHeight; ++i)
        if (magenta(shown, i)) ++onScreen;
    for (unsigned i : diskPixels(2.0f))
        if (magenta(shown, i) && !direct.in(float(i % kWidth) + 0.5f, float(i / kWidth) + 0.5f)) ++inSphere;
    int inHistory = 0, inReflection = 0;
    if (haveHistory)
        for (size_t i = 0; i < size_t(history.width) * history.height; ++i)
            if (magentaF(history, i, false)) ++inHistory;
    if (haveReflection)
        for (size_t i = 0; i < size_t(reflection.width) * reflection.height; ++i)
            if (magentaF(reflection, i, true)) ++inReflection;
    std::printf("   helper-coloured px: on screen %d, in the sphere's disk off the shell %d, in the SSR history %d, "
                "in the reflection texture %d\n", onScreen, inSphere, inHistory, inReflection);
    if (dumpDir) writePpm(shown, std::string(dumpDir) + "/helpers-shown.ppm");
    CHECK_MSG(onScreen > 400, "the outline and the grid are still DRAWN (%d px on screen)", onScreen);
    CHECK_MSG(inHistory == 0, "NO HELPER IN THE SSR HISTORY (%d px)", inHistory);
    CHECK_MSG(inReflection == 0, "NO HELPER IN THE REFLECTION TEXTURE (%d px)", inReflection);
    CHECK_MSG(inSphere <= 4, "NO HELPER IN THE CHROME SPHERE (%d px off the shell's own outline)", inSphere);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// gi.cards_move_keep
// ---------------------------------------------------------------------------
static int cardsMain(Engine *e, Scene *s, View *view)
{
    Room room = buildRoom(e, s, view, true);
    // THE FIELD THAT FILLS THE ATLAS: full-size crates (1.4 m: 128-texel cards, a
    // whole page each) in a block beside the camera — 256 pages hold ~42 six-card
    // sets, and the gate shuts at 7/8.
    PbrParams cp; cp.albedo = Colour(0.7f, 0.6f, 0.5f); cp.roughness = 0.8f;
    const MaterialId crateMat = s->createPbrMaterial(cp);
    std::vector<NodeId> crates;
    for (int i = 0; i < 48; ++i) {
        const NodeId n = s->createNode();
        s->attachMesh(n, room.cardedCube, crateMat);
        const float x = -6.0f + 2.0f * float(i % 7), z = -3.0f - 2.0f * float(i / 7);
        s->setNodeTransform(n, Vec3(x, 0.7f, z), Quat(), Vec3(1.4f, 1.4f, 1.4f));
        crates.push_back(n);
    }
    render(e, 240);
    const CardCacheStatus c0 = s->giStatus().cards;
    std::printf("   the field: %u instances, %u cards resident, %u of %u pages (gate at %u)\n", c0.instancesResident,
                c0.cardsResident, c0.pagesUsed, c0.pages, c0.pages * 7u / 8u);
    CHECK_MSG(c0.pages > 0 && c0.pagesUsed * 8u >= c0.pages * 7u,
              "the atlas is past the 7/8 gate (%u of %u pages) — the case is not vacuous", c0.pagesUsed, c0.pages);
    // THE SUBJECT: a resident crate (the nearest — the arrival order is by distance).
    NodeId subject = 0;
    CardSample probe;
    for (NodeId n : crates)
        if (s->readCardTexel(n, 4u, 0.5f, 0.5f, probe) && probe.ok) { subject = n; break; }
    CHECK_MSG(subject != 0, "a crate holds captured cards");
    if (!subject) return 1;
    // ONE MOVE, then rest (a drag's last frame and the stillness after it).
    Vec3 pos;
    {
        // the crate's own index in the field
        size_t i = size_t(std::find(crates.begin(), crates.end(), subject) - crates.begin());
        pos = Vec3(-6.0f + 2.0f * float(i % 7) + 0.6f, 0.7f, -3.0f - 2.0f * float(i / 7));
    }
    s->setNodeTransform(subject, pos, Quat(), Vec3(1.4f, 1.4f, 1.4f));
    render(e, 1);
    const CardCacheStatus c1 = s->giStatus().cards;
    const int kRecapture = 60;
    int capturedAt = -1;
    for (int f = 0; f < kRecapture; ++f) {
        render(e, 1);
        CardSample after;
        if (s->readCardTexel(subject, 4u, 0.5f, 0.5f, after) && after.ok) { capturedAt = f + 2; break; }
    }
    render(e, 30);
    const CardCacheStatus c2 = s->giStatus().cards;
    std::printf("   after the move: %u instances / %u cards (frame 1), %u / %u (at rest); %llu transform "
                "invalidations; the moved crate's card read back at frame %d\n",
                c1.instancesResident, c1.cardsResident, c2.instancesResident, c2.cardsResident,
                c2.invalidTransform - c0.invalidTransform, capturedAt);
    CHECK_MSG(c2.invalidTransform > c0.invalidTransform, "the move invalidated the crate's cards (it is the case)");
    CHECK_MSG(c1.instancesResident == c0.instancesResident && c1.cardsResident == c0.cardsResident,
              "A MOVED STILL KEEPS ITS CARDS: re-allocated on the move's own frame (%u/%u -> %u/%u)",
              c0.instancesResident, c0.cardsResident, c1.instancesResident, c1.cardsResident);
    CHECK_MSG(c2.cardsResident == c0.cardsResident, "...and still holds them at rest (%u -> %u)", c0.cardsResident,
              c2.cardsResident);
    CHECK_MSG(capturedAt > 0, "...captured again within %d frames (frame %d)", kRecapture, capturedAt);

    // A GESTURE (the editor's drag, the app's Mirror Room teapot): a move every frame
    // for a while — the second move inside the settle window promotes the still onto
    // the drag channel and takes it off the GI channel, so the cache stops being
    // OFFERED it — then rest. At the gesture's end it must hold its cards again.
    for (int f = 0; f < 40; ++f) {
        pos.x += 0.02f;
        s->setNodeTransform(subject, pos, Quat(), Vec3(1.4f, 1.4f, 1.4f));
        render(e, 1);
    }
    const CardCacheStatus c3 = s->giStatus().cards;
    int backAt = -1;
    for (int f = 0; f < 240; ++f) {
        render(e, 1);
        CardSample back;
        if (s->readCardTexel(subject, 4u, 0.5f, 0.5f, back) && back.ok) { backAt = f + 1; break; }
    }
    render(e, 30);
    const CardCacheStatus c4 = s->giStatus().cards;
    std::printf("   a 40-frame gesture: %u / %u resident during it, %u / %u at rest; the crate's card read back %d "
                "frames after the gesture\n", c3.instancesResident, c3.cardsResident, c4.instancesResident,
                c4.cardsResident, backAt);
    CHECK_MSG(backAt > 0 && c4.cardsResident == c0.cardsResident,
              "A DRAGGED STILL GETS ITS CARDS BACK when the gesture ends (read back after %d frames; %u -> %u "
              "cards)", backAt, c0.cardsResident, c4.cardsResident);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// gi.reflect_metal_hit
// ---------------------------------------------------------------------------
static int metalMain(Engine *e, Scene *s, View *view, const char *dumpDir)
{
    Room room = buildRoom(e, s, view, true);
    // OFF SCREEN, BEHIND THE CAMERA: only the rays reach it (the march has no screen
    // pixel of it), and it is static and carded — the route that read its card.
    const Vec3 at(-1.2f, 0.8f, 6.5f);
    const NodeId crate = s->createNode();
    PbrParams matte; matte.albedo = Colour(1.0f, 0.77f, 0.34f); matte.roughness = 0.5f;
    PbrParams gold = matte; gold.metalness = 1.0f; gold.roughness = 0.3f;
    const MaterialId matteMat = s->createPbrMaterial(matte), goldMat = s->createPbrMaterial(gold);
    s->attachMesh(crate, room.cardedCube, matteMat);
    s->setNodeTransform(crate, at, Quat(), Vec3(1.6f, 1.6f, 1.6f));
    const std::vector<unsigned> disk = diskPixels(3.0f);

    const auto shot = [&](Image &img) { render(e, 240); return view->readPixels(img); };
    Image withMatte, withGold, without;
    const bool ok = shot(withMatte);
    s->attachMesh(crate, room.cardedCube, goldMat);
    const bool ok2 = shot(withGold);
    s->setNodeVisible(crate, false);
    const bool ok3 = shot(without);
    CHECK_MSG(ok && ok2 && ok3, "the three frames read back");
    // THE FOOTPRINT: where the sphere shows the crate (matte arm against no crate).
    double mR = 0, mG = 0, mB = 0, gR = 0, gG = 0, gB = 0;
    int n = 0;
    for (unsigned i : disk) {
        const unsigned char *a = &withMatte.rgba[size_t(i) * 4u], *z = &without.rgba[size_t(i) * 4u];
        const int d = std::abs(int(a[0]) - int(z[0])) + std::abs(int(a[1]) - int(z[1])) + std::abs(int(a[2]) - int(z[2]));
        if (d < 45) continue;
        const unsigned char *g = &withGold.rgba[size_t(i) * 4u];
        mR += a[0]; mG += a[1]; mB += a[2];
        gR += g[0]; gG += g[1]; gB += g[2];
        ++n;
    }
    if (n) { mR /= n; mG /= n; mB /= n; gR /= n; gG /= n; gB /= n; }
    std::printf("   the crate's image in the sphere: %d px; matte gold (%.0f, %.0f, %.0f), METAL gold (%.0f, %.0f, "
                "%.0f) in display codes\n", n, mR, mG, mB, gR, gG, gB);
    if (dumpDir) {
        writePpm(withMatte, std::string(dumpDir) + "/metal-matte.ppm");
        writePpm(withGold, std::string(dumpDir) + "/metal-gold.ppm");
        writePpm(without, std::string(dumpDir) + "/metal-none.ppm");
    }
    CHECK_MSG(n > 100, "the sphere shows the crate behind the camera (%d px)", n);
    const double lumM = 0.2126 * mR + 0.7152 * mG + 0.0722 * mB, lumG = 0.2126 * gR + 0.7152 * gG + 0.0722 * gB;
    CHECK_MSG(lumG >= 0.35 * lumM,
              "A METAL IN A REFLECTION IS NOT BLACK: the gold crate's image %.0f codes against the matte one's %.0f "
              "(bar 35 %%)", lumG, lumM);
    CHECK_MSG(gR > 1.15 * gB && gG > 1.05 * gB, "...and it is GOLD (R %.0f > G %.0f > B %.0f)", gR, gG, gB);
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    const char *dumpDir = getenv("JAH_REFLECT_UNCOVER_DUMP");
    const std::string mode = argc > 1 ? argv[1] : "";
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-reflect-uncover-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("reflectuncover", kWidth, kHeight, Colour(0.45f, 0.55f, 0.70f));
    Scene *s = e->createScene("reflectuncover");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    const bool rayTier = e->rayQueryAvailable() && e->rayTracing();
    const bool raysWanted = !getenv("JAHSHAKA_NO_RAY_QUERY");
    if ((raysWanted || mode == "--metal") && !rayTier) {
        std::printf("ok: no ray queries on this machine — this row is about the ray tier; skipping\n");
        return 0;
    }
    if (mode == "--helpers") return helpersMain(e, s, view, dumpDir);
    if (mode == "--cards") return cardsMain(e, s, view);
    if (mode == "--metal") return metalMain(e, s, view, dumpDir);
    return uncoverMain(e, s, view, dumpDir);
}
