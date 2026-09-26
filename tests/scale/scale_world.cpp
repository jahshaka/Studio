// THE SCALE FIXTURES — see scale_world.h for what they are and why they are built
// this way.
#include "scale_world.h"

#include "../support/proceduralshell.h"
#include "../support/wiredocumentgraph.h"

#include "irisgl/core/logger.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/import/meshbake.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRegularExpression>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>

namespace scale {

bool openRigWindow(unsigned w, unsigned h, void **display, unsigned long *window);
void closeRigWindow(void *display, unsigned long window);

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// ---------------------------------------------------------------------------
// memory
// ---------------------------------------------------------------------------

static unsigned long long statusKb(const char *key)
{
    std::ifstream f("/proc/self/status");
    std::string line;
    const size_t n = std::strlen(key);
    while (std::getline(f, line))
        if (line.compare(0, n, key) == 0) return std::strtoull(line.c_str() + n + 1, nullptr, 10);
    return 0;
}
unsigned long long peakRssKb() { return statusKb("VmHWM"); }
unsigned long long rssKb() { return statusKb("VmRSS"); }

// ---------------------------------------------------------------------------
// the bake cache
// ---------------------------------------------------------------------------

QString cacheDir()
{
    const QByteArray env = qgetenv("JAH_SCALE_ASSET_CACHE");
    const QString d = env.isEmpty() ? QStringLiteral(SCALE_ASSET_CACHE_DIR) : QString::fromLocal8Bit(env);
    QDir().mkpath(d);
    return d;
}

static QString fileOid(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

static void fillInfo(BakeInfo &info, const QList<iris::MeshPtr> &meshes)
{
    info.pieces = meshes.size();
    info.triangles = 0;
    info.levels = 0;
    info.cards = info.clusters = info.groups = 0;
    info.coarsestTriangles = 0;
    info.level7Triangles = 0;
    for (const iris::MeshPtr &mesh : meshes) {
        if (mesh.isNull()) continue;
        const iris::IndexBufferPtr ib = mesh->getIndexBuffer();
        info.triangles += ib ? size_t(ib->dataSize) / sizeof(unsigned) / 3u : 0u;
        info.levels = std::max(info.levels, int(mesh->lodIndices.size()) + 1);
        info.cards += mesh->cards.size();
        info.clusters += mesh->clusterDag.clusters.size();
        info.groups += mesh->clusterDag.groups.size();
        const auto tris = [&](int level) -> size_t {
            if (level <= 0) return ib ? size_t(ib->dataSize) / sizeof(unsigned) / 3u : 0u;
            return size_t(mesh->lodIndices[level - 1].size()) / 3u;
        };
        const int last = int(mesh->lodIndices.size());
        info.coarsestTriangles += tris(last);
        info.level7Triangles += tris(std::min(last, 7));
    }
}

/// Captures the bake's own "cluster DAG ... N ms" log line — the DAG stage's time
/// as the bake measured it, without a second DAG build.
struct DagLogTap {
    double ms = 0.0;
    DagLogTap()
    {
        iris::Logger::setSink([this](int, const QString &text) {
            static const QRegularExpression re(QStringLiteral("cluster DAG .*; ([0-9.]+) ms"));
            const auto m = re.match(text);
            if (m.hasMatch()) ms += m.captured(1).toDouble();
        });
    }
    ~DagLogTap() { iris::Logger::setSink(nullptr); }
};

static QList<iris::MeshPtr> bakeOrRead(const QString &sourcePath, const QString &name, const QString &oid,
                                       BakeInfo *infoOut, bool forceBake)
{
    BakeInfo info;
    info.name = name;
    const QString fp = iris::MeshBake::fingerprintFor(oid);
    const QString blob = cacheDir() + "/" + name + "-" + iris::MeshBake::fileNameFor(oid);
    info.blobPath = blob;
    QList<iris::MeshPtr> mesh;
    if (!forceBake && QFileInfo::exists(blob)) {
        const auto t0 = Clock::now();
        iris::MeshBake::Model m = iris::MeshBake::read(blob, fp);
        info.readMs = msSince(t0);
        if (m.valid && !m.meshes.isEmpty()) {
            mesh = m.meshes;
            info.fromCache = true;
            // THE BAKE'S OWN RECORD, written beside the blob when it was made: a
            // cache hit still reports what the bake cost (W11's row).
            QFile side(blob + ".json");
            if (side.open(QIODevice::ReadOnly)) {
                const QJsonObject o = QJsonDocument::fromJson(side.readAll()).object();
                info.bakeMs = o.value("bakeMs").toDouble();
                info.dagMs = o.value("dagMs").toDouble();
                info.peakRssKb = (unsigned long long)o.value("peakRssKb").toDouble();
            }
        }
    }
    if (mesh.isEmpty()) {
        if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath)) {
            if (infoOut) *infoOut = info;
            return mesh;
        }
        const QString extract = cacheDir() + "/extract-" + name;
        QDir().mkpath(extract);
        iris::MeshBake::Model m;
        {
            DagLogTap tap;
            const auto t0 = Clock::now();
            m = iris::MeshBake::buildFromFile(sourcePath, fp, extract);
            info.bakeMs = msSince(t0);
            info.dagMs = tap.ms;
        }
        info.peakRssKb = peakRssKb();
        QDir(extract).removeRecursively();
        if (!m.valid || m.meshes.isEmpty()) {
            std::printf("FAIL: the bake of %s produced nothing\n", qPrintable(sourcePath));
            if (infoOut) *infoOut = info;
            return mesh;
        }
        QString err;
        if (!iris::MeshBake::write(blob, m, &err)) {
            std::printf("   (the bake cache could not be written: %s)\n", qPrintable(err));
        } else {
            QJsonObject o;
            o["bakeMs"] = info.bakeMs;
            o["dagMs"] = info.dagMs;
            o["peakRssKb"] = double(info.peakRssKb);
            o["source"] = sourcePath;
            QFile side(blob + ".json");
            if (side.open(QIODevice::WriteOnly)) side.write(QJsonDocument(o).toJson());
        }
        mesh = m.meshes;
    }
    info.blobBytes = QFileInfo(blob).size();
    fillInfo(info, mesh);
    if (infoOut) *infoOut = info;
    return mesh;
}

