// gi.card_capture / gi.card_shadow / gi.card_budget — the SURFACE CACHE's gates
// (SURFACE-CACHE-1b, phase 2 of SPECS/SURFACE_CACHE_ASSESSMENT.md §7).
//
// THREE CTEST NAMES, ONE BINARY, selected by argv — because the three questions
// need three different scenes, one process each, while the fixture scaffolding
// is the same in all three.
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
//   gi.card_lighting THE LIT CARD (PHOTON-CARDS-1): the sixth layer's radiance
//                    equals HlmsPbs's diffuse lobe (pbsDirect's closed form) x
//                    the captured shadow term within 2 %, follows a light's
//                    intensity with no recapture, and a tilted sun.
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
    // ...and THE SIXTH LAYER, radiance (PHOTON-CARDS-1): four more bytes where
    // the device stores R11G11B10F from a compute job (RGBA16F's eight else).
    // ...and the cached INDIRECT half beside it, in the same format.
    const unsigned radBytes = c0.radianceFormat == "R11G11B10F" ? 4u : 8u;
    CHECK_MSG(c0.bytesPerTexel == 16u + 2u * radBytes,
              "the card texel is %u bytes (albedo 4 + normal 4 + depth 2 + emissive 4 +"
              " shadow/rough 2 + radiance %u + indirect %u, %s), measured %u",
              16u + 2u * radBytes, radBytes, radBytes, c0.radianceFormat.c_str(), c0.bytesPerTexel);
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
    // capture — which is what keeps the CPU read (`sampleCard`, which flips v)
    // honest; the ray job's GLSL pick (jah_rq_card.glsl) uses the same integer
    // rule and gi.card_read_parity holds it to this read.
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
        // ...AND A SUB-PAGE CARD HOLDS ITS WHOLE PICTURE (PHOTON-CARDS-1). The
        // same map on a 0.5 m cube cuts a 32-texel card: the capture must RENDER
        // into the 32-texel square the copy takes, or the card holds the
        // top-left eighth of its own picture — both halves then read the bright
        // top, which is what this lane measured before the capture's viewport
        // followed the card.
        const NodeId small = s->createNode();
        CHECK(small && s->attachMesh(small, mesh, mat), "the 0.5 m v-asymmetric cube exists");
        enginetest::setNodeScale(s, small, Vec3(0.5f, 0.5f, 0.5f));
        enginetest::setNodePosition(s, small, Vec3(3.5f, 0.25f, -3.0f));
        render(f.e, 16);
        CardSample sUp, sDown;
        const bool okSUp = s->readCardTexel(small, 4u, 0.5f, 0.75f, sUp);
        const bool okSDown = s->readCardTexel(small, 4u, 0.5f, 0.25f, sDown);
        CHECK(okSUp && okSDown && sUp.ok && sDown.ok, "both halves of the sub-page card read back");
        if (sUp.ok && sDown.ok)
            CHECK_MSG(sUp.emissive[0] > sDown.emissive[0] + 0.2f,
                      "a 32-texel card holds its whole face: v 0.75 emissive %.3f, v 0.25 %.3f",
                      sUp.emissive[0], sDown.emissive[0]);
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
    // THE SHIPPED BUDGET — the High tier's own, no override. This case used to
    // run with a budget that captured the whole resident set in ONE frame,
    // because only a frame that followed a hand-driven scene-graph update
    // (the GI build's) had a light list at all: the capture ran before the
    // frame's `updateSceneGraph`, after the previous frame's `clearFrameData`
    // had emptied the manager's global light list, so every later capture saw
    // NO light and read a flat 1.0 (PHOTON-CARDS-1 §1.1, measured: 93 of 96
    // captures). The capture now runs inside Ogre's frame, so the profile
    // below is asserted at the budget a user runs (five cards a frame at
    // High: the floor's forty-eight cards land over ten frames).
    //
    // This case is about the CONTENT of a card; `gi.card_budget` is the one
    // about the cadence.
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
    float profile[9];
    for (int i = -4; i <= 4; ++i) {
        CardSample row;
        profile[i + 4] = -1.0f;
        if (s->readCardAt(Vec3(float(i), 0.0f, 0.0f), up, row) && row.ok) {
            std::printf("x%+d:%.2f ", i, row.shadow);
            profile[i + 4] = row.shadow;
        } else {
            std::printf("x%+d:---- ", i);
        }
    }
    std::printf("\n");
    // THE PROFILE, AT THE SHIPPED BUDGET: lit / the crate's penumbra / the
    // footprint / the penumbra / lit at x = 0..+4 — the crate covers x in
    // [1, 3] and the two edge values are the shadow filter straddling its
    // faces. Every card of the floor is captured in a different frame at this
    // budget, so this is the per-card fit, not the first card's.
    {
        const float want[5] = { 1.00f, 0.57f, 0.00f, 0.52f, 1.00f };
        for (int k = 0; k < 5; ++k)
            CHECK_MSG(std::fabs(profile[4 + k] - want[k]) <= 0.1f,
                      "the floor's shadow term at x = +%d is %.2f (want %.2f +- 0.10)", k,
                      profile[4 + k], want[k]);
    }

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
    // on the queue; after the sun tilts, the crate's shadow slides off its
    // footprint and falls on floor that was lit.
    //
    // THE POINT IS BESIDE THE CRATE, NOT UNDER IT. The floor under a crate that
    // STANDS on it is occluded from every sun above the horizon, so "the term
    // under the crate changes with the tilt" is not physics — the case used to
    // assert exactly that, and passed only because the re-captures after the
    // tilt saw an empty light list and read 1.0.
    const float ang = 0.7f;   // radians, about Z
    const Quat tilt(0.0f, 0.0f, std::sin(ang * 0.5f), std::cos(ang * 0.5f));
    // Where the light now goes: the document light points down -Y, rotated.
    const Vec3 d0(0.0f, -1.0f, 0.0f);
    const Vec3 dir(d0.x * std::cos(ang) - d0.y * std::sin(ang), d0.x * std::sin(ang) + d0.y * std::cos(ang),
                   0.0f);
    // The crate's 2 m top throws its shadow `shift` metres along x; the probe
    // is halfway across the part of that band that lay outside the footprint.
    const float shift = 2.0f * dir.x / -dir.y;
    const float side = shift > 0.0f ? 1.0f : -1.0f;
    const Vec3 pSlide(2.0f + side * (1.0f + 0.5f * std::fabs(shift)), 0.0f, 0.0f);
    CardSample beforeTilt;
    const bool gotBefore = s->readCardAt(pSlide, up, beforeTilt) && beforeTilt.ok;
    s->setNodeTransform(sun, Vec3(0, 0, 0), tilt, Vec3(1, 1, 1));
    const unsigned long long lightBefore = st.cards.invalidLight;
    render(f.e, 40);
    st = s->giStatus();
    CHECK_MSG(st.cards.invalidLight > lightBefore, "the light write reached the cache (%llu)",
              (unsigned long long)st.cards.invalidLight);
    CardSample afterTilt;
    if (gotBefore && s->readCardAt(pSlide, up, afterTilt) && afterTilt.ok) {
        std::printf("    at x %+.2f (the shadow slides %.2f m): shadow %.4f before the tilt, %.4f"
                    " after\n",
                    pSlide.x, shift, beforeTilt.shadow, afterTilt.shadow);
        CHECK_MSG(beforeTilt.shadow > 0.9f && afterTilt.shadow < 0.1f,
                  "the card's shadow term FOLLOWED the light: lit floor beside the crate went"
                  " dark when the shadow slid over it (%.4f -> %.4f)",
                  beforeTilt.shadow, afterTilt.shadow);
    } else {
        CHECK(false, "the cache answers beside the crate before and after the tilt");
    }
    // ...and under the crate the floor stays dark: it is occluded by the crate
    // itself from every sun above the horizon.
    CardSample under;
    if (s->readCardAt(pShadow, up, under) && under.ok)
        CHECK_MSG(under.shadow < 0.1f, "the floor UNDER the crate is still shadowed (%.4f)",
                  under.shadow);
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
// gi.card_lighting — THE LIT CARD (PHOTON-CARDS-1, SC-1c), direct half
// ---------------------------------------------------------------------------
//
// HlmsPbs's BRDF_Default diffuse lobe (200.BRDFs_piece_ps.any:144-230; the same
// transcription as `pbsDirect()` in test_gi_field_energy.cpp, with N, L and V
// free), times a light's irradiance E. A cached texel stores the
// view-INDEPENDENT diffuse, V = N (JahCardLight_cs.glsl says why), and kD is
// the datablock's diffuse already divided by pi.
static double pbsDiffuse(double kD, double E, double perceptualRoughness, const double N[3],
                         const double L[3], const double V[3])
{
    double H[3] = { L[0] + V[0], L[1] + V[1], L[2] + V[2] };
    const double hl = std::sqrt(H[0] * H[0] + H[1] * H[1] + H[2] * H[2]);
    for (int k = 0; k < 3; ++k) H[k] /= hl;
    const auto dot = [](const double a[3], const double b[3]) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    const double NdotL = std::max(0.0, dot(N, L));
    const double NdotV = std::max(0.0, dot(N, V));
    const double VdotH = std::max(0.0, dot(V, H));
    const double rp = std::max(perceptualRoughness, 1e-4);
    const double energyBias = 0.5 * rp;
    const double energyFactor = 1.0 + (1.0 / 1.51 - 1.0) * rp;
    const double fd90 = energyBias + 2.0 * VdotH * VdotH * rp;
    const double lightScatter = 1.0 + (fd90 - 1.0) * std::pow(1.0 - NdotL, 5.0);
    const double viewScatter = 1.0 + (fd90 - 1.0) * std::pow(1.0 - NdotV, 5.0);
    return NdotL * lightScatter * viewScatter * energyFactor * kD * E;
}

