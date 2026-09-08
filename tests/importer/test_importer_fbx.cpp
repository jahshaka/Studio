// FBX importer regression suite.
//
// Section 1 — orphan skinned control point (assimp-patches/0001).
//   An FBX geometry may carry control points that no polygon references. The
//   FBX skin cluster still lists them, so FBXConverter::ConvertWeights asks
//   MeshGeometry::ToOutputVertexIndex for their output mapping — which is
//   empty, and whose offset sits one past the end of the mapping table.
//   Unpatched assimp evaluates &m_mappings[offset] there: undefined behaviour,
//   and a hard abort in hardened libstdc++ builds (Ubuntu's default). Four of
//   eight stock Mixamo character/animation exports hit it, so the Avatar
//   module's Load dialogs could kill the app on ordinary user files.
//
//   fixtures/orphan_skin_vertex.fbx is a hand-written 2 KB ASCII FBX with
//   exactly that shape: 4 control points, 1 triangle over the first 3, and a
//   skin cluster whose Indexes list all 4. It aborts on unpatched assimp and
//   loads here.
//
// Section 2 — UNIT SCALE (the FBX unit-scale defect, owner report 2026-09-08
//   "Dreyar is massively huge, head touches the ceiling").
//   An FBX declares its unit in GlobalSettings::UnitScaleFactor (centimetres
//   per unit). assimp honours it ONLY with aiProcess_GlobalScale in the flags;
//   iris::ImportFlags::Canonical did not carry it, so every Mixamo download
//   (UnitScaleFactor 1 = centimetres) imported 100x too large. The three
//   fixtures/unit_cube_*.fbx are ONE cube declared cm / m / mm, so the claim
//   is arithmetic: 0.01 m, 1 m, 0.001 m on a side.
//
// Section 3 — the CONTROL. glTF has no unit factor (its spec fixes the metre)
//   and assimp's glTF importer never calls SetFileScale, so ScaleProcess is a
//   no-op there. Every GLB fixture in the tree must come out of the canonical
//   preset with GlobalScale BIT-IDENTICAL to the preset without it — asserted
//   over the whole vertex array, not just the AABB.
//
// Everything goes through iris::ImportFlags::Canonical — the one post-process
// preset every Jahshaka load site uses.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "irisgl/import/importflags.h"

static int failures = 0;

