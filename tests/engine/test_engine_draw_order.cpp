// engine.draw_order — THE PICTURE NEVER DEPENDS ON CREATION ORDER: COINCIDENT ON-TOP PARTS DRAW IN A STATED ORDER
// (GIZMO-ORDER-1). Two on-top parts on the same mesh shape and transform hit the same pixels at bit-identical depth and
// both pass the overlay pass's LESS_EQUAL test, so the one drawn LAST shows. Without a stated order that is decided by
// the VAO's render-queue id in Ogre's sort key - a count of every vertex array the process made - so the translate
// gizmo's shared legs changed colour between launches (spikes/gizmo-order/DIAGNOSIS.md). The law, in exact codes on
// the Plain instrument (linear writes; an unlit 1.0 is code 255):
//   (1) red at order 1 and blue at order 2 show BLUE whichever was created first, the two pictures byte-equal;
//   (2) the orders swapped show RED in both creation orders, byte-equal;
//   (3) a BLENDED on-top part still draws after an opaque one of order 4 (the VR markers' law: the sub-group sits
//       above the key's transparency bit) - created first, its half-alpha green still mixes over the blue;
//   (4) the verb refuses by name: an unknown node, an order past 6, a node with no on-top part, a blended part.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Pictures.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <string>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

namespace {

constexpr unsigned kSize = 32;
const Colour kRed(1.0f, 0.0f, 0.0f), kBlue(0.0f, 0.0f, 1.0f), kGreenHalf(0.0f, 1.0f, 0.0f, 0.5f);

struct Code { int r = -1, g = -1, b = -1; };
Code centre(const Image &img)
{
    const size_t i = (size_t(kSize / 2) * img.width + kSize / 2) * 4u;
    if (img.rgba.size() < i + 4) return Code();
    return Code{ img.rgba[i], img.rgba[i + 1], img.rgba[i + 2] };
}

/// The camera on the cube's corner: the centre pixel is the parts' shared face.
void aim(View *v) { enginetest::testCameraLookAt(v, Vec3(1.2f, 0.9f, 2.0f), Vec3(0, 0, 0)); }

/// One on-top part: its own mesh (the gizmo's handles are one mesh each), its own material, at the origin.
NodeId addPart(Scene *s, const Colour &c)
{
    const NodeId n = s->createNode();
    const MeshId m = s->createMesh(enginetest::unitCubeMesh());
    const MaterialId mat = s->createUnlitMaterial(c, /*depthTest*/ false);
    return n && m && mat && s->attachMesh(n, m, mat) ? n : 0;
}

/// A fresh scene: `first` then `second` created in that order, each with its stated order; the picture read.
bool picture(Engine *e, View *v, const Colour &first, unsigned firstOrder, const Colour &second, unsigned secondOrder,
             Image &out, const char *what)
{
    Scene *s = e->createScene(what);
    if (!s) return false;
    const NodeId a = addPart(s, first);
    const NodeId b = addPart(s, second);
    bool ok = a && b;
    ok = ok && s->setNodeDrawOrder(a, firstOrder);
    ok = ok && s->setNodeDrawOrder(b, secondOrder);
    if (!ok) std::printf("    %s: the parts or their orders refused: %s\n", what, e->lastError().c_str());
    ok = ok && v->setScene(s);
    aim(v);
    enginetest::frames(e, 3);
    ok = ok && v->readPixels(out);
    v->setScene(nullptr);
    e->destroyScene(s);
    return ok;
}

}  // namespace

