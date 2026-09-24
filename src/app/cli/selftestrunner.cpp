/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "app/cli/selftestrunner.h"

#include <cstdio>

#include <QApplication>
#include <QColor>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSize>
#include <QThread>
#include <QWidget>

#include "bridge/enginehost.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "scripting/scriptengine.h"
#include "shell/mainwindow.h"
#include "viewport/ieditorviewport.h"

namespace {

/// The default scene's built-in ground (MainWindow::createDefaultScene), found
/// the way the viewport and the scene-extents service recognise it.
iris::MeshNodePtr findDefaultGround(const iris::ScenePtr &scene)
{
    if (!scene || !scene->getRootNode()) return iris::MeshNodePtr();
    for (const auto &child : scene->getRootNode()->children()) {
        if (!child || child->getSceneNodeType() != iris::SceneNodeType::Mesh) continue;
        auto mesh = child.staticCast<iris::MeshNode>();
        if (mesh->isBuiltIn && mesh->meshPath == QStringLiteral(":/models/ground.obj")) return mesh;
    }
    return iris::MeshNodePtr();
}

/// The sha256 of the FILE, so it is the number `sha256sum <png>` prints — the
/// hash CLAUDE.md's law is stated in, and the one a lane A/Bs against a base
/// binary by hand.
QString fileSha256(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return QString();
    return QString::fromLatin1(h.result().toHex());
}

/// `out.png` -> `out.pose2.png` (and `out` -> `out.pose2.png`): the second pose
/// is a SECOND FILE beside the first, so the first one's bytes — and therefore
/// its hash — are exactly what they always were.
QString posePath(const QString &outPng)
{
    const QFileInfo fi(outPng);
    const QString suffix = fi.completeSuffix();
    const QString base = suffix.isEmpty() ? outPng : outPng.left(outPng.size() - suffix.size() - 1);
    return base + QStringLiteral(".pose2.") + (suffix.isEmpty() ? QStringLiteral("png") : suffix);
}

int countNodes(const iris::SceneNodePtr &node)
{
    if (!node) return 0;
    int n = 1;
    for (const auto &child : node->children()) n += countNodes(child);
    return n;
}

/// `out.png` -> `out.B1.png` — the same rule posePath() uses, for any tag.
QString taggedPath(const QString &outPng, const QString &tag)
{
    const QFileInfo fi(outPng);
    const QString suffix = fi.completeSuffix();
    const QString base = suffix.isEmpty() ? outPng : outPng.left(outPng.size() - suffix.size() - 1);
    return base + QStringLiteral(".") + tag + QStringLiteral(".")
           + (suffix.isEmpty() ? QStringLiteral("png") : suffix);
}

/// THE SHOT WAITS FOR THE GATHER'S SETTLED HISTORY (PHOTON-GATHER-1d). A shot is
/// its own offscreen view, and where the screen-probe gather runs that view has a
/// pixel history of its own that starts young — a two-frame shot would photograph
/// the raw estimate. So, exactly where the gather runs, the shot settles through
/// its own view until GiStatus::giAtRest (whose fourth term is that history) holds,
/// as editor.screenshot does. Where it does not run nothing is rendered and the
/// shot is the one it always was — which is what keeps the gather-off arm's four
/// lines byte-identical to the pre-gather picture.
void settleShotIfGathering(MainWindow &window)
{
    ScriptEngine *host = window.scripting();
    if (!host) return;
    const ScriptResult r =
        host->evaluate(QStringLiteral("world.giStatus().gather.running === true"),
                       QStringLiteral("selftest-gather-running"), true, 0, ScriptRunPolicy::Off);
    if (r.ok && r.value.toBool()) window.viewport()->settleGiBeforeNextScreenshot(2000);
}

// ---------------------------------------------------------------------------
// POSE PAIR B — A FENCE THAT CAN SEE LIGHTING (PHOTON phase A, A1 section 1.1;
// lane FENCE-1)
// ---------------------------------------------------------------------------
//
// WHY. Poses 1 and 2 are the DEFAULT SCENE. It has no reflective pixel, no
// cascade crossing, no emissive above 1.0 and no ray-traced anything, so not one
// defect the PHOTON build exists to fix — and not one regression it could
// introduce — can move either hash. Six lanes in a row have had to prove "the
// hashes are exact" about code whose subject the hashes cannot see.
//
// WHAT FIXTURE B IS, and every element is there for a term:
//   * an EMPTY project (never a shipped sample: a sample is content that moves
//     for content reasons, and a fence must not);
//   * the realistic sky + ONE directional light, which is the sun role — the
//     environment term and the direct term;
//   * a 60 x 60 m GLOSSY floor (metallic 1, roughness 0.2): the reflection
//     terms. The default Ground cannot take a reflective look, so it is a
//     `plane` primitive (DOCS/traps/ENGINE.md, "a moved camera is not a new
//     view" — the fixture that shows reflection defects is a glossy FLOOR at a
//     grazing angle with occluders standing on it);
//   * seven 1 x 3 x 1 m pillars across the view, of which #4 is EMISSIVE at
//     radiance 3.0 (above the voxel material store's 1.0 clip — VOXEL-CLIP-1's
//     witness in a hash) and #7 is a MIRROR (metallic 1, roughness 0);
//   * a matte white wall 12 x 4 m at z = -9, so a bounce off the sunlit floor
//     has a receiver BEYOND cascade 0's face (cascade 0's half-extent is 5 m at
//     every tier — `giQualityFacts`);
//   * `world.photon({enabled:true, tier:"high"})` and a camera at (0,5,14)
//     looking at (0,1,0): the view spans cascade 0's face, the floor mirrors the
//     pillars and the sky, the mirror pillar holds a reflection, the emissive
//     lights the floor.
//
// TWO HASHES, because the ray tier is half the renderer: B1 with rays, then
// `world.rayTracing("off")` and B2. On a machine with no ray query B1 == B2 is
// LEGAL and said so; otherwise identical hashes mean the rays did nothing.
//
// BUILT THROUGH THE SCRIPTING VERBS, in this process, API-first: the fixture is
// a sequence of verbs the editor itself offers, so it cannot drift from what the
// application can express, and every verb it needs already existed.
const char *const kFixtureBScript = R"JS(
var g = project.create("selftest-fixture-B", { empty: true });
if (!g || g.length < 10) throw new Error("project.create refused");

// The sun FIRST: the realistic sky takes its sun position from the scene's sun
// (the first directional light), so the light has to exist before the sky is
// asked for. A fixed rotation, because a fence may not depend on a default.
var sun = scene.addLight("directional", { position: { x: 0, y: 12, z: 0 },
                                          rotation: { x: -52, y: 28, z: 0 },
                                          intensity: 3.0 });
if (!sun) throw new Error("scene.addLight(directional) refused");
if (!world.sky("realistic", { density: 0.35, diffusion: 1.6, power: 1.0, sunHaze: 2.5 }))
    throw new Error("world.sky(realistic) refused");

// THE GLOSSY FLOOR, 60 x 60 m, lifted 2 cm so it is unambiguously above y = 0.
var floor = scene.addPrimitive("plane", { position: { x: 0, y: 0.02, z: 0 },
                                         scale: { x: 60, y: 1, z: 60 } });
if (!material.set(floor, { baseColor: "#d8d8d8", metallic: 1.0, roughness: 0.2 }))
    throw new Error("material.set(floor) refused");

// SEVEN PILLARS at x = -12 .. 12 step 4, z = -2. #4 (index 3) is the emissive
// above the clip; #7 (index 6) is the mirror.
var cols = ["#ff3030", "#30ff30", "#3060ff", "#ffffff", "#ff30ff", "#20e0e0", "#ffffff"];
for (var i = 0; i < 7; ++i) {
    var c = scene.addPrimitive("cube", { position: { x: -12 + i * 4, y: 1.5, z: -2 },
                                        scale: { x: 1, y: 3, z: 1 } });
    if (i === 3) {
        if (!material.set(c, { baseColor: "#101010", roughness: 0.9,
                               emissiveColor: "#ffffff", emissiveIntensity: 3.0 }))
            throw new Error("material.set(emissive pillar) refused");
    } else if (i === 6) {
        if (!material.set(c, { baseColor: "#ffffff", metallic: 1.0, roughness: 0.0 }))
            throw new Error("material.set(mirror pillar) refused");
    } else {
        if (!material.set(c, { baseColor: cols[i], roughness: 0.6 }))
            throw new Error("material.set(pillar) refused");
    }
}

// THE MATTE WALL at z = -9: a receiver for the floor's bounce BEYOND cascade 0's
// 5 m half-extent.
var wall = scene.addPrimitive("cube", { position: { x: 0, y: 2, z: -9 },
                                       scale: { x: 12, y: 4, z: 0.3 } });
if (!material.set(wall, { baseColor: "#f0f0f0", metallic: 0.0, roughness: 1.0 }))
    throw new Error("material.set(wall) refused");

var ph = world.photon({ enabled: true, tier: "high" });
if (!ph || ph.enabled !== true || ph.tier !== "high")
    throw new Error("world.photon(high) did not take: " + JSON.stringify(ph));
// SCREEN-SPACE REFLECTIONS ON, EXPLICITLY, AND IT IS LOAD-BEARING: the RAY tier
// rides the SSR chain's prepass (`gi.rt_reflect` case 6, "with the view's SSR row
// OFF there is no trace at all, whatever the machine can do"). The `ssr` row's
// own tier column is the WORLD MODE's, not Photon's, so a Photon tier does not
// set it and the fixture must — measured: without this line B1 and B2 hashed
// identically on a machine with ray queries.
var ssr = world.override({ id: "ssr", value: "hq" });
if (world.settings().ssr.valueId !== "hq")
    throw new Error("world.override(ssr=hq) did not take: " + JSON.stringify(ssr));
if (world.rayTracing("auto") !== "auto") throw new Error("world.rayTracing(auto) refused");
editor.setCamera({ position: { x: 0, y: 5, z: 14 }, lookAt: { x: 0, y: 1, z: 0 } });

// SETTLE IN FRAMES, on the fixed clock, and generously: the cascade scheduler
// spends one cascade per frame, the incremental settle pays one injection per
// frame after that, and the ray tier's history has its own warm-up. 300 frames
// is many times what any of them owes.
editor.frame(300);
"ok"
)JS";

