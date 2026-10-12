// lod.padding_untouched — LodStrategy::lodSet NEVER WRITES A PADDING SLOT (LOD-RACE-1).
//
// THE DEFECT (TSAN-LOD-UPDATE-RACE, measured under ThreadSanitizer in 25 of the 27
// tsan-blocked rows): every SoA pack of an ObjectMemoryManager holds ARRAY_PACKED_REALS
// slots, and a slot no object occupies — the tail of a render queue's last pack, or a
// hole a destroyed or moved object left — names the manager's ONE shared dummy
// (ObjectMemoryManager::mDummyObject, a NullEntity) as its owner. `lodSet` wrote
// `owner->mCurrentMeshLod` (and the fork's hysteresis band `owner->mHysteresisLod`) for
// every slot of the pack, so every updateAllLodsThread worker whose range held a padding
// slot wrote the same dummy bytes: two threads, one byte, no lock. The fix (the fork's
// LOD-RACE-1 commit) hands the manager's dummy to the strategy and `lodSet` skips a slot
// whose owner IS it — the exact mark Ogre puts on padding (initializeEmptySlots and
// destroyNode write it), so no real object is ever skipped.
//
// WHAT THIS ROW ASSERTS, with no engine, no display and no GPU (pure OgreMain):
//   (a) the setup really has padding: one render queue of seven objects with one
//       destroyed in the middle (a hole) and a partial last pack (a tail), and those
//       slots' owner is the manager's dummy;
//   (b) lodSet over every pack, with a band (the view pass) and without (every other
//       pass), leaves the dummy's bytes exactly as they were — mCurrentMeshLod AND
//       mHysteresisLod (0xFF, "no banded pass yet") at their initial values;
//   (c) the positive control: every REAL object took the level its value selects.
// RED ON BASE BY CONSTRUCTION: base lodSet writes the dummy (mHysteresisLod 0xFF -> 0
// on the first banded pack), and base has no padding parameter.
#include <OgreLodStrategy.h>
#include <OgreMovableObject.h>
#include <Math/Array/OgreObjectMemoryManager.h>
#include <Math/Array/OgreObjectData.h>
#include <OgreLodStrategyPrivate.inl>

#include <cstdio>
#include <cstring>
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

/// The smallest real MovableObject: lives in the manager's render queue 0 and carries
/// a three-level LOD list (the thresholds `lodSet` binary-searches).
class LodProbe final : public Ogre::MovableObject
{
public:
    LodProbe(Ogre::IdType id, Ogre::ObjectMemoryManager *mgr,
             const Ogre::FastArray<Ogre::Real> *lodList)
        : Ogre::MovableObject(id, mgr, nullptr, 0)
    {
        mLodMesh = lodList;
    }
    const Ogre::String &getMovableType() const override
    {
        static const Ogre::String kType("LodProbe");
        return kType;
    }
};

/// Every byte of the dummy, so a write of ANY member — not only the two we know of —
/// is seen. (The dummy is a heap NullEntity the manager owns; nothing else writes it.)
std::vector<unsigned char> bytesOf(const Ogre::NullEntity *o)
{
    const auto *p = reinterpret_cast<const unsigned char *>(o);
    return std::vector<unsigned char>(p, p + sizeof(Ogre::NullEntity));
}

void lodSetAllPacks(Ogre::ObjectMemoryManager &mgr, Ogre::Real value, Ogre::Real hysteresis)
{
    Ogre::ObjectData objData;
    const size_t total = mgr.getFirstObjectData(objData, 0);
    const Ogre::MovableObject *padding = mgr._getDummyObject();
    for (size_t i = 0; i < total; i += ARRAY_PACKED_REALS) {
        Ogre::Real lodValues[ARRAY_PACKED_REALS];
        for (size_t j = 0; j < ARRAY_PACKED_REALS; ++j) lodValues[j] = value;
        Ogre::LodStrategy::lodSet(objData, lodValues, hysteresis, padding);
        objData.advanceLodPack();
    }
}

}  // namespace

int main()
{
    Ogre::FastArray<Ogre::Real> lodList;
    lodList.push_back(Ogre::Real(0));
    lodList.push_back(Ogre::Real(10));
    lodList.push_back(Ogre::Real(20));

    {
        Ogre::ObjectMemoryManager mgr;
        std::vector<LodProbe *> objs;
        for (Ogre::IdType id = 1; id <= 7; ++id) objs.push_back(new LodProbe(id, &mgr, &lodList));
        // A HOLE: object 2 of 7 is not the last slot, so its slot goes to the reuse
        // pool and keeps the dummy as its owner (destroyNode) — mid-range padding.
        delete objs[1];
        objs.erase(objs.begin() + 1);

        const Ogre::NullEntity *dummy = mgr._getDummyObject();
        CHECK_MSG(dummy != nullptr, "the manager has no dummy object");

        // (a) the padding is there and it is the dummy.
        Ogre::ObjectData objData;
        const size_t total = mgr.getFirstObjectData(objData, 0);
        const size_t packs = (total + ARRAY_PACKED_REALS - 1) / ARRAY_PACKED_REALS;
        size_t paddingSlots = 0, realSlots = 0;
        for (size_t p = 0; p < packs; ++p) {
            for (size_t j = 0; j < ARRAY_PACKED_REALS; ++j) {
                const Ogre::MovableObject *o = objData.mOwner[j];
                if (o == dummy) ++paddingSlots; else ++realSlots;
            }
            objData.advanceLodPack();
        }
        CHECK_MSG(realSlots == objs.size(), "real slots %zu, objects %zu", realSlots, objs.size());
        CHECK_MSG(paddingSlots >= 2, "only %zu padding slots: the row needs a hole and a tail",
                  paddingSlots);
        std::printf("setup: %zu slots used (incl. the hole), %zu packs of %d, %zu real, %zu padding = the dummy %p\n",
                    total, packs, int(ARRAY_PACKED_REALS), realSlots, paddingSlots,
                    static_cast<const void *>(dummy));

        const std::vector<unsigned char> before = bytesOf(dummy);
        const unsigned char lod0 = dummy->getCurrentMeshLod();

        // (b) a banded pass (the view) then an unbanded one (every sibling pass), twice.
        lodSetAllPacks(mgr, Ogre::Real(25), Ogre::Real(0.1));
        lodSetAllPacks(mgr, Ogre::Real(25), Ogre::Real(0));
        lodSetAllPacks(mgr, Ogre::Real(15), Ogre::Real(0.1));

        CHECK_MSG(bytesOf(dummy) == before,
                  "lodSet wrote the shared dummy's bytes (the padding slots' owner)");
        CHECK_MSG(dummy->getCurrentMeshLod() == lod0, "dummy mCurrentMeshLod %u -> %u",
                  unsigned(lod0), unsigned(dummy->getCurrentMeshLod()));

        // (c) every real object took its level: 15 lies in [10, 20) -> level 1; the band
        // (0.1 of the crossed threshold 20, i.e. 18..22) does not hold 15 at level 2.
        for (const LodProbe *o : objs)
            CHECK_MSG(o->getCurrentMeshLod() == 1, "real object %u: level %u, want 1",
                      unsigned(o->getId()), unsigned(o->getCurrentMeshLod()));

        for (LodProbe *o : objs) delete o;
    }

    std::printf("lod.padding_untouched: %d checks failed, %s\n", gFailures,
                gFailures == 0 ? "PASS" : "FAIL");
    return gFailures == 0 ? 0 : 1;
}
