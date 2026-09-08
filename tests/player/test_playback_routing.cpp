// PlaybackService routing — the play/edit state machine, per call, per flag.
//
// ENGINEERING_DEBT_SPEC addendum item 6 (A1.6): "PlaybackService routing has no
// unit test (IEditorViewport = 63 pure virtuals; a stub was judged more brittle
// than valuable — the routing was audited manually instead)". That judgement is
// obsolete: HeadlessEditorViewport (src/viewport/headlesseditorviewport.h) is a
// SHIPPING implementation of the whole interface — the one --headless script
// runs use — so the "stub" this suite needs is a six-method subclass of it that
// records what the service asked for. Nothing here duplicates the interface;
// if IEditorViewport grows a method, the shipping stand-in absorbs it and this
// file does not move.
//
// What it pins (all of it behaviour the editor/player seam depends on and the
// parity suites cannot see — they compare PIXELS of two already-running
// scenes, never the transitions that get you there):
//
//   * play / pause / resume / restart / stop, as ORDERED viewport calls;
//   * pause keeps PlayMode and does NOT stop — the resume contract;
//   * enterEditMode STOPS the viewport (the 2026-09-05 "can't click anything
//     in a loaded scene" regression: the service's flag and the viewport's
//     desynced and every editor click went to the player controller);
//   * enterPlayMode deliberately does NOT start the editor viewport (the
//     player page owns its own playback — two drivers on one document);
//   * isPlaying() delegates to the VIEWPORT, which is the truth, and falls
//     back to the member only while no viewport is wired;
//   * the physics-simulation trio routes independently of play mode;
//   * every transition emits its mode signal exactly once.
//
// No engine, no GL, no display: QT_QPA_PLATFORM=offscreen and a QWidget the
// stand-in never shows.

#include <QApplication>
#include <QStringList>
#include <cstdio>

// HeadlessEditorViewport binds the editor camera into the scene, so the
// document types it forward-declares have to be complete here.
#include "irisgl/document/scenegraph/scene.h"
#include "services/playbackservice.h"
#include "viewport/headlesseditorviewport.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// The shipping headless viewport plus a call log. `mPlaying` mirrors
/// EngineSceneViewport's own flag — the one isPlaying() publishes and the one
/// the service is contractually forbidden to shadow.
class RecordingViewport : public HeadlessEditorViewport
{
public:
    QStringList calls;
    bool mPlaying = false;

    void startPlayingScene() override { calls << QStringLiteral("start"); mPlaying = true; }
    void pausePlayingScene() override { calls << QStringLiteral("pause"); mPlaying = false; }
    void stopPlayingScene()  override { calls << QStringLiteral("stop");  mPlaying = false; }
    bool isPlaying() const   override { return mPlaying; }

    void startPhysicsSimulation()   override { calls << QStringLiteral("sim.start"); }
    void restartPhysicsSimulation() override { calls << QStringLiteral("sim.restart"); }
    void stopPhysicsSimulation()    override { calls << QStringLiteral("sim.stop"); }

