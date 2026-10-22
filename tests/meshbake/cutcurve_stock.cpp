// THE CUT CURVE'S STOCK ARM (ATOM-UPGRADE stage 0; `test_mesh_bake --cut-curve <model> --arm stock`).
//
// A REFERENCE ARM, NEVER A PRODUCT PATH: upstream's clusterlod.h as meshoptimizer v1.2 ships it (its demo/ copy,
// byte-identical to the copy the old bake vendored; ATOM-2 deleted that copy and its patches), positions only, the
// quadric error it stores
// (upstream's own monotone merge), no measurement — the bound on what the simplifier alone would give
// (ATOM_PIPELINE_AUDIT_2026-10-21_A_BAKE.md §2c, the STOCK row). Every public name is renamed so this TU cannot
// collide with anything else in the binary. Its meshoptimizer entry points come from the meshoptimizer target the
// test links (tests/meshbake/CMakeLists.txt): the clusterizer and the partitioner are no longer in libIrisGL.
#define clodConfig stock_clodConfig
#define clodMesh stock_clodMesh
#define clodBounds stock_clodBounds
#define clodCluster stock_clodCluster
#define clodGroup stock_clodGroup
#define clodOutput stock_clodOutput
#define clodNode stock_clodNode
#define clodDefaultConfig stock_clodDefaultConfig
#define clodDefaultConfigRT stock_clodDefaultConfigRT
#define clodBuild stock_clodBuild
#define clodLocalIndices stock_clodLocalIndices
#define clodBuildHierarchyBound stock_clodBuildHierarchyBound
#define clodBuildHierarchy stock_clodBuildHierarchy
#define clod stock_clod
#include <assert.h>
#include "meshoptimizer.h"
#define CLUSTERLOD_IMPLEMENTATION
#include "irisgl/thirdparty/meshoptimizer/demo/clusterlod.h"   // upstream as shipped: meshoptimizer v1.2's demo copy

#include "cutcurve_stock.h"

namespace cutcurve
{

void stockBuild(const float *positions, size_t vertexCount, size_t stride, const unsigned *indices, size_t indexCount,
                StockDag &out)
{
    stock_clodConfig cfg = stock_clodDefaultConfig(128);
    cfg.simplify_fallback_sloppy = false;   // the audit's STOCK arm (its "stocksloppy" was a fifth, unquoted)
    stock_clodMesh m = {};
    m.indices = indices;
    m.index_count = indexCount;
    m.vertex_count = vertexCount;
    m.vertex_positions = positions;
    m.vertex_positions_stride = stride;
    m.vertex_attributes = nullptr;
    m.vertex_attributes_stride = 0;
    m.attribute_count = 0;
    out = StockDag();
    stock_clodBuild(cfg, m, &out, [](void *ctx, stock_clodGroup group, const stock_clodCluster *cl, size_t n) -> int {
        StockDag *d = static_cast<StockDag *>(ctx);
        const int id = int(d->groupError.size());
        d->groupError.push_back(group.simplified.error);
        d->groupDepth.push_back(group.depth);
        for (size_t i = 0; i < n; ++i) {
            d->clusterGroup.push_back(id);
            d->clusterRefined.push_back(cl[i].refined);
            d->clusterTriangles.push_back(int(cl[i].index_count / 3));
        }
        return id;
    });
}

}   // namespace cutcurve
