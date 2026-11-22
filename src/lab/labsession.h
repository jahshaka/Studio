// jahlab — THE SESSION (LAB-1 stage 1): the engine, the document, the mirror, the camera and the loop, and the ONE
// executor every command reaches (labcommands.h: the keys, --script and stdin all end in Session::exec).
//
// THE FRAME, the app's order (DOCS/ARCHITECTURE.md "How one frame happens"; Engine.h:1971-1989): the window (or an
// offscreen view) BEFORE the scene, the scene, the view bound to it, the document mirrored into it; each frame
// doc->refresh(), mirror->sync(the view's environment + sky + camera), Engine::renderOneFrame() — LOCKSTEP (the frame is
// drawn inside the call), the engine on THIS thread with no host records (jahengine's shape: the EngineConfig defaults,
// so the engine's script host can run here - labsession.cpp boot()). Teardown mirrors it: the mirror, the document,
// the engine, then the X connection the engine presented through.
#pragma once

#include "labcamera.h"
#include "labcommands.h"
#include "labscene.h"
#include "labwindow.h"
#include "envs/env.h"

#include <QString>

#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "jahshaka/engine/Math.h"

class SceneMirror;
namespace jahshaka { namespace engine { class Engine; class Scene; class View; namespace script { class Host; } } }

namespace lab {

/// the probe's frame-wide numbers the lab prints beside its per-object rows
struct AtomCutProbeSummary
{
    unsigned atomPixels = 0, objects = 0, groupsDropped = 0;
    float tolerance = 0.0f, viewportHeight = 0.0f;
    unsigned long long drawnTriangles = 0, hiddenTriangles = 0;   ///< all drawn / drawn by objects with no visible pixel
};

struct Options
{
    QString file;              ///< a model or a .jlab
    QString env;               ///< a generated environment (envs/env.h), instead of a file
    unsigned seed = 1;         ///< the environment's seed
    int footprint = 2;         ///< the occlusion test's footprint in texels the expectations assume (envs/env.h Optics)
    QString script;
    QString js;                ///< --js: an engine script loaded and run once after the first frame
    QString jsDir;             ///< --js-dir: the scripts folder Load walks when no script is loaded
    QString dataRoot;
    QString binDir;            ///< the binary's directory (media, app/content)
    bool headless = false;     ///< an OFFSCREEN Vulkan view, no window (never EngineConfig::headless; DISPLAY still needed)
    bool readStdin = true;
    /// --no-idle-frames: the window draws no frame unasked (it waits on stdin as --headless does) - a card row's
    /// declared frames are then the frames it draws, never a count paced by the driver's wall clock
    bool idleFrames = true;
    bool bakeOnly = false;     ///< bake (or verify the cached bake of) the file and exit: CPU only, no engine, no GPU
    bool hasCamera = false;
    iris::Vec3 camPos, camTarget;
    QString tier = QStringLiteral("epic");
    /// --arm <name>=<value>: engine arms set right after the engine is made, before any scene (REDS-1: a cache's FIRST
    /// gather takes them - a truth door set after the load re-gathers nothing)
    std::vector<std::pair<std::string, double>> bootArms;
    unsigned width = 1920, height = 1080;
};

class Session
{
public:
    Session();
    ~Session();
    /// Boots, loads, runs the script and the loop; the process's exit code.
    int run(const Options &opt);