int main()
{
    std::printf("== engine.draw_order: coincident on-top parts draw in their stated order, whatever was created first\n");
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-draw-order-ogre.log"), err);
    CHECK(engine != nullptr, "engine created (%s)", err.c_str());
    if (!engine) return 1;
    Engine *e = engine.get();
    View *v = e->createOffscreenView("order", kSize, kSize, Colour(0, 0, 0), OffscreenContract::Live);
    CHECK(v != nullptr, "an offscreen view (%s)", e->lastError().c_str());
    if (!v) return 1;
    CHECK(!enginetest::displayEncoded(v), "the Plain instrument: linear writes, an unlit 1.0 is code 255");

    // (1) and (2): both creation orders at each stated order.
    Image redFirst, blueFirst, redFirstSwapped, blueFirstSwapped;
    CHECK(picture(e, v, kRed, 1, kBlue, 2, redFirst, "red-then-blue"), "red (order 1) created before blue (order 2)");
    CHECK(picture(e, v, kBlue, 2, kRed, 1, blueFirst, "blue-then-red"), "blue (order 2) created before red (order 1)");
    CHECK(picture(e, v, kRed, 2, kBlue, 1, redFirstSwapped, "red-then-blue-swapped"),
          "red (order 2) created before blue (order 1)");
    CHECK(picture(e, v, kBlue, 1, kRed, 2, blueFirstSwapped, "blue-then-red-swapped"),
          "blue (order 1) created before red (order 2)");
    const Code c1 = centre(redFirst), c2 = centre(blueFirst), c3 = centre(redFirstSwapped), c4 = centre(blueFirstSwapped);
    CHECK(c1.r == 0 && c1.g == 0 && c1.b == 255, "red created first, blue at the higher order: blue shows (%d,%d,%d)",
          c1.r, c1.g, c1.b);
    CHECK(c2.r == 0 && c2.g == 0 && c2.b == 255, "blue created first, blue at the higher order: blue shows (%d,%d,%d)",
          c2.r, c2.g, c2.b);
    CHECK(redFirst.rgba == blueFirst.rgba, "orders red 1 / blue 2: the two creation orders give the same bytes");
    CHECK(c3.r == 255 && c3.g == 0 && c3.b == 0, "red created first, red at the higher order: red shows (%d,%d,%d)",
          c3.r, c3.g, c3.b);
    CHECK(c4.r == 255 && c4.g == 0 && c4.b == 0, "blue created first, red at the higher order: red shows (%d,%d,%d)",
          c4.r, c4.g, c4.b);
    CHECK(redFirstSwapped.rgba == blueFirstSwapped.rgba, "orders red 2 / blue 1: the two creation orders give the same bytes");

    // (3) A blended on-top part created FIRST, an opaque one at order 4 after it: the blended one still draws last.
    // Drawn first, the opaque blue would cover it at the shared depth (0,0,255); drawn last, its half alpha mixes
    // over the blue: 255 x 0.5 = 127.5 in green and in blue, red 0.
    {
        Scene *s = e->createScene("blended-after-opaque");
        const NodeId glass = s ? addPart(s, kGreenHalf) : 0;
        const NodeId solid = s ? addPart(s, kBlue) : 0;
        CHECK(glass && solid && s->setNodeDrawOrder(solid, 4), "a blended part, then an opaque one at order 4 (%s)",
              e->lastError().c_str());
        Image img;
        const bool attached = s && v->setScene(s);
        if (attached) aim(v);
        enginetest::frames(e, 3);
        const bool read = attached && v->readPixels(img);
        const Code c = centre(img);
        CHECK(read && c.r == 0 && c.g >= 127 && c.g <= 128 && c.b >= 127 && c.b <= 128,
              "the blended part mixes over the order-4 opaque one (%d,%d,%d; drawn before it would read 0,0,255)",
              c.r, c.g, c.b);
        v->setScene(nullptr);

        // (4) the refusals, each by name.
        const auto refused = [&](bool r, const char *words) {
            return !r && e->lastError().find(words) != std::string::npos;
        };
        CHECK(refused(s->setNodeDrawOrder(NodeId(987654321), 1), "unknown node"), "an unknown node is refused (%s)",
              e->lastError().c_str());
        CHECK(refused(s->setNodeDrawOrder(solid, 7), "past 6"), "order 7 is refused (%s)", e->lastError().c_str());
        CHECK(refused(s->setNodeDrawOrder(glass, 1), "ordered by depth"), "a blended part is refused (%s)",
              e->lastError().c_str());
        const NodeId tested = s->createNode();
        const MeshId tm = s->createMesh(enginetest::unitCubeMesh());
        const MaterialId tmat = s->createUnlitMaterial(kRed, /*depthTest*/ true);
        CHECK(tested && tm && tmat && s->attachMesh(tested, tm, tmat), "a depth-tested part");
        CHECK(refused(s->setNodeDrawOrder(tested, 1), "no on-top part"), "a depth-tested part is refused (%s)",
              e->lastError().c_str());
        const NodeId bare = s->createNode();
        CHECK(refused(s->setNodeDrawOrder(bare, 1), "no on-top part"), "a node with no mesh is refused (%s)",
              e->lastError().c_str());
        e->destroyScene(s);
    }

    e->destroyView(v);
    enginetest::frames(e, 1);
    engine.reset();   // the engine's teardown logs; the verdict is the row's last line
    if (failures) std::printf("engine.draw_order: FAILED (%d)\n", failures);
    else std::printf("engine.draw_order: PASSED\n");
    return failures ? 1 : 0;
}
