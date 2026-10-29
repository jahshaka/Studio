#include "labsession.h"

#include "labclasses.h"
#include "laboverlay.h"

#include "irisgl/document/animation/animation.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/worldmodes.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Stillness.h"
#include "jahshaka/engine/Types.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>

#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <set>
#include <map>
#include <sstream>

namespace lab {

using namespace jahshaka::engine;

namespace {
/// THE LAST REFUSAL'S REASON (URGENT #50): every LAB REFUSED / ... FAILED line is said through labFail, which keeps
/// its text, so a refused --script line's STOP names the reason beside the line.
std::string gLabLastFailure;
void labFail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void labFail(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::fputs(buf, stdout);
    std::string s(buf);
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    gLabLastFailure = s.rfind("LAB ", 0) == 0 ? s.substr(4) : s;
}
}   // namespace

namespace {

/// The overlay's rows are recomposed every this many frames (a number that changes 60 times a second is unreadable;
/// the app rate-limits the same way).
constexpr unsigned long long kOverlayEveryFrames = 10;
/// The render counters are subscribed in windows of this many frames (RenderStats: "the first read arms").
constexpr unsigned kStatsWindowFrames = 240;
/// settle: the cut's triangles unchanged over this many consecutive frames, nothing pending.
constexpr int kSettleSteadyFrames = 8;
constexpr int kSettleDefaultCap = 1200;
/// rest: the cap without one (a refusal past it, never a longer wait).
constexpr int kRestDefaultCap = 1200;

const char *tierName(int t) { return tierNames()[size_t(std::max(0, std::min(3, t)))].c_str(); }

// THE DIFFUSE LOBE'S DIRECTIONAL AND HEMISPHERICAL ALBEDO, the fork's closed form (JahDiffuseAlbedo_piece_all.any,
// REDS-1): cardat's and hemi's view term
double labDiffuseAlbedo(double v, double r)
{
    v = std::clamp(v, 0.0, 1.0);
    r = std::clamp(r, 0.0, 1.0);
    const double t = 1.0 - v, b = t * t * t * t * t, al = 1.5 * r - 1.0, rv = r * v, ef = 1.0 + (1.0 / 1.51 - 1.0) * r;
    return ef * (1.0 + al / 21.0 + rv / 84.0 +
                 b * (al + rv * 2.0 / 3.0 + al * al / 21.0 + al * rv / 42.0 + r * r * (11.0 - 9.0 * v * v) / 504.0));
}
double labDiffuseAlbedoHemi(double r)
{
    r = std::clamp(r, 0.0, 1.0);
    const double al = 1.5 * r - 1.0, ef = 1.0 + (1.0 / 1.51 - 1.0) * r;
    return ef * (1.0 + al * 2.0 / 21.0 + r / 63.0 + al * al / 441.0 + al * r / 3528.0 + r * r * 123.0 / 127008.0);
}

bool parsePose(const std::string &word, iris::Vec3 &eye, iris::Vec3 &target)
{
    std::vector<float> n;
    std::string cur;
    for (char c : word + ",") {
        if (c == ',') {
            char *end = nullptr;
            const double v = std::strtod(cur.c_str(), &end);
            if (cur.empty() || !end || *end) return false;
            n.push_back(float(v));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (n.size() != 6) return false;
    eye = iris::Vec3(n[0], n[1], n[2]);
    target = iris::Vec3(n[3], n[4], n[5]);
    return true;
}

bool onOff(const std::string &w, bool current, bool &out)
{
    if (w == "on") { out = true; return true; }
    if (w == "off") { out = false; return true; }
    if (w == "toggle") { out = !current; return true; }
    return false;
}

std::string jstr(const QString &s)
{
    std::string o = "\"";
    for (char c : s.toStdString()) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o + "\"";
}

}   // namespace

Session::Session() = default;

Session::~Session()
{
    // TEARDOWN MIRRORS THE BOOT (ENGINE.md trap 1): the mirror lets go of the document and its engine objects, the
    // document's meshes go, the engine (its views, its scenes, Root), and only then the X connection it presented on.
    if (mMirror) mMirror->setSource(iris::ScenePtr());
    mMirror.reset();
    mDoc.reset();
    mEngine.reset();
    mWindow.close();
}

double Session::msSinceStart() const
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mStart).count();
}

bool Session::boot(std::string &error)
{
    EngineConfig cfg;
    // THE ENGINE'S FILES, resolved as the app resolves them (src/bridge/enginehost.cpp resolveConfig): the env
    // override, else beside the binary, else the build's own.
    const QByteArray envPlugins = qgetenv("JAHSHAKA_OGRE_PLUGINS");
    const QByteArray envMedia = qgetenv("JAHSHAKA_OGRE_MEDIA");
    const QString appMedia = mOpt.binDir + QStringLiteral("/media/");
    cfg.pluginDir = !envPlugins.isEmpty() ? envPlugins.toStdString() : std::string(JAHLAB_OGRE_PLUGIN_DIR_DEFAULT);
    cfg.hlmsMediaDir = !envMedia.isEmpty() ? envMedia.toStdString()
                       : QDir(appMedia + QStringLiteral("Hlms/Pbs")).exists() ? appMedia.toStdString()
                                                                                : std::string(JAHLAB_OGRE_MEDIA_DIR_DEFAULT);
    cfg.logFile = QDir(mOpt.dataRoot).filePath(QStringLiteral("jahlab-ogre.log")).toStdString();
    cfg.logSink = [](int level, const std::string &message) {
        if (level == 1) std::fprintf(stderr, "ENGINE ERROR: %s\n", message.c_str());
    };
    // THE SHADER CACHE: the lab's own, in its data root — or the harness's shared warm cache when a ctest row hands
    // one over (JAHSHAKA_SHADER_CACHE_DIR, tests/CMakeLists.txt jah_gpu_row CLASS engine; the fixtures' 1 GiB cap).
    if (const char *dir = std::getenv("JAHSHAKA_SHADER_CACHE_DIR")) {
        cfg.shaderCacheDir = dir;
        cfg.shaderCacheMaxBytes = 1024ull * 1024ull * 1024ull;
    } else {
        cfg.shaderCacheDir = QDir(mOpt.dataRoot).filePath(QStringLiteral("shadercache")).toStdString();
    }
    // THE APP'S SHAPE: the host's calls are records and the frames are drawn on the engine's render thread.
    cfg.hostRecords = true;
    cfg.renderThread = true;
    // --headless is an OFFSCREEN Vulkan view, never the NULL render system (createOffscreenView refuses there). It still
    // needs an X server: Ogre-Next's VulkanXcbSupport connects to $DISPLAY at boot (measured: unset = "XCB: failed to
    // connect to the display server", then a refused config) — display 0 only means "no window of ours".
    cfg.headless = false;
    if (mOpt.headless && !std::getenv("DISPLAY")) {
        error = "--headless needs an X display: Ogre-Next's Vulkan connects to $DISPLAY at boot even for an offscreen "
                "view (no window is made) - run it with DISPLAY=:NN of the rig's Xvfb";
        return false;
    }
    if (!mOpt.headless) {
        std::string werr;
        if (!mWindow.open(mOpt.width, mOpt.height, "jahlab", werr)) { error = werr; return false; }
        cfg.display = static_cast<NativeDisplayHandle>(mWindow.displayHandle());
    }
    std::string err;
    mEngine = Engine::create(cfg, err);
    if (!mEngine) { error = "engine: " + err; return false; }
    // THE BOOT ARMS (--arm): before any view or scene, so every cache's first gather takes them
    for (const auto &a : mOpt.bootArms) {
        bool armOk = false;
        mEngine->request([&](Engine &e) { armOk = e.setArm(a.first, a.second); });
        if (!armOk) { error = "--arm " + a.first + ": no such arm, or the value out of its range"; return false; }
        std::printf("LAB BOOT ARM %s = %g\n", a.first.c_str(), a.second);
    }
    // THE ORDER (Engine.h:1971): a View before any Scene.
    if (mOpt.headless) {
        mView = mEngine->createOffscreenView("jahlab", mOpt.width, mOpt.height, Colour(0, 0, 0), OffscreenContract::StillPicture);
        // A STILL PICTURE: it gathers where the scene gathers (the editor screenshot's contract).
    } else {
        mView = mEngine->createView("jahlab", static_cast<NativeWindowHandle>(mWindow.windowHandle()), mOpt.width,
                                    mOpt.height, Colour(0, 0, 0));
    }
    if (!mView) { error = "view: " + mEngine->lastError(); return false; }
    // NO EDITOR FURNITURE: the lights' wires and icons are the editor's helpers; the lab's picture is the world alone
    // (a graph-shape setting, set once at creation — the editor's own user shot does the same).
    mView->setHelpersVisible(false);
    mScene = mEngine->createScene("jahlab", 0);
    if (!mScene) { error = "scene: " + mEngine->lastError(); return false; }
    mView->setScene(mScene);
    mDoc = iris::Scene::create();
    mMirror.reset(new SceneMirror(mScene));
    mMirror->setSource(mDoc);
    mDoc->setCamera(mCamera.node());
    mCamera.setAspect(float(mOpt.width) / float(mOpt.height));
    mEngine->publishRenderStats(kStatsWindowFrames);
    std::printf("LAB BOOT %.0f ms: %s %ux%u, data root %s\n", msSinceStart(),
                mOpt.headless ? "offscreen view (--headless)" : "window", mOpt.width, mOpt.height,
                qPrintable(mOpt.dataRoot));
    if (mWindow.isOpen()) std::printf("LAB WINDOW %llu\n", mWindow.windowHandle());   // an agent's xdotool target
    std::fflush(stdout);
    return true;
}

bool Session::loadEnv(const std::string &name, std::string &error)
{
    mEnv = envs::find(name);
    if (!mEnv) { error = "no environment '" + name + "' (jahlab --env-list)"; return false; }
    mBuilt.reset(new envs::Built(mEnv->build(mOpt.seed, true)));
    // THE ENVIRONMENT IS THE DOCUMENT: the mirror takes the built scene in place of the empty one boot made
    mDoc = mBuilt->doc;
    mMirror->setSource(mDoc);
    mDoc->setCamera(mCamera.node());
    mSceneTriangles = mBuilt->triangles;
    mJlab.file.clear();
    mJlab.env = QString::fromStdString(name);
    mJlab.seed = mOpt.seed;
    std::printf("LAB ENV %s seed %u: %u meshes, %u instances, %s triangles, %zu features | build %.0f ms (bake %.0f ms) | "
                "hash %s\n", mEnv->name, mOpt.seed, mBuilt->meshes, mBuilt->instances, grouped(mBuilt->triangles).c_str(),
                mBuilt->features.size(), mBuilt->buildMs, mBuilt->bakeMs, mBuilt->hash.left(16).constData());
    std::fflush(stdout);
    if (!mOpt.tier.isEmpty()) {
        bool ok = false;
        const worldmodes::Mode m = worldmodes::modeFromName(mOpt.tier, &ok);
        if (!ok || m == worldmodes::Mode::Custom) { error = "unknown tier '" + mOpt.tier.toStdString() + "'"; return false; }
        mTier = int(m);
        worldmodes::setMode(mDoc, m);
    }
    if (mOpt.hasCamera) mCamera.set(mOpt.camPos, mOpt.camTarget);
    else if (!mJlab.hasCamera) envCamera(envs::stops().front());
    return true;
}

bool Session::envCamera(double d)
{
    if (!mEnv) return false;
    iris::Vec3 eye, target;
    mEnv->stop(d, eye, target);
    mCamera.node()->farClip = mEnv->farClip;
    mCamera.setExtent(float(d) + 50.0f);
    mCamera.set(eye, target);
    return true;
}

envs::Optics Session::optics() const
{
    envs::Optics o;
    o.eye = mCamera.position();
    o.target = mCamera.target();
    o.fovYDegrees = mCamera.node()->angle;
    const unsigned w = mInput.width ? mInput.width : mOpt.width, h = mInput.height ? mInput.height : mOpt.height;
    o.aspect = float(w) / float(h);
    o.heightPixels = float(h);
    o.tolerance = float(std::max(1, mDoc->atomPixelError));
    o.farClip = mCamera.node()->farClip;
    o.footprint = mOpt.footprint;
    return o;
}

void Session::nameEngineNodes()
{
    mNodeNames.clear();
    std::function<void(const iris::SceneNodePtr &)> walk = [&](const iris::SceneNodePtr &n) {
        if (const NodeId id = mMirror->engineNode(n.data())) mNodeNames.push_back({ (unsigned long long)id, n->getName() });
        for (const iris::SceneNodePtr &c : n->children()) walk(c);
    };
    walk(mDoc->getRootNode());
    std::sort(mNodeNames.begin(), mNodeNames.end());
}

bool Session::probe(std::vector<envs::ObjectReading> &out, AtomCutProbeSummary &sum)
{
    out.clear();
    sum = AtomCutProbeSummary();
    if (!mScene->armAtomCutProbe()) { std::printf("LAB PROBE refused: no view carries the id pass\n"); return false; }
    AtomCutProbe p;
    bool ok = false;
    for (int i = 0; i < 8 && !ok && !mQuit; ++i) {
        frame();
        ok = mScene->atomCutProbe(p) && p.valid;
    }
    if (!ok) {
        mScene->disarmAtomCutProbe();
        std::printf("LAB PROBE: no reading (%s)\n", p.error.c_str());
        return false;
    }
    if (mNodeNames.empty()) nameEngineNodes();
    for (const AtomCutProbeObject &o : p.objects) {
        envs::ObjectReading r;
        const auto it = std::lower_bound(mNodeNames.begin(), mNodeNames.end(), std::make_pair((unsigned long long)o.node, QString()));
        if (it != mNodeNames.end() && it->first == (unsigned long long)o.node) r.name = it->second;
        r.drawnTriangles = o.drawnTriangles;
        r.pixels = o.pixels;
        r.drawnClusters = o.drawnClusters;
        for (int d = 0; d < 16; ++d) r.depthPixels[d] = o.depthPixels[d];
        sum.drawnTriangles += o.drawnTriangles;
        if (o.pixels == 0) sum.hiddenTriangles += o.drawnTriangles;
        out.push_back(r);
    }
    sum.atomPixels = p.atomPixels;
    sum.objects = unsigned(p.objects.size());
    sum.groupsDropped = p.groupsDropped;
    sum.tolerance = p.tolerance;
    sum.viewportHeight = p.viewportHeight;
    mProbed = true;
    mLastProbe = sum;
    mLastProbeFrame = mFrames;
    return true;
}

bool Session::load(std::string &error)
{
    if (!mOpt.env.isEmpty()) return loadEnv(mOpt.env.toStdString(), error);
    QString path = mOpt.file;
    if (path.endsWith(QStringLiteral(".jlab"), Qt::CaseInsensitive)) {
        QString err;
        if (!readJlab(path, mJlab, err)) { error = err.toStdString(); return false; }
        if (!mJlab.env.isEmpty()) {
            mOpt.seed = mJlab.seed;
            const Jlab saved = mJlab;
            if (!loadEnv(mJlab.env.toStdString(), error)) return false;
            mJlab.hasCamera = saved.hasCamera; mJlab.camPos = saved.camPos; mJlab.camTarget = saved.camTarget;
            mJlab.view = saved.view; mJlab.gi = saved.gi; mJlab.occlusion = saved.occlusion; mJlab.draw = saved.draw;
            mJlab.overlay = saved.overlay;
            if (mJlab.hasCamera) mCamera.set(mJlab.camPos, mJlab.camTarget);
            bool ok = false;
            const worldmodes::Mode m = worldmodes::modeFromName(mOpt.tier.isEmpty() ? mJlab.tier : mOpt.tier, &ok);
            if (ok && m != worldmodes::Mode::Custom) { mTier = int(m); worldmodes::setMode(mDoc, m); }
            return true;
        }
        path = mJlab.file;
    } else {
        mJlab = Jlab();
        mJlab.file = QFileInfo(path).absoluteFilePath();
    }
    LoadReport r;
    iris::SceneNodePtr model = loadModel(path, mOpt.dataRoot, r);
    if (!model) { error = r.error.toStdString(); return false; }
    std::printf("LAB LOAD %s: %s | %d meshes, %s triangles, %d file lights, %d animated nodes | hash %.0f ms, parse %.0f ms, "
                "bake %.0f ms, read %.0f ms, fragment %.0f ms | maps %d resolved, %d missing, %d name collisions, %.0f ms | %d "
                "two-sided nodes | foliage (cut-out) %d meshes / %d maps, two-sided opaque %d meshes / %d maps | %s\n",
                qPrintable(QFileInfo(path).fileName()),
                r.baked ? "BAKED (cache miss)" : r.parsed ? "BAKE HIT, PARSED (the sidecar was missing or stale)"
                                                          : "CACHE HIT (no parse, no bake)",
                r.meshes, grouped(r.triangles).c_str(), r.lights, r.animatedNodes, r.hashMs, r.parseMs, r.bakeMs, r.readMs, r.fragmentMs,
                r.texturesResolved, r.texturesMissing, r.textureNameCollisions, r.texturesMs, r.twoSidedNodes,
                r.cutoutMeshes, r.cutoutMaps, r.twoSidedOpaqueMeshes, r.twoSidedOpaqueMaps, qPrintable(r.bakePath));
    std::fflush(stdout);
    mSceneTriangles = r.triangles;
    QString werr;
    if (!addBasicWorld(mDoc, mOpt.binDir, mOpt.dataRoot, !hasDirectional(model), werr)) {
        error = werr.toStdString();
        return false;
    }
    mDoc->getRootNode()->addChild(model);
    mDoc->getRootNode()->applyStaticDefaults();

    // the camera: the flag, else the .jlab's, else framing the model's bounds from the editor's default direction
    iris::Vec3 mn, mx;
    const bool bounded = worldBounds(mDoc, mn, mx);
    const iris::Vec3 centre = bounded ? (mn + mx) * 0.5f : iris::Vec3(0, 0, 0);
    const float extent = bounded ? (mx - mn).length() : 10.0f;
    mCamera.setExtent(extent);
    if (mOpt.hasCamera) mCamera.set(mOpt.camPos, mOpt.camTarget);
    else if (mJlab.hasCamera) mCamera.set(mJlab.camPos, mJlab.camTarget);
    else if (bounded) mCamera.set(centre + iris::Vec3(0, 5, 14).normalized() * std::max(2.0f, extent * 0.8f), centre);

    // the tier: the flag beats the .jlab
    bool ok = false;
    QString tier = mOpt.tier;
    if (tier.isEmpty()) tier = mJlab.tier;
    const worldmodes::Mode m = worldmodes::modeFromName(tier, &ok);
    if (!ok || m == worldmodes::Mode::Custom) { error = "unknown tier '" + tier.toStdString() + "'"; return false; }
    mTier = int(m);
    worldmodes::setMode(mDoc, m);
    if (mJlab.hasSun) {
        std::function<bool(const iris::SceneNodePtr &)> sun = [&](const iris::SceneNodePtr &n) {
            if (n->getSceneNodeType() == iris::SceneNodeType::Light &&
                n.staticCast<iris::LightNode>()->lightType == iris::LightType::Directional) {
                n->setLocalRot(mJlab.sunRot);
                n->setPropertyValue(QStringLiteral("intensity"), mJlab.sunIntensity);
                return true;
            }
            for (const iris::SceneNodePtr &c : n->children()) if (sun(c)) return true;
            return false;
        };
        sun(mDoc->getRootNode());
    }
    setTitle();
    return true;
}

void Session::setTitle()
{
    if (!mWindow.isOpen()) return;
    const QString name = mJlab.env.isEmpty() ? QFileInfo(mJlab.file).fileName()
                                             : mJlab.env + QStringLiteral(" (seed %1)").arg(mJlab.seed);
    mWindow.setTitle("jahlab - " + name.toStdString() + " - " + tierName(mTier) + " - view " +
                     atomViewNames()[size_t(mAtomView)]);
}

void Session::frame()
{
    if (mWindow.isOpen()) {
        mWindow.pump(mInput);
        if (mInput.closeRequested) mQuit = true;
        if (mInput.resized && mInput.width && mInput.height) {
            mView->resize(mInput.width, mInput.height);
            mCamera.setAspect(float(mInput.width) / float(mInput.height));
        }
        // THE KEYS ARE LINES (labcommands.h): each command key runs its line through the one parser
        const std::vector<std::string> pressed = mInput.pressed;
        for (const std::string &k : pressed) {
            const std::string line = keyLine(k);
            if (!line.empty()) execLine(line, "key");
        }
        mCamera.fly(mInput.held, mInput.dx, mInput.dy);
    }
    // THE ANIMATION CLOCK (`play on`): one fixed step a frame - the engine's own grid (frames, never wall time)
    if (mPlaying) mDoc->advance(1.0f / 60.0f);
    mDoc->refresh();
    SceneMirror::FrameView fv;
    fv.view = mView;
    fv.environment = true;
    fv.sky = true;
    fv.camera = mCamera.node();
    fv.framingAspect = mCamera.node()->framingAspect();
    if (mOpt.headless || mReflectSources != ReflectSources::Both || mHdrReadback) {
        // THE OFFSCREEN CHAIN'S OPT-IN (PostFxDesc::allowOffscreen): the world's post chain on the offscreen view, the
        // editor screenshot's door (enginesceneviewport.cpp takeScreenshot's Viewport grade) - and `reflections rays` /
        // `reflections screen` (the one estimator's sources narrowed to one).
        const bool headless = mOpt.headless;
        const ReflectSources sources = mReflectSources;
        const bool hdr = mHdrReadback;
        fv.finish = [headless, sources, hdr](FrameInputView &part, View *v) {
            PostFxDesc fx = part.postFx ? *part.postFx : v->pushedPostFx();
            if (headless) fx.allowOffscreen = true;
            if (sources != ReflectSources::Both) fx.reflectSources = sources;
            if (hdr) fx.hdrReadback = true;
            part.postFx = fx;
        };
    }
    mMirror->sync(SceneMirror::FrameViews{ { fv } });
    mEngine->renderOneFrame();
    ++mFrames;
    if (mFrames % (kStatsWindowFrames / 2) == 0) mEngine->publishRenderStats(kStatsWindowFrames);
    if (mOverlay && mFrames % kOverlayEveryFrames == 0) pushOverlay();
}

void Session::renderFrames(int n)
{
    for (int i = 0; i < n && !mQuit; ++i) frame();
}

void Session::pushOverlay()
{
    OverlayInput in;
    const RenderStats &rs = mEngine->published().render;
    in.frameMs = rs.frameMs;
    in.fps = rs.fps;
    in.counted = rs.metricsRecording;
    in.sceneTriangles = mSceneTriangles;
    in.submittedTriangles = rs.triangles;
    in.draws = rs.draws;
    const AtomDrawStatus st = mScene->atomDrawStatus();
    in.atomOn = st.on;
    in.cutValid = st.cutValid;
    in.atomItems = st.atomItems;
    in.pbsItems = st.pbsItems;
    in.pending = st.pending;
    in.cutTriangles = st.cutTriangles;
    in.cutClusters = st.cutClusters;
    in.occluded = st.occluded;
    in.disoccluded = st.disoccluded;
    in.tier = tierName(mTier);
    in.view = atomViewNames()[size_t(mAtomView)];
    in.gi = mGi;
    in.occlusion = mOcclusion;
    in.draw = mDraw;
    const iris::Vec3 p = mCamera.position(), t = mCamera.target();
    in.cam[0] = p.x(); in.cam[1] = p.y(); in.cam[2] = p.z();
    in.target[0] = t.x(); in.target[1] = t.y(); in.target[2] = t.z();
    in.frame = mFrames;
    ViewOverlayDesc d;
    d.stats = true;
    d.lines = overlayRows(in);
    d.allowOffscreen = mOpt.headless;
    mView->setOverlay(d);
}

bool Session::setView(int mode)
{
    if (mode != 0 && !mScene->published().atomViewPaintable) {
        labFail("LAB REFUSED view %s: the view carries no id pass (the Low tier's view draws straight into its "
                    "window, or `draw off` shut the split)\n", atomViewNames()[size_t(mode)].c_str());
        return false;
    }
    mScene->setAtomView(AtomView(mode));
    mAtomView = mode;
    setTitle();
    return true;
}

bool Session::shot(const std::string &nameIn)
{
    const std::string name = nameIn.empty() ? "shot-" + std::to_string(++mShotSerial) : nameIn;
    const QString dir = QDir(mOpt.dataRoot).filePath(QStringLiteral("shots"));
    QDir().mkpath(dir);
    QString path = QString::fromStdString(name);
    if (!path.endsWith(QStringLiteral(".png"))) path += QStringLiteral(".png");
    if (QFileInfo(path).isRelative()) path = QDir(dir).filePath(path);
    Image img;
    bool ok = false;
    int extra = 0;
    if (mOpt.headless) {
        ok = mView->readPixels(img);
    } else {
        // THE ON-SCREEN PICTURE (readPixels refuses a swapchain view, Engine.h:1636): the frame capture of the next
        // frame drawn — the bytes on screen exactly as presented (keepHelpers: the Atom view and the HUD included; the
        // lab draws no editor furniture, so that is the picture the owner sees).
        if (mView->requestFrameCapture(true)) {
            for (; extra < 8 && !ok; ++extra) {
                frame();
                ok = mView->takeFrameCapture(img, false);
            }
            if (!ok) ok = mView->takeFrameCapture(img, true);
        }
    }
    if (!ok || !img.width || !img.height) {
        labFail("LAB SHOT FAILED %s: %s\n", qPrintable(path), mEngine->lastError().c_str());
        return false;
    }
    QImage q(img.rgba.data(), int(img.width), int(img.height), int(img.width * 4), QImage::Format_RGBA8888);
    const QImage rgb = q.convertToFormat(QImage::Format_RGB888);
    std::set<unsigned> colours;
    for (int y = 0; y < rgb.height(); y += 2) {
        const uchar *s = rgb.constScanLine(y);
        for (int x = 0; x < rgb.width(); x += 2) colours.insert(unsigned(s[x * 3]) << 16 | unsigned(s[x * 3 + 1]) << 8 | s[x * 3 + 2]);
    }
    if (!rgb.save(path, "PNG")) {
        labFail("LAB SHOT FAILED %s: cannot write\n", qPrintable(path));
        return false;
    }
    // (THE MODE LABEL: the sources the reflection traced this picture with - what it ran, read from the engine)
    const int sources = mScene->published().rayQuery.reflectSources;
    std::printf("LAB SHOT %s %ux%u colours %zu frame %llu sources %s%s\n", qPrintable(path), img.width, img.height,
                colours.size(), mFrames, sources < 0 ? "-" : reflectSourcesName(ReflectSources(sources)),
                extra ? (" (+" + std::to_string(extra) + " frames for the capture)").c_str() : "");
    std::fflush(stdout);
    return true;
}

std::string Session::statusJson()
{
    mEngine->publishRenderStats(kStatsWindowFrames);
    const RenderStats &rs = mEngine->published().render;
    const AtomDrawStatus st = mScene->atomDrawStatus();
    const GiStatus gi = mScene->published().gi;
    const iris::Vec3 p = mCamera.position(), t = mCamera.target();
    std::ostringstream o;
    o.precision(9);
    o << "{\"lab\":{\"frame\":" << mFrames << ",\"ms\":" << msSinceStart() << ",\"file\":" << jstr(mJlab.file)
      << ",\"tier\":\"" << tierName(mTier) << "\",\"view\":\"" << atomViewNames()[size_t(mAtomView)]
      << "\",\"viewPaints\":" << (mAtomView == 0 || mScene->published().atomViewPaintable ? "true" : "false")
      << ",\"gi\":" << (mGi ? "true" : "false") << ",\"occlusion\":" << (mOcclusion ? "true" : "false")
      << ",\"draw\":" << (mDraw ? "true" : "false") << ",\"sceneTriangles\":" << mSceneTriangles
      << ",\"camera\":{\"position\":[" << p.x() << "," << p.y() << "," << p.z() << "],\"target\":[" << t.x() << ","
      << t.y() << "," << t.z() << "]}}";
    o << ",\"atom\":{";
    bool first = true;
    const auto f = [&](const char *k, unsigned long long v) { o << (first ? "" : ",") << "\"" << k << "\":" << v; first = false; };
    const auto b = [&](const char *k, bool v) { o << (first ? "" : ",") << "\"" << k << "\":" << (v ? "true" : "false"); first = false; };
    b("live", st.live); b("on", st.on);
    f("atomItems", st.atomItems); f("atomTwoSided", st.atomTwoSided); f("atomMasked", st.atomMasked);
    f("pbsItems", st.pbsItems); f("notWorld", st.notWorld); f("notPbs", st.notPbs); f("customPiece", st.customPiece);
    f("blended", st.blended); f("cullFront", st.cullFront); f("planar", st.planar); f("pending", st.pending);
    f("alphaTested", st.alphaTested); f("skinned", st.skinned); f("noRow", st.noRow); f("atomCaster", st.atomCaster);
    f("stockItems", st.stockItems); f("materials", st.materials); f("buckets", st.buckets); f("twins", st.twins);
    f("decodeDraws", st.decodeDraws); f("screenDraws", st.screenDraws); f("stereoViews", st.stereoViews);
    f("passthroughViews", st.passthroughViews); b("viewPaintable", st.viewPaintable);
    b("cutValid", st.cutValid); f("cutClusters", st.cutClusters); f("cutIndices", st.cutIndices);
    f("cutEvaluated", st.cutEvaluated); f("cutOverflow", st.cutOverflow); f("cutMissing", st.cutMissing);
    f("cutOverflowIndices", st.cutOverflowIndices); f("cutIndexBudget", st.cutIndexBudget);
    f("cutTriangles", st.cutTriangles);
    b("occlusion", st.occlusion); f("occluded", st.occluded); f("disoccluded", st.disoccluded);
    b("casterValid", st.casterValid); f("casterMaps", st.casterMaps); f("casterClusters", st.casterClusters);
    f("casterInstances", st.casterInstances); f("casterOverflow", st.casterOverflow); f("casterMissing", st.casterMissing);
    f("casterIndexBudget", st.casterIndexBudget); f("casterTriangles", st.casterTriangles);
    f("casterOccluded", st.casterOccluded); f("casterLateInstances", st.casterLateInstances);
    f("casterLateOccluded", st.casterLateOccluded); f("casterLateTriangles", st.casterLateTriangles);
    b("densityValid", st.densityValid); f("densityPixels", st.densityPixels); f("densityObjects", st.densityObjects);
    o << ",\"densityClassPixels\":[" << st.densityClassPixels[0] << "," << st.densityClassPixels[1] << ","
      << st.densityClassPixels[2] << "],\"densityDepthPixels\":[";
    for (int i = 0; i < 16; ++i) o << (i ? "," : "") << st.densityDepthPixels[i];
    o << "],\"frame\":" << st.frame << "}";
    o << ",\"render\":{\"metricsRecording\":" << (rs.metricsRecording ? "true" : "false") << ",\"fps\":" << rs.fps
      << ",\"frameMs\":" << rs.frameMs << ",\"p95Ms\":" << rs.p95Ms << ",\"p99Ms\":" << rs.p99Ms
      << ",\"draws\":" << rs.draws << ",\"batches\":" << rs.batches << ",\"triangles\":" << rs.triangles
      << ",\"vertices\":" << rs.vertices << ",\"instances\":" << rs.instances
      << ",\"gpuCountLagFrames\":" << rs.gpuCountLagFrames << "}";
    // ...and the field's state (PHOTON-7 stage 5, 5-4: the marks, the restarts and refinements owed, the follows) and
    // the surface cache's counters - an agent's per-frame `status file` reads them
    o << ",\"gi\":{\"atRest\":" << (gi.giAtRest ? "true" : "false") << ",\"ifdOwedRestarts\":" << gi.ifdOwedRestarts
      << ",\"ifdOwedRefines\":" << gi.ifdOwedRefines << ",\"ifdWorking\":" << (gi.ifdWorking ? "true" : "false")
      << ",\"ifdMarkedByMoves\":" << gi.ifdMarkedByMoves << ",\"ifdMarkedByRegion\":" << gi.ifdMarkedByRegion
      << ",\"ifdMarkAlls\":" << gi.ifdMarkAlls << ",\"ifdRestarts\":" << gi.ifdRestarts
      << ",\"ifdFollows\":" << gi.ifdFollows << ",\"ifdDispatched\":" << gi.ifdDispatchedLastFrame;
    // ...the screen-probe gather's shape and memory (PHOTON-7 stage 7: the tier's row as it ran, its resident VRAM)
    o << ",\"gather\":{\"running\":" << (gi.gather.running ? "true" : "false") << ",\"stride\":" << gi.gather.stride
      << ",\"raysPerProbe\":" << gi.gather.raysPerProbe << ",\"probes\":" << gi.gather.probes
      << ",\"raysPerFrame\":" << gi.gather.raysPerFrame << ",\"vramBytes\":" << gi.gather.vramBytes
      << ",\"historyBytes\":" << gi.gather.historyBytes << ",\"restFrames\":" << gi.gather.restFrames
      // ...and its twins against their cap (PHOTON-7 stage 10: the cap is an L7 count; a real scene's demand reads it)
      << ",\"adaptive\":" << gi.gather.adaptive << ",\"adaptiveCap\":" << gi.gather.adaptiveCap
      << ",\"adaptiveRequested\":" << gi.gather.adaptiveRequested << "}";
    o << ",\"cards\":{\"captures\":" << gi.cards.captures << ",\"capturesLastFrame\":" << gi.cards.capturesLastFrame
      << ",\"queue\":" << gi.cards.queueLength << ",\"relights\":" << gi.cards.relights << ",\"indirectRelights\":"
      << gi.cards.indirectRelights << ",\"invalidRadiance\":" << gi.cards.invalidRadiance << ",\"invalidIndirect\":"
      << gi.cards.invalidIndirect << ",\"invalidMaterial\":" << gi.cards.invalidMaterial << ",\"invalidTransform\":"
      << gi.cards.invalidTransform << ",\"pages\":" << gi.cards.pages << ",\"pagesUsed\":" << gi.cards.pagesUsed
      << ",\"instancesResident\":" << gi.cards.instancesResident << ",\"cardsResident\":" << gi.cards.cardsResident
      << ",\"residentHash\":\"" << std::hex << gi.cards.residentHash << std::dec << "\",\"restDecisions\":" << gi.cards.restDecisions
      << ",\"decisionRefused\":" << gi.cards.decisionRefused
      // ...and THE ROUNDS (PHOTON-7 stage 10-3, the round chain's read): the newest relight reading's largest move in codes
      // and the cache frame it was relit at, the deepest round among the last plan's roots and those roots (kind+frame),
      // the visible and sub-quantum relight frames
      << ",\"moveCodesLast\":" << gi.cards.moveCodesLast << ",\"moveCodesFrame\":" << gi.cards.moveCodesFrame
      << ",\"workRound\":" << gi.cards.workRound << ",\"workRoots\":\"" << [&gi] {
             std::string r;
             for (const auto &w : gi.cards.workRoots) r += std::string(1, w.kind) + std::to_string(w.frame) + " ";
             return r;
         }() << "\",\"visibleRelights\":" << gi.cards.visibleRelights << ",\"subQuantumRelights\":"
      << gi.cards.subQuantumRelights << ",\"cacheFrame\":" << gi.cards.cacheFrame
      << ",\"indirectReported\":" << gi.cards.indirectReported << ",\"indirectByRegion\":" << gi.cards.indirectByRegion
      << ",\"indirectOverflows\":" << gi.cards.indirectOverflows << ",\"admitted\":" << gi.cards.admitted
      << ",\"evicted\":" << gi.cards.evicted << ",\"compactionMoves\":" << gi.cards.compactionMoves
      << ",\"demandHits\":[" << gi.cards.demandHits[0] << "," << gi.cards.demandHits[1] << "," << gi.cards.demandHits[2]
      << "," << gi.cards.demandHits[3] << "],\"lampTraces\":" << gi.cards.lampTraces << ",\"cardLampsMax\":"
      << gi.cards.cardLampsMax << ",\"lampOverflowShareMax\":" << gi.cards.lampOverflowShareMax
      << ",\"cardAreaLampsMax\":" << gi.cards.cardAreaLampsMax << ",\"cardsMultiArea\":" << gi.cards.cardsMultiArea
      << ",\"cardsSharedFractional\":" << gi.cards.cardsSharedFractional << "}";
    // ...and THE CASCADES' SCROLL WORK (PHOTON-7 stage 2, the work law's door; cumulative since the chain was built):
    // the commits, the voxels their delta injections lit in the slabs and outside them (read back a few frames late:
    // `counted` injections), the slabs' cells, the cells the commits computed the direct over, the voxels every delta
    // MARCHED (PHOTON-7 stage 4: an outer commit's inner re-marches included), the lag (the frames from a step to the
    // rebuild that served it), and THE FEEDBACK (stage 4): the frames it ran a pass, the bricks those passes ran, its
    // stops and resumes, the bricks active by its newest reading; the chain's rests
    o << ",\"commitShaderWaits\":" << gi.cascadeCommitShaderWaits << ",\"cascades\":[";
    for (size_t i = 0; i < gi.cascades.size(); ++i) {
        const GiStatus::CascadeStatus &cs = gi.cascades[i];
        o << (i ? "," : "") << "{\"res\":" << cs.resolution << ",\"scrolls\":" << cs.scrolls << ",\"counted\":"
          << cs.commitCounted << ",\"litSlab\":" << cs.commitLitSlab << ",\"litBand\":" << cs.commitLitBand
          << ",\"slabCells\":" << cs.commitSlabCells << ",\"injectCells\":" << cs.commitInjectCells
          << ",\"marched\":" << cs.deltaMarched << ",\"fbPasses\":" << cs.feedbackPasses << ",\"fbBricksRan\":"
          << cs.feedbackBricksRan << ",\"fbStops\":" << cs.feedbackStops << ",\"fbResumes\":" << cs.feedbackResumes
          << ",\"fbActive\":" << cs.feedbackActive << ",\"fbCarried\":" << cs.feedbackCarried
          << ",\"relightCarried\":" << cs.relightCarried << ",\"lagLast\":"
          << cs.lagLast << ",\"lagMax\":" << cs.lagMax << ",\"lightsInRangeMax\":" << cs.lightsInRangeMax
          << ",\"injectionsOverSlots\":" << cs.injectionsOverSlots << "}";
    }
    o << "],\"feedbackRests\":" << gi.feedbackRests << "}";
    // ...and THE HIT LIST (PHOTON-2 stage 2's tail): its demand-sized capacity, the records the traces appended and the
    // ones a full list dropped (no sample) - read back a few frames late
    {
        const RayQueryStatus &rq = mScene->published().rayQuery;
        o << ",\"hits\":{\"records\":" << rq.hitRecords << ",\"dropped\":" << rq.hitDropped << ",\"capacity\":"
          << rq.hitCapacity << "}";
        o << ",\"escapes\":{\"records\":" << rq.escapeRecords << ",\"dropped\":" << rq.escapeDropped
          << ",\"capacity\":" << rq.escapeCapacity << ",\"traced\":" << rq.escapeTraced << ",\"card\":" << rq.escapeCard
          << ",\"voxel\":" << rq.escapeVoxel << ",\"direct\":" << rq.escapeDirect << ",\"far\":" << rq.escapeFar
          << ",\"sky\":" << rq.escapeSky << ",\"back\":" << rq.escapeBack << ",\"noSample\":" << rq.escapeNoSample
          << ",\"saturated\":" << rq.escapeSaturated
          << "}";
        o << ",\"panesOverflow\":" << rq.panesOverflow;
        // ...and THE RAY STRUCTURES (PHOTON-7 stage 6, one ray level per mesh): the near and far copies, the bottom
        // levels held and their bytes, the ones built from a level above 0 (the far copies'), the top level, and the
        // bottom-level builds ever
        o << ",\"rays\":{\"instances\":" << rq.instances << ",\"farInstances\":" << rq.farInstances
          << ",\"blasCount\":" << rq.blasCount << ",\"blasBytes\":" << rq.blasBytes << ",\"levelBlasCount\":"
          << rq.levelBlasCount << ",\"levelBlasBytes\":" << rq.levelBlasBytes << ",\"tlasBytes\":" << rq.tlasBytes
          << ",\"triangles\":" << rq.triangles << ",\"blasBuilds\":" << rq.blasBuilds << "}";
    }
    // THE LAST CUT PROBE (`probe` / `measure`): the drawn triangles of its frame summed over the objects, and those of
    // the objects with no visible pixel — the hidden share's numerator (the probe's frame, said beside the status's)
    if (mProbed)
        o << ",\"probe\":{\"frame\":" << mLastProbeFrame << ",\"objects\":" << mLastProbe.objects
          << ",\"atomPixels\":" << mLastProbe.atomPixels << ",\"drawnTriangles\":" << mLastProbe.drawnTriangles
          << ",\"hiddenTriangles\":" << mLastProbe.hiddenTriangles << ",\"groupsDropped\":" << mLastProbe.groupsDropped
          << ",\"tolerance\":" << mLastProbe.tolerance << ",\"viewportHeight\":" << mLastProbe.viewportHeight << "}";
    else
        o << ",\"probe\":null";
    {
        // THE REFLECTION'S REST AND ITS NEE (PHOTON-7 stage 11): the rest as the last view drew it, the emitter table and
        // the cone's hit rate (RayQueryStatus)
        const RayQueryStatus &rq = mScene->published().rayQuery;
        o << ",\"reflect\":{\"sources\":" << rq.reflectSources << ",\"restFrames\":" << rq.reflectRestFrames
          << ",\"held\":" << (rq.reflectHeld ? "true" : "false") << ",\"restActive\":" << rq.reflectRestActive
          << ",\"restCapped\":" << rq.reflectRestCapped << ",\"emitters\":" << rq.reflectEmitters << ",\"portals\":" << rq.reflectPortals
          << ",\"emittersOver\":" << rq.reflectEmittersOver << ",\"neeRays\":" << rq.reflectNeeRays
          << ",\"neeHits\":" << rq.reflectNeeHits << ",\"restBound\":" << rq.reflectRestBound
          << ",\"restBoundOwed\":" << rq.reflectRestBoundOwed << ",\"restNeedMax\":" << rq.reflectRestNeedMax << "}";
    }
    o << "}";
    return o.str();
}

bool Session::execLine(const std::string &line, const char *source)
{
    Command c;
    std::string err;
    gLabLastFailure.clear();
    if (!parse(line, c, err)) {
        if (!err.empty()) {
            labFail("LAB REFUSED (%s) %s\n", source, err.c_str());
            std::fflush(stdout);
            if (std::string(source) == "script") mExit = 1;   // a script line the grammar refuses fails the run
        }
        return err.empty();
    }
    c.source = source;
    return exec(c);
}

bool Session::exec(const Command &c)
{
    const std::string a0 = c.args.empty() ? std::string() : c.args[0];
    bool ok = true;
    bool v = false;
    if (c.verb == "camera") {
        const auto n = [&](int i) { return float(std::strtod(c.args[size_t(i)].c_str(), nullptr)); };
        mCamera.set(iris::Vec3(n(0), n(1), n(2)), iris::Vec3(n(3), n(4), n(5)));
    } else if (c.verb == "view") {
        int mode = -1;
        if (a0 == "next") mode = (mAtomView + 1) % int(atomViewNames().size());
        for (size_t i = 0; i < atomViewNames().size(); ++i) if (atomViewNames()[i] == a0) mode = int(i);
        if (mode < 0) { labFail("LAB REFUSED view '%s'\n", a0.c_str()); ok = false; }
        else ok = setView(mode);
    } else if (c.verb == "tier") {
        int t = -1;
        if (a0 == "next") t = (mTier + 1) % 4;
        for (size_t i = 0; i < tierNames().size(); ++i) if (tierNames()[i] == a0) t = int(i);
        if (t < 0) { labFail("LAB REFUSED tier '%s'\n", a0.c_str()); ok = false; }
        else {
            mTier = t;
            // THE APP'S TABLE (irisgl worldmodes::setMode, world.mode's call): every tier row, Photon at the tier's name
            worldmodes::setMode(mDoc, worldmodes::Mode(t));
            if (!mGi) worldmodes::setPhoton(mDoc, false, worldmodes::photonTier(mDoc));
            if (mAtomView != 0 && t == int(worldmodes::Mode::Low))
                std::printf("LAB NOTE the %s view paints nothing at Low (its view carries no id pass); the status says "
                            "viewPaints false\n", atomViewNames()[size_t(mAtomView)].c_str());
            setTitle();
        }
    } else if (c.verb == "gi") {
        if (!onOff(a0, mGi, v)) { ok = false; labFail("LAB REFUSED gi '%s'\n", a0.c_str()); }
        else { mGi = v; worldmodes::setPhoton(mDoc, v, worldmodes::photonTier(mDoc)); }
    } else if (c.verb == "occlusion") {
        if (!onOff(a0, mOcclusion, v)) { ok = false; labFail("LAB REFUSED occlusion '%s'\n", a0.c_str()); }
        else { mOcclusion = v; mScene->setAtomOcclusionEnabled(v); }
    } else if (c.verb == "draw") {
        if (!onOff(a0, mDraw, v)) { ok = false; labFail("LAB REFUSED draw '%s'\n", a0.c_str()); }
        else { mDraw = v; mScene->setAtomDrawEnabled(v); }
    } else if (c.verb == "reflections") {
        // THE REFLECTION'S SOURCES (the agreement row's door): both = the one estimator, rays = the rays alone (the
        // screen sources leave the chain: a graph rebuild, the histories restart), screen = the screen alone
        if (a0 == "both" || a0 == "rays" || a0 == "screen") {
            mReflectSources = a0 == "both" ? ReflectSources::Both : a0 == "rays" ? ReflectSources::Rays
                                                                                : ReflectSources::Screen;
            std::printf("LAB REFLECTIONS %s\n", a0.c_str());
        } else {
            ok = false;
            labFail("LAB REFUSED reflections '%s' (both|rays|screen)\n", a0.c_str());
        }
    } else if (c.verb == "play") {
        if (!onOff(a0, mPlaying, v)) { ok = false; labFail("LAB REFUSED play '%s'\n", a0.c_str()); }
        else {
            mPlaying = v;
            mDoc->setPlaying(v);
            // what plays: the nodes carrying an animation, and how many of them a property track (position, rotation,
            // scale: the document's own) moves - a skeletal clip is the engine's (the mirror pushes the clock)
            int animated = 0, tracks = 0, clips = 0;
            std::function<void(iris::SceneNode *)> walk = [&](iris::SceneNode *n) {
                if (!n) return;
                if (const iris::AnimationPtr a = n->getAnimation()) {
                    ++animated;
                    if (a->hasPropertyAnim(QStringLiteral("position")) || a->hasPropertyAnim(QStringLiteral("rotation")) ||
                        a->hasPropertyAnim(QStringLiteral("scale")))
                        ++tracks;
                }
                clips += int(n->getAnimations().size());
                for (int i = 0; i < n->childCount(); ++i) walk(n->childAt(i));
            };
            walk(mDoc->getRootNode().data());
            std::printf("LAB PLAY %s: %d nodes carry an animation (%d with a property track), %d clips in all\n",
                        v ? "on" : "off", animated, tracks, clips);
        }
    } else if (c.verb == "overlay") {
        if (!onOff(a0, mOverlay, v)) { ok = false; labFail("LAB REFUSED overlay '%s'\n", a0.c_str()); }
        else {
            mOverlay = v;
            if (v) pushOverlay();
            else mView->setOverlay(ViewOverlayDesc());
        }
    } else if (c.verb == "frames") {
        const int n = std::atoi(a0.c_str());
        if (n < 0) ok = false;
        else renderFrames(n);
    } else if (c.verb == "settle") {
        const int cap = a0.empty() ? kSettleDefaultCap : std::max(1, std::atoi(a0.c_str()));
        const double t0 = msSinceStart();
        unsigned long long last = ~0ull;
        int steady = 0, n = 0;
        AtomDrawStatus st;
        for (; n < cap && !mQuit && steady < kSettleSteadyFrames; ++n) {
            frame();
            st = mScene->atomDrawStatus();
            const bool still = st.pending == 0 && (!st.on || st.cutValid) && st.cutTriangles == last;
            steady = still ? steady + 1 : 0;
            last = st.cutTriangles;
        }
        std::printf("LAB SETTLE %s after %d frames (%.0f ms; %.0f ms since start): cutTriangles %llu, pending %u, "
                    "giAtRest %d\n", steady >= kSettleSteadyFrames ? "steady" : "NOT STEADY (cap)", n,
                    msSinceStart() - t0, msSinceStart(), (unsigned long long)st.cutTriangles, st.pending,
                    int(mScene->published().gi.giAtRest));
        ok = steady >= kSettleSteadyFrames;
    } else if (c.verb == "rest") {
        // THE GI's REST (HARNESS-3 (a)): a converged picture is the one GiStatus::giAtRest says has stopped moving - the
        // chain's owed work, the cards' rest decision, the gather's settled history (settleFramesOf) and its hold - never a
        // frame count (gi.walk_rough's picked 600: the predicate held at 172-245 frames after a pose change). One frame at
        // least, so a status published before a camera move never answers for the pose after it.
        // `held` (HARNESS-3 (f)): and every reader HELD - the gather held and, where the reflection traces, the reflection
        // held: what the stillness door's freeze requires (Stillness.cpp), since giAtRest's reflection term is its hand-over
        const int cap = a0.empty() ? kRestDefaultCap : std::max(1, std::atoi(a0.c_str()));
        const bool held = c.args.size() > 1 && c.args[1] == "held";
        const double t0 = msSinceStart();
        int n = 0;
        bool rest = false;
        while (n < cap && !mQuit && !rest) {
            frame();
            ++n;
            const auto &pub = mScene->published();
            rest = pub.gi.giAtRest &&
                   (!held || (pub.gi.gather.running && pub.gi.gather.held && (!pub.rayQuery.reflect || pub.rayQuery.reflectHeld)));
        }
        if (rest)
            std::printf("LAB REST after %d frames (%.0f ms)%s\n", n, msSinceStart() - t0, held ? ", every reader held" : "");
        else
            labFail("LAB REFUSED rest: the GI not at rest%s after %d frames (the cap)\n", held ? " and held" : "", n);
        ok = rest;
    } else if (c.verb == "shot") {
        ok = shot(a0);
    } else if (c.verb == "status") {
        const std::string js = statusJson();
        if (a0.empty()) std::printf("LAB STATUS %s\n", js.c_str());
        else {
            QString path = QString::fromStdString(a0);
            if (QFileInfo(path).isRelative()) path = QDir(mOpt.dataRoot).filePath(path);
            std::ofstream out(path.toStdString(), std::ios::app);
            out << js << "\n";
            ok = bool(out);
            std::printf("LAB STATUS -> %s\n", qPrintable(path));
        }
    } else if (c.verb == "cardat") {
        // THE CARD AT A WORLD POINT against the drawn pixel (labcommands.cpp's text)
        float P[3] = { 0, 0, 0 }, N[3] = { 0, 0, 1 };
        ok = std::sscanf(a0.c_str(), "%f,%f,%f", &P[0], &P[1], &P[2]) == 3 &&
             std::sscanf(c.args[1].c_str(), "%f,%f,%f", &N[0], &N[1], &N[2]) == 3;
        CardSample cs;
        if (ok) ok = mScene->readCardAt(Vec3(P[0], P[1], P[2]), Vec3(N[0], N[1], N[2]), cs) && cs.ok;
        if (!ok) labFail("LAB FAILED cardat: '%s %s' - no card answers that point and normal\n", a0.c_str(), c.args[1].c_str());
        else {
            if (!mHdrReadback) mHdrReadback = true;
            renderFrames(2);
            ImageF img;
            const bool hdrOk = mView->readPixelsHdr(img);
            const auto da = labDiffuseAlbedo;
            const auto daHemi = labDiffuseAlbedoHemi;
            const auto dot = [](const double *u, const double *v) { return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]; };
            const auto unit = [&](double *u) { const double l = std::sqrt(dot(u, u)); if (l > 0.0) for (int k = 0; k < 3; ++k) u[k] /= l; };
            const iris::Vec3 ce = mCamera.position(), ct = mCamera.target();
            const double T[3] = { cs.position[0], cs.position[1], cs.position[2] }, E[3] = { ce.x(), ce.y(), ce.z() },
                         G[3] = { ct.x(), ct.y(), ct.z() };
            double Nw[3] = { cs.worldNormal[0], cs.worldNormal[1], cs.worldNormal[2] }, V[3] = { E[0] - T[0], E[1] - T[1], E[2] - T[2] };
            unit(Nw);
            unit(V);
            const double NdotV = std::max(0.0, dot(Nw, V));
            const double r = std::sqrt(std::max(0.0, double(cs.roughness)));
            const double envFactor = da(NdotV, r) / std::max(daHemi(r), 1e-4);
            // the texel's point in the picture (the lab's camera: its vertical angle, the view's aspect)
            double f[3] = { G[0] - E[0], G[1] - E[1], G[2] - E[2] };
            unit(f);
            double rt[3] = { -f[2], 0.0, f[0] };
            unit(rt);
            const double up[3] = { rt[1] * f[2] - rt[2] * f[1], rt[2] * f[0] - rt[0] * f[2], rt[0] * f[1] - rt[1] * f[0] };
            const double dv[3] = { T[0] - E[0], T[1] - E[1], T[2] - E[2] };
            const double xc = dot(dv, rt), yc = dot(dv, up), zc = dot(dv, f);
            const double tanH = std::tan(0.5 * double(mCamera.node()->angle) * 3.14159265358979 / 180.0);
            const double aspect = img.width && img.height ? double(img.width) / double(img.height) : 1.0;
            const int px = int(std::floor((0.5 + 0.5 * xc / (zc * tanH * aspect)) * img.width));
            const int py = int(std::floor((0.5 - 0.5 * yc / (zc * tanH)) * img.height));
            double pix = -1.0, mean = 0.0;
            int n = 0;
            if (hdrOk && zc > 0.0 && px >= 1 && py >= 1 && px + 1 < int(img.width) && py + 1 < int(img.height)) {
                const auto br = [&](int x, int y) { const auto p = img.at(unsigned(x), unsigned(y)); return std::max(p.r, std::max(p.g, p.b)); };
                pix = br(px, py);
                for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) { mean += br(px + x, py + y); ++n; }
                mean /= n;
            }
            const auto mx = [](const float *v) { return double(std::max(v[0], std::max(v[1], v[2]))); };
            const double ind = mx(cs.indirect), rad = mx(cs.radiance), fl = mx(cs.flat);
            std::printf("LAB CARDAT card %d texel (%u, %u) point (%.4f %.4f %.4f) n (%.3f %.3f %.3f) r %.3f | stored: radiance %.6f "
                        "(V = N), indirect %.6f, flat %.6f, direct %.6f | toward the eye: N.V %.4f, the view term %.4f, the "
                        "indirect half %.6f | the pixel (%d, %d): %.6f, 3x3 %.6f | the card's indirect toward the eye / the "
                        "pixel %+.2f %% (3x3 %+.2f %%)\n", cs.card, cs.texelX, cs.texelY, T[0], T[1], T[2], Nw[0], Nw[1], Nw[2], r,
                        rad, ind, fl, rad - ind - fl, NdotV, envFactor, ind * envFactor, px, py, pix, mean,
                        pix > 0.0 ? 100.0 * (ind * envFactor / pix - 1.0) : 0.0, mean > 0.0 ? 100.0 * (ind * envFactor / mean - 1.0) : 0.0);
            ok = hdrOk;
            if (!hdrOk) labFail("LAB FAILED cardat: the view's float readback: %s\n", mEngine->lastError().c_str());
        }
    } else if (c.verb == "hemi") {
        // THE IRRADIANCE AT A CARD TEXEL THREE WAYS (REDS-1's three-way; labcommands.cpp's text)
        float P[3] = { 0, 0, 0 }, Nn[3] = { 0, 0, 1 };
        ok = std::sscanf(a0.c_str(), "%f,%f,%f", &P[0], &P[1], &P[2]) == 3 &&
             std::sscanf(c.args[1].c_str(), "%f,%f,%f", &Nn[0], &Nn[1], &Nn[2]) == 3;
        const int m = std::max(4, std::min(256, std::atoi(c.args[2].c_str())));
        CardSample cs;
        if (ok) ok = mScene->readCardAt(Vec3(P[0], P[1], P[2]), Vec3(Nn[0], Nn[1], Nn[2]), cs) && cs.ok;
        if (!ok) labFail("LAB FAILED hemi: no card answers '%s %s'\n", a0.c_str(), c.args[1].c_str());
        else {
            if (!mHdrReadback) mHdrReadback = true;
            renderFrames(2);
            const auto dot = [](const double *u, const double *v) { return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]; };
            const auto unit = [&](double *u) { const double l = std::sqrt(dot(u, u)); if (l > 0.0) for (int k = 0; k < 3; ++k) u[k] /= l; };
            const double T[3] = { cs.position[0], cs.position[1], cs.position[2] };
            double Nw[3] = { cs.worldNormal[0], cs.worldNormal[1], cs.worldNormal[2] };
            unit(Nw);
            const double r = std::sqrt(std::max(0.0, double(cs.roughness)));
            const double alb = double(cs.albedo[1]) * 3.14159265358979;   // kD x pi: the albedo
            // (a) THE CARD'S GATHER LAYER (its last TLAS gather's envColourD) and the check that the indirect half is it
            const double gA = cs.gather[1];
            const double gCheck = cs.indirect[1] / std::max(1e-12, alb * labDiffuseAlbedoHemi(r));
            // (b) THE SCREEN PROBES' IRRADIANCE AT THE PIXEL: the diffuse view's pixel over albedo x DA(N.V, r)
            ImageF img;
            double gB = -1.0;
            const iris::Vec3 ce = mCamera.position(), ct = mCamera.target();
            const double E[3] = { ce.x(), ce.y(), ce.z() }, G[3] = { ct.x(), ct.y(), ct.z() };
            if (mView->readPixelsHdr(img) && img.width > 2 && img.height > 2) {
                double f[3] = { G[0] - E[0], G[1] - E[1], G[2] - E[2] };
                unit(f);
                double rt[3] = { -f[2], 0.0, f[0] };
                unit(rt);
                const double up[3] = { rt[1] * f[2] - rt[2] * f[1], rt[2] * f[0] - rt[0] * f[2], rt[0] * f[1] - rt[1] * f[0] };
                const double dv[3] = { T[0] - E[0], T[1] - E[1], T[2] - E[2] };
                const double zc = dot(dv, f), tanH = std::tan(0.5 * double(mCamera.node()->angle) * 3.14159265358979 / 180.0);
                const double aspect = double(img.width) / double(img.height);
                const int px = int(std::floor((0.5 + 0.5 * dot(dv, rt) / (zc * tanH * aspect)) * img.width));
                const int py = int(std::floor((0.5 - 0.5 * dot(dv, up) / (zc * tanH)) * img.height));
                double V[3] = { E[0] - T[0], E[1] - T[1], E[2] - T[2] };
                unit(V);
                if (zc > 0.0 && px >= 0 && py >= 0 && px < int(img.width) && py < int(img.height))
                    gB = double(img.at(unsigned(px), unsigned(py)).g) /
                         std::max(1e-12, alb * labDiffuseAlbedo(std::max(0.0, dot(Nw, V)), r));
            }
            // (c) THE BRUTE-FORCE HEMISPHERE AT THE TEXEL: m x m stratified cosine rays through the TLAS; a hit on the
            // mirror floor (photon-contact's: y 0.02, roughness 0, metal 1, F0 0.9) continues once by reflection weighted
            // by Schlick's F; a diffuse hit reads that surface's card toward the ray (readCardAt's stored radiance, at
            // V = N: no view restore, stated); a miss, or a hit no card answers, is counted
            double b0[3] = { 1, 0, 0 };
            if (std::fabs(Nw[0]) > 0.9) { b0[0] = 0; b0[1] = 1; }
            double tA[3] = { Nw[1] * b0[2] - Nw[2] * b0[1], Nw[2] * b0[0] - Nw[0] * b0[2], Nw[0] * b0[1] - Nw[1] * b0[0] };
            unit(tA);
            const double tB[3] = { Nw[1] * tA[2] - Nw[2] * tA[1], Nw[2] * tA[0] - Nw[0] * tA[2], Nw[0] * tA[1] - Nw[1] * tA[0] };
            const auto trace = [&](const double *o, const double *dir, double &dist) {
                std::vector<float> rays = { float(o[0]), float(o[1]), float(o[2]), 0.0f, float(dir[0]), float(dir[1]), float(dir[2]),
                                            100.0f, 0.0f, 0.0f, 0.0f, 0.0f };
                unsigned mask = 0xFFu;
                std::memcpy(&rays[8], &mask, sizeof(float));   // the mask BIT-COPIED (ENGINE.md: a float(mask) traces nothing)
                std::vector<float> hits;
                dist = -1.0;
                if (mScene->traceRays(rays, hits) && hits.size() >= 4) dist = hits[0];
                return dist >= 0.0;
            };
            double sum = 0.0;
            unsigned n = 0, misses = 0, noCard = 0, mirror = 0;
            const double lift = 0.002;
            const double O[3] = { T[0] + Nw[0] * lift, T[1] + Nw[1] * lift, T[2] + Nw[2] * lift };
            for (int i = 0; i < m; ++i)
                for (int j = 0; j < m; ++j) {
                    const double u = (i + 0.5) / m, ph = 6.283185307179586 * (j + 0.5) / m, rr = std::sqrt(u), zz = std::sqrt(1.0 - u);
                    double dir[3];
                    for (int k = 0; k < 3; ++k) dir[k] = tA[k] * rr * std::cos(ph) + tB[k] * rr * std::sin(ph) + Nw[k] * zz;
                    ++n;
                    double dist;
                    if (!trace(O, dir, dist)) { ++misses; continue; }
                    double H[3] = { O[0] + dir[0] * dist, O[1] + dir[1] * dist, O[2] + dir[2] * dist };
                    double w = 1.0;
                    double d2[3] = { dir[0], dir[1], dir[2] };
                    if (std::fabs(H[1] - 0.02) < 0.003 && dir[1] < 0.0) {   // the mirror floor: one reflection
                        ++mirror;
                        const double c = std::min(1.0, -dir[1]);
                        w = 0.9 + 0.1 * std::pow(1.0 - c, 5.0);
                        d2[1] = -d2[1];
                        const double O2[3] = { H[0], H[1] + 0.001, H[2] };
                        if (!trace(O2, d2, dist)) { ++misses; continue; }
                        for (int k = 0; k < 3; ++k) H[k] = O2[k] + d2[k] * dist;
                    }
                    CardSample hs;
                    if (!mScene->readCardAt(Vec3(float(H[0]), float(H[1]), float(H[2])), Vec3(float(-d2[0]), float(-d2[1]), float(-d2[2])), hs) || !hs.ok) {
                        ++noCard;
                        continue;
                    }
                    sum += w * double(hs.radiance[1]);
                }
            const double gC = n ? sum / double(n) : -1.0;
            std::printf("LAB HEMI texel (%.4f %.4f %.4f) n (%.3f %.3f %.3f) r %.3f albedo %.4f | (a) the card's gather %.6f (its indirect "
                        "half's own %.6f) | (b) the screen probes' irradiance %.6f | (c) the brute-force hemisphere %.6f (%u rays: %u on "
                        "the mirror, %u misses, %u with no card) | a/c %+.2f %%, b/c %+.2f %%, a/b %+.2f %%\n",
                        T[0], T[1], T[2], Nw[0], Nw[1], Nw[2], r, alb, gA, gCheck, gB, gC, n, mirror, misses, noCard,
                        gC > 0 ? 100.0 * (gA / gC - 1.0) : 0.0, gC > 0 ? 100.0 * (gB / gC - 1.0) : 0.0, gB > 0 ? 100.0 * (gA / gB - 1.0) : 0.0);
        }
    } else if (c.verb == "sideshares") {
        GiVoxelVolume vol;
        GiVoxelSideShares sh;
        ok = mScene->giVoxelLight(0, vol) && vol.available && mScene->giVoxelSideShares(sh) && sh.available &&
             sh.width == vol.width && sh.height == vol.height && sh.depth == vol.depth;
        if (!ok) {
            labFail("LAB FAILED sideshares: no cascade-0 volume or shares (or their grids differ)\n");
        } else if (FILE *f = std::fopen(a0.c_str(), "w")) {
            std::fprintf(f, "# x y z  cx cy cz  nx ny nz  front back twoSided  (cell %.4f %.4f %.4f)\n", vol.cell[0],
                         vol.cell[1], vol.cell[2]);
            unsigned n = 0u;
            for (int z = 0; z < vol.depth; ++z)
                for (int y = 0; y < vol.height; ++y)
                    for (int x = 0; x < vol.width; ++x) {
                        const size_t i = (size_t(z) * vol.height + y) * vol.width + x;
                        const uint32_t w = sh.words[i];
                        if (!(w & (1u << 16u))) continue;
                        const float *nr = &vol.normal[i * 4u];
                        std::fprintf(f, "%d %d %d %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %d\n", x, y, z,
                                     vol.origin[0] + (x + 0.5f) * vol.cell[0], vol.origin[1] + (y + 0.5f) * vol.cell[1],
                                     vol.origin[2] + (z + 0.5f) * vol.cell[2], nr[0] * 2.0f - 1.0f, nr[1] * 2.0f - 1.0f,
                                     nr[2] * 2.0f - 1.0f, float(w & 255u) / 255.0f, float((w >> 8u) & 255u) / 255.0f,
                                     int((w >> 17u) & 1u));
                        ++n;
                    }
            std::fclose(f);
            std::printf("LAB SIDESHARES %u voxels -> %s (grid %dx%dx%d)\n", n, a0.c_str(), vol.width, vol.height, vol.depth);
        } else {
            ok = false;
            labFail("LAB FAILED sideshares: cannot write %s\n", a0.c_str());
        }
    } else if (c.verb == "pick") {
        const int px = std::atoi(c.args[0].c_str()), py = std::atoi(c.args[1].c_str());
        const int n = std::min(std::max(std::atoi(c.args[2].c_str()), 1), 60);
        const double v = 1.0 + double(px) + 4096.0 * double(py);
        mEngine->request([&](Engine &e) { ok = e.setArm("reflect.pick", v); });
        if (ok) {
            renderFrames(n + 4);   // (+ the ring's frames in flight: the last n slots have landed)
            const RayQueryStatus rq = mScene->published().rayQuery;
            mEngine->request([&](Engine &e) { e.setArm("reflect.pick", 0.0); });
            FILE *f = std::fopen(c.args[3].c_str(), "w");
            if (!f || rq.reflectPick.size() < 64u * 64u || rq.reflectPickFrames.size() < 64u) {
                ok = false;
                labFail("LAB FAILED pick: %s\n", f ? "no ring published (no ray tier's reflection?)" : "cannot write the file");
                if (f) std::fclose(f);
            } else {
                // (this pick's frames alone: the newest n + 4 dispatches - an older slot is an earlier pick's, never this texel's)
                uint32_t newest = 0u;
                for (unsigned i = 0; i < 64u; ++i) newest = std::max(newest, rq.reflectPickFrames[i]);
                std::vector<unsigned> order;
                for (unsigned i = 0; i < 64u; ++i)
                    if ((rq.reflectPick[i * 64u] || rq.reflectPick[i * 64u + 32u]) &&
                        rq.reflectPickFrames[i] + uint32_t(n + 4) > newest)
                        order.push_back(i);
                std::sort(order.begin(), order.end(), [&](unsigned a, unsigned b) { return rq.reflectPickFrames[a] < rq.reflectPickFrames[b]; });
                const auto fv = [&](unsigned i, unsigned k) { float x; std::memcpy(&x, &rq.reflectPick[i * 64u + k], 4); return x; };
                std::fprintf(f, "# frame flags slot src hitT  L.xyz  traced.rgb  c2.rgb  nee.rgb  screen.r vouch gain delta.r  vNow restStop prim c2hit(slot t brdf.r portal.r hitLampSpec.r) mirror(slot part.r) cut.r"
                                " | e share v stop flags LD answer.rgb m2 sample.rgb\n");
                for (unsigned i : order) {
                    const uint32_t *w = &rq.reflectPick[i * 64u];
                    std::fprintf(f, "%u %u %u %u %.4f  %.4f %.4f %.4f  %.5f %.5f %.5f  %.5f %.5f %.5f  %.5f %.5f %.5f  %.5f %.3f %.3f %.5f  %u %u %u |",
                                 rq.reflectPickFrames[i], w[1], w[2], w[3], fv(i, 4), fv(i, 5), fv(i, 6), fv(i, 7), fv(i, 8), fv(i, 9),
                                 fv(i, 10), fv(i, 11), fv(i, 12), fv(i, 13), fv(i, 14), fv(i, 15), fv(i, 16), fv(i, 17), fv(i, 18),
                                 fv(i, 19), fv(i, 20), w[21], w[22], w[23]);
                    std::fprintf(f, " c2hit %d %.4f %.5f %.5f %.5f mirror %d %.5f", w[24] == 0xFFFFFFFFu ? -1 : int(w[24]), fv(i, 25), fv(i, 26), fv(i, 27),
                                 fv(i, 28), w[30] == 0xFFFFFFFFu ? -1 : int(w[30]), fv(i, 29));
                    std::fprintf(f, " cut %.5f", fv(i, 31));
                    if (w[32])
                        std::fprintf(f, " %.6f %.6f %.0f %u %u %.5f  %.5f %.5f %.5f  %.6f  %.5f %.5f %.5f", fv(i, 33), fv(i, 34),
                                     fv(i, 35), w[36], w[37], fv(i, 38), fv(i, 39), fv(i, 40), fv(i, 41), fv(i, 42), fv(i, 43),
                                     fv(i, 44), fv(i, 45));
                    std::fprintf(f, "\n");
                }
                std::fclose(f);
                std::printf("LAB PICK (%d, %d) %zu frames -> %s\n", px, py, order.size(), c.args[3].c_str());
            }
        } else {
            labFail("LAB REFUSED pick: no arm reflect.pick\n");
        }
    } else if (c.verb == "colourpick") {
        const RayQueryStatus rq = mScene->published().rayQuery;
        FILE *f = std::fopen(a0.c_str(), "w");
        constexpr unsigned kSlots = 256u, kWords = 128u, kRec = 31u;
        if (!f || rq.colourPick.size() < size_t(kSlots) * kWords || rq.colourPickFrames.size() < kSlots) {
            ok = false;
            labFail("LAB FAILED colourpick: %s\n", f ? "no ring published (the arm colour.pick off, or no ray tier)" : "cannot write the file");
            if (f) std::fclose(f);
        } else {
            std::vector<unsigned> order;
            for (unsigned i = 0; i < kSlots; ++i)
                if (rq.colourPickFrames[i]) order.push_back(i);
            std::sort(order.begin(), order.end(), [&](unsigned a, unsigned b) { return rq.colourPickFrames[a] < rq.colourPickFrames[b]; });
            const auto fv = [&](size_t k) { float x; std::memcpy(&x, &rq.colourPick[k], 4); return x; };
            std::fprintf(f, "# engine_frame k/n template fog blend depth WxH | lights.rgb (before the first Forward+ lamp) | "
                            "lamp atten x4 | before lamp 1 .rgb | before env .rgb | envD.rgb | envS.rgb | before fog .rgb | out.rgb a | fogLit\n");
            unsigned lines = 0;
            for (unsigned i : order) {
                const size_t base = size_t(i) * kWords;
                const unsigned n = std::min(rq.colourPick[base], 4u);
                if (n == 0u) std::fprintf(f, "%u 0/0\n", rq.colourPickFrames[i]);
                for (unsigned k = 0; k < n; ++k) {
                    const size_t o = base + 4u + size_t(k) * kRec;
                    const uint32_t tag = rq.colourPick[o];
                    std::fprintf(f, "%u %u/%u %s %u %u %.7f %ux%u |", rq.colourPickFrames[i], k + 1u, rq.colourPick[base],
                                 (tag & 2u) ? "atom" : "pbs", (tag >> 2u) & 1u, (tag >> 3u) & 1u, fv(o + 1u),
                                 rq.colourPick[o + 2u] & 0xFFFFu, rq.colourPick[o + 2u] >> 16u);
                    std::fprintf(f, " %.6f %.6f %.6f | %.5f %.5f %.5f %.5f (%u lamps) | %.6f %.6f %.6f | %.6f %.6f %.6f |"
                                    " %.6f %.6f %.6f | %.6f %.6f %.6f | %.6f %.6f %.6f | %.6f %.6f %.6f %.4f | %.5f\n",
                                 fv(o + 3u), fv(o + 4u), fv(o + 5u), fv(o + 6u), fv(o + 7u), fv(o + 8u), fv(o + 9u),
                                 (tag >> 8u) & 255u, fv(o + 10u), fv(o + 11u), fv(o + 12u), fv(o + 13u), fv(o + 14u),
                                 fv(o + 15u), fv(o + 16u), fv(o + 17u), fv(o + 18u), fv(o + 19u), fv(o + 20u), fv(o + 21u),
                                 fv(o + 22u), fv(o + 23u), fv(o + 24u), fv(o + 25u), fv(o + 26u), fv(o + 27u), fv(o + 28u),
                                 fv(o + 30u));
                    ++lines;
                }
            }
            std::fclose(f);
            std::printf("LAB COLOURPICK %zu frames, %u fragments -> %s\n", order.size(), lines, a0.c_str());
        }
    } else if (c.verb == "histdump") {
        const RayQueryStatus rq = mScene->published().rayQuery;
        FILE *f = std::fopen(a0.c_str(), "w");
        if (!f || rq.reflectHistoryCount.empty() || !rq.reflectHistoryW) {
            ok = false;
            labFail("LAB FAILED histdump: %s\n", f ? "no history copy (arm reflect.historyReadback 1 first)" : "cannot write");
            if (f) std::fclose(f);
        } else {
            std::fprintf(f, "%u %u %u\n", rq.reflectHistoryW, rq.reflectHistoryH, rq.reflectHistoryFrame);
            for (unsigned y = 0; y < rq.reflectHistoryH; ++y) {
                for (unsigned x = 0; x < rq.reflectHistoryW; ++x)
                    std::fprintf(f, "%u ", unsigned(rq.reflectHistoryCount[size_t(y) * rq.reflectHistoryW + x]));
                std::fprintf(f, "\n");
            }
            std::fclose(f);
            std::printf("LAB HISTDUMP %ux%u frame %u -> %s\n", rq.reflectHistoryW, rq.reflectHistoryH, rq.reflectHistoryFrame, a0.c_str());
        }
    } else if (c.verb == "cards") {
        std::vector<CardRadiance> cards;
        unsigned long long cacheFrame = 0ull;
        ok = mScene->readCardRadiance(cards, cacheFrame);
        if (mNodeNames.empty()) nameEngineNodes();
        struct PerNode { unsigned count = 0u, texels = 0u; unsigned faces[6] = {}; unsigned long long read[6] = {}; };
        std::map<unsigned long long, PerNode> by;
        for (const CardRadiance &r : cards) {
            PerNode &p = by[(unsigned long long)r.node];
            ++p.count;
            p.texels += r.size * r.size;
            if (r.bucket < 6u) { ++p.faces[r.bucket]; p.read[r.bucket] = std::max(p.read[r.bucket], r.lastRead); }
        }
        std::ostringstream o;
        static const char *kFace[6] = { "+x", "-x", "+y", "-y", "+z", "-z" };
        for (const auto &kv : by) {
            const auto it = std::lower_bound(mNodeNames.begin(), mNodeNames.end(), std::make_pair(kv.first, QString()));
            const std::string name = (it != mNodeNames.end() && it->first == kv.first) ? it->second.toStdString()
                                                                                       : std::to_string(kv.first);
            o << "LAB CARDS " << name << "\t" << kv.second.count << " cards\t" << kv.second.texels << " texels\t";
            for (unsigned b = 0; b < 6u; ++b)
                if (kv.second.faces[b])
                    o << kFace[b] << ":" << kv.second.faces[b] << "@" << (cacheFrame - kv.second.read[b]) << " ";
            o << "\n";
        }
        o << "LAB CARDS TOTAL " << cards.size() << " cards on " << by.size() << " objects (cache frame " << cacheFrame
          << "; @N = frames since the face was last read)\n";
        if (a0.empty()) std::printf("%s", o.str().c_str());
        else {
            QString path = QString::fromStdString(a0);
            if (QFileInfo(path).isRelative()) path = QDir(mOpt.dataRoot).filePath(path);
            std::ofstream out(path.toStdString(), std::ios::app);
            out << o.str();
            ok = ok && bool(out);
            std::printf("LAB CARDS -> %s\n", qPrintable(path));
        }
    } else if (c.verb == "save") {
        Jlab j = mJlab;
        j.hasCamera = true;
        j.camPos = mCamera.position();
        j.camTarget = mCamera.target();
        j.tier = QString::fromLatin1(tierName(mTier));
        j.view = QString::fromStdString(atomViewNames()[size_t(mAtomView)]);
        j.gi = mGi; j.occlusion = mOcclusion; j.draw = mDraw; j.overlay = mOverlay;
        std::function<iris::SceneNodePtr(const iris::SceneNodePtr &)> sun = [&](const iris::SceneNodePtr &n) {
            if (n->getSceneNodeType() == iris::SceneNodeType::Light &&
                n.staticCast<iris::LightNode>()->lightType == iris::LightType::Directional) return n;
            for (const iris::SceneNodePtr &ch : n->children()) if (auto s = sun(ch)) return s;
            return iris::SceneNodePtr();
        };
        if (auto s = sun(mDoc->getRootNode())) {
            j.hasSun = true;
            j.sunRot = s->getLocalRot();
            j.sunIntensity = s.staticCast<iris::LightNode>()->intensity;
        }
        QString path = a0.empty() ? QDir(mOpt.dataRoot).filePath(QStringLiteral("jlab/") +
                                                                  (mJlab.env.isEmpty() ? QFileInfo(mJlab.file).completeBaseName()
                                                                                       : mJlab.env + QStringLiteral("-seed") + QString::number(mOpt.seed)) + ".jlab")
                                  : QString::fromStdString(a0);
        QString err;
        ok = writeJlab(path, j, err);
        if (ok) std::printf("LAB SAVED %s\n", qPrintable(path));
        else labFail("LAB SAVE FAILED %s\n", qPrintable(err));
    } else if (c.verb == "envcam") {
        if (!mEnv) { ok = false; labFail("LAB REFUSED envcam: the session is not an environment (--env)\n"); }
        else envCamera(std::strtod(a0.c_str(), nullptr));
    } else if (c.verb == "probe") {
        std::vector<envs::ObjectReading> rd;
        AtomCutProbeSummary sum;
        ok = probe(rd, sum);
        if (ok) {
            std::printf("LAB PROBE %u objects, %u Atom pixels, %llu drawn triangles, %llu of them by objects with no "
                        "visible pixel (tolerance %.2f px, %0.f rows)\n", sum.objects, sum.atomPixels,
                        sum.drawnTriangles, sum.hiddenTriangles, sum.tolerance, sum.viewportHeight);
            if (!a0.empty()) {
                QString path = QString::fromStdString(a0);
                if (QFileInfo(path).isRelative()) path = QDir(mOpt.dataRoot).filePath(path);
                std::ofstream out(path.toStdString(), std::ios::app);
                for (const envs::ObjectReading &r : rd) {
                    // ...and its visible pixels by DAG depth, "depth:pixels" for every depth that drew one
                    out << mFrames << "\t" << r.name.toStdString() << "\t" << r.drawnTriangles << "\t" << r.drawnClusters
                        << "\t" << r.pixels << "\t";
                    for (int d = 0; d < 16; ++d)
                        if (r.depthPixels[d]) out << d << ":" << r.depthPixels[d] << " ";
                    out << "\n";
                }
                ok = bool(out);
            }
        }
    } else if (c.verb == "measure") {
        if (!mEnv) { ok = false; labFail("LAB REFUSED measure: the session is not an environment (--env)\n"); }
        else if (!mEnv->atom) {
            ok = false;
            labFail("LAB REFUSED measure: %s's laws (the bounce residual, the keep rule) are read from Photon's own "
                        "status, not the cut probe — the Photon spec measures them\n", mEnv->name);
        } else {
            const double d = std::strtod(a0.c_str(), nullptr);
            envCamera(d);
            execLine("settle", "measure");
            std::vector<envs::ObjectReading> rd;
            AtomCutProbeSummary sum;
            ok = probe(rd, sum);
            const envs::Optics o = optics();
            const std::vector<envs::Prediction> pred = mEnv->expect(*mBuilt, o);
            QString path;
            if (c.args.size() > 1) {
                path = QString::fromStdString(c.args[1]);
                if (QFileInfo(path).isRelative()) path = QDir(mOpt.dataRoot).filePath(path);
            }
            const bool header = !path.isEmpty() && !QFileInfo::exists(path);
            std::ofstream out;
            if (!path.isEmpty()) out.open(path.toStdString(), std::ios::app);
            out.precision(12);   // a count above a million keeps every digit (the stream's default 6 does not)
            if (header)
                out << "env\tseed\ttier\ttolerance_px\tstop_m\tquantity\tpredicted\tmeasured\tmeasured_over_predicted"
                       "\tdrawn_triangles_all\thidden_object_triangles\tatom_pixels\tframe\n";
            for (const envs::Prediction &pr : pred) {
                const double m = ok ? envs::measured(*mBuilt, pr, rd) : -1.0;
                const double ratio = pr.value > 0 ? m / pr.value : 0.0;
                std::printf("LAB MEASURE %s seed %u stop %.0f m: %s predicted %.0f measured %.0f (x%.3f) — %s\n",
                            mEnv->name, mOpt.seed, d, pr.quantity.c_str(), pr.value, m, ratio, pr.note.c_str());
                if (out)
                    out << mEnv->name << "\t" << mOpt.seed << "\t" << tierName(mTier) << "\t" << o.tolerance << "\t" << d
                        << "\t" << pr.quantity << "\t" << pr.value << "\t" << m << "\t" << ratio << "\t"
                        << sum.drawnTriangles << "\t" << sum.hiddenTriangles << "\t" << sum.atomPixels << "\t" << mFrames
                        << "\n";
            }
        }
    } else if (c.verb == "stillness") {
        ok = stillness(c);
    } else if (c.verb == "lamp") {
        ok = lamp(c);
    } else if (c.verb == "material") {
        ok = material(c);
    } else if (c.verb == "photon") {
        // THE SCENE'S PHOTON VIEW (Scene::setPhotonView, world.setPhotonView's names in PhotonView's order)
        static const char *const kNames[] = { "off", "voxels", "probes", "cards", "screenProbes", "diffuse",
                                              "reflections", "hits", "gatherHits" };
        static_assert(sizeof(kNames) / sizeof(kNames[0]) == size_t(kPhotonViewCount), "one name a Photon view");
        int mode = -1;
        for (int i = 0; i < kPhotonViewCount; ++i)
            if (a0 == kNames[i]) mode = i;
        if (mode < 0) {
            ok = false;
            labFail("LAB REFUSED photon '%s'\n", a0.c_str());
        } else {
            mScene->setPhotonView(PhotonView(mode));
            std::printf("LAB PHOTON %s\n", a0.c_str());
        }
    } else if (c.verb == "arm") {
        // A MEASURING ARM, asked on the engine's side (Engine::request: the affinity law)
        const double v = std::strtod(c.args[1].c_str(), nullptr);
        mEngine->request([&](Engine &e) { ok = e.setArm(c.args[0], v); });
        if (!ok) labFail("LAB REFUSED arm: no arm '%s' (or %s out of its range)\n", c.args[0].c_str(), c.args[1].c_str());
        else std::printf("LAB ARM %s = %s\n", c.args[0].c_str(), c.args[1].c_str());
    } else if (c.verb == "row") {
        // A WORLD ROW, through the panel's own setter (PHOTON-7 stage 7, 7-3's reference: the reflection's cutoff)
        const worldmodes::Row *r = worldmodes::row(QString::fromStdString(c.args[0]));
        if (!r || !r->set) {
            ok = false;
            labFail("LAB REFUSED row: no settable world row '%s'\n", c.args[0].c_str());
        } else {
            r->set(mDoc, std::atoi(c.args[1].c_str()));
            std::printf("LAB ROW %s = %s\n", c.args[0].c_str(), c.args[1].c_str());
        }
    } else if (c.verb == "rays") {
        iris::RayTracingMode m;
        if (!iris::rayTracingModeFromName(QString::fromStdString(a0), m)) {
            ok = false;
            labFail("LAB REFUSED rays '%s' (auto|on|off)\n", a0.c_str());
        } else {
            mDoc->rayTracing = m;
            std::printf("LAB RAYS %s\n", iris::rayTracingModeName(m));
        }
    } else if (c.verb == "monitor") {
        ok = monitor(c);
    } else if (c.verb == "move") {
        ok = move(c);
    } else if (c.verb == "lights") {
        std::function<void(const iris::SceneNodePtr &)> walk = [&](const iris::SceneNodePtr &n) {
            if (n->getSceneNodeType() == iris::SceneNodeType::Light) {
                const iris::LightNodePtr l = n.staticCast<iris::LightNode>();
                const iris::Vec3 p = n->getGlobalPosition();
                std::printf("LAB LIGHT %s type %d at %.4f,%.4f,%.4f intensity %.4g range %.4g sourceRadius %.4g\n",
                            qPrintable(n->getName()), int(l->lightType), p.x(), p.y(), p.z(), l->intensity, l->distance,
                            l->sourceRadius);
            }
            for (const iris::SceneNodePtr &ch : n->children()) walk(ch);
        };
        walk(mDoc->getRootNode());
    } else if (c.verb == "pin") {
        ok = pin(c);
    } else if (c.verb == "voxels") {
        ok = voxels(c);
    } else if (c.verb == "regions") {
        ok = regions(c);
    } else if (c.verb == "help") {
        std::printf("%s", helpText().c_str());
    } else if (c.verb == "echo") {
        std::string s;
        for (const std::string &w : c.args) s += (s.empty() ? "" : " ") + w;
        std::printf("LAB ECHO %s\n", s.c_str());
    } else if (c.verb == "quit") {
        mQuit = true;
    }
    std::printf("LAB %s (%s) %s\n", ok ? "OK" : "FAILED", c.source.c_str(), c.line.c_str());
    std::fflush(stdout);
    if (!ok && gLabLastFailure.empty()) gLabLastFailure = "the verb '" + c.verb + "' failed (its lines above)";
    if (!ok && c.source == "script") mExit = 1;
    return ok;
}

