// See cluster_fixtures.h.
#include "cluster_fixtures.h"

#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/mirror/scenemirror.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

namespace clusterfix {

iris::MeshPtr meshFromArrays(const std::vector<float> &positions, const std::vector<float> &normals,
                             const std::vector<float> &uvs, const std::vector<unsigned> &indices)
{
    iris::MeshPtr m = iris::Mesh::create();
    // One attribute per buffer, exactly the shape the bake reader builds.
    const auto add = [&](iris::VertexAttribUsage usage, const std::vector<float> &v, int comps) {
        iris::VertexLayout layout;
        layout.addAttrib(usage, iris::AttribTypeFloat, comps, comps * int(sizeof(float)));
        auto vb = iris::VertexBuffer::create(layout);
        vb->setData(const_cast<float *>(v.data()), unsigned(v.size() * sizeof(float)));
        m->addVertexBuffer(vb);
    };
    add(iris::VertexAttribUsage::Position, positions, 3);
    if (!normals.empty()) add(iris::VertexAttribUsage::Normal, normals, 3);
    if (!uvs.empty()) add(iris::VertexAttribUsage::TexCoord0, uvs, 2);
    auto ib = iris::IndexBuffer::create();
    ib->setData(const_cast<unsigned *>(indices.data()), unsigned(indices.size() * sizeof(unsigned)));
    m->setIndexBuffer(ib);
    m->usesIndexBuffer = true;
    m->numVerts = int(positions.size() / 3);
    m->numFaces = int(indices.size() / 3);
    m->primitiveMode = iris::PrimitiveMode::Triangles;
    return m;
}

std::vector<iris::MeshPtr> loadModel(const std::string &path)
{
    // THE IMPORT'S OWN ENTRY POINT: parse + the whole bake (chain, cards, field,
    // DAG), exactly what a library import runs — so a loaded fixture arrives
    // already carrying the DAG the product would store.
    std::vector<iris::MeshPtr> out;
    QTemporaryDir scratch;
    const QString fp = iris::MeshBake::fingerprintFor(
        QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    const iris::MeshBake::Model model =
        iris::MeshBake::buildFromFile(QString::fromStdString(path), fp, scratch.path());
    if (!model.valid) return out;
    for (const iris::MeshPtr &m : model.meshes)
        if (m && m->primitiveMode == iris::PrimitiveMode::Triangles && m->getIndexBuffer())
            out.push_back(m);
    return out;
}

iris::MeshPtr uvSphere(int seg, int ring, float radius)
{
    std::vector<float> pos, nrm, uv;
    for (int r = 0; r <= ring; ++r)
        for (int s = 0; s <= seg; ++s) {
            // THE SEAM AND THE POLES ARE EXACT: the last column wraps to phi = 0
            // and the pole rows sit at exactly (0, +-1, 0), so the duplicated
            // seam and pole vertices WELD by position. (float sin(2 pi) is
            // -1.7e-7, not 0: computed naively the seam is a 1e-7 m slit that
            // simplification opens into a visible crack — found by this lane's
            // own crack suite on the first run.)
            const float phi = s == seg ? 0.0f : float(s) / float(seg) * 6.28318530718f;
            const float theta = float(r) / float(ring) * 3.14159265359f;
            const bool pole = r == 0 || r == ring;
            const float st = pole ? 0.0f : std::sin(theta);
            const float x = st * std::cos(phi), y = r == 0 ? 1.0f : (r == ring ? -1.0f : std::cos(theta)),
                        z = st * std::sin(phi);
            pos.insert(pos.end(), { radius * x, radius * y, radius * z });
            nrm.insert(nrm.end(), { x, y, z });
            uv.insert(uv.end(), { float(s) / float(seg), float(r) / float(ring) });
        }
    std::vector<unsigned> idx;
    for (int r = 0; r < ring; ++r)
        for (int s = 0; s < seg; ++s) {
            const unsigned a = unsigned(r * (seg + 1) + s), b = unsigned(r * (seg + 1) + s + 1);
            const unsigned c = unsigned((r + 1) * (seg + 1) + s + 1), e = unsigned((r + 1) * (seg + 1) + s);
            idx.insert(idx.end(), { a, b, c, a, c, e });
        }
    return meshFromArrays(pos, nrm, uv, idx);
}

iris::MeshPtr bar(float length, float width, int segments, int around)
{
    // Four long faces, each a (around x segments) grid with its own vertices (hard
    // edges, like an imported box), plus two end caps.
    std::vector<float> pos, nrm, uv;
    std::vector<unsigned> idx;
    const float h = width * 0.5f;
    struct Face { float n[3]; float u[3]; };   // outward normal, across-face axis
    const Face faces[4] = { { { 1, 0, 0 }, { 0, 1, 0 } }, { { -1, 0, 0 }, { 0, -1, 0 } },
                            { { 0, 1, 0 }, { -1, 0, 0 } }, { { 0, -1, 0 }, { 1, 0, 0 } } };
    for (const Face &f : faces) {
        const unsigned base = unsigned(pos.size() / 3);
        for (int sgm = 0; sgm <= segments; ++sgm)
            for (int a = 0; a <= around; ++a) {
                const float t = float(a) / float(around) * 2.0f - 1.0f;   // -1..1 across
                const float z = -length * float(sgm) / float(segments);
                pos.insert(pos.end(), { f.n[0] * h + f.u[0] * h * t, f.n[1] * h + f.u[1] * h * t, z });
                nrm.insert(nrm.end(), { f.n[0], f.n[1], f.n[2] });
                uv.insert(uv.end(), { float(a) / float(around), float(sgm) / float(segments) });
            }
        const unsigned row = unsigned(around + 1);
        for (int sgm = 0; sgm < segments; ++sgm)
            for (int a = 0; a < around; ++a) {
                const unsigned p0 = base + unsigned(sgm) * row + unsigned(a);
                const unsigned p1 = p0 + 1, p2 = p0 + row + 1, p3 = p0 + row;
                // Outward (CCW seen from outside): (p2 - p0) x (p1 - p0) = u x z = n.
                idx.insert(idx.end(), { p0, p2, p1, p0, p3, p2 });
            }
    }
    // The two caps.
    for (int end = 0; end < 2; ++end) {
        const float z = end == 0 ? 0.0f : -length;
        const float nz = end == 0 ? 1.0f : -1.0f;
        const unsigned b = unsigned(pos.size() / 3);
        const float cx[4] = { -h, h, h, -h }, cy[4] = { -h, -h, h, h };
        for (int k = 0; k < 4; ++k) {
            pos.insert(pos.end(), { cx[k], cy[k], z });
            nrm.insert(nrm.end(), { 0.0f, 0.0f, nz });
            uv.insert(uv.end(), { cx[k] > 0 ? 1.0f : 0.0f, cy[k] > 0 ? 1.0f : 0.0f });
        }
        if (end == 0) idx.insert(idx.end(), { b, b + 1, b + 2, b, b + 2, b + 3 });
        else          idx.insert(idx.end(), { b, b + 2, b + 1, b, b + 3, b + 2 });
    }
    return meshFromArrays(pos, nrm, uv, idx);
}

iris::MeshPtr roundBar(float length, float radius, int segments, int around, float bump)
{
    std::vector<float> pos, nrm, uv;
    std::vector<unsigned> idx;
    const float twoPi = 6.28318530718f;
    for (int s = 0; s <= segments; ++s)
        for (int a = 0; a <= around; ++a) {
            const float t = a == around ? 0.0f : float(a) / float(around) * twoPi;   // exact seam
            const float z = -length * float(s) / float(segments);
            // The ends stay round (bump 0 at s = 0 and s = segments) so the caps close.
            const bool end = s == 0 || s == segments;
            const float rr = radius * (1.0f + (end ? 0.0f : bump * std::sin(z * twoPi / 0.5f) * std::cos(3.0f * t)));
            pos.insert(pos.end(), { rr * std::cos(t), rr * std::sin(t), z });
            nrm.insert(nrm.end(), { std::cos(t), std::sin(t), 0.0f });
            uv.insert(uv.end(), { float(a) / float(around), float(s) / float(segments) });
        }
    const unsigned row = unsigned(around + 1);
    for (int s = 0; s < segments; ++s)
        for (int a = 0; a < around; ++a) {
            const unsigned p0 = unsigned(s) * row + unsigned(a), p1 = p0 + 1, p2 = p0 + row + 1, p3 = p0 + row;
            // Outward: (p3 - p0) runs along -Z and (p1 - p0) around (+theta);
            // (p3 - p0) x (p1 - p0) = (-z) x t points OUT.
            idx.insert(idx.end(), { p0, p3, p1, p1, p3, p2 });
        }
    // The two caps, as fans around a centre vertex of their own.
    for (int end = 0; end < 2; ++end) {
        const float z = end == 0 ? 0.0f : -length;
        const float nz = end == 0 ? 1.0f : -1.0f;
        const unsigned centre = unsigned(pos.size() / 3);
        pos.insert(pos.end(), { 0.0f, 0.0f, z });
        nrm.insert(nrm.end(), { 0.0f, 0.0f, nz });
        uv.insert(uv.end(), { 0.5f, 0.5f });
        const unsigned ring = unsigned(pos.size() / 3);
        for (int a = 0; a <= around; ++a) {
            const float t = a == around ? 0.0f : float(a) / float(around) * twoPi;
            pos.insert(pos.end(), { radius * std::cos(t), radius * std::sin(t), z });
            nrm.insert(nrm.end(), { 0.0f, 0.0f, nz });
            uv.insert(uv.end(), { 0.5f + 0.5f * std::cos(t), 0.5f + 0.5f * std::sin(t) });
        }
        for (int a = 0; a < around; ++a) {
            const unsigned q0 = ring + unsigned(a), q1 = q0 + 1;
            if (end == 0) idx.insert(idx.end(), { centre, q0, q1 });
            else          idx.insert(idx.end(), { centre, q1, q0 });
        }
    }
    return meshFromArrays(pos, nrm, uv, idx);
}

bool bake(Fixture &f, iris::MeshPtr mesh, const std::string &name,
          iris::MeshBake::ClusterDagVariant variant, bool wantRegions)
{
    f = Fixture();
    f.name = name;
    f.mesh = mesh;
    if (mesh.isNull()) return false;
    // A loaded fixture already carries the import's chain; a procedural one gets
    // it here. The DAG is always rebuilt: the stats (and the provenance, when
    // asked) are what the suites read, and the build is deterministic.
    if (mesh->lodIndices.isEmpty()) iris::MeshBake::buildLodChain(mesh);
    f.stats.wantRegions = wantRegions;
    iris::MeshBake::buildClusterDag(mesh, &f.stats, variant);
    if (!SceneMirror::toMeshData(mesh.data(), f.data)) return false;
    f.triangles = f.data.indices.size() / 3;
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (size_t v = 0; v + 2 < f.data.positions.size(); v += 3)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], f.data.positions[v + size_t(k)]);
            hi[k] = std::max(hi[k], f.data.positions[v + size_t(k)]);
        }
    f.extent = std::max(hi[0] - lo[0], std::max(hi[1] - lo[1], hi[2] - lo[2]));
    return true;
}

