// engine.ray_view_cache — THE RAY TIER's VIEW CACHE, as a pure structure (GATHER-HANG-1 stage 1;
// irisgl/engine/src/RayQueryViewCache.h says the law). No device, no Ogre: make, retire and watch are this file's
// counters, and the watch counter behaves as Ogre's TextureGpu listener list does (addListener does not de-duplicate,
// removeListener of an absent listener asserts, the destructor asserts on a listener left).
// LAW: one view per (texture, kind) on one image with one set of parameters, made once; retired exactly once when its
// image goes (LostResidency, Deleted, a lookup on another image or with other parameters, close); never a view of
// image 0; a failed make caches and registers nothing and a failed re-make keeps the old view; one registration per
// texture, gone at Deleted and at close.
#include <cstdio>
#include <map>
#include <set>

#ifndef JAH_RAY_VIEW_CACHE_UNDER_TEST   // (spikes/gather-hang-1/stage1 runs these cases on stage 0's arm J1 as the red)
#include "RayQueryViewCache.h"
#define JAH_RAY_VIEW_CACHE_UNDER_TEST jahshaka::engine::detail::RayQueryViewCache
#endif

using jahshaka::engine::detail::RayViewKey;
using jahshaka::engine::detail::RayViewKind;
using Cache = JAH_RAY_VIEW_CACHE_UNDER_TEST;

static int gFail = 0;
static void check(bool ok, const char *what) { std::printf("%s: %s\n", ok ? "ok" : "FAIL", what); if (!ok) ++gFail; }

namespace {
/// The world the cache is injected into: every make, every retire, every registration counted.
struct World {
    uint64_t next = 0x1000u;
    int makes = 0;
    std::map<uint64_t, int> retired;          // view -> times retired (a second retire is a double destroy)
    std::map<const void *, int> listeners;    // texture -> registrations now (Ogre's vector, no de-dup)
    int maxListeners = 0, badRemoves = 0, watchOns = 0, watchOffs = 0;
    bool failMake = false;   // the next makes fail (a driver refusal: _createView / vkCreateImageView gave no view)
    Cache cache{ [this](const RayViewKey &, uint64_t) -> uint64_t { ++makes; return failMake ? 0u : ++next; },
                 [this](uint64_t v) { ++retired[v]; },
                 [this](const void *t, bool on) {
                     if (on) {
                         ++watchOns;
                         const int n = ++listeners[t];
                         if (n > maxListeners) maxListeners = n;
                     } else {
                         ++watchOffs;
                         if (listeners[t] <= 0) ++badRemoves; else --listeners[t];
                     }
                 } };
    int retiredTotal() const { int n = 0; for (const auto &kv : retired) n += kv.second; return n; }
    bool eachRetiredOnce() const { for (const auto &kv : retired) if (kv.second != 1) return false; return true; }
    int registrations() const { int n = 0; for (const auto &kv : listeners) n += kv.second; return n; }
};
RayViewKey key(const void *t, RayViewKind kind) {
    RayViewKey k;
    k.texture = t;
    k.kind = kind;
    k.format = 28u;
    if (kind == RayViewKind::Storage || kind == RayViewKind::LayerStorage) k.mipCount = 1u;
    if (kind == RayViewKind::LayerSampled || kind == RayViewKind::LayerStorage) k.layerCount = 2u;
    return k;
}
int texA = 0, texB = 0;   // two textures' addresses
const void *const A = &texA, *const B = &texB;
}   // namespace