// ---- THE STILLNESS DOOR, THE LAMP STEP, THE SCRIPTED MOVE (PHOTON-2 stage 0) ------------------------------------------

bool Session::stillness(const Command &c)
{
    using namespace jahshaka::engine;
    std::string script = c.args[0];
    if (!script.empty() && script[0] == '@') {
        std::ifstream in(script.substr(1));
        if (!in) { labFail("LAB REFUSED stillness: cannot read the script %s\n", script.c_str() + 1); return false; }
        std::stringstream ss;
        ss << in.rdbuf();
        script = ss.str();
    }
    StillnessSpec spec;
    spec.script = script;
    {   // THE CARDS' DEMAND SETTLE of the document's tier (the mirror's quality mapping, scenemirror.cpp)
        GiQuality q = GiQuality::Medium;
        switch (mDoc->giQuality) {
        case iris::GiQuality::LOW: q = GiQuality::Low; break;
        case iris::GiQuality::HIGH: q = GiQuality::High; break;
        case iris::GiQuality::EPIC: q = GiQuality::Epic; break;
        default: break;
        }
        spec.cardsSettleFrames = giQualityFacts(q).cardDemandSettleFrames;
    }
    spec.frames = unsigned(std::atoi(c.args[1].c_str()));
    std::string out = "stillness-" + std::to_string(mFrames);
    for (size_t i = 2; i < c.args.size(); ++i) {
        const std::string &a = c.args[i];
        if (a == "path") spec.path = true;
        else if (a.rfind("age=", 0) == 0) spec.visibleAge = unsigned(std::atoi(a.c_str() + 4));
        else if (a.rfind("trail=", 0) == 0) spec.moveTrail = unsigned(std::max(0, std::atoi(a.c_str() + 6)));
        else if (a == "restarts") spec.classRestarts = true;
        else if (a == "freeze") spec.freezeWalk = true;   // M1's frozen walk, from the converged picture (P2-1)
        else if (a == "freeze=world") {   // THE WORLD HELD: the caches and the gather frozen, the reflections live
            spec.freezeWalk = true;
            spec.freezeValue = 2.0;
        }
        else if (a.rfind("dump=", 0) == 0 || a.rfind("dumprgb=", 0) == 0) {
            // THE MOVING WINDOW'S RAW MATERIAL (PHOTON-2 round 2, M1): <data-root>/stillness/dump-<name>/, emptied;
            // dumprgb= the pictures alone (a walk compared byte for byte: the walk rows' second frozen walk, PHOTON-7 stage 5)
            spec.dumpRgbOnly = a.rfind("dumprgb=", 0) == 0;
            std::string name = a.substr(spec.dumpRgbOnly ? 8 : 5);
            for (char &ch : name)
                if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') ch = '_';
            const QString dir = QDir(mOpt.dataRoot).filePath(QStringLiteral("stillness/dump-") + QString::fromStdString(name));
            QDir(dir).removeRecursively();
            QDir().mkpath(dir);
            spec.dumpDir = dir.toStdString();
            spec.dumpName = name;
        }
        else if (a.rfind("dumpwin=", 0) == 0) {
            // THE WINDOW FRAMES DUMPED (StillnessSpec::dumpWindowFrames): a comma list of indices from the stop
            const std::string list = a.substr(8);
            size_t at = 0;
            while (at < list.size()) {
                const size_t comma = list.find(',', at);
                spec.dumpWindowFrames.push_back(unsigned(std::max(0, std::atoi(list.substr(at, comma - at).c_str()))));
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
        }
        else if (a.rfind("out=", 0) == 0) {
            // A NAME, never a path: the files stay in <data-root>/stillness (Studio's world.stillness does the same)
            out = a.substr(4);
            for (char &ch : out)
                if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') ch = '_';
            if (out.empty()) out = "stillness";
        }
        else { labFail("LAB REFUSED stillness: '%s' (path, age=N, trail=N, restarts, dump=<name>, dumprgb=<name>, dumpwin=<i,j,...>, freeze, freeze=world, out=<name>)\n", a.c_str()); return false; }
    }
    std::vector<StillnessMove> moves;
    std::string err;
    Vec3 fe, ft;
    if (!parseStillnessScript(spec.script, moves, err) || !stillnessFinalPose(moves, fe, ft)) {
        labFail("LAB REFUSED %s\n", err.c_str());
        return false;
    }
    // THE CLASSES AT THE STOP'S POSE, from the environment's own objects (a model session counts every pixel as one)
    const unsigned w = mInput.width ? mInput.width : mOpt.width, h = mInput.height ? mInput.height : mOpt.height;
    if (mBuilt) {
        const double tc = msSinceStart();
        spec.classNames = stillnessClassNames();
        spec.classes = stillnessClassImage(mBuilt->objects, iris::Vec3(fe.x, fe.y, fe.z), iris::Vec3(ft.x, ft.y, ft.z),
                                           mCamera.node()->angle, w, h);
        spec.classW = w;
        spec.classH = h;
        std::printf("LAB STILLNESS classes at the stop's pose: %ux%u in %.0f ms\n", w, h, msSinceStart() - tc);
        const std::vector<envs::Object> objects = mBuilt->objects;
        const float fov = mCamera.node()->angle;
        spec.classesAt = [objects, fov](const Vec3 &e, const Vec3 &t, unsigned cw, unsigned ch) {
            return stillnessClassImage(objects, iris::Vec3(e.x, e.y, e.z), iris::Vec3(t.x, t.y, t.z), fov, cw, ch);
        };
    }
    spec.fovYDegrees = mCamera.node()->angle;
    StillnessRun run(*mEngine, *mScene, *mView);
    if (!run.begin(spec, err)) { labFail("LAB REFUSED %s\n", err.c_str()); return false; }
    const double t0 = msSinceStart();
    Vec3 eye, target;
    while (!mQuit && run.nextPose(eye, target)) {
        mCamera.set(iris::Vec3(eye.x, eye.y, eye.z), iris::Vec3(target.x, target.y, target.z));
        frame();
        if (!run.frameDone(err)) { labFail("LAB STILLNESS FAILED %s\n", err.c_str()); return false; }
    }
    StillnessReport rep;
    if (!run.finish(rep, err)) { labFail("LAB STILLNESS FAILED %s\n", err.c_str()); return false; }
    const QString dir = QDir(mOpt.dataRoot).filePath(QStringLiteral("stillness"));
    QDir().mkpath(dir);
    const QString tsv = QDir(dir).filePath(QString::fromStdString(out) + QStringLiteral(".tsv"));
    {
        std::ofstream f(tsv.toStdString());
        f << rep.tsv();
        if (!f) { labFail("LAB STILLNESS FAILED cannot write %s\n", qPrintable(tsv)); return false; }
    }
    // THE PICTURES BESIDE THE TSV: each window's last frame, its heat (the largest change a pixel made over the
    // window, 32 grey levels a code: one code reads 32, eight or more white) and its late heat (the last quarter),
    // and the class map the rows were split by.
    const auto grey = [&](const std::vector<unsigned char> &v, unsigned gw, unsigned gh, const std::string &name, int k) {
        if (v.empty() || !gw || !gh) return;
        QImage g(int(gw), int(gh), QImage::Format_Grayscale8);
        for (unsigned y = 0; y < gh; ++y) {
            uchar *row = g.scanLine(int(y));
            for (unsigned x = 0; x < gw; ++x) row[x] = uchar(std::min(255, int(v[size_t(y) * gw + x]) * k));
        }
        g.save(QDir(dir).filePath(QString::fromStdString(out + "-" + name) + QStringLiteral(".png")), "PNG");
    };
    for (const StillnessWindow &win : rep.windows) {
        const Image &img = win.lastPicture;
        if (!img.width) continue;
        QImage q(img.rgba.data(), int(img.width), int(img.height), int(img.width * 4), QImage::Format_RGBA8888);
        q.convertToFormat(QImage::Format_RGB888)
            .save(QDir(dir).filePath(QString::fromStdString(out + "-" + win.name) + QStringLiteral(".png")), "PNG");
        grey(win.heat, win.heatW, win.heatH, win.name + "-heat", 32);
        grey(win.heatLate, win.heatW, win.heatH, win.name + "-heatlate", 32);
    }
    // THE CLASS MAP in colour (plaster grey, rough brown, glossy blue, mirror white, foliage green, mover red; none
    // black): what the rows are split by — a class, not an object, so a pillar before a wall is one grey with it.
    if (!spec.classes.empty()) {
        static const QRgb kPalette[7] = { qRgb(0, 0, 0), qRgb(110, 110, 110), qRgb(150, 100, 60), qRgb(60, 110, 230),
                                          qRgb(245, 245, 245), qRgb(60, 190, 70), qRgb(220, 50, 40) };
        QImage c(int(spec.classW), int(spec.classH), QImage::Format_RGB32);
        for (unsigned y = 0; y < spec.classH; ++y)
            for (unsigned x = 0; x < spec.classW; ++x)
                c.setPixel(int(x), int(y), kPalette[std::min<unsigned>(6u, spec.classes[size_t(y) * spec.classW + x])]);
        c.save(QDir(dir).filePath(QString::fromStdString(out + "-classes") + QStringLiteral(".png")), "PNG");
    }
    std::istringstream lines(rep.summary());
    for (std::string line; std::getline(lines, line);) std::printf("LAB %s\n", line.c_str());
    std::printf("LAB STILLNESS %u frames in %.0f ms -> %s\n", run.plannedFrames(), msSinceStart() - t0, qPrintable(tsv));
    return true;
}

bool Session::lamp(const Command &c)
{
    iris::LightNodePtr found;
    std::function<void(const iris::SceneNodePtr &)> walk = [&](const iris::SceneNodePtr &n) {
        if (!found && n->getSceneNodeType() == iris::SceneNodeType::Light && n->getName().toStdString() == c.args[0])
            found = n.staticCast<iris::LightNode>();
        for (const iris::SceneNodePtr &ch : n->children()) walk(ch);
    };
    walk(mDoc->getRootNode());
    if (!found) { labFail("LAB REFUSED lamp: no light named '%s'\n", c.args[0].c_str()); return false; }
    if (c.args[1] == "intensity") {
        char *end = nullptr;
        const double v = std::strtod(c.args[2].c_str(), &end);
        if (!end || *end || v < 0.0) { labFail("LAB REFUSED lamp: '%s' is not an intensity\n", c.args[2].c_str()); return false; }
        found->setPropertyValue(QStringLiteral("intensity"), float(v));
        return true;
    }
    if (c.args[1] == "position") {
        iris::Vec3 p, unused;
        if (!parsePose(c.args[2] + ",0,0,0", p, unused)) {
            labFail("LAB REFUSED lamp: '%s' is not x,y,z\n", c.args[2].c_str());
            return false;
        }
        found->setLocalPos(p);
        return true;
    }
    labFail("LAB REFUSED lamp: '%s' (intensity | position)\n", c.args[1].c_str());
    return false;
}

bool Session::material(const Command &c)
{
    iris::MeshNodePtr found;
    std::function<void(const iris::SceneNodePtr &)> walk = [&](const iris::SceneNodePtr &n) {
        if (!found && n->getSceneNodeType() == iris::SceneNodeType::Mesh && n->getName().toStdString() == c.args[0])
            found = n.staticCast<iris::MeshNode>();
        for (const iris::SceneNodePtr &ch : n->children()) walk(ch);
    };
    walk(mDoc->getRootNode());
    if (!found) { labFail("LAB REFUSED material: no mesh named '%s'\n", c.args[0].c_str()); return false; }
    const iris::PbrMaterialPtr mat = found->getMaterial().dynamicCast<iris::PbrMaterial>();
    if (!mat) { labFail("LAB REFUSED material: '%s' wears no PBR material\n", c.args[0].c_str()); return false; }
    if (c.args[1] == "colour") {
        iris::Vec3 rgb, unused;
        if (!parsePose(c.args[2] + ",0,0,0", rgb, unused) || rgb.x() < 0 || rgb.y() < 0 || rgb.z() < 0 ||
            rgb.x() > 1 || rgb.y() > 1 || rgb.z() > 1) {
            labFail("LAB REFUSED material: '%s' is not r,g,b in 0..1\n", c.args[2].c_str());
            return false;
        }
        mat->setValue("baseColor", QColor::fromRgbF(rgb.x(), rgb.y(), rgb.z()));
        return true;
    }
    if (c.args[1] == "roughness") {
        char *end = nullptr;
        const double v = std::strtod(c.args[2].c_str(), &end);
        if (!end || *end || v < 0.0 || v > 1.0) {
            labFail("LAB REFUSED material: '%s' is not a roughness in 0..1\n", c.args[2].c_str());
            return false;
        }
        mat->setValue("roughness", float(v));
        return true;
    }
    labFail("LAB REFUSED material: '%s' (colour | roughness)\n", c.args[1].c_str());
    return false;
}

// ---- THE GPU MONITOR (PHOTON-7, the step before stage 10) ------------------------------------------------------------
//
// The engine's OWN frame monitor (irisgl engine/src/OgreFrameMonitor.cpp: Ogre-Next's compositor pass listeners and the
// fork's GPU timestamp pairs, one per pass and one per GPU row - the compute dispatches the compositor never sees), read
// through a TEE (Engine::openMonitorTee) so a host's own capture is never drained here. Nothing is drawn; the frames are
// counted (the fixed 1/60 s grid). A row's time is its timestamp pair's: the begin written at TOP_OF_PIPE, the end at
// BOTTOM_OF_PIPE, and only the DURATION comes back - so a row recorded behind a barrier opens while the work in front of it
// still runs and its time ENCLOSES that wait (stage 7's no-ray resolve behind the integrate). The verb cannot split one
// row's wait from its work with these timestamps; it measures the frame's double count instead - the OVERLAP: the
// TOP-LEVEL passes and rows (CacheWork::topLevel; a row inside a pass is in its time, "nested-row:") and the unnamed
// gaps summed, less the busy span - which bounds every such wait of the frame. (The split needs a row's absolute begin
// and end, or a timestamp written at the barrier's destination stage after it: a fork change, named in the step's report.)
namespace {
/// The nearest-rank percentile (p in (0, 1]).
double percentile(std::vector<double> v, double p)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double rank = std::ceil(p * double(v.size()));
    return v[size_t(std::max(1.0, std::min(rank, double(v.size())))) - 1u];
}
}   // namespace

