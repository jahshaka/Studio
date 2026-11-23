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
// AND THE PICTURE STAYS (the lead's ruling C1): the engine's refusal is atomic, so the mirror pushes the lamp once more
// WITHOUT the profile and the emitter WITHOUT the ramp - the lamp exists and its edits reach it, the emitter is built.
// NULL render system, offscreen platform: no display, no card.
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <cstdint>
#include <cstdio>
#include <string>

#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
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

        // THE LAMP STAYS IN THE PICTURE (the lead's ruling C1): its profile refused, the lamp exists - the scene's light
        // serial (Scene::published().lightWriteSerial: setLight advances it, a refused setLight does not) moved from 0 -
        // and an edit of its intensity REACHES it (the serial moves again), the refused path still named, no new refusal
        const std::uint64_t lit = target->published().lightWriteSerial;
        lamp->intensity = 5.0f;
        lamp->markChanged(iris::NodeChange::Params);
        const unsigned long long beforeEdit = refused();
        for (int i = 0; i < 2; ++i) {
            mirror.sync();
            enginetest::frozenFrames(e, 1);
        }
        const std::uint64_t edited = target->published().lightWriteSerial;
        std::printf("      the lamp's light serial after its refusals: %llu; after an intensity edit: %llu (%llu refusals)\n",
                    (unsigned long long)lit, (unsigned long long)edited, refused() - beforeEdit);
        check(lit > 0 && edited > lit && refused() == beforeEdit,
              "A MISSING .ies DOES NOT TAKE THE LAMP OUT: the lamp exists (light serial " + std::to_string(lit) +
                  ") and an intensity edit reaches it (" + std::to_string(edited) + "), no refusal said again");

        // ...AND A MISSING RAMP DOES NOT TAKE THE EMITTER DOWN: an emitter naming a ramp image no file holds is built
        // (Scene::published().particleDefinitionsCreated 0 -> 1), the refusal said once
        const unsigned defs0 = target->published().particleDefinitionsCreated;
        auto emitter = iris::ParticleSystemNode::create();
        emitter->setName("emitter");
        emitter->colourRampImage = QStringLiteral("/no/such/dir/ramp.png");
        doc->getRootNode()->addChild(emitter);
        const unsigned long long beforeEmitter = refused();
        for (int i = 0; i < 4; ++i) {
            mirror.sync();
            enginetest::frozenFrames(e, 1);
        }
        const unsigned defs1 = target->published().particleDefinitionsCreated;
        const unsigned long long rampRefusals = refused() - beforeEmitter;
        std::printf("      an emitter naming a missing ramp, 4 syncs: definitions %u -> %u, %llu refusals\n", defs0, defs1,
                    rampRefusals);
        check(defs1 == defs0 + 1 && rampRefusals == 1,
              "A MISSING RAMP DOES NOT TAKE THE EMITTER DOWN: its definition built (" + std::to_string(defs0) + " -> " +
                  std::to_string(defs1) + "), the refusal said once (" + std::to_string(rampRefusals) + ")");

        // THE FALLBACK's OWN ANSWER DECIDES (the confirm's D2, the lead's ruling): an emitter with a PRESENT ramp image and
        // a broken texture (a file that is there and no image: the engine's load refuses, the emitter's texture is
        // unknown) is refused - NOT for its ramp: the fallback without the ramp is refused too, so nothing is latched.
        // The texture fixed, the WHOLE value is pushed, its ramp included. THE READ: the ramp is then cleared in the
        // document - a NEW topology (no ramp affector) builds one more definition (published particleDefinitionsCreated);
        // had the good ramp been latched as refused, the emitter would already be ramp-less and nothing would build
        const QString dir = QDir::currentPath();
        QImage(4, 1, QImage::Format_RGBA8888).save(dir + "/final_refusal_ramp.png");
        QImage(2, 2, QImage::Format_RGBA8888).save(dir + "/final_refusal_texture.png");
        if (QFile bad(dir + "/final_refusal_broken.png"); bad.open(QIODevice::WriteOnly)) bad.write("\x89PNG\r\n\x1a\n not an image");
        auto broken = iris::Texture2D::create(QImage());
        broken->source = dir + "/final_refusal_broken.png";
        auto good = iris::Texture2D::create(QImage());
        good->source = dir + "/final_refusal_texture.png";
        auto ramped = iris::ParticleSystemNode::create();
        ramped->setName("ramped");
        ramped->colourRampImage = dir + "/final_refusal_ramp.png";
        ramped->texture = broken;
        doc->getRootNode()->addChild(ramped);
        for (int i = 0; i < 3; ++i) {
            mirror.sync();
            enginetest::frozenFrames(e, 1);
        }
        ramped->texture = good;
        ramped->markChanged(iris::NodeChange::Params);
        for (int i = 0; i < 3; ++i) {
            mirror.sync();
            enginetest::frozenFrames(e, 1);
        }
        const unsigned withRamp = target->published().particleDefinitionsCreated;
        ramped->colourRampImage.clear();
        ramped->markChanged(iris::NodeChange::Params);
        for (int i = 0; i < 3; ++i) {
            mirror.sync();
            enginetest::frozenFrames(e, 1);
        }
        const unsigned rampless = target->published().particleDefinitionsCreated;
        std::printf("      a present ramp and a broken texture, the texture fixed, then the ramp cleared: definitions %u -> %u\n",
                    withRamp, rampless);
        check(rampless == withRamp + 1,
              "A REFUSAL NOT FOR THE FILE LATCHES NOTHING: a present ramp kept through a texture's refusal (the fallback "
              "refused too); the texture fixed, the emitter pushed WITH its ramp - clearing the ramp builds a new "
              "definition (" + std::to_string(withRamp) + " -> " + std::to_string(rampless) + ")");

        // THE FALLBACK READS NO FREED NODE (the confirm's D1): a lamp naming a missing .ies pushed, then removed and FREED
        // before the next sync - its refusal arrives at that sync's top, before the mirror releases the entry; the
        // fallback was built while the node lived and reads nothing of it (a use-after-free here is shown under
        // AddressSanitizer: the red-once arm). THE CONTROL: the sync completes and the row goes on
        {
            auto doomed = iris::LightNode::create();
            doomed->setName("doomed");
            doomed->setLightType(iris::LightType::Point);
            doomed->iesProfilePath = QStringLiteral("/no/such/dir/doomed.ies");
            doc->getRootNode()->addChild(doomed);
            mirror.sync();
            enginetest::frozenFrames(e, 1);
            doc->getRootNode()->removeChild(doomed);
            doomed.reset();   // freed: no reference left (no undo stack here)
        }
        for (int i = 0; i < 2; ++i) {
            mirror.sync();
            enginetest::frozenFrames(e, 1);
        }
        check(true, "A FREED LAMP's REFUSAL: the sync that reads it completes (AddressSanitizer is the arm that sees a read)");
    }
    e->destroyScene(target);
    std::printf("%s\n", gFailures == 0 ? "mirror.final_refusal: PASSED" : ("mirror.final_refusal: FAILED (" +
                                                                         std::to_string(gFailures) + ")").c_str());
    return gFailures == 0 ? 0 : 1;
}
