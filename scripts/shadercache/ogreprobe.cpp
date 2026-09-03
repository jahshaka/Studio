// ogreprobe — LD_PRELOAD attribution probe for the shader-cache benchmark.
//
// Times the Ogre-side halves of a cold start by PLT interposition:
//   Ogre::VulkanProgram::compile      GLSL parse + glslang + SPIR-V reflect
//   glslang::GlslangToSpv             the GLSL->SPIR-V step alone
//   Ogre::Hlms::createShaderCacheEntry one Hlms shader end to end
//   Ogre::Hlms::compileShaderCode     Hlms template preprocessing + compile
//   ResourceGroupManager::initialiseAllResourceGroups   script parsing
// plus the wall the probe sees and when the first/last compile happened.
//
// Read scripts/shadercache/README.md before touching this: the RTLD_LOCAL trap
// documented there is why the naive version of this file segfaults.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

// RenderSystem_Vulkan.so is dlopen'd RTLD_LOCAL, so dlsym(RTLD_NEXT, ...) cannot
// see its symbols even though our PLT interposition of them DOES take effect
// (global scope wins during its relocation). Resolve "the real one" out of the
// object itself instead. This is why the naive probe segfaults on a null next.
static void *rsHandle() {
    static void *h = nullptr; static bool tried = false;
    if (!tried) { tried = true;
        const char *env = getenv("SC_VULKAN_RS_SO");
        h = dlopen(env ? env : "RenderSystem_Vulkan.so", RTLD_LAZY | RTLD_NOLOAD | RTLD_LOCAL);
    }
    return h;
}
static void *realSym(const char *n) {
    void *p = dlsym(RTLD_NEXT, n);
    if (!p && rsHandle()) p = dlsym(rsHandle(), n);
    return p;
}

static double nowNs(){ timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e9+t.tv_nsec; }
static double gCompileNs, gSpvNs; static long gCompileN, gSpvN;
static double gLoadT, gFirstCompileT, gLastCompileT;

extern "C" {

typedef void (*compile_t)(void*, bool, bool);
void _ZN4Ogre13VulkanProgram7compileEbb(void *self, bool a, bool b) {
    static compile_t next;
    if (!next) next = (compile_t)realSym("_ZN4Ogre13VulkanProgram7compileEbb");
    if (!next) { return; }
    double t0 = nowNs(); if(!gFirstCompileT) gFirstCompileT=t0; next(self, a, b); gLastCompileT=nowNs(); gCompileNs += gLastCompileT-t0; ++gCompileN;
}

typedef void (*spv_t)(const void*, void*, void*, void*);
void _ZN7glslang12GlslangToSpvERKNS_13TIntermediateERSt6vectorIjSaIjEEPN3spv14SpvBuildLoggerEPNS_10SpvOptionsE(
        const void *im, void *sp, void *lg, void *op) {
    static spv_t next;
    if (!next) next = (spv_t)realSym(
        "_ZN7glslang12GlslangToSpvERKNS_13TIntermediateERSt6vectorIjSaIjEEPN3spv14SpvBuildLoggerEPNS_10SpvOptionsE");
    if (!next) { return; }
    double t0 = nowNs(); next(im, sp, lg, op); gSpvNs += nowNs()-t0; ++gSpvN;
}


// --- Hlms side (libOgreNextMain.so is in the global scope: RTLD_NEXT works) ---
static double gEntryNs, gCodeNs; static long gEntryN, gCodeN;

typedef void *(*entry_t)(void*, uint32_t, const void*, uint32_t, const void*, void*, size_t, size_t);
void *_ZN4Ogre4Hlms22createShaderCacheEntryEjRKNS_9HlmsCacheEjRKNS_16QueuedRenderableEPS1_mm(
        void *self, uint32_t a, const void *b, uint32_t c, const void *d, void *e, size_t f, size_t g) {
    static entry_t next;
    if (!next) next = (entry_t)realSym("_ZN4Ogre4Hlms22createShaderCacheEntryEjRKNS_9HlmsCacheEjRKNS_16QueuedRenderableEPS1_mm");
    if (!next) return 0;
    double t0 = nowNs(); void *r = next(self,a,b,c,d,e,f,g); gEntryNs += nowNs()-t0; ++gEntryN; return r;
}

typedef void (*code_t)(void*, void*, uint32_t, size_t);
void _ZN4Ogre4Hlms17compileShaderCodeERNS0_15ShaderCodeCacheEjm(void *self, void *a, uint32_t b, size_t c) {
    static code_t next;
    if (!next) next = (code_t)realSym("_ZN4Ogre4Hlms17compileShaderCodeERNS0_15ShaderCodeCacheEjm");
    if (!next) return;
    double t0 = nowNs(); next(self,a,b,c); gCodeNs += nowNs()-t0; ++gCodeN;
}

__attribute__((destructor)) static void dump(){
    const char *p = getenv("SC_GLSLPROBE_OUT"); FILE *f = p ? fopen(p,"w") : stderr; if(!f) f=stderr;
    fprintf(f, "VulkanProgram::compile  calls=%ld  ms=%.1f\n", gCompileN, gCompileNs/1e6);
    fprintf(f, "glslang::GlslangToSpv   calls=%ld  ms=%.1f\n", gSpvN, gSpvNs/1e6);
    fprintf(f, "Hlms::createShaderCacheEntry calls=%ld  ms=%.1f   (OUTER: includes template parse + compile)\n", gEntryN, gEntryNs/1e6);
    fprintf(f, "Hlms::compileShaderCode      calls=%ld  ms=%.1f   (template->GLSL->SPIRV, inside the above)\n", gCodeN, gCodeNs/1e6);
    { double t=nowNs(); fprintf(f, "probe-load -> exit  ms=%.1f  (first compile at +%.1f, last at +%.1f)\n",
        (t-gLoadT)/1e6, (gFirstCompileT?gFirstCompileT-gLoadT:0)/1e6, (gLastCompileT?gLastCompileT-gLoadT:0)/1e6); }
    if(f!=stderr) fclose(f);
}
}

// --- coarse phase markers -------------------------------------------------
extern "C" {
__attribute__((constructor(101))) static void mark_load(){ gLoadT = nowNs(); }

typedef void (*rgm_t)(void*, bool);
void _ZN4Ogre20ResourceGroupManager27initialiseAllResourceGroupsEb(void *self, bool b){
    static rgm_t next; static double ns; static long n;
    if(!next) next = (rgm_t)realSym("_ZN4Ogre20ResourceGroupManager27initialiseAllResourceGroupsEb");
    if(!next) return;
    double t0=nowNs(); next(self,b); ns += nowNs()-t0; ++n;
    const char *p = getenv("SC_GLSLPROBE_OUT2"); FILE *f = p?fopen(p,"a"):0;
    if(f){ fprintf(f,"initialiseAllResourceGroups calls=%ld ms=%.1f\n", n, ns/1e6); fclose(f);} }
}
