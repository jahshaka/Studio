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
//   THE LIGHT (tests/support/voxel_lab_colour.h; PHOTON-VOXEL-5 item (ii)), per light model - one
//   light per voxel, LIGHT PER FACE SIDE (two sides by the voxel's normal groups), per face:
//   leak DIR [T]  gi.gather's leak room on a dump's four lattices: the red the pixel's cones
//                 carry, and the halves holding red their faces do not carry, by voxel class;
//   thin DIR [dz] its thin walls: a ray's hit read and a probe's ray (the lattices moved dz);
//   roof DIR      gi.voxel_coverage's MEASURE-2 (b) roof: the underside's light, the floor's cones;
//   flatwall [R]  a dark wall over a lit 0.1 m floor: the column the cones read.
//   THE STORE LIT BY LAMPS THROUGH THE INJECTION (synthLamps: its normal, its shadow march), per
//   store - one light, two sides, the FUSED form (light per half-axis into the directional level
//   0; axis / group / THE GPU PLAN's side cosine), per face: `fused DIR [dz] [T]` the leak room
//   with its two lamps (cones, hit read, probe ray), `fusedroof DIR`, `fusedflat [R]`, all with
//   THE SHIPPED MARCH (a 3D-DDA over the voxels, each half's plane by its stored position, the
//   per-half march starting on its face); the `...stepped` twins run the RETIRED stepped march
//   from the voxel's centre, the record of why it went; `fusedwhy DIR T CASCADE [stepped]` lists
//   the halves holding red their faces do not.
//   LAB_ROWS_FROM=N prints the stores from row N.
#include "../support/voxel_lab.h"
#include "../support/voxel_lab_colour.h"

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
                "    own texel, a surface below the axis's crossing of the ceiling never read. THE LATERAL-ONLY\n"
                "    MIP FAMILY (PHOTON-VOXEL-5 item (i)) closed neither and is deleted - its table is in\n"
                "    spikes/photon-voxel-5/EVIDENCE.txt.)\n");
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

static int gRowFrom = 0;
static const char *kModelName[3] = { "ONE LIGHT PER VOXEL (before)", "LIGHT PER FACE SIDE (the store)",
                                     "PER FACE (the ideal)" };

/// The four cascades' lattices from the headers of a JAH_VOXEL_DUMP (voxel_leak_trace's room).
static std::vector<Cascade> dumpHeads(const std::string &dir)
{
    std::vector<Cascade> heads;
    for (int k = 0; k < 8; ++k) {
        Cascade x;
        if (!loadDump(dir + "/leak_" + std::to_string(k) + ".bin", x)) break;
        heads.push_back(x);
    }
    return heads;
}

static std::vector<LitCascade> litChain(const std::vector<Cascade> &heads, const std::vector<Box> &boxes,
                                        const std::function<Rgb(int, int, int)> &rad, int model)
{
    std::vector<LitCascade> ch;
    for (const Cascade &h : heads) ch.push_back(synthLit(boxes, rad, model, h.origin, h.size, h.R));
    std::vector<Cascade> plain;
    for (const LitCascade &lc : ch) plain.push_back(lc.c);
    setHandOver(plain);
    for (size_t j = 0; j < ch.size(); ++j) ch[j].c.maxLod = plain[j].maxLod;
    return ch;
}

/// gi.gather's leak room (tests/support/leakroom.h): a sealed 10 m room of wall thickness T.
static std::vector<Box> leakBoxes(double T)
{
    const double ho = 5.0 + T * 0.5, span = 10.0 + 2.0 * T;
    const auto slab = [&](double cx, double cy, double cz, double sx, double sy, double sz) {
        return Box{ { cx - sx / 2, cy - sy / 2, cz - sz / 2 }, { cx + sx / 2, cy + sy / 2, cz + sz / 2 } };
    };
    return { slab(0, -T * 0.5, 0, span, T, span), slab(0, 4.0 + T * 0.5, 0, span, T, span),
             slab(0, 2, -ho, span, 4.0, T),       slab(0, 2, ho, span, 4.0, T),
             slab(-ho, 2, 0, T, 4.0, span),       slab(ho, 2, 0, T, 4.0, span) };
}
/// ...its light: the -Z wall's outer face red 1.0 (the lamp outside), every face looking into the
/// room green 0.3 (the lamp inside), the rest dark.
static std::function<Rgb(int, int, int)> leakRad(const std::vector<Box> &boxes)
{
    return [boxes](int bi, int a, int side) {
        const Box &b = boxes[size_t(bi)];
        double c[3];
        for (int k = 0; k < 3; ++k) c[k] = 0.5 * (b.lo[k] + b.hi[k]);
        c[a] = (side ? b.hi[a] + 1e-3 : b.lo[a] - 1e-3);
        const bool interior = std::fabs(c[0]) < 5.0 && std::fabs(c[2]) < 5.0 && c[1] > 0.0 && c[1] < 4.0;
        Rgb L;
        if (interior) L.g = 0.3;
        else if (bi == 2 && a == 2 && side == 0) L.r = 1.0;
        return L;
    };
}