    /// THE EXECUTOR: one parsed command, from any source. False (the reason printed) when refused.
    bool exec(const Command &c);
    /// Parse + exec of one line from `source` ("key" / "script" / "stdin").
    bool execLine(const std::string &line, const char *source);

private:
    bool boot(std::string &error);
    bool load(std::string &error);
    /// One frame: the window's input (pumpWindow), the sync, the render, the overlay.
    void frame();
    /// The window's input, drained: keys and strip clicks -> their lines through the one parser, the close, the
    /// resize, the fly. Every frame's first step, and - without a frame - the window's service under --no-idle-frames.
    void pumpWindow();
    void renderFrames(int n);
    void pushOverlay();
    bool shot(const std::string &name);
    std::string statusJson();
    void setTitle();
    void readStdinLines();
    bool setView(int mode);
    bool loadEnv(const std::string &name, std::string &error);
    /// the environment's dolly camera at stop d (its far clip too)
    bool envCamera(double d);
    /// the cut probe of the next drawn frame, per object name (false: no id pass / never answered)
    bool probe(std::vector<envs::ObjectReading> &out, AtomCutProbeSummary &sum);
    envs::Optics optics() const;
    /// THE STILLNESS DOOR's lab verb (PHOTON-2 stage 0; irisgl Stillness.h), the lamp step and the scripted move.
    bool stillness(const Command &c);
    bool lamp(const Command &c);
    /// `sky brightness|rayleigh <v>` (PHOTON-7-9A): the realistic sky's dial in the document (parseSkyDial's rule).
    bool sky(const Command &c);
    /// `routes <words>` (PHOTON-7-9A): the lab's route declaration, judged after each frame drawn (judgeDeclaredRoutes)
    bool routes(const Command &c);
    void judgeDeclaredRoutes();
    bool material(const Command &c);
    bool move(const Command &c);
    bool slide(const Command &c);
    /// THE GPU MONITOR (PHOTON-7, the step before stage 10): N frames, one line each - every pass's and every GPU row's
    /// milliseconds from the engine's own frame monitor (its tee) - and a summary line.
    bool monitor(const Command &c);
    /// THE GI DOORS (PHOTON-7 stage 1): the placement pin and one cascade's voxels by world cell.
    bool pin(const Command &c);
    bool voxels(const Command &c);
    /// THE CHANGED-REGION LIST (PHOTON-7 stage 3): the last frame's, printed.
    bool regions(const Command &c);
    /// THE ENGINE's SCRIPTS (LAB-SCRIPTS-1 stage 1): `js load|run|next|clear`, and the run itself.
    bool js(const Command &c);
    /// `place`: the script's "<i> of <n>: <name>" when the press already read the folder (`js next`'s pick); empty = read
    /// it now - ONE listing a press, kept in mJsPlace (the frame loop never reads the folder: the audit's F4)
    bool jsLoad(const std::string &path, const std::string &place = std::string());
    bool jsRun();
    /// The lab's world back in the window's view (a script was handed it by lab.view()); then the last run's host ends.
    void jsClear();
    /// THE SCRIPTS FOLDER `js next` walks (labscripts.h): the loaded script's, else --js-dir's, else <data root>/scripts.
    std::string jsFolder() const;
    /// The script's state on the strip and the overlay, now.
    void showJs();
    void nameEngineNodes();
    double msSinceStart() const;

    Options mOpt;
    std::unique_ptr<jahshaka::engine::Engine> mEngine;
    jahshaka::engine::View *mView = nullptr;
    jahshaka::engine::Scene *mScene = nullptr;
    iris::ScenePtr mDoc;
    std::unique_ptr<SceneMirror> mMirror;
    LabCamera mCamera;
    LabWindow mWindow;
    WindowInput mInput;
    Jlab mJlab;                 ///< what the session was opened from (a .jlab or a file)
    const envs::Env *mEnv = nullptr;        ///< the generated environment, when the session is one
    std::unique_ptr<envs::Built> mBuilt;    ///< ...and what its build made (the feature table the closed form reads)
    std::vector<std::pair<unsigned long long, QString>> mNodeNames;
    bool mProbed = false;                   ///< a probe has answered (the status carries its summary)
    AtomCutProbeSummary mLastProbe;
    unsigned long long mLastProbeFrame = 0;   ///< engine node id -> document node name

    // THE LAB'S STATE — what it pushed (the host keeps what it set; the engine has no getters for these)
    int mAtomView = 0;
    int mTier = 3;              ///< worldmodes::Mode (Low 0 .. Epic 3)
    bool mGi = true, mOcclusion = true, mDraw = true, mOverlay = false;
    bool mPlaying = false;   ///< `play on`: the document's clock advances one fixed step a frame
    bool mReflectOff = false;   ///< `reflections off`: PostFxDesc::ssr 0 (an A/B's arm)
    bool mHdrReadback = false;   ///< `cardat` turned the view's float readback on (PostFxDesc::hdrReadback)
    bool mQuit = false;
    int mExit = 0;
    RouteSet mRoutesDeclared;     ///< the `routes` verb's declaration (judged after each frame while pending)
    bool mRoutesPending = false;
    int mRoutesFrames = 0;
    unsigned long long mFrames = 0;
    unsigned long long mSceneTriangles = 0;
    int mShotSerial = 0;
    std::deque<std::string> mScriptLines;
    std::string mStdinBuf;
    bool mStdinOpen = true;
    bool mWaitingSaid = false;
    std::chrono::steady_clock::time_point mStart;

    // THE SCRIPT (LAB-SCRIPTS-1): the loaded file, the LAST run's host (kept until the next run, `js clear` or the end,
    // after the view is given back: a host's end may take the objects its run made), whether the window's view is
    // lent to a script (lab.view(): the lab's world detached, frame() then draws without the mirror's sync), and what
    // the overlay says of it
    std::string mJsPath;
    std::string mJsPlace;   ///< mJsPath's place in its folder, read at the press that loaded it (labscripts.h)
    std::unique_ptr<jahshaka::engine::script::Host> mJsHost;
    bool mViewLent = false;
    jahshaka::engine::Colour mLabBackground;   ///< the lab world's background while the view is lent (given back at clear)
    int mJsRuns = 0;
    std::string mJsState, mJsError;
};

}   // namespace lab
