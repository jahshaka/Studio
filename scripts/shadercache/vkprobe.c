// vkprobe — LD_PRELOAD interposer that times vkCreateShaderModule and
// vkCreateGraphicsPipelines/vkCreateComputePipelines. Writes a summary to
// the file named by SC_VKPROBE_OUT at exit.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

typedef int (*fn_t)();
static double g_smNs, g_gpNs, g_cpNs;
static long   g_smN,  g_gpN,  g_cpN;

static double now_ns(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e9+t.tv_nsec; }

static void *real(const char *n){ static void *h; if(!h) h = dlopen("libvulkan.so.1", RTLD_LAZY|RTLD_LOCAL);
    void *p = dlsym(RTLD_NEXT, n); if(!p && h) p = dlsym(h, n); return p; }

int vkCreateShaderModule(void *d, const void *ci, const void *a, void *out){
    static fn_t f; if(!f) f = (fn_t)real("vkCreateShaderModule");
    double t0 = now_ns(); int r = ((int(*)(void*,const void*,const void*,void*))f)(d,ci,a,out);
    g_smNs += now_ns()-t0; g_smN++; return r; }

int vkCreateGraphicsPipelines(void *d, void *c, uint32_t n, const void *ci, const void *a, void *out){
    static fn_t f; if(!f) f = (fn_t)real("vkCreateGraphicsPipelines");
    double t0 = now_ns();
    int r = ((int(*)(void*,void*,uint32_t,const void*,const void*,void*))f)(d,c,n,ci,a,out);
    g_gpNs += now_ns()-t0; g_gpN += n; return r; }

int vkCreateComputePipelines(void *d, void *c, uint32_t n, const void *ci, const void *a, void *out){
    static fn_t f; if(!f) f = (fn_t)real("vkCreateComputePipelines");
    double t0 = now_ns();
    int r = ((int(*)(void*,void*,uint32_t,const void*,const void*,void*))f)(d,c,n,ci,a,out);
    g_cpNs += now_ns()-t0; g_cpN += n; return r; }

__attribute__((destructor)) static void dump(void){
    const char *p = getenv("SC_VKPROBE_OUT"); FILE *f = p ? fopen(p,"w") : stderr; if(!f) f = stderr;
    fprintf(f, "vkCreateShaderModule       calls=%ld  ms=%.1f\n", g_smN, g_smNs/1e6);
    fprintf(f, "vkCreateGraphicsPipelines  psos=%ld  ms=%.1f\n", g_gpN, g_gpNs/1e6);
    fprintf(f, "vkCreateComputePipelines   psos=%ld  ms=%.1f\n", g_cpN, g_cpNs/1e6);
    if(f!=stderr) fclose(f); }
