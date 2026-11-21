// particles.ring_full_handles — A FULL PARTICLE RING NEVER HANDS ONE SLOT TO TWO ALLOCATIONS (OGRE-BUMP-2 stage 1,
// upstream c5d535f3c "[PFX2] allocParticle() would not mark returned particle as active").
//
// THE DEFECT: ParticleSystemDef::allocParticle has an easy path (the ring not full: the next slot, marked used) and a hard
// path (the ring's tracked range spans the whole quota: search the bitset for a hole). The hard path returned the hole it
// found WITHOUT marking it, so the next allocation found the same hole: two billboards (or particles) on one handle — one
// of them drawn where the other was written, and a later free of either frees the slot under the other.
//
// OUR PATH REACHES IT (irisgl/engine/src/OgreParticles.cpp, OgreScene::setBillboards): a billboard set filled to its
// capacity, then shrunk by freeing from the BACK (LIFO), then grown again. A LIFO free below the top of a full ring leaves
// mLastParticleIdx at the quota (deallocParticle's "non-FIFO" branch: the first slot is still live), so the regrowth runs the
// hard path once per billboard — the exact shape this row drives.
//
// WHAT THIS ROW ASSERTS, with no engine, no render system, no display and no GPU (OgreMain and a bare Root, which
// ParticleSystem's constructor reads for its frame number):
//   (a) the positive control: filling an 8-slot ring gives 8 distinct handles, and a ninth is refused (InvalidHandle);
//   (b) after two LIFO frees, two allocations get two DISTINCT valid handles, both among the two freed slots;
//   (c) the ring is then full again: a third allocation is refused.
// RED ON THE OLD PIN (cb862d184) BY CONSTRUCTION: (b)'s second allocation returned the first's handle (7, 7), and (c)
// returned 7 a third time.
#include <OgreAbiUtils.h>
#include <OgreRoot.h>
#include <OgreLogManager.h>
#include <Math/Array/OgreObjectMemoryManager.h>
#include <ParticleSystem/OgreParticleSystem2.h>

#include <cstdio>
#include <set>
#include <vector>

namespace {

int gFailures = 0;
#define CHECK_MSG(cond, ...)                                       \
    do {                                                           \
        if (!(cond)) {                                             \
            ++gFailures;                                           \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__);       \
            std::printf(__VA_ARGS__);                              \
            std::printf("\n");                                     \
        }                                                          \
    } while (0)

/// A billboard-set definition that is never init()ed (no VaoManager, no GPU buffers): its allocator is pure CPU
/// bookkeeping over the active bitset. deallocParticle writes the freed slot's time-to-live, so the probe lends it an
/// array of its own (mPosition stays null: the definition is "not initialised" and its destructor frees nothing).
class RingProbe final : public Ogre::ParticleSystemDef
{
public:
    RingProbe(Ogre::ObjectMemoryManager *mgr, Ogre::ArrayReal *timeToLive)
        : Ogre::ParticleSystemDef(1u, mgr, nullptr, nullptr, "ring_probe", true)
    {
        mParticleCpuData.mTimeToLive = timeToLive;
    }
    ~RingProbe() override { mParticleCpuData.mTimeToLive = nullptr; }
    using Ogre::ParticleSystemDef::allocParticle;
    using Ogre::ParticleSystemDef::deallocParticle;
};

}  // namespace

int main()
{
    // A silent default log BEFORE the Root: the Root's constructor adopts it instead of writing Ogre.log.
    Ogre::LogManager logs;
    logs.createLog("", true, false, true);
    // No plugins, no config, no render system: the Root exists only for ParticleSystem's constructor.
    Ogre::AbiCookie abi = Ogre::generateAbiCookie();
    auto *root = new Ogre::Root(&abi, "", "", "");
    {
        constexpr uint32_t kQuota = 8u;   // two SIMD packs at ARRAY_PACKED_REALS 4: setParticleQuota keeps it exact
        Ogre::ArrayReal timeToLive[kQuota / ARRAY_PACKED_REALS + 1];
        Ogre::ObjectMemoryManager mgr;
        auto *ring = new RingProbe(&mgr, timeToLive);
        ring->setParticleQuota(kQuota);
        const uint32_t invalid = Ogre::ParticleSystemDef::InvalidHandle;
        CHECK_MSG(ring->getQuota() == kQuota, "the ring's quota is %u, want %u", ring->getQuota(), kQuota);

        // (a) fill: 8 distinct handles in [0, quota), then refused.
        std::vector<uint32_t> live;
        std::set<uint32_t> seen;
        for (uint32_t i = 0; i < kQuota; ++i) {
            const uint32_t h = ring->allocParticle();
            CHECK_MSG(h != invalid && h < kQuota, "fill #%u: handle %u", i, h);
            seen.insert(h);
            live.push_back(h);
        }
        CHECK_MSG(seen.size() == kQuota, "fill: %zu distinct handles, want %u", seen.size(), kQuota);
        const uint32_t ninth = ring->allocParticle();
        CHECK_MSG(ninth == invalid, "fill: a ninth allocation from a full ring returned %u, want InvalidHandle", ninth);

        // (b) setBillboards' shrink: free the two at the BACK, last first.
        const uint32_t freedTop = live.back();
        live.pop_back();
        ring->deallocParticle(freedTop);
        const uint32_t freedNext = live.back();
        live.pop_back();
        ring->deallocParticle(freedNext);
        // ... and its regrowth: two allocations, two handles.
        const uint32_t a = ring->allocParticle();
        const uint32_t b = ring->allocParticle();
        std::printf("regrowth after freeing %u then %u: handles %u, %u\n", freedTop, freedNext, a, b);
        CHECK_MSG(a != invalid && b != invalid, "regrowth: a refused allocation (%u, %u) with two slots free", a, b);
        CHECK_MSG(a != b, "regrowth: two allocations got ONE handle (%u): the ring-full path did not mark its slot", a);
        const bool aFreed = a == freedTop || a == freedNext, bFreed = b == freedTop || b == freedNext;
        CHECK_MSG(aFreed && bFreed, "regrowth: handles %u, %u are not the freed slots %u, %u", a, b, freedTop, freedNext);

        // (c) full again: refused.
        const uint32_t c = ring->allocParticle();
        CHECK_MSG(c == invalid, "refill: a third allocation into a full ring returned %u, want InvalidHandle", c);

        delete ring;
    }
    delete root;
    std::printf("%s (%d failure%s)\n", gFailures ? "FAILED" : "PASSED", gFailures, gFailures == 1 ? "" : "s");
    return gFailures ? 1 : 0;
}
