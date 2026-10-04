// picking.trimesh_index — THE PICKING MESH'S SPATIAL INDEX GIVES THE BRUTE-FORCE
// ANSWER (SPEED-CPU, perf audit 2026-10-03 P1).
//
// TriMesh::getSegmentIntersections used to test EVERY triangle of a candidate
// mesh: 20.1 ms per pick at -O0 on a 233k-triangle scan, per hand per frame in
// VR. It now asks a TriangleBvh which triangles the segment can touch and runs
// the SAME triangle test on exactly those, in ascending order. This suite is the
// arm: on every mesh of the sample set — the shipped primitives, the bake's flat
// grid fixture, a 233k-triangle random soup and the degenerate cases a box tree
// is most likely to get wrong (a stack of identical triangles, a coplanar sheet,
// axis-parallel segments, segments that graze a vertex or an edge) — the indexed
// answer must equal the brute-force loop's answer EXACTLY: the same count, the
// same triangle indices in the same order, the same hit points and t to the bit.
//
// The brute-force reference is the loop the tree used to run, written here over
// TriMesh::segmentHitsTriangle — the one triangle test both paths share.
//
// Optional argument: a model file (any format assimp reads, e.g. the owner's
// greek_temple_scan.glb) — its meshes join the sample set and the timing line
// reports brute force vs the index on its biggest mesh. Timings are PRINTED,
// never asserted (this is a correctness suite; a millisecond bar would need the
// GPU-timing admission and a quiet box).

#include "irisgl/core/geometry/trimesh.h"
#include "irisgl/core/geometry/trianglebvh.h"
#include "irisgl/core/math/vec.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

static int gFailures = 0;
#define CHECK(cond, msg)                                                                  \
    do {                                                                                  \
        if (cond) std::printf("ok: %s\n", msg);                                           \
        else { std::printf("FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); ++gFailures; } \
    } while (0)

namespace {

struct Sample { std::string name; iris::TriMesh mesh; };

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }
bool sameBits(const iris::Vec3 &a, const iris::Vec3 &b)
{
    return sameBits(a.x(), b.x()) && sameBits(a.y(), b.y()) && sameBits(a.z(), b.z());
}

/// THE REFERENCE: the brute-force loop, over the shared triangle test.
QList<iris::TriangleIntersectionResult> bruteForce(const iris::TriMesh &m, const iris::Vec3 &a,
                                                   const iris::Vec3 &b)
{
    QList<iris::TriangleIntersectionResult> out;
    for (int i = 0; i < m.triangles.size(); ++i) {
        iris::TriangleIntersectionResult r;
        if (!iris::TriMesh::segmentHitsTriangle(m.triangles.at(i), a, b, r.t, r.hitPoint)) continue;
        r.triangleIndex = i;
        out.append(r);
    }
    return out;
}

/// Every segment of `segs` against `m`: indexed vs brute force, exactly.
/// Returns the number of segments that hit anything (so a sample cannot pass
/// vacuously).
int compare(const char *what, iris::TriMesh &m, const std::vector<std::pair<iris::Vec3, iris::Vec3>> &segs,
            int &mismatches)
{
    int hitting = 0;
    for (const auto &s : segs) {
        QList<iris::TriangleIntersectionResult> got;
        const int n = m.getSegmentIntersections(s.first, s.second, got);
        const QList<iris::TriangleIntersectionResult> want = bruteForce(m, s.first, s.second);
        bool same = n == want.size() && got.size() == want.size();
        for (int i = 0; same && i < want.size(); ++i)
            same = got[i].triangleIndex == want[i].triangleIndex && sameBits(got[i].t, want[i].t) &&
                   sameBits(got[i].hitPoint, want[i].hitPoint);
        if (!same) {
            if (mismatches < 8)
                std::printf("    MISMATCH %s: segment (%g %g %g)->(%g %g %g) indexed %d hits, brute %d\n",
                            what, s.first.x(), s.first.y(), s.first.z(), s.second.x(), s.second.y(),
                            s.second.z(), int(got.size()), int(want.size()));
            ++mismatches;
        }
        if (!want.isEmpty()) ++hitting;
    }
    return hitting;
}

void bounds(const iris::TriMesh &m, iris::Vec3 &lo, iris::Vec3 &hi)
{
    lo = iris::Vec3(1e30f, 1e30f, 1e30f);
    hi = iris::Vec3(-1e30f, -1e30f, -1e30f);
    for (const iris::Triangle &t : m.triangles)
        for (const iris::Vec3 &p : { t.a, t.b, t.c })
            for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], p[k]); hi[k] = std::max(hi[k], p[k]); }
}

