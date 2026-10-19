// atom.view_colour — THE TRIANGLES VIEW'S COLOUR IS THE STRUCTURE'S, NEVER THE SLOT'S (ENGINE-POLISH-ATOM-1 stage 6; the
// owner's observation (a), spikes/bistro-oneoff/report.md §3).
//
// The Atom view's 'triangles' mode painted hash(slot | depth, cluster | triangle): a GPU-scene slot is renumbered when
// an item before it is deleted (the table swap-removes: the last slot moves into the freed one), so objects that did
// not change were recoloured. The colour hashes the slot's MESH ENTRY now (stable for as long as the mesh lives), the
// cluster's index in its mesh's DAG, the triangle and the depth the cut drew it at. The fixture: three Atom spheres -
// two copies of one mesh and one of another - under the 'triangles' view; the FIRST item is deleted.
//   (a) THE KEPT OBJECTS KEEP THEIR COLOURS: every pixel the kept spheres covered (the id image's slots before the
//       delete) reads the same colour after it, the renumbered one (the last slot, moved into the first) included;
//   (b) THE MOVE HAPPENED: the last item's slot did change (the claim is not vacuous);
//   (c) two copies of one mesh show the same colour set (the hash names the mesh, not the object).
// FRAMES, NEVER TIME: every picture is read until two consecutive reads agree to a code.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include "EnginePrivate.h"
#include "GpuScene.h"
#include "HlmsAtom.h"

#include <Compositor/OgreCompositorNode.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <OgreImage2.h>
#include <OgreTextureBox.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using jahshaka::engine::detail::AtomId;
using jahshaka::engine::detail::OgreScene;
using jahshaka::engine::detail::OgreView;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[640];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static const unsigned kW = 480, kH = 270;

/// A UV sphere, the last column and ring wrapped by index (DOCS/traps/ENGINE.md: procedural seams do not weld).
static MeshData sphereMesh(int seg, int ring, float r)
{
    MeshData d;
    for (int j = 0; j <= ring; ++j) {
        const float th = float(j) / float(ring) * 3.14159265f;
        for (int i = 0; i <= seg; ++i) {
            const float ph = float(i) / float(seg) * 6.28318531f;
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = -std::sin(th) * std::sin(ph);
            d.positions.insert(d.positions.end(), { r * x, r * y, r * z });
            d.normals.insert(d.normals.end(), { x, y, z });
            d.uvs.insert(d.uvs.end(), { float(i) / float(seg), float(j) / float(ring) });
        }
    }
    for (int j = 0; j < ring; ++j)
        for (int i = 0; i < seg; ++i) {
            const unsigned a = unsigned(j * (seg + 1) + i), b = a + 1u;
            const unsigned c = unsigned((j + 1) * (seg + 1) + i), e = c + 1u;
            d.indices.insert(d.indices.end(), { a, c, b, b, c, e });
        }
    return d;
}

static bool readIds(OgreView *v, std::vector<uint32_t> &x)
{
    Ogre::CompositorWorkspace *ws = v->workspace();
    if (!ws) return false;
    Ogre::TextureGpu *tex = nullptr;
    for (Ogre::CompositorNode *n : ws->getNodeSequence())
        if ((tex = n->getDefinedTexture(Ogre::IdString(detail::kAtomIdTexture))) != nullptr) break;
    if (!tex) return false;
    Ogre::Image2 img;
    img.convertFromTexture(tex, 0u, 0u);
    const Ogre::TextureBox box = img.getData(0u);
    x.assign(size_t(kW) * kH, AtomId::kEmpty);
    for (unsigned r = 0; r < kH; ++r) {
        const auto *row = reinterpret_cast<const uint32_t *>(box.at(0, r, 0));
        for (unsigned c = 0; c < kW; ++c) x[size_t(r) * kW + c] = row[c * 2u];
    }
    return true;
}

static bool settle(Engine *e, View *view, Image &img)
{
    Image prev;
    int stable = 0, frames = 0;
    while (frames < 400) {
        enginetest::frames(e, 4);
        frames += 4;
        if (!view->readPixels(img)) return false;
        if (frames >= 12 && prev.rgba.size() == img.rgba.size()) {
            stable = std::memcmp(prev.rgba.data(), img.rgba.data(), img.rgba.size()) == 0 ? stable + 1 : 0;
            if (stable >= 2) return true;
        }
        prev = img;
    }
    return false;
}

static uint32_t slotOf(OgreScene *os, NodeId n)
{
    os->ensureGpuScene(false);
    const detail::GpuScene &gs = os->gpuScene();
    for (unsigned s = 0; s < gs.slotCount(); ++s)
        if (os->nodeOfGpuKey(gs.entry(s).ids[0]) == n) return s;
    return 0xFFFFFFFFu;
}

