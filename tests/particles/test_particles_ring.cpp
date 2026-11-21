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
//   (c) the ring is then full again: a third allocation is refused;
//   (d) setBillboards(0) then a refill of the whole quota (particles.document_to_engine's "clears" / "re-populate"):
//       every slot freed from the top, then quota distinct valid handles - the merged fork 5eb057200 gave 2 and refused
//       6, and BillboardSet::allocBillboard wrote through the InvalidHandle (the row's segfault);
//   (e) the same after a hole below the top: the ring-full search must cover the whole ring.
// RED ON THE OLD PIN (cb862d184) BY CONSTRUCTION: (b)'s two allocations shared one handle and (c) returned it again.
// RED ON THE MERGED FORK (5eb057200, upstream's c5d535f3c in): (d) 2 distinct handles, 6 refused; (e) red too. GREEN
// with the fork's ParticleFX2 allocator fix (the top-free seen as LIFO when mLastParticleIdx == quota; the ring-full
// search over the whole ring); (e) alone stays red with only the first of the two.
#include <OgreAbiUtils.h>
#include <OgreRoot.h>
#include <OgreLogManager.h>
#include <Math/Array/OgreObjectMemoryManager.h>
#include <OgreBitset.h>
#include <OgreBitset.inl>   // the searches inline: compiled into THIS target, under its shift sanitizer
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

        // (b) two holes in the MIDDLE of a full ring (neither the first slot nor the top): the regrowth runs the hard
        //     (ring-full) path, which must mark each hole it hands out.
        const uint32_t holeHigh = live[5], holeLow = live[3];
        ring->deallocParticle(holeHigh);
        ring->deallocParticle(holeLow);
        const uint32_t a = ring->allocParticle();
        const uint32_t b = ring->allocParticle();
        std::printf("regrowth after freeing %u then %u: handles %u, %u\n", holeHigh, holeLow, a, b);
        CHECK_MSG(a != invalid && b != invalid, "regrowth: a refused allocation (%u, %u) with two slots free", a, b);
        CHECK_MSG(a != b, "regrowth: two allocations got ONE handle (%u): the ring-full path did not mark its slot", a);
        const bool aFreed = a == holeHigh || a == holeLow, bFreed = b == holeHigh || b == holeLow;
        CHECK_MSG(aFreed && bFreed, "regrowth: handles %u, %u are not the freed slots %u, %u", a, b, holeHigh, holeLow);

        // (c) full again: refused.
        const uint32_t c = ring->allocParticle();
        CHECK_MSG(c == invalid, "refill: a third allocation into a full ring returned %u, want InvalidHandle", c);

        // (d) setBillboards(0) then setBillboards(quota) - particles.document_to_engine's "clears" then "re-populate":
        //     every slot freed from the BACK (the top first), then the whole quota allocated again. An empty ring must
        //     take the whole quota: quota distinct valid handles, then refused. (The fork at 5eb057200 refused the THIRD
        //     allocation - the LIFO frees left the tracked range [1, 8) over an empty bitset - and BillboardSet::
        //     allocBillboard wrote through that InvalidHandle: the segfault.)
        for (uint32_t h = kQuota; h-- > 0;)
            ring->deallocParticle(h);
        std::set<uint32_t> refilled;
        uint32_t refused = 0;
        for (uint32_t i = 0; i < kQuota; ++i) {
            const uint32_t h = ring->allocParticle();
            if (h == invalid) ++refused; else refilled.insert(h);
        }
        std::printf("refill after freeing all from the top: %zu distinct handles, %u refused\n", refilled.size(), refused);
        CHECK_MSG(refused == 0, "refill: %u of %u allocations refused from an EMPTY ring", refused, kQuota);
        CHECK_MSG(refilled.size() == kQuota, "refill: %zu distinct handles, want %u", refilled.size(), kQuota);
        CHECK_MSG(ring->allocParticle() == invalid, "refill: an allocation past the quota was not refused");

        // (e) a hole BELOW the top, then the rest freed from the top down: the top-free shrinks the tracked range only
        //     to the hole (slot 3 stays inside it), and slot 0's FIFO free leaves the empty ring tracking [1; 4). The
        //     refill then reaches the ring-full search with free slots ABOVE the first tracked one (2 and 3): the
        //     search must cover the whole ring. (Red with the top-free fix alone: 2 of 8 refused.)
        for (uint32_t h = kQuota; h-- > 0;)
            ring->deallocParticle(h);                       // (d) left the ring full: empty it from the top
        for (uint32_t i = 0; i < kQuota; ++i)
            ring->allocParticle();                          // full again, [0; quota) in order
        ring->deallocParticle(3);                           // the hole
        for (uint32_t h = kQuota; h-- > 0;)
            if (h != 3) ring->deallocParticle(h);
        std::set<uint32_t> again;
        uint32_t refusedAgain = 0;
        for (uint32_t i = 0; i < kQuota; ++i) {
            const uint32_t h = ring->allocParticle();
            if (h == invalid) ++refusedAgain; else again.insert(h);
        }
        std::printf("refill after a hole and the top-down frees: %zu distinct handles, %u refused\n", again.size(),
                    refusedAgain);
        CHECK_MSG(refusedAgain == 0, "refill after a hole: %u of %u allocations refused from an EMPTY ring", refusedAgain,
                  kQuota);
        CHECK_MSG(again.size() == kQuota, "refill after a hole: %zu distinct handles, want %u", again.size(), kQuota);

        delete ring;
    }
    // (f) THE SEARCH AT A WORD'S EDGE: the ring-full search is findLastBitUnset() = findLastBitUnset( capacity ), and a
    //     capacity that is a multiple of 64 (a quota of 64, 128, ...) ends on a word's edge. The last word's mask was
    //     `~0 >> ( 64 - ( startFrom & 63 ) )` - a shift by 64 there, undefined in C++ (x86 shifts by 0 and happens to
    //     answer right). This target is built with -fsanitize=shift -fno-sanitize-recover, and these calls compile the
    //     header's search into it: the shift aborts the row (RED on the fork before its bitset commit), the masked form
    //     passes. findLastBitSetPlusOne( startFrom ), deallocParticle's search, had the same line.
    for (const size_t cap : {size_t(64), size_t(128)}) {
        Ogre::bitset64 bits;
        bits.reset(cap);
        bits.setAll();
        bits.unset(cap - 1u);                               // the word's top bit is the only free one
        CHECK_MSG(bits.findLastBitUnset() == cap - 1u, "bitset %zu: findLastBitUnset() %zu, want %zu", cap,
                  bits.findLastBitUnset(), cap - 1u);
        CHECK_MSG(bits.findLastBitUnset(cap) == cap - 1u, "bitset %zu: findLastBitUnset(%zu) %zu, want %zu", cap, cap,
                  bits.findLastBitUnset(cap), cap - 1u);
        Ogre::bitset64 one;
        one.reset(cap);
        one.set(cap - 1u);                                  // only the top bit set
        CHECK_MSG(one.findLastBitSetPlusOne(cap) == cap, "bitset %zu: findLastBitSetPlusOne(%zu) %zu, want %zu", cap, cap,
                  one.findLastBitSetPlusOne(cap), cap);
    }
    // ... and the allocator at those quotas: (d)'s clear-and-refill through a full-word ring.
    for (const uint32_t quota : {64u, 128u}) {
        std::vector<Ogre::ArrayReal> ttl(quota / ARRAY_PACKED_REALS + 1);
        Ogre::ObjectMemoryManager mgr;
        auto *ring = new RingProbe(&mgr, ttl.data());
        ring->setParticleQuota(quota);
        for (uint32_t i = 0; i < quota; ++i) ring->allocParticle();
        ring->deallocParticle(quota / 2u);                  // a hole, then everything else from the top
        for (uint32_t h = quota; h-- > 0;)
            if (h != quota / 2u) ring->deallocParticle(h);
        std::set<uint32_t> got;
        uint32_t refused = 0;
        for (uint32_t i = 0; i < quota; ++i) {
            const uint32_t h = ring->allocParticle();
            if (h == Ogre::ParticleSystemDef::InvalidHandle) ++refused; else got.insert(h);
        }
        std::printf("quota %u: refill after a hole and the top-down frees: %zu distinct handles, %u refused\n", quota,
                    got.size(), refused);
        CHECK_MSG(refused == 0 && got.size() == quota, "quota %u: %zu distinct, %u refused, want %u and 0", quota,
                  got.size(), refused, quota);
        delete ring;
    }
    delete root;
    std::printf("%s (%d failure%s)\n", gFailures ? "FAILED" : "PASSED", gFailures, gFailures == 1 ? "" : "s");
    return gFailures ? 1 : 0;
}
