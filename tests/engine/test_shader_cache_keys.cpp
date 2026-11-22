// engine.shader_cache_keys (SHADER-SYSTEM-1 stage 5; CPU, no engine, no card) - ONLY A PRODUCER DROPS A LAYER. The shader
// cache's four layer keys are one function of their inputs (irisgl engine/src/ShaderCacheKeys.h, composeLayerKeys). The
// laws:
//   (1) THE PIPELINE KEY's TEXT IS BYTE-IDENTICAL to the key it replaced (the start preflight's F5: no kCacheFormat bump,
//       the driver-validated pipeline cache survives the landing) - the golden case, against today's composition restated
//       below from the code it replaced (OgreShaderCache.cpp configure / load at irisgl 3e2e28cb);
//   (2) EACH KEY MOVES ON EACH OF ITS PRODUCER TERMS, and on no other key's: the table below, every term against every key;
//   (3) NO KEY MOVES ON THE FORK's COMMIT OR A MEDIA BYTE: neither is an input any more. NEGATIVE CONTROL: today's
//       composition moves the microcode, hlms and compute keys on a fork commit and the hlms key on a media byte;
//   (4) a changed PREPROCESSOR moves the compute key and NOT the hlms key - the hlms layer's guard is Ogre's re-parse under
//       the salted template checksum (ruling 2; engine.template_checksum_salt), never a drop.
#include "ShaderCacheKeys.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace jahshaka::engine::detail;

static int gFailures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++gFailures; } } while (0)

namespace {

LayerKeyInputs base() {
    LayerKeyInputs in;
    in.format = 2;
    in.code = "c0de";
    in.microcodePath = "m1c0";
    in.preprocessorPath = "9e9e";
    in.computePath = "c0c0";
    in.glslang = "16.0.0";
    in.debugBuild = true;
    in.dbgstr = "32";
    in.hashBits = 32;
    in.haveGpu = true;
    in.rs = "Vulkan Rendering Subsystem";
    in.vendor = "1";
    in.device = "NVIDIA GeForce RTX 5060 Ti";
    in.driver = "615.71.9.0";
    return in;
}

/// TODAY's composition (OgreShaderCache.cpp at irisgl 3e2e28cb), restated: the fork's commit in three keys, the staged
/// Hlms and Photon trees' hashes in the hlms key.
LayerKeys todays(const LayerKeyInputs &in, const std::string &forkCommit, const std::string &mediaHash,
                 const std::string &photonHash) {
    const std::string format = "format=" + std::to_string(in.format);
    const std::string build = in.debugBuild ? "|build=debug" : "|build=release";
    const std::string g = "|rs=" + in.rs + "|vendor=" + in.vendor + "|device=" + in.device + "|driver=" + in.driver;
    LayerKeys k;
    k.pipeline = format + g;
    k.microcode = format + "|ogre=" + forkCommit + "|glslang=" + in.glslang + build + "|rs=" + in.rs;
    k.hlms = format + "|code=" + in.code + "|ogre=" + forkCommit + "|media=" + mediaHash + "|photon=" + photonHash +
             "|dbgstr=" + in.dbgstr + "|hashbits=" + std::to_string(in.hashBits) + build + g;
    k.compute = format + "|code=" + in.code + "|ogre=" + forkCommit + "|glslang=" + in.glslang + build + "|rs=" + in.rs;
    return k;
}

struct Moved { bool pipeline, microcode, hlms, compute; };
Moved moved(const LayerKeys &a, const LayerKeys &b) {
    return { a.pipeline != b.pipeline, a.microcode != b.microcode, a.hlms != b.hlms, a.compute != b.compute };
}

}  // namespace

