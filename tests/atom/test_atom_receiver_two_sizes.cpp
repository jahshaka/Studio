// atom.receiver_two_sizes (COLD-BOOT-1 stage 2's owed card row, built in SHADER-SYSTEM-1 stage 3F (e); the lead's ruling
// P1) - NO PERMUTATION OF THE RECEIVER GRID IS KEYED ON A VIEW'S SIZE. Atom's receiver depth job (Jahshaka/ReceiverDepth,
// JahshakaCompute.material.json) is dispatched per VIEW with group counts that follow the view's depth rectangle
// (OgreAtomCasterPass.cpp, setNumThreadGroups). A job told its sizes (the fork's default, inform_shader_of_texture_size)
// writes num_thread_groups_x/y/z into its PROPERTIES - the permutation's key - so every distinct view size (the editor,
// the recorder, a PiP, a headset's eye size) built a permutation of its own. Stage 2 set the flag false on the job, and the
// dispatch takes the job's own counts (fork BUGS-6). The laws, one view of an Atom scene with shadows drawn at two sizes:
//   (1) the job ran and its permutation was built at the first size (threads_per_group_x 8 in its properties: written at
//       its build);
//   (2) AT BOTH SIZES no group count is in its properties - the permutation the two sizes dispatch is ONE;
//   (3) NEGATIVE CONTROL: the same job told its sizes (setInformHlmsOfTextureSize(true), the base's state) carries the
//       group count in its properties, and the two sizes give two values - two permutations.
// Frames, never time: 12 frames at each size and arm (48 in all). The split off on a device is a stated skip.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <OgreHlmsCompute.h>
#include <OgreHlmsComputeJob.h>
#include <OgreHlmsManager.h>
#include <OgreRoot.h>

#include <cstdio>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok: "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

namespace {
struct Props { int threadsX = -1, groupsX = -1; };
Props readJob() {
    Props p;
    Ogre::HlmsCompute *hc = Ogre::Root::getSingleton().getHlmsManager()->getComputeHlms();
    if (Ogre::HlmsComputeJob *job = hc ? hc->findComputeJobNoThrow("Jahshaka/ReceiverDepth") : nullptr) {
        p.threadsX = job->getProperty(Ogre::ComputeProperty::ThreadsPerGroupX, -1);
        p.groupsX = job->getProperty(Ogre::ComputeProperty::NumThreadGroupsX, -1);
    }
    return p;
}
void informSizes(bool on) {
    Ogre::HlmsCompute *hc = Ogre::Root::getSingleton().getHlmsManager()->getComputeHlms();
    if (Ogre::HlmsComputeJob *job = hc ? hc->findComputeJobNoThrow("Jahshaka/ReceiverDepth") : nullptr)
        job->setInformHlmsOfTextureSize(on);
}
}  // namespace

int main() {
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-atom-receiver-two-sizes-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();

    View *v = e->createOffscreenView("recvsizes", 128u, 128u, Colour(0, 0, 0), OffscreenContract::Live);
    Scene *s = e->createScene("recvsizes");
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.04f, 0.04f, 0.04f));
    const NodeId floorN = enginetest::addTestCube(s, Colour(0.6f, 0.6f, 0.6f), 0.0f, 0.9f);
    s->setNodeTransform(floorN, Vec3(0.0f, -0.55f, 0.0f), Quat(), Vec3(12.0f, 0.1f, 12.0f));
    for (int i = 0; i < 4; ++i) {
        const NodeId c = enginetest::addTestCube(s, Colour(0.7f, 0.4f, 0.2f), 0.0f, 0.6f);
        enginetest::setNodePosition(s, c, Vec3(-1.5f + 1.0f * float(i), 0.0f, float(i % 2) - 0.5f));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.6f), 3.0f);
    v->setScene(s);
    v->setShadows(true);
    enginetest::testCameraLookAt(v, Vec3(0.0f, 3.0f, 6.0f), Vec3(0.0f, 0.0f, 0.0f));

    enginetest::frames(e, 12);
    if (!s->atomDrawStatus().on) {
        std::printf("ok: the split is off on this device (no receiver grid) - atom.receiver_two_sizes skips\n");
        return 0;
    }
    const Props a = readJob();
    v->resize(320u, 96u);
    enginetest::frames(e, 12);
    const Props b = readJob();
    std::printf("   ReceiverDepth's properties: at 128 x 128 threads_per_group_x %d, num_thread_groups_x %d; at 320 x 96 "
                "%d, %d (-1 = not in the key)\n", a.threadsX, a.groupsX, b.threadsX, b.groupsX);
    CHECK(a.threadsX == 8, "(1) the receiver depth job ran and its permutation was built (threads_per_group_x %d)",
          a.threadsX);
    CHECK(a.groupsX == -1 && b.groupsX == -1,
          "(2) no group count in its key at either size: the two view sizes dispatch ONE permutation");

    // (3) THE NEGATIVE CONTROL: the base's state, the job told its sizes
    informSizes(true);
    enginetest::frames(e, 12);
    const Props c = readJob();
    v->resize(128u, 128u);
    enginetest::frames(e, 12);
    const Props d = readJob();
    informSizes(false);
    std::printf("   told its sizes: num_thread_groups_x %d at 320 x 96, %d at 128 x 128\n", c.groupsX, d.groupsX);
    CHECK(c.groupsX > 0 && d.groupsX > 0 && c.groupsX != d.groupsX,
          "(3) NEGATIVE CONTROL: told its sizes, the job keys its group count - two sizes, two permutations");

    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