static void check(bool cond, const std::string &what)
{
    if (cond) {
        printf("ok: %s\n", what.c_str());
    } else {
        printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

static std::string fixture(const char *name)
{
    return std::string(JAHSHAKA_TEST_SOURCE_DIR) + "/tests/importer/fixtures/" + name;
}

// ---------------------------------------------------------------------------
// 1. An FBX whose skin cluster references a control point no polygon uses.
static void testOrphanSkinnedControlPoint()
{
    printf("--- section 1: orphan skinned control point\n");

    Assimp::Importer importer;
    // Reaching this line at all is the regression: unpatched assimp aborts
    // inside ReadFile, so there is nothing to assert on.
    const aiScene *scene = importer.ReadFile(fixture("orphan_skin_vertex.fbx"),
                                             iris::ImportFlags::Canonical);
    check(scene != nullptr,
          std::string("the fixture loads: ") + (scene ? "ok" : importer.GetErrorString()));
    if (!scene) return;

    check(scene->mNumMeshes == 1, "one mesh");
    if (scene->mNumMeshes != 1) return;

    const aiMesh *mesh = scene->mMeshes[0];
    // The orphan control point contributes no output vertex: the triangle's
    // three corners are all that survive.
    check(mesh->mNumVertices == 3, "3 output vertices (the orphan is dropped)");
    check(mesh->mNumFaces == 1, "1 face");
    check(mesh->mNumBones == 1, "1 bone");
    if (mesh->mNumBones != 1) return;

    // The guard must skip ONLY the orphan: every real vertex keeps its weight.
    const aiBone *bone = mesh->mBones[0];
    check(bone->mNumWeights == 3, "the bone keeps a weight for each real vertex");

    std::vector<double> sum(mesh->mNumVertices, 0.0);
    for (unsigned w = 0; w < bone->mNumWeights; ++w) {
        const unsigned vid = bone->mWeights[w].mVertexId;
        check(vid < mesh->mNumVertices, "weight vertex id is in range");
        if (vid < mesh->mNumVertices) sum[vid] += bone->mWeights[w].mWeight;
    }
    bool allOne = true;
    for (unsigned v = 0; v < mesh->mNumVertices; ++v)
        if (std::fabs(sum[v] - 1.0) > 1e-5) allOne = false;
    check(allOne, "every surviving vertex's weights still sum to 1");
}

// ---------------------------------------------------------------------------
// Shared measurement helpers for sections 2 and 3.

struct Measure
{
    bool ok = false;
    double size[3] = { 0, 0, 0 };
    unsigned vertices = 0;
    unsigned long long hash = 1469598103934665603ull;   // FNV-1a over every float
};

static Measure measureFile(const std::string &path, unsigned flags, std::string *errorOut)
{
    Measure m;
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(path, flags);
    if (!scene) {
        if (errorOut) *errorOut = importer.GetErrorString();
        return m;
    }
    double mn[3] = { 1e30, 1e30, 1e30 }, mx[3] = { -1e30, -1e30, -1e30 };

    // World space: the node transforms are scaled by ScaleProcess too, and a
    // mesh-local AABB would miss exactly the half of the conversion that made
    // Dreyar's bones 967 units up.
    std::function<void(const aiNode *, aiMatrix4x4)> walk =
        [&](const aiNode *node, aiMatrix4x4 acc) {
            acc = acc * node->mTransformation;
            for (unsigned i = 0; i < node->mNumMeshes; ++i) {
                const aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
                for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
                    const aiVector3D p = acc * mesh->mVertices[v];
                    const float c[3] = { p.x, p.y, p.z };
                    for (int k = 0; k < 3; ++k) {
                        if (c[k] < mn[k]) mn[k] = c[k];
                        if (c[k] > mx[k]) mx[k] = c[k];
                        unsigned bits = 0;
                        std::memcpy(&bits, &c[k], sizeof(bits));
                        for (int b = 0; b < 4; ++b) {
                            m.hash ^= (bits >> (b * 8)) & 0xFF;
                            m.hash *= 1099511628211ull;
                        }
                    }
                    ++m.vertices;
                }
            }
            for (unsigned c = 0; c < node->mNumChildren; ++c) walk(node->mChildren[c], acc);
        };
    walk(scene->mRootNode, aiMatrix4x4());
    if (m.vertices == 0) return m;
    for (int k = 0; k < 3; ++k) m.size[k] = mx[k] - mn[k];
    m.ok = true;
    return m;
}

static bool approx(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// ---------------------------------------------------------------------------
// 2. The file's unit declaration is honoured.
static void testUnitScale()
{
    printf("--- section 2: FBX UnitScaleFactor\n");

    struct Case { const char *file; double side; };
    const Case cases[] = {
        { "unit_cube_cm.fbx", 0.01 },     // declared centimetres
        { "unit_cube_m.fbx", 1.0 },       // declared metres
        { "unit_cube_mm.fbx", 0.001 },    // declared millimetres
    };

    for (const Case &c : cases) {
        std::string error;
        const Measure m = measureFile(fixture(c.file), iris::ImportFlags::Canonical, &error);
        check(m.ok, std::string(c.file) + " loads: " + (m.ok ? "ok" : error));
        if (!m.ok) continue;
        char what[256];
        snprintf(what, sizeof(what), "%s imports %.4f x %.4f x %.4f m (expected %g)",
                 c.file, m.size[0], m.size[1], m.size[2], c.side);
        check(approx(m.size[0], c.side, c.side * 1e-4) &&
              approx(m.size[1], c.side, c.side * 1e-4) &&
              approx(m.size[2], c.side, c.side * 1e-4), what);
    }

    // The RATIO is the defect, stated as arithmetic: the same cube declared in
    // centimetres and in metres must differ by exactly 100.
    const Measure cm = measureFile(fixture("unit_cube_cm.fbx"), iris::ImportFlags::Canonical, nullptr);
    const Measure me = measureFile(fixture("unit_cube_m.fbx"), iris::ImportFlags::Canonical, nullptr);
    if (cm.ok && me.ok)
        check(approx(me.size[1] / cm.size[1], 100.0, 0.01),
              "cm and m declarations of one cube differ by exactly 100x");

    // WITHOUT the flag every declaration reads the same — which is precisely
    // the bug: the factor was parsed and dropped, so a centimetre file was as
    // big as a metre file, i.e. 100x too big. This asserts what
    // aiProcess_GlobalScale is DOING, so nobody can drop it from the canonical
    // preset and still see the section above pass.
    const unsigned oldFlags = iris::ImportFlags::Canonical & ~unsigned(aiProcess_GlobalScale);
    const Measure cmOld = measureFile(fixture("unit_cube_cm.fbx"), oldFlags, nullptr);
    const Measure meOld = measureFile(fixture("unit_cube_m.fbx"), oldFlags, nullptr);
    check(cmOld.ok && meOld.ok && approx(cmOld.size[1], 1.0, 1e-4) && approx(meOld.size[1], 1.0, 1e-4),
          "without aiProcess_GlobalScale both declarations import as 1 unit (the defect)");
    check((iris::ImportFlags::Canonical & unsigned(aiProcess_GlobalScale)) != 0,
          "the canonical preset carries aiProcess_GlobalScale");
    check((iris::ImportFlags::ClipNamesOnly & unsigned(aiProcess_GlobalScale)) != 0,
          "the clip-parsing preset carries it too (a clip's keys are in file units)");
}

// ---------------------------------------------------------------------------
// 3. glTF/GLB is untouched — the control for the change above.
static void testGlbUnaffected()
{
    printf("--- section 3: GLB fixtures are bit-identical\n");

    const char *glbs[] = {
        "scaled_two_meshes.glb",
        "textured_pbr_quad.glb",
        "material_workflows.glb",
        "ticks_anim.glb",
        "../../avatar/fixtures/rig2.glb",
    };
    const unsigned oldFlags = iris::ImportFlags::Canonical & ~unsigned(aiProcess_GlobalScale);
    for (const char *rel : glbs) {
        const std::string path = fixture(rel);
        std::string error;
        const Measure now = measureFile(path, iris::ImportFlags::Canonical, &error);
        const Measure before = measureFile(path, oldFlags, nullptr);
        check(now.ok, std::string(rel) + " loads: " + (now.ok ? "ok" : error));
        if (!now.ok) continue;
        char what[320];
        snprintf(what, sizeof(what),
                 "%s: %u vertices, AABB %.6f x %.6f x %.6f — identical with and without GlobalScale",
                 rel, now.vertices, now.size[0], now.size[1], now.size[2]);
        check(now.vertices == before.vertices && now.hash == before.hash &&
              now.size[0] == before.size[0] && now.size[1] == before.size[1] &&
              now.size[2] == before.size[2], what);
    }
}

int main(int, char **)
{
    testOrphanSkinnedControlPoint();
    testUnitScale();
    testGlbUnaffected();

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