bool Session::monitor(const Command &c)
{
    const int n = std::atoi(c.args[0].c_str());
    if (n < 1) { labFail("LAB REFUSED monitor: '%s' frames (at least 1)\n", c.args[0].c_str()); return false; }
    iris::Vec3 eye, target;
    const bool walk = c.args.size() > 2;
    if (walk && !parsePose(c.args[2], eye, target)) {
        labFail("LAB REFUSED monitor: '%s' is not x,y,z,tx,ty,tz\n", c.args[2].c_str());
        return false;
    }
    std::FILE *out = std::fopen(c.args[1].c_str(), "w");
    if (!out) { labFail("LAB FAILED monitor: cannot write %s\n", c.args[1].c_str()); return false; }
    const unsigned tee = mEngine->openMonitorTee();
    if (!tee) { std::fclose(out); labFail("LAB FAILED monitor: the engine opened no monitor tee\n"); return false; }
    const bool ours = mEngine->published().monitor.level == MonitorLevel::Off;
    if (ours) {
        mEngine->request([](Engine &e) { e.setFrameMonitor(MonitorLevel::Review); });
        frame();   // the monitor attaches on this frame's drain: not measured
    }
    std::vector<MonitorRecord> recs, got;
    std::set<unsigned long long> measured;
    // THE COUNTS (the lead's addition for stage 10's ray budget): per frame, read from the published GI status after it -
    // the READ-BACK ones land a few frames late (the feedback's brick-passes, the voxels the injections marched: their
    // deltas over the frame, "landed"); the cards' texels the frame relit (direct: every relit card; indirect: the cards
    // whose march through the cascades ran in the same dispatch) and captured are the host's own.
    std::map<unsigned long long, std::map<std::string, double>> countsOf;   // engine frame -> name -> count
    std::vector<unsigned long long> prevBricks, prevPasses, prevMarched;
    std::map<std::string, unsigned long long> prevCommit;   // the scroll commits' cumulative cells, by count name
    const auto counts = [&](unsigned long long frameNo) {
        const GiStatus &g = mScene->published().gi;
        auto &m = countsOf[frameNo];
        if (prevBricks.size() != g.cascades.size()) {
            prevBricks.assign(g.cascades.size(), 0ull);
            prevPasses.assign(g.cascades.size(), 0ull);
            prevMarched.assign(g.cascades.size(), 0ull);
            for (size_t i = 0; i < g.cascades.size(); ++i) {
                prevBricks[i] = g.cascades[i].feedbackBricksRan;
                prevPasses[i] = g.cascades[i].feedbackPasses;
                prevMarched[i] = g.cascades[i].deltaMarched;
            }
        }
        double bricks = 0.0, marched = 0.0;
        for (size_t i = 0; i < g.cascades.size(); ++i) {
            const GiStatus::CascadeStatus &cs = g.cascades[i];
            const double b = double(cs.feedbackBricksRan - std::min(prevBricks[i], cs.feedbackBricksRan));
            const double mv = double(cs.deltaMarched - std::min(prevMarched[i], cs.deltaMarched));
            m["count:vct.bricks_passed" + std::to_string(i)] = b;
            m["count:vct.inject_marched" + std::to_string(i)] = mv;
            // A SCROLL COMMIT's SLAB AND ITS INJECTION (PHOTON-7's hitch table): the entering slabs' cells and the cells its
            // direct term was computed over, read back a few frames late like the march (GiStatus commitSlabCells / Inject)
            const auto commitDelta = [&](const std::string &name, unsigned long long now) {
                const auto it = prevCommit.find(name);
                m[name] = it == prevCommit.end() ? 0.0 : double(now - std::min(it->second, now));
                prevCommit[name] = now;
            };
            commitDelta("count:vct.commit_slab" + std::to_string(i), cs.commitSlabCells);
            commitDelta("count:vct.commit_inject" + std::to_string(i), cs.commitInjectCells);
            // ...and the voxels its delta injections LIT (their direct changed) in the slabs and outside them (the band),
            // landing with the read-back (commitCounted) - the hitch step's 2: what the reach past the slab changed
            commitDelta("count:vct.commit_lit_slab" + std::to_string(i), cs.commitLitSlab);
            commitDelta("count:vct.commit_lit_band" + std::to_string(i), cs.commitLitBand);
            commitDelta("count:vct.commit_counted" + std::to_string(i), cs.commitCounted);
            bricks += b;
            marched += mv;
            prevBricks[i] = cs.feedbackBricksRan;
            prevPasses[i] = cs.feedbackPasses;
            prevMarched[i] = cs.deltaMarched;
        }
        m["count:vct.bricks_passed"] = bricks;
        m["count:vct.inject_marched"] = marched;
        m["count:cards.relit_texels_direct"] = double(g.cards.relitTexelsLastFrame);
        m["count:cards.relit_texels_indirect"] = double(g.cards.indirectTexelsLastFrame);
        m["count:cards.captured_texels"] = double(g.cards.texelsLastFrame);
    };
    counts(0ull);   // the baseline of the cumulative counters
    const iris::Vec3 e0 = mCamera.position(), t0 = mCamera.target();
    for (int i = 1; i <= n && !mQuit; ++i) {
        if (walk) {
            const float t = float(i) / float(n);
            mCamera.set(e0 + (eye - e0) * t, t0 + (target - t0) * t);
        }
        frame();
        measured.insert(mEngine->published().frame);
        counts(mEngine->published().frame);
        got.clear();
        mEngine->popMonitorTee(tee, got);
        for (MonitorRecord &r : got) recs.push_back(std::move(r));
    }
    // THE TAIL: a record is published once its GPU timestamps came back - a few frames later (the monitor's holding queue)
    const auto have = [&]() {
        size_t k = 0;
        for (const MonitorRecord &r : recs) k += measured.count(r.frame);
        return k;
    };
    int tail = 0;
    constexpr int kTailCap = 32;
    while (have() < measured.size() && tail < kTailCap && !mQuit) {
        frame();
        ++tail;
        got.clear();
        mEngine->popMonitorTee(tee, got);
        for (MonitorRecord &r : got) recs.push_back(std::move(r));
    }
    if (ours) mEngine->request([](Engine &e) { e.setFrameMonitor(MonitorLevel::Off); });
    mEngine->closeMonitorTee(tee);

    std::sort(recs.begin(), recs.end(), [](const MonitorRecord &a, const MonitorRecord &b) { return a.frame < b.frame; });
    std::map<std::string, std::vector<double>> per;   // name -> the ms of every frame it ran
    std::vector<double> busyAll, overlapAll;
    int index = 0;
    unsigned unmeasured = 0, dropped = 0;
    for (const MonitorRecord &r : recs) {
        if (!measured.count(r.frame)) continue;
        ++index;
        std::map<std::string, double> names;
        // (a name is one word in the file: its spaces become '_' - Ogre's pass ids carry them)
        const auto word = [](std::string n) { std::replace(n.begin(), n.end(), ' ', '_'); return n; };
        double passes = 0.0, rows = 0.0;
        unsigned tops = 0u;   // the top-level passes and rows (the frame's sample boundaries: law (3)'s resolution)
        // A ROW'S OWN TIME where it encloses others (self:<name>, the work none of its children names) and whether it
        // SUBMITTED inside it (submitted:<name>=1: its own time holds the GPU's wait for the CPU, a bubble)
        // ...and an OVERRUN (overrun:<name>=ms): its children sum past it beyond the rounding - an overlap or a double count
        // ...and staging:<name>=1 when the UPLOAD STAGING submitted inside its own stretches (the submission's cause)
        // ...and THE MEASURED BUBBLE (urgent #64, the fork's own timestamp pair at each flush): bubble:<name> its own
        // stretches' (what its own time holds of the GPU's idle), bubblelost:<name>=1 where the fork could not measure
        // one, and bubblenosub:<name> - THE SANITY LAW's breach - a bubble on a row (or its own stretches) with no
        // submission inside: a credit the queue's counters never saw
        const auto selfOf = [&](const std::string &n, float ms, float self, bool measured, bool submitted, bool staging,
                                float bubbleWhole, float bubbleOwn, bool inside) {
            if (submitted) names[word("submitted:" + n)] = 1.0;   // a leaf too: its time holds a bubble
            if (staging) names[word("staging:" + n)] = 1.0;
            if (bubbleOwn < 0.0f) names[word("bubblelost:" + n)] = 1.0;
            else if (bubbleOwn > 0.0f) names[word("bubble:" + n)] += double(bubbleOwn);
            if (!inside && bubbleWhole != 0.0f) names[word("bubblenosub:" + n)] += std::fabs(double(bubbleWhole));
            if (!submitted && bubbleOwn > 0.0f) names[word("bubblenosub:" + n)] += double(bubbleOwn);
            if (!measured || ms < 0.0f) return;
            if (self < 0.0f) { names[word("overrun:" + n)] += -double(self); return; }
            if (double(ms) - double(self) < 1e-4) return;   // a leaf: its own time IS its time
            names[word("self:" + n)] += double(self);
        };
        for (const FramePass &p : r.passes) {
            if (p.gpuMs < 0.0f) continue;   // not measured (never faked as 0)
            const std::string n = (p.topLevel ? "pass:" : "nested:") + p.node + "/" + p.pass;
            names[word(n)] += p.gpuMs;
            selfOf(n, p.gpuMs, p.selfGpuMs, p.selfMeasured, p.selfSubmitted, p.selfStaging, p.bubbleGpuMs,
                   p.selfBubbleGpuMs, p.insideSubmitted);
            if (p.topLevel) { passes += p.gpuMs; ++tops; }
        }
        std::map<std::string, double> untimed;   // the frame's work with no GPU time of its own (inside a row's time)
        // WHAT TRIGGERED A GI ROW (why:<name>:<WorkReason>=units) and its CPU ms (cpu:<name>) - the hitch table's columns
        static const char *const kReason[] = { "None", "Build", "Rebuild", "Refresh", "Sweep", "Moved", "Caster", "Light",
                                               "Material", "Sky", "Ambient", "Fog", "Mobility", "Added", "Removed",
                                               "Bounds", "Resolution", "Camera", "Permutation", "Request" };
        for (const CacheWork &w : r.cacheWork) {
            if (w.reason != WorkReason::None) {
                const size_t ri = size_t(w.reason);
                untimed[word(std::string("why:") + w.detail + ":" +
                             (ri < sizeof(kReason) / sizeof(kReason[0]) ? kReason[ri] : "?"))] += double(w.units);
            }
            if (w.ms >= 0.0f && w.ms > 0.05f) untimed[word("cpu:" + w.detail)] += double(w.ms);
        }
        for (const CacheWork &w : r.cacheWork) {
            if (w.gpuMs < 0.0f) {
                untimed[word("untimed:" + w.detail)] += double(w.units);
                continue;
            }
            names[word((w.topLevel ? "row:" : "nested-row:") + w.detail)] += w.gpuMs;
            selfOf((w.topLevel ? "row:" : "nested-row:") + w.detail, w.gpuMs, w.selfGpuMs, w.selfMeasured,
                   w.selfSubmitted, w.selfStaging, w.bubbleGpuMs, w.selfBubbleGpuMs, w.insideSubmitted);
            if (w.topLevel) { rows += w.gpuMs; ++tops; }
        }
        dropped += r.gpuMarksDropped;
        const double busy = r.frameGpuMs >= 0.0f && r.gpuIdleMs >= 0.0f ? double(r.frameGpuMs - r.gpuIdleMs) : -1.0;
        // (NaN where it is not measured: a negative overlap is a reading - the clocks' quantum - not a flag)
        // THE ROWS' MEASURED BUBBLES (rbubble, urgent #64) are idle, not work: the top-level rows' times hold them and
        // the busy span does not (MonitorRecord::rowBubbleGpuMs)
        const double rbubble = r.rowBubbleGpuMs >= 0.0f ? double(r.rowBubbleGpuMs) : std::nan("");
        const double overlap = busy >= 0.0 && r.unattributedGpuMs >= 0.0f
                                   ? passes + rows - rbubble + double(r.unattributedGpuMs) - busy : std::nan("");
        if (busy < 0.0) ++unmeasured;
        std::fprintf(out, "F %d frame %llu span %.4f idle %.4f busy %.4f passes %.4f rows %.4f rbubble %.4f tops %u "
                          "unattr %.4f overlap %.4f "
                          "dropped %u rebuilds %u compiles %u wall %.2f sflush %u swrap %u swait %u swaitsub %u swaitms %.3f "
                          "skb %.0f qflush %u qfenced %u qdrain %u qdrainms %.3f gap %.4f %s |", index, r.frame,
                     double(r.frameGpuMs), double(r.gpuIdleMs), busy, passes, rows, rbubble, tops,
                     double(r.unattributedGpuMs), overlap,
                     r.gpuMarksDropped, r.cascadeRebuilds, r.shaderCompiles, double(r.totalMs), r.stagingFlushes,
                     r.stagingWraps, r.stagingWaits, r.stagingWaitSubmits, double(r.stagingWaitMs),
                     double(r.stagingBytes) / 1024.0, r.queueFlushes, r.queueFencedFlushes, r.queueSelfDrains,
                     double(r.queueSelfDrainMs), double(r.widestGapMs),
                     r.widestGap.empty() ? "-" : word(r.widestGap).c_str());
        for (const auto &kv : names) {
            std::fprintf(out, " %s=%.4f", kv.first.c_str(), kv.second);
            per[kv.first].push_back(kv.second);
        }
        for (const auto &kv : untimed) std::fprintf(out, " %s=%.4g", kv.first.c_str(), kv.second);   // (units; cpu: ms)
        if (const auto ci = countsOf.find(r.frame); ci != countsOf.end())
            for (const auto &kv : ci->second) {
                std::fprintf(out, " %s=%.0f", kv.first.c_str(), kv.second);
                per[kv.first].push_back(kv.second);
            }
        std::fprintf(out, "\n");
        if (busy >= 0.0) busyAll.push_back(busy);
        if (busy >= 0.0 && r.unattributedGpuMs >= 0.0f) overlapAll.push_back(overlap);
    }
    std::fprintf(out, "# SUMMARY %d frames measured of %d (tail %d, records without GPU time %u, GPU marks dropped %u)%s\n",
                 index, int(measured.size()), tail, unmeasured, dropped, walk ? " - a walk" : " - still");
    const auto line = [&](const std::string &name, const std::vector<double> &v) {
        double s = 0.0;
        for (double x : v) s += x;
        std::fprintf(out, "S %s frames %zu mean %.4f p95 %.4f max %.4f\n", name.c_str(), v.size(),
                     v.empty() ? 0.0 : s / double(v.size()), percentile(v, 0.95), v.empty() ? 0.0 : *std::max_element(v.begin(), v.end()));
    };
    line("frame:busy", busyAll);
    line("frame:overlap", overlapAll);
    for (const auto &kv : per) line(kv.first, kv.second);
    std::fclose(out);
    std::printf("LAB MONITOR %d of %d frames (tail %d) -> %s\n", index, int(measured.size()), tail, c.args[1].c_str());
    std::fflush(stdout);
    if (index != int(measured.size())) {
        labFail("LAB FAILED monitor: %d of the %d frames' records came back within %d tail frames\n", index,
                int(measured.size()), kTailCap);
        return false;
    }
    return true;
}

