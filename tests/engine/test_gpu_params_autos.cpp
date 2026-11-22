// engine.gpu_params_autos (SHADER-SYSTEM-1 7a, R3; CPU, no Root, no card) - A PROGRAM's AUTO CONSTANTS SURVIVE A COPY BY
// NAME. GpuProgramParameters::copyMatchingNamedConstantsFrom is how Ogre carries parameters across programs: a
// program reload's GpuProgramUsage::recreateParameters, a high-level program's reloaded defaults, and the engine's
// program twins (chain::programTwin: an SMAA preset's set, a stereo quad's /Multiview). An auto constant bound to a name
// of the shader's own (SMAA.material's `param_named_auto viewportSize viewport_size`) was DROPPED by it: the fork's
// filter looked the PARAMETER's name up in the auto TYPE dictionary (upstream a7480a325), so a twin drew with
// SMAA_RT_METRICS and worldViewProj at zero (engine.smaa_preset_picture red: every preset one picture, 2976 px from off).
// THE LAWS: (1) every auto of the source is bound in the destination at the destination's own slot of the same name,
// with its type and extra data, the destination's layout differing from the source's; (2) a float auto is never bound
// through a constant of another type that shares its physical index (an int sampler at float auto's index: the
// collision upstream's filter was written against); (3) a plain value is copied.
// RED before the fork fix (c1412ac93): (1) reads 0 autos bound.
#include <OgreGpuProgramParams.h>

#include <cstdio>
#include <string>

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } } while (0)

namespace {

void add(Ogre::GpuNamedConstants &nc, const std::string &name, Ogre::GpuConstantType type, size_t physical,
         size_t elementSize) {
    Ogre::GpuConstantDefinition d;
    d.constType = type;
    d.physicalIndex = physical;
    d.logicalIndex = physical;
    d.elementSize = elementSize;
    d.arraySize = 1;
    nc.map[name] = d;
}

/// A layout: a matrix, a float4 and an int sampler, the float slots in the order given.
Ogre::GpuNamedConstantsPtr layout(bool matrixFirst) {
    Ogre::GpuNamedConstantsPtr nc(OGRE_NEW Ogre::GpuNamedConstants());
    add(*nc, "worldViewProj", Ogre::GCT_MATRIX_4X4, matrixFirst ? 0 : 4, 16);
    add(*nc, "viewportSize", Ogre::GCT_FLOAT4, matrixFirst ? 16 : 0, 4);
    add(*nc, "rt_input", Ogre::GCT_SAMPLER2D, 0, 1);   // the int buffer's index 0: a float auto's index in one layout
    nc->floatBufferSize = 20;
    nc->intBufferSize = 1;
    return nc;
}

}   // namespace

int main() {
    Ogre::GpuProgramParameters src;
    src._setNamedConstants(layout(false));
    src.setNamedAutoConstant("worldViewProj", Ogre::GpuProgramParameters::ACT_WORLDVIEWPROJ_MATRIX);
    src.setNamedAutoConstant("viewportSize", Ogre::GpuProgramParameters::ACT_VIEWPORT_SIZE);
    src.setNamedConstant("rt_input", 3);

    Ogre::GpuProgramParameters dst;
    dst._setNamedConstants(layout(true));
    dst.copyMatchingNamedConstantsFrom(src);

    const Ogre::GpuProgramParameters_AutoConstantEntry *vp = dst.findAutoConstantEntry("viewportSize");
    const Ogre::GpuProgramParameters_AutoConstantEntry *wvp = dst.findAutoConstantEntry("worldViewProj");
    CHECK(dst.getAutoConstantCount() == 2, "(1) the destination binds the source's 2 autos (it binds %zu)",
          dst.getAutoConstantCount());
    CHECK(vp && vp->paramType == Ogre::GpuProgramParameters::ACT_VIEWPORT_SIZE && vp->physicalIndex == 16,
          "(1) viewportSize is viewport_size at the destination's own slot 16 (%s)",
          vp ? std::to_string(vp->physicalIndex).c_str() : "unbound");
    CHECK(wvp && wvp->paramType == Ogre::GpuProgramParameters::ACT_WORLDVIEWPROJ_MATRIX && wvp->physicalIndex == 0,
          "(1) worldViewProj is worldviewproj_matrix at the destination's own slot 0 (%s)",
          wvp ? std::to_string(wvp->physicalIndex).c_str() : "unbound");
    // (2) the source's viewportSize auto sits at FLOAT index 0, and rt_input at INT index 0: the sampler never takes it.
    const Ogre::GpuProgramParameters_AutoConstantEntry *rt = dst.findAutoConstantEntry("rt_input");
    CHECK(!rt, "(2) the int sampler sharing the float auto's physical index carries no auto");
    // (3)
    CHECK(*dst.getIntPointer(0) == 3, "(3) the sampler's value is copied (%d)", *dst.getIntPointer(0));

    std::printf(gFailures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", gFailures);
    return gFailures ? 1 : 0;
}