/// Part two: the rays come off and the picture settles again. A separate
/// evaluation because the screenshot between them is the RUNNER's.
const char *const kFixtureBNoRaysScript = R"JS(
if (world.rayTracing("off") !== "off") throw new Error("world.rayTracing(off) refused");
editor.frame(300);
"ok"
)JS";

}   // namespace

int runEngineSelftest(MainWindow &window, QApplication &app, const QString &outPng)
{
    window.show();
    app.processEvents();

    QString why;
    if (!window.beginEngineSelftest(why)) {
        std::fprintf(stderr, "engine-selftest: %s\n", qPrintable(why));
        return 1;
    }
    // EVERY exit from here on tears down the way the passing one always did.
    // The failure returns below used to skip it, and a failing self-test then
    // HUNG at process exit after its last shutdown step instead of exiting 1
    // (found by the broken-ground arm: ctest's 180 s timeout, not an exit code).
    struct EndSelftest {
        MainWindow &window;
        ~EndSelftest() { window.endEngineSelftest(); EngineHost::instance().shutdown(); }
    } endSelftest{ window };

    // THE DEFAULT SCENE MUST BE THE DEFAULT SCENE (smoke L10 item 3). On
    // 2026-09-11 the ground failed to parse (`model :/models/ground.obj: error
    // parsing file`) and this self-test rendered a groundless scene and exited
    // 0: the pixel check below only asks that the centre is not the CLEAR
    // colour, and the sky answers that on its own. So the scene is checked
    // before a frame is pumped — the ground node exists and carries geometry.
    //
    // JAHSHAKA_SELFTEST_BREAK_GROUND is the test-only arm that proves this bites
    // (app.engine_selftest_validation): it re-points the ground at a resource
    // that does not exist, which leaves the node exactly as a parse failure
    // does — a mesh path and no mesh. Read here and nowhere else.
    const iris::ScenePtr scene = window.getScene();
    const iris::MeshNodePtr ground = findDefaultGround(scene);
    if (ground && qEnvironmentVariableIsSet("JAHSHAKA_SELFTEST_BREAK_GROUND"))
        ground->setMesh(iris::MeshPtr());   // a mesh path and no mesh: the parse/seed failure
    const iris::MeshPtr groundMesh = ground ? ground->getMesh() : iris::MeshPtr();
    if (!groundMesh || groundMesh->numVerts <= 0) {
        std::fprintf(stderr, "engine-selftest: the default scene's ground did not load (%s) — "
                             "a groundless scene is not the default scene; see the model "
                             "error in the log\n",
                     !ground ? "no Ground node" : qPrintable(QStringLiteral("mesh '%1' has no geometry")
                                                                .arg(ground->meshPath)));
        return 1;
    }
    std::fprintf(stderr, "engine-selftest: default scene: %d nodes, ground %d vertices\n",
                 countNodes(scene->getRootNode()) - 1, groundMesh->numVerts);

    // THE SUN CONTACT ARM (PHOTON-RAYS-1): `JAHSHAKA_SELFTEST_SUN_CONTACT` turns
    // world.sunContact on for the default scene and for fixture B, so the four
    // hash lines of the row ON can be quoted beside the shipped (off) four. A
    // MEASUREMENT switch, not a mode: read here and nowhere else, and without
    // it this function runs exactly the verbs it always ran.
    const bool sunContactArm = qEnvironmentVariableIsSet("JAHSHAKA_SELFTEST_SUN_CONTACT");
    if (sunContactArm) {
        ScriptEngine *armHost = window.scripting();
        const ScriptResult r = armHost
            ? armHost->evaluate(QStringLiteral("world.sunContact({enabled:true}).enabled ? 'ok' : "
                                               "(function(){ throw new Error('refused'); })()"),
                                QStringLiteral("selftest-sun-contact"), true, 0, ScriptRunPolicy::Off)
            : ScriptResult();
        if (!armHost || !r.ok) {
            std::fprintf(stderr, "engine-selftest: the sun contact arm could not turn the row on\n");
            return 1;
        }
        std::fprintf(stderr, "engine-selftest: SUN CONTACT ARM - world.sunContact is ON\n");
    }

    // THE GATHER-OFF ARM (PHOTON-GATHER-1d): `JAHSHAKA_SELFTEST_GATHER_OFF` pins
    // world.gi({gather:false}) on the default scene and on fixture B — the A/B the
    // gather's default-on is quoted against: with it, the four hash lines are the
    // pre-gather picture byte for byte. A MEASUREMENT switch, the sun contact
    // arm's shape: read here and nowhere else.
    const bool gatherOffArm = qEnvironmentVariableIsSet("JAHSHAKA_SELFTEST_GATHER_OFF");
    if (gatherOffArm) {
        ScriptEngine *armHost = window.scripting();
        const ScriptResult r = armHost
            ? armHost->evaluate(QStringLiteral("world.gi({ gather: false }) ? 'ok' : "
                                               "(function(){ throw new Error('refused'); })()"),
                                QStringLiteral("selftest-gather-off"), true, 0, ScriptRunPolicy::Off)
            : ScriptResult();
        if (!armHost || !r.ok) {
            std::fprintf(stderr, "engine-selftest: the gather-off arm could not pin the row off\n");
            return 1;
        }
        std::fprintf(stderr, "engine-selftest: GATHER-OFF ARM - world.gi({gather:false})\n");
    }

    // Pump the render loop for ~30 frames (the driver ticks every 16 ms).
    QElapsedTimer clock;
    clock.start();
    // Resize twice on the way (the layout does this to the viewport in real use):
    // the engine must survive a swapchain rebuild without a stale depth buffer.
    QSize afterFirstResize, afterSecondResize;
    QSize widgetAfterFirst, widgetAfterSecond;
    for (int frame = 0; frame < 40; ++frame) {
        if (frame == 10) window.resize(1100, 760);
        if (frame == 25) window.resize(700, 520);
        if (frame == 20) { afterFirstResize  = window.viewport()->renderTargetSize();
                           widgetAfterFirst  = window.viewport()->asWidget()->size(); }
        if (frame == 35) { afterSecondResize = window.viewport()->renderTargetSize();
                           widgetAfterSecond = window.viewport()->asWidget()->size(); }
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(16);
    }
    app.processEvents();

    // On-screen coverage that does NOT assert pixels (MACOS_VIEWPORT_SPEC §5.1):
    // the pixel assertion below deliberately goes through a separate OFFSCREEN
    // view, so without this the selftest would pass just as happily with a blank
    // widget. Platforms where the on-screen path is a swapchain window must prove
    // the view is not offscreen and that it survived both resizes with a real size.
//
// LINUX TOO since the swapchain lane (deep audit area 7 F3). renderTargetSize()
// used to report the values the widget had pushed down, so this block could only
// ever have compared them with themselves; it now reads the live render target,
// which is what makes the size comparison below an assertion rather than a
// tautology. Windows joins when it grows a window backend.
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    if (window.viewport()->isOffscreen()) {
        std::fprintf(stderr, "engine-selftest: the editor viewport is OFFSCREEN — the on-screen "
                             "window backend did not take\n");
        return 1;
    }
    if (afterFirstResize.isEmpty() || afterSecondResize.isEmpty()) {
        std::fprintf(stderr, "engine-selftest: viewport lost its render target across a resize "
                             "(%dx%d then %dx%d)\n",
                     afterFirstResize.width(), afterFirstResize.height(),
                     afterSecondResize.width(), afterSecondResize.height());
        return 1;
    }
    // THE assertion: the render target tracks the widget. Not "the two resizes
    // produced different numbers" — a window manager is entitled to refuse a
    // resize, and then the widget did not change either and there is nothing to
    // report. What must never happen is the swapchain being a different size
    // from the window it presents into: that is the stale-swapchain state the
    // viewport gets stuck in when nothing but Ogre's OUT_OF_DATE self-heal is
    // driving the rebuild.
    if (afterFirstResize != widgetAfterFirst || afterSecondResize != widgetAfterSecond) {
        std::fprintf(stderr,
                     "engine-selftest: the render target did not follow the viewport across a "
                     "resize — target %dx%d vs widget %dx%d, then target %dx%d vs widget %dx%d\n",
                     afterFirstResize.width(), afterFirstResize.height(),
                     widgetAfterFirst.width(), widgetAfterFirst.height(),
                     afterSecondResize.width(), afterSecondResize.height(),
                     widgetAfterSecond.width(), widgetAfterSecond.height());
        return 1;
    }
    std::fprintf(stderr, "engine-selftest: on-screen view survived resizes: %dx%d then %dx%d "
                         "(widget %dx%d then %dx%d)\n",
                 afterFirstResize.width(), afterFirstResize.height(),
                 afterSecondResize.width(), afterSecondResize.height(),
                 widgetAfterFirst.width(), widgetAfterFirst.height(),
                 widgetAfterSecond.width(), widgetAfterSecond.height());
#endif

    settleShotIfGathering(window);
    QImage img = window.viewport()->takeScreenshot(256, 256);
    if (img.isNull()) {
        std::fprintf(stderr, "engine-selftest: takeScreenshot returned a null image after %lld ms\n",
                     static_cast<long long>(clock.elapsed()));
        return 1;
    }
    if (!img.save(outPng, "PNG")) {
        std::fprintf(stderr, "engine-selftest: could not save %s\n", qPrintable(outPng));
        return 1;
    }
    const QColor clear = QColor::fromRgbF(0.10f, 0.11f, 0.14f);
    const int tolerance = 2;
    const auto isPicture = [&](const QImage &im) {
        const QColor c = im.pixelColor(im.width() / 2, im.height() / 2);
        return qAbs(c.red() - clear.red()) > tolerance ||
               qAbs(c.green() - clear.green()) > tolerance ||
               qAbs(c.blue() - clear.blue()) > tolerance;
    };
    const QColor centre = img.pixelColor(img.width() / 2, img.height() / 2);
    const bool differs = isPicture(img);
    std::fprintf(stderr, "engine-selftest: %dx%d image, centre pixel (%d,%d,%d), clear (%d,%d,%d) -> %s\n",
                 img.width(), img.height(), centre.red(), centre.green(), centre.blue(),
                 clear.red(), clear.green(), clear.blue(), differs ? "PASS" : "FAIL");
    const QString hash1 = fileSha256(outPng);
    std::fprintf(stderr, "engine-selftest: pose 1 sha256 %s (%s)\n",
                 qPrintable(hash1), qPrintable(outPng));
    if (!differs) return 1;

    // ---- THE SECOND POSE (lane ENGINE-SMALL-B item 5) ----------------------
    //
    // WHY A SECOND POSE AT ALL. The hash CLAUDE.md's law is stated in describes
    // ONE picture of the default scene from ONE camera that has never moved, so
    // the whole camera-relative half of this renderer is outside it: a cascade
    // chain that has scrolled, a probe/field placement that has followed, and
    // the settle that a re-voxelisation owes are all invisible to it. Six lanes
    // in a row have had to prove "the hash is exact" about code whose subject
    // the hash cannot see. This adds the cheapest possible second observation:
    // the same scene, one camera move, one turn, settled, hashed.
    //
    // FIVE METRES AND A TURN, and the number is not arbitrary: the innermost
    // cascade's step at the default (Medium) tier is 5 m, so this crosses at
    // least cascade 0's step plane and re-centres it — the scroll path — while
    // the turn puts different geometry in front of the camera. The frames after
    // it are the SETTLE: the scheduler spends one cascade per frame and
    // LAMPREST-3's incremental settle pays one injection per frame after that,
    // so a short pump would hash a picture that is still converging (and would
    // be a flake, not a gate). 240 frames on the fixed clock is many times
    // what either owes.
    //
    // IT CANNOT MOVE THE FIRST POSE'S HASH: the first image is saved, read and
    // hashed above, and the second pose writes a SECOND FILE.
    const QString pose2Png = posePath(outPng);
    EditorCameraPose pose;
    pose.position = iris::Vec3(5.0f, 5.0f, 14.0f);
    pose.hasPosition = true;
    pose.lookAt = iris::Vec3(-2.0f, 0.5f, -3.0f);
    pose.hasLookAt = true;
    if (!window.viewport()->setCameraPose(pose)) {
        std::fprintf(stderr, "engine-selftest: the viewport refused the second pose — "
                             "no editor camera\n");
        return 1;
    }
    if (!window.viewport()->canRenderFrames()) {
        std::fprintf(stderr, "engine-selftest: the viewport cannot render frames — "
                             "the second pose cannot settle\n");
        return 1;
    }
    window.viewport()->renderFrames(240, 1.0f / 60.0f);
    // ...AND UNTIL GI IS AT REST (PHOTON-FIELD-ROTATE-1): the 240 frames move the
    // document; the ONE settle predicate then decides when the picture has stopped
    // moving - the cascade steps, the chain's settle and the irradiance field's
    // refinement passes all paid. At dt 0, so the document's clock stays where
    // the 240 frames left it. (Pose 2 used to be captured at a fixed frame count
    // with a 23-40 frame margin over the field's convergence - determinism by
    // luck, not by construction.)
    int restFrames = 0;
    for (; restFrames < 4000 && !window.viewport()->giStatus().giAtRest; ++restFrames)
        window.viewport()->renderFrames(1, 0.0f);
    if (!window.viewport()->giStatus().giAtRest) {
        std::fprintf(stderr, "engine-selftest: pose 2's GI never came to rest (%d frames)\n",
                     restFrames);
        return 1;
    }
    app.processEvents();
    settleShotIfGathering(window);
    QImage img2 = window.viewport()->takeScreenshot(256, 256);
    if (img2.isNull() || !img2.save(pose2Png, "PNG")) {
        std::fprintf(stderr, "engine-selftest: could not take or save the second pose (%s)\n",
                     qPrintable(pose2Png));
        return 1;
    }
    const QColor centre2 = img2.pixelColor(img2.width() / 2, img2.height() / 2);
    const bool differs2 = isPicture(img2);
    const QString hash2 = fileSha256(pose2Png);
    std::fprintf(stderr, "engine-selftest: pose 2 sha256 %s (%s)\n",
                 qPrintable(hash2), qPrintable(pose2Png));
    std::fprintf(stderr, "engine-selftest: pose 2 (camera +5 m in x, turned, 240 frames + GI at rest): "
                         "%dx%d image, centre pixel (%d,%d,%d) -> %s\n",
                 img2.width(), img2.height(), centre2.red(), centre2.green(), centre2.blue(),
                 differs2 ? "PASS" : "FAIL");
    if (!differs2) return 1;
    // A MOVED CAMERA THAT PRODUCES THE SAME BYTES DID NOT MOVE. The pose could
    // be refused silently by a viewport that returns true and ignores it, and a
    // second hash equal to the first would then be a gate on nothing.
    if (!hash1.isEmpty() && hash1 == hash2) {
        std::fprintf(stderr, "engine-selftest: the two poses hash IDENTICALLY — the camera move "
                             "did not take\n");
        return 1;
    }

    // ---- POSE PAIR B (lane FENCE-1) ---------------------------------------
    // See kFixtureBScript's header for what the fixture is and why each element
    // of it is there. It runs LAST and writes its own files, so it cannot move
    // poses 1 and 2: both are saved, read and hashed above.
    ScriptEngine *scripting = window.scripting();
    if (!scripting) {
        std::fprintf(stderr, "engine-selftest: no scripting host — fixture B is built through the "
                             "verbs and cannot be built without one\n");
        return 1;
    }
    const auto runFixtureStep = [&](const char *source, const char *what) {
        // ScriptRunPolicy::Off, like `--script`: the driver takes no tick of its
        // own, so the ONLY frames drawn are the ones `editor.frame(n)` asks for.
        // That is what makes a hash of this fixture a statement about a frame
        // count rather than about how fast this box happens to be.
        const ScriptResult r = scripting->evaluate(QString::fromUtf8(source),
                                                  QStringLiteral("selftest-fixture-B"), true, 0,
                                                  ScriptRunPolicy::Off);
        if (!r.ok) {
            std::fprintf(stderr, "engine-selftest: fixture B (%s) failed: %s\n", what,
                         qPrintable(r.toString()));
            if (!r.stack.isEmpty()) std::fprintf(stderr, "%s\n", qPrintable(r.stack));
        }
        return r.ok;
    };
    // A REAL VIEWPORT FIRST. Pose 2 leaves the window at 700x520 and the
    // viewport at 726x131 — the two deliberate resizes above are a swapchain
    // test, and what they leave behind is a 131-pixel-tall view. Fixture B is
    // about SCREEN-SPACE reflections and a ray prepass that rides them, so it
    // gets a window of a stated size, and the size is part of the fence.
    window.resize(1280, 800);
    for (int frame = 0; frame < 12; ++frame) {
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(16);
    }
    std::fprintf(stderr, "engine-selftest: fixture B renders at %dx%d (window 1280x800)\n",
                 window.viewport()->renderTargetSize().width(),
                 window.viewport()->renderTargetSize().height());

    // The arm's row, set before the fixture's 300 settling frames so B1/B2 are
    // taken after the same frame count as the shipped pair.
    QByteArray fixtureB(kFixtureBScript);
    if (sunContactArm)
        fixtureB.replace("editor.frame(300);", "world.sunContact({ enabled: true });\neditor.frame(300);");
    if (gatherOffArm)
        fixtureB.replace("editor.frame(300);", "world.gi({ gather: false });\neditor.frame(300);");
    if (!runFixtureStep(fixtureB.constData(), "build + settle")) return 1;
    app.processEvents();

    // THE GRADE IS PART OF THE FENCE, and this is the one thing about fixture B
    // that had to be discovered rather than designed.
    //
    // `takeScreenshot(w, h)` is the PLAIN grade — "NO POST-PROCESSING AT ALL:
    // 1x MSAA, linear radiance clipped to 8 bits" (ieditorviewport.h). Poses 1
    // and 2 are Plain shots, and that is right for them: they are an exactly
    // reproducible measuring instrument. But Plain has no post chain, so it has
    // no SSR prepass, and the RAY tier rides that prepass — measured on this
    // tree: a Plain-grade fixture B with a glossy floor, a mirror pillar and the
    // ray tier on hashed IDENTICALLY with rays on and off, 0 of 65,536 pixels
    // different. A Plain hash structurally cannot see the half of this renderer
    // PHOTON's P3 and P5 exist to change.
    //
    // So B1/B2 are the VIEWPORT grade: the scene's whole chain — SSAO, SSR, the
    // ray tier, bloom, SMAA, the looks stack, HDR and the tonemap — with the
    // exposure RE-SEEDED from the description rather than carried over from the
    // on-screen view. `Scene` would have been the other candidate and is the
    // wrong one here: it pins the shot to the on-screen view's MEASURED exposure,
    // which is a temporal filter's state, and a fence may not depend on one.
    // Viewport is deterministic by construction (`resetExposureHistory`).
    const QString b1Png = taggedPath(outPng, QStringLiteral("B1"));
    settleShotIfGathering(window);
    QImage imgB1 = window.viewport()->takeScreenshot(256, 256,
                                                     IEditorViewport::ScreenshotGrade::Viewport);
    if (imgB1.isNull() || !imgB1.save(b1Png, "PNG")) {
        std::fprintf(stderr, "engine-selftest: could not take or save pose B1 (%s)\n",
                     qPrintable(b1Png));
        return 1;
    }
    const QString hashB1 = fileSha256(b1Png);
    std::fprintf(stderr, "engine-selftest: pose B1 (rays) sha256 %s (%s)\n",
                 qPrintable(hashB1), qPrintable(b1Png));

    if (!runFixtureStep(kFixtureBNoRaysScript, "rays off + settle")) return 1;
    app.processEvents();

    const QString b2Png = taggedPath(outPng, QStringLiteral("B2"));
    settleShotIfGathering(window);
    QImage imgB2 = window.viewport()->takeScreenshot(256, 256,
                                                     IEditorViewport::ScreenshotGrade::Viewport);
    if (imgB2.isNull() || !imgB2.save(b2Png, "PNG")) {
        std::fprintf(stderr, "engine-selftest: could not take or save pose B2 (%s)\n",
                     qPrintable(b2Png));
        return 1;
    }
    const QString hashB2 = fileSha256(b2Png);
    std::fprintf(stderr, "engine-selftest: pose B2 (no rays) sha256 %s (%s)\n",
                 qPrintable(hashB2), qPrintable(b2Png));

    // HOW MUCH THE RAYS MOVED, in pixels and codes. The hashes say "different";
    // this says whether the difference is a reflection or a rounding, which is
    // what a reviewer needs from a fence they cannot look at.
    int movedPixels = 0, worstCode = 0;
    if (imgB1.size() == imgB2.size()) {
        for (int y = 0; y < imgB1.height(); ++y)
            for (int x = 0; x < imgB1.width(); ++x) {
                const QColor a = imgB1.pixelColor(x, y), b = imgB2.pixelColor(x, y);
                const int d = qMax(qMax(qAbs(a.red() - b.red()), qAbs(a.green() - b.green())),
                                   qAbs(a.blue() - b.blue()));
                if (d > 0) ++movedPixels;
                worstCode = qMax(worstCode, d);
            }
    }
    const QColor centreB1 = imgB1.pixelColor(imgB1.width() / 2, imgB1.height() / 2);
    const QColor centreB2 = imgB2.pixelColor(imgB2.width() / 2, imgB2.height() / 2);
    std::fprintf(stderr, "engine-selftest: pose B (glossy floor, cascade crossing, emissive 3.0, "
                         "mirror pillar; 300 frames each): centre B1 (%d,%d,%d), B2 (%d,%d,%d); "
                         "the rays moved %d of %d pixels, worst %d/255\n",
                 centreB1.red(), centreB1.green(), centreB1.blue(),
                 centreB2.red(), centreB2.green(), centreB2.blue(),
                 movedPixels, imgB1.width() * imgB1.height(), worstCode);
    if (!isPicture(imgB1) || !isPicture(imgB2)) {
        std::fprintf(stderr, "engine-selftest: fixture B rendered the CLEAR COLOUR — the fixture "
                             "did not reach the screen\n");
        return 1;
    }
    // IDENTICAL B1/B2 MEANS THE RAYS DID NOTHING — unless this machine has none,
    // in which case it is the correct answer and is said so rather than asserted
    // away. The engine is the authority on whether the tier is live: a host-side
    // guess would be a second source of truth.
    // THE ENGINE IS THE AUTHORITY, read the same way the `machine.rayTracing`
    // scene issue reads it (src/services/sceneissues.cpp): the process latch
    // (`--no-ray-query` / JAHSHAKA_NO_RAY_QUERY) keeps the extensions off the
    // device entirely, so a run under it IS a machine without the hardware.
    bool raysLive = false;
    if (const auto eng = EngineHost::instance().engine())
        raysLive = eng->rayTracing() && eng->rayQueryAvailable();
    if (!hashB1.isEmpty() && hashB1 == hashB2) {
        if (raysLive) {
            std::fprintf(stderr, "engine-selftest: pose B1 and B2 hash IDENTICALLY while this "
                                 "machine HAS ray queries — the ray tier moved no pixel of a "
                                 "fixture built to show it\n");
            return 1;
        }
        std::fprintf(stderr, "engine-selftest: pose B1 == B2, and this machine has no ray query — "
                             "that is the correct answer, not a failure\n");
    }
    return 0;
}
