// rtreflect.far_record — A FAR HIT IS RECORDED AS A FAR HIT (PHOTON-7 stage 6, READ_B finding 1).
//
// THE LAW: a reflected ray that escapes the near length (photonRayReach: max(the outer cascade's diagonal, 50 m))
// continues on the FAR copies (each mesh's coarsest level, kRayMaskFar). A far hit lies outside every cascade, so the
// visibility-buffer decode shades it (a record) - and the record must say FAR (the decode's bit 27: it reads triangle
// `prim` of the COARSEST level, and the hit's sun leg lifts by the far copy's hand-over). A record that says near makes
// the decode shade triangle `prim` of LEVEL 0 - another place of the mesh: a reflected far object shows a shuffled
// patchwork of its own surface.
// THE FIXTURE: a mirror (metal, roughness 0: the lobe's centre ray, its sample whole every frame) 3 m in front of the
// camera; behind the camera, 93 m along the reflected rays, a 15 m sphere lit from +X - its lit half on the image's
// right - with two levels (level 1 = every other ring and segment: its triangle k is not level 0's). One cascade of
// 16 m (a near length of 50 m). THE REFERENCE: the same sphere seen DIRECTLY from the mirrored camera (the mirror
// hidden), flipped left to right - a mirror image. Inside the sphere's disk (eroded by 3 px), every pixel the reference
// shows clearly lit (or clearly dark: the thirds between each picture's own 10th and 90th percentile) must be so in the
// reflection, but for the terminator moving by under a pixel - at most one disagreeing pixel per row of the disk: the
// decode shades the coarsest level's facets under its hemisphere ambient, the rasteriser the level it draws (measured
// 32 for 42 rows; the near-flagged record, 486 - nine a row).
// Frames: 1 + 40 + 1 + 24 = 66.
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
#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
    } while (0)

static const unsigned kWidth = 256, kHeight = 144;
static const float kSphereR = 15.0f;
static const int kRings = 32, kSegments = 64;

/// A UV SPHERE with a second level over the same vertices: every other ring and segment.
static MeshData twoLevelSphere()
{
    MeshData d;
    const float pi = 3.14159265f;
    for (int i = 0; i <= kRings; ++i) {
        const float th = pi * float(i) / float(kRings);
        for (int j = 0; j <= kSegments; ++j) {
            const float ph = 2.0f * pi * float(j) / float(kSegments);
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = std::sin(th) * std::sin(ph);
            d.positions.insert(d.positions.end(), { kSphereR * x, kSphereR * y, kSphereR * z });
            d.normals.insert(d.normals.end(), { x, y, z });
            d.uvs.insert(d.uvs.end(), { float(j) / float(kSegments), float(i) / float(kRings) });
        }
    }
    const auto vtx = [](int i, int j) { return unsigned(i * (kSegments + 1) + j); };
    const auto quads = [&](std::vector<unsigned> &idx, int step) {
        for (int i = 0; i < kRings; i += step)
            for (int j = 0; j < kSegments; j += step) {
                const unsigned a = vtx(i, j), b = vtx(i + step, j), c = vtx(i + step, j + step), e = vtx(i, j + step);
                idx.insert(idx.end(), { a, e, c, a, c, b });   // outward (counter-clockwise from outside)
            }
    };
    quads(d.indices, 1);
    std::vector<unsigned> level1;
    quads(level1, 2);
    d.lodIndices.push_back(level1);
    // level 1's two-sided distance from level 0: the doubled step's sagitta
    const float bound = kSphereR * (1.0f - std::cos(pi / float(kRings)));
    d.lodBounds.push_back(bound);
    d.lodErrors.push_back(bound);
    return d;
}

static NodeId addBox(Scene *s, MeshId cube, const PbrParams &p, const Vec3 &at, const Vec3 &size)
{
    const NodeId n = s->createNode();
    if (!n || !s->attachMesh(n, cube, s->createPbrMaterial(p))) return 0;
    enginetest::setNodeScale(s, n, size);
    enginetest::setNodePosition(s, n, at);
    return n;
}