bool Session::move(const Command &c)
{
    iris::Vec3 eye, target;
    if (!parsePose(c.args[0], eye, target)) {
        labFail("LAB REFUSED move: '%s' is not x,y,z,tx,ty,tz\n", c.args[0].c_str());
        return false;
    }
    const int n = std::atoi(c.args[1].c_str());
    if (n <= 0) {
        mCamera.set(eye, target);
        frame();
        return true;
    }
    const iris::Vec3 e0 = mCamera.position(), t0 = mCamera.target();
    for (int i = 1; i <= n && !mQuit; ++i) {
        const float t = float(i) / float(n);
        mCamera.set(e0 + (eye - e0) * t, t0 + (target - t0) * t);
        frame();
    }
    return true;
}

// ---- THE GI DOORS (PHOTON-7 stage 1): the placement pin, one cascade's voxels by world cell ----------------------------

bool Session::pin(const Command &c)
{
    using namespace jahshaka::engine;
    if (c.args[0] == "off") {
        if (c.args.size() != 1) { labFail("LAB REFUSED pin: 'pin off' takes nothing more\n"); return false; }
        mScene->setGiPlacementPin(GiPlacementPin());   // a message: applied at the next frame's drain
        frame();
        std::printf("LAB PIN off\n");
        return true;
    }
    if (c.args.size() < 2) { labFail("LAB REFUSED pin: pin <c|all> x,y,z [scroll] | pin off\n"); return false; }
    const bool scroll = c.args.size() == 3 && c.args[2] == "scroll";
    if (c.args.size() == 3 && !scroll) { labFail("LAB REFUSED pin: '%s' (scroll)\n", c.args[2].c_str()); return false; }
    iris::Vec3 p, unused;
    if (!parsePose(c.args[1] + ",0,0,0", p, unused)) {
        labFail("LAB REFUSED pin: '%s' is not x,y,z\n", c.args[1].c_str());
        return false;
    }
    const GiStatus g = mScene->published().gi;
    if (g.cascades.empty()) { labFail("LAB REFUSED pin: the scene has no cascade chain\n"); return false; }
    const int n = int(g.cascades.size());
    int only = -1;
    if (c.args[0] != "all") {
        char *end = nullptr;
        const long v = std::strtol(c.args[0].c_str(), &end, 10);
        if (!end || *end || v < 0 || v >= n) {
            labFail("LAB REFUSED pin: '%s' is not a cascade 0..%d or all\n", c.args[0].c_str(), n - 1);
            return false;
        }
        only = int(v);
    }
    // EVERY CASCADE IS PINNED (the pin is the chain's placement): the named one at the position, the rest where they stand
    GiPlacementPin pinned;
    pinned.on = true;
    for (int i = 0; i < n; ++i)
        pinned.cascadeBuiltCam.push_back(only < 0 || i == only ? Vec3(p.x(), p.y(), p.z()) : g.cascades[size_t(i)].builtCam);
    mScene->setGiPlacementPin(pinned);
    if (!scroll) {
        // A WHOLE REBUILD of each pinned cascade at its pin (the measuring door gi.rebuildCascade, one frame each)
        for (int i = 0; i < n && !mQuit; ++i) {
            if (only >= 0 && i != only) continue;
            bool armed = false;
            mEngine->request([&](Engine &e) { armed = e.setArm("gi.rebuildCascade", double(i)); });
            if (!armed) { labFail("LAB REFUSED pin: the engine refused the arm gi.rebuildCascade %d\n", i); return false; }
            frame();
        }
        mEngine->request([&](Engine &e) { e.setArm("gi.rebuildCascade", -1.0); });
    }
    frame();
    std::printf("LAB PIN %s at %.4f,%.4f,%.4f%s\n", c.args[0].c_str(), p.x(), p.y(), p.z(),
                scroll ? " (the scroll path)" : " (whole rebuilds)");
    return true;
}

