#include "labscene.h"

#include "irisgl/core/geometry/aabb.h"
#include "irisgl/core/math/mat4.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/material.h"
#include "irisgl/document/materials/meshmaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/worldmodes.h"
#include "irisgl/import/importsettings.h"
#include "irisgl/import/materialhelper.h"
#include "irisgl/import/meshbake.h"
#include "irisgl/import/scenelights.h"
#include "irisgl/import/scenesource.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <chrono>
#include <cstdio>
#include <functional>

namespace lab {

using iris::MeshBake;

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

/// The source's content id: the sha256 of its bytes, hex — what the app's store names it by (AssetCas::hashFile).
QString fileOid(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return QString();
    return QString::fromLatin1(h.result().toHex());
}

QJsonArray vec3Json(const iris::Vec3 &v) { return QJsonArray{ double(v.x()), double(v.y()), double(v.z()) }; }
iris::Vec3 jsonVec3(const QJsonValue &v)
{
    const QJsonArray a = v.toArray();
    return a.size() == 3 ? iris::Vec3(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble())) : iris::Vec3();
}
QJsonArray quatJson(const iris::Quat &q) { return QJsonArray{ double(q.x()), double(q.y()), double(q.z()), double(q.scalar()) }; }
iris::Quat jsonQuat(const QJsonValue &v)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 4) return iris::Quat();
    return iris::Quat(float(a[3].toDouble()), float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble()));
}

/// The adopted lights of a parsed fragment, as the sidecar records them (see labscene.h).
QJsonArray lightsOf(const iris::SceneNodePtr &root)
{
    QJsonArray out;
    std::function<void(const iris::SceneNodePtr &, QJsonArray)> walk = [&](const iris::SceneNodePtr &n, QJsonArray path) {
        const QList<iris::SceneNodePtr> kids = n->children();
        for (int i = 0; i < kids.size(); ++i) {
            const iris::SceneNodePtr &c = kids[i];
            if (c->getSceneNodeType() == iris::SceneNodeType::Light) {
                auto l = c.staticCast<iris::LightNode>();
                QJsonObject o;
                o["parent"] = path;
                o["name"] = l->getName();
                o["type"] = int(l->lightType);
                o["intensity"] = double(l->intensity);
                o["color"] = l->color.name();
                o["distance"] = double(l->distance);
                o["spotCutOff"] = double(l->spotCutOff);
                o["spotCutOffSoftness"] = double(l->spotCutOffSoftness);
                o["rectWidth"] = double(l->rectWidth);
                o["rectHeight"] = double(l->rectHeight);
                o["pos"] = vec3Json(l->getLocalPos());
                o["rot"] = quatJson(l->getLocalRot());
                out.append(o);
                continue;
            }
            if (c->getSceneNodeType() == iris::SceneNodeType::Camera) continue;
            QJsonArray p = path;
            p.append(i);
            walk(c, p);
        }
    };
    walk(root, QJsonArray());
    return out;
}

int attachLights(const iris::SceneNodePtr &root, const QJsonArray &lights)
{
    int n = 0;
    for (const QJsonValue &v : lights) {
        const QJsonObject o = v.toObject();
        iris::SceneNodePtr parent = root;
        for (const QJsonValue &i : o["parent"].toArray()) {
            const QList<iris::SceneNodePtr> kids = parent->children();
            const int k = i.toInt(-1);
            if (k < 0 || k >= kids.size()) { parent.reset(); break; }
            parent = kids[k];
        }
        if (!parent) continue;
        auto l = iris::LightNode::create();
        l->setName(o["name"].toString());
        l->setLightType(iris::LightType(o["type"].toInt()));
        l->intensity = float(o["intensity"].toDouble());
        l->color = QColor(o["color"].toString());
        l->distance = float(o["distance"].toDouble());
        l->spotCutOff = float(o["spotCutOff"].toDouble());
        l->spotCutOffSoftness = float(o["spotCutOffSoftness"].toDouble());
        l->rectWidth = float(o["rectWidth"].toDouble());
        l->rectHeight = float(o["rectHeight"].toDouble());
        l->setLocalPos(jsonVec3(o["pos"]));
        l->setLocalRot(jsonQuat(o["rot"]));
        parent->addChild(l, false);   // the file's own transform chain places it (scenelights.cpp's addChild)
        ++n;
    }
    return n;
}