iris::MeshPtr bakedMesh(const QString &sourcePath, const QString &name, BakeInfo *info, bool forceBake)
{
    const QList<iris::MeshPtr> ms = bakeOrRead(sourcePath, name, fileOid(sourcePath), info, forceBake);
    return ms.isEmpty() ? iris::MeshPtr() : ms.first();
}

/// The shell's oid is its GENERATOR's identity, not its bytes: the same parameters
/// always write the same file, so the key is known without writing the 290 MB PLY.
/// proceduralShellKey hashes the version constant AND every shape parameter, so a
/// shape change can never reuse an old blob.
static QString shellOid(size_t triangles)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        QByteArray::fromStdString(enginetest::proceduralShellKey(triangles)), QCryptographicHash::Sha256).toHex());
}

QString shellBlobPath(size_t triangles)
{
    return cacheDir() + "/" + QStringLiteral("shell-%1").arg(qulonglong(triangles)) + "-" +
           iris::MeshBake::fileNameFor(shellOid(triangles));
}

QList<iris::MeshPtr> shellAsset(size_t triangles, BakeInfo *info, bool bakeIfMissing)
{
    const QString name = QStringLiteral("shell-%1").arg(qulonglong(triangles));
    const QString oid = shellOid(triangles);
    if (QFileInfo::exists(shellBlobPath(triangles)))
        return bakeOrRead(QString(), name, oid, info, false);
    if (!bakeIfMissing) {
        if (info) { *info = BakeInfo(); info->name = name; }
        return QList<iris::MeshPtr>();
    }
    const QString ply = cacheDir() + "/" + name + ".ply";
    {
        const enginetest::ShellMesh s = enginetest::proceduralShell(triangles);
        if (!enginetest::writeBinaryPly(s, ply.toStdString())) {
            std::printf("FAIL: could not write %s\n", qPrintable(ply));
            return QList<iris::MeshPtr>();
        }
    }
    QList<iris::MeshPtr> mesh = bakeOrRead(ply, name, oid, info, true);
    QFile::remove(ply);
    return mesh;
}

// ---------------------------------------------------------------------------
// the environment
// ---------------------------------------------------------------------------