/// THE LEAK ROOM WITH LIGHT (PHOTON-VOXEL-5 items (iii) / (ii)): the room voxelised on the four
/// cascades' own lattices, the pixel's four cones from the -Z wall's inner face at nine points:
/// the red each cone carries, per light model.
static void armLeak(const std::string &dir, double T)
{
    std::printf("== THE LEAK ROOM WITH LIGHT, wall %.2f m (SYNTH on the dumped lattices of %s)\n", T, dir.c_str());
    const std::vector<Box> boxes = leakBoxes(T);
    const std::vector<Cascade> heads = dumpHeads(dir);
    if (heads.empty()) { std::printf("   (no dump in %s)\n", dir.c_str()); return; }
    const double ax[4][3] = { { 0.707107, 0, 0.707107 }, { 0, 0.707107, 0.707107 }, { -0.707107, 0, 0.707107 },
                              { 0, -0.707107, 0.707107 } };
    double redOf[3] = { 0, 0, 0 };
    for (int model = 0; model < 3; ++model) {
        const std::vector<LitCascade> ch = litChain(heads, boxes, leakRad(boxes), model);
        int cones = 0, red = 0, byC[8] = { 0 }, redByC[8] = { 0 };
        double sumR = 0, sumG = 0, worstRatio = 0;
        for (double px : { 3.6, 2.0, 0.0 })
            for (double py : { 2.0, 0.5, 3.5 }) {
                const double pw[3] = { px, py, -5.0 };
                for (int k = 0; k < 4; ++k) {
                    int lc = 0;
                    const ResultC R = walkNC(ch, pw, 2, 1.0, ax[k], 0.98269, lc);
                    ++cones; ++byC[lc];
                    if (R.col[0] > 1e-3) { ++red; ++redByC[lc]; }
                    sumR += R.col[0]; sumG += R.col[1];
                    worstRatio = std::max(worstRatio, R.col[1] > 0 ? R.col[0] / R.col[1] : 0.0);
                }
            }
        std::printf("   %-34s %2d/%d cones carry red (by the cascade they stop in:", kModelName[model], red, cones);
        for (int j = 0; j < int(ch.size()); ++j) std::printf(" c%d %d/%d", j, redByC[j], byC[j]);
        std::printf("); mean r %.5f g %.5f, worst cone r/g %.4f\n", sumR / cones, sumG / cones, worstRatio);
        redOf[model] = sumR / cones;
    }
    // THE MECHANISM: per cascade, the halves (voxel, axis, side) with coverage whose stored red
    // under LIGHT PER FACE SIDE exceeds the ideal's (their own faces'), by the voxel's class -
    // two-sided or not, and how many face orientations it holds.
    {
        const std::vector<LitCascade> ds = litChain(heads, boxes, leakRad(boxes), kPerSide);
        const std::vector<LitCascade> id = litChain(heads, boxes, leakRad(boxes), kPerFace);
        for (size_t j = 0; j < ds.size(); ++j) {
            const Cascade &c = ds[j].c;
            const size_t n = size_t(c.R[0]) * c.R[1] * c.R[2];
            long bad[2][7] = {}, halves = 0;
            for (size_t i = 0; i < n; ++i) {
                int orient = 0; double sP = 0, sN = 0;
                for (int h = 0; h < 2; ++h)
                    for (int a = 0; a < 3; ++a)
                        if (c.L[0].O[h][i * 3 + a] > 0) { ++orient; (h == kP ? sP : sN) += 1; }
                if (!orient) continue;
                // two-sided by the lab's own test on the coverage sums (synthLit's)
                double nU[3], nF[3];
                for (int a = 0; a < 3; ++a) { nU[a] = c.L[0].O[kP][i * 3 + a]; nF[a] = -c.L[0].O[kN][i * 3 + a]; }
                const double dUF = nU[0] * nF[0] + nU[1] * nF[1] + nU[2] * nF[2];
                const double lU = nU[0] * nU[0] + nU[1] * nU[1] + nU[2] * nU[2], lF = nF[0] * nF[0] + nF[1] * nF[1] + nF[2] * nF[2];
                const int two = (sP > 0 && sN > 0 && dUF < 0 && dUF * dUF > 0.25 * lU * lF) ? 1 : 0;
                for (int h = 0; h < 2; ++h)
                    for (int a = 0; a < 3; ++a) {
                        if (!(c.L[0].O[h][i * 3 + a] > 0)) continue;
                        ++halves;
                        if (ds[j].rad0[h][i * 9 + a * 3] > id[j].rad0[h][i * 9 + a * 3] + 1e-6) ++bad[two][std::min(orient, 6)];
                    }
            }
            std::printf("   cascade %zu (cell %.3f m): %ld covered halves; red the faces do not carry - one-sided voxels by "
                        "orientations 1..6: %ld %ld %ld %ld %ld %ld | two-sided: %ld %ld %ld %ld %ld %ld\n", j, c.cell[0], halves,
                        bad[0][1], bad[0][2], bad[0][3], bad[0][4], bad[0][5], bad[0][6], bad[1][1], bad[1][2], bad[1][3],
                        bad[1][4], bad[1][5], bad[1][6]);
        }
    }
    CHECK(redOf[kPerFace] <= 1e-9, "the ideal (each half its own faces' light) carries no red");
}

