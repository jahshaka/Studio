// document.light_defaults — SHADOWS ARE ON, AND THE SUN IS THE FIRST
// DIRECTIONAL LIGHT (SPECS/SUN_AND_LIGHT_DEFAULTS_SPEC.md, owner decisions
// Q1/Q1e and decision 1).
//
// FOUR CLAIMS, all document-side, all previously untested:
//
//  1. A NEW LIGHT OF ANY TYPE IS BORN CASTING. That has been ShadowMap's
//     constructor for years — and two places said otherwise behind the user's
//     back: the default scene forced its point light to None, and the file
//     READER returned None for a missing key, so the constructor and the reader
//     stated two different defaults for one question. This suite pins the
//     constructor AND the reader's fallback, which is the half that could bite
//     a hand-edited or imported file.
//
//  2. `Scene::shadowEnabled` IS INITIALISED. It was declared and never assigned
//     in the constructor (an uninitialised bool); every shipped creation path
//     happened to set it afterwards, so it was latent rather than live.
//
//  3. ONE SUN RESOLVER. `Scene::sunLight()` replaces three rules that could
//     each answer differently — the sky link's depth-first walk, the GI bounce
//     light's lowest-nodeId scan and Ogre's creation-order sort. The rules
//     asserted here are its whole contract: an explicit pin wins; otherwise the
//     lowest forwardShadingPriority; TIES break by nodeId, deterministically,
//     which is the case R6 in the spec says must be pinned rather than the
//     happy path; a scene with NO directional light answers "none" cleanly and
//     is a completely normal scene (two of the eight shipped samples).
//
//  4. THE AUTO-SLOT. `nextForwardShadingPriority()` hands the first directional
//     0 and each further one the lowest free number, so "add a second sun" can
//     never be decided by creation luck.
//
// Document only: the headless NULL render system, no display, no pixels.
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/shadowmap.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); \
    else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

iris::LightNodePtr addLight(const iris::ScenePtr &scene, iris::LightType type, const char *name)
{
    auto light = iris::LightNode::create();
    light->setLightType(type);
    light->setName(QString::fromLatin1(name));
    scene->getRootNode()->addChild(light, false);
    return light;
}

