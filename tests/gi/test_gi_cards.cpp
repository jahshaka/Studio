// gi.card_capture / gi.card_shadow / gi.card_budget — the SURFACE CACHE's gates
// (SURFACE-CACHE-1b, phase 2 of SPECS/SURFACE_CACHE_ASSESSMENT.md §7).
//
// THREE CTEST NAMES, ONE BINARY, selected by argv — because the three questions
// need three different scenes and a GI arm binds process-wide state
// (sVctBindingOwner), so they must not share a process, while the fixture
// scaffolding is the same in all three.
//
//   gi.card_capture  the cache captures what it says it captures, and an edit
//                    costs exactly the cards it should: a moved instance
//                    re-allocates ITS cards and nothing else, a material edit
//                    re-captures without freeing a page, a light write likewise.
//                    Plus the FRAME CONVENTION — the six axis frames the bake,
//                    the capture and phase 4's read all share, asserted
//                    right-handed.
//   gi.card_shadow   THE MEASUREMENT this phase exists for: a card's shadow
//                    term is OCCLUSION BY ANOTHER OBJECT, not the prepass's
//                    constant 1.0. SURFACE-CACHE-0 could not take it (a scratch
//                    scene with one item can only self-shadow); this one
//                    captures in the real scene manager with the scene's shadow
//                    node, so a wall's card under a crate reads dark where the
//                    crate's shadow falls and bright beside it.
//   gi.card_budget   the queue drains at the texel budget and never over it,
//                    in Lumen's priority order, and a small budget takes more
//                    frames rather than more time.
//
// WHAT NO CASE ASSERTS: a picture. Nothing reads a card until phase 4, so the
// selftest hashes and every pixel suite must be untouched by this lane — which
// is asserted by their own suites, not by this one.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                \
        std::printf(__VA_ARGS__);                                               \
        std::printf("\n");                                                      \
        if (!(cond)) ++failures;                                                \
    } while (0)

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

/// The helpers' cube carries no UVs; a textured one is needed wherever a card
/// must show a pattern rather than a silhouette.
static MeshData texturedCubeMesh()
{
    MeshData d = enginetest::unitCubeMesh();
    for (int f = 0; f < 6; ++f) {
        const float uv[4][2] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };
        for (int v = 0; v < 4; ++v) d.uvs.insert(d.uvs.end(), { uv[v][0], uv[v][1] });
    }
    return d;
}

/// THE SIX-FACE BOX CARD LIST for a unit cube of half-extent `h`, in MESH
/// space — the shape the bake's generator produces for a convex mesh, written
/// by hand here so an engine suite needs no document and no .jmb.
static std::vector<MeshCardDesc> boxCards(float h, float margin = 0.02f)
{
    std::vector<MeshCardDesc> cards;
    for (unsigned a = 0; a < 6u; ++a) {
        MeshCardDesc c;
        c.axis = static_cast<unsigned char>(a);
        c.lodLevel = 0;
        c.origin = Vec3(0, 0, 0);
        c.halfU = h;
        c.halfV = h;
        c.halfDepth = h + margin;
        cards.push_back(c);
    }
    return cards;
}