/// THE BAKE'S TEXTURES BY NAME (meshbake.cpp:6340-6373: a bake records every map by its BARE file name — the app
/// re-points them from the catalog's definition at open, AssetHelper::updateNodeMaterial). The lab has no catalog: it
/// resolves a name against the bake's own extract directory (the embedded images and split maps the bake wrote) and
/// then the model's folder tree — where MaterialHelper found them at the parse (materialhelper.cpp containedTexturePath:
/// inside the model's directory). A name found twice in the tree resolves to the shallowest, and is counted.
struct TextureIndex
{
    QHash<QString, QString> byName;   ///< lower-case file name -> absolute path
    int collisions = 0, resolved = 0, missing = 0;
    void add(const QString &root, int maxDepth)
    {
        std::function<void(const QString &, int)> walk = [&](const QString &dir, int depth) {
            const QFileInfoList list = QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                                                               QDir::DirsLast | QDir::Name);
            for (const QFileInfo &f : list) {
                if (f.isDir()) { if (depth < maxDepth) walk(f.absoluteFilePath(), depth + 1); continue; }
                const QString k = f.fileName().toLower();
                if (byName.contains(k)) { ++collisions; continue; }
                byName.insert(k, f.absoluteFilePath());
            }
        };
        if (!root.isEmpty() && QFileInfo(root).isDir()) walk(root, 0);
    }
    void resolve(QString &name)
    {
        if (name.isEmpty() || QFileInfo(name).isAbsolute()) return;
        const auto it = byName.constFind(QFileInfo(name).fileName().toLower());
        if (it == byName.constEnd()) { ++missing; return; }
        name = it.value();
        ++resolved;
    }
    void resolve(iris::MeshMaterialData &d)
    {
        for (QString *t : { &d.diffuseTexture, &d.specularTexture, &d.normalTexture, &d.hightTexture,
                            &d.baseColorTexture, &d.metallicTexture, &d.roughnessTexture, &d.emissiveTexture,
                            &d.specularMapTexture })
            resolve(*t);
    }
};

unsigned long long trianglesOf(const iris::SceneNodePtr &n, int &meshes)
{
    unsigned long long t = 0;
    if (n->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto m = n.staticCast<iris::MeshNode>();
        if (m->getMesh()) { t += (unsigned long long)std::max(0, m->getMesh()->numFaces); ++meshes; }
    }
    for (const iris::SceneNodePtr &c : n->children()) t += trianglesOf(c, meshes);
    return t;
}

}   // namespace

