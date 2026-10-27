// jahlab — THE CATALOGUE (LAB-1 stage 2): every environment, in the spec's order. `jahlab --env-list` prints it.
#include "env.h"

namespace lab {
namespace envs {

Env atomStreet();
Env atomArcade();
Env atomForest();
Env atomOcclusion();
Env atomSmall();
Env atomFlat();
Env photonRoom();
Env photonCourt();
Env photonGlass();
Env photonRamp();
Env photonShells();
Env photonRoof();
Env photonRough();
Env photonFirefly();
Env photonGlint();
Env photonArea();
Env photonContact();

const std::vector<Env> &catalogue()
{
    static const std::vector<Env> c = { atomStreet(), atomArcade(), atomForest(), atomOcclusion(),
                                        atomSmall(),  atomFlat(),   photonRoom(), photonCourt(),
                                        photonGlass(), photonRamp(), photonShells(), photonRoof(), photonRough(),
                                        photonFirefly(), photonGlint(),
                                        photonArea(), photonContact() };
    return c;
}

}   // namespace envs
}   // namespace lab