bool Session::regions(const Command &c)
{
    using namespace jahshaka::engine;
    const bool boxes = !c.args.empty() && c.args[0] == "boxes";
    if (!c.args.empty() && !boxes) { labFail("LAB REFUSED regions: '%s' (boxes)\n", c.args[0].c_str()); return false; }
    GiChangedRegions r;
    if (!mScene->giChangedRegions(r)) { labFail("LAB REGIONS FAILED: no list has closed (no frame drew the scene)\n"); return false; }
    static const char *const kKinds[kGiRegionKindCount] = { "?", "Slab", "Card", "Edit", "Mover", "MoverShadow", "LampShadow", "Lamp" };
    unsigned per[kGiRegionKindCount] = {};
    for (const GiChangedRegion &g : r.regions)
        if (unsigned(g.kind) < kGiRegionKindCount) ++per[unsigned(g.kind)];
    std::printf("LAB REGIONS frame %llu count %zu", r.frame, r.regions.size());
    for (unsigned k = 1; k < kGiRegionKindCount; ++k) std::printf(" %s %u", kKinds[k], per[k]);
    std::printf(" overflow %u restart %d merges %u sun %d toSun %.4f,%.4f,%.4f\n", r.overflow, int(r.restart), r.merges,
                int(r.sun), double(r.toSun.x), double(r.toSun.y), double(r.toSun.z));
    if (boxes)
        for (const GiChangedRegion &g : r.regions)
            std::printf("LAB REGION %s %.4f,%.4f,%.4f %.4f,%.4f,%.4f cascades 0x%x\n",
                        unsigned(g.kind) < kGiRegionKindCount ? kKinds[unsigned(g.kind)] : "?", double(g.lo.x), double(g.lo.y),
                        double(g.lo.z), double(g.hi.x), double(g.hi.y), double(g.hi.z), g.cascadeMask);
    return true;
}