static int caseLighting()
{
    Fixture f;
    if (!makeFixture(f, "cardlighting")) return 1;
    Scene *s = f.s;
    // NO AMBIENT and nothing emissive on the subjects: the radiance is the
    // direct term plus the floor's bounce, and the direct half is read as the
    // radiance less the card's cached indirect half.
    s->setAmbient(Colour(0.0f, 0.0f, 0.0f), Colour(0.0f, 0.0f, 0.0f));

    const float kRough = 0.7f;
    // A MATTE CRATE, 2 m, floating (nothing shadows its faces), and a floor
    // with a second crate standing on it (the shadow arm).
    PbrParams cp;
    cp.albedo = Colour(0.6f, 0.5f, 0.4f);
    cp.roughness = kRough;
    const MaterialId crateMat = s->createPbrMaterial(cp);
    MeshData md = enginetest::unitCubeMesh();
    md.cards = boxCards(0.5f);
    const MeshId mesh = s->createMesh(md);
    const NodeId crate = s->createNode();
    CHECK(crate && crateMat && mesh && s->attachMesh(crate, mesh, crateMat), "the matte crate exists");
    enginetest::setNodeScale(s, crate, Vec3(2.0f, 2.0f, 2.0f));
    enginetest::setNodePosition(s, crate, Vec3(-4.0f, 3.0f, 0.0f));

    const NodeId floorNode = s->createNode();
    {
        PbrParams p;
        p.albedo = Colour(0.8f, 0.8f, 0.8f);
        p.roughness = kRough;
        const MaterialId mat = s->createPbrMaterial(p);
        CHECK(floorNode && mat && s->attachMesh(floorNode, mesh, mat), "the carded floor exists");
        enginetest::setNodeScale(s, floorNode, Vec3(8.0f, 0.2f, 8.0f));
        enginetest::setNodePosition(s, floorNode, Vec3(2.0f, -0.1f, 0.0f));
    }
    const NodeId occluder = s->createNode();
    {
        PbrParams p;
        p.albedo = Colour(0.5f, 0.5f, 0.5f);
        const MaterialId mat = s->createPbrMaterial(p);
        const MeshId plain = s->createMesh(enginetest::unitCubeMesh());
        CHECK(occluder && mat && plain && s->attachMesh(occluder, plain, mat), "the occluder exists");
        enginetest::setNodeScale(s, occluder, Vec3(2.0f, 2.0f, 2.0f));
        enginetest::setNodePosition(s, occluder, Vec3(3.0f, 1.0f, 0.0f));
    }

    // ONE SUN, casting, irradiance E = 2 (colour 1, intensity E / pi — the
    // engine's power scale is intensity * pi).
    const double E = 2.0;
    double curE = E;   // the sun's irradiance as it stands (an arm doubles it)
    const NodeId sun = s->createNode();
    LightDesc l;
    l.type = LightType::Directional;
    l.colour = Colour(1.0f, 1.0f, 1.0f);
    l.intensity = float(E / 3.14159265358979323846);
    l.castShadows = true;
    s->setNodeTransform(sun, Vec3(0, 0, 0), Quat(), Vec3(1, 1, 1));   // straight down
    CHECK(sun && s->setLight(sun, l), "a shadow-casting sun points straight down");
    f.view->setShadows(true);

    GiParams gi = baseGi();
    gi.cardResidencyRadius = 40.0f;
    CHECK(s->setGlobalIllumination(gi), "GI builds");
    enginetest::testCameraLookAt(f.view, Vec3(0.0f, 6.0f, -12.0f), Vec3(0.0f, 1.0f, 0.0f));
    render(f.e, 40);

    const CardCacheStatus st = s->giStatus().cards;
    std::printf("    radiance layer %s; %u cards; %llu relit (%u last frame, budget %u texels);"
                " %u bytes a texel, %.1f MB; last relight recorded in %.3f ms CPU\n",
                st.radianceFormat.c_str(), st.cardsResident, (unsigned long long)st.relights,
                st.relitLastFrame, st.lightBudgetTexels, st.bytesPerTexel,
                double(st.bytes) / (1024.0 * 1024.0), st.lightMs);
    CHECK_MSG(!st.radianceFormat.empty(), "the sixth layer exists (%s)", st.radianceFormat.c_str());
    CHECK_MSG(st.relights >= st.cardsResident && st.cardsResident > 0u,
              "every resident card has been relit (%llu relights, %u cards)",
              (unsigned long long)st.relights, st.cardsResident);

    const auto check = [&](const char *what, const Vec3 &p, const Vec3 &n, const double L[3],
                           bool expectShadowed) {
        CardSample t;
        if (!s->readCardAt(p, n, t) || !t.ok) {
            CHECK_MSG(false, "%s: the cache answers at (%.2f %.2f %.2f)", what, p.x, p.y, p.z);
            return;
        }
        const double N[3] = { n.x, n.y, n.z };
        // pbsDirect's closed form x the stored shadow term, on the ATLAS's own
        // kD (an 8-bit store: its quantisation is the atlas's, not the light's).
        // THE BAR KNOWS THE STORE (the phase B merge, 2026-09-23): the direct
        // half is the difference of TWO stored terms, the Radiance texel and
        // the Indirect texel, each in the atlas's format — R11G11B10F carries 6
        // mantissa bits on red and green and 5 on blue, so one step is 1/64
        // and 1/32 of the value's octave (2.0-3.1 % on blue). The job ROUNDS
        // TO NEAREST before the truncating store (PHOTON-CARDS-2 A2: half a
        // step added to the float's own bits), so each stored term sits at
        // most HALF a step off — the constant pre-scale it replaced left blue a
        // whole step high at the top of an octave (2.12 % on the tilted crate
        // top, 0.09 % after). The physics bar stays 2 %; half the store's
        // quantum of each stored term is added to it, computed from the format
        // the status reports, never assumed. (RGBA16F: 10 bits, a 0.1 % step —
        // the same rule, no room needed.)
        const auto storeQuantum = [&st](double v, int k) -> double {
            if (!(v > 0.0)) return 0.0;
            const int bits = st.radianceFormat == "R11G11B10F" ? (k == 2 ? 5 : 6) : 10;
            return 0.5 * std::ldexp(1.0, std::ilogb(v) - bits);
        };
        for (int k = 0; k < 3; ++k) {
            const double want = pbsDiffuse(t.albedo[k], curE, kRough, N, L, N) * double(t.shadow);
            // THE DIRECT HALF: the radiance less its cached indirect half (the
            // floor's bounce is real here; gi.card_lighting_indirect holds it to
            // the pixel).
            const double got = double(t.radiance[k]) - double(t.indirect[k]);
            const double quantum = storeQuantum(t.radiance[k], k) + storeQuantum(t.indirect[k], k);
            const double tol = 0.02 * want + quantum;
            const double rel = want > 1e-6 ? std::fabs(got - want) / want : std::fabs(got);
            CHECK_MSG(want > 1e-6 ? std::fabs(got - want) <= tol : std::fabs(got) < 2e-3,
                      "%s channel %d: direct half %.5f (radiance %.5f - indirect %.5f),"
                      " pbsDirect x shadow (%.3f) = %.5f (%.2f%%; bar 2%% + half the store quanta %.5f)",
                      what, k, got, t.radiance[k], t.indirect[k], double(t.shadow), want,
                      100.0 * rel, quantum);
        }
        if (expectShadowed)
            CHECK_MSG(t.shadow < 0.1f, "%s: the stored shadow term is dark (%.3f)", what, t.shadow);
        else
            CHECK_MSG(t.shadow > 0.9f, "%s: the stored shadow term is lit (%.3f)", what, t.shadow);
    };

    // 1. The crate's TOP under a vertical sun: NdotL = 1, the lobe is
    //    energyFactor alone.
    const double down[3] = { 0.0, 1.0, 0.0 };   // TOWARDS the light
    check("crate top, sun overhead", Vec3(-4.0f, 4.0f, 0.3f), Vec3(0, 1, 0), down, false);
    // 2. The floor beside the occluder (lit) and under it (occluded): the SAME
    //    lobe times the captured term.
    check("floor, lit", Vec3(-1.0f, 0.0f, 0.5f), Vec3(0, 1, 0), down, false);
    check("floor, under the occluder's shadow", Vec3(3.0f, 0.0f, 0.0f), Vec3(0, 1, 0), down, true);

    // 3. A TILTED sun, 60 degrees off vertical about Z: NdotL < 1 on the top
    //    and a side lights up, so the Disney lobe's lightScatter term is live.
    //    A colour-only arm first: the radiance must follow an INTENSITY change
    //    with no recapture (the radiance signature, not the shadow one).
    const unsigned long long capturesBefore = s->giStatus().cards.captures;
    const unsigned long long radianceBefore = s->giStatus().cards.invalidRadiance;
    l.intensity = float(2.0 * E / 3.14159265358979323846);
    CHECK(s->setLight(sun, l), "the sun's intensity doubles");
    curE = 2.0 * E;
    render(f.e, 20);
    {
        const CardCacheStatus a = s->giStatus().cards;
        CHECK_MSG(a.invalidRadiance > radianceBefore, "an intensity change reached the cache as a"
                  " RADIANCE change (%llu -> %llu)", (unsigned long long)radianceBefore,
                  (unsigned long long)a.invalidRadiance);
        CHECK_MSG(a.captures == capturesBefore, "...and recaptured nothing (%llu -> %llu)",
                  (unsigned long long)capturesBefore, (unsigned long long)a.captures);
        CardSample t;
        if (s->readCardAt(Vec3(-4.0f, 4.0f, 0.3f), Vec3(0, 1, 0), t) && t.ok) {
            const double N[3] = { 0.0, 1.0, 0.0 };
            const double want = pbsDiffuse(t.albedo[1], 2.0 * E, kRough, N, down, N) * t.shadow;
            const double got = double(t.radiance[1]) - double(t.indirect[1]);
            CHECK_MSG(std::fabs(got - want) <= 0.02 * want,
                      "the crate top's direct half doubled with the light (%.5f, want %.5f)", got, want);
        }
    }
    const float ang = 3.14159265f / 3.0f;
    s->setNodeTransform(sun, Vec3(0, 0, 0), Quat(0.0f, 0.0f, std::sin(ang * 0.5f), std::cos(ang * 0.5f)),
                        Vec3(1, 1, 1));
    render(f.e, 40);
    // The light now TRAVELS along R(-Y) = (sin a, -cos a, 0), so it comes FROM
    // (-sin a, cos a, 0).
    const double tilted[3] = { -std::sin(double(ang)), std::cos(double(ang)), 0.0 };
    check("crate top, sun 60 degrees off", Vec3(-4.0f, 4.0f, 0.3f), Vec3(0, 1, 0), tilted, false);
    check("crate -X side, sun 60 degrees off", Vec3(-5.0f, 3.0f, 0.3f), Vec3(-1, 0, 0), tilted, false);

    // 4. A LAMP NO CAMERA SEES, LIGHTING A SURFACE NO CAMERA SEES. A card lights
    //    what a ray HITS, off screen, so its lights are the SCENE's and never
    //    the frame's camera-culled list. A carded panel 30 m BEHIND the view
    //    camera, its lit face turned away from it, and a point lamp in front of
    //    that face — outside the view's frustum and outside every capture
    //    camera's (each looks INTO its own card's box, no deeper than it). The
    //    card's direct term must be pbsDirect for the lamp; and the same number
    //    again with the camera turned round to see both.
    const NodeId panel = s->createNode();
    {
        PbrParams p;
        p.albedo = Colour(0.6f, 0.6f, 0.6f);
        p.roughness = kRough;
        const MaterialId mat = s->createPbrMaterial(p);
        CHECK(panel && mat && s->attachMesh(panel, mesh, mat), "a carded panel stands behind the camera");
        s->setNodeTransform(panel, Vec3(0.0f, 1.5f, -30.0f), Quat(), Vec3(2.0f, 2.0f, 0.2f));
    }
    const NodeId lamp = s->createNode();
    const Vec3 lampPos(0.0f, 1.5f, -33.0f);
    const double kLampRange = 8.0;
    LightDesc pl;
    pl.type = LightType::Point;
    pl.colour = Colour(1.0f, 1.0f, 1.0f);
    pl.intensity = 1.0f;
    pl.range = float(kLampRange);
    pl.castShadows = false;
    s->setNodeTransform(lamp, lampPos, Quat(), Vec3(1, 1, 1));
    CHECK(lamp && s->setLight(lamp, pl), "a point lamp in front of the panel's far face, behind the camera");
    render(f.e, 60);
    // (Measured: with the relight reading the frame's camera-culled light list,
    // this arm reads a direct term of 0 — no rendered camera holds the lamp.)
    pl.intensity = 1.5f;
    s->setLight(lamp, pl);
    render(f.e, 30);
    // The panel's -Z face is at z = -30.1; the texel read, and the closed form
    // for the lamp: E = intensity * pi (the engine's power scale) times the
    // authored-range curve 1 / (0.5 + (0.5 / R^2) d^2) (OgreScene::setLight:
    // setAttenuation(R, 0.5, 0, 0.5 / R^2)) times patch 0018's fade (R - d) / R.
    const Vec3 pt(0.3f, 1.7f, -30.1f);
    const auto lampDirect = [&](const CardSample &t, double outL[3], double &E) {
        const double dx = lampPos.x - pt.x, dy = lampPos.y - pt.y, dz = lampPos.z - pt.z;
        const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        outL[0] = dx / d; outL[1] = dy / d; outL[2] = dz / d;
        const double atten = 1.0 / (0.5 + (0.5 / (kLampRange * kLampRange)) * d * d) *
                             std::max((kLampRange - d) / kLampRange, 0.0);
        E = double(pl.intensity) * 3.14159265358979323846 * atten;
        (void)t;
    };
    const double Nback[3] = { 0.0, 0.0, -1.0 };
    const auto lampArm = [&](const char *what, double &outDirect) {
        CardSample t;
        outDirect = -1.0;
        if (!s->readCardAt(pt, Vec3(0, 0, -1), t) || !t.ok) {
            CHECK_MSG(false, "%s: the card answers on the panel's far face", what);
            return;
        }
        double L[3], E;
        lampDirect(t, L, E);
        const double want = pbsDiffuse(t.albedo[1], E, kRough, Nback, L, Nback);
        const double got = double(t.radiance[1]) - double(t.indirect[1]);
        outDirect = got;
        const double rel = want > 1e-6 ? std::fabs(got - want) / want : 1.0;
        CHECK_MSG(want > 0.02 && rel <= 0.05,
                  "%s: the off-screen card's direct term %.5f is pbsDirect for the off-screen lamp"
                  " %.5f within 5 %% (%.2f %%)", what, got, want, 100.0 * rel);
    };
    double behind = -1.0, seen = -1.0;
    lampArm("lamp and panel behind the camera", behind);
    CHECK_MSG(s->giStatus().cards.lightsDropped == 0u,
              "no light was dropped from the relight's list (%u)", s->giStatus().cards.lightsDropped);
    // ...and with the camera turned round to see both, the relight asked for
    // by a light write (twice the intensity, then back), the same number.
    enginetest::testCameraLookAt(f.view, Vec3(0.0f, 2.0f, -22.0f), Vec3(0.0f, 1.5f, -32.0f));
    pl.intensity = 2.0f;
    s->setLight(lamp, pl);
    render(f.e, 20);
    pl.intensity = 1.5f;
    s->setLight(lamp, pl);
    render(f.e, 40);
    lampArm("camera turned to see both", seen);
    CHECK_MSG(behind > 0.0 && std::fabs(seen - behind) <= 0.02 * behind,
              "the off-screen relight equals the on-screen one (%.5f vs %.5f)", behind, seen);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// gi.card_lighting_indirect — THE LIT CARD's INDIRECT half against the pixel
// ---------------------------------------------------------------------------
//
// The card's indirect is the pixel's own diffuse GI (the one voxel reader's
// six-cone march + the one environment at each escape, through BRDF_EnvMap's
// envColourD x diffuse x pi x the energy factor) evaluated from the texel, so
// at the same world point the two must agree. The fixture is the field-energy
// one (tests/gi/test_gi_field_energy.cpp): a matte floor under a vertical sun
// and a matte WALL standing on its edge, facing it — the sun is perpendicular
// to the wall, so the wall's pixel is its indirect term and nothing else
// (F0 = 0: the Specular workflow at ior 1.0 with a black specular colour, so no
// environment specular either), read as RADIANCE through the view's float
// readback (HDR-READBACK-1).
// The irradiance field is OFF (it routes the pixel's diffuse at every shipped
// tier: the trap file's rule), and so are the rays and the gather — the pixel's
// diffuse is then exactly the cone march this job ports.
/// THE CARD'S INDIRECT CONVENTION against a head-on pixel (PHOTON-CARDS-2 audit
/// F4). The pixel's environment lobe reflects envColourD x kD x pi x A(NdotV, r),
/// the Disney lobe's DIRECTIONAL albedo at its own view angle; a card texel is
/// read from every direction and stores the lobe's HEMISPHERICAL mean,
/// A_hemi(r) = 2 x integral of A(mu, r) mu dmu (the bounce job's convention). So
/// a pixel seen head-on (NdotV = 1) and the card at the same point differ by
/// exactly A_hemi(r) / A(1, r) — 1.020 at r = 1 — and the suites compare the card
/// to the pixel times that ratio. Both halves by direct integration of the lobe
/// (enginetest::disneyDiffuseAlbedo), never by the shader's fit.
static double hemiOverHeadOn(double r)
{
    const int n = 64;
    double hemi = 0.0;
    for (int i = 0; i < n; ++i) {
        const double mu = (double(i) + 0.5) / double(n);
        hemi += 2.0 * enginetest::disneyDiffuseAlbedo(mu, r) * mu / double(n);
    }
    return hemi / enginetest::disneyDiffuseAlbedo(1.0, r);
}

static int caseLightingIndirect()
{
    const unsigned kPx = 256u;
    // The walls are roughness 1 and read head-on: the pixel re-expressed in the
    // card's hemispherical convention (hemiOverHeadOn's comment).
    const double kConv = hemiOverHeadOn(1.0);
    std::printf("    the card's hemispherical-mean albedo over the head-on pixel's, r = 1: %.4f\n", kConv);
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-cardlightingindirect-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("cardind", kPx, kPx, Colour(0, 0, 0));
    Scene *s = e->createScene("cardind");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;
    fx.hdr = false;         // no tonemap, no exposure, no dither
    fx.hdrReadback = true;  // the scene RADIANCE, in float (field_energy's currency)
    view->setPostFx(fx);
    view->setShadows(true);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    const MeshId plain = s->createMesh(enginetest::unitCubeMesh());
    MeshData md = enginetest::unitCubeMesh();
    md.cards = boxCards(0.5f);
    const MeshId carded = s->createMesh(md);
    const auto matte = [&](float albedo) {
        PbrParams p;
        p.albedo = Colour(albedo, albedo, albedo);
        p.roughness = 1.0f;
        p.workflow = PbrParams::Workflow::Specular;
        p.ior = 1.0f;
        p.specularColour = Colour(0.0f, 0.0f, 0.0f);
        return s->createPbrMaterial(p);
    };
    // The floor: top at y = 0, x in [-6, 6], z in [-12, 0].
    const NodeId floorNode = s->createNode();
    CHECK(floorNode && s->attachMesh(floorNode, plain, matte(0.8f)), "the matte floor exists");
    s->setNodeTransform(floorNode, Vec3(0.0f, -0.15f, -6.0f), Quat(), Vec3(12.0f, 0.3f, 12.0f));
    // The wall, carded: 12 x 4 m, its FRONT face at z = 0 facing -Z (the floor).
    const NodeId wall = s->createNode();
    CHECK(wall && s->attachMesh(wall, carded, matte(0.7f)), "the carded matte wall exists");
    s->setNodeTransform(wall, Vec3(0.0f, 2.0f, 0.15f), Quat(), Vec3(12.0f, 4.0f, 0.3f));
    const double kSunPower = 12.0;
    const NodeId sun = enginetest::addDirectionalLight(s, Vec3(0.0f, -1.0f, 0.0f), float(kSunPower));
    CHECK(sun != 0, "the sun is straight down: the wall's only light is the floor's bounce");

    GiParams gi = baseGi();
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::Off;
    gi.cardResidencyRadius = 40.0f;
    CHECK(s->setGlobalIllumination(gi), "GI builds (the chain, no field, no gather)");
    s->setRayTracing(RayTracingMode::Off);
    // Straight at the wall's face from the floor's side, orthographic.
    CameraDesc cam;
    cam.position = Vec3(0.0f, 2.0f, -10.0f);
    cam.orientation = Quat{ 0.0f, 1.0f, 0.0f, 0.0f };   // -Z forward turned to +Z
    cam.orthographic = true;
    cam.orthoSize = 3.0f;
    cam.farClip = 200.0f;
    view->setCamera(cam);
    render(e, 60);

    const CardCacheStatus st = s->giStatus().cards;
    std::printf("    indirect: on %d, %llu marches (budget %u texels), %llu relights\n",
                int(st.indirectOn), (unsigned long long)st.indirectRelights,
                st.indirectBudgetTexels, (unsigned long long)st.relights);
    CHECK_MSG(st.indirectOn && st.indirectRelights > 0ull,
              "the relight job marched the chain (%llu marches)",
              (unsigned long long)st.indirectRelights);

    ImageF img;
    CHECK(view->readPixelsHdr(img), "the view reads its radiance back");
    // World (x, y) on the wall's face -> pixel: screen right is world -X after
    // the half turn, screen down is world -Y.
    const auto toPixel = [&](double wx, double wy, double &px, double &py) {
        px = (-wx / 3.0 * 0.5 + 0.5) * kPx;
        py = (-(wy - 2.0) / 3.0 * 0.5 + 0.5) * kPx;
    };
    const double heights[3] = { 0.8, 1.6, 2.8 };
    const double xs[2] = { -1.5, 1.5 };
    for (double x : xs)
        for (double h : heights) {
            double px, py, m[3];
            toPixel(x, h, px, py);
            // A 9 x 9 block of the float read.
            double sum[3] = { 0, 0, 0 };
            int n = 0;
            for (int y = int(py) - 4; y <= int(py) + 4; ++y)
                for (int xx = int(px) - 4; xx <= int(px) + 4; ++xx) {
                    const Colour c = img.at(unsigned(xx), unsigned(y));
                    sum[0] += c.r; sum[1] += c.g; sum[2] += c.b;
                    ++n;
                }
            for (int k = 0; k < 3; ++k) m[k] = sum[k] / n * kConv;
            CardSample t;
            const bool ok = s->readCardAt(Vec3(float(x), float(h), 0.0f), Vec3(0, 0, -1), t) && t.ok;
            if (!ok) {
                CHECK_MSG(false, "the card answers on the wall at (%.1f, %.1f)", x, h);
                continue;
            }
            std::printf("    wall (%+.1f, %.1f): pixel %.4f %.4f %.4f | card indirect %.4f %.4f %.4f"
                        " | radiance %.4f\n",
                        x, h, m[0], m[1], m[2], t.indirect[0], t.indirect[1], t.indirect[2],
                        t.radiance[0]);
            CHECK_MSG(m[0] > 0.05, "the pixel is lit (%.4f)", m[0]);
            for (int k = 0; k < 3; ++k) {
                const double rel = m[k] > 1e-4 ? std::fabs(t.indirect[k] - m[k]) / m[k] : 1.0;
                CHECK_MSG(rel <= 0.05,
                          "at (%+.1f, %.1f) channel %d: the card's indirect %.4f equals the pixel"
                          " diffuse %.4f within 5 %% (%.2f %%) — the two marches are one piece",
                          x, h, k, t.indirect[k], m[k], 100.0 * rel);
            }
            // ...and the card's radiance is that indirect and nothing else (no
            // direct: the sun is perpendicular; no emissive).
            CHECK_MSG(std::fabs(t.radiance[0] - t.indirect[0]) <= 0.02 * t.indirect[0] + 1e-3,
                      "the wall's radiance is its indirect half (%.4f vs %.4f)", t.radiance[0],
                      t.indirect[0]);
        }

    // THE COMPARISON, as a function: the card's indirect against the pixel at
    // the three heights of x = -1.5, both read NOW.
    const auto compareAll = [&](const char *what) {
        ImageF im;
        view->readPixelsHdr(im);
        for (double h : heights) {
            double px, py;
            toPixel(-1.5, h, px, py);
            double m[3] = { 0.0, 0.0, 0.0 };
            int n = 0;
            for (int y = int(py) - 4; y <= int(py) + 4; ++y)
                for (int xx = int(px) - 4; xx <= int(px) + 4; ++xx) {
                    const Colour c = im.at(unsigned(xx), unsigned(y));
                    m[0] += c.r; m[1] += c.g; m[2] += c.b;
                    ++n;
                }
            for (double &v : m) v = v / n * kConv;
            CardSample t;
            if (!s->readCardAt(Vec3(-1.5f, float(h), 0.0f), Vec3(0, 0, -1), t) || !t.ok) {
                CHECK_MSG(false, "%s: the card answers at h %.1f", what, h);
                continue;
            }
            // ALL THREE CHANNELS: the sky ambient is chromatic.
            for (int k = 0; k < 3; ++k) {
                const double rel = m[k] > 1e-4 ? std::fabs(t.indirect[k] - m[k]) / m[k] : 1.0;
                CHECK_MSG(m[k] > 0.02 && rel <= 0.05,
                          "%s, h %.1f, channel %d: card indirect %.4f, pixel diffuse x A_hemi/A(1) %.4f (%.2f %%,"
                          " bar 5 %%)", what, h, k, t.indirect[k], m[k], 100.0 * rel);
            }
        }
    };

    // A DRAGGED LIGHT IS NOT A RE-INJECTION (audit F2). A light written every
    // frame for thirty frames, with nothing scheduling the chain's refresh,
    // leaves the voxels where they are — so the card must not re-march its
    // indirect against them. Then the refresh + settle lands, and the
    // indirect is re-marched in ONE burst (the resident set once, over
    // however many frames its budget takes).
    {
        const NodeId lamp = s->createNode();
        LightDesc pl;
        pl.type = LightType::Point;
        pl.colour = Colour(1.0f, 0.9f, 0.8f);
        pl.intensity = 0.5f;
        pl.range = 6.0f;
        pl.castShadows = false;
        s->setNodeTransform(lamp, Vec3(0.0f, 1.0f, -3.0f), Quat(), Vec3(1, 1, 1));
        CHECK(lamp && s->setLight(lamp, pl), "a lamp stands in front of the wall");
        s->refreshGlobalIllumination();
        render(e, 90);   // its arrival lands and settles
        const CardCacheStatus d0 = s->giStatus().cards;
        for (int i = 0; i < 30; ++i) {
            s->setNodeTransform(lamp, Vec3(-1.5f + 0.1f * float(i), 1.0f, -3.0f), Quat(),
                                Vec3(1, 1, 1));
            render(e, 1);
        }
        const CardCacheStatus d1 = s->giStatus().cards;
        std::printf("    a 30-frame lamp drag with no refresh: indirect marches %llu -> %llu,"
                    " re-injections seen %llu -> %llu\n",
                    (unsigned long long)d0.indirectRelights, (unsigned long long)d1.indirectRelights,
                    (unsigned long long)d0.invalidIndirect, (unsigned long long)d1.invalidIndirect);
        CHECK_MSG(d1.indirectRelights == d0.indirectRelights,
                  "a dragged light re-marched NO card's indirect (%llu -> %llu marches) — the"
                  " signature follows the chain, not the write",
                  (unsigned long long)d0.indirectRelights, (unsigned long long)d1.indirectRelights);
        s->refreshGlobalIllumination();
        render(e, 90);
        const CardCacheStatus d2 = s->giStatus().cards;
        std::printf("    ...then the refresh + settle: re-injections seen %llu -> %llu, marches"
                    " %llu -> %llu (%u cards resident)\n",
                    (unsigned long long)d1.invalidIndirect, (unsigned long long)d2.invalidIndirect,
                    (unsigned long long)d1.indirectRelights, (unsigned long long)d2.indirectRelights,
                    d2.cardsResident);
        CHECK_MSG(d2.invalidIndirect == d1.invalidIndirect + 1ull,
                  "the refresh + settle reached the cards as exactly ONE re-injection burst"
                  " (%llu -> %llu)", (unsigned long long)d1.invalidIndirect,
                  (unsigned long long)d2.invalidIndirect);
        CHECK_MSG(d2.indirectRelights > d1.indirectRelights,
                  "...and re-marched the indirect (%llu -> %llu)",
                  (unsigned long long)d1.indirectRelights, (unsigned long long)d2.indirectRelights);
        s->removeNode(lamp);
        s->refreshGlobalIllumination();
        render(e, 90);
    }

    // THE INDIRECT HALF HAS ITS OWN TRIGGER: the sun's intensity rises by
    // half — a light write, so the chain re-injects (the light tick) and the
    // wall's INDIRECT is marched again; the shadow signature does not move, so
    // nothing is recaptured.
    const unsigned long long capturesBefore = s->giStatus().cards.captures;
    const unsigned long long indBefore = s->giStatus().cards.invalidIndirect;
    CardSample before;
    s->readCardAt(Vec3(-1.5f, 1.6f, 0.0f), Vec3(0, 0, -1), before);
    {
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1.0f, 1.0f, 1.0f);
        l.intensity = float(1.5 * kSunPower / 3.14159265358979323846);
        CHECK(s->setLight(sun, l), "the sun brightens by half");
    }
    // An engine-level scene re-injects when told to (the mirror's light-move
    // refresh, in the app): the refresh is the chain's re-injection.
    s->refreshGlobalIllumination();
    render(e, 60);
    {
        const CardCacheStatus a = s->giStatus().cards;
        CardSample after;
        s->readCardAt(Vec3(-1.5f, 1.6f, 0.0f), Vec3(0, 0, -1), after);
        std::printf("    the sun x1.5: wall indirect %.4f -> %.4f (x%.3f); invalidIndirect"
                    " %llu -> %llu; captures +%llu\n",
                    before.indirect[0], after.indirect[0],
                    before.indirect[0] > 0.0f ? after.indirect[0] / before.indirect[0] : 0.0f,
                    (unsigned long long)indBefore, (unsigned long long)a.invalidIndirect,
                    (unsigned long long)(a.captures - capturesBefore));
        CHECK_MSG(a.invalidIndirect > indBefore, "the re-injection reached the cache as an INDIRECT"
                  " change (%llu -> %llu)", (unsigned long long)indBefore,
                  (unsigned long long)a.invalidIndirect);
        CHECK_MSG(std::fabs(after.indirect[0] / std::max(before.indirect[0], 1e-6f) - 1.5f) < 0.075f,
                  "the wall's indirect followed the sun, x1.5 within 5 %% (%.4f -> %.4f)",
                  before.indirect[0], after.indirect[0]);
        CHECK_MSG(a.captures == capturesBefore, "...and nothing was recaptured (+%llu)",
                  (unsigned long long)(a.captures - capturesBefore));
    }
    compareAll("after the re-injection");

    // THE ESCAPE READS THE ONE ENVIRONMENT: a sky ambient (no cube: the
    // environment is then its SH — jahEnvCone's no-cube branch), and the card
    // and the pixel still agree.
    s->setAmbient(Colour(0.30f, 0.35f, 0.45f), Colour(0.10f, 0.08f, 0.06f));
    render(e, 90);
    compareAll("with a sky ambient (the escape's environment)");

    // AN EMISSIVE CARD TEXEL: radiance = direct + indirect + emissive. A small
    // emissive tile on the wall's face (its own carded instance, lit only by
    // the floor's bounce and the sky); the emissive term is the radiance less
    // the cached indirect less the (zero: the sun is perpendicular) direct.
    {
        const NodeId tile = s->createNode();
        PbrParams p;
        // DARK and BRIGHT: the emissive dominates the texel, so the subtraction
        // below is not a difference of two nearly equal packed floats (the
        // radiance store's half-step is 1.2 % of 0.3 on blue).
        p.albedo = Colour(0.05f, 0.05f, 0.05f);
        p.emissive = Colour(0.60f, 0.45f, 0.30f);
        p.roughness = 1.0f;
        p.workflow = PbrParams::Workflow::Specular;
        p.ior = 1.0f;
        p.specularColour = Colour(0.0f, 0.0f, 0.0f);
        const MaterialId m = s->createPbrMaterial(p);
        CHECK(tile && m && s->attachMesh(tile, carded, m), "an emissive tile stands on the wall");
        s->setNodeTransform(tile, Vec3(2.5f, 1.2f, -0.1f), Quat(), Vec3(0.8f, 0.8f, 0.2f));
        render(e, 90);
        CardSample t;
        if (s->readCardAt(Vec3(2.5f, 1.2f, -0.2f), Vec3(0, 0, -1), t) && t.ok) {
            const float want[3] = { 0.60f, 0.45f, 0.30f };
            for (int k = 0; k < 3; ++k) {
                const double em = double(t.radiance[k]) - double(t.indirect[k]);
                const double rel = std::fabs(em - want[k]) / want[k];
                CHECK_MSG(rel <= 0.02,
                          "the emissive tile, channel %d: radiance %.4f - indirect %.4f = %.4f,"
                          " the authored emissive %.3f (%.2f %%, bar 2 %%)",
                          k, t.radiance[k], t.indirect[k], em, want[k], 100.0 * rel);
            }
        } else {
            CHECK(false, "the card answers on the emissive tile");
        }
    }
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// gi.cone_integrator_parity — THE PIXEL'S CONE ANSWER = THE CARD JOB'S
// ---------------------------------------------------------------------------
//
// PHOTON-CARDS-2 part A: the diffuse cone integrator is ONE text
// (jah_voxel_cones.glsl, the piece JahVoxelCones) that the pixel shader, the
// bounce job and the card job all insert, and the card's environment lobe is
// the pixel's albedo at its hemispherical mean (hemiOverHeadOn). So at the same
// world point, seen head-on (NdotV = 1), the card's cached indirect and the
// pixel's diffuse times A_hemi/A(1) are the same arithmetic over the same
// volumes, and this holds them to 1 %.
//
// TWO WALLS in one scene (one GI arm per process — the trap file's rule): one
// facing -Z, where the cone frame is a world-axis frame, and one turned 30
// degrees about Y, where Frisvad's frame and the view-to-volume mapping of the
// pixel's cones are exercised off the axes. The fixture is
// gi.card_lighting_indirect's: a matte floor under a vertical sun, the walls lit
// only by its bounce, F0 = 0 (no specular), the field, the gather and the rays
// off (the field routes the pixel's diffuse at every shipped tier), read as
// RADIANCE through the view's float readback, orthographic and head-on.
//
// THE BAR IS 1 % PLUS THE TWO STORES' OWN HALF-QUANTA, both computed rather
// than assumed: the card's Indirect layer is R11G11B10F (6 mantissa bits on red
// and green, 5 on blue; the job rounds to nearest, so half a step of the value's
// octave), and the pixel is the view's RGBA16F scene target (10 mantissa bits:
// half a step of ITS octave). The pixel term used to be half an 8-bit code,
// 0.5/255 = 0.00196 absolute — 0.8-2.2 % of the 0.09-0.25 these walls read
// (CARDS-2's audit F6), as large as the 1 % the bar is about; in float it is
// 0.5 x 2^(ilogb(v) - 10), 3.1e-5 at 0.09 and 6.1e-5 at 0.25 — 0.02-0.03 %
// (HDR-READBACK-1).
//
// WHAT THE FLOAT READ FOUND (HDR-READBACK-1, 2026-09-24). With the pixel's term
// at its float size the WALL FACING -Z holds 1 % + the quanta at all eighteen
// samples (worst 1.48 %, within the card store's half step), and the WALL TURNED
// 30 DEGREES does not: the card reads 1.64 % and 2.06 % below the pixel at
// (-1.5, 1.6) and (-1.5, 2.8) — two whole R11G11B10F steps, not rounding, and
// the card is below the pixel at EVERY sample of both walls (a bias, not
// noise). The 8-bit term's 0.00196 absolute was hiding exactly that. So the
// turned wall is its own row, `gi.cone_integrator_parity_offaxis` (label
// photon-target: it runs, prints `target:` and does not decide a gate), and
// the owner of the card's cone frame deletes the label when the off-axis frame
// agrees; the axis-aligned wall keeps gating at the unwidened bar.
static int caseConeParity(bool offAxisTarget)
{
    const unsigned kPx = 256u;
    const double kConv = hemiOverHeadOn(1.0);   // the card's convention (hemiOverHeadOn)
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-coneparity-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("coneparity", kPx, kPx, Colour(0, 0, 0));
    Scene *s = e->createScene("coneparity");
    if (!view || !s) { std::printf("FAIL: view/scene: %s\n", e->lastError().c_str()); return 1; }
    view->setScene(s);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;
    fx.hdr = false;
    fx.hdrReadback = true;
    view->setPostFx(fx);
    view->setShadows(true);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

    const MeshId plain = s->createMesh(enginetest::unitCubeMesh());
    MeshData md = enginetest::unitCubeMesh();
    md.cards = boxCards(0.5f);
    const MeshId carded = s->createMesh(md);
    const auto matte = [&](float albedo) {
        PbrParams p;
        p.albedo = Colour(albedo, albedo, albedo);
        p.roughness = 1.0f;
        p.workflow = PbrParams::Workflow::Specular;
        p.ior = 1.0f;
        p.specularColour = Colour(0.0f, 0.0f, 0.0f);
        return s->createPbrMaterial(p);
    };
    // One floor under both walls: top at y = 0, x in [-15, 15], z in [-12, 0].
    const NodeId floorNode = s->createNode();
    CHECK(floorNode && s->attachMesh(floorNode, plain, matte(0.8f)), "the matte floor exists");
    s->setNodeTransform(floorNode, Vec3(0.0f, -0.15f, -6.0f), Quat(), Vec3(30.0f, 0.3f, 12.0f));

    // THE TWO WALLS: 8 x 4 m, carded, their front face 0.15 m ahead of their
    // centre; wall A at x = -7 facing -Z, wall B at x = +7 turned by 30 degrees.
    struct Arm { const char *name; float cx; float theta; NodeId node = 0; };
    Arm arms[2] = { { "wall facing -Z", -7.0f, 0.0f }, { "wall turned 30 degrees", 7.0f, 30.0f } };
    const auto rotY = [](float deg, const Vec3 &v) {
        const float t = deg * 3.14159265f / 180.0f;
        return Vec3(v.x * std::cos(t) + v.z * std::sin(t), v.y, -v.x * std::sin(t) + v.z * std::cos(t));
    };
    const auto quatY = [](float deg) {
        const float h = 0.5f * deg * 3.14159265f / 180.0f;
        return Quat(0.0f, std::sin(h), 0.0f, std::cos(h));
    };
    for (Arm &a : arms) {
        a.node = s->createNode();
        CHECK_MSG(a.node && s->attachMesh(a.node, carded, matte(0.7f)), "%s exists", a.name);
        s->setNodeTransform(a.node, Vec3(a.cx, 2.0f, 0.15f), quatY(a.theta), Vec3(8.0f, 4.0f, 0.3f));
    }
    const NodeId sun = enginetest::addDirectionalLight(s, Vec3(0.0f, -1.0f, 0.0f), 12.0f);
    CHECK(sun != 0, "the sun is straight down: the walls' only light is the floor's bounce");

    GiParams gi = baseGi();
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::Off;
    gi.cardResidencyRadius = 40.0f;
    CHECK(s->setGlobalIllumination(gi), "GI builds (the chain, no field, no gather)");
    s->setRayTracing(RayTracingMode::Off);

    const double heights[3] = { 0.8, 1.6, 2.8 };
    const double xs[2] = { -1.5, 1.5 };
    double worst = 0.0;
    double worstOffAxis = 0.0;
    for (const Arm &a : arms) {
        const bool offAxis = a.theta != 0.0f;
        // The camera straight at the front face, orthographic, turned with it.
        CameraDesc cam;
        const Vec3 off = rotY(a.theta, Vec3(0.0f, 0.0f, -10.15f));
        cam.position = Vec3(a.cx + off.x, 2.0f, 0.15f + off.z);
        cam.orientation = quatY(a.theta + 180.0f);
        cam.orthographic = true;
        cam.orthoSize = 3.0f;
        cam.farClip = 200.0f;
        view->setCamera(cam);
        render(e, std::getenv("JAH_CONE_PARITY_FRAMES") ? std::atoi(std::getenv("JAH_CONE_PARITY_FRAMES")) : 90);
        {
            const CardCacheStatus cs = s->giStatus().cards;
            std::printf("    %s: indirect marches %llu, relights %llu\n", a.name,
                        (unsigned long long)cs.indirectRelights, (unsigned long long)cs.relights);
        }
        ImageF img;
        CHECK(view->readPixelsHdr(img), "the view reads its radiance back");
        const Vec3 n = rotY(a.theta, Vec3(0.0f, 0.0f, -1.0f));
        const auto worldAt = [&](double lx, double lh) {
            const Vec3 local = rotY(a.theta, Vec3(float(lx), float(lh) - 2.0f, -0.15f));
            return Vec3(a.cx + local.x, 2.0f + local.y, 0.15f + local.z);
        };
        for (double x0 : xs)
            for (double h0 : heights) {
                double x = x0, h = h0;
                // THE TEXEL-CENTRE ARM (JAH_CONE_PARITY_CENTRES, a diagnostic):
                // the card's value belongs to its texel's CENTRE, so move the
                // sample there — the texel's boundaries found by walking the
                // wall until readCardAt's texel index changes — and read the
                // pixel at that point too. A bias that is the nearest-texel
                // read's vanishes here.
                if (std::getenv("JAH_CONE_PARITY_CENTRES")) {
                    CardSample c0;
                    if (s->readCardAt(worldAt(x, h), n, c0, a.node) && c0.ok) {
                        const double stepM = 0.002;
                        const auto walk = [&](double dxs, double dhs, bool alongX) {
                            double d = 0.0;
                            for (int i = 1; i < 200; ++i) {
                                CardSample ci;
                                const bool okc = s->readCardAt(worldAt(x + dxs * i, h + dhs * i), n, ci, a.node) && ci.ok;
                                if (!okc || (alongX ? ci.texelX != c0.texelX : ci.texelY != c0.texelY)) break;
                                d = i * stepM;
                            }
                            return d + 0.5 * stepM;
                        };
                        const double left = walk(-stepM, 0.0, true), right = walk(stepM, 0.0, true);
                        const double down = walk(0.0, -stepM, false), up = walk(0.0, stepM, false);
                        x += 0.5 * (right - left);
                        h += 0.5 * (up - down);
                        std::printf("    %s (%+.1f, %.1f): texel %u,%u spans %.4f x %.4f m, its centre is "
                                    "(%+.4f, %.4f)\n", a.name, x0, h0, c0.texelX, c0.texelY,
                                    left + right, down + up, x, h);
                    }
                }
                // Local (x, h) on the face -> the pixel: screen right is the
                // wall's -x after the half turn, screen down is -y. The 9 x 9
                // block is taken at the point's FRACTIONAL pixel position
                // (bilinear between the four integer-centred blocks around it).
                const double px = (-x / 3.0 * 0.5 + 0.5) * kPx - 0.5;
                const double py = (-(h - 2.0) / 3.0 * 0.5 + 0.5) * kPx - 0.5;
                double m[3] = { 0, 0, 0 };
                {
                    const int ix = int(std::floor(px)), iy = int(std::floor(py));
                    const double fx = px - ix, fy = py - iy;
                    for (int oy = 0; oy < 2; ++oy)
                        for (int ox = 0; ox < 2; ++ox) {
                            const double wgt = (ox ? fx : 1.0 - fx) * (oy ? fy : 1.0 - fy);
                            for (int y = iy + oy - 4; y <= iy + oy + 4; ++y)
                                for (int xx = ix + ox - 4; xx <= ix + ox + 4; ++xx) {
                                    const Colour c = img.at(unsigned(xx), unsigned(y));
                                    m[0] += wgt * c.r / 81.0; m[1] += wgt * c.g / 81.0; m[2] += wgt * c.b / 81.0;
                                }
                        }
                }
                for (double &v : m) v = v * kConv;
                const Vec3 local = rotY(a.theta, Vec3(float(x), float(h) - 2.0f, -0.15f));
                const Vec3 w(a.cx + local.x, 2.0f + local.y, 0.15f + local.z);
                CardSample t;
                if (!s->readCardAt(w, n, t, a.node) || !t.ok) {
                    CHECK_MSG(false, "%s: the card answers at (%+.1f, %.1f)", a.name, x, h);
                    continue;
                }
                const CardCacheStatus st = s->giStatus().cards;
                for (int k = 0; k < 3; ++k) {
                    const int bits = st.radianceFormat == "R11G11B10F" ? (k == 2 ? 5 : 6) : 10;
                    const double store = t.indirect[k] > 0.0f
                                             ? 0.5 * std::ldexp(1.0, std::ilogb(double(t.indirect[k])) - bits)
                                             : 0.0;
                    // The pixel's own half-quantum: RGBA16F, 10 mantissa bits, at
                    // the pixel's value (m / kConv), carried through the same kConv.
                    const double pix = m[k] / kConv;
                    const double pixQ = pix > 0.0 ? 0.5 * std::ldexp(1.0, std::ilogb(pix) - 10) : 0.0;
                    const double tol = 0.01 * m[k] + store + kConv * pixQ;
                    const double diff = std::fabs(double(t.indirect[k]) - m[k]);
                    const double rel = m[k] > 1e-4 ? diff / m[k] : 1.0;
                    if (offAxis) {
                        // How far beyond the bar, as a fraction of the value.
                        worstOffAxis = std::max(worstOffAxis, (diff - tol) / m[k]);
                        if (!offAxisTarget) {
                            std::printf("    %s (%+.1f, %.1f) channel %d: card %.4f, pixel %.4f "
                                        "(%.2f %%; the target row's)\n", a.name, x, h, k,
                                        t.indirect[k], m[k], 100.0 * rel);
                            continue;
                        }
                    } else {
                        worst = std::max(worst, rel);
                        if (offAxisTarget) continue;
                    }
                    CHECK_MSG(m[k] > 0.05 && diff <= tol,
                              "%s (%+.1f, %.1f) channel %d: card indirect %.4f, pixel diffuse x A_hemi/A(1) %.4f"
                              " (%.2f %%; bar 1 %% + half the store's step %.5f + half the pixel's "
                              "float step %.6f)",
                              a.name, x, h, k, t.indirect[k], m[k], 100.0 * rel, store, kConv * pixQ);
                }
            }
    }
    std::printf("    worst relative difference on the axis-aligned wall: %.2f %%\n", 100.0 * worst);
    std::printf("target: %.4f (bar 0.0000) the wall turned 30 degrees: the card's indirect beyond "
                "1 %% + the stores' half-quanta of the pixel's, as a fraction of the value%s\n",
                std::max(0.0, worstOffAxis), worstOffAxis <= 0.0 ? " -- MET" : "");
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// gi.card_read_parity — THE RAY JOB'S CARD READ = THE CPU REFERENCE
// ---------------------------------------------------------------------------
//
// PHOTON-CARDS-2 part B (SC-1d): a reflection ray's hit reads the surface cache
// FIRST, through jah_rq_card.glsl — `SurfaceCache::readAt` ported to GLSL. The
// CPU `readAt` stays as the reference, and this holds the port to it: 1,000
// random points on the crate's six faces, each asked of both with the same
// facing direction — the face's GEOMETRIC normal, which is what the ray job
// hands its pick now (rebuilt from the hit triangle, jah_rq_geom.glsl; the
// oblique-hit arm of gi.rt_reflect_hitres holds that reconstruction), and what
// the CPU reference has always taken — must pick the SAME card
// and the SAME atlas texel, agree on whether the card is lit, and return the
// same radiance (both decode the one R11G11B10F texel).
//
// The GPU half is Engine::cardReadParity: the SAME include behind the SAME
// four bindings, in a test-only job, dispatched once, the picks read back
// (flushCommands first — the AsyncTextureTicket rule's cousin: the captures,
// the relight and the table uploads are recorded, not yet submitted).
//
// ...and THE ROW'S TOGGLE COSTS NO GI REBUILD: `cards` travels through
// setGiTuning, the atlas is freed and built again by the frame, and
// giStatus().rebuilds does not move.
static int caseReadParity()
{
    Fixture f;
    if (!makeFixture(f, "cardreadparity")) return 1;
    Engine *e = f.e;
    Scene *s = f.s;
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("ok: no ray-query device here — the card read's GPU half does not exist;"
                    " gi.card_read_parity skips cleanly\n");
        return 0;
    }
    s->setAmbient(Colour(0.20f, 0.22f, 0.25f), Colour(0.10f, 0.08f, 0.06f));
    PbrParams cp;
    cp.albedo = Colour(0.6f, 0.5f, 0.4f);
    cp.roughness = 0.7f;
    const MaterialId crateMat = s->createPbrMaterial(cp);
    MeshData md = enginetest::unitCubeMesh();
    md.cards = boxCards(0.5f);
    const MeshId mesh = s->createMesh(md);
    const NodeId crate = s->createNode();
    CHECK(crate && crateMat && mesh && s->attachMesh(crate, mesh, crateMat), "the carded crate exists");
    // A 2 x 1.5 x 1 crate, turned 20 degrees about Y: its cards are world
    // rectangles off the world axes, so the read's three dot products are not
    // three component picks.
    const float kTurn = 20.0f * 3.14159265f / 180.0f;
    s->setNodeTransform(crate, Vec3(0.5f, 1.2f, 0.0f),
                        Quat(0.0f, std::sin(0.5f * kTurn), 0.0f, std::cos(0.5f * kTurn)),
                        Vec3(2.0f, 1.5f, 1.0f));
    const NodeId floorNode = s->createNode();
    {
        PbrParams p;
        p.albedo = Colour(0.8f, 0.8f, 0.8f);
        const MaterialId mat = s->createPbrMaterial(p);
        const MeshId plain = s->createMesh(enginetest::unitCubeMesh());
        CHECK(floorNode && mat && plain && s->attachMesh(floorNode, plain, mat), "the floor exists");
        s->setNodeTransform(floorNode, Vec3(0.0f, -0.1f, 0.0f), Quat(), Vec3(10.0f, 0.2f, 10.0f));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.4f, -1.0f, 0.3f), 2.0f);
    f.view->setShadows(true);

    GiParams gi = baseGi();
    gi.cardResidencyRadius = 40.0f;
    CHECK(s->setGlobalIllumination(gi), "GI builds");
    enginetest::testCameraLookAt(f.view, Vec3(0.0f, 4.0f, -8.0f), Vec3(0.5f, 1.0f, 0.0f));
    render(e, 90);
    const CardCacheStatus st = s->giStatus().cards;
    CHECK_MSG(st.built && st.cardsResident >= 6u && st.indirectRelights >= 6u,
              "the crate's cards are captured, lit and marched (%u resident, %llu marches)",
              st.cardsResident, (unsigned long long)st.indirectRelights);

    // THE 1,000 HITS: a face, a point on it inset a hair from its edges, and the
    // face's normal as the facing (with it only the face's own card can face the
    // hit — the side cards' facing is 0). THE POINT SITS 0.1 mm INSIDE
    // THE FACE, and not on it, for a reason measured while the facing was the
    // reversed ray (kept: it costs nothing and the depth rule reads the plane):
    // a box's face plane is
    // EXACTLY the edge of its four neighbours' cards (each side card spans the
    // box along this face's axis), so a point on the plane projects to v = 0 of
    // them to the last bit — and 3 of the first 1,000 such hits, all inside the
    // two-texel edge band where a side card legitimately passes the depth test,
    // went to the side card on one side and the face card on the other on
    // nothing but the rounding of a v of 0.000000 (CPU dv against the half
    // extent, GPU one fused row). Both answers are legal there; neither is a
    // question about the port. A tenth of a millimetre is 6.7e-5 of the card's
    // v — six hundred float epsilons clear of the edge — and still inside the
    // depth test's own millimetre, so every card that competes still competes.
    // (A ray's hit lands within float rounding of the surface on either side; a
    // side card answering the edge band in place of the face's own card is the
    // same surface within two texels.)
    const Vec3 half(1.0f, 0.75f, 0.5f);
    const auto toWorld = [&](const Vec3 &l) {
        const float c = std::cos(kTurn), sn = std::sin(kTurn);
        return Vec3(0.5f + l.x * c + l.z * sn, 1.2f + l.y, -l.x * sn + l.z * c);
    };
    const auto dirWorld = [&](const Vec3 &l) {
        const float c = std::cos(kTurn), sn = std::sin(kTurn);
        return Vec3(l.x * c + l.z * sn, l.y, -l.x * sn + l.z * c);
    };
    unsigned rng = 12345u;
    const auto rnd = [&rng]() {
        rng = rng * 1664525u + 1013904223u;
        return float(rng >> 8) / float(1u << 24);
    };
    std::vector<CardReadQuery> q;
    for (int i = 0; i < 1000; ++i) {
        const int face = int(rnd() * 6.0f) % 6;
        const int ax = face / 2;
        const float sign = (face & 1) ? -1.0f : 1.0f;
        float l[3];
        const float h[3] = { half.x, half.y, half.z };
        for (int k = 0; k < 3; ++k) l[k] = (rnd() * 2.0f - 1.0f) * h[k] * 0.98f;
        l[ax] = sign * (h[ax] - 1e-4f);
        float n[3] = { 0, 0, 0 };
        n[ax] = sign;
        // A direction on the face's hemisphere, at least 10 degrees off grazing.
        float d[3];
        for (;;) {
            for (int k = 0; k < 3; ++k) d[k] = rnd() * 2.0f - 1.0f;
            const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len < 1e-3f || len > 1.0f) continue;
            for (float &c : d) c /= len;
            if (d[0] * n[0] + d[1] * n[1] + d[2] * n[2] > 0.17f) break;
        }
        CardReadQuery cq;
        cq.position = toWorld(Vec3(l[0], l[1], l[2]));
        (void)d;   // the hemisphere direction is drawn to keep the sequence; the facing is the normal
        cq.facing = dirWorld(Vec3(n[0], n[1], n[2]));
        cq.node = crate;
        q.push_back(cq);
    }
    std::vector<CardReadPick> gpu;
    const bool ran = e->cardReadParity(s, q, gpu);
    CHECK_MSG(ran && gpu.size() == q.size(), "the ray job's card read ran over %zu hits (%s)",
              q.size(), ran ? "ok" : e->lastError().c_str());
    if (!ran) return 1;
    unsigned same = 0, cpuOk = 0, gpuOk = 0, litBoth = 0, firstBad = ~0u;
    double worstRad = 0.0;
    for (size_t i = 0; i < q.size(); ++i) {
        CardSample t;
        const bool okCpu = s->readCardAt(q[i].position, q[i].facing, t, crate) && t.ok;
        const CardReadPick &g = gpu[i];
        cpuOk += okCpu ? 1u : 0u;
        gpuOk += g.ok ? 1u : 0u;
        bool match = okCpu == g.ok;
        if (match && okCpu) {
            match = t.card == g.card && t.texelX == g.texelX && t.texelY == g.texelY && t.lit == g.lit;
            if (match && g.lit) {
                ++litBoth;
                for (int k = 0; k < 3; ++k)
                    worstRad = std::max(worstRad, std::fabs(double(t.radiance[k]) - double(g.radiance[k])));
            }
        }
        if (match) ++same;
        else if (firstBad == ~0u) {
            firstBad = unsigned(i);
            std::printf("    first disagreement, hit %zu at (%.4f %.4f %.4f): CPU ok %d card %d texel"
                        " (%u, %u) lit %d | GPU ok %d card %d texel (%u, %u) lit %d\n",
                        i, q[i].position.x, q[i].position.y, q[i].position.z, int(okCpu), t.card,
                        t.texelX, t.texelY, int(t.lit), int(g.ok), g.card, g.texelX, g.texelY,
                        int(g.lit));
        }
    }
    std::printf("    %u of %zu hits answered by the CPU, %u by the GPU; %u lit on both\n", cpuOk,
                q.size(), gpuOk, litBoth);
    CHECK_MSG(cpuOk >= 990u, "the reference answers the crate's own surface (%u / 1000)", cpuOk);
    CHECK_MSG(same == q.size(), "the GLSL and the CPU pick the same card and texel for every hit"
              " (%u / %zu)", same, q.size());
    CHECK_MSG(litBoth > 0u && worstRad <= 1e-6,
              "...and read the same radiance off it (worst difference %.2e over %u lit hits)",
              worstRad, litBoth);

    // THE TOGGLE: off and on again through the tuning door — the atlas goes and
    // comes back, and not one GI rebuild is paid for it.
    const unsigned long long rebuildsBefore = s->giStatus().rebuilds;
    GiParams off = gi;
    off.cards = GiToggle::Off;
    CHECK(s->setGiTuning(off), "cards off through setGiTuning");
    render(e, 3);
    const bool freed = !s->giStatus().cards.built;
    CHECK(s->setGiTuning(gi), "cards on again");
    render(e, 30);
    const GiStatus after = s->giStatus();
    CHECK_MSG(freed && after.cards.built, "the toggle freed the atlas and built it again (%d -> %d)",
              int(!freed), int(after.cards.built));
    CHECK_MSG(after.rebuilds == rebuildsBefore, "...with no GI rebuild (rebuilds %llu -> %llu)",
              (unsigned long long)rebuildsBefore, (unsigned long long)after.rebuilds);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------