bool boot(Env &env, const char *logFile, int w, int h)
{
    const bool window = qgetenv("JAH_SCALE_WINDOW") == "1";
    std::string err;
    EngineConfig cfg;
    cfg.backend = Backend::Vulkan;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = logFile;
    if (window) {
        if (!openRigWindow(unsigned(w), unsigned(h), &env.xDisplay, &env.xWindow)) {
            std::printf("FAIL: no X display (DISPLAY must name the rig's Xvfb)\n");
            return false;
        }
        cfg.display = static_cast<NativeDisplayHandle>(reinterpret_cast<unsigned long long>(env.xDisplay));
    }
    env.engine = Engine::create(cfg, err);
    if (!env.engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return false; }
    // WIRED EXACTLY AS THE APP WIRES IT (src/bridge/enginehost.cpp): the document's
    // nodes live in the engine's staging scene, and the renderer learns "nothing
    // moved" from the document's transform-write counter — without it every frame
    // re-scans every item (and a document write never moves the ray tier's epoch).
    enginetest::wireDocumentGraph(env.engine.get());
    env.width = w;
    env.height = h;
    env.view = window ? env.engine->createView("scale", static_cast<NativeWindowHandle>(env.xWindow),
                                               unsigned(w), unsigned(h), Colour(0, 0, 0))
                      : env.engine->createOffscreenView("scale", unsigned(w), unsigned(h), Colour(0, 0, 0));
    if (!env.view) { std::printf("FAIL: createView: %s\n", env.engine->lastError().c_str()); return false; }
    env.scene = env.engine->createScene("scale");
    env.view->setScene(env.scene);
    if (!window) {
        env.envView = env.engine->createOffscreenView("scale-env", 64, 64, Colour(0, 0, 0));
        env.envView->setScene(env.scene);
        env.envView->setEnabled(false);
    }
    env.doc = iris::Scene::create();
    env.mirror.reset(new SceneMirror(env.scene));
    env.mirror->setSource(env.doc);
    std::printf("VIEW: %s %dx%d\n", window ? "WINDOW (JAH_SCALE_WINDOW=1)" : "offscreen + the viewport's post chain",
                w, h);
    env.camera = iris::CameraNode::create();
    setCamera(env, iris::Vec3(0, 20, 60), iris::Vec3(0, 0, 0));
    return env.view && env.scene;
}

void setCamera(Env &env, const iris::Vec3 &pos, const iris::Vec3 &lookAt)
{
    env.camera->setLocalPos(pos);
    env.camera->lookAt(lookAt);
    env.camera->update(0.0f);
}

void frame(Env &env, int count)
{
    for (int i = 0; i < count; ++i) {
        env.doc->refresh();
        env.mirror->sync();
        if (env.envView) {
            // THE OFFSCREEN CHAIN (the editor screenshot's door, PostFxDesc::
            // allowOffscreen): the document's environment is applied every frame —
            // its scene half (the GI settle, the lamp re-inject) as the editor runs
            // it — through a DISABLED stand-in view, and the world's post
            // description is copied onto the measured view with the flag set. The
            // mirror builds a fresh description each call, so pushing it onto the
            // measured view directly and then setting the flag would rebuild that
            // view's workspace twice a frame.
            env.mirror->applyEnvironment(env.envView, env.engine.get());
            PostFxDesc fx = env.envView->postFx();
            fx.allowOffscreen = true;
            if (env.fxOverride) env.fxOverride(fx);
            if (fx != env.view->postFx()) env.view->setPostFx(fx);
            if (env.view->shadows() != env.doc->shadowEnabled) env.view->setShadows(env.doc->shadowEnabled);
        } else {
            env.mirror->applyEnvironment(env.view, env.engine.get());
            if (env.fxOverride) {
                // A MEASUREMENT'S OWN POST FLAGS (W7's depth pyramid) on a window
                // view: pushed over the world's description after it — a rebuild
                // only on the frame the flag first differs, then the same value.
                PostFxDesc fx = env.view->postFx();
                env.fxOverride(fx);
                if (fx != env.view->postFx()) env.view->setPostFx(fx);
            }
        }
        env.mirror->applyCamera(env.camera, env.view);
        env.engine->renderOneFrame();
    }
}

void shutdown(Env &env)
{
    if (env.mirror) env.mirror->setSource(iris::ScenePtr());
    env.mirror.reset();
    env.doc.reset();
    env.camera.reset();
    enginetest::unwireDocumentGraph();
    env.engine.reset();
    if (env.xDisplay) {
        closeRigWindow(env.xDisplay, env.xWindow);
        env.xDisplay = nullptr;
        env.xWindow = 0;
    }
}