/// gi.gather's THIN WALL (the leak room at 0.50 / 0.20 / 0.10 / 0.05 m): a RAY's hit on the -Z
/// wall's inner face read by jahVoxelRadiance (the texel either side, the larger opacity) at lod
/// 0 and 1, and a field probe's RAY (aperture 0, the march) from 0.5 m inside the wall - the red
/// each reads, per light model. The physics: the inner face's green 0.3, no red.
static void armThin(const std::string &dir, double shiftZ)
{
    std::printf("== gi.gather's THIN WALL: a ray's hit read and a probe ray, per light model (SYNTH on %s, the lattices "
                "moved %.3f m along z)\n", dir.c_str(), shiftZ);
    std::vector<Cascade> heads = dumpHeads(dir);
    for (Cascade &h : heads) h.origin[2] += shiftZ;
    if (heads.empty()) { std::printf("   (no dump in %s)\n", dir.c_str()); return; }
    const double dirs[3][3] = { { 0, 0, -1 }, { 0.5, 0, -0.866025 }, { 0, -0.5, -0.866025 } };
    for (double T : { 0.5, 0.2, 0.1, 0.05 }) {
        const std::vector<Box> boxes = leakBoxes(T);
        for (int model = 0; model < 3; ++model) {
            const std::vector<LitCascade> ch = litChain(heads, boxes, leakRad(boxes), model);
            double hr[2] = { 0, 0 }, hg[2] = { 0, 0 }, fr = 0, fg = 0;
            int nh = 0, nf = 0;
            for (double px : { 3.6, 2.0, 0.0 })
                for (double py : { 2.0, 0.5, 3.5 })
                    for (const auto &d : dirs) {
                        const double hit[3] = { px, py, -5.0 };
                        for (int l = 0; l < 2; ++l) {
                            double rgb[3]; bool ok = false;
                            hitReadC(ch, hit, d, double(l), rgb, ok);
                            hr[l] += rgb[0]; hg[l] += rgb[1];
                        }
                        ++nh;
                        // the probe 0.5 m in front of the face, the ray toward the same hit
                        const double pp[3] = { px - d[0] * 0.5 / -d[2], py - d[1] * 0.5 / -d[2], -4.5 };
                        const ResultC R = walkRayC(ch, pp, d);
                        fr += R.col[0]; fg += R.col[1]; ++nf;
                    }
            std::printf("   %.2f m %-34s hit lod 0: r %.4f g %.4f | lod 1: r %.4f g %.4f | probe ray: r %.4f g %.4f\n", T,
                        kModelName[model], hr[0] / nh, hg[0] / nh, hr[1] / nh, hg[1] / nh, fr / nf, fg / nf);
            if (model == kPerSide)
                std::printf("   %s\n", hr[0] / nh <= 1e-6 && hr[1] / nh <= 1e-6 && fr / nf <= 1e-6
                                         ? "(light per face side: no red in the hit or the probe's ray)"
                                         : "(light per face side: RED in the hit or the probe's ray)");
        }
    }
}

/// MEASURE-2 (b) IN THE LAB (gi.voxel_coverage's roof): a 0.1 m roof lit by the sun over a closed
/// dark room, at x = 11 (inside cascade 2) and x = 40 (only in cascade 3), on the dumped lattices:
/// over the roof voxels above the room's interior the radiance x coverage the top's half (+y) and
/// the underside's (-y) carry, and the four cones from the room's floor (the physics: 0 - the room
/// is sealed and dark), per light model.
static void armRoof(const std::string &dir)
{
    std::printf("== MEASURE-2 (b) IN THE LAB: a 0.1 m sunlit roof over a closed dark room (SYNTH on %s)\n", dir.c_str());
    const std::vector<Cascade> heads = dumpHeads(dir);
    if (heads.size() < 4) { std::printf("   (no four-cascade dump in %s)\n", dir.c_str()); return; }
    const auto slab = [](double x, double y, double z, double sx, double sy, double sz) {
        return Box{ { x - sx / 2, y - sy / 2, z - sz / 2 }, { x + sx / 2, y + sy / 2, z + sz / 2 } };
    };
    const double rooms[2] = { 11.0, 40.0 };
    std::vector<Box> boxes;
    for (double cx : rooms) {
        boxes.push_back(slab(cx, -0.25, 0.0, 7.0, 0.5, 7.0));
        boxes.push_back(slab(cx, 3.05, 0.0, 7.0, 0.1, 7.0));
        boxes.push_back(slab(cx - 3.35, 1.5, 0.0, 0.3, 3.0, 7.0));
        boxes.push_back(slab(cx + 3.35, 1.5, 0.0, 0.3, 3.0, 7.0));
        boxes.push_back(slab(cx, 1.5, -3.35, 7.0, 3.0, 0.3));
        boxes.push_back(slab(cx, 1.5, 3.35, 7.0, 3.0, 0.3));
    }
    double l[3] = { -0.1, 1.0, -0.2 };
    const double ll = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    for (double &v : l) v /= ll;
    const auto rad = [&](int bi, int a, int side) {
        const Box &b = boxes[size_t(bi)];
        double c[3];
        for (int k = 0; k < 3; ++k) c[k] = 0.5 * (b.lo[k] + b.hi[k]);
        c[a] = side ? b.hi[a] + 1e-3 : b.lo[a] - 1e-3;
        const double cx = rooms[bi / 6];
        const bool interior = std::fabs(c[0] - cx) < 3.2 && std::fabs(c[2]) < 3.2 && c[1] > 0.0 && c[1] < 3.0;
        const double nl = (side ? 1.0 : -1.0) * l[a];
        Rgb L;
        if (!interior && nl > 0) L.r = L.g = L.b = 0.8 * nl;
        return L;
    };
    for (int model = 0; model < 3; ++model) {
        const std::vector<LitCascade> ch = litChain(heads, boxes, rad, model);
        std::printf("   %s:\n", kModelName[model]);
        for (int k = 0; k < 2; ++k) {
            const Cascade &c = ch[size_t(2 + k)].c;
            double top = 0, under = 0;
            long n = 0;
            for (int z = 0; z < c.R[2]; ++z) for (int y = 0; y < c.R[1]; ++y) for (int x = 0; x < c.R[0]; ++x) {
                const double lo[3] = { c.origin[0] + x * c.cell[0], c.origin[1] + y * c.cell[1], c.origin[2] + z * c.cell[2] };
                if (lo[0] + c.cell[0] <= rooms[k] - 2.5 || lo[0] >= rooms[k] + 2.5 || lo[2] + c.cell[2] <= -2.5 || lo[2] >= 2.5)
                    continue;
                if (lo[1] > 3.1 || lo[1] + c.cell[1] < 3.0) continue;
                const size_t i = (size_t(z) * c.R[1] + y) * c.R[0] + x;
                const LitCascade &lc = ch[size_t(2 + k)];
                top += lc.rad0[kP][i * 9 + 3] * c.L[0].O[kP][i * 3 + 1];
                under += lc.rad0[kN][i * 9 + 3] * c.L[0].O[kN][i * 3 + 1];
                ++n;
            }
            // the room's floor, the four cones (normal +y) at nine points
            const double four[4][3] = { { 0.707107, 0.707107, 0 }, { 0, 0.707107, 0.707107 }, { -0.707107, 0.707107, 0 },
                                        { 0, 0.707107, -0.707107 } };
            double sum = 0, worst = 0;
            int cones = 0;
            for (double px : { -2.0, 0.0, 2.0 })
                for (double pz : { -2.0, 0.0, 2.0 }) {
                    const double pw[3] = { rooms[k] + px, 0.0, pz };
                    for (const auto &d : four) {
                        int lc = 0;
                        const ResultC R = walkNC(ch, pw, 1, 1.0, d, 0.98269, lc);
                        sum += R.col[1]; worst = std::max(worst, R.col[1]); ++cones;
                    }
                }
            std::printf("     cascade %d (cell %.3f m, room x = %.0f): %ld roof voxels, top %.4f, UNDERSIDE %.4f (%.2f of "
                        "the top's; the physics 0) | the floor's cones read %.4f mean, %.4f worst (the physics 0)\n",
                        2 + k, c.cell[0], rooms[k], n, n ? top / n : 0.0, n ? under / n : 0.0, top > 0 ? under / top : 0.0,
                        sum / cones, worst);
            if (model == kPerFace)
                CHECK(under <= 1e-9 * std::max(1.0, top), "the ideal: the roof's underside carries none of the top's light");
        }
    }
}

