// Scene cameras, document half (CAMERAS_SPEC phase 1).
//
// Everything here is pure iris:: — no GL, no engine, no app. What it gates:
//
//  1. THE TYPE-ENUM FIX. CameraNode's constructor sets SceneNodeType::Camera.
//     For the whole life of the codebase it did not, so every switch on the
//     enum read a camera as `Empty`; the exporters had to recognise cameras by
//     dynamic_cast, cameras could not be serialized as themselves, and a
//     camera added to a scene was registered as nothing. The regression points
//     that follow from the fix are asserted here and in the play-routing
//     suite: Scene::addNode registers it, Scene::removeNode unregisters it and
//     drops the active-camera choice with it, and the export walker classifies
//     it off the enum.
//
//  2. THE LENS BINDING. `angle` (vertical degrees) and `focalLength` (mm) are
//     ONE value seen two ways, bound through the sensor HEIGHT. Both
//     directions are asserted exactly, including the round trip and the
//     authorMode rule that decides which of the two survives a sensor change.
//
//  3. REFLECTION AND DUPLICATION. Every §2 field is readable and writable
//     through get/setPropertyValue (which is what makes them keyable and
//     scriptable), and every one of them is copied by duplicate().
//
// The serialization round trip is gated end-to-end by the cameras.e2e suite
// (the real writer, the real reader, a real save/close/open) rather than
// re-implemented here against a stub.
//
// Runs under QT_QPA_PLATFORM=offscreen. Framework-free; non-zero exit on failure.

#include <QGuiApplication>
#include <QVariant>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "irisgl/irisglfwd.h"
#include "jahshaka/engine/Types.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/core/properties/property.h"
#include "irisgl/document/scenegraph/cameralens.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "export/walkers/scenewalker.h"

#include "../support/documentgraph.h"
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); \
    else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

