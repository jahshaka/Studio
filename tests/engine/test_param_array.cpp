// engine.param_array (NONDET-1 D2) - AN ARRAY PARAMETER OF OURS LONGER THAN 16 ELEMENTS REACHES ITS SHADER WHOLE
// (irisgl/engine/src/photon/voxel/PhotonParamArray.h, the one door for it). A real Ogre ShaderParams::Param, no device.
// LAW: PhotonParamArray::set( param, 64 values ) leaves the parameter holding all 64 (256 bytes, the values themselves,
// through setManualValueEx onto the block's own storage); the block copies, so the caller's array may die; more values
// than the block holds are REFUSED, never cut. RED before: the calls were setManualValue, which keeps 64 bytes and drops
// the rest silently (OgreShaderParams.cpp:250) - shown below on the same Param, the negative control.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "photon/voxel/PhotonParamArray.h"

static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }

int main()
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    Ogre::ShaderParams::Param param;
    param.name = "injectBoxLo";
    Ogre::PhotonParamArray<Ogre::uint32, 64u> block;
    {
        std::vector<Ogre::uint32> values(64u);   // (dies before the reads below: the block copied it)
        for (Ogre::uint32 i = 0; i < 64u; ++i) values[i] = 1000u + i;
        block.set(&param, values.data(), 64u);
    }
    bool same = param.isEx && param.isDirty && param.mpEx.dataSizeBytes == 256u && param.mpEx.dataBytes;
    const Ogre::uint32 *held = static_cast<const Ogre::uint32 *>(param.mpEx.dataBytes);
    for (Ogre::uint32 i = 0; same && i < 64u; ++i) same = held[i] == 1000u + i;
    std::printf("held: isEx %d, %u bytes, [0] %u, [16] %u, [63] %u\n", int(param.isEx), unsigned(param.mpEx.dataSizeBytes),
                held ? held[0] : 0u, held ? held[16] : 0u, held ? held[63] : 0u);
    check(same, "SIXTY-FOUR VALUES SET ARE SIXTY-FOUR HELD: 256 bytes, each the value set, after the caller's array died");
    // the last values set win (the shared jobs' semantics: every dispatch sets its arrays first)
    std::vector<Ogre::uint32> again(20u, 7u);
    block.set(&param, again.data(), 20u);
    check(param.mpEx.dataSizeBytes == 80u && static_cast<const Ogre::uint32 *>(param.mpEx.dataBytes)[19] == 7u,
          "a later set of 20 values holds those 20 (80 bytes)");
    // more than the block holds: refused, loudly
    bool refused = false;
    std::vector<Ogre::uint32> tooMany(65u, 1u);
    try { block.set(&param, tooMany.data(), 65u); } catch (const Ogre::Exception &) { refused = true; }
    check(refused, "65 values into a block of 64: REFUSED (an exception), never cut");
    // NEGATIVE CONTROL: the base's call, setManualValue, on the same kind of Param keeps 16 of the 64 values
    Ogre::ShaderParams::Param base;
    base.name = "injectBoxLo";
    std::vector<Ogre::uint32> values(64u, 5u);
    base.setManualValue(values.data(), 64u);
    std::printf("negative control: setManualValue( 64 values ) holds %u bytes\n", unsigned(base.mp.dataSizeBytes));
    check(!base.isEx && base.mp.dataSizeBytes == 64u, "negative control: setManualValue keeps 64 bytes of the 256 (the defect)");
    std::printf("%s\n", gFail ? "engine.param_array: FAILED" : "engine.param_array: PASSED");
    return gFail ? 1 : 0;
}