iris::SceneNodePtr loadModel(const QString &pathIn, const QString &dataRoot, LoadReport &r)
{
    r = LoadReport();
    const QFileInfo fi(pathIn);
    if (!fi.isFile()) { r.error = QStringLiteral("no such file: %1").arg(pathIn); return {}; }
    const QString path = fi.absoluteFilePath();

    // THE KEY: the default import settings (an import with no dialog change) and the file's content.
    const iris::ImportSettings settings;
    const iris::ImportTransform xf = settings.transform();
    const QString settingsHash = settings.hash();
    auto t0 = Clock::now();
    const QString oid = fileOid(path);
    r.hashMs = msSince(t0);
    if (oid.isEmpty()) { r.error = QStringLiteral("could not read %1").arg(path); return {}; }
    const QString fingerprint = MeshBake::fingerprintFor(oid, settingsHash);
    const QString bakeDir = QDir(dataRoot).filePath(QStringLiteral("bakes"));
    QDir().mkpath(bakeDir);
    const QString bakeName = MeshBake::fileNameFor(oid, settingsHash);
    r.bakePath = QDir(bakeDir).filePath(bakeName);
    const QString lightsPath = r.bakePath + QStringLiteral(".lights.json");
    // where a bake writes the model's EMBEDDED textures and split maps (the app's staging dir); kept with the bake,
    // because the fragment's materials name them by path
    const QString extractDir = r.bakePath + QStringLiteral(".files");

    MeshBake::Model model;
    if (MeshBake::headerMatches(r.bakePath, fingerprint) && QFileInfo::exists(lightsPath)) {
        t0 = Clock::now();
        model = MeshBake::read(r.bakePath, fingerprint);
        r.readMs = msSince(t0);
    }
    if (!model.valid) {
        r.baked = true;
        QDir().mkpath(extractDir);
        iris::MaterialHelper::takeContainmentWarnings();
        iris::MaterialHelper::takeMissingMaps();
        t0 = Clock::now();
        iris::SceneSource source;
        if (!source.read(path, xf)) {
            r.error = QStringLiteral("could not parse %1: %2").arg(path, source.errorString());
            return {};
        }
        r.parseMs = msSince(t0);
        std::printf("LAB BAKE %s (oid %s, %d bake threads) ...\n", qPrintable(fi.fileName()), qPrintable(oid.left(16)),
                    MeshBake::bakeThreads());
        std::fflush(stdout);
        t0 = Clock::now();
        model = MeshBake::buildFromScene(source, path, fingerprint, extractDir, xf);
        r.bakeMs = msSince(t0);
        if (!model.valid) { r.error = QStringLiteral("the bake of %1 failed").arg(path); return {}; }
        std::printf("LAB BAKE stages: %s\n", qPrintable(model.stageSummary().left(400)));
        QString err;
        if (!MeshBake::write(r.bakePath, model, &err)) {
            r.error = QStringLiteral("could not write the bake %1: %2").arg(r.bakePath, err);
            return {};
        }
        // THE FILE'S LIGHTS, from the parse, into the sidecar (labscene.h): adopted onto a fragment of this parse's
        // bake (the same node shape as loadAsSceneFragment's — adoptLightsAndCameras pairs by name), then recorded.
        const iris::SceneNodePtr probe =
            MeshBake::buildFragment(model, path, [](iris::MeshPtr, iris::MeshMaterialData &d) {
                return iris::MaterialPtr(iris::materialFromMeshData(d));
            });
        if (probe) iris::adoptLightsAndCameras(source, probe);
        QJsonObject side;
        side["fingerprint"] = fingerprint;
        side["lights"] = probe ? lightsOf(probe) : QJsonArray();
        QFile lf(lightsPath + QStringLiteral(".tmp"));
        if (!lf.open(QIODevice::WriteOnly) || lf.write(QJsonDocument(side).toJson()) < 0) {
            r.error = QStringLiteral("could not write %1").arg(lightsPath);
            return {};
        }
        lf.close();
        QFile::remove(lightsPath);
        QFile::rename(lightsPath + QStringLiteral(".tmp"), lightsPath);
        const QStringList missing = iris::MaterialHelper::takeMissingMaps();
        if (!missing.isEmpty())
            std::printf("LAB BAKE: %lld map(s) the file names were not found (first: %s)\n",
                        (long long)missing.size(), qPrintable(missing.first()));
    }

    // THE FRAGMENT, every open the same way: the bake's node tree, its maps resolved by name (TextureIndex), the one
    // material conversion, the file's lights.
    t0 = Clock::now();
    TextureIndex tex;
    tex.add(extractDir, 0);
    tex.add(fi.absolutePath(), 3);
    iris::SceneNodePtr node = MeshBake::buildFragment(model, path, [&tex](iris::MeshPtr, iris::MeshMaterialData &d) {
        iris::MeshMaterialData resolved = d;
        tex.resolve(resolved);
        return iris::MaterialPtr(iris::materialFromMeshData(resolved));
    });
    r.texturesResolved = tex.resolved;
    r.texturesMissing = tex.missing;
    r.textureNameCollisions = tex.collisions;
    if (!node) { r.error = QStringLiteral("the bake of %1 holds no fragment").arg(path); return {}; }
    QFile lf(lightsPath);
    if (lf.open(QIODevice::ReadOnly))
        r.lights = attachLights(node, QJsonDocument::fromJson(lf.readAll()).object()["lights"].toArray());
    r.fragmentMs = msSince(t0);
    r.triangles = trianglesOf(node, r.meshes);
    if (node->getName().isEmpty()) node->setName(fi.completeBaseName());
    r.ok = true;
    return node;
}

