// See cluster_fixtures.h.
#include "cluster_fixtures.h"

#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/mirror/scenemirror.h"

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

iris::MeshPtr meshFromShell(const enginetest::ShellMesh &m)
{
    const std::vector<unsigned> idx(m.indices.begin(), m.indices.end());
    return meshFromArrays(m.positions, m.normals, m.uvs, idx);
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

bool bake(Fixture &f, iris::MeshPtr mesh, const std::string &name, const iris::MeshBake::ClusterDagOptions &options)
{
    f = Fixture();
    f.name = name;
    f.mesh = mesh;
    if (!mesh) return false;
    // The DAG is always rebuilt: the stats (and the provenance or the group records,
    // when asked) are what the suites read, and the build is deterministic; the level
    // table is its uniform cuts (ATOM-2 D10), rebuilt from it.
    iris::MeshBake::buildClusterDag(mesh, &f.stats, options);
    iris::MeshBake::buildLevelTable(mesh);
    if (!SceneMirror::toMeshData(mesh.get(), f.data)) return false;
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

}   // namespace clusterfix