bool Session::voxels(const Command &c)
{
    using namespace jahshaka::engine;
    char *end = nullptr;
    const long ci = std::strtol(c.args[0].c_str(), &end, 10);
    if (!end || *end || ci < 0) { labFail("LAB REFUSED voxels: '%s' is not a cascade\n", c.args[0].c_str()); return false; }
    const std::string kind = c.args[1];
    // THE CHAIN'S ANSWER: no voxel work owed for two consecutive frames (GiStatus::chainOwed), cap 600 frames
    int waited = 0, quiet = 0;
    for (; waited < 600 && !mQuit && quiet < 2; ++waited) {
        const GiStatus &g = mScene->published().gi;
        bool busy = g.chainOwed;
        for (const auto &cs : g.cascades) busy = busy || cs.pending > 0 || cs.scrolling;
        quiet = busy ? 0 : quiet + 1;
        if (quiet < 2) frame();
    }
    if (quiet < 2) { labFail("LAB VOXELS FAILED: the chain still owed voxel work after %d frames\n", waited); return false; }
    if (kind == "stats") {
        const GiVoxelStats st = mScene->giVoxelStats(int(ci));
        if (!st.available) { labFail("LAB VOXELS FAILED: no stats for cascade %ld\n", ci); return false; }
        std::printf("LAB VOXELS STATS cascade %d %dx%dx%d format %s peak %.6g peakDirect %.6g (radiance) meanLit %.6g lit %lld "
                    "atMax %lld directAtMax %lld aboveOne %lld peakEmissive %.6g light %s direct %s store %s (waited %d "
                    "frames)\n",
                    st.cascade, st.width, st.height, st.depth, st.format.c_str(), st.peak, st.peakDirect, st.meanLit, st.voxelsLit,
                    st.voxelsAtMax, st.directAtMax, st.voxelsAboveOne, st.peakEmissive, st.lightDigest.c_str(),
                    st.directDigest.c_str(), st.storeDigest.c_str(), waited);
        return true;
    }
    GiVoxelVolume v;
    if (!mScene->giVoxelVolume(int(ci), v) || !v.available) {
        labFail("LAB VOXELS FAILED: no volume for cascade %ld\n", ci);
        return false;
    }
    const std::vector<float> *arr = kind == "direct" ? &v.direct : kind == "light" ? &v.light : kind == "back" ? &v.lightBack
                                  : kind == "albedo" ? &v.albedo : kind == "covp" ? &v.coverageP : kind == "covn" ? &v.coverageN
                                  : kind == "posp" ? &v.positionP : kind == "posn" ? &v.positionN : nullptr;
    if (!arr) {
        labFail("LAB REFUSED voxels: '%s' (direct|light|back|albedo|covp|covn|posp|posn|stats)\n", kind.c_str());
        return false;
    }
    const int res[3] = { v.width, v.height, v.depth };
    if (arr->size() != size_t(res[0]) * res[1] * res[2] * 4u) {
        labFail("LAB VOXELS FAILED: cascade %ld keeps no '%s' volume\n", ci, kind.c_str());
        return false;
    }
    // THE WORLD CELL of voxel 0 (the origin is an exact multiple of the cell: the lattice's own corner) and the box's span
    long long base[3];
    int lo[3] = { 0, 0, 0 }, hi[3] = { res[0], res[1], res[2] };
    for (int a = 0; a < 3; ++a) base[a] = (long long)std::llround(double(v.origin[a]) / double(v.cell[a]));
    if (c.args.size() > 3) {
        std::vector<float> b;
        std::string cur;
        for (char ch : c.args[3] + ",") {
            if (ch == ',') { if (!cur.empty()) b.push_back(float(std::strtod(cur.c_str(), nullptr))); cur.clear(); }
            else cur += ch;
        }
        if (b.size() != 6) { labFail("LAB REFUSED voxels: '%s' is not x0,y0,z0,x1,y1,z1\n", c.args[3].c_str()); return false; }
        for (int a = 0; a < 3; ++a) {
            // the voxels whose CENTRE lies in [b_a, b_a+3]
            lo[a] = std::max(0, int(std::ceil((double(b[size_t(a)]) - v.origin[a]) / v.cell[a] - 0.5)));
            hi[a] = std::min(res[a], int(std::floor((double(b[size_t(a + 3)]) - v.origin[a]) / v.cell[a] - 0.5)) + 1);
            if (hi[a] <= lo[a]) { labFail("LAB REFUSED voxels: the box misses cascade %ld\n", ci); return false; }
        }
    }
    QString path = QString::fromStdString(c.args.size() > 2 ? c.args[2] : "voxels-" + c.args[0] + "-" + kind + ".vox");
    if (QFileInfo(path).isRelative()) path = QDir(mOpt.dataRoot).filePath(path);
    std::ofstream out(path.toStdString(), std::ios::binary);
    out.precision(9);
    out << "{\"cascade\":" << ci << ",\"kind\":\"" << kind << "\",\"res\":[" << res[0] << "," << res[1] << "," << res[2]
        << "],\"origin\":[" << v.origin[0] << "," << v.origin[1] << "," << v.origin[2] << "],\"cell\":[" << v.cell[0]
        << "," << v.cell[1] << "," << v.cell[2] << "],\"lo\":[" << base[0] + lo[0]
        << "," << base[1] + lo[1] << "," << base[2] + lo[2] << "],\"dims\":[" << hi[0] - lo[0] << "," << hi[1] - lo[1]
        << "," << hi[2] - lo[2] << "]}\n";
    double peak = 0.0;
    long long nonzero = 0;
    for (int z = lo[2]; z < hi[2]; ++z)
        for (int y = lo[1]; y < hi[1]; ++y)
            for (int x = lo[0]; x < hi[0]; ++x) {
                const float *t = &(*arr)[((size_t(z) * res[1] + y) * res[0] + x) * 4u];
                out.write(reinterpret_cast<const char *>(t), 4 * sizeof(float));
                const double m = std::max(std::max(t[0], t[1]), t[2]);
                peak = std::max(peak, m);
                nonzero += m > 0.0 ? 1 : 0;
            }
    const bool ok = bool(out);
    std::printf("LAB VOXELS cascade %ld %s origin %.4f,%.4f,%.4f cell %.6f res %dx%dx%d world lo %lld,%lld,%lld dims %dx%dx%d "
                "peak %.6g nonzero %lld (waited %d frames) -> %s\n",
                ci, kind.c_str(), v.origin[0], v.origin[1], v.origin[2], v.cell[0], res[0], res[1], res[2], base[0] + lo[0],
                base[1] + lo[1], base[2] + lo[2], hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], peak, nonzero,
                waited, qPrintable(path));
    return ok;
}