std::vector<Named> shippedMeshes(const std::string &sourceDir, const std::string &fixtureDir,
                                 bool includeSmall)
{
    std::vector<Named> out;
    const QDir prim(QString::fromStdString(sourceDir) + "/app/content/primitives");
    for (const QString &file : prim.entryList({ "*.obj" }, QDir::Files, QDir::Name)) {
        const std::vector<iris::MeshPtr> ms = loadModel(prim.filePath(file).toStdString());
        for (size_t i = 0; i < ms.size(); ++i) {
            const iris::IndexBufferPtr ib = ms[i]->getIndexBuffer();
            const size_t tris = ib ? size_t(ib->dataSize) / sizeof(unsigned) / 3u : 0u;
            if (!includeSmall && tris < 256u) continue;
            out.push_back({ file.toStdString() + (ms.size() > 1 ? "#" + std::to_string(i) : ""), ms[i] });
        }
    }
    const QDir fx(QString::fromStdString(fixtureDir));
    const struct { const char *file; const char *name; } models[] = {
        { "matcaps_dragon.obj", "matcaps-dragon" }, { "physics_model.obj", "physics-model" } };
    for (const auto &m : models) {
        const std::vector<iris::MeshPtr> ms = loadModel(fx.filePath(m.file).toStdString());
        for (size_t i = 0; i < ms.size(); ++i)
            out.push_back({ std::string(m.name) + (ms.size() > 1 ? "#" + std::to_string(i) : ""), ms[i] });
    }
    out.push_back({ "uv-sphere-20k", uvSphere() });
    return out;
}

}   // namespace clusterfix