    QString log() const { return calls.join(QLatin1Char(',')); }
    void clear() { calls.clear(); }
};

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // ---- 1. no viewport: the member is the fallback, nothing dereferences ----
    {
        PlaybackService svc;
        CHECK(!svc.isPlaying(), "no viewport: a fresh service is not playing");
        svc.setPlaying(true);
        CHECK(svc.isPlaying(), "no viewport: isPlaying() falls back to the member");
        svc.playScene();       // must not dereference the null viewport
        svc.pauseScene();
        svc.restartScene();
        svc.stopScene();
        svc.startSimulation();
        svc.restartSimulation();
        svc.stopSimulation();
        svc.enterPlayMode();
        svc.enterEditMode();
        CHECK(svc.sceneMode() == SceneMode::EditMode,
              "no viewport: the state machine still runs (every call is viewport-guarded)");
    }

    RecordingViewport viewport;
    PlaybackService svc;
    svc.setViewport(&viewport);

    int editSignals = 0, playSignals = 0;
    QObject::connect(&svc, &PlaybackService::editModeEntered, [&] { ++editSignals; });
    QObject::connect(&svc, &PlaybackService::playModeEntered, [&] { ++playSignals; });

    // ---- 2. isPlaying() is the VIEWPORT's answer, not the service's ----
    svc.setPlaying(true);
    CHECK(!svc.isPlaying(),
          "isPlaying() delegates to the viewport (a stale member can never lie for it)");
    svc.setPlaying(false);

    // ---- 3. play ----
    viewport.clear();
    svc.playScene();
    CHECK(viewport.log() == QLatin1String("start"), "playScene: exactly one startPlayingScene");
    CHECK(svc.sceneMode() == SceneMode::PlayMode, "playScene: scene mode is PlayMode");
    CHECK(svc.isPlaying(), "playScene: the viewport reports playing");

    // ---- 4. pause is a RESUME point, not a stop ----
    viewport.clear();
    svc.pauseScene();
    CHECK(viewport.log() == QLatin1String("pause"),
          "pauseScene: pausePlayingScene only — never a stop (a stop would drop the "
          "physics world and the pre-play transforms)");
    CHECK(svc.sceneMode() == SceneMode::PlayMode, "pauseScene: stays in PlayMode");
    CHECK(!svc.isPlaying(), "pauseScene: not running while paused");

    viewport.clear();
    svc.playScene();
    CHECK(viewport.log() == QLatin1String("start"), "resume: playScene after a pause starts again");
    CHECK(svc.isPlaying(), "resume: running again");

    // ---- 5. restart is stop THEN start, in that order ----
    viewport.clear();
    svc.restartScene();
    CHECK(viewport.log() == QLatin1String("stop,start"), "restartScene: stop then start");
    CHECK(svc.sceneMode() == SceneMode::PlayMode, "restartScene: PlayMode");
    CHECK(svc.isPlaying(), "restartScene: running");

    // ---- 6. stop ----
    viewport.clear();
    svc.stopScene();
    CHECK(viewport.log() == QLatin1String("stop"), "stopScene: one stopPlayingScene");
    CHECK(!svc.isPlaying(), "stopScene: not running");

    // ---- 7. enterEditMode STOPS the viewport (the 2026-09-05 regression) ----
    viewport.clear();
    svc.playScene();                 // play-in-place, then leave it the way the UI does
    viewport.clear();
    svc.enterEditMode();
    CHECK(viewport.log() == QLatin1String("stop"),
          "enterEditMode: drives the viewport's own flag down (without this every "
          "editor click after play-in-place routed to the player controller)");
    CHECK(!viewport.isPlaying(), "enterEditMode: the viewport is not playing");
    CHECK(!svc.isPlaying(), "enterEditMode: the service agrees");
    CHECK(svc.sceneMode() == SceneMode::EditMode, "enterEditMode: EditMode");
    CHECK(editSignals == 1, "enterEditMode: editModeEntered emitted once");

    // ---- 8. enterPlayMode does NOT start the editor viewport ----
    viewport.clear();
    svc.enterPlayMode();
    CHECK(viewport.log().isEmpty(),
          "enterPlayMode: the editor viewport is NOT started (the player page starts "
          "its own playback — two drivers on one document is the bug this prevents)");
    CHECK(svc.sceneMode() == SceneMode::PlayMode, "enterPlayMode: PlayMode");
    CHECK(!svc.isPlaying(),
          "enterPlayMode: nothing is running on the editor viewport yet");
    CHECK(playSignals == 1, "enterPlayMode: playModeEntered emitted once");

    // ---- 9. the player-tab flag is state, not routing ----
    viewport.clear();
    CHECK(!svc.isPlayerMode(), "playerMode: off by default");
    svc.setPlayerMode(true);
    CHECK(svc.isPlayerMode() && viewport.log().isEmpty(),
          "playerMode: a pure flag — it drives no viewport call");
    svc.setPlayerMode(false);

    // ---- 10. physics simulation routes independently of play ----
    viewport.clear();
    svc.startSimulation();
    svc.restartSimulation();
    svc.stopSimulation();
    CHECK(viewport.log() == QLatin1String("sim.start,sim.restart,sim.stop"),
          "simulation: the three physics calls route straight through");
    CHECK(svc.sceneMode() == SceneMode::PlayMode,
          "simulation: play-in-place physics does not touch the scene mode");
    CHECK(!svc.isSimulationRunning(), "simulation: the running flag is the shell's to set");
    svc.setSimulationRunning(true);
    CHECK(svc.isSimulationRunning(), "simulation: and it round-trips");

    // ---- 11. a viewport swap re-points the routing ----
    RecordingViewport second;
    svc.setViewport(&second);
    viewport.clear();
    svc.playScene();
    CHECK(second.log() == QLatin1String("start") && viewport.log().isEmpty(),
          "setViewport: play goes to the CURRENT viewport only");
    svc.setViewport(nullptr);
    svc.stopScene();
    CHECK(second.log() == QLatin1String("start"),
          "setViewport(nullptr): the detached viewport receives nothing more");

    CHECK(editSignals == 1 && playSignals == 1,
          "signals: no transition emitted a stray mode signal");

    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