/// The binding, written out independently of the implementation: the vertical
/// angle a lens of `mm` subtends on a sensor `h` mm high.
static float angleFor(float mm, float h)
{
    return float(2.0 * std::atan(double(h) / (2.0 * double(mm))) * 180.0 / M_PI);
}
static float mmFor(float angleDeg, float h)
{
    return float(double(h) / (2.0 * std::tan(double(angleDeg) * M_PI / 180.0 / 2.0)));
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    // v1 INTERIM (SPECS/SCENEGRAPH_SPEC.md §3): a document node IS an engine
    // node now, so even a document-only suite needs an engine. Declared here,
    // before anything builds a document, and destroyed last.
    enginetest::DocumentGraph graph("cameras-document-ogre.log");
    if (!graph.require()) return 1;
    // ---- 1. the type enum -------------------------------------------------
    {
        auto cam = iris::CameraNode::create();
        CHECK(cam->getSceneNodeType() == iris::SceneNodeType::Camera,
              "CameraNode SETS SceneNodeType::Camera (the trap CAMERAS_SPEC §1 names)");
        CHECK(cam->sceneNodeType == iris::SceneNodeType::Camera,
              "the public field agrees with the getter");
        // The export walker's classification is the sweep site with the most
        // to lose: it used to recognise cameras by dynamic_cast BECAUSE of the
        // missing enum, and the glTF exporter static_casts on its verdict.
        CHECK(exportwalk::classifyNode(cam.staticCast<iris::SceneNode>()) ==
                  exportwalk::NodeKind::Camera,
              "the export walker classifies a camera off the enum (no dynamic_cast)");
        auto plain = iris::SceneNode::create();
        CHECK(exportwalk::classifyNode(plain) == exportwalk::NodeKind::Empty,
              "a plain node is still Empty (the sweep did not widen anything)");
        // Cameras must be first-class editor objects: deletable, duplicable,
        // pickable (the BODY that gets picked arrives in phase 2).
        CHECK(cam->isDuplicable() && cam->isRemovable() && cam->isPickable(),
              "a camera is duplicable, removable and pickable");
    }

    // ---- 2. defaults ------------------------------------------------------
    {
        auto cam = iris::CameraNode::create();
        CHECK(near(cam->sensorWidth, 36.0f) && near(cam->sensorHeight, 24.0f),
              "the sensor defaults to 36 x 24 mm (full frame)");
        CHECK(near(cam->angle, 45.0f), "the angle default is unchanged at 45 vertical degrees");
        CHECK(cam->authorMode == iris::CameraAuthorMode::Degrees, "authored in degrees by default");
        CHECK(!cam->constrainAspect && !cam->dofEnabled, "constrainAspect and DOF default off");
        CHECK(cam->focusMode == iris::CameraFocusMode::Manual && near(cam->focusDistance, 10.0f) &&
                  near(cam->fStop, 2.8f) && cam->focusTarget.isEmpty(),
              "focus defaults: manual, 10 m, f/2.8, no target");
        CHECK(cam->outputHeight == 1080 && cam->bodyVisible,
              "output height 1080 and the body visible by default");
    }

    // ---- 3. the lens binding, both directions, exactly --------------------
    {
        auto cam = iris::CameraNode::create();
        // 45 degrees on a 24 mm-high sensor is ~28.9706 mm.
        CHECK(near(cam->focalLength(), mmFor(45.0f, 24.0f), 1e-3f),
              "focalLength() derives the mm the 45 degree default subtends");
        CHECK(near(cam->focalLength(), 28.9706f, 1e-3f),
              "and that number is 28.9706 mm (36x24, 45 deg)");

        // mm -> degrees
        cam->setFocalLength(50.0f);
        CHECK(near(cam->angle, angleFor(50.0f, 24.0f), 1e-3f),
              "setFocalLength(50) sets the angle a 50 mm lens subtends");
        CHECK(near(cam->angle, 26.9915f, 1e-3f), "and that angle is 26.9915 degrees");
        CHECK(cam->authorMode == iris::CameraAuthorMode::Millimeters,
              "setFocalLength flips authorMode to millimetres");
        CHECK(near(cam->focalLength(), 50.0f, 1e-3f), "and reading it back gives 50 mm exactly");

        // degrees -> mm
        cam->setFieldOfViewDegrees(90.0f);
        CHECK(near(cam->focalLength(), mmFor(90.0f, 24.0f), 1e-3f),
              "setFieldOfViewDegrees(90) moves the focal length with it");
        CHECK(near(cam->focalLength(), 12.0f, 1e-3f), "90 vertical degrees on 24 mm is a 12 mm lens");
        CHECK(cam->authorMode == iris::CameraAuthorMode::Degrees,
              "setFieldOfViewDegrees flips authorMode back to degrees");

        // A direct write to `angle` — what the scene reader and the camera
        // controllers do — cannot desynchronise the pair, because the mm are
        // derived and never stored.
        cam->angle = 60.0f;
        CHECK(near(cam->focalLength(), mmFor(60.0f, 24.0f), 1e-3f),
              "a RAW write to angle still reports the matching focal length");

        // Round trips, both ways, to float precision.
        for (float mm : { 8.0f, 14.0f, 24.0f, 35.0f, 50.0f, 85.0f, 200.0f }) {
            cam->setFocalLength(mm);
            const float back = cam->focalLength();
            if (!near(back, mm, 1e-2f)) {
                printf("FAIL: focal length round trip %.3f -> %.3f\n", mm, back);
                ++failures;
            }
        }
        printf("ok:   focal length round-trips for 8..200 mm\n");
        for (float deg : { 5.0f, 20.0f, 45.0f, 90.0f, 140.0f }) {
            cam->setFieldOfViewDegrees(deg);
            const float back = angleFor(cam->focalLength(), cam->sensorHeight);
            if (!near(back, deg, 1e-2f)) {
                printf("FAIL: angle round trip %.3f -> %.3f\n", deg, back);
                ++failures;
            }
        }
        printf("ok:   angle round-trips for 5..140 degrees\n");
    }

    // ---- 4. authorMode decides what a sensor change preserves -------------
    {
        auto degrees = iris::CameraNode::create();
        degrees->setFieldOfViewDegrees(45.0f);         // authored in degrees
        degrees->setSensorSize(36.0f, 12.0f);          // half-height sensor
        CHECK(near(degrees->angle, 45.0f),
              "degrees-authored: a sensor change KEEPS the framing");
        CHECK(near(degrees->focalLength(), mmFor(45.0f, 12.0f), 1e-3f),
              "degrees-authored: and moves the focal length instead");

        auto mm = iris::CameraNode::create();
        mm->setFocalLength(50.0f);                      // authored in mm
        const float wasAngle = mm->angle;
        mm->setSensorSize(36.0f, 12.0f);
        CHECK(near(mm->focalLength(), 50.0f, 1e-3f),
              "mm-authored: a sensor change KEEPS the lens");
        CHECK(!near(mm->angle, wasAngle) && near(mm->angle, angleFor(50.0f, 12.0f), 1e-3f),
              "mm-authored: and narrows the angle to match the smaller sensor");

        auto guard = iris::CameraNode::create();
        guard->setSensorSize(0.0f, 24.0f);
        CHECK(near(guard->sensorWidth, 36.0f), "a zero sensor dimension is ignored, not stored");
        const float before = guard->angle;
        guard->setFocalLength(0.0f);
        CHECK(near(guard->angle, before), "a zero focal length is ignored, not stored");
    }

    // ---- 5. reflection: every §2 field, read and written -------------------
    {
        auto cam = iris::CameraNode::create();
        auto props = cam->getProperties();
        QStringList names;
        for (auto *p : props) names << p->name;
        for (auto *p : props) delete p;
        for (const char *want : { "angle", "focalLength", "sensorWidth", "sensorHeight",
                                  "authorMode", "constrainAspect", "dofEnabled", "focusMode",
                                  "focusDistance", "fStop", "outputHeight", "bodyVisible",
                                  "aspectRatio", "nearClip", "farClip", "orthoSize", "projMode" }) {
            if (!names.contains(QString::fromLatin1(want))) {
                printf("FAIL: getProperties() is missing '%s'\n", want);
                ++failures;
            }
        }
        printf("ok:   getProperties() reflects every keyable camera setting\n");

        // Writes go through the setters, so the lens pair stays bound.
        CHECK(cam->setPropertyValue("focalLength", 35.0f), "setPropertyValue(focalLength)");
        CHECK(near(cam->angle, angleFor(35.0f, 24.0f), 1e-3f),
              "writing focalLength as a PROPERTY moves the angle too");
        CHECK(cam->getPropertyValue("focalLength").toFloat() > 34.9f,
              "and getPropertyValue reports the derived millimetres");
        CHECK(cam->setPropertyValue("sensorHeight", 18.0f) && near(cam->sensorHeight, 18.0f),
              "setPropertyValue(sensorHeight)");
        CHECK(near(cam->focalLength(), 35.0f, 1e-3f),
              "sensorHeight through reflection honours authorMode (mm kept)");

        CHECK(cam->setPropertyValue("constrainAspect", true) && cam->constrainAspect,
              "setPropertyValue(constrainAspect)");
        CHECK(cam->setPropertyValue("dofEnabled", true) && cam->dofEnabled,
              "setPropertyValue(dofEnabled)");
        CHECK(cam->setPropertyValue("focusMode", int(iris::CameraFocusMode::Track)) &&
                  cam->focusMode == iris::CameraFocusMode::Track,
              "setPropertyValue(focusMode)");
        CHECK(cam->setPropertyValue("focusDistance", 4.5f) && near(cam->focusDistance, 4.5f),
              "setPropertyValue(focusDistance)");
        CHECK(cam->setPropertyValue("focusDistance", -3.0f) && near(cam->focusDistance, 0.0f),
              "a negative focus distance clamps to zero rather than being stored");
        CHECK(cam->setPropertyValue("focusTarget", QStringLiteral("some-guid")) &&
                  cam->focusTarget == QLatin1String("some-guid"),
              "focusTarget is reachable through reflection (it is not a keyable ROW: guids "
              "have no widget and nothing to interpolate)");
        CHECK(cam->setPropertyValue("fStop", 1.4f) && near(cam->fStop, 1.4f),
              "setPropertyValue(fStop)");
        CHECK(cam->setPropertyValue("outputHeight", 2160) && cam->outputHeight == 2160,
              "setPropertyValue(outputHeight)");
        CHECK(cam->setPropertyValue("outputHeight", 0) && cam->outputHeight == 1,
              "outputHeight 0 clamps to 1 (a zero-pixel render is not a render)");
        CHECK(cam->setPropertyValue("bodyVisible", false) && !cam->bodyVisible,
              "setPropertyValue(bodyVisible)");
        CHECK(cam->setPropertyValue("authorMode", int(iris::CameraAuthorMode::Degrees)) &&
                  cam->authorMode == iris::CameraAuthorMode::Degrees,
              "setPropertyValue(authorMode)");
        CHECK(!cam->getPropertyValue("noSuchCameraSetting").isValid(),
              "an unknown key still reads as invalid (node.setProperty's refusal path)");
    }

    // ---- 6. duplicate carries every field ---------------------------------
    {
        auto cam = iris::CameraNode::create();
        cam->setName("Hero Cam");
        cam->setLocalPos(iris::Vec3(1, 2, 3));
        cam->setSensorSize(22.3f, 14.9f);              // APS-C
        cam->setFocalLength(35.0f);                    // and authored in mm
        cam->nearClip = 0.25f;
        cam->farClip = 900.0f;
        cam->aspectRatio = 2.39f;
        cam->setOrthagonalZoom(7.5f);
        cam->setProjection(iris::CameraProjection::Orthogonal);
        cam->vrViewScale = 3.5f;
        cam->constrainAspect = true;
        cam->dofEnabled = true;
        cam->focusMode = iris::CameraFocusMode::Track;
        cam->focusDistance = 6.25f;
        cam->focusTarget = QStringLiteral("target-guid");
        cam->fStop = 1.8f;
        cam->outputHeight = 2160;
        cam->bodyVisible = false;

        auto dupNode = cam->duplicate();
        CHECK(!dupNode.isNull(), "a camera duplicates");
        CHECK(dupNode->getSceneNodeType() == iris::SceneNodeType::Camera,
              "the duplicate is a camera too");
        auto dup = dupNode.staticCast<iris::CameraNode>();
        CHECK(dup->getGUID() != cam->getGUID(), "with a fresh guid");
        CHECK(near(dup->angle, cam->angle) && near(dup->focalLength(), cam->focalLength()),
              "duplicate: the lens (angle and the mm it derives)");
        CHECK(near(dup->sensorWidth, 22.3f) && near(dup->sensorHeight, 14.9f),
              "duplicate: the sensor");
        CHECK(dup->authorMode == iris::CameraAuthorMode::Millimeters, "duplicate: authorMode");
        CHECK(near(dup->nearClip, 0.25f) && near(dup->farClip, 900.0f) &&
                  near(dup->aspectRatio, 2.39f) && near(dup->orthoSize, 7.5f),
              "duplicate: clip planes, aspect, ortho size");
        CHECK(dup->projMode == iris::CameraProjection::Orthogonal && !dup->isPerspective,
              "duplicate: the projection PAIR (projMode and isPerspective in lock-step — the "
              "old createDuplicate copied neither, so an orthographic camera duplicated "
              "perspective)");
        CHECK(near(dup->vrViewScale, 3.5f),
              "duplicate: vrViewScale (silently dropped before this change)");
        CHECK(dup->constrainAspect && dup->dofEnabled, "duplicate: constrainAspect + dofEnabled");
        CHECK(dup->focusMode == iris::CameraFocusMode::Track && near(dup->focusDistance, 6.25f) &&
                  dup->focusTarget == QLatin1String("target-guid") && near(dup->fStop, 1.8f),
              "duplicate: the whole focus block");
        CHECK(dup->outputHeight == 2160 && !dup->bodyVisible,
              "duplicate: output height and bodyVisible");
    }

    // ---- 7. the scene registry and the active camera ----------------------
    {
        auto scene = iris::Scene::create();
        CHECK(scene->cameras.isEmpty() && scene->getActiveCameraGuid().isEmpty(),
              "a fresh scene has no cameras and no active camera");

        auto a = iris::CameraNode::create(); a->setName("A");
        auto b = iris::CameraNode::create(); b->setName("B");
        scene->getRootNode()->addChild(a);
        scene->getRootNode()->addChild(b);
        CHECK(scene->cameras.size() == 2,
              "Scene::addNode registers scene-graph cameras (only reachable since the type fix)");
        CHECK(scene->cameras.contains(a->getGUID()) && scene->cameras.contains(b->getGUID()),
              "both cameras are keyed by guid");

        CHECK(!scene->setActiveCamera(QStringLiteral("not-a-camera")),
              "setActiveCamera REFUSES a guid that is not a camera in this scene");
        CHECK(scene->getActiveCameraGuid().isEmpty(), "and leaves the choice alone");
        CHECK(scene->setActiveCamera(a->getGUID()) && scene->getActiveCamera() == a,
              "setActiveCamera(a) resolves to the node");
        CHECK(scene->setActiveCamera(QString()) && scene->getActiveCamera().isNull(),
              "an empty guid clears it (back to the free viewer)");

        scene->setActiveCamera(b->getGUID());
        scene->getRootNode()->removeChild(b);
        CHECK(!scene->cameras.contains(b->getGUID()), "removing a camera unregisters it");
        CHECK(scene->getActiveCameraGuid().isEmpty(),
              "deleting the ACTIVE camera falls back to the free viewer rather than leaving "
              "play pointed at a guid that resolves to nothing");
        CHECK(scene->getActiveCamera().isNull(), "and getActiveCamera() reports null");

        CHECK(!scene->isPlaying(),
              "a scene is not playing by default (the flag applyCamera reads)");
        scene->setPlaying(true);
        CHECK(scene->isPlaying(), "setPlaying(true) is visible to the mirror");
    }

    // ---- 8. lookAt is rotation only ---------------------------------------
    {
        auto cam = iris::CameraNode::create();
        cam->setLocalPos(iris::Vec3(0, 0, 5));
        cam->update(0.0f);
        cam->lookAt(iris::Vec3(0, 0, 0));
        cam->update(0.0f);
        const iris::Vec3 pos = cam->getLocalPos();
        CHECK(near(pos.x(), 0.0f) && near(pos.y(), 0.0f) && near(pos.z(), 5.0f),
              "lookAt leaves the camera where it is");
        // Looking down -Z from +Z at the origin is the identity orientation.
        const iris::Vec3 forward =
            (cam->getGlobalTransform() * iris::Vec4(0, 0, -1, 0)).toVector3D().normalized();
        CHECK(near(forward.z(), -1.0f, 1e-3f),
              "and points its -Z axis at the target");
    }

    // ---- 9. THE WIDE-ASPECT FRAMING HOLD ---------------------------------
    // (owner report 2026-09-07; the policy RE-SCOPED 2026-09-08 after the
    // first version shipped a fixed 95-degree horizontal cap and zoomed every
    // wide-lens scene in — the pre-2026-09-09 Grand Showroom's 75-degree camera (re-staged to 45° since) rendered at
    // 63 degrees vertical on any monitor wider than 3:2, which the owner saw as
    // "imported assets have the wrong scale".)
    //
    // THE POLICY, as a pure function: hold the 16:9 framing beyond 16:9. `angle`
    // is VERTICAL, so the horizontal angle of view grows with the target's
    // aspect and an ultra-wide window fisheyes a free camera (45 vertical is
    // 118 degrees wide at 32:9). ABOVE the framing aspect the vertical angle
    // narrows just enough to keep the horizontal extent the shot has AT that
    // aspect; at or below it nothing happens at all.
    //
    // The load-bearing half is what the hold does NOT do: at and below 16:9 it
    // returns the authored angle BIT-IDENTICALLY, for EVERY lens — which is
    // what lets every existing pixel suite stay byte-exact, and is precisely
    // what a degree cap could not promise.
    {
        using jahshaka::engine::verticalFovForFramingAspect;
        const float hold = 16.0f / 9.0f;
        const float authored = 45.0f;
        const auto hfov = [](float vfovDeg, float aspect) {
            const float k = float(M_PI) / 180.0f;
            return 2.0f * std::atan(std::tan(vfovDeg * 0.5f * k) * aspect) / k;
        };

        // Off is the identity, in every spelling.
        CHECK(verticalFovForFramingAspect(authored, 1.777f, 0.0f) == authored,
              "framing hold: a zero hold aspect is the identity, bit for bit");
        CHECK(verticalFovForFramingAspect(authored, 0.0f, hold) == authored,
              "framing hold: a zero target aspect is the identity");

        // ORDINARY WINDOWS, EVERY LENS: UNTOUCHED, and the equality is exact
        // (==, not near). This is the assertion the 95-degree cap could not
        // make: it held for a 45-degree lens and failed for a 75-degree one.
        const float normal[] = { 1.0f, 4.0f/3.0f, 3.0f/2.0f, 16.0f/10.0f, 16.0f/9.0f };
        const float lenses[] = { 30.0f, 45.0f, 60.0f, 70.0f, 75.0f, 90.0f };
        bool identical = true;
        for (float lens : lenses) {
            for (float a : normal) {
                const float out = verticalFovForFramingAspect(lens, a, hold);
                if (out != lens) {
                    identical = false;
                    printf("    lens %.0f at aspect %.3f -> %.6f\n", lens, a, out);
                }
            }
        }
        CHECK(identical, "framing hold: 1:1 through 16:9 return the authored angle "
                         "BIT-IDENTICALLY, for lenses 30..90");
        printf("    16:9  hfov = %.2f deg -> vfov stays %.2f (45-degree lens)\n",
               hfov(authored, 16.0f/9.0f), verticalFovForFramingAspect(authored, 16.0f/9.0f, hold));

        // THE SWEEP. Below the hold nothing moves; above it the HORIZONTAL
        // extent is held at exactly the 16:9 one and the vertical angle narrows
        // to do it. MEASURED, so the sweep also documents what the free camera
        // now does on the panels people actually own: 21:9 keeps 72.7 degrees
        // of horizontal view (not 88), 32:9 keeps 72.7 (not 112).
        const float sweep[] = { 4.0f/3.0f, 16.0f/9.0f, 2.0f, 21.0f/9.0f, 2.4f,
                                32.0f/9.0f, 4.0f, 5.0f };
        const float heldHfov = hfov(authored, hold);
        bool ok = true;
        for (float a : sweep) {
            const float before = hfov(authored, a);
            const float v = verticalFovForFramingAspect(authored, a, hold);
            const float after = hfov(v, a);
            printf("    aspect %.3f: hfov %.2f -> %.2f deg (vfov %.2f -> %.2f)%s\n",
                   a, before, after, authored, v, a > hold ? "  HELD" : "");
            if (a <= hold) {
                if (v != authored) ok = false;      // the identity, exactly
            } else {
                if (!near(after, heldHfov, 0.01f)) ok = false;
                if (!(v < authored) || !(v > 1.0f)) ok = false;
            }
        }
        CHECK(ok, "framing hold: at or below 16:9 nothing moves; above it the HORIZONTAL "
                  "extent is held at the 16:9 one and the vertical angle narrows to do it");

        // Monotonic: wider target, narrower vertical angle. No inversions.
        float prev = 1e9f;
        bool monotonic = true;
        for (float a = 1.0f; a <= 6.0f; a += 0.25f) {
            const float v = verticalFovForFramingAspect(authored, a, hold);
            if (v > prev + 1e-4f) monotonic = false;
            prev = v;
        }
        CHECK(monotonic, "framing hold: the vertical angle never widens as the target widens");

        // An AUTHORED wide lens is still held when a FREE camera carries it,
        // and left alone when the hold is off — which is the whole rule the
        // mirror enforces by passing 0 for authored cameras.
        CHECK(verticalFovForFramingAspect(90.0f, 32.0f/9.0f, 0.0f) == 90.0f,
              "framing hold: an authored 90-degree lens at 32:9 is untouched with the hold off");
        CHECK(verticalFovForFramingAspect(90.0f, 32.0f/9.0f, hold) < 90.0f,
              "framing hold: ...and IS narrowed when a free camera carries it");

        // THE DOCUMENT AND THE ENGINE ARE ONE POLICY. Two implementations —
        // double-precision lens helpers here, float trig in the engine header —
        // that must never disagree about what is on screen, because one draws
        // the frame and the other casts the pick ray through it.
        float worst = 0.0f;
        for (float lens : lenses) {
            for (float a = 1.0f; a <= 6.0f; a += 0.1f) {
                const float d = iris::lens::verticalFovDegForFramingAspect(lens, a, hold);
                const float e = verticalFovForFramingAspect(lens, a, hold);
                worst = std::max(worst, std::fabs(d - e));
            }
        }
        printf("    document vs engine, 300 (lens, aspect) pairs: worst delta %.3e deg\n", worst);
        CHECK(worst < 1e-4f, "framing hold: document and engine agree to 1e-4 degrees everywhere");

        // ---- THE PROJECTION IS BIT-IDENTICAL AT ORDINARY ASPECTS ----------
        // The function above is the policy; THIS is the promise the rest of the
        // app depends on. A free camera (hold set) and an authored one (hold
        // off) must produce the same projection MATRIX, byte for byte, at every
        // ordinary aspect and every lens — that is what makes "the cap made my
        // scene zoom in" impossible to reintroduce.
        {
            bool bitIdentical = true;
            for (float lens : lenses) {
                for (float a : normal) {
                    auto plain = iris::CameraNode::create();
                    auto free  = iris::CameraNode::create();
                    for (auto &c : { plain, free }) {
                        c->angle = lens;
                        c->nearClip = 0.1f;
                        c->farClip = 1000.0f;
                        c->setAspectRatio(a);
                    }
                    free->setFramingAspect(hold);
                    plain->updateCameraMatrices();
                    free->updateCameraMatrices();
                    if (std::memcmp(&plain->projMatrix, &free->projMatrix, sizeof(iris::Mat4)) != 0) {
                        bitIdentical = false;
                        printf("    MOVED: lens %.0f at aspect %.3f\n", lens, a);
                    }
                    if (free->effectiveFovDegrees() != lens) {
                        bitIdentical = false;
                        printf("    effective fov %.4f != authored %.0f at aspect %.3f\n",
                               free->effectiveFovDegrees(), lens, a);
                    }
                }
            }
            CHECK(bitIdentical,
                  "framing hold: 4:3 / 3:2 / 16:10 / 16:9 projections are BIT-IDENTICAL "
                  "with the hold set, for lenses 30..90 (the 2026-09-08 regression gate)");

            // ...and on a WIDER window it does engage, on the same camera, with
            // the AUTHORED angle left alone. The Grand Showroom's own numbers.
            auto showroom = iris::CameraNode::create();
            showroom->angle = 75.0f;
            showroom->setAspectRatio(2.4f);
            showroom->setFramingAspect(hold);
            showroom->updateCameraMatrices();
            printf("    Showroom lens 75 at 2.40:1 -> effective vfov %.2f (was 63.13 under the "
                   "95-degree cap, at EVERY aspect past 1.42:1)\n",
                   showroom->effectiveFovDegrees());
            CHECK(showroom->effectiveFovDegrees() < 75.0f && showroom->angle == 75.0f,
                  "framing hold: past 16:9 the rendered angle narrows and the AUTHORED one does not");
            showroom->setAspectRatio(16.0f / 9.0f);
            showroom->updateCameraMatrices();
            CHECK(showroom->effectiveFovDegrees() == 75.0f,
                  "framing hold: the SAME free camera renders its authored 75 degrees at 16:9 "
                  "(the owner-blocking defect: it rendered 63.13)");
        }
    }

    printf(failures == 0 ? "\nALL CAMERA DOCUMENT CHECKS PASSED\n"
                         : "\n%d CAMERA DOCUMENT CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