int main()
{
    std::printf("== atom.view_colour: the triangles view's colour survives a delete's slot renumbering\n");
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-atom-view-colour-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    View *view = e->createOffscreenView("viewcolour", kW, kH, Colour(0, 0, 0));
    view->setOffscreenContract(OffscreenContract::StillPicture);
    Scene *scene = e->createScene("viewcolour");
    view->setScene(scene);
    auto *os = static_cast<OgreScene *>(scene);
    auto *ov = static_cast<OgreView *>(view);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 0;
        view->setPostFx(fx);
    }
    scene->setLodBias(1.0f);
    scene->setAmbient(Colour(0.10f, 0.11f, 0.13f), Colour(0.05f, 0.05f, 0.05f));
    enginetest::testCameraLookAt(view, Vec3(0.0f, 0.4f, 6.0f), Vec3(0.0f, 0.0f, 0.0f));
    enginetest::addDirectionalLight(scene, Vec3(-0.3f, -0.8f, -0.5f), 2.0f);
    const MeshId meshA = scene->createMesh(sphereMesh(64, 32, 0.7f));
    const MeshId meshB = scene->createMesh(sphereMesh(48, 24, 0.6f));
    PbrParams p;
    p.albedo = Colour(0.6f, 0.6f, 0.6f);
    const MaterialId mat = scene->createPbrMaterial(p);
    // THE FIRST ITEM (deleted below), then a copy of its mesh, then the other mesh LAST (its slot moves on the delete)
    const NodeId first = scene->createNode(), copyA = scene->createNode(), lastB = scene->createNode();
    scene->setNodeTransform(first, Vec3(-1.8f, 0.0f, 0.0f), Quat(), Vec3(1, 1, 1));
    scene->setNodeTransform(copyA, Vec3(0.0f, 0.0f, 0.0f), Quat(), Vec3(1, 1, 1));
    scene->setNodeTransform(lastB, Vec3(1.8f, 0.0f, 0.0f), Quat(), Vec3(1, 1, 1));
    scene->attachMesh(first, meshA, mat);
    scene->attachMesh(copyA, meshA, mat);
    scene->attachMesh(lastB, meshB, mat);
    enginetest::frames(e, 4);
    scene->setAtomView(AtomView::Triangles);
    Image before;
    CHECK(settle(e, view, before), "the triangles view settles");
    const uint32_t sFirst = slotOf(os, first), sCopy = slotOf(os, copyA), sLast = slotOf(os, lastB);
    std::vector<uint32_t> ids;
    CHECK(readIds(ov, ids), "the id image reads back");
    std::printf("   slots: first %u, copy %u, last %u\n", sFirst, sCopy, sLast);
    // (c) the copy of the first's mesh shows the first's colour set
    std::set<uint32_t> coloursFirst, coloursCopy;
    for (size_t i = 0; i < ids.size(); ++i) {
        const uint32_t s = ids[i] & AtomId::kSlotMask;
        uint32_t c = 0u;
        std::memcpy(&c, &before.rgba[i * 4u], 3u);
        if (ids[i] == AtomId::kEmpty) continue;
        if (s == sFirst) coloursFirst.insert(c);
        if (s == sCopy) coloursCopy.insert(c);
    }
    unsigned shared = 0;
    for (uint32_t c : coloursCopy) shared += coloursFirst.count(c) ? 1u : 0u;
    std::printf("   colours: first %zu, copy %zu, shared %u\n", coloursFirst.size(), coloursCopy.size(), shared);
    CHECK_MSG(shared * 2u >= unsigned(coloursCopy.size()) && !coloursCopy.empty(),
              "(c) two copies of one mesh show the same colours (%u of the copy's %zu are the first's)", shared,
              coloursCopy.size());
    // THE DELETE
    scene->removeNode(first);
    Image after;
    CHECK(settle(e, view, after), "the triangles view settles after the delete");
    const uint32_t sLastAfter = slotOf(os, lastB), sCopyAfter = slotOf(os, copyA);
    std::printf("   after the delete: copy slot %u, last slot %u\n", sCopyAfter, sLastAfter);
    CHECK_MSG(sLastAfter != sLast || sCopyAfter != sCopy, "(b) a kept item was renumbered (last %u -> %u, copy %u -> %u)",
              sLast, sLastAfter, sCopy, sCopyAfter);
    unsigned kept = 0, changed = 0;
    for (size_t i = 0; i < ids.size(); ++i) {
        const uint32_t s = ids[i] & AtomId::kSlotMask;
        if (ids[i] == AtomId::kEmpty || (s != sCopy && s != sLast)) continue;
        ++kept;
        if (std::memcmp(&before.rgba[i * 4u], &after.rgba[i * 4u], 3u) != 0) ++changed;
    }
    std::printf("   the kept objects' pixels: %u, %u changed colour\n", kept, changed);
    CHECK_MSG(kept > 1000u && changed == 0u,
              "(a) the kept objects keep their colours across the delete (%u of %u pixels changed)", changed, kept);
    scene->setAtomView(AtomView::Off);
    enginetest::frames(e, 2);
    e->destroyView(view);
    e->destroyScene(scene);
    engine.reset();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