static float dot3(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 cross3(const Vec3 &a, const Vec3 &b) {
    return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

// ---------------------------------------------------------------------------
// The frame convention — an arithmetic case, no engine needed
// ---------------------------------------------------------------------------
static void checkFrames()
{
    std::printf("\n== the six axis frames (the contract the bake, the capture and phase 4 share)\n");
    for (unsigned a = 0; a < 6u; ++a) {
        const Vec3 d = cardAxisDirection(a), u = cardAxisU(a), v = cardAxisV(a);
        const Vec3 uv = cross3(u, v);
        // RIGHT-HANDED: u x v = the axis. A left-handed row is a REFLECTION, and
        // a capture camera built from a reflection is not a rotation at all —
        // which is exactly what the +Y row was until this lane.
        CHECK_MSG(std::fabs(uv.x - d.x) < 1e-6f && std::fabs(uv.y - d.y) < 1e-6f &&
                      std::fabs(uv.z - d.z) < 1e-6f,
                  "axis %u: u x v = the axis (%.0f %.0f %.0f)", a, uv.x, uv.y, uv.z);
        // ...and orthonormal, which is what lets a world hit be projected with
        // three dot products and no matrix.
        CHECK_MSG(std::fabs(dot3(u, v)) < 1e-6f && std::fabs(dot3(u, d)) < 1e-6f &&
                      std::fabs(dot3(v, d)) < 1e-6f,
                  "axis %u: the frame is orthogonal", a);
        CHECK_MSG(std::fabs(dot3(u, u) - 1.0f) < 1e-6f && std::fabs(dot3(v, v) - 1.0f) < 1e-6f &&
                      std::fabs(dot3(d, d) - 1.0f) < 1e-6f,
                  "axis %u: the frame is unit", a);
    }
}

// ---------------------------------------------------------------------------
struct Fixture {
    std::unique_ptr<Engine> engine;
    Engine *e = nullptr;
    View *view = nullptr;
    Scene *s = nullptr;
};

static bool makeFixture(Fixture &f, const char *name)
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = std::string("test-") + name + "-ogre.log";
    f.engine = Engine::create(cfg, err);
    if (!f.engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return false; }
    f.engine->setFixedFrameDelta(1.0f / 60.0f);
    f.e = f.engine.get();
    f.view = f.e->createOffscreenView(name, 192u, 192u, Colour(0, 0, 0));
    f.s = f.e->createScene(name);
    if (!f.view || !f.s) { std::printf("FAIL: view/scene: %s\n", f.e->lastError().c_str()); return false; }
    f.view->setScene(f.s);
    return true;
}

/// The GI arm every case runs under: a pinned volume so the cache's numbers are
/// about the cache and not about where an automatic fit landed.
static GiParams baseGi()
{
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.testBoundsMin = Vec3(-16.0f, -2.0f, -16.0f);
    gi.testBoundsMax = Vec3(16.0f, 12.0f, 16.0f);
    gi.cards = GiToggle::On;
    return gi;
}

// ---------------------------------------------------------------------------
// gi.card_capture
// ---------------------------------------------------------------------------
static int caseCapture()
{
    checkFrames();

    Fixture f;
    if (!makeFixture(f, "cardcapture")) return 1;
    Scene *s = f.s;
    s->setAmbient(Colour(0.20f, 0.20f, 0.20f), Colour(0.15f, 0.15f, 0.15f));
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.4f), 2.0f);

    // TWO CRATES, both carded, four metres apart — so that "a moved instance
    // recaptures ITS cards and nothing else" has a second instance to be about.
    NodeId crate[2] = { 0, 0 };
    MaterialId crateMat[2] = { 0, 0 };
    for (int i = 0; i < 2; ++i) {
        crate[i] = s->createNode();
        PbrParams p;
        p.albedo = Colour(0.6f, 0.45f, 0.25f);
        p.emissive = Colour(i == 0 ? 2.0f : 0.0f, 0.0f, 0.0f);
        p.roughness = 0.6f;
        crateMat[i] = s->createPbrMaterial(p);
        MeshData md = texturedCubeMesh();
        md.cards = boxCards(0.5f);
        const MeshId mesh = s->createMesh(md);
        CHECK(crate[i] && crateMat[i] && mesh && s->attachMesh(crate[i], mesh, crateMat[i]),
              "a carded crate exists");
        enginetest::setNodeScale(s, crate[i], Vec3(2.0f, 2.0f, 2.0f));
        enginetest::setNodePosition(s, crate[i], Vec3(i == 0 ? -2.0f : 2.0f, 1.0f, 0.0f));
    }
    CHECK(s->setGlobalIllumination(baseGi()), "GI builds with the card row on");
    enginetest::testCameraLookAt(f.view, Vec3(0.0f, 2.0f, -8.0f), Vec3(0.0f, 1.0f, 0.0f));
    render(f.e, 24);

    GiStatus st = s->giStatus();
    // COPIES, NOT REFERENCES: `st` is re-assigned below and a reference into it
    // would silently read the LATEST values in every comparison (this lane made
    // that mistake once — the material counter compared equal to itself).
    const CardCacheStatus c0 = st.cards;
    CHECK(c0.built, "the atlas is built");
    std::printf("    atlas: %u pages of %u, %u bytes/texel (emissive %s), %llu bytes VRAM\n",
                c0.pages, c0.pageSize, c0.bytesPerTexel, c0.emissiveFormat.c_str(),
                (unsigned long long)c0.bytes);
    // THE DELIBERATE FORMATS: 16 bytes a texel, not the prepass's 22
    // (SURFACE-CACHE-0 §3's table — emissive RGBA16F -> 4 bytes, the
    // shadow/roughness pair RG16 -> RG8).
    CHECK_MSG(c0.bytesPerTexel == 16u, "the card texel is 16 bytes (albedo 4 + normal 4 + depth 2"
                                       " + emissive 4 + shadow/rough 2), measured %u",
              c0.bytesPerTexel);
    CHECK_MSG(c0.pageSize == 128u && c0.pages == 256u,
              "the atlas is 2k square = %u pages of %u texels", c0.pages, c0.pageSize);
    CHECK_MSG(c0.instancesResident == 2u, "both crates are resident (%u)", c0.instancesResident);
    CHECK_MSG(c0.cardsResident == 12u, "twelve cards (six a crate), measured %u", c0.cardsResident);
    CHECK_MSG(c0.captures >= 12u, "every card has been captured at least once (%llu)",
              (unsigned long long)c0.captures);
    CHECK_MSG(c0.queueLength == 0u, "the queue has drained (%u left)", c0.queueLength);
    // PHASE 4's TABLES. Nothing binds them yet; what this asserts is that the
    // layout the ray job will read is MAINTAINED — one record per allocated
    // card, and an instance table indexed by the item slot the TLAS already
    // carries as `instanceCustomIndex`.
    CHECK_MSG(c0.cardRecords == c0.cardsResident,
              "the card table describes every resident card (%u records, %u cards)",
              c0.cardRecords, c0.cardsResident);
    CHECK_MSG(c0.instanceSlots > 0u, "the instance table is indexed by the item slot (%u slots)",
              c0.instanceSlots);

    // ---- WHAT THE CAPTURE WROTE ------------------------------------------
    // The +Z card of crate 0 at its centre: the cube's +Z face, so the depth is
    // the capture margin, the normal points at the camera, the albedo is the
    // material's kD and the emissive is what was authored.
    CardSample sample;
    CHECK(s->readCardTexel(crate[0], 4u, 0.5f, 0.5f, sample), "the +Z card's centre reads back");
    if (sample.ok) {
        std::printf("    +Z card centre: albedo %.4f %.4f %.4f  normal %.2f %.2f %.2f"
                    "  depth %.4f  shadow %.3f  rough(alpha) %.3f  emissive %.3f\n",
                    sample.albedo[0], sample.albedo[1], sample.albedo[2], sample.normal[0],
                    sample.normal[1], sample.normal[2], sample.depth, sample.shadow,
                    sample.roughness, sample.emissive[0]);
        // ALBEDO IS kD — HlmsPbs keeps a datablock's diffuse pre-divided by pi
        // (SURFACE-CACHE-0's finding 0; phase 3 must not divide again).
        const float expect = 0.6f / 3.14159265358979323846f;
        CHECK_MSG(std::fabs(sample.albedo[0] - expect) < 0.01f,
                  "the albedo is kD = albedo/pi (%.4f, expected %.4f)", sample.albedo[0], expect);
        CHECK_MSG(sample.normal[2] > 0.8f, "the normal faces the capture camera (z %.2f)",
                  sample.normal[2]);
        CHECK_MSG(sample.depth > 0.0f && sample.depth < 0.2f,
                  "the depth is the capture margin, not zero and not the far plane (%.4f)",
                  sample.depth);
        CHECK_MSG(sample.emissive[0] > 1.0f, "the emissive survived the four-byte HDR store (%.3f)",
                  sample.emissive[0]);
    }
    // ...and a texel the object does not cover reads the CLEAR, which is what a
    // phase-4 depth test rejects a hit on.
    CardSample empty;
    if (s->readCardTexel(crate[0], 4u, 0.99f, 0.99f, empty)) {
        // The card is exactly the box's face, so the very corner may or may not
        // be covered; what must hold is that an uncovered texel reads depth 0.
        std::printf("    +Z card corner: depth %.4f (0 = uncovered)\n", empty.depth);
    }

    // ---- INVALIDATION: A MOVE COSTS ITS OWN CARDS ------------------------
    const unsigned long long capturesBefore = c0.captures;
    const unsigned long long transformBefore = c0.invalidTransform;
    const unsigned pagesBefore = c0.pagesUsed;
    enginetest::setNodePosition(s, crate[1], Vec3(2.5f, 1.0f, 0.0f));
    render(f.e, 8);
    st = s->giStatus();
    const CardCacheStatus c1 = st.cards;
    CHECK_MSG(c1.invalidTransform > transformBefore,
              "the moved instance was seen to move (%llu -> %llu)",
              (unsigned long long)transformBefore, (unsigned long long)c1.invalidTransform);
    CHECK_MSG(c1.cardsResident == 12u, "both crates still hold six cards each (%u)",
              c1.cardsResident);
    CHECK_MSG(c1.pagesUsed == pagesBefore, "the page count is unchanged (%u)", c1.pagesUsed);
    // THE WHOLE POINT: six new captures, not twelve. The other crate's cards are
    // in the atlas, unmoved and untouched — `gi.material_swap`'s counter model.
    // SIX, AND THE WINDOW ALLOWS SEVEN because the counter is read after a
    // fixed number of frames rather than after a drained queue: a single card
    // of the OTHER crate may have been queued by the same frame's light or
    // residency bookkeeping and captured inside the window. Twelve would mean
    // the whole resident set was re-captured, which is the defect the assertion
    // is about; seven cannot be that.
    const unsigned long long moved = c1.captures - capturesBefore;
    CHECK_MSG(moved >= 6u && moved <= 7u,
              "a moved instance re-captured ITS six cards and nothing else (%llu captures)",
              moved);

    // ---- INVALIDATION: A MATERIAL EDIT RE-CAPTURES, AND FREES NOTHING ----
    const unsigned long long capturesBeforeMat = c1.captures;
    const unsigned pagesBeforeMat = c1.pagesUsed;
    {
        PbrParams p;
        p.albedo = Colour(0.2f, 0.7f, 0.3f);
        p.roughness = 0.6f;
        CHECK(s->setPbrMaterial(crateMat[1], p), "a crate's material is edited");
    }
    render(f.e, 12);
    st = s->giStatus();
    const CardCacheStatus c2 = st.cards;
    CHECK_MSG(c2.invalidMaterial > c1.invalidMaterial,
              "the material edit reached the cache (%llu -> %llu)",
              (unsigned long long)c1.invalidMaterial, (unsigned long long)c2.invalidMaterial);
    CHECK_MSG(c2.pagesUsed == pagesBeforeMat,
              "a material edit frees NO page (%u -> %u)", pagesBeforeMat, c2.pagesUsed);
    // SIX, NOT TWELVE: the material hook is PRECISE — only the instances
    // WEARING the edited material are thrown back on the queue. That is the
    // whole point of the model MATERIAL-SWAP-GI-1 built for the voxel side, and
    // it is what makes a hover preview cost the object under the mouse.
    const unsigned long long matCaptures = c2.captures - capturesBeforeMat;
    // Six, with the same one-card window and for the same reason as above.
    CHECK_MSG(matCaptures >= 6u && matCaptures <= 7u,
              "a material edit re-captured the cards of the instance WEARING it and nothing"
              " else (%llu captures)", matCaptures);
    // ...and the new albedo is in the atlas.
    CardSample after;
    if (s->readCardTexel(crate[1], 4u, 0.5f, 0.5f, after) && after.ok) {
        const float expectG = 0.7f / 3.14159265358979323846f;
        CHECK_MSG(std::fabs(after.albedo[1] - expectG) < 0.02f,
                  "the card carries the NEW albedo (g %.4f, expected %.4f)", after.albedo[1],
                  expectG);
    }

    // ---- THE ATLAS MAPPING IS NOT MIRRORED IN v -------------------------
    // Nothing else in this suite exercises v: a card's +Z face is uniform, so
    // an upside-down atlas rect would read identically. This arm makes the
    // card's two v halves DIFFERENT and asserts the read agrees with the
    // capture — which is what keeps the GPU record phase 4 is told to port
    // (`CardGpuRec::uvScaleBias`, whose scale.y is negative) honest against the
    // CPU read (`sampleCard`, which flips v).
    {
        const NodeId tall = s->createNode();
        PbrParams p;
        p.albedo = Colour(0.5f, 0.5f, 0.5f);
        p.roughness = 0.5f;
        const MaterialId mat = s->createPbrMaterial(p);
        MeshData md = texturedCubeMesh();
        md.cards = boxCards(0.5f);
        const MeshId mesh = s->createMesh(md);
        CHECK(tall && mat && mesh && s->attachMesh(tall, mesh, mat), "the v-asymmetric cube exists");
        // AN EMISSIVE MAP THAT IS BRIGHT ON ITS TOP HALF AND DARK ON ITS
        // BOTTOM. The cube's +Z face carries the texture's own 0..1 v with
        // v = 0 at the TOP row of the image (texturedCubeMesh's uv table), and
        // the card's +v is world +Y, so the BRIGHT half must read at card
        // v > 0.5.
        std::vector<unsigned char> tex(32u * 32u * 4u);
        for (unsigned y = 0; y < 32u; ++y)
            for (unsigned x = 0; x < 32u; ++x) {
                unsigned char *o = &tex[(size_t(y) * 32u + x) * 4u];
                const bool top = y < 16u;
                o[0] = top ? 255u : 0u; o[1] = 0u; o[2] = 0u; o[3] = 255u;
            }
        const TextureId em = s->createTexture(32u, 32u, tex.data(), true, false);
        CHECK(em && s->setPbrTexture(mat, PbrTextureSlot::Emissive, em),
              "...wearing an emissive map that is bright on its top half");
        enginetest::setNodeScale(s, tall, Vec3(2.0f, 2.0f, 2.0f));
        enginetest::setNodePosition(s, tall, Vec3(0.0f, 1.0f, -3.0f));
        render(f.e, 16);
        CardSample up, down;
        const bool okUp = s->readCardTexel(tall, 4u, 0.5f, 0.75f, up);
        const bool okDown = s->readCardTexel(tall, 4u, 0.5f, 0.25f, down);
        CHECK(okUp && okDown && up.ok && down.ok, "both halves of the +Z card read back");
        if (up.ok && down.ok) {
            std::printf("    +Z card: v 0.75 emissive %.3f, v 0.25 emissive %.3f\n",
                        up.emissive[0], down.emissive[0]);
            CHECK_MSG(up.emissive[0] > down.emissive[0] + 0.2f,
                      "the card's HIGH v is the world's UP (%.3f vs %.3f) — the atlas rect is"
                      " not mirrored in v",
                      up.emissive[0], down.emissive[0]);
        }
    }

    // ---- A SMALL CARD IS SUB-ALLOCATED, AND THE MASK HOLDS ---------------
    // A 10 cm mesh wants a 7-texel card, which the allocator floors at
    // `kCardMinSize`. At 8 that page's slot mask would be 256 slots in a
    // 64-bit word — undefined, and on x86 three quarters of the page reads as
    // permanently taken. This arm exists so that a future change to the floor
    // has to answer to a test.
    {
        const NodeId tiny = s->createNode();
        PbrParams p;
        p.albedo = Colour(0.9f, 0.1f, 0.1f);
        const MaterialId mat = s->createPbrMaterial(p);
        MeshData md = enginetest::unitCubeMesh();
        md.cards = boxCards(0.5f);
        const MeshId mesh = s->createMesh(md);
        CHECK(tiny && mat && mesh && s->attachMesh(tiny, mesh, mat), "a 10 cm mesh exists");
        enginetest::setNodeScale(s, tiny, Vec3(0.1f, 0.1f, 0.1f));
        enginetest::setNodePosition(s, tiny, Vec3(-3.0f, 0.5f, -2.0f));
        render(f.e, 16);
        CardSample small;
        CHECK(s->readCardAt(Vec3(-3.0f, 0.5f, -2.05f), Vec3(0, 0, -1), small) && small.ok,
              "the 10 cm mesh's card is allocated and captured");
        const CardCacheStatus st2 = s->giStatus().cards;
        std::printf("    after a 10 cm mesh: %u instances, %u cards, %u of %u pages\n",
                    st2.instancesResident, st2.cardsResident, st2.pagesUsed, st2.pages);
        CHECK_MSG(st2.pagesUsed <= st2.pages, "the page count is sane (%u of %u)", st2.pagesUsed,
                  st2.pages);
    }

    // ---- A TURN IS A TRANSFORM CHANGE, EVEN WHEN THE BOX DOES NOT MOVE ---
    // A 90 degree turn of a cube leaves its world AABB exactly where it was
    // while every card now describes a different face. A signature built on
    // the box alone cannot see it; this arm is why the signature carries the
    // derived orientation too.
    {
        const CardCacheStatus before = s->giStatus().cards;
        const float s45 = 0.70710678f;
        s->setNodeTransform(crate[0], Vec3(-2.0f, 1.0f, 0.0f), Quat(0.0f, s45, 0.0f, s45),
                            Vec3(2.0f, 2.0f, 2.0f));
        render(f.e, 16);
        const CardCacheStatus after2 = s->giStatus().cards;
        std::printf("    a 90 degree turn: invalidTransform %llu -> %llu, captures +%llu\n",
                    (unsigned long long)before.invalidTransform,
                    (unsigned long long)after2.invalidTransform,
                    (unsigned long long)(after2.captures - before.captures));
        CHECK_MSG(after2.invalidTransform > before.invalidTransform,
                  "a TURN that leaves the world box alone still re-allocates the cards (%llu ->"
                  " %llu)",
                  (unsigned long long)before.invalidTransform,
                  (unsigned long long)after2.invalidTransform);
    }

    // ---- A HOVER PREVIEW IS A MATERIAL SWAP, NOT A MATERIAL EDIT ---------
    // MATERIAL-SWAP-GI-1's in-place swap writes the node's material and swaps
    // the datablock on the live Item WITHOUT destroying or editing a material,
    // so `noteMaterialChanged` is never called. The cache hears about it by
    // re-reading the candidate's material every frame, and this is the arm that
    // says so: the previous arm edits the SAME material's params and cannot.
    {
        PbrParams p;
        p.albedo = Colour(0.05f, 0.05f, 0.95f);
        p.roughness = 0.4f;
        const MaterialId preset = s->createPbrMaterial(p);
        const CardCacheStatus before = s->giStatus().cards;
        CHECK(preset && s->setNodeMaterial(crate[0], preset),
              "a preset is hovered onto a carded crate (an in-place swap)");
        render(f.e, 16);
        const CardCacheStatus after2 = s->giStatus().cards;
        CHECK_MSG(after2.invalidMaterial > before.invalidMaterial,
                  "the SWAP reached the cache (%llu -> %llu)",
                  (unsigned long long)before.invalidMaterial,
                  (unsigned long long)after2.invalidMaterial);
        CHECK_MSG(after2.pagesUsed == before.pagesUsed,
                  "...and freed no page (%u -> %u)", before.pagesUsed, after2.pagesUsed);
        CardSample swapped;
        if (s->readCardTexel(crate[0], 4u, 0.5f, 0.5f, swapped) && swapped.ok) {
            const float expectB = 0.95f / 3.14159265358979323846f;
            std::printf("    after the hover: albedo %.4f %.4f %.4f (expected b %.4f)\n",
                        swapped.albedo[0], swapped.albedo[1], swapped.albedo[2], expectB);
            CHECK_MSG(std::fabs(swapped.albedo[2] - expectB) < 0.02f,
                      "the atlas carries the PRESET's albedo, not the one it replaced (b %.4f)",
                      swapped.albedo[2]);
        }
    }

    // ---- RESIDENCY BY DISTANCE ------------------------------------------
    GiParams gi = baseGi();
    gi.cardResidencyRadius = 4.0f;   // the camera is 8 m back: both crates leave
    CHECK(s->setGlobalIllumination(gi), "the residency radius shrinks");
    render(f.e, 8);
    st = s->giStatus();
    CHECK_MSG(st.cards.instancesResident == 0u,
              "nothing beyond the radius holds pages (%u instances)", st.cards.instancesResident);
    CHECK_MSG(st.cards.pagesUsed == 0u, "...and every page came back (%u used)",
              st.cards.pagesUsed);
    gi.cardResidencyRadius = 60.0f;
    CHECK(s->setGlobalIllumination(gi), "the radius grows again");
    render(f.e, 12);
    st = s->giStatus();
    // FOUR, not two: the arms above added a v-asymmetric cube and a 10 cm mesh
    // to the scene, and both are carded. What this asserts is that widening the
    // radius brings the whole resident set BACK, whatever its size.
    CHECK_MSG(st.cards.instancesResident >= 2u, "the resident set comes back (%u instances)",
              st.cards.instancesResident);

    // ---- THE ROW OFF FREES EVERYTHING ------------------------------------
    gi.cards = GiToggle::Off;
    CHECK(s->setGlobalIllumination(gi), "the card row goes off");
    render(f.e, 4);
    st = s->giStatus();
    CHECK_MSG(!st.cards.built, "the atlas is gone with the row");
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// gi.card_shadow — THE measurement of this phase
// ---------------------------------------------------------------------------
static int caseShadow()
{
    Fixture f;
    if (!makeFixture(f, "cardshadow")) return 1;
    Scene *s = f.s;
    // A BLACK AMBIENT, so the shadow term is the only thing that can vary
    // across the card. (The prepass writes the term directly; the ambient never
    // reaches it — but a dark scene keeps the picture honest too.)
    s->setAmbient(Colour(0.0f, 0.0f, 0.0f), Colour(0.0f, 0.0f, 0.0f));

    // THE FLOOR IS THE SUBJECT: a wide, flat slab whose +Y card is one page of
    // ground. A crate sits on it, and the sun comes down almost vertically, so
    // the crate's shadow falls on a KNOWN part of that card.
    const NodeId floorNode = s->createNode();
    {
        PbrParams p;
        p.albedo = Colour(0.8f, 0.8f, 0.8f);
        p.roughness = 0.8f;
        const MaterialId mat = s->createPbrMaterial(p);
        MeshData md = enginetest::unitCubeMesh();
        md.cards = boxCards(0.5f);
        const MeshId mesh = s->createMesh(md);
        CHECK(floorNode && mat && mesh && s->attachMesh(floorNode, mesh, mat),
              "the carded floor slab exists");
    }
    // 8 m x 8 m, 20 cm thick, its top at y = 0.
    enginetest::setNodeScale(s, floorNode, Vec3(8.0f, 0.2f, 8.0f));
    enginetest::setNodePosition(s, floorNode, Vec3(0.0f, -0.1f, 0.0f));

    // THE OCCLUDER: a 2 m crate standing on the floor's +X half. It carries NO
    // cards of its own — it is here to cast, not to be captured — which also
    // proves the capture's include channel is doing its job: if the crate
    // rendered INTO the floor's card, the card would hold the crate's own
    // surface instead of the floor's shadowed one.
    const NodeId occluder = s->createNode();
    {
        PbrParams p;
        p.albedo = Colour(0.5f, 0.5f, 0.5f);
        p.roughness = 0.6f;
        const MaterialId mat = s->createPbrMaterial(p);
        const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
        CHECK(occluder && mat && mesh && s->attachMesh(occluder, mesh, mat), "the occluder exists");
    }
    enginetest::setNodeScale(s, occluder, Vec3(2.0f, 2.0f, 2.0f));
    enginetest::setNodePosition(s, occluder, Vec3(2.0f, 1.0f, 0.0f));

    // A SUN STRAIGHT DOWN, casting. Straight down is what makes the shadow's
    // place on the card arithmetic rather than a guess: the crate covers
    // x in [1, 3], z in [-1, 1], so the floor's +Y card is dark exactly there.
    const NodeId sun = s->createNode();
    {
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = 2.0f / 3.14159265358979323846f;
        l.castShadows = true;
        // -Y is the document's light direction; an identity rotation aims it
        // straight down.
        s->setNodeTransform(sun, Vec3(0, 0, 0), Quat(), Vec3(1, 1, 1));
        CHECK(sun && s->setLight(sun, l), "a shadow-casting sun points straight down");
    }
    f.view->setShadows(true);

    GiParams gi = baseGi();
    gi.cardResidencyRadius = 40.0f;
    // A BUDGET THAT HOLDS THE WHOLE RESIDENT SET IN ONE FRAME, and the reason
    // is a MEASURED limitation the capture still carries. The pin's shadow node
    // caches its light list AND its casters box per (camera, compositor-manager
    // frame count) — a hand-driven workspace never bumps that count — so every
    // card captured after a frame's FIRST reuses the first card's fit and reads
    // a flat 1.0 wherever that fit does not reach.
    //
    // TWO ATTEMPTS ARE RECORDED AT THE CAPTURE, both MEASURED and both failed:
    // a separate wide cull camera, and ALTERNATING TWO capture cameras (which
    // defeats `mLastCamera == newCamera` and gives every card its own camera —
    // shipped anyway, because a per-card camera is strictly closer to correct
    // and costs nothing). Neither restored the profile at the shipped
    // three-cards-a-frame budget: the casters box is fitted under the pass's
    // subject-only visibility mask, which is the half neither attempt moves.
    // Phase 3 owns a card's lighting and is where this belongs.
    //
    // This case is about the CONTENT of a card; `gi.card_budget` is the one
    // about the cadence.
    gi.cardBudgetTexels = 64u * 128u * 128u;
    CHECK(s->setGlobalIllumination(gi), "GI builds");
    enginetest::testCameraLookAt(f.view, Vec3(0.0f, 6.0f, -10.0f), Vec3(0.0f, 0.0f, 0.0f));
    render(f.e, 32);

    GiStatus st = s->giStatus();
    CHECK(st.cards.built && st.cards.cardsResident > 0u, "the floor's cards are resident");
    std::printf("    %u cards resident, %llu captured, %.3f ms last frame\n",
                st.cards.cardsResident, (unsigned long long)st.cards.captures,
                st.cards.captureMs);

    // THE FLOOR'S TOP SURFACE, READ AS A RAY WOULD READ IT. The floor is 8 m
    // square, so its +Y card is SPLIT across sixteen pages (Lumen's rule: a
    // card wider than a page is cut into whole pages so its texel stays fine) —
    // which means there is no "the +Y card" to index, and asking by INDEX is
    // asking the wrong question anyway. `readCardAt` asks the right one, and it
    // is the one phase 4 asks at a hit: what does the cache hold at this world
    // point, on a surface facing this way?
    //
    // The crate is 2 m wide at x = 2, so with the sun straight down its shadow
    // covers x in [1, 3], z in [-1, 1]. Two points on the floor's top face:
    // under the crate, and the mirror point at x = -2 that nothing can occlude.
    const Vec3 up(0.0f, 1.0f, 0.0f);
    const Vec3 pShadow(2.0f, 0.0f, 0.0f), pLit(-2.0f, 0.0f, 0.0f);

    std::printf("    shadow across the floor's top face (z = 0):\n      ");
    for (int i = -4; i <= 4; ++i) {
        CardSample row;
        if (s->readCardAt(Vec3(float(i), 0.0f, 0.0f), up, row) && row.ok)
            std::printf("x%+d:%.2f ", i, row.shadow);
        else
            std::printf("x%+d:---- ", i);
    }
    std::printf("\n");

    CardSample inShadow, inLight;
    const bool gotShadow = s->readCardAt(pShadow, up, inShadow);
    const bool gotLit = s->readCardAt(pLit, up, inLight);
    CHECK(gotShadow && gotLit && inShadow.ok && inLight.ok,
          "the cache answers at both points of the floor's top face");
    if (inShadow.ok && inLight.ok) {
        std::printf("    under the crate: shadow %.4f, depth %.4f, albedo %.3f\n",
                    inShadow.shadow, inShadow.depth, inShadow.albedo[0]);
        std::printf("    beside it      : shadow %.4f, depth %.4f, albedo %.3f\n",
                    inLight.shadow, inLight.depth, inLight.albedo[0]);
        // BOTH POINTS ARE FLOOR — the include channel kept the occluder out of
        // the capture, so the depth test accepted both and the albedo is the
        // floor's own. Had the crate rendered into the card, the point under it
        // would carry the crate's surface two metres nearer and the depth test
        // would have REJECTED it, which is the other way this reads.
        CHECK_MSG(std::fabs(inShadow.depth - inLight.depth) < 0.02f,
                  "both points read the FLOOR's own surface (depths %.4f vs %.4f) — the"
                  " capture's include channel kept the occluder out of the card",
                  inShadow.depth, inLight.depth);
        // ...AND THE SHADOW TERM IS OCCLUSION BY THE OTHER OBJECT. This is the
        // line SURFACE-CACHE-0 could not write: its capture scene held one item
        // and no light, so its cards read the prepass's constant 1.0 everywhere.
        CHECK_MSG(inLight.shadow > 0.7f, "the unoccluded point is LIT (%.4f)", inLight.shadow);
        CHECK_MSG(inShadow.shadow < 0.3f,
                  "the point under the crate is SHADOWED BY THE CRATE (%.4f) — a card's shadow"
                  " term is occlusion by another object, measured",
                  inShadow.shadow);
        CHECK_MSG(inLight.shadow - inShadow.shadow > 0.4f,
                  "...and the two differ by %.3f, which no constant branch can produce",
                  inLight.shadow - inShadow.shadow);
    }

    // AND THE SHADOW MOVES WITH THE LIGHT. A light write throws every card back
    // on the queue; after the sun tilts, the same texel's term must change.
    {
        // Tilt the sun 40 degrees about Z so the shadow slides in -X: the texel
        // under the crate comes out into the light.
        const float ang = 0.7f;   // radians
        Quat q(0.0f, 0.0f, std::sin(ang * 0.5f), std::cos(ang * 0.5f));
        s->setNodeTransform(sun, Vec3(0, 0, 0), q, Vec3(1, 1, 1));
    }
    const unsigned long long lightBefore = st.cards.invalidLight;
    render(f.e, 40);
    st = s->giStatus();
    CHECK_MSG(st.cards.invalidLight > lightBefore, "the light write reached the cache (%llu)",
              (unsigned long long)st.cards.invalidLight);
    CardSample afterTilt;
    if (s->readCardAt(pShadow, up, afterTilt) && afterTilt.ok) {
        std::printf("    after the sun tilts: shadow %.4f (was %.4f)\n", afterTilt.shadow,
                    inShadow.shadow);
        CHECK_MSG(std::fabs(afterTilt.shadow - inShadow.shadow) > 0.2f,
                  "the card's shadow term FOLLOWED the light (%.4f -> %.4f)", inShadow.shadow,
                  afterTilt.shadow);
    }
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// gi.card_budget
// ---------------------------------------------------------------------------
static int caseBudget()
{
    Fixture f;
    if (!makeFixture(f, "cardbudget")) return 1;
    Scene *s = f.s;
    s->setAmbient(Colour(0.15f, 0.15f, 0.15f), Colour(0.10f, 0.10f, 0.10f));
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, 0.4f), 2.0f);

    // TWENTY-FOUR CRATES = 144 cards, more than any one frame's budget here.
    PbrParams p;
    p.albedo = Colour(0.6f, 0.45f, 0.25f);
    p.roughness = 0.6f;
    const MaterialId mat = s->createPbrMaterial(p);
    MeshData md = enginetest::unitCubeMesh();
    md.cards = boxCards(0.5f);
    const MeshId mesh = s->createMesh(md);
    for (int i = 0; i < 24; ++i) {
        const NodeId n = s->createNode();
        if (!n || !s->attachMesh(n, mesh, mat)) { std::printf("FAIL: crate %d\n", i); return 1; }
        enginetest::setNodeScale(s, n, Vec3(2.0f, 2.0f, 2.0f));
        enginetest::setNodePosition(s, n, Vec3(float(i % 6) * 3.0f - 7.5f, 1.0f,
                                               float(i / 6) * 3.0f - 4.5f));
    }

    GiParams gi = baseGi();
    gi.cardResidencyRadius = 60.0f;
    // A DELIBERATELY SMALL BUDGET: two 128-texel cards a frame.
    gi.cardBudgetTexels = 2u * 128u * 128u;
    CHECK(s->setGlobalIllumination(gi), "GI builds with a two-card budget");
    enginetest::testCameraLookAt(f.view, Vec3(0.0f, 8.0f, -16.0f), Vec3(0.0f, 1.0f, 0.0f));

    // ONE FRAME AT A TIME, reading what it spent.
    unsigned worstCaptures = 0u, worstTexels = 0u;
    unsigned long long total = 0ull;
    unsigned frames = 0u;
    for (; frames < 200u; ++frames) {
        render(f.e, 1);
        const CardCacheStatus st = s->giStatus().cards;
        if (!st.built) continue;
        worstCaptures = std::max(worstCaptures, st.capturesLastFrame);
        worstTexels = std::max(worstTexels, st.texelsLastFrame);
        total = st.captures;
        if (st.queueLength == 0u && st.captures > 0ull) break;
    }
    const CardCacheStatus st = s->giStatus().cards;
    std::printf("    %u instances, %u cards, drained in %u frames; worst frame %u captures /"
                " %u texels against a %u budget\n",
                st.instancesResident, st.cardsResident, frames + 1u, worstCaptures, worstTexels,
                st.budgetTexels);
    CHECK_MSG(st.cardsResident > 0u, "cards are resident (%u)", st.cardsResident);
    // THE BUDGET IS A CEILING, and it is never gone over: the drain stops
    // BEFORE a card that would not fit, so no frame spends more than it was
    // given.
    CHECK_MSG(worstTexels <= st.budgetTexels,
              "no frame spent more than the budget (%u <= %u)", worstTexels, st.budgetTexels);
    CHECK_MSG(worstCaptures <= 2u, "no frame captured more than two cards (%u)", worstCaptures);
    // ...and a queue of 144 cards at two a frame takes many frames, which is the
    // whole reason a budget exists.
    CHECK_MSG(frames + 1u >= st.cardsResident / 2u,
              "the queue took at least %u frames to drain at two a frame (%u)",
              st.cardsResident / 2u, frames + 1u);
    CHECK_MSG(st.queueLength == 0u, "the queue drained (%u left)", st.queueLength);
    CHECK_MSG(total == st.cardsResident,
              "every resident card was captured exactly once (%llu captures, %u cards)",
              (unsigned long long)total, st.cardsResident);

    // A BIGGER BUDGET DRAINS IN FEWER FRAMES. The knob does something.
    GiParams big = gi;
    big.cardBudgetTexels = 16u * 128u * 128u;
    CHECK(s->setGlobalIllumination(big), "the budget grows to sixteen cards a frame");
    // Move every instance so the whole set re-allocates and re-queues.
    render(f.e, 2);
    const unsigned long long before = s->giStatus().cards.captures;
    {
        GiParams shrink = big;
        shrink.cardResidencyRadius = 1.0f;
        s->setGlobalIllumination(shrink);
        render(f.e, 3);
        s->setGlobalIllumination(big);
    }
    unsigned bigFrames = 0u;
    for (; bigFrames < 200u; ++bigFrames) {
        render(f.e, 1);
        const CardCacheStatus b = s->giStatus().cards;
        if (b.built && b.queueLength == 0u && b.captures > before) break;
    }
    std::printf("    re-queued at sixteen a frame: drained in %u frames (was %u at two)\n",
                bigFrames + 1u, frames + 1u);
    CHECK_MSG(bigFrames + 1u < frames + 1u,
              "a bigger budget drains in fewer frames (%u < %u)", bigFrames + 1u, frames + 1u);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv)
{
    const std::string which = argc > 1 ? argv[1] : "capture";
    int rc = 0;
    if (which == "capture") rc = caseCapture();
    else if (which == "shadow") rc = caseShadow();
    else if (which == "budget") rc = caseBudget();
    else { std::printf("FAIL: unknown case '%s'\n", which.c_str()); return 1; }
    std::printf("\n%s: %d failure(s)\n", which.c_str(), failures);
    return rc;
}