/// THE FLAT WALL (gi.card_lighting_indirect's class): a 0.4 m wall standing on a 0.1 m floor lit
/// from above (radiance 0.5), the wall's face dark; the four cones from the wall's face at 0.8 /
/// 1.6 / 2.8 m on gi.ddgi_ambient's open-scene lattice - the colour must not change with the light
/// model where no voxel holds two opposed faces, and the floor's lit top must read the same.
static void armFlatWall(int R)
{
    std::printf("== THE FLAT WALL, %d x %d x %d cubic cells (SYNTH): the four cones from a dark wall over a lit floor\n",
                2 * R, R, 2 * R);
    const std::vector<Box> scene = { { { -8.0, -0.1, -8.0 }, { 8.0, 0.0, 8.0 } }, { { -6.0, 0.0, -2.2 }, { 6.0, 4.0, -1.8 } } };
    const auto rad = [](int bi, int a, int side) { Rgb L; if (bi == 0 && a == 1 && side == 1) L.r = L.g = L.b = 0.5; return L; };
    const double org[3] = { -9.0, -1.5, -9.0 }, size[3] = { 18.0, 9.0, 18.0 };
    const int Rv[3] = { 2 * R, R, 2 * R };
    const double ax[4][3] = { { 0.707107, 0, 0.707107 }, { 0, 0.707107, 0.707107 }, { -0.707107, 0, 0.707107 },
                              { 0, -0.707107, 0.707107 } };
    double col[3][3] = {};
    for (int model = 0; model < 3; ++model) {
        std::vector<LitCascade> ch = { synthLit(scene, rad, model, org, size, Rv) };
        std::printf("   %-34s", kModelName[model]);
        int i = 0;
        for (double py : { 0.8, 1.6, 2.8 }) {
            const double pw[3] = { 0.0, py, -1.8 };
            double s = 0;
            for (const auto &d : ax) { int lc = 0; s += 0.25 * walkNC(ch, pw, 2, 1.0, d, 0.98269, lc).col[1]; }
            col[model][i++] = s;
            std::printf("  %.1f m: %.4f", py, s);
        }
        std::printf("\n");
    }
    double worst = 0;
    for (int i = 0; i < 3; ++i) worst = std::max(worst, std::fabs(col[kPerSide][i] / col[kOnePerVoxel][i] - 1.0));
    std::printf("   (the floor 0.1 m on %.3f m cells; light per face side against one light per voxel: worst %.2f %%)\n",
                18.0 / (2 * R), 100.0 * worst);
    CHECK(worst <= 0.01, "the flat wall's column moves by at most 1 % with light per face side");
}


static const char *kInjName[6] = { "one light (before)", "two sides (lab2's design)", "FUSED, axis cosine",
                                   "FUSED, group cosine", "per face (ideal)", "FUSED, THE GPU PLAN" };

/// The chain lit by lamps, row r of kInjName.
static std::vector<LitCascade> lampChain(const std::vector<Cascade> &heads, const std::vector<Box> &boxes,
                                         const std::vector<Lamp> &lamps, int row)
{
    const int model = row <= 1 ? row : (row <= 3 ? kInjFused : (row == 4 ? kInjIdeal : kInjFusedGpu));
    std::vector<LitCascade> ch;
    for (const Cascade &h : heads) ch.push_back(synthLamps(boxes, lamps, 0.8, model, row == 3, h.origin, h.size, h.R));
    std::vector<Cascade> plain;
    for (const LitCascade &lc : ch) plain.push_back(lc.c);
    setHandOver(plain);
    for (size_t j = 0; j < ch.size(); ++j) ch[j].c.maxLod = plain[j].maxLod;
    return ch;
}

