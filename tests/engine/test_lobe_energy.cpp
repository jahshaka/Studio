// engine.lobe_energy (MATERIAL-ENERGY-1 stage 1) - THE LOBE RETURNS NO MORE LIGHT THAN IT RECEIVES. CPU only: the fork's
// shipped JahBrdf / JahDiffuseAlbedo / JahSpecularAlbedo text COMPILED as C++ (tests/support/glsl_as_cpp.h, through the
// engine's own unwrap step), the table it reads #included (irisgl PhotonSpecularAlbedo.h); tests/support/lobe_energy.h.
//
// THE SHIM'S PROOF: the compiled shipped text reproduces the ground read's independent evaluation of the same text
// (spikes/material-energy-1/albedo_at_b92b60013.py, python, the table parsed from the file): today's total albedo
// rho_d DA( mu, r ) + F0 A + B at rho_d 1, F0 0.04, kS 1, r 1 = 1.1155 at mu 0, 1.1138 at mu 0.001, 0.698 head-on.
#include <cmath>
#include <cstdio>

#include "lobe_energy.h"

using namespace lobeenergy;

static int gFail = 0;
static void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++gFail;
}

int main()
{
    const Channel plaster{ 1.0, 0.04, 1.0 };
    const double t0 = todayAlbedo(0.0, 1.0) + es(plaster, 0.0, 1.0);
    const double t1 = todayAlbedo(0.001, 1.0) + es(plaster, 0.001, 1.0);
    const double tn = todayAlbedo(1.0, 1.0) + es(plaster, 1.0, 1.0);
    std::printf("-- the shipped text, compiled: today's total albedo at rho_d 1, F0 0.04, r 1: %.4f at mu 0, %.4f at mu 0.001, "
                "%.4f head-on\n", t0, t1, tn);
    check(std::fabs(t0 - 1.1155) < 5e-5 && std::fabs(t1 - 1.1138) < 5e-5 && std::fabs(tn - 0.698) < 5e-4,
          "the compiled shipped text reproduces the ground read's python evaluation (1.1155 / 1.1138 / 0.698)");
    std::printf("%s\n", gFail ? "engine.lobe_energy: FAILED" : "engine.lobe_energy: PASSED");
    return gFail ? 1 : 0;
}
