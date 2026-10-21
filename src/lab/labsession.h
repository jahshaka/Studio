// jahlab — THE SESSION (LAB-1 stage 1): the engine, the document, the mirror, the camera and the loop, and the ONE
// executor every command reaches (labcommands.h: the keys, --script and stdin all end in Session::exec).
//
// THE FRAME, the app's order (DOCS/ARCHITECTURE.md "How one frame happens"; Engine.h:1971-1989): the window (or an
// offscreen view) BEFORE the scene, the scene, the view bound to it, the document mirrored into it; each frame
// doc->refresh(), mirror->sync(the view's environment + sky + camera), Engine::renderOneFrame() — LOCKSTEP (the frame is
// drawn inside the call, the newest record exactly), on the engine's render thread as the app runs it
// (EngineConfig::renderThread + hostRecords, src/bridge/enginehost.cpp). Teardown mirrors it: the mirror, the
// document, the engine, then the X connection the engine presented through.
#pragma once

#include "labcamera.h"
#include "labcommands.h"
#include "labscene.h"
#include "labwindow.h"

#include <QString>

#include <chrono>
#include <deque>
#include <memory>
#include <string>

class SceneMirror;
namespace jahshaka { namespace engine { class Engine; class Scene; class View; } }

namespace lab {

struct Options
{
    QString file;              ///< a model or a .jlab
    QString script;
    QString dataRoot;
    QString binDir;            ///< the binary's directory (media, app/content)
    bool headless = false;     ///< an OFFSCREEN Vulkan view, no window (never EngineConfig::headless; DISPLAY still needed)
    bool readStdin = true;
    bool bakeOnly = false;     ///< bake (or verify the cached bake of) the file and exit: CPU only, no engine, no GPU
    bool hasCamera = false;
    iris::Vec3 camPos, camTarget;
    QString tier = QStringLiteral("epic");
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
    /// One frame: the window's input (keys -> their lines, the fly), the sync, the render, the overlay.
    void frame();
    void renderFrames(int n);
    void pushOverlay();
    bool shot(const std::string &name);
    std::string statusJson();
    void setTitle();
    void readStdinLines();
    bool setView(int mode);
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

    // THE LAB'S STATE — what it pushed (the host keeps what it set; the engine has no getters for these)
    int mAtomView = 0;
    int mTier = 3;              ///< worldmodes::Mode (Low 0 .. Epic 3)
    bool mGi = true, mOcclusion = true, mDraw = true, mOverlay = false;
    bool mQuit = false;
    int mExit = 0;
    unsigned long long mFrames = 0;
    unsigned long long mSceneTriangles = 0;
    int mShotSerial = 0;
    std::deque<std::string> mScriptLines;
    std::string mStdinBuf;
    bool mStdinOpen = true;
    bool mWaitingSaid = false;
    std::chrono::steady_clock::time_point mStart;
};

}   // namespace lab
