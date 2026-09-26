// scale_assets_gen — THE LARGE ASSETS (lane D1-SCALE-FIXTURES, brief §4.2), a
// TOOL (EXCLUDE_FROM_ALL), never a suite.
//
//   scale_assets_gen [--force] <triangles>...     e.g. 1000000 5000000 10000000
//
// Each count is enginetest::proceduralShell(triangles), written as a binary PLY
// and baked through the PRODUCT'S import door (MeshBake::buildFromFile: parse,
// chain, cards, SDF, cluster DAG) into the scale bake cache the suites read. A
// cached count is skipped unless --force.
//
// W11 (the single-threaded bake; meshbake.cpp) IS this tool's run time, so it
// prints the row: seconds per million triangles, the DAG stage apart (the bake's
// own log line), the blob, the chain's level count, and the process's peak RSS
// (the memory law: the 10 M bake is heavy — measured, not guessed).
#include "scale_world.h"

#include <QCoreApplication>
#include <QFile>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool force = false;
    std::vector<size_t> counts;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--force")) force = true;
        else counts.push_back(size_t(std::strtoull(argv[i], nullptr, 10)));
    }
    if (counts.empty()) counts = { 1000000u, 5000000u, 10000000u };
    std::printf("scale_assets_gen: cache %s\n", qPrintable(scale::cacheDir()));
    std::printf("%-14s %6s %10s %6s %8s %10s %10s %8s %10s %12s %8s %6s %10s %9s\n", "asset", "pieces", "tris",
                "cached", "bake s", "s per MT", "dag s", "dag %", "read ms", "blob MB", "levels", "cards",
                "coarsest", "peak MB");
    int rc = 0;
    for (size_t t : counts) {
        if (force) QFile::remove(scale::shellBlobPath(t));
        scale::BakeInfo info;
        const QList<iris::MeshPtr> m = scale::shellAsset(t, &info, true);
        if (m.isEmpty()) { std::printf("FAIL: shell %zu\n", t); rc = 1; continue; }
        const double s = info.bakeMs / 1000.0;
        std::printf("%-14s %6d %10zu %6s %8.1f %10.1f %10.1f %7.1f%% %10.0f %12.1f %8d %6d %10zu %9.0f\n",
                    qPrintable(info.name), info.pieces, info.triangles, info.fromCache ? "yes" : "no", s,
                    info.triangles ? s / (double(info.triangles) / 1e6) : 0.0, info.dagMs / 1000.0,
                    info.bakeMs > 0 ? 100.0 * info.dagMs / info.bakeMs : 0.0, info.readMs,
                    double(info.blobBytes) / (1024.0 * 1024.0), info.levels, info.cards,
                    info.coarsestTriangles, double(info.peakRssKb ? info.peakRssKb : scale::peakRssKb()) / 1024.0);
        std::fflush(stdout);
    }
    return rc;
}
