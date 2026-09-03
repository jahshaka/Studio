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
// Everything goes through iris::ImportFlags::Canonical — the one post-process
// preset every Jahshaka load site uses.
#include <cmath>
#include <cstdio>
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

int main(int, char **)
{
    testOrphanSkinnedControlPoint();

    if (failures) {
        printf("\n%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
