// gi.voxel_lab — THE VOXEL LAB (PHOTON-VOXEL-4): the reader's plane march on the CPU over an
// ANALYTIC store (SYNTH: boxes voxelised exactly into the split coverage and position), against
// the CONE-TRACE REFERENCE of the true geometry. No engine: tests/support/voxel_lab.h is the
// march, the store, the references and the cone sets. It is the instrument the reader's rules
// were chosen on (spikes/photon-voxel-4/EVIDENCE.txt), kept as the RECORD every later lane
// starts from - it prints; it asserts only what is physics (a closed shell's analytic store is
// closed; the reference is a probability).
//
// ARMS (the ctest row runs `sealed 64` and `split 32`):
//   sealed [R]   gi.ddgi_ambient case 5's shell (0.4 m walls, overlapping corners) at R^3 cubic
//                cells: the four-cone set from every inner surface point on a 0.3 m grid; every
//                escape is sky through a closed shell. Per CLASS (A a wall within 0.4 m above
//                the floor, B the ceiling-wall edge, C the floor within 0.4 m of a wall, D the
//                rest), for the shipped reader and the candidates the lab measured.
//   sets [R]     the same shell for every set (four, six, Fibonacci-8/12/16): the per-set record.
//   split [R]    gi.ddgi_ambient's open scene (the floor slab and the wall in its 18 x 9 x 18 m
//                box, 2R x R x 2R cubic cells): per set, BAR 1 (the read against the cone-trace
//                reference - the store's error) and BAR 2 (the reference against the hemisphere's
//                truth - the set's error).
//   validate DIR the reader over stores the suites dumped (tests/support/voxeldump.h,
//                JAH_VOXEL_DUMP=DIR): the four-cone set at gi.ddgi_ambient's two floor points.
#include "../support/voxel_lab.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace voxlab;

static int failures = 0;
#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        if (cond) std::printf("ok: %s\n", msg);                            \
        else { std::printf("FAIL: %s\n", msg); ++failures; }               \
    } while (0)

static const std::vector<Box> kShell = {
    { { -4.4, -0.4, -4.4 }, { 4.4, 0.0, 4.4 } }, { { -4.4, 5.0, -4.4 }, { 4.4, 5.4, 4.4 } },
    { { -4.4, 0.0, -4.4 }, { 4.4, 5.0, -4.0 } }, { { -4.4, 0.0, -4.4 }, { -4.0, 5.0, 4.4 } },
    { { 4.0, 0.0, -4.4 }, { 4.4, 5.0, 4.4 } },   { { -4.4, 0.0, 4.0 }, { 4.4, 5.0, 4.4 } } };

struct Candidate { const char *name; bool integ, box, startTexel, latTile; };