// gi.card_clouds (CLOUDS-2D-2): the card relight's direct sun crosses the 2D
// cloud sheet by the pixel's own factor. An opaque deck (coverage 1, density
// 4: tau ~ 60 a column, exp(-tau) ~ 1e-26) at shadow strength s leaves
// 1 - s + s exp(-tau) = 1 - s of the sun; the crate top's DIRECT HALF must
// follow: exactly half at s = 0.5 (bar 2 % + the store's quanta, the lighting
// case's bar), nothing at s = 1, and all of it with the layer off.
// ---------------------------------------------------------------------------
static int caseClouds()
{
    Fixture f;
    if (!makeFixture(f, "cardclouds")) return 1;
    Scene *s = f.s;
    s->setAmbient(Colour(0.0f, 0.0f, 0.0f), Colour(0.0f, 0.0f, 0.0f));
    PbrParams cp;
    cp.albedo = Colour(0.6f, 0.5f, 0.4f);
    cp.roughness = 0.7f;
    const MaterialId crateMat = s->createPbrMaterial(cp);
    MeshData md = enginetest::unitCubeMesh();
    md.cards = boxCards(0.5f);
    const MeshId mesh = s->createMesh(md);
    const NodeId crate = s->createNode();
    CHECK(crate && crateMat && mesh && s->attachMesh(crate, mesh, crateMat), "the matte crate exists");
    enginetest::setNodeScale(s, crate, Vec3(2.0f, 2.0f, 2.0f));
    enginetest::setNodePosition(s, crate, Vec3(0.0f, 3.0f, 0.0f));
    const NodeId sun = s->createNode();
    LightDesc l;
    l.type = LightType::Directional;
    l.colour = Colour(1.0f, 1.0f, 1.0f);
    l.intensity = float(2.0 / 3.14159265358979323846);
    l.castShadows = true;
    s->setNodeTransform(sun, Vec3(0, 0, 0), Quat(), Vec3(1, 1, 1));   // straight down
    CHECK(sun && s->setLight(sun, l), "a shadow-casting sun points straight down");
    f.view->setShadows(true);
    // A SKY to draw the sheet over (the layer needs one): the analytic sky.
    SkyDesc sky;
    sky.mode = SkyMode::Atmosphere;
    sky.atmosphere.hasSun = true;
    sky.atmosphere.sunDir[0] = 0.0f; sky.atmosphere.sunDir[1] = 1.0f; sky.atmosphere.sunDir[2] = 0.0f;
    CHECK(s->setSky(sky), "the analytic sky");
    GiParams gi = baseGi();
    gi.cardResidencyRadius = 40.0f;
    CHECK(s->setGlobalIllumination(gi), "GI builds");
    enginetest::testCameraLookAt(f.view, Vec3(0.0f, 8.0f, -10.0f), Vec3(0.0f, 3.0f, 0.0f));
    render(f.e, 40);

    const auto directTop = [&](const char *what) {
        CardSample t;
        if (!s->readCardAt(Vec3(0.0f, 4.0f, 0.3f), Vec3(0, 1, 0), t) || !t.ok) {
            CHECK_MSG(false, "%s: the cache answers on the crate top", what);
            return -1.0;
        }
        const double d = double(t.radiance[1]) - double(t.indirect[1]);
        std::printf("    %s: direct half %.5f (radiance %.5f - indirect %.5f), shadow %.3f\n", what, d,
                    t.radiance[1], t.indirect[1], double(t.shadow));
        return d;
    };
    const double clear = directTop("no layer");
    CHECK_MSG(clear > 0.05, "the sun lights the crate top (%.5f)", clear);

    const auto setClouds = [&](bool on, float strength) {
        SkyDesc d = sky;
        d.clouds.enabled = on;
        d.clouds.coverage = 1.0f;
        d.clouds.density = 4.0f;
        d.clouds.shadow = strength;
        d.clouds.hasSun = true;
        d.clouds.sunDir[0] = 0.0f; d.clouds.sunDir[1] = 1.0f; d.clouds.sunDir[2] = 0.0f;
        d.clouds.sunIrradiance = Colour(2.0f, 2.0f, 2.0f, 1.0f);
        CHECK(s->setSky(d), "the cloud layer pushed");
        render(f.e, 30);
    };
    setClouds(true, 0.5f);
    const double half = directTop("opaque deck, shadow 0.5");
    // Two stored terms, each within half an R11G11B10F step (1/64 of the octave).
    const double bar = 0.02 * 0.5 * clear + 2.0 * std::ldexp(1.0, std::ilogb(clear) - 6) * 0.5;
    CHECK_MSG(std::fabs(half - 0.5 * clear) <= bar,
              "half the shadow strength halves the card's direct sun: %.5f vs %.5f (bar %.5f)",
              half, 0.5 * clear, bar);
    setClouds(true, 1.0f);
    const double full = directTop("opaque deck, shadow 1");
    CHECK_MSG(std::fabs(full) <= 2e-3, "an opaque deck removes the card's direct sun (%.5f)", full);
    setClouds(false, 1.0f);
    const double back = directTop("layer off again");
    CHECK_MSG(std::fabs(back - clear) <= bar, "...and the layer off gives it back (%.5f vs %.5f)",
              back, clear);
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
    else if (which == "lighting") rc = caseLighting();
    else if (which == "lighting_indirect") rc = caseLightingIndirect();
    else if (which == "cone_parity") rc = caseConeParity(false);
    else if (which == "cone_parity_offaxis") rc = caseConeParity(true);
    else if (which == "read_parity") rc = caseReadParity();
    else if (which == "clouds") rc = caseClouds();
    else { std::printf("FAIL: unknown case '%s'\n", which.c_str()); return 1; }
    std::printf("\n%s: %d failure(s)\n", which.c_str(), failures);
    return rc;
}
