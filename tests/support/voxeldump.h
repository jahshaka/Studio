// THE STORE DUMP (PHOTON-VOXEL-4): `JAH_VOXEL_DUMP=<dir>` makes a suite that calls it write each
// cascade's read-back split store (GiVoxelVolume) to <dir>/<tag>_<cascade>.bin - dims, origin,
// cell, multiplier, then per texel RGBA float coverage+, coverage-, position+, position-, light. The
// voxel lab reads them (tests/support/voxel_lab.h loadDump; gi.voxel_lab's `validate DIR` arm):
// the reader's rules are measured over the stores the suites' scenes really build. Unset, it
// does nothing.
#pragma once
#include "jahshaka/engine/Engine.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace enginetest {

inline void dumpVoxelStore(jahshaka::engine::Scene *s, const char *tag)
{
    const char *dir = std::getenv("JAH_VOXEL_DUMP");
    if (!dir) return;
    for (int c = 0; c < 8; ++c) {
        jahshaka::engine::GiVoxelVolume v;
        if (!s->giVoxelVolume(c, v) || !v.available) break;
        const std::string path = std::string(dir) + "/" + tag + "_" + std::to_string(c) + ".bin";
        FILE *f = std::fopen(path.c_str(), "wb");
        if (!f) break;
        const int dims[3] = { v.width, v.height, v.depth };
        std::fwrite(dims, sizeof(int), 3, f);
        std::fwrite(v.origin, sizeof(float), 3, f);
        std::fwrite(v.cell, sizeof(float), 3, f);
        std::fwrite(&v.multiplier, sizeof(float), 1, f);
        std::fwrite(v.coverageP.data(), sizeof(float), v.coverageP.size(), f);
        std::fwrite(v.coverageN.data(), sizeof(float), v.coverageN.size(), f);
        std::fwrite(v.positionP.data(), sizeof(float), v.positionP.size(), f);
        std::fwrite(v.positionN.data(), sizeof(float), v.positionN.size(), f);
        // then the total light (rgba, premultiplied by c, the volume's k units) - after the four
        // the lab's loadDump reads, so older readers stop before it
        std::fwrite(v.light.data(), sizeof(float), v.light.size(), f);
        std::fclose(f);
        std::printf("   voxel store dumped: %s (%dx%dx%d)\n", path.c_str(), v.width, v.height, v.depth);
    }
}

}   // namespace enginetest