bool hasDirectional(const iris::SceneNodePtr &n)
{
    if (!n) return false;
    if (n->getSceneNodeType() == iris::SceneNodeType::Light &&
        n.staticCast<iris::LightNode>()->lightType == iris::LightType::Directional)
        return true;
    for (const iris::SceneNodePtr &c : n->children())
        if (hasDirectional(c)) return true;
    return false;
}

bool addBasicWorld(const iris::ScenePtr &doc, const QString &binDir, const QString &dataRoot, bool wantSun,
                   QString &error)
{
    // THE BASIC TEMPLATE'S VALUES (src/services/scenetemplate.h:76-86, defaultfloormaterial.h:56-99,
    // scenetemplatebuilder.cpp:126-221) — the document half, without the store: Studio's builder pins the tile and
    // writes asset rows the lab has no database for.
    constexpr float kFloorSize = 100.0f, kFloorThickness = 1.0f, kSunPitchDegrees = 40.0f;
    const iris::Vec3 kSunPosition(4.0f, 4.0f, 0.0f);
    worldmodes::setMode(doc, worldmodes::Mode::Epic);   // a new scene starts on Epic (POST_CHAIN_SPEC §12 decision 8)

    // THE FLOOR: the shipped cube (a 2 m cube about its centre) through the same bake as any model.
    const QString cube = QDir(binDir).filePath(QStringLiteral("app/content/primitives/cube.obj"));
    LoadReport fr;
    iris::SceneNodePtr floorFrag = loadModel(cube, dataRoot, fr);
    if (!floorFrag || floorFrag->getSceneNodeType() != iris::SceneNodeType::Mesh) {
        error = QStringLiteral("the floor cube: %1").arg(fr.error.isEmpty() ? QStringLiteral("not one mesh") : fr.error);
        return false;
    }
    auto floor = floorFrag.staticCast<iris::MeshNode>();
    floor->setName(QStringLiteral("Floor"));
    floor->setLocalScale(iris::Vec3(kFloorSize * 0.5f, kFloorThickness * 0.5f, kFloorSize * 0.5f));
    floor->setLocalPos(iris::Vec3(0, -kFloorThickness * 0.5f, 0));
    floor->setShadowCastingEnabled(false);
    floor->setPickable(false);
    floor->defaultFloor = true;
    {
        auto m = iris::PbrMaterial::create();
        m->setValue("baseColor", QColor(255, 255, 255));
        m->setValue("baseColorMap",
                    iris::Material::textureRef(QDir(binDir).filePath(QStringLiteral("app/content/textures/tile.png")), QString()));
        m->setValue("textureScale", 25.0f);
        m->setValue("roughness", 1.0f);
        m->setValue("metallic", 0.0f);
        m->setValue("workflow", 1);
        m->setValue("ior", 1.0f);
        m->setValue("specularColor", QColor(0, 0, 0));
        floor->setMaterial(m);
    }
    doc->getRootNode()->addChild(floor);

    if (wantSun) {
        auto sun = iris::LightNode::create();
        sun->setLightType(iris::LightType::Directional);
        doc->getRootNode()->addChild(sun);
        sun->setName(QStringLiteral("Directional Light"));
        sun->setLocalPos(kSunPosition);
        sun->setLocalRot(iris::Quat::fromEulerAngles(kSunPitchDegrees, 0, 0));
        sun->setPropertyValue(QStringLiteral("intensity"), 1.0f);
    }
    auto sky = iris::LightNode::create();
    sky->setLightType(iris::LightType::Sky);
    doc->getRootNode()->addChild(sky);
    sky->setName(QStringLiteral("Sky Light"));
    sky->setLocalPos(iris::Vec3(-4, 4, 0));
    sky->setPropertyValue(QStringLiteral("intensity"), 1.0f);
    sky->setPropertyValue(QStringLiteral("lightColor"), QColor(255, 255, 255));

    doc->skyType = iris::SkyType::REALISTIC;
    doc->setSkyRealistic(iris::SkyRealistic::defaults());
    doc->skyColor = QColor(96, 96, 96);
    doc->fogColor = QColor(96, 96, 96);
    doc->shadowEnabled = true;
    doc->heightFog = iris::HeightFog();
    doc->heightFog.enabled = true;
    return true;
}