/// THE FUSED FORM IN THE LAB (PHOTON-VOXEL-5 item (ii), the lead's option 3): gi.gather's leak
/// room LIT BY ITS LAMPS (the green point lamp inside at (0, 3, 2.5), intensity 3; the red one a
/// metre outside the -Z wall, 25) through the injection's own lighting - its normal, its shadow
/// march - per store: the pixel's four cones from the -Z wall's inner face (nine points), a ray's
/// hit read on that face and a probe's ray toward it. The physics: no red inside.
static void armFused(const std::string &dir, double shiftZ, double onlyT)
{
    std::printf("== THE FUSED FORM: the leak room lit by its lamps through the injection (SYNTH on %s, lattices moved "
                "%.3f m in z)\n", dir.c_str(), shiftZ);
    std::vector<Cascade> heads = dumpHeads(dir);
    if (heads.empty()) { std::printf("   (no dump in %s)\n", dir.c_str()); return; }
    for (Cascade &h : heads) h.origin[2] += shiftZ;
    const double ax[4][3] = { { 0.707107, 0, 0.707107 }, { 0, 0.707107, 0.707107 }, { -0.707107, 0, 0.707107 },
                              { 0, -0.707107, 0.707107 } };
    const double rays[3][3] = { { 0, 0, -1 }, { 0.5, 0, -0.866025 }, { 0, -0.5, -0.866025 } };
    for (double T : { 0.5, 0.2, 0.1, 0.05 }) {
        if (onlyT > 0 && std::fabs(T - onlyT) > 1e-9) continue;
        const std::vector<Box> boxes = leakBoxes(T);
        const double ho = 5.0 + T * 0.5;
        std::vector<Lamp> lamps(2);
        lamps[0].pos[0] = 0; lamps[0].pos[1] = 3.0; lamps[0].pos[2] = 2.5; lamps[0].col.g = 3.0;
        lamps[1].pos[0] = 0; lamps[1].pos[1] = 2.0; lamps[1].pos[2] = -(ho + T * 0.5 + 1.0); lamps[1].col.r = 25.0;
        for (int row = gRowFrom; row < 6; ++row) {
            const std::vector<LitCascade> ch = lampChain(heads, boxes, lamps, row);
            int cones = 0, red = 0, byC[8] = { 0 }, redByC[8] = { 0 };
            double sumR = 0, sumG = 0, worstRatio = 0, hr = 0, hg = 0, pr = 0, pg = 0;
            int nh = 0;
            for (double px : { 3.6, 2.0, 0.0 })
                for (double py : { 2.0, 0.5, 3.5 }) {
                    const double pw[3] = { px, py, -5.0 };
                    for (int k = 0; k < 4; ++k) {
                        int lc = 0;
                        const ResultC R = walkNC(ch, pw, 2, 1.0, ax[k], 0.98269, lc);
                        ++cones; ++byC[lc];
                        if (R.col[0] > 1e-3 * std::max(R.col[1], 1e-3)) { ++red; ++redByC[lc]; }
                        sumR += R.col[0]; sumG += R.col[1];
                        worstRatio = std::max(worstRatio, R.col[1] > 0 ? R.col[0] / R.col[1] : 0.0);
                    }
                    for (const auto &d : rays) {
                        double rgb[3]; bool ok = false;
                        hitReadC(ch, pw, d, 0.0, rgb, ok);
                        hr += rgb[0]; hg += rgb[1];
                        const double pp[3] = { px - d[0] * 0.5 / -d[2], py - d[1] * 0.5 / -d[2], -4.5 };
                        const ResultC R = walkRayC(ch, pp, d);
                        pr += R.col[0]; pg += R.col[1]; ++nh;
                    }
                }
            std::printf("   %.2f m %-26s cones: %2d/%d red (c3 %d/%d), mean r/g %.5f/%.4f = %.4f, worst %.4f | hit r/g "
                        "%.4f/%.4f | probe ray r/g %.4f/%.4f\n", T, kInjName[row], red, cones, redByC[3], byC[3],
                        sumR / cones, sumG / cones, sumG > 0 ? sumR / sumG : 0.0, worstRatio, hr / nh, hg / nh, pr / nh, pg / nh);
            std::fflush(stdout);
        }
    }
}

