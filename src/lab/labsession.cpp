#include "labsession.h"

#include "laboverlay.h"

#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/worldmodes.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Types.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>

#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

namespace lab {

using namespace jahshaka::engine;

namespace {

/// The overlay's rows are recomposed every this many frames (a number that changes 60 times a second is unreadable;
/// the app rate-limits the same way).
constexpr unsigned long long kOverlayEveryFrames = 10;
/// The render counters are subscribed in windows of this many frames (RenderStats: "the first read arms").
constexpr unsigned kStatsWindowFrames = 240;
/// settle: the cut's triangles unchanged over this many consecutive frames, nothing pending.
constexpr int kSettleSteadyFrames = 8;
constexpr int kSettleDefaultCap = 1200;

const char *tierName(int t) { return tierNames()[size_t(std::max(0, std::min(3, t)))].c_str(); }

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
    // THE ORDER (Engine.h:1971): a View before any Scene.
    if (mOpt.headless) {
        mView = mEngine->createOffscreenView("jahlab", mOpt.width, mOpt.height, Colour(0, 0, 0));
        // A STILL PICTURE: it gathers where the scene gathers (the editor screenshot's contract).
        if (mView) mView->setOffscreenContract(OffscreenContract::StillPicture);
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

bool Session::load(std::string &error)
{
    QString path = mOpt.file;
    if (path.endsWith(QStringLiteral(".jlab"), Qt::CaseInsensitive)) {
        QString err;
        if (!readJlab(path, mJlab, err)) { error = err.toStdString(); return false; }
        if (!mJlab.env.isEmpty()) { error = "environments arrive with stage 2 (--env)"; return false; }
        path = mJlab.file;
    } else {
        mJlab = Jlab();
        mJlab.file = QFileInfo(path).absoluteFilePath();
    }
    LoadReport r;
    iris::SceneNodePtr model = loadModel(path, mOpt.dataRoot, r);
    if (!model) { error = r.error.toStdString(); return false; }
    std::printf("LAB LOAD %s: %s | %d meshes, %s triangles, %d file lights | hash %.0f ms, parse %.0f ms, bake %.0f ms, "
                "read %.0f ms, fragment %.0f ms | maps %d resolved, %d missing, %d name collisions, %.0f ms | %d two-sided nodes | %s\n",
                qPrintable(QFileInfo(path).fileName()),
                r.baked ? "BAKED (cache miss)" : r.parsed ? "BAKE HIT, PARSED (the sidecar was missing or stale)"
                                                          : "CACHE HIT (no parse, no bake)",
                r.meshes, grouped(r.triangles).c_str(), r.lights, r.hashMs, r.parseMs, r.bakeMs, r.readMs, r.fragmentMs,
                r.texturesResolved, r.texturesMissing, r.textureNameCollisions, r.texturesMs, r.twoSidedNodes,
                qPrintable(r.bakePath));
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
    const QString name = QFileInfo(mJlab.file).fileName();
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
    mDoc->refresh();
    SceneMirror::FrameView fv;
    fv.view = mView;
    fv.environment = true;
    fv.sky = true;
    fv.camera = mCamera.node();
    fv.framingAspect = mCamera.node()->framingAspect();
    if (mOpt.headless) {
        // THE OFFSCREEN CHAIN'S OPT-IN (PostFxDesc::allowOffscreen): the world's post chain on the offscreen view, the
        // editor screenshot's door (enginesceneviewport.cpp takeScreenshot's Viewport grade).
        fv.finish = [](FrameInputView &part, View *v) {
            PostFxDesc fx = part.postFx ? *part.postFx : v->pushedPostFx();
            fx.allowOffscreen = true;
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
        std::printf("LAB REFUSED view %s: the view carries no id pass (the Low tier's view draws straight into its "
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
        std::printf("LAB SHOT FAILED %s: %s\n", qPrintable(path), mEngine->lastError().c_str());
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
        std::printf("LAB SHOT FAILED %s: cannot write\n", qPrintable(path));
        return false;
    }
    std::printf("LAB SHOT %s %ux%u colours %zu frame %llu%s\n", qPrintable(path), img.width, img.height, colours.size(),
                mFrames, extra ? (" (+" + std::to_string(extra) + " frames for the capture)").c_str() : "");
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
    o << ",\"gi\":{\"atRest\":" << (gi.giAtRest ? "true" : "false") << "}}";
    return o.str();
}

bool Session::execLine(const std::string &line, const char *source)
{
    Command c;
    std::string err;
    if (!parse(line, c, err)) {
        if (!err.empty()) {
            std::printf("LAB REFUSED (%s) %s\n", source, err.c_str());
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
        if (mode < 0) { std::printf("LAB REFUSED view '%s'\n", a0.c_str()); ok = false; }
        else ok = setView(mode);
    } else if (c.verb == "tier") {
        int t = -1;
        if (a0 == "next") t = (mTier + 1) % 4;
        for (size_t i = 0; i < tierNames().size(); ++i) if (tierNames()[i] == a0) t = int(i);
        if (t < 0) { std::printf("LAB REFUSED tier '%s'\n", a0.c_str()); ok = false; }
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
        if (!onOff(a0, mGi, v)) { ok = false; std::printf("LAB REFUSED gi '%s'\n", a0.c_str()); }
        else { mGi = v; worldmodes::setPhoton(mDoc, v, worldmodes::photonTier(mDoc)); }
    } else if (c.verb == "occlusion") {
        if (!onOff(a0, mOcclusion, v)) { ok = false; std::printf("LAB REFUSED occlusion '%s'\n", a0.c_str()); }
        else { mOcclusion = v; mScene->setAtomOcclusionEnabled(v); }
    } else if (c.verb == "draw") {
        if (!onOff(a0, mDraw, v)) { ok = false; std::printf("LAB REFUSED draw '%s'\n", a0.c_str()); }
        else { mDraw = v; mScene->setAtomDrawEnabled(v); }
    } else if (c.verb == "overlay") {
        if (!onOff(a0, mOverlay, v)) { ok = false; std::printf("LAB REFUSED overlay '%s'\n", a0.c_str()); }
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
                                                                  QFileInfo(mJlab.file).completeBaseName() + ".jlab")
                                  : QString::fromStdString(a0);
        QString err;
        ok = writeJlab(path, j, err);
        std::printf(ok ? "LAB SAVED %s\n" : "LAB SAVE FAILED %s\n", qPrintable(ok ? path : err));
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
    if (!ok && c.source == "script") mExit = 1;
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

    while (!mQuit) {
        if (!mScriptLines.empty()) {
            const std::string line = mScriptLines.front();
            mScriptLines.pop_front();
            execLine(line, "script");
            continue;
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