bool worldBounds(const iris::ScenePtr &doc, iris::Vec3 &mn, iris::Vec3 &mx)
{
    iris::AABB box;
    bool any = false;
    std::function<void(const iris::SceneNodePtr &)> walk = [&](const iris::SceneNodePtr &n) {
        if (n->getSceneNodeType() == iris::SceneNodeType::Mesh) {
            auto m = n.staticCast<iris::MeshNode>();
            if (m->getMesh() && !m->defaultFloor) {
                const iris::AABB b = m->getMesh()->getAABB();
                const iris::Mat4 w = n->getGlobalTransform();
                const iris::Vec3 lo = b.getMin(), hi = b.getMax();
                for (int c = 0; c < 8; ++c) {
                    box.merge(w.map(iris::Vec3(c & 1 ? hi.x() : lo.x(), c & 2 ? hi.y() : lo.y(), c & 4 ? hi.z() : lo.z())));
                    any = true;
                }
            }
        }
        for (const iris::SceneNodePtr &c : n->children()) walk(c);
    };
    walk(doc->getRootNode());
    if (any) { mn = box.getMin(); mx = box.getMax(); }
    return any;
}

bool readJlab(const QString &path, Jlab &j, QString &error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { error = QStringLiteral("cannot read %1").arg(path); return false; }
    QJsonParseError pe;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll(), &pe).object();
    if (pe.error != QJsonParseError::NoError) { error = QStringLiteral("%1: %2").arg(path, pe.errorString()); return false; }
    j = Jlab();
    const QString dir = QFileInfo(path).absolutePath();
    if (o.contains("file")) j.file = QDir(dir).absoluteFilePath(o["file"].toString());
    j.env = o["env"].toString();
    if (j.file.isEmpty() && j.env.isEmpty()) { error = QStringLiteral("%1 names no file and no env").arg(path); return false; }
    if (o.contains("camera")) {
        const QJsonObject c = o["camera"].toObject();
        j.hasCamera = true;
        j.camPos = jsonVec3(c["position"]);
        j.camTarget = jsonVec3(c["target"]);
    }
    j.tier = o["tier"].toString(j.tier);
    const QJsonObject v = o["views"].toObject();
    j.view = v["atom"].toString(j.view);
    j.gi = v["gi"].toBool(j.gi);
    j.occlusion = v["occlusion"].toBool(j.occlusion);
    j.draw = v["draw"].toBool(j.draw);
    j.overlay = v["overlay"].toBool(j.overlay);
    if (o.contains("sun")) {
        const QJsonObject s = o["sun"].toObject();
        j.hasSun = true;
        j.sunRot = jsonQuat(s["rotation"]);
        j.sunIntensity = float(s["intensity"].toDouble(1.0));
    }
    return true;
}

bool writeJlab(const QString &path, const Jlab &j, QString &error)
{
    QJsonObject o;
    if (!j.file.isEmpty()) o["file"] = j.file;
    if (!j.env.isEmpty()) o["env"] = j.env;
    if (j.hasCamera) o["camera"] = QJsonObject{ { "position", vec3Json(j.camPos) }, { "target", vec3Json(j.camTarget) } };
    o["tier"] = j.tier;
    o["views"] = QJsonObject{ { "atom", j.view }, { "gi", j.gi }, { "occlusion", j.occlusion }, { "draw", j.draw },
                              { "overlay", j.overlay } };
    if (j.hasSun) o["sun"] = QJsonObject{ { "rotation", quatJson(j.sunRot) }, { "intensity", double(j.sunIntensity) } };
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(o).toJson()) < 0) {
        error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    return true;
}

}   // namespace lab