int main()
{
    {   // 1. a key asked twice: one make, the same handle
        World w;
        const uint64_t v1 = w.cache.view(key(A, RayViewKind::Sampled), 0xA1u);
        const uint64_t v2 = w.cache.view(key(A, RayViewKind::Sampled), 0xA1u);
        check(v1 && v1 == v2 && w.makes == 1, "1. a key asked twice on one image: one make, the same view");
    }
    {   // 2. two kinds of one texture on one image do not collide
        World w;
        const uint64_t s = w.cache.view(key(A, RayViewKind::Sampled), 0xA1u);
        const uint64_t u = w.cache.view(key(A, RayViewKind::Storage), 0xA1u);
        check(s && u && s != u && w.makes == 2, "2. a sampled and a storage view of one texture: two makes, two views");
    }
    {   // 3. LostResidency: each view of the texture retired exactly once; a second Lost retires nothing; still watched
        World w;
        w.cache.view(key(A, RayViewKind::Sampled), 0xA1u);
        w.cache.view(key(A, RayViewKind::LayerSampled), 0xA1u);
        w.cache.view(key(B, RayViewKind::Sampled), 0xB1u);
        w.cache.lostResidency(A);
        const int first = w.retiredTotal();
        w.cache.lostResidency(A);
        check(first == 2 && w.retiredTotal() == 2 && w.eachRetiredOnce(),
              "3. LostResidency retires the texture's two views once each; a second Lost retires 0");
        check(w.listeners[A] == 1 && w.cache.watched() == 2 && w.cache.live() == 1,
              "3. after LostResidency the texture stays registered once, B's view stays");
    }
    {   // 4. Lost, then a lookup on the re-made image: a new make, NO second registration; close unregisters once
        World w;
        const uint64_t v1 = w.cache.view(key(A, RayViewKind::Sampled), 0xA1u);
        w.cache.lostResidency(A);
        const uint64_t v2 = w.cache.view(key(A, RayViewKind::Sampled), 0xA2u);
        check(v2 && v2 != v1 && w.makes == 2, "4. a lookup after LostResidency makes a new view");
        check(w.maxListeners == 1 && w.watchOns == 1, "4. one registration across the re-make (Ogre's list never holds two)");
        w.cache.close();
        check(w.watchOffs == 1 && w.badRemoves == 0 && w.registrations() == 0 && w.cache.watched() == 0,
              "4. close takes the one registration away exactly once; none left for the destructor's assert");
    }
    {   // 5. the image changed with no reason (the display swap): the old view retired once, a new one made
        World w;
        const uint64_t v1 = w.cache.view(key(A, RayViewKind::Sampled), 0xA0u);   // the blank image
        const uint64_t v2 = w.cache.view(key(A, RayViewKind::Sampled), 0xA1u);   // ready: the final image
        check(v2 && v2 != v1 && w.makes == 2 && w.retired[v1] == 1 && w.retiredTotal() == 1 && w.cache.live() == 1,
              "5. a lookup on a different image retires the old view once and makes one new");
    }
    {   // 6. Deleted: retired once, unregistered once; a new texture at the SAME address starts fresh
        World w;
        const uint64_t v1 = w.cache.view(key(A, RayViewKind::Storage), 0xA1u);
        w.cache.deleted(A);
        check(w.retired[v1] == 1 && w.retiredTotal() == 1 && w.watchOffs == 1 && w.registrations() == 0 &&
                  w.cache.watched() == 0,
              "6. Deleted retires the view once and takes the registration away once");
        const uint64_t v2 = w.cache.view(key(A, RayViewKind::Storage), 0xA1u);   // same pointer, same image handle
        check(v2 && v2 != v1 && w.makes == 2 && w.listeners[A] == 1,
              "6. a new texture at the same address, even on an equal image handle, makes a fresh view and registers once");
    }
    {   // 7. close: every live view retired once, nothing left
        World w;
        w.cache.view(key(A, RayViewKind::Sampled), 0xA1u);
        w.cache.view(key(A, RayViewKind::LayerStorage), 0xA1u);
        w.cache.view(key(B, RayViewKind::Storage), 0xB1u);
        w.cache.close();
        check(w.retiredTotal() == 3 && w.eachRetiredOnce() && w.cache.live() == 0 && w.cache.watched() == 0 &&
                  w.registrations() == 0 && w.badRemoves == 0,
              "7. close retires all three views once each and leaves no view and no registration");
        w.cache.close();
        check(w.retiredTotal() == 3 && w.watchOffs == 2, "7. a second close does nothing");
    }
    {   // 8. two jobs asking one key (the chain's depth read by several jobs): one make, one registration
        World w;
        const uint64_t a = w.cache.view(key(A, RayViewKind::Sampled), 0xA1u);   // the motion job
        const uint64_t b = w.cache.view(key(A, RayViewKind::Sampled), 0xA1u);   // the sun contact
        const uint64_t c = w.cache.view(key(A, RayViewKind::LayerSampled), 0xA1u);   // the reflection's layer view
        check(a == b && a != c && w.makes == 2 && w.watchOns == 1 && w.listeners[A] == 1,
              "8. jobs sharing a key share the view; the texture is registered once for all its views");
    }
    {   // 9. image 0 (a texture with no storage): refused - no make, a null view, no registration
        World w;
        const uint64_t v = w.cache.view(key(A, RayViewKind::LayerSampled), 0u);
        check(v == 0u && w.makes == 0 && w.watchOns == 0 && w.cache.watched() == 0,
              "9. image 0: no make, a null view, nothing registered");
    }
    {   // 10. a re-format under one image (a lookup while not resident, then setPixelFormat: no listener reason fires):
        //     the old-format view retired once, ONE entry left - never an orphan that waits for the next Lost or Deleted
        World w;
        RayViewKey k = key(A, RayViewKind::Storage);
        const uint64_t v1 = w.cache.view(k, 0xA0u);
        k.format = 91u;
        const uint64_t v2 = w.cache.view(k, 0xA0u);
        check(v2 && v2 != v1 && w.makes == 2 && w.retired[v1] == 1 && w.retiredTotal() == 1 && w.cache.live() == 1,
              "10. a lookup with another format on the same image: the old view retired once, one entry left");
        const uint64_t v3 = w.cache.view(k, 0xA0u);
        check(v3 == v2 && w.makes == 2, "10. the new format's view is then the cached one");
    }
    {   // 11. each other field of the key is a re-make too: mips, base mip, layers, base layer, cube-as-array
        World w;
        RayViewKey k = key(A, RayViewKind::LayerSampled);
        uint64_t prev = w.cache.view(k, 0xA1u);
        bool each = prev != 0u;
        const auto step = [&]() {
            const uint64_t v = w.cache.view(k, 0xA1u);
            each = each && v && v != prev && w.retired[prev] == 1;
            prev = v;
        };
        k.mipCount = 7u;      step();
        k.baseMip = 1u;       step();
        k.layerCount = 1u;    step();
        k.baseLayer = 1u;     step();
        k.cubeAsArray = true; step();
        check(each && w.makes == 6 && w.retiredTotal() == 5 && w.eachRetiredOnce() && w.cache.live() == 1 &&
                  w.watchOns == 1,
              "11. mips, base mip, layers, base layer, cube-as-array: each a re-make, the old retired once, one entry");
    }
    {   // 12. LostResidency, then Deleted: Lost retires the views, Deleted retires nothing more and unregisters once
        World w;
        const uint64_t s = w.cache.view(key(A, RayViewKind::Sampled), 0xA1u);
        const uint64_t u = w.cache.view(key(A, RayViewKind::Storage), 0xA1u);
        w.cache.lostResidency(A);
        w.cache.deleted(A);
        check(w.retired[s] == 1 && w.retired[u] == 1 && w.retiredTotal() == 2 && w.watchOffs == 1 &&
                  w.badRemoves == 0 && w.registrations() == 0 && w.cache.watched() == 0 && w.cache.live() == 0,
              "12. Lost then Deleted: two retires in all, one unregistration, nothing left");
        w.cache.close();
        check(w.retiredTotal() == 2 && w.watchOffs == 1, "12. close after that does nothing");
    }
    {   // 13. a failed make: a null view, nothing cached, nothing registered; the next lookup tries again
        World w;
        w.failMake = true;
        const uint64_t v0 = w.cache.view(key(A, RayViewKind::Sampled), 0xA1u);
        check(v0 == 0u && w.makes == 1 && w.watchOns == 0 && w.cache.watched() == 0 && w.cache.live() == 0,
              "13. a failed first make: a null view, nothing cached, no registration");
        w.failMake = false;
        const uint64_t v1 = w.cache.view(key(A, RayViewKind::Sampled), 0xA1u);
        check(v1 && w.makes == 2 && w.watchOns == 1 && w.cache.live() == 1,
              "13. the next lookup makes it and registers once");
        // a failed RE-make (the display swap): a null answer, the old view kept and not retired; a later success
        // retires it exactly once
        w.failMake = true;
        const uint64_t v2 = w.cache.view(key(A, RayViewKind::Sampled), 0xA2u);
        check(v2 == 0u && w.retiredTotal() == 0 && w.cache.live() == 1 && w.watchOns == 1,
              "13. a failed re-make on a new image: a null view, the old one kept, nothing retired");
        w.failMake = false;
        const uint64_t v3 = w.cache.view(key(A, RayViewKind::Sampled), 0xA2u);
        check(v3 && v3 != v1 && w.retired[v1] == 1 && w.retiredTotal() == 1 && w.cache.live() == 1,
              "13. the re-make that succeeds retires the old view exactly once");
    }
    std::printf("%s\n", gFail ? "engine.ray_view_cache: FAILED" : "engine.ray_view_cache: PASSED");
    return gFail ? 1 : 0;
}