/// MEASURE-2 (b) lit by its sun through the injection, per store: the roof's underside and the
/// room's floor cones (the physics 0).
static void armFusedRoof(const std::string &dir)
{
    std::printf("== THE FUSED FORM: MEASURE-2 (b)'s roof lit by its sun through the injection (SYNTH on %s)\n", dir.c_str());
    const std::vector<Cascade> heads = dumpHeads(dir);
    if (heads.size() < 4) return;
    const auto slab = [](double x, double y, double z, double sx, double sy, double sz) {
        return Box{ { x - sx / 2, y - sy / 2, z - sz / 2 }, { x + sx / 2, y + sy / 2, z + sz / 2 } };
    };
    const double rooms[2] = { 11.0, 40.0 };
    std::vector<Box> boxes;
    for (double cx : rooms) {
        boxes.push_back(slab(cx, -0.25, 0.0, 7.0, 0.5, 7.0)); boxes.push_back(slab(cx, 3.05, 0.0, 7.0, 0.1, 7.0));
        boxes.push_back(slab(cx - 3.35, 1.5, 0.0, 0.3, 3.0, 7.0)); boxes.push_back(slab(cx + 3.35, 1.5, 0.0, 0.3, 3.0, 7.0));
        boxes.push_back(slab(cx, 1.5, -3.35, 7.0, 3.0, 0.3)); boxes.push_back(slab(cx, 1.5, 3.35, 7.0, 3.0, 0.3));
    }
    std::vector<Lamp> lamps(1);
    lamps[0].directional = true;
    const double l[3] = { -0.1, 1.0, -0.2 }, ll = std::sqrt(0.01 + 1.0 + 0.04);
    for (int a = 0; a < 3; ++a) lamps[0].toLight[a] = l[a] / ll;
    lamps[0].col.r = lamps[0].col.g = lamps[0].col.b = 1.0;
    const double four[4][3] = { { 0.707107, 0.707107, 0 }, { 0, 0.707107, 0.707107 }, { -0.707107, 0.707107, 0 },
                                { 0, 0.707107, -0.707107 } };
    for (int row = gRowFrom; row < 6; ++row) {
        const std::vector<LitCascade> ch = lampChain(heads, boxes, lamps, row);
        std::printf("   %-26s", kInjName[row]);
        for (int k = 0; k < 2; ++k) {
            const Cascade &c = ch[size_t(2 + k)].c;
            const LitCascade &lc = ch[size_t(2 + k)];
            double top = 0, under = 0, top0 = 0, under0 = 0;
            for (int z = 0; z < c.R[2]; ++z) for (int y = 0; y < c.R[1]; ++y) for (int x = 0; x < c.R[0]; ++x) {
                const double lo[3] = { c.origin[0] + x * c.cell[0], c.origin[1] + y * c.cell[1], c.origin[2] + z * c.cell[2] };
                if (lo[0] + c.cell[0] <= rooms[k] - 2.5 || lo[0] >= rooms[k] + 2.5 || lo[2] + c.cell[2] <= -2.5 || lo[2] >= 2.5) continue;
                if (lo[1] > 3.1 || lo[1] + c.cell[1] < 3.0) continue;
                const size_t i = (size_t(z) * c.R[1] + y) * c.R[0] + x;
                top += lc.rad0[kP][i * 9 + 3] * c.L[0].O[kP][i * 3 + 1];
                under += lc.rad0[kN][i * 9 + 3] * c.L[0].O[kN][i * 3 + 1];
                top0 += lc.rad0L[kP][i * 9 + 3] * c.L[0].O[kP][i * 3 + 1];
                under0 += lc.rad0L[kN][i * 9 + 3] * c.L[0].O[kN][i * 3 + 1];
            }
            double sum = 0, worst = 0;
            int cones = 0;
            for (double px : { -2.0, 0.0, 2.0 })
                for (double pz : { -2.0, 0.0, 2.0 }) {
                    const double pw[3] = { rooms[k] + px, 0.0, pz };
                    for (const auto &d : four) {
                        int lcn = 0;
                        const ResultC R = walkNC(ch, pw, 1, 1.0, d, 0.98269, lcn);
                        sum += R.col[1]; worst = std::max(worst, R.col[1]); ++cones;
                    }
                }
            std::printf(" | c%d under/top: dir-0 %.3f, lvl-0 %.3f; floor cones %.4f mean %.4f worst", 2 + k,
                        top > 0 ? under / top : 0.0, top0 > 0 ? under0 / top0 : 0.0, sum / cones, worst);
        }
        std::printf("\n");
        std::fflush(stdout);
    }
}

/// THE FLAT WALL lit by a sun behind it (its face dark, the 0.1 m floor lit): the column the
/// four cones read, per store.
static void armFusedFlat(int R)
{
    std::printf("== THE FUSED FORM: the flat wall over a sunlit 0.1 m floor, %d x %d x %d cubic cells\n", 2 * R, R, 2 * R);
    const std::vector<Box> scene = { { { -8.0, -0.1, -8.0 }, { 8.0, 0.0, 8.0 } }, { { -6.0, 0.0, -2.2 }, { 6.0, 4.0, -1.8 } } };
    std::vector<Lamp> lamps(1);
    lamps[0].directional = true;
    const double l[3] = { 0.0, 1.0, -0.4 }, ll = std::sqrt(1.16);
    for (int a = 0; a < 3; ++a) lamps[0].toLight[a] = l[a] / ll;
    lamps[0].col.r = lamps[0].col.g = lamps[0].col.b = 1.0;
    const double org[3] = { -9.0, -1.5, -9.0 }, size[3] = { 18.0, 9.0, 18.0 };
    const int Rv[3] = { 2 * R, R, 2 * R };
    const double ax[4][3] = { { 0.707107, 0, 0.707107 }, { 0, 0.707107, 0.707107 }, { -0.707107, 0, 0.707107 },
                              { 0, -0.707107, 0.707107 } };
    for (int row = gRowFrom; row < 6; ++row) {
        const int model = row <= 1 ? row : (row <= 3 ? kInjFused : (row == 4 ? kInjIdeal : kInjFusedGpu));
        std::vector<LitCascade> ch = { synthLamps(scene, lamps, 0.8, model, row == 3, org, size, Rv) };
        std::printf("   %-26s", kInjName[row]);
        for (double py : { 0.8, 1.6, 2.8 }) {
            const double pw[3] = { 0.0, py, -1.8 };
            double s = 0;
            for (const auto &d : ax) { int lc = 0; s += 0.25 * walkNC(ch, pw, 2, 1.0, d, 0.98269, lc).col[1]; }
            std::printf("  %.1f m: %.4f", py, s);
        }
        std::printf("\n");
    }
}