/// The sealed shell for one set under the switches set now: the per-class table.
static void sealedRun(const std::vector<Cascade> &ch, const ConeSet &st, const char *label, double &worstPoint)
{
    struct S { const char *name; int n; double sg; double fixed; };
    const S surf[] = { { "floor", 1, 1.0, 0.0 }, { "ceiling", 1, -1.0, 5.0 }, { "-Z wall", 2, 1.0, -4.0 },
                       { "+Z wall", 2, -1.0, 4.0 }, { "-X wall", 0, 1.0, -4.0 }, { "+X wall", 0, -1.0, 4.0 } };
    long nCones = 0, nEsc = 0, nPts = 0, cE[4] = { 0, 0, 0, 0 }, cN[4] = { 0, 0, 0, 0 };
    double worstC = 0, sumP = 0, cW[4] = { 0, 0, 0, 0 }, cP[4] = { 0, 0, 0, 0 };
    worstPoint = 0;
    for (const S &sf : surf) {
        const int u = (sf.n + 1) % 3, v = (sf.n + 2) % 3;
        const double lo[3] = { -3.9, 0.1, -3.9 }, hi[3] = { 3.9, 4.9, 3.9 };
        for (double a = lo[u]; a <= hi[u] + 1e-9; a += 0.3)
            for (double b = lo[v]; b <= hi[v] + 1e-9; b += 0.3) {
                double p[3];
                p[sf.n] = sf.fixed; p[u] = a; p[v] = b;
                const bool wall = sf.n != 1;
                const double nearWall = std::min(4.0 - std::fabs(p[0]), 4.0 - std::fabs(p[2]));
                int cls = 3;
                if (wall && p[1] <= 0.41) cls = 0;
                else if ((wall && p[1] >= 4.59) || (sf.n == 1 && sf.sg < 0 && nearWall <= 0.41)) cls = 1;
                else if (sf.n == 1 && sf.sg > 0 && nearWall <= 0.41) cls = 2;
                double leak = 0;
                for (size_t j = 0; j < st.dirs.size(); ++j) {
                    double dir[3];
                    dir[sf.n] = sf.sg * st.dirs[j][1]; dir[u] = st.dirs[j][0]; dir[v] = st.dirs[j][2];
                    const double esc = keepOf(walkN(ch, true, p, sf.n, sf.sg, dir, st.tan));
                    ++nCones; ++cN[cls];
                    if (esc > 1e-3) { ++nEsc; ++cE[cls]; }
                    worstC = std::max(worstC, esc);
                    cW[cls] = std::max(cW[cls], esc);
                    leak += st.w[j] * esc;
                }
                ++nPts; sumP += leak;
                cP[cls] = std::max(cP[cls], leak);
                worstPoint = std::max(worstPoint, leak);
            }
    }
    std::printf("   %-24s %-34s cones escaping %5ld of %5ld, worst cone %.3f, worst point %.4f, mean %.5f\n",
                st.name.c_str(), label, nEsc, nCones, worstC, worstPoint, sumP / std::max<long>(nPts, 1));
    const char *cn[4] = { "A wall <=0.4 m above the floor", "B the ceiling-wall edge", "C floor <=0.4 m from a wall",
                          "D elsewhere" };
    for (int k = 0; k < 4; ++k)
        std::printf("      class %-32s %5ld of %5ld escape, worst cone %.3f, worst point %.4f\n", cn[k], cE[k], cN[k],
                    cW[k], cP[k]);
}

static std::vector<Cascade> sealedStore(int R)
{
    const double org[3] = { -5.0, -2.5, -5.0 }, size[3] = { 10.0, 10.0, 10.0 };
    const int Rv[3] = { R, R, R };
    std::vector<Cascade> ch = { synthStore(kShell, false, 0.0, org, size, Rv) };
    buildDir(ch[0]);
    return ch;
}

static void armSealed(int R)
{
    std::printf("== THE SEALED ROOM, four-cone set, %d^3 cubic (SYNTH)\n", R);
    const std::vector<Cascade> ch = sealedStore(R);
    const ConeSet four = coneSets()[0];
    const Candidate cands[] = { { "the shipped reader", false, false, false, false },
                                { "+ minor axes integrated (not shipped)", true, true, true, false },
                                { "+ lateral tiling (not shipped)", true, true, true, true } };
    for (const Candidate &k : cands) {
        gInteg = k.integ; gBox = k.box; gStartTexel = k.startTexel; gLatTile = k.latTile;
        double worst = 0;
        sealedRun(ch, four, k.name, worst);
        CHECK(worst == worst && worst <= 1.0, "the sealed room's leak is a share of the sky");
    }
    gInteg = gBox = gStartTexel = gLatTile = false;
    std::printf("   (A/C: the crease within 0.4 m of the floor - the floor's texel spreads its surface over the\n"
                "    part behind a start 0.1 m up; B/D: the coarse edge - the plane axis's lateral reach is its\n"
                "    own texel, a surface below the axis's crossing of the ceiling never read. VOXEL-5: a\n"
                "    lateral-only mip family per axis.)\n");
    // THE ANALYTIC STORE IS CLOSED: a ray (aperture 0) from the room's middle, in every axis
    // direction, is stopped by the shell (both halves, the ray rule).
    const double mid[3] = { 0.0, 2.5, 0.0 };
    bool closed = true;
    for (int a = 0; a < 3; ++a)
        for (double sg : { -1.0, 1.0 }) {
            double d[3] = { 0, 0, 0 };
            d[a] = sg;
            double dl[3] = { d[0] + 1e-3, d[1] + 2e-3, d[2] + 3e-3 };
            if (walkN(ch, true, mid, 1, 1.0, dl, 0.0) < 0.95) closed = false;
        }
    CHECK(closed, "the analytic shell stops a ray from the room's middle in every axis direction");
}