const char *typeName(iris::LightType t)
{
    switch (t) {
    case iris::LightType::Point: return "point";
    case iris::LightType::Directional: return "directional";
    case iris::LightType::Spot: return "spot";
    case iris::LightType::Area: return "area";
    }
    return "?";
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("document-light-defaults-ogre.log");
    if (!graph.require()) return 1;

    // ---- 1. a new light of every type is born casting ---------------------
    {
        printf("-- a new light casts soft shadows, whatever its type\n");
        for (iris::LightType t : { iris::LightType::Point, iris::LightType::Directional,
                                   iris::LightType::Spot, iris::LightType::Area }) {
            auto light = iris::LightNode::create();
            light->setLightType(t);
            char msg[128];
            std::snprintf(msg, sizeof msg, "a fresh %s light is born casting SOFT shadows",
                          typeName(t));
            CHECK(light->shadowMap && light->shadowMap->shadowType == iris::ShadowMapType::Soft,
                  msg);
            std::snprintf(msg, sizeof msg, "...at 2048 (a fresh %s light)", typeName(t));
            CHECK(light->shadowMap && light->shadowMap->resolution == 2048, msg);
        }
    }

    // ---- 2. the scene's master switch is initialised ----------------------
    {
        printf("-- the scene's shadow master switch\n");
        auto scene = iris::Scene::create();
        CHECK(scene->shadowEnabled, "a fresh scene has shadowEnabled TRUE (it was never assigned)");
    }

    // ---- 3. the reader's fallback, at the level the document owns ---------
    // The reader's own evalShadowMapType lives in Studio (src/io), so the
    // DOCUMENT half asserted here is the invariant it now agrees with: the
    // constructor's default, and that an explicit "none" is the only way to a
    // non-casting light. scripting.e2e.sun_light drives the reader end to end
    // through a real save/reopen.
    {
        printf("-- the off switch is explicit, never a default\n");
        auto light = iris::LightNode::create();
        light->setShadowMapType(iris::ShadowMapType::None);
        CHECK(light->getShadowMapType() == iris::ShadowMapType::None,
              "a light CAN be switched off — explicitly, and only explicitly");
        auto copy = light->createDuplicate().dynamicCast<iris::LightNode>();
        CHECK(copy && copy->getShadowMapType() == iris::ShadowMapType::None,
              "...and a duplicate keeps the author's choice");
    }

    // ---- 4. the sun: no directional light at all --------------------------
    {
        printf("-- a scene with no directional light\n");
        auto scene = iris::Scene::create();
        addLight(scene, iris::LightType::Point, "Lamp");
        addLight(scene, iris::LightType::Spot, "Spot");
        CHECK(scene->sunLight().isNull(), "no directional light -> there is no sun, cleanly");
        CHECK(scene->sunReason() == QLatin1String("none"), "...and the reason says so: 'none'");
        CHECK(scene->directionalLights().isEmpty(), "...the directional list is empty");
        CHECK(scene->secondaryDirectionals().isEmpty(), "...and so is the secondary list");
        CHECK(scene->nextForwardShadingPriority() == 0,
              "...and the first directional added here would take priority 0");
    }

    // ---- 5. the sun: one, two, three directionals -------------------------
    {
        printf("-- the sun is the FIRST directional light, by priority\n");
        auto scene = iris::Scene::create();
        // The order the editor uses: ask the SCENE for the next free number,
        // then build the light with it (SceneEditService::addDirectionalLight).
        const int firstSlot = scene->nextForwardShadingPriority();
        auto first = addLight(scene, iris::LightType::Directional, "Sun");
        first->forwardShadingPriority = firstSlot;
        CHECK(firstSlot == 0, "the first directional auto-slots to 0");
        CHECK(scene->sunLight() == first, "...and IS the sun");
        CHECK(scene->sunReason() == QLatin1String("priority"), "...chosen by priority");

        const int secondSlot = scene->nextForwardShadingPriority();
        auto second = addLight(scene, iris::LightType::Directional, "Moon");
        second->forwardShadingPriority = secondSlot;
        CHECK(secondSlot == 1, "a second directional auto-slots to 1");
        CHECK(scene->sunLight() == first, "...and the sun does not move");
        CHECK(scene->secondaryDirectionals().size() == 1 &&
                  scene->secondaryDirectionals().first() == second,
              "...the second is reported as a SECONDARY directional");

        const int thirdSlot = scene->nextForwardShadingPriority();
        auto third = addLight(scene, iris::LightType::Directional, "Fill");
        third->forwardShadingPriority = thirdSlot;
        CHECK(thirdSlot == 2, "a third auto-slots to 2");

        // The author changes the row: the lowest number wins, immediately.
        third->forwardShadingPriority = -5;   // clamped on the reflected setter; raw here
        third->forwardShadingPriority = 0;
        first->forwardShadingPriority = 3;
        CHECK(scene->sunLight() == third, "lowering a light's priority makes IT the sun");
        CHECK(scene->nextForwardShadingPriority() == 2,
              "...and the next free number is the lowest one nobody is using");
    }

    // ---- 6. the TIE, which is the case that used to be luck ---------------
    {
        printf("-- two directionals at the same priority resolve deterministically\n");
        auto scene = iris::Scene::create();
        auto a = addLight(scene, iris::LightType::Directional, "A");
        auto b = addLight(scene, iris::LightType::Directional, "B");
        a->forwardShadingPriority = 0;
        b->forwardShadingPriority = 0;
        const auto winner = scene->sunLight();
        CHECK(winner == (a->nodeId < b->nodeId ? a : b),
              "a TIE breaks by nodeId — creation order, not hash order");
        // Ten reads in a row, because the list comes out of a QHash whose
        // iteration order is arbitrary: the old resolvers' bug class exactly.
        bool stable = true;
        for (int i = 0; i < 10; ++i) stable = stable && scene->sunLight() == winner;
        CHECK(stable, "...and it answers the same every single time");
        CHECK(scene->secondaryDirectionals().size() == 1,
              "...with the loser listed as a secondary (which is what the toast names)");
    }

    // ---- 7. the pin beats the priority, and a dead pin falls back ---------
    {
        printf("-- an explicit pin\n");
        auto scene = iris::Scene::create();
        auto a = addLight(scene, iris::LightType::Directional, "A");
        auto b = addLight(scene, iris::LightType::Directional, "B");
        a->forwardShadingPriority = 0;
        b->forwardShadingPriority = 1;
        scene->sunLightGuid = b->getGUID();
        CHECK(scene->sunLight() == b, "a pinned light is the sun whatever the priorities say");
        CHECK(scene->sunReason() == QLatin1String("pinned"), "...and the reason says 'pinned'");
        CHECK(scene->secondaryDirectionals().size() == 1 &&
                  scene->secondaryDirectionals().first() == a,
              "...and the priority-0 light becomes the secondary");

        // A pin naming something that is not a live directional falls back to
        // the automatic answer rather than to nothing.
        scene->sunLightGuid = QStringLiteral("no-such-node");
        CHECK(scene->sunLight() == a, "a stale pin falls back to the priority order");
        CHECK(scene->sunReason() == QLatin1String("priority"), "...and says so");

        auto lamp = addLight(scene, iris::LightType::Point, "Lamp");
        scene->sunLightGuid = lamp->getGUID();
        CHECK(scene->sunLight() == a, "a pin naming a POINT light is ignored (only a "
                                      "directional can be the sun)");
    }

    // ---- 8. the sky steers the sun, and that is a separate switch ---------
    {
        printf("-- the sky's steering is its own switch\n");
        auto scene = iris::Scene::create();
        auto sun = addLight(scene, iris::LightType::Directional, "Sun");
        scene->skyType = iris::SkyType::REALISTIC;
        CHECK(!scene->skyDrivesSun, "a fresh scene's sky steers nothing");
        CHECK(!scene->applySunCoupling(), "...so the coupling is a no-op");
        const iris::Quat authored = sun->getGlobalRotation();
        scene->skyDrivesSun = true;
        CHECK(scene->applySunCoupling(), "turning the steering on aims the SUN");
        CHECK(sun->getGlobalRotation() != authored, "...and the light really moved");
        // ...and it aims whichever light IS the sun, not a light named in a
        // second field (the whole reason the two were split).
        auto other = addLight(scene, iris::LightType::Directional, "Other");
        other->forwardShadingPriority = -1;   // deliberately below the first
        const iris::Quat otherBefore = other->getGlobalRotation();
        scene->applySunCoupling();
        CHECK(other->getGlobalRotation() != otherBefore,
              "the steering follows the sun when the sun changes");
    }

    printf(failures ? "document.light_defaults: %d FAILURES\n"
                    : "document.light_defaults: all passed\n", failures);
    return failures ? 1 : 0;
}