/// Where the fused store holds red its faces do not: per cascade of the leak room, every covered
/// half whose red exceeds the ideal's, with the world box of its voxel.
static void armFusedWhy(const std::string &dir, double T, int casc)
{
    std::vector<Cascade> heads = dumpHeads(dir);
    if (int(heads.size()) <= casc) return;
    const std::vector<Box> boxes = leakBoxes(T);
    const double ho = 5.0 + T * 0.5;
    std::vector<Lamp> lamps(2);
    lamps[0].pos[0] = 0; lamps[0].pos[1] = 3.0; lamps[0].pos[2] = 2.5; lamps[0].col.g = 3.0;
    lamps[1].pos[0] = 0; lamps[1].pos[1] = 2.0; lamps[1].pos[2] = -(ho + T * 0.5 + 1.0); lamps[1].col.r = 25.0;
    const Cascade &h = heads[size_t(casc)];
    const LitCascade fu = synthLamps(boxes, lamps, 0.8, kInjFused, false, h.origin, h.size, h.R);
    const LitCascade id = synthLamps(boxes, lamps, 0.8, kInjIdeal, false, h.origin, h.size, h.R);
    const Cascade &c = fu.c;
    const char *hn[2] = { "+", "-" }, *an = "xyz";
    int shown = 0;
    for (int z = 0; z < c.R[2]; ++z) for (int y = 0; y < c.R[1]; ++y) for (int x = 0; x < c.R[0]; ++x) {
        const size_t i = (size_t(z) * c.R[1] + y) * c.R[0] + x;
        for (int hh = 0; hh < 2; ++hh) for (int a = 0; a < 3; ++a) {
            const double o = c.L[0].O[hh][i * 3 + a];
            if (!(o > 0)) continue;
            const double rf = fu.rad0[hh][i * 9 + a * 3], ri = id.rad0[hh][i * 9 + a * 3];
            if (rf <= ri + 0.05) continue;
            if (shown++ < 60)
                std::printf("   c%d voxel [%.2f,%.2f]x[%.2f,%.2f]x[%.2f,%.2f] half %s%c O %.2f pos %.3f: fused r %.3f ideal r %.3f, "
                            "level-0 side r %.3f\n", casc, c.origin[0] + x * c.cell[0], c.origin[0] + (x + 1) * c.cell[0],
                            c.origin[1] + y * c.cell[1], c.origin[1] + (y + 1) * c.cell[1], c.origin[2] + z * c.cell[2],
                            c.origin[2] + (z + 1) * c.cell[2], hn[hh], an[a], o,
                            c.origin[a] + c.L[0].P[hh][i * 3 + a] / o * c.size[a], rf, ri, fu.rad0L[hh][i * 9 + a * 3]);
        }
    }
    std::printf("   %d halves\n", shown);
}


/// THE GREEN LAMP'S LIT HALVES, per store against the ideal: per cascade, the covered halves the
/// ideal lights (the lamp inside the leak room) that the store leaves dark (LOST - over-shadowed)
/// and the ones it lights that the ideal does not (GAINED - leaks).
static void armFusedLit(const std::string &dir, double T)
{
    const std::vector<Cascade> heads = dumpHeads(dir);
    const std::vector<Box> boxes = leakBoxes(T);
    std::vector<Lamp> lamps(1);
    lamps[0].pos[0] = 0; lamps[0].pos[1] = 3.0; lamps[0].pos[2] = 2.5; lamps[0].col.g = 3.0;
    std::printf("== the green lamp's lit halves (wall %.2f m): store against the ideal, per cascade\n", T);
    for (size_t k = 0; k < heads.size(); ++k) {
        const Cascade &h = heads[k];
        const LitCascade id = synthLamps(boxes, lamps, 0.8, kInjIdeal, false, h.origin, h.size, h.R);
        for (int model : { kInjOneLight, kInjFusedGpu }) {
            const LitCascade st = synthLamps(boxes, lamps, 0.8, model, false, h.origin, h.size, h.R);
            long lit = 0, lost = 0, gained = 0;
            const size_t n = size_t(h.R[0]) * h.R[1] * h.R[2];
            for (size_t i = 0; i < n; ++i)
                for (int hh = 0; hh < 2; ++hh) for (int a = 0; a < 3; ++a) {
                    if (!(st.c.L[0].O[hh][i * 3 + a] > 0)) continue;
                    const bool li = id.rad0[hh][i * 9 + a * 3 + 1] > 1e-6, ls = st.rad0[hh][i * 9 + a * 3 + 1] > 1e-6;
                    lit += li; lost += li && !ls; gained += ls && !li;
                }
            std::printf("   cascade %zu %-26s: the ideal lights %ld halves; LOST %ld, GAINED %ld\n", k,
                        model == kInjOneLight ? "one light (before)" : "FUSED, THE GPU PLAN", lit, lost, gained);
        }
    }
}