void Session::readStdinLines()
{
    if (!mStdinOpen || !mOpt.readStdin) return;
    pollfd p{ 0, POLLIN, 0 };
    while (::poll(&p, 1, 0) > 0 && (p.revents & (POLLIN | POLLHUP))) {
        char buf[4096];
        const ssize_t n = ::read(0, buf, sizeof buf);
        if (n <= 0) { mStdinOpen = false; break; }
        mStdinBuf.append(buf, size_t(n));
    }
    size_t nl;
    while ((nl = mStdinBuf.find('\n')) != std::string::npos && !mQuit) {
        const std::string line = mStdinBuf.substr(0, nl);
        mStdinBuf.erase(0, nl + 1);
        execLine(line, "stdin");
    }
}

int Session::run(const Options &opt)
{
    mOpt = opt;
    mStart = std::chrono::steady_clock::now();
    QDir().mkpath(mOpt.dataRoot);
    if (!mOpt.script.isEmpty()) {
        std::ifstream in(mOpt.script.toStdString());
        if (!in) { std::fprintf(stderr, "jahlab: cannot read the script %s\n", qPrintable(mOpt.script)); return 2; }
        std::string line;
        while (std::getline(in, line)) mScriptLines.push_back(line);
    }
    std::string err;
    if (mOpt.bakeOnly) {
        // THE BAKE ALONE (a rig's first import, off the card: the bake is CPU work and never needs the lock)
        LoadReport r;
        const iris::SceneNodePtr m = loadModel(mOpt.file, mOpt.dataRoot, r);
        std::printf("LAB BAKE-ONLY %s: %s | %d meshes, %s triangles, %d file lights | parse %.0f ms, bake %.0f ms, read "
                    "%.0f ms | maps %d resolved, %d missing | %s\n", qPrintable(QFileInfo(mOpt.file).fileName()),
                    !m ? qPrintable(r.error) : r.baked ? "BAKED" : r.parsed ? "BAKE HIT, PARSED (the sidecar)" : "CACHE HIT", r.meshes, grouped(r.triangles).c_str(),
                    r.lights, r.parseMs, r.bakeMs, r.readMs, r.texturesResolved, r.texturesMissing, qPrintable(r.bakePath));
        return m ? 0 : 4;
    }
    // A RUN ON A MODEL NEVER BAKES UNDER THE CARD (the urgent list's #30, PHOTON-7 stage 5: two Bistro runs spent 37-46 s
    // of CPU baking inside the card lock, past the 60 s law): a cache miss refuses before the engine boots.
    if (mOpt.env.isEmpty() && !mOpt.file.isEmpty()) {
        QString model = mOpt.file;
        if (model.endsWith(QStringLiteral(".jlab"), Qt::CaseInsensitive)) {
            Jlab j;
            QString jerr;
            model = readJlab(model, j, jerr) && j.env.isEmpty() ? j.file : QString();
        }
        QString missing;
        if (!model.isEmpty() && !bakeCached(model, mOpt.dataRoot, missing)) {
            std::fprintf(stderr, "jahlab: %s: the bake cache under %s has %s - a lab run never bakes under the card; "
                                 "bake it first, off the card: jahlab --bake-only %s --data-root %s\n",
                         qPrintable(QFileInfo(model).fileName()), qPrintable(mOpt.dataRoot), qPrintable(missing),
                         qPrintable(QFileInfo(model).absoluteFilePath()), qPrintable(mOpt.dataRoot));
            return 5;
        }
    }
    if (!boot(err)) { std::fprintf(stderr, "jahlab: %s\n", err.c_str()); return 3; }
    if (!load(err)) { std::fprintf(stderr, "jahlab: %s\n", err.c_str()); return 4; }
    // THE FIRST FRAME: every mesh of the world is built by this sync (the mirror unbudgeted — the whole world arrives
    // in the frame, as a script's or a screenshot's sync does in the app).
    const double t0 = msSinceStart();
    frame();
    std::printf("LAB FIRST FRAME %.0f ms since start (the frame itself %.0f ms)\n", msSinceStart(), msSinceStart() - t0);
    std::fflush(stdout);
    // the .jlab's views, applied once the scene has drawn (the Atom view asks whether it can paint)
    if (mJlab.view != QStringLiteral("off")) execLine("view " + mJlab.view.toStdString(), "flag");
    if (!mJlab.gi) execLine("gi off", "flag");
    if (!mJlab.occlusion) execLine("occlusion off", "flag");
    if (!mJlab.draw) execLine("draw off", "flag");
    if (mJlab.overlay) execLine("overlay on", "flag");

    int scriptLine = 0;
    while (!mQuit) {
        if (!mScriptLines.empty()) {
            const std::string line = mScriptLines.front();
            mScriptLines.pop_front();
            ++scriptLine;
            // A REFUSED SCRIPT LINE STOPS THE SCRIPT (URGENT #50): no later line runs on a session that is not what its
            // script says - nonzero, the line and its reason named; `try <line>` opts the one line out (said, the
            // run's exit untouched)
            size_t b = 0;
            while (b < line.size() && std::isspace((unsigned char)line[b])) ++b;
            const bool tryLine = line.compare(b, 4, "try ") == 0;
            const std::string body = tryLine ? line.substr(b + 4) : line;
            if (execLine(body, tryLine ? "script-try" : "script")) continue;
            if (tryLine) {
                std::printf("LAB TRY (line %d) '%s' refused - %s - the script goes on\n", scriptLine, body.c_str(),
                            gLabLastFailure.c_str());
                std::fflush(stdout);
                continue;
            }
            std::printf("LAB SCRIPT STOPPED at line %d: '%s' - %s\n", scriptLine, line.c_str(), gLabLastFailure.c_str());
            std::fflush(stdout);
            mExit = 1;
            mScriptLines.clear();
            break;
        }
        if (mOpt.headless) {
            // NOTHING DRAWS UNASKED OFFSCREEN: the lab waits for the next line (an agent's stdin) or ends
            if (!mOpt.readStdin || !mStdinOpen) break;
            pollfd p{ 0, POLLIN, 0 };
            ::poll(&p, 1, 50);
            readStdinLines();
            continue;
        }
        frame();
        readStdinLines();
        if (!mWaitingSaid && mScriptLines.empty() && (!mOpt.readStdin || !mStdinOpen)) {
            mWaitingSaid = true;
            std::printf("LAB WAITING: no more commands (the script ended%s) — the window runs until Escape, the "
                        "window's close or `quit`\n", mOpt.readStdin ? ", stdin is closed" : "");
            std::fflush(stdout);
        }
    }
    std::printf("LAB END frames %llu, %.0f ms, exit %d\n", mFrames, msSinceStart(), mExit);
    std::fflush(stdout);
    return mExit;
}

}   // namespace lab
