// cluster_config_measure — ATOM stage 2's CONFIG MEASUREMENT (B2 §1: "configure
// and measure first") and its BAKE TABLE. A measuring tool (EXCLUDE_FROM_ALL),
// not a suite: it prints the numbers the shipped `clodConfig` was chosen by and
// the per-asset bake cost of the DAG, and asserts nothing.
//
//   cluster_config_measure configs   — every variant (MeshBake::ClusterDagVariant)
//       on every shipped mesh the bake gives a DAG: the cut's TRIANGLE COUNT at
//       seven allowed errors (fractions of the mesh's extent — "equal measured
//       bound"), the chain's count at the same allowed error, the DAG's depth,
//       terminal groups and the bake's monotonicity/sphere fix counts.
//   cluster_config_measure bake      — the shipped variant: per asset, the full
//       bake's time with and without the DAG and the blob's size with and without
//       its DAG block.
#include "cluster_fixtures.h"

#include "irisgl/import/meshbake.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <string>

using namespace jahshaka::engine;
using V = iris::MeshBake::ClusterDagVariant;

static const float kFractions[] = { 0.0005f, 0.001f, 0.002f, 0.005f, 0.01f, 0.02f, 0.05f };

static int configs(const std::string &only)
{
    auto meshes = clusterfix::shippedMeshes(JAHSHAKA_TEST_SOURCE_DIR, CLUSTER_FIXTURE_DIR);
    meshes.push_back({ "round-bar-40m", clusterfix::roundBar(40.0f, 0.25f, 400, 32) });
    meshes.push_back({ "box-bar-40m", clusterfix::bar(40.0f, 0.5f, 400, 4) });
    if (!only.empty())
        meshes.erase(std::remove_if(meshes.begin(), meshes.end(),
                                    [&](const clusterfix::Named &n) { return n.name.find(only) == std::string::npos; }),
                     meshes.end());
    std::printf("== the cut's triangles at equal measured bound (allowed = fraction x extent) ==\n");
    std::printf("%-22s %-28s %6s %6s %5s %4s %5s %5s %8s %8s |", "mesh", "variant", "tris", "clus",
                "depth", "term", "monoF", "sphF", "build ms", "meas ms");
    for (float f : kFractions) std::printf(" %7.4f", f);
    std::printf("\n");
    // Aggregate per variant: the sum over meshes and thresholds of cut / level-0.
    double aggregate[int(V::Count)] = {};
    double chainAggregate = 0.0;
    int rows = 0;
    for (const auto &named : meshes) {
        bool chainPrinted = false;
        for (int vi = int(V::DefaultProtectUv); vi < int(V::Count); ++vi) {
            clusterfix::Fixture f;
            // A fresh parse per variant: the bake writes into the mesh.
            iris::MeshPtr m = named.mesh;
            if (!clusterfix::bake(f, m, named.name, V(vi))) continue;
            if (f.data.clusters.empty()) break;
            if (!chainPrinted) {
                std::printf("%-22s %-28s %6zu %6s %5s %4s %5s %5s %8s %8s |", named.name.c_str(),
                            "(the chain)", f.triangles, "", "", "", "", "", "", "");
                for (float fr : kFractions) {
                    const float allowed = fr * f.extent;
                    const size_t level =
                        lodLevelForWorldError(f.data.lodBounds, allowed, f.data.lodIndices.size());
                    const size_t tris = level == 0 ? f.triangles : f.data.lodIndices[level - 1].size() / 3;
                    std::printf(" %7zu", tris);
                    chainAggregate += double(tris) / double(f.triangles);
                }
                std::printf("\n");
                chainPrinted = true;
                ++rows;
            }
            std::printf("%-22s %-28s %6zu %6d %5d %4d %5d %5d %8.1f %8.1f |", "",
                        iris::MeshBake::clusterDagVariantName(V(vi)), f.triangles, f.stats.clusters,
                        f.stats.depth, f.stats.terminalGroups, f.stats.monotoneFixes,
                        f.stats.sphereFixes, f.stats.buildMs, f.stats.measureMs);
            std::vector<unsigned> cut;
            for (float fr : kFractions) {
                const size_t tris = clusterCutAtAllowed(f.data.clusterGroups, f.data.clusters,
                                                        fr * f.extent, cut);
                std::printf(" %7zu", tris);
                aggregate[vi] += double(tris) / double(f.triangles);
            }
            std::printf("\n");
        }
    }
    std::printf("\n== aggregate: mean over %d meshes x %zu thresholds of (cut triangles / level 0) ==\n",
                rows, sizeof(kFractions) / sizeof(kFractions[0]));
    const double n = double(rows) * double(sizeof(kFractions) / sizeof(kFractions[0]));
    std::printf("   %-28s %.4f\n", "(the chain)", n > 0 ? chainAggregate / n : 0.0);
    for (int vi = int(V::DefaultProtectUv); vi < int(V::Count); ++vi)
        std::printf("   %-28s %.4f%s\n", iris::MeshBake::clusterDagVariantName(V(vi)),
                    n > 0 ? aggregate[vi] / n : 0.0,
                    V(vi) == iris::MeshBake::shippedClusterDagVariant() ? "   <- shipped" : "");
    return 0;
}