static float luma(const Image &img, unsigned x, unsigned y)
{
    const size_t i = (size_t(y) * img.width + x) * 4u;
    return 0.2126f * img.rgba[i] + 0.7152f * img.rgba[i + 1] + 0.0722f * img.rgba[i + 2];
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-far-record-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    View *view = e->createOffscreenView("farrecord", kWidth, kHeight, Colour(0.30f, 0.40f, 0.55f));
    Scene *s = e->createScene("farrecord");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("SKIP: no ray-query device here - the far copies are the ray tier's\n");
        return 77;
    }
    s->setAmbient(Colour(0.30f, 0.40f, 0.55f), Colour(0.02f, 0.02f, 0.02f));
    enginetest::addDirectionalLight(s, Vec3(-1.0f, -0.15f, 0.0f), 3.0f);   // from +X
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    PbrParams mp;
    mp.albedo = Colour(0.95f, 0.95f, 0.95f);
    mp.metalness = 1.0f;
    mp.roughness = 0.0f;
    const NodeId mirror = addBox(s, cube, mp, Vec3(0.0f, 0.0f, -3.0f), Vec3(14.0f, 10.0f, 0.02f));
    PbrParams sp;
    sp.albedo = Colour(0.85f, 0.85f, 0.85f);
    sp.roughness = 0.9f;
    const NodeId sphere = s->createNode();
    const MeshId ball = s->createMesh(twoLevelSphere());
    CHECK(mirror && sphere && ball && s->attachMesh(sphere, ball, s->createPbrMaterial(sp)),
          "the mirror and the two-level sphere exist");
    enginetest::setNodePosition(s, sphere, Vec3(0.0f, 0.0f, 90.0f));
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.bounces = true;   // (the bounces by feedback: PHOTON-7 stage 4 deleted the count)
    gi.cascadeCount = 1;
    gi.cascadeSet[0] = GiParams::GiCascadeDesc{ 16.0f, 64, 0.0f };
    CHECK(s->setGlobalIllumination(gi), "GI builds (one 16 m cascade: a 50 m near length)");
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    fx.reflectSources = ReflectSources::Rays;
    view->setPostFx(fx);

    // THE REFLECTION: the camera at the origin facing the mirror.
    enginetest::testCameraLookAt(view, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -3.0f));
    enginetest::frames(e, 40);
    const RayQueryStatus rq = s->published().rayQuery;
    CHECK(rq.reflect && rq.reflectFrame > 0u, "the reflection runs (%u traced frames; a still mirror holds)",
          unsigned(rq.reflectFrame));
    Image refl;
    CHECK(view->readPixels(refl) && refl.width == kWidth && refl.height == kHeight, "the reflection reads back");
    // THE REFERENCE: the mirrored camera, the mirror hidden.
    s->setNodeVisible(mirror, false);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 0.0f, -6.0f), Vec3(0.0f, 0.0f, 90.0f));
    enginetest::frames(e, 24);
    Image ref;
    CHECK(view->readPixels(ref) && ref.width == kWidth && ref.height == kHeight, "the reference reads back");
    if (failures) { std::printf("rtreflect.far_record: FAIL\n"); return 1; }
    // (JAH_FAR_RECORD_DUMP=<dir>: the two pictures as PPM, the reflection flipped - the row's own evidence)
    if (const char *dir = std::getenv("JAH_FAR_RECORD_DUMP")) {
        for (int k = 0; k < 2; ++k) {
            const Image &img = k ? refl : ref;
            FILE *f = std::fopen((std::string(dir) + (k ? "/reflection.ppm" : "/reference.ppm")).c_str(), "wb");
            if (!f) continue;
            std::fprintf(f, "P6 %u %u 255\n", img.width, img.height);
            for (unsigned y = 0; y < img.height; ++y)
                for (unsigned x = 0; x < img.width; ++x) {
                    const size_t i = (size_t(y) * img.width + (k ? img.width - 1u - x : x)) * 4u;
                    std::fwrite(&img.rgba[i], 1, 3, f);
                }
            std::fclose(f);
        }
    }

    // THE SPHERE'S DISK in the reference: off its own background (a corner), eroded by 3 px.
    const float bg[3] = { float(ref.rgba[0]), float(ref.rgba[1]), float(ref.rgba[2]) };
    std::vector<unsigned char> disk(size_t(kWidth) * kHeight, 0);
    for (unsigned y = 0; y < kHeight; ++y)
        for (unsigned x = 0; x < kWidth; ++x) {
            const size_t i = (size_t(y) * kWidth + x) * 4u;
            float d = 0.0f;
            for (int c = 0; c < 3; ++c) d = std::max(d, std::fabs(float(ref.rgba[i + c]) - bg[c]));
            disk[size_t(y) * kWidth + x] = d > 20.0f ? 1 : 0;
        }
    std::vector<unsigned> px;
    for (unsigned y = 3; y + 3 < kHeight; ++y)
        for (unsigned x = 3; x + 3 < kWidth; ++x) {
            bool in = true;
            for (int dy = -3; dy <= 3 && in; ++dy)
                for (int dx = -3; dx <= 3 && in; ++dx)
                    in = disk[size_t(int(y) + dy) * kWidth + size_t(int(x) + dx)] != 0;
            if (in) px.push_back(y * kWidth + x);
        }
    CHECK(px.size() > 1000u, "the sphere's disk holds %zu pixels", px.size());
    // THE CLASSES: each image's own 10th and 90th percentile luma inside the disk, the thirds between them.
    const auto thirds = [&](const Image &img, bool flip, float &lo, float &hi) {
        std::vector<float> l;
        for (unsigned i : px) {
            const unsigned x = i % kWidth, y = i / kWidth;
            l.push_back(luma(img, flip ? kWidth - 1u - x : x, y));
        }
        std::sort(l.begin(), l.end());
        const float p10 = l[l.size() / 10], p90 = l[l.size() * 9 / 10];
        lo = p10 + (p90 - p10) / 3.0f;
        hi = p10 + 2.0f * (p90 - p10) / 3.0f;
        return p90 - p10;
    };
    float rLo, rHi, mLo, mHi;
    const float refContrast = thirds(ref, false, rLo, rHi);
    const float reflContrast = thirds(refl, true, mLo, mHi);
    CHECK(refContrast > 20.0f && reflContrast > 20.0f,
          "both pictures show the lit and the dark half (contrast %.1f / %.1f codes)", refContrast, reflContrast);
    size_t classified = 0, wrong = 0;
    for (unsigned i : px) {
        const unsigned x = i % kWidth, y = i / kWidth;
        const float a = luma(ref, x, y), b = luma(refl, kWidth - 1u - x, y);
        if (a > rHi) { ++classified; if (b < mLo) ++wrong; }
        else if (a < rLo) { ++classified; if (b > mHi) ++wrong; }
    }
    std::vector<unsigned char> rowSeen(kHeight, 0);
    size_t rows = 0;
    for (unsigned i : px)
        if (!rowSeen[i / kWidth]) { rowSeen[i / kWidth] = 1; ++rows; }
    std::printf("   the reflected far sphere: %zu of %zu classified pixels on the other side of the terminator, over %zu rows\n",
                wrong, classified, rows);
    CHECK(wrong <= rows, "the far copy's record shades the triangle it hit (%zu disagree, bar one a row: %zu)", wrong, rows);
    std::printf("%s\n", failures ? "rtreflect.far_record: FAIL" : "rtreflect.far_record: PASS");
    return failures ? 1 : 0;
}