/// The segment set for one mesh: random segments through its box, segments
/// down each axis (zero direction components — the slab test's special case),
/// and segments that pass EXACTLY through a corner or an edge midpoint of a
/// triangle along its normal (the grazing case the pad exists for).
std::vector<std::pair<iris::Vec3, iris::Vec3>> segmentsFor(const iris::TriMesh &m, std::mt19937 &rng, int count)
{
    std::vector<std::pair<iris::Vec3, iris::Vec3>> segs;
    iris::Vec3 lo, hi;
    bounds(m, lo, hi);
    const iris::Vec3 size = hi - lo;
    const float reach = std::max(1.0f, size.length());
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    auto inBox = [&]() {
        return iris::Vec3(lo.x() + u(rng) * size.x(), lo.y() + u(rng) * size.y(), lo.z() + u(rng) * size.z());
    };
    for (int i = 0; i < count; ++i) {
        const iris::Vec3 p = inBox(), q = inBox();
        iris::Vec3 d = q - p;
        if (d.lengthSquared() == 0.0f) d = iris::Vec3(0, 0, 1);
        d.normalize();
        segs.emplace_back(p - d * reach, p + d * reach);
    }
    for (int i = 0; i < count / 4; ++i) {
        const iris::Vec3 p = inBox();
        for (int k = 0; k < 3; ++k) {
            iris::Vec3 a = p, b = p;
            a[k] = lo[k] - 1.0f;
            b[k] = hi[k] + 1.0f;
            segs.emplace_back(a, b);
        }
    }
    if (!m.triangles.isEmpty()) {
        std::uniform_int_distribution<int> pick(0, int(m.triangles.size()) - 1);
        for (int i = 0; i < count / 4; ++i) {
            const iris::Triangle &t = m.triangles.at(pick(rng));
            iris::Vec3 n = iris::Vec3::crossProduct(t.b - t.a, t.c - t.a);
            if (n.lengthSquared() == 0.0f) n = iris::Vec3(0, 1, 0);
            n.normalize();
            const iris::Vec3 targets[3] = { t.a, (t.a + t.b) * 0.5f, t.c };
            for (const iris::Vec3 &x : targets) segs.emplace_back(x + n * reach, x - n * reach);
        }
    }
    return segs;
}