int main() {
    const LayerKeys k0 = composeLayerKeys(base());

    // (1) THE GOLDEN CASE
    const LayerKeys t0 = todays(base(), "d1436214f662e6ba6b5ceb578b9d468c05016fd0", "aa", "bb");
    CHECK(k0.pipeline == t0.pipeline && k0.pipeline == "format=2|rs=Vulkan Rendering Subsystem|vendor=1|"
                                                       "device=NVIDIA GeForce RTX 5060 Ti|driver=615.71.9.0",
          "(1) the pipeline key's text is byte-identical to today's: %s", k0.pipeline.c_str());
    LayerKeyInputs noGpu = base();
    noGpu.haveGpu = false;
    CHECK(composeLayerKeys(noGpu).pipeline == "format=2",
          "(1) ...and before the device it is today's \"format=2\" (configure's key, load adds the GPU)");

    // (2) EACH TERM, EACH KEY: {name, the edit, which keys must move}
    struct Term { const char *name; std::function<void(LayerKeyInputs &)> edit; Moved expect; };
    const std::vector<Term> terms = {
        { "format",       [](LayerKeyInputs &i) { i.format = 3; },                { true, true, true, true } },
        { "code",         [](LayerKeyInputs &i) { i.code += "x"; },               { false, false, true, true } },
        { "microcode",    [](LayerKeyInputs &i) { i.microcodePath += "x"; },      { false, true, false, false } },
        { "preprocessor", [](LayerKeyInputs &i) { i.preprocessorPath += "x"; },   { false, false, false, true } },
        { "compute",      [](LayerKeyInputs &i) { i.computePath += "x"; },        { false, false, false, true } },
        { "glslang",      [](LayerKeyInputs &i) { i.glslang = "16.1.0"; },        { false, true, false, true } },
        { "build",        [](LayerKeyInputs &i) { i.debugBuild = false; },        { false, true, true, true } },
        { "dbgstr",       [](LayerKeyInputs &i) { i.dbgstr = "0"; },              { false, false, true, false } },
        { "hashbits",     [](LayerKeyInputs &i) { i.hashBits = 64; },             { false, false, true, false } },
        { "rs",           [](LayerKeyInputs &i) { i.rs = "Metal Rendering Subsystem"; }, { true, true, true, true } },
        { "vendor",       [](LayerKeyInputs &i) { i.vendor = "2"; },              { true, false, true, false } },
        { "device",       [](LayerKeyInputs &i) { i.device += "x"; },             { true, false, true, false } },
        { "driver",       [](LayerKeyInputs &i) { i.driver = "616.0.0.0"; },      { true, false, true, false } },
    };
    for (const Term &t : terms) {
        LayerKeyInputs in = base();
        t.edit(in);
        const Moved m = moved(k0, composeLayerKeys(in));
        const bool ok = m.pipeline == t.expect.pipeline && m.microcode == t.expect.microcode && m.hlms == t.expect.hlms &&
                        m.compute == t.expect.compute;
        CHECK(ok, "(2) the %s term moves pipeline %d microcode %d hlms %d compute %d (expected %d %d %d %d)", t.name,
              m.pipeline, m.microcode, m.hlms, m.compute, t.expect.pipeline, t.expect.microcode, t.expect.hlms,
              t.expect.compute);
    }

    // (3) THE FORK's COMMIT AND A MEDIA BYTE: no input of the new keys; today's moved on both (the NEGATIVE CONTROL)
    const Moved fork = moved(t0, todays(base(), "6828d2d78000000000000000000000000000000", "aa", "bb"));
    const Moved media = moved(t0, todays(base(), "d1436214f662e6ba6b5ceb578b9d468c05016fd0", "ab", "bb"));
    const Moved photon = moved(t0, todays(base(), "d1436214f662e6ba6b5ceb578b9d468c05016fd0", "aa", "bc"));
    CHECK(fork.microcode && fork.hlms && fork.compute && !fork.pipeline && media.hlms && photon.hlms,
          "(3) NEGATIVE CONTROL: today's composition drops microcode, hlms and compute on a fork commit and hlms on a "
          "media or a Photon byte");
    const std::string all = k0.pipeline + k0.microcode + k0.hlms + k0.compute;
    CHECK(all.find("ogre=") == std::string::npos && all.find("media=") == std::string::npos &&
              all.find("photon=") == std::string::npos,
          "(3) no key holds a fork-commit, media or Photon term: neither moves any layer now");

    // (4) THE PREPROCESSOR
    LayerKeyInputs pre = base();
    pre.preprocessorPath = "other";
    const Moved p = moved(k0, composeLayerKeys(pre));
    CHECK(p.compute && !p.hlms, "(4) a changed preprocessor drops the compute records and NOT the recipes (Ogre "
                                "re-parses them under the salted checksum)");

    std::printf(gFailures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", gFailures);
    return gFailures ? 1 : 0;
}