// ---------------------------------------------------------------------------
// the world
// ---------------------------------------------------------------------------

/// The world's meshes: the sixteen shipped primitives + the two shipped
/// high-poly models (the Matcaps dragon, the Physics sample's model), each
/// through the bake.
static std::vector<std::pair<QString, QString>> worldMeshSources(bool withModels)
{
    std::vector<std::pair<QString, QString>> out;
    const QDir prim(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives"));
    for (const QString &f : prim.entryList({ "*.obj" }, QDir::Files, QDir::Name)) {
        if (f == QLatin1String("endlessplane.obj") || f == QLatin1String("plane.obj")) continue;
        out.push_back({ prim.filePath(f), QFileInfo(f).baseName() });
    }
    if (withModels) {
        const QDir fx(QStringLiteral(SCALE_MODEL_DIR));
        out.push_back({ fx.filePath("matcaps_dragon.obj"), "matcaps-dragon" });
        out.push_back({ fx.filePath("physics_model.obj"), "physics-model" });
    }
    return out;
}

/// A tiny distinct texture per index — a real picture a real material binds, so
/// each one is its own decode bucket (HlmsAtom::BucketKey::textures).
static QString textureFile(int i)
{
    const QString dir = cacheDir() + "/textures";
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/tex-%1.png").arg(i, 4, 10, QLatin1Char('0'));
    if (!QFileInfo::exists(path)) {
        QImage img(16, 16, QImage::Format_RGB888);
        const int r = (i * 97) % 200 + 40, g = (i * 57) % 200 + 40, b = (i * 31) % 200 + 40;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                img.setPixelColor(x, y, ((x / 4 + y / 4) & 1) ? QColor(r, g, b) : QColor(b, r, g));
        img.save(path);
    }
    return path;
}

static iris::PbrMaterialPtr worldMaterial(int i, const std::vector<QString> &texFiles)
{
    auto mat = iris::PbrMaterial::create();
    const int c = i * 37;
    mat->setValue("baseColor", QColor(90 + c % 160, 90 + (c / 3) % 160, 90 + (c / 7) % 160));
    mat->setValue("roughness", 0.35f + 0.5f * float((i * 13) % 10) / 10.0f);
    mat->setValue("metallic", (i % 11) == 0 ? 1.0f : 0.0f);
    if (!texFiles.empty()) mat->setValue("baseColorMap", texFiles[size_t(i) % texFiles.size()]);
    return mat;
}

void applyMaterials(World &world, int materials, int textures)
{
    std::vector<QString> texFiles;
    for (int t = 0; t < textures; ++t) texFiles.push_back(textureFile(t));
    std::vector<iris::PbrMaterialPtr> pool;
    for (int i = 0; i < materials; ++i) pool.push_back(worldMaterial(i, texFiles));
    for (size_t i = 0; i < world.items.size(); ++i)
        world.items[i]->setMaterial(pool.empty() ? worldMaterial(int(i), texFiles) : pool[i % pool.size()]);
    world.spec.materials = materials;
    world.spec.textures = textures;
}

static float meshExtent(const iris::MeshPtr &m)
{
    const iris::AABB b = m->getAABB();
    const iris::Vec3 d = b.getMax() - b.getMin();
    return std::max(d.x(), std::max(d.y(), d.z()));
}

bool buildWorld(Env &env, const WorldSpec &spec, World &world, int settleCap)
{
    world = World();
    world.spec = spec;
    iris::ScenePtr doc = env.doc;

    // ---- the meshes, through the bake (the cache makes a process pay a read) --
    auto t0 = Clock::now();
    std::vector<iris::MeshPtr> meshes;
    std::vector<float> extents;
    for (const auto &src : worldMeshSources(spec.withShippedModels)) {
        BakeInfo info;
        iris::MeshPtr m = bakedMesh(src.first, src.second, &info);
        if (m.isNull()) { std::printf("FAIL: world mesh %s\n", qPrintable(src.second)); return false; }
        meshes.push_back(m);
        extents.push_back(std::max(1e-3f, meshExtent(m)));
        world.meshes.push_back(info);
    }
    BakeInfo groundInfo;
    iris::MeshPtr groundMesh =
        bakedMesh(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/models/ground.obj"), "ground", &groundInfo);
    if (groundMesh.isNull()) { std::printf("FAIL: the ground mesh\n"); return false; }
    world.bakeOrReadMs = msSince(t0);

    // ---- the document --------------------------------------------------------
    t0 = Clock::now();
    // The new-scene template's sky and lights (MainWindow's new scene): the
    // realistic sky with the sun following it, the Sky Light's fill.
    doc->skyType = iris::SkyType::REALISTIC;
    doc->setSkyRealistic(iris::SkyRealistic::defaults());
    doc->skyColor = QColor(96, 96, 96);
    doc->fogColor = QColor(96, 96, 96);
    doc->shadowEnabled = true;
    worldmodes::setMode(doc, worldmodes::Mode(int(spec.tier)));
    worldmodes::setPhoton(doc, true, spec.tier);

    // THE GROUND: the default floor's mesh and material values, scaled from its
    // 100 m to the spec's size (defaultfloor.cpp's node, without the project).
    {
        auto g = iris::MeshNode::create();
        g->setName("Ground");
        g->setMesh(groundMesh);
        const float k = spec.groundSize / std::max(1e-3f, meshExtent(groundMesh));
        g->setLocalPos(iris::Vec3(0, 1e-4f, 0));
        g->setLocalScale(iris::Vec3(k, 1.0f, k));
        g->setFaceCullingMode(iris::FaceCullingMode::None);
        g->setShadowCastingEnabled(false);
        auto mat = iris::PbrMaterial::create();
        mat->setValue("baseColor", QColor(255, 255, 255));
        mat->setValue("baseColorMap", QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/textures/tile.png"));
        mat->setValue("roughness", 0.9f);
        mat->setValue("metallic", 0.0f);
        g->setMaterial(mat);
        doc->getRootNode()->addChild(g);
        world.ground = g;
    }

    // THE SUN (the template's first directional) and THE SKY LIGHT.
    {
        auto sun = iris::LightNode::create();
        sun->setLightType(iris::LightType::Directional);
        sun->setName("Sun");
        sun->setLocalPos(iris::Vec3(4, 4, 0));
        sun->setLocalRot(iris::Quat::fromEulerAngles(-50.0f, 30.0f, 0.0f));
        doc->getRootNode()->addChild(sun);
        sun->setPropertyValue(QStringLiteral("intensity"), 1.0f);
        world.sun = sun;
        auto sky = iris::LightNode::create();
        sky->setLightType(iris::LightType::Sky);
        sky->setName("Sky Light");
        doc->getRootNode()->addChild(sky);
        sky->setPropertyValue(QStringLiteral("intensity"), 1.0f);
    }

    // THE INSTANCES: a jittered grid centred on the origin, the meshes cycled,
    // three scales (0.5 m, 1 m, 2.5 m largest axis). Deterministic (a fixed seed).
    std::mt19937 rng(20260926u);
    std::uniform_real_distribution<float> jit(-0.3f, 0.3f), yaw(0.0f, 360.0f);
    const int side = std::max(1, int(std::ceil(std::sqrt(double(spec.instances)))));
    const float half = 0.5f * spec.spacing * float(side - 1);
    static const float kScales[3] = { 0.5f, 1.0f, 2.5f };
    for (int i = 0; i < spec.instances; ++i) {
        const int gx = i % side, gz = i / side;
        const size_t mi = size_t(i) % meshes.size();
        auto n = iris::MeshNode::create();
        n->setName(QStringLiteral("item%1").arg(i));
        n->setMesh(meshes[mi]);
        const float size = kScales[(i / int(meshes.size())) % 3];
        const float k = size / extents[mi];
        const iris::AABB b = meshes[mi]->getAABB();
        const float x = float(gx) * spec.spacing - half + jit(rng) * spec.spacing;
        const float z = float(gz) * spec.spacing - half + jit(rng) * spec.spacing;
        n->setLocalPos(iris::Vec3(x, -b.getMin().y() * k, z));   // standing on the ground
        n->setLocalRot(iris::Quat::fromEulerAngles(0.0f, yaw(rng), 0.0f));
        n->setLocalScale(iris::Vec3(k, k, k));
        // (materials are assigned below, all at once — applyMaterials)
        doc->getRootNode()->addChild(n);
        world.items.push_back(n);
    }

    applyMaterials(world, spec.materials, spec.textures);

    // THE LAMPS: `lights` on a lightGrid-metre grid over the populated square,
    // 4 m up, 15 m range; every fourth a spot pointing down.
    const int lside = std::max(1, int(std::ceil(std::sqrt(double(spec.lights)))));
    const float lhalf = 0.5f * spec.lightGrid * float(lside - 1);
    for (int i = 0; i < spec.lights; ++i) {
        auto l = iris::LightNode::create();
        const bool spot = (i % 4) == 3;
        l->setLightType(spot ? iris::LightType::Spot : iris::LightType::Point);
        l->setName(QStringLiteral("lamp%1").arg(i));
        l->setLocalPos(iris::Vec3(float(i % lside) * spec.lightGrid - lhalf, 4.0f,
                                  float(i / lside) * spec.lightGrid - lhalf));
        if (spot) l->setLocalRot(iris::Quat::fromEulerAngles(-90.0f, 0.0f, 0.0f));
        doc->getRootNode()->addChild(l);
        l->setPropertyValue(QStringLiteral("intensity"), 2.0f);
        l->setPropertyValue(QStringLiteral("distance"), 15.0f);
        l->setPropertyValue(QStringLiteral("lightColor"),
                            QColor(255, 200 + (i * 7) % 55, 160 + (i * 13) % 95));
        l->shadowMap->shadowType = iris::ShadowMapType::None;
        world.lights.push_back(l);
    }
    doc->getRootNode()->applyStaticDefaults();
    world.documentMs = msSince(t0);

    // ---- the first sync + frame, then GI to rest ------------------------------
    setCamera(env, worldEye() + iris::Vec3(0, 8.0f, 30.0f), worldEye());
    t0 = Clock::now();
    frame(env, 1);
    world.firstSyncMs = msSince(t0);
    int f = 0;
    for (; f < settleCap; ++f) {
        if (env.scene->giStatus().giAtRest) break;
        frame(env, 1);
    }
    world.settleFrames = f;
    const GiStatus gi = env.scene->giStatus();
    std::printf("WORLD: %d instances of %zu baked meshes + ground %.0f m, %d lamps on %.0f m, "
                "tier %s, %s materials, %d textures | meshes %.0f ms (%s), document %.0f ms, "
                "first sync+frame %.0f ms, settle %d frames (atRest %d) | cascades %zu, rss %.0f MB\n",
                spec.instances, meshes.size(), spec.groundSize, spec.lights, spec.lightGrid,
                qPrintable(worldmodes::photonTierName(spec.tier)),
                spec.materials ? qPrintable(QString::number(spec.materials)) : "per-instance", spec.textures,
                world.bakeOrReadMs, world.meshes.empty() || world.meshes[0].fromCache ? "cache" : "baked",
                world.documentMs, world.firstSyncMs, f, int(gi.giAtRest), gi.cascades.size(),
                double(rssKb()) / 1024.0);
    std::fflush(stdout);
    return true;
}

// ---------------------------------------------------------------------------
// measuring
// ---------------------------------------------------------------------------

void armMonitor(Env &env)
{
    RenderStats rs;
    env.engine->renderStats(rs);
    env.engine->setFrameMonitor(MonitorLevel::Review);
}

std::vector<FrameRecord> drain(Env &env)
{
    std::vector<FrameRecord> out;
    env.engine->takeFrameRecords(out);
    return out;
}

bool gpuTimed(Env &env)
{
    const MonitorStatus st = env.engine->monitorStatus();
    return st.gpuCompiled && st.gpuSupported;
}

float median(std::vector<float> v)
{
    if (v.empty()) return -1.0f;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double medianD(std::vector<double> v)
{
    if (v.empty()) return -1.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

void target(const char *wall, double value, const char *unit, const char *what, const char *bar)
{
    // A NEGATIVE READING IS "NOT MEASURED" (the monitor's convention: an unsampled GPU
    // ms, an empty median) and is never printed as a number.
    if (value < 0.0) {
        std::printf("target: unsampled (bar %s) %s %s: %s\n", bar, wall, unit, what);
        std::fflush(stdout);
        return;
    }
    std::printf("target: %.4f (bar %s) %s %s: %s\n", value, bar, wall, unit, what);
    std::fflush(stdout);
}

}   // namespace scale