/// One mesh per aiMesh of a file, in the mesh's own space (the narrow phase's).
void loadFile(const QString &path, std::vector<Sample> &out)
{
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(path.toStdString(), aiProcess_Triangulate |
                                                                     aiProcess_JoinIdenticalVertices);
    if (!scene) { std::printf("    could not read %s\n", qPrintable(path)); return; }
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh *mesh = scene->mMeshes[i];
        Sample s;
        s.name = QFileInfo(path).fileName().toStdString() + "#" + std::to_string(i);
        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace &face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue;
            const aiVector3D &a = mesh->mVertices[face.mIndices[0]];
            const aiVector3D &b = mesh->mVertices[face.mIndices[1]];
            const aiVector3D &c = mesh->mVertices[face.mIndices[2]];
            s.mesh.addTriangle(iris::Vec3(a.x, a.y, a.z), iris::Vec3(b.x, b.y, b.z), iris::Vec3(c.x, c.y, c.z));
        }
        out.push_back(std::move(s));
    }
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::mt19937 rng(20261004u);
    std::vector<Sample> samples;

    // THE SHIPPED PRIMITIVES and the bake's grid fixture.
    const QDir prims(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives"));
    for (const QString &f : prims.entryList({ QStringLiteral("*.obj") }, QDir::Files, QDir::Name))
        loadFile(prims.filePath(f), samples);
    loadFile(QStringLiteral(JAHSHAKA_SOURCE_DIR "/tests/meshbake/fixtures/flat_grid_100m.obj"), samples);
    CHECK(samples.size() >= 15, "the sample set holds the shipped primitives and the grid fixture");

    // A 233k-TRIANGLE SOUP: the audit's scan size, small random triangles.
    {
        Sample s;
        s.name = "soup233k";
        std::uniform_real_distribution<float> u(-20.0f, 20.0f), e(-0.15f, 0.15f);
        for (int i = 0; i < 233000; ++i) {
            const iris::Vec3 c(u(rng), u(rng) * 0.25f, u(rng));
            s.mesh.addTriangle(c + iris::Vec3(e(rng), e(rng), e(rng)), c + iris::Vec3(e(rng), e(rng), e(rng)),
                               c + iris::Vec3(e(rng), e(rng), e(rng)));
        }
        samples.push_back(std::move(s));
    }
    // DEGENERATE: a stack of identical triangles (no split can separate them),
    // a coplanar sheet, and a triangle with zero area.
    {
        Sample s;
        s.name = "identical-stack";
        for (int i = 0; i < 37; ++i)
            s.mesh.addTriangle(iris::Vec3(0, 0, 0), iris::Vec3(1, 0, 0), iris::Vec3(0, 0, 1));
        s.mesh.addTriangle(iris::Vec3(2, 0, 2), iris::Vec3(2, 0, 2), iris::Vec3(2, 0, 2));
        samples.push_back(std::move(s));
    }
    {
        Sample s;
        s.name = "coplanar-sheet";
        for (int x = 0; x < 64; ++x)
            for (int z = 0; z < 64; ++z) {
                const float fx = float(x) * 0.5f, fz = float(z) * 0.5f;
                s.mesh.addTriangle(iris::Vec3(fx, 1, fz), iris::Vec3(fx + 0.5f, 1, fz), iris::Vec3(fx, 1, fz + 0.5f));
                s.mesh.addTriangle(iris::Vec3(fx + 0.5f, 1, fz), iris::Vec3(fx + 0.5f, 1, fz + 0.5f),
                                   iris::Vec3(fx, 1, fz + 0.5f));
            }
        samples.push_back(std::move(s));
    }
    QString big;
    if (argc > 1) {
        big = QString::fromLocal8Bit(argv[1]);
        loadFile(big, samples);
    }

    int mismatches = 0, hitting = 0, segments = 0, indexed = 0;
    for (Sample &s : samples) {
        s.mesh.buildIndex();
        if (s.mesh.hasIndex()) ++indexed;
        const auto segs = segmentsFor(s.mesh, rng, s.mesh.triangles.size() > 100000 ? 400 : 200);
        segments += int(segs.size());
        hitting += compare(s.name.c_str(), s.mesh, segs, mismatches);
    }
    std::printf("    %d meshes (%d indexed), %d segments, %d of them hitting something\n",
                int(samples.size()), indexed, segments, hitting);
    CHECK(indexed == int(samples.size()), "every sample mesh built an index");
    CHECK(hitting > segments / 10, "the segment set is not vacuous (a tenth of it hits)");
    CHECK(mismatches == 0, "the indexed narrow phase answers exactly what the brute-force loop answers");

    // AN INDEX THAT NO LONGER MATCHES ITS LIST IS NOT TRUSTED: adding a triangle
    // after the build drops the index, and the answer still includes the new one.
    {
        iris::TriMesh m;
        m.addTriangle(iris::Vec3(0, 0, 0), iris::Vec3(1, 0, 0), iris::Vec3(0, 0, 1));
        m.buildIndex();
        m.addTriangle(iris::Vec3(10, 0, 10), iris::Vec3(11, 0, 10), iris::Vec3(10, 0, 11));
        QList<iris::TriangleIntersectionResult> r;
        m.getSegmentIntersections(iris::Vec3(10.2f, 5, 10.2f), iris::Vec3(10.2f, -5, 10.2f), r);
        CHECK(r.size() == 1 && r[0].triangleIndex == 1, "a triangle added after the build is still found");
    }
    // A NON-FINITE CORNER BUILDS NO INDEX (the brute-force loop keeps the mesh).
    {
        iris::TriMesh m;
        m.addTriangle(iris::Vec3(0, 0, 0), iris::Vec3(NAN, 0, 0), iris::Vec3(0, 0, 1));
        m.buildIndex();
        CHECK(!m.hasIndex(), "a mesh with a NaN corner keeps the brute-force loop");
    }

    // THE COST, PRINTED: brute force vs the index on every sample of 50k+ triangles.
    for (Sample &big : samples) {
        if (big.mesh.triangles.size() < 50000) continue;
        const Sample *largest = &big;
        iris::TriMesh &m = big.mesh;
        QElapsedTimer built; built.start();
        m.buildIndex();
        const double buildMs = double(built.nsecsElapsed()) / 1e6;
        const auto segs = segmentsFor(m, rng, 40);
        QElapsedTimer tb; tb.start();
        int sink = 0;
        for (const auto &sg : segs) sink += bruteForce(m, sg.first, sg.second).size();
        const double bruteMs = double(tb.nsecsElapsed()) / 1e6 / double(segs.size());
        QElapsedTimer ti; ti.start();
        for (const auto &sg : segs) {
            QList<iris::TriangleIntersectionResult> r;
            sink -= m.getSegmentIntersections(sg.first, sg.second, r);
        }
        const double indexMs = double(ti.nsecsElapsed()) / 1e6 / double(segs.size());
        std::printf("    COST %s: %d triangles, index build %.2f ms (%d nodes); per pick brute %.3f ms, "
                    "indexed %.4f ms (%d)\n",
                    largest->name.c_str(), int(m.triangles.size()), buildMs, m.indexNodeCount(), bruteMs,
                    indexMs, sink);
    }

    std::printf(gFailures ? "FAILED: %d\n" : "all passed\n", gFailures);
    return gFailures ? 1 : 0;
}
