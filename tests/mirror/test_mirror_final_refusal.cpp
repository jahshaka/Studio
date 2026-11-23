// mirror.final_refusal — A REFUSAL THE ENGINE WILL GIVE AGAIN IS TAKEN ONCE (SCRIPT-IN-ENGINE-1 2e e1; the audit of builder
// P's lane, finding 1, the lead's ruling).
//
// THE LAW: a document light that names an IES profile no file holds is pushed ONCE and refused ONCE, however many times
// the mirror syncs, until the document's value changes; then it is pushed again. The engine's refusal of that value is
// FINAL (OgreScene::setLight refuses the same request again in the same words without a retry): the mirror records the
// push as made-and-refused, never hands its latch back for a retry.
// THE DEFECT IT GUARDS: e1 made Scene::setLight answer false on a missing profile; the mirror's entryRefusal handed the
// latch back and marked the node changed (scenemirror.cpp entryRefusal), so every sync re-pushed the same light, refused
// again: a published refusal, a warning, a node re-walk and the whole setLight each sync, for as long as the file is
// missing.
// THE CONTROL: the refusals counted are above zero (the first push is refused), and a CHANGED value is pushed again.
// NULL render system, offscreen platform: no display, no card.
#include <QGuiApplication>
#include <cstdio>
#include <string>

#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Published.h"
#include "../support/documentgraph.h"

static int gFailures = 0;
static void check(bool ok, const std::string &what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
    std::fflush(stdout);
    if (!ok) ++gFailures;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    enginetest::DocumentGraph graph("test_mirror_final_refusal-ogre.log");   // FIRST: destroyed last
    QGuiApplication app(argc, argv);
    if (!graph.require()) return 1;
    jahshaka::engine::Engine *e = graph.engine();
    jahshaka::engine::Scene *target = e->createScene("final-refusal");
    check(target != nullptr, "a NULL-engine scene");
    if (!target) return 1;

    auto doc = iris::Scene::create();
    auto lamp = iris::LightNode::create();
    lamp->setName("lamp");
    lamp->setLightType(iris::LightType::Spot);
    lamp->iesProfilePath = QStringLiteral("/no/such/dir/missing.ies");
    doc->getRootNode()->addChild(lamp);

    const auto refused = [e] { return e->published().input.messagesRefused; };
    {
        SceneMirror mirror(target);
        mirror.setSource(doc);
        const unsigned long long before = refused();
        const int syncs = 8;
        for (int i = 0; i < syncs; ++i) {
            mirror.sync();
            enginetest::frozenFrames(e, 1);
        }
        const unsigned long long same = refused() - before;
        std::printf("      %d syncs of a spot light naming a missing .ies: %llu refusals\n", syncs, same);
        check(same == 1, "a refusal the engine will give again is taken ONCE over " + std::to_string(syncs) +
                             " syncs (" + std::to_string(same) + "); THE CONTROL: it is above zero");

        // a CHANGED value (the user fixes the path to another missing one, then clears it) is pushed again
        lamp->iesProfilePath = QStringLiteral("/no/such/dir/other.ies");
        lamp->markChanged(iris::NodeChange::Params);
        const unsigned long long beforeChange = refused();
        for (int i = 0; i < 3; ++i) {
            mirror.sync();
            enginetest::frozenFrames(e, 1);
        }
        const unsigned long long changed = refused() - beforeChange;
        std::printf("      the path changed, 3 syncs: %llu refusals\n", changed);
        check(changed == 1, "a CHANGED value is pushed again, and refused once (" + std::to_string(changed) + ")");
    }
    e->destroyScene(target);
    std::printf("%s\n", gFailures == 0 ? "mirror.final_refusal: PASSED" : ("mirror.final_refusal: FAILED (" +
                                                                         std::to_string(gFailures) + ")").c_str());
    return gFailures == 0 ? 0 : 1;
}