/// scripting.e2e.gi_voxel_store's ROOM (a closed 5 m cube of 0.2 m slabs, one point lamp at (0, 4, 0))
/// on the Epic chain's cascade 0 (128 cells over 10 m, centred on the camera (0, 2.5, 1.8) snapped to
/// the cell): the voxels its lit geometry PREDICTS lit - every voxel holding a face the lamp sees
/// (the ideal) - and each voxel's direct light relative to the brightest (the injection has no
/// distance falloff: albedo x cos), the input of the suite's above-1 count.
static void armStoreRoom()
{
    // THE ROOM AS THE SUITE BUILDS IT: the document's cube primitive is 2 x 2 x 2, so its scale
    // (5, 0.2, 5) makes a 10 x 0.4 x 10 slab - the six overlap into a closed room x, z in
    // [-2.3, 2.3], y in [0.2, 4.8] (node.size, measured) - not the 5 m cube its comment says.
    const double S = 5.0, T = 0.2, cell = 10.0 / 128.0;
    const auto box = [](double x, double y, double z, double sx, double sy, double sz) {
        return Box{ { x - sx, y - sy, z - sz }, { x + sx, y + sy, z + sz } };
    };
    const std::vector<Box> boxes = { box(0, 0, 0, S, T, S), box(0, S, 0, S, T, S), box(-S / 2, S / 2, 0, T, S, S),
                                     box(S / 2, S / 2, 0, T, S, S), box(0, S / 2, -S / 2, S, S, T), box(0, S / 2, S / 2, S, S, T) };
    const double cam[3] = { 0.0, 2.5, 1.8 };
    double org[3];
    for (int a = 0; a < 3; ++a) org[a] = std::round(cam[a] / cell) * cell - 5.0;
    const double size[3] = { 10.0, 10.0, 10.0 };
    const int R[3] = { 128, 128, 128 };
    std::vector<Lamp> lamps(1);
    lamps[0].pos[0] = 0; lamps[0].pos[1] = S - 1.0; lamps[0].pos[2] = 0; lamps[0].col.r = lamps[0].col.g = lamps[0].col.b = 1.0;
    std::printf("== scripting.e2e.gi_voxel_store's room on Epic's cascade 0 (cell %.4f m, origin %.4f %.4f %.4f)\n", cell,
                org[0], org[1], org[2]);
    for (int model : { kInjIdeal, kInjFusedGpu, kInjOneLight }) {
        const LitCascade lc = synthLamps(boxes, lamps, 0.8, model, false, org, size, R);
        const size_t n = size_t(R[0]) * R[1] * R[2];
        std::vector<double> lvl;
        long lit = 0;
        for (size_t i = 0; i < n; ++i) {
            double best = 0;
            for (int h = 0; h < 2; ++h) for (int a = 0; a < 3; ++a) best = std::max(best, double(lc.rad0L[h][i * 9 + a * 3 + 1]));
            // the level-0 total the GPU counts: the two sides' mean, premultiplied (li0 / c)
            const double mean = lc.c0[i] > 0 ? double(lc.Li[0][i * 4 + 1]) / double(lc.c0[i]) : 0.0;
            if (mean > 1e-6) { ++lit; lvl.push_back(mean); }
            (void)best;
        }
        // the voxels whose light hangs on a coverage within the store's quantum (1/1023) of zero:
        // the count's own uncertainty
        long nearQuantum = 0;
        for (size_t i = 0; i < n; ++i)
            if (lc.c0[i] > 0 && lc.c0[i] < 1.0 / 1023.0) ++nearQuantum;
        std::printf("   (voxels with a coverage under the store's quantum: %ld)\n", nearQuantum);
        std::sort(lvl.begin(), lvl.end());
        const double peak = lvl.empty() ? 0.0 : lvl.back();
        std::printf("   %-22s lit voxels %ld; direct / peak at the 50/90/99 %% points %.3f %.3f %.3f\n",
                    model == kInjIdeal ? "the ideal" : (model == kInjFusedGpu ? "FUSED, THE GPU PLAN" : "one light (before)"),
                    lit, lvl.empty() ? 0.0 : lvl[lvl.size() / 2] / peak, lvl.empty() ? 0.0 : lvl[lvl.size() * 9 / 10] / peak,
                    lvl.empty() ? 0.0 : lvl[lvl.size() * 99 / 100] / peak);
        for (double th : { 0.60, 0.65, 0.70, 0.75, 0.80 }) {
            long above = 0;
            for (double v : lvl) above += v / peak > th;
            std::printf("      direct above %.2f of the peak: %ld voxels\n", th, above);
        }
    }
}
int main(int argc, char **argv)
{
    if (std::getenv("LAB_ROWS_FROM")) gRowFrom = std::atoi(std::getenv("LAB_ROWS_FROM"));
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
        else if (arm == "leak" && argc > 2) armLeak(argv[2], argc > 3 ? std::atof(argv[3]) : 0.5);
        else if (arm == "thin" && argc > 2) armThin(argv[2], argc > 3 ? std::atof(argv[3]) : 0.0);
        else if (arm == "roof" && argc > 2) armRoof(argv[2]);
        else if (arm == "flatwall") armFlatWall(argc > 2 ? std::atoi(argv[2]) : 32);
        else if (arm == "fusedwhy" && argc > 4) { gMarchDDA = gStartOnFace = argc <= 5; armFusedWhy(argv[2], std::atof(argv[3]), std::atoi(argv[4])); }
        else if (arm == "fusedlit" && argc > 2) armFusedLit(argv[2], argc > 3 ? std::atof(argv[3]) : 0.5);
        else if (arm == "storeroom") armStoreRoom();
        else if (arm == "fused" && argc > 2) armFused(argv[2], argc > 3 ? std::atof(argv[3]) : 0.0, argc > 4 ? std::atof(argv[4]) : 0.0);
        else if (arm == "fusedroof" && argc > 2) armFusedRoof(argv[2]);
        else if (arm == "fusedflat") armFusedFlat(argc > 2 ? std::atoi(argv[2]) : 32);
        else if (arm.size() > 7 && arm.compare(arm.size() - 7, 7, "stepped") == 0) {
            gMarchDDA = gStartOnFace = false;   // the RETIRED march, the record
            if (arm == "fusedstepped" && argc > 2) armFused(argv[2], argc > 3 ? std::atof(argv[3]) : 0.0, argc > 4 ? std::atof(argv[4]) : 0.0);
            else if (arm == "fusedroofstepped" && argc > 2) armFusedRoof(argv[2]);
            else if (arm == "fusedflatstepped") armFusedFlat(argc > 2 ? std::atoi(argv[2]) : 32);
            else { std::printf("usage: %s fusedstepped DIR [dz] [T] | fusedroofstepped DIR | fusedflatstepped [R]\n", argv[0]); return 2; }
        }
        else { std::printf("usage: %s [sealed|sets|split|flatwall R | validate|thin|roof DIR | leak DIR [T]]\n", argv[0]); return 2; }
    }
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