static int bakeTable()
{
    std::printf("== the bake, per asset (the shipped variant) ==\n");
    std::printf("%-22s %8s %7s %7s %10s %10s %10s %10s %6s %6s\n", "asset", "tris", "clusters",
                "groups", "bake ms", "dag ms", "blob B", "no-dag B", "monoF", "sphF");
    const QDir prim(QString::fromLatin1(JAHSHAKA_TEST_SOURCE_DIR) + "/app/content/primitives");
    QStringList files;
    for (const QString &f : prim.entryList({ "*.obj" }, QDir::Files, QDir::Name)) files << prim.filePath(f);
    const QDir fx(QString::fromLatin1(CLUSTER_FIXTURE_DIR));
    files << fx.filePath("matcaps_dragon.obj") << fx.filePath("physics_model.obj");
    QTemporaryDir scratch;
    for (const QString &path : files) {
        const QString fp = iris::MeshBake::fingerprintFor(
            QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
        QElapsedTimer t; t.start();
        // parse + the whole bake (chain, cards, field, DAG) — what an import runs.
        iris::MeshBake::Model model = iris::MeshBake::buildFromFile(path, fp, scratch.path());
        const double bakeMs = double(t.nsecsElapsed()) / 1e6;
        if (!model.valid) { std::printf("%-22s (unreadable)\n", qPrintable(QFileInfo(path).fileName())); continue; }
        const QByteArray blob = iris::MeshBake::serialize(model);
        // The DAG's own time, re-measured on the same meshes, and the blob without it.
        double dagMs = 0.0; int clusters = 0, groups = 0, mono = 0, sph = 0; size_t tris = 0;
        for (const iris::MeshPtr &m : model.meshes) {
            iris::MeshBake::ClusterDagStats st;
            iris::MeshBake::buildClusterDag(m, &st);
            dagMs += st.buildMs + st.measureMs;
            clusters += st.clusters; groups += st.groups; mono += st.monotoneFixes; sph += st.sphereFixes;
            if (m->getIndexBuffer()) tris += size_t(m->getIndexBuffer()->dataSize) / 12u;
            m->clusterDag = iris::MeshClusterDag();
        }
        const QByteArray bare = iris::MeshBake::serialize(model);
        std::printf("%-22s %8zu %7d %7d %10.1f %10.1f %10lld %10lld %6d %6d\n",
                    qPrintable(QFileInfo(path).fileName()), tris, clusters, groups, bakeMs, dagMs,
                    (long long)blob.size(), (long long)bare.size(), mono, sph);
    }
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const std::string mode = argc > 1 ? argv[1] : "configs";
    if (mode == "bake") return bakeTable();
    return configs(argc > 2 ? argv[2] : "");
}
