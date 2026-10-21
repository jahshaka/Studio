// THE CUT CURVE'S STOCK ARM (cutcurve_stock.cpp): upstream clusterlod, positions only, its quadric stored.
#ifndef TESTS_MESHBAKE_CUTCURVE_STOCK_H
#define TESTS_MESHBAKE_CUTCURVE_STOCK_H

#include <cstddef>
#include <vector>

namespace cutcurve
{

/// A DAG as the cut reads it: per group its stored error (FLT_MAX = terminal) and depth; per cluster the group it
/// belongs to, the group that produced it (-1 = a level-0 leaf) and its triangles.
struct StockDag
{
    std::vector<float> groupError;
    std::vector<int>   groupDepth;
    std::vector<int>   clusterGroup, clusterRefined, clusterTriangles;
};

void stockBuild(const float *positions, size_t vertexCount, size_t stride, const unsigned *indices, size_t indexCount,
                StockDag &out);

}   // namespace cutcurve

#endif