static void armSets(int R)
{
    std::printf("== THE SEALED ROOM, every set, %d^3 cubic (SYNTH; the shipped reader)\n", R);
    const std::vector<Cascade> ch = sealedStore(R);
    for (const ConeSet &st : coneSets()) {
        if (st.name.find("turned") != std::string::npos) continue;
        double worst = 0;
        sealedRun(ch, st, "", worst);
    }
}

static void armSplit(int R)
{
    std::printf("== gi.ddgi_ambient's OPEN SCENE, %d x %d x %d cubic cells (SYNTH): BAR 1 read - ref, BAR 2 ref - truth\n",
                2 * R, R, 2 * R);
    const std::vector<Box> wall = { { { -6.0, 0.0, -2.2 }, { 6.0, 4.0, -1.8 } } };
    const std::vector<Box> scene = { { { -8.0, -0.1, -8.0 }, { 8.0, 0.0, 8.0 } }, wall[0] };
    const double org[3] = { -9.0, -1.5, -9.0 }, size[3] = { 18.0, 9.0, 18.0 };
    const int Rv[3] = { 2 * R, R, 2 * R };
    std::vector<Cascade> ch = { synthStore(scene, false, 0.0, org, size, Rv) };
    const double pts[2][3] = { { 0.01, 1e-4, 3.42 }, { 0.02, 1e-4, 0.40 } };
    const char *ptName[2] = { "open floor", "wall foot" };
    for (int p = 0; p < 2; ++p) {
        const double truth = truthVis(pts[p], wall);
        for (const ConeSet &st : coneSets()) {
            double kRead = 0, kRef = 0;
            bool sane = true;
            for (size_t j = 0; j < st.dirs.size(); ++j) {
                const double ar = refAlpha(pts[p], st.dirs[j].data(), st.tan, wall);
                const double a = walkN(ch, false, pts[p], 1, 1.0, st.dirs[j].data(), st.tan);
                if (!(ar >= 0 && ar <= 1)) sane = false;
                kRead += st.w[j] * keepOf(a);
                kRef += st.w[j] * keepOf(ar);
            }
            std::printf("   %-10s %-24s read %.4f ref %.4f truth %.4f | BAR 1 %+.4f  BAR 2 %+.4f\n", ptName[p],
                        st.name.c_str(), kRead, kRef, truth, kRead - kRef, kRef - truth);
            if (!sane) { std::printf("FAIL: the cone-trace reference is not a share\n"); ++failures; }
        }
    }
}

static void armValidate(const std::string &dir)
{
    std::printf("== THE READER OVER DUMPED STORES (%s)\n", dir.c_str());
    for (const char *tag : { "amb", "amb2" }) {
        std::vector<Cascade> ch;
        for (int c = 0; c < 8; ++c) {
            Cascade x;
            if (!loadDump(dir + "/" + tag + "_" + std::to_string(c) + ".bin", x)) break;
            ch.push_back(x);
        }
        if (ch.empty()) continue;
        const bool directional = std::string(tag) == "amb2";   // the anisotropic tier's store
        if (directional)
            for (Cascade &x : ch) buildDir(x);
        setHandOver(ch);
        const double pts[2][3] = { { 0.01, 1e-4, 3.42 }, { 0.02, 1e-4, 0.40 } };
        const ConeSet four = coneSets()[0];
        for (auto &p : pts) {
            std::printf("   %s %s:", tag, p[2] > 1 ? "open floor" : "wall foot ");
            for (auto &d : four.dirs) std::printf(" %.3f", walkN(ch, directional, p, 1, 1.0, d.data(), four.tan));
            std::printf("\n");
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        armSealed(64);
        armSplit(32);
    } else {
        const std::string arm = argv[1];
        const int R = argc > 2 ? std::atoi(argv[2]) : 64;
        if (arm == "sealed") armSealed(R);
        else if (arm == "sets") armSets(R);
        else if (arm == "split") armSplit(R);
        else if (arm == "validate" && argc > 2) armValidate(argv[2]);
        else { std::printf("usage: %s [sealed|sets|split R | validate DIR]\n", argv[0]); return 2; }
    }
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
