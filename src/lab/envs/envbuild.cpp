#include "envbuild.h"

#include "../labscene.h"

#include "irisgl/core/geometry/aabb.h"
#include "irisgl/document/assets/livetextures.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/worldmodes.h"
#include "irisgl/import/importsettings.h"
#include "irisgl/import/meshbake.h"

#include <QColor>
#include <QImage>
#include <QHash>

#include <algorithm>
#include <functional>
#include <chrono>
#include <cmath>

namespace lab {
namespace envs {

namespace {
double nowMs()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void hashFloats(QCryptographicHash &h, const float *f, size_t n)
{
    h.addData(QByteArrayView(reinterpret_cast<const char *>(f), qsizetype(n * sizeof(float))));
}
}   // namespace

// ---- optics ------------------------------------------------------------------------------------------------------

double Optics::pixelScale() const
{
    return double(heightPixels) / (2.0 * std::tan(double(fovYDegrees) * M_PI / 360.0));
}

bool Optics::inFrustum(const iris::Vec3 &c, float r) const
{
    iris::Vec3 f = (target - eye).normalized();
    const iris::Vec3 right = iris::Vec3::crossProduct(f, iris::Vec3(0, 1, 0)).normalized();
    const iris::Vec3 up = iris::Vec3::crossProduct(right, f);
    const iris::Vec3 v = c - eye;
    const float z = iris::Vec3::dotProduct(v, f);
    if (z < -r || z - r > farClip) return false;
    const float ty = std::tan(fovYDegrees * float(M_PI) / 360.0f), tx = ty * aspect;
    const float y = std::fabs(iris::Vec3::dotProduct(v, up)), x = std::fabs(iris::Vec3::dotProduct(v, right));
    return y - r * std::sqrt(1.0f + ty * ty) <= z * ty && x - r * std::sqrt(1.0f + tx * tx) <= z * tx;
}

bool Optics::straddles(const iris::Vec3 &c, float r) const
{
    if (!inFrustum(c, r)) return false;
    iris::Vec3 f = (target - eye).normalized();
    const iris::Vec3 right = iris::Vec3::crossProduct(f, iris::Vec3(0, 1, 0)).normalized();
    const iris::Vec3 up = iris::Vec3::crossProduct(right, f);
    const iris::Vec3 v = c - eye;
    const float z = iris::Vec3::dotProduct(v, f);
    const float ty = std::tan(fovYDegrees * float(M_PI) / 360.0f), tx = ty * aspect;
    const float y = std::fabs(iris::Vec3::dotProduct(v, up)), x = std::fabs(iris::Vec3::dotProduct(v, right));
    const bool inside = z - r >= 0.1f && z + r <= farClip && y + r * std::sqrt(1.0f + ty * ty) <= z * ty &&
                        x + r * std::sqrt(1.0f + tx * tx) <= z * tx;
    return !inside;
}

float largestIslandDiagonal(const Arrays &a)
{
    const size_t nv = a.p.size() / 3;
    std::vector<size_t> parent(nv);
    for (size_t i = 0; i < nv; ++i) parent[i] = i;
    const std::function<size_t(size_t)> root = [&](size_t i) {
        while (parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
        return i;
    };
    const auto join = [&](size_t x, size_t y) { x = root(x); y = root(y); if (x != y) parent[y] = x; };
    QHash<QByteArray, size_t> weld;   // the position's bytes -> the first vertex there
    for (size_t i = 0; i < nv; ++i) {
        const QByteArray key(reinterpret_cast<const char *>(&a.p[i * 3]), 3 * int(sizeof(float)));
        const auto it = weld.constFind(key);
        if (it == weld.constEnd()) weld.insert(key, i);
        else join(it.value(), i);
    }
    for (size_t t = 0; t + 2 < a.idx.size(); t += 3) { join(a.idx[t], a.idx[t + 1]); join(a.idx[t], a.idx[t + 2]); }
    QHash<size_t, iris::AABB> boxes;
    for (size_t i = 0; i < nv; ++i) boxes[root(i)].merge(iris::Vec3(a.p[i * 3], a.p[i * 3 + 1], a.p[i * 3 + 2]));
    float best = 0.0f;
    for (const iris::AABB &b : boxes) best = std::max(best, (b.getMax() - b.getMin()).length());
    return best;
}

// ---- arrays ------------------------------------------------------------------------------------------------------

void Arrays::quad(const iris::Vec3 &a, const iris::Vec3 &b, const iris::Vec3 &c, const iris::Vec3 &d,
                  const iris::Vec3 &nrm)
{
    const unsigned base = unsigned(p.size() / 3);
    const iris::Vec3 v[4] = { a, b, c, d };
    const float uvs[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    for (int i = 0; i < 4; ++i) {
        p.insert(p.end(), { v[i].x(), v[i].y(), v[i].z() });
        n.insert(n.end(), { nrm.x(), nrm.y(), nrm.z() });
        uv.insert(uv.end(), { uvs[i][0], uvs[i][1] });
    }
    // the winding follows `nrm`: flip when a-b-c turns the other way
    const iris::Vec3 g = iris::Vec3::crossProduct(b - a, c - a);
    if (iris::Vec3::dotProduct(g, nrm) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
}

void Arrays::box(const iris::Vec3 &c, const iris::Vec3 &h)
{
    const float x0 = c.x() - h.x(), x1 = c.x() + h.x(), y0 = c.y() - h.y(), y1 = c.y() + h.y(), z0 = c.z() - h.z(),
                z1 = c.z() + h.z();
    using V = iris::Vec3;
    quad(V(x0, y0, z1), V(x1, y0, z1), V(x1, y1, z1), V(x0, y1, z1), V(0, 0, 1));
    quad(V(x1, y0, z0), V(x0, y0, z0), V(x0, y1, z0), V(x1, y1, z0), V(0, 0, -1));
    quad(V(x1, y0, z1), V(x1, y0, z0), V(x1, y1, z0), V(x1, y1, z1), V(1, 0, 0));
    quad(V(x0, y0, z0), V(x0, y0, z1), V(x0, y1, z1), V(x0, y1, z0), V(-1, 0, 0));
    quad(V(x0, y1, z1), V(x1, y1, z1), V(x1, y1, z0), V(x0, y1, z0), V(0, 1, 0));
    quad(V(x0, y0, z0), V(x1, y0, z0), V(x1, y0, z1), V(x0, y0, z1), V(0, -1, 0));
}

void Arrays::orientedBox(const iris::Vec3 &c, const iris::Vec3 &u, const iris::Vec3 &v, const iris::Vec3 &w,
                         const iris::Vec3 &h)
{
    const auto P = [&](float a, float b, float d) { return c + u * (a * h.x()) + v * (b * h.y()) + w * (d * h.z()); };
    quad(P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), w);
    quad(P(1, -1, -1), P(-1, -1, -1), P(-1, 1, -1), P(1, 1, -1), w * -1.0f);
    quad(P(1, -1, 1), P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), u);
    quad(P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), P(-1, 1, -1), u * -1.0f);
    quad(P(-1, 1, 1), P(1, 1, 1), P(1, 1, -1), P(-1, 1, -1), v);
    quad(P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1), v * -1.0f);
}

void cardSoup(Arrays &a, unsigned cards, float half, float radius, const iris::Vec3 &centre, unsigned seedIn)
{
    unsigned seed = seedIn;
    const auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1u << 24); };
    for (unsigned c = 0; c < cards; ++c) {
        float cx, cy, cz;
        do { cx = rnd() * 2.0f - 1.0f; cy = rnd() * 2.0f - 1.0f; cz = rnd() * 2.0f - 1.0f; }
        while (cx * cx + cy * cy + cz * cz > 1.0f);
        cx = centre.x() + cx * radius; cy = centre.y() + cy * radius; cz = centre.z() + cz * radius;
        float nx = rnd() * 2.0f - 1.0f, ny = rnd() * 2.0f - 1.0f, nz = rnd() * 2.0f - 1.0f;
        float nl = std::sqrt(nx * nx + ny * ny + nz * nz); if (nl < 1e-3f) { nx = 0; ny = 1; nz = 0; nl = 1; }
        nx /= nl; ny /= nl; nz /= nl;
        float ax = std::fabs(nx) < 0.9f ? 1.0f : 0.0f, ay = ax > 0.5f ? 0.0f : 1.0f, az = 0.0f;
        float ux = ay * nz - az * ny, uy = az * nx - ax * nz, uz = ax * ny - ay * nx;
        const float ul = std::sqrt(ux * ux + uy * uy + uz * uz); ux /= ul; uy /= ul; uz /= ul;
        const float vx = ny * uz - nz * uy, vy = nz * ux - nx * uz, vz = nx * uy - ny * ux;
        const float h = half;
        const unsigned b = unsigned(a.p.size() / 3);
        const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (const auto &k : corners) {
            a.p.insert(a.p.end(), { cx + h * (k[0] * ux + k[1] * vx), cy + h * (k[0] * uy + k[1] * vy), cz + h * (k[0] * uz + k[1] * vz) });
            a.n.insert(a.n.end(), { nx, ny, nz });
            a.uv.insert(a.uv.end(), { k[0] * 0.5f + 0.5f, k[1] * 0.5f + 0.5f });
        }
        a.idx.insert(a.idx.end(), { b, b + 1, b + 2, b, b + 2, b + 3 });
    }
}

iris::Vec3 place(const iris::Vec3 &l, const iris::Vec3 &pos, float yawDegrees, const iris::Vec3 &s)
{
    const float t = yawDegrees * float(M_PI) / 180.0f, c = std::cos(t), sn = std::sin(t);
    const float x = l.x() * s.x(), y = l.y() * s.y(), z = l.z() * s.z();
    return iris::Vec3(pos.x() + x * c + z * sn, pos.y() + y, pos.z() - x * sn + z * c);
}

void boxBounds(const iris::Vec3 &centre, const iris::Vec3 &half, const iris::Vec3 &pos, float yaw, const iris::Vec3 &s,
               iris::Vec3 &mn, iris::Vec3 &mx)
{
    iris::AABB b;
    for (int k = 0; k < 8; ++k)
        b.merge(place(iris::Vec3(centre.x() + (k & 1 ? half.x() : -half.x()), centre.y() + (k & 2 ? half.y() : -half.y()),
                                 centre.z() + (k & 4 ? half.z() : -half.z())),
                      pos, yaw, s));
    mn = b.getMin();
    mx = b.getMax();
}

// ---- the builder -------------------------------------------------------------------------------------------------

Builder::Builder(const char *env, unsigned seed, bool bake)
    : mHash(QCryptographicHash::Sha256), mBakeHash(QCryptographicHash::Sha256), mBake(bake)
{
    mStartMs = nowMs();
    mBuilt.doc = iris::Scene::create();
    mHash.addData(QByteArray(env));
    mHash.addData(QByteArray::number(seed));
}

iris::MeshPtr Builder::mesh(const Arrays &a)
{
    hashFloats(mHash, a.p.data(), a.p.size());
    hashFloats(mHash, a.n.data(), a.n.size());
    hashFloats(mHash, a.uv.data(), a.uv.size());
    mHash.addData(QByteArrayView(reinterpret_cast<const char *>(a.idx.data()), qsizetype(a.idx.size() * sizeof(unsigned))));
    iris::MeshPtr m = iris::Mesh::create();
    const auto add = [&](iris::VertexAttribUsage usage, const std::vector<float> &v, int comps) {
        iris::VertexLayout layout;
        layout.addAttrib(usage, iris::AttribTypeFloat, comps, comps * int(sizeof(float)));
        auto vb = iris::VertexBuffer::create(layout);
        vb->setData(const_cast<float *>(v.data()), unsigned(v.size() * sizeof(float)));
        m->addVertexBuffer(vb);
    };
    add(iris::VertexAttribUsage::Position, a.p, 3);
    add(iris::VertexAttribUsage::Normal, a.n, 3);
    add(iris::VertexAttribUsage::TexCoord0, a.uv, 2);
    auto ib = iris::IndexBuffer::create();
    ib->setData(const_cast<unsigned *>(a.idx.data()), unsigned(a.idx.size() * sizeof(unsigned)));
    m->setIndexBuffer(ib);
    m->usesIndexBuffer = true;
    m->numVerts = int(a.p.size() / 3);
    m->numFaces = int(a.idx.size() / 3);
    m->primitiveMode = iris::PrimitiveMode::Triangles;
    iris::AABB box;
    for (size_t i = 0; i + 2 < a.p.size(); i += 3) box.merge(iris::Vec3(a.p[i], a.p[i + 1], a.p[i + 2]));
    m->aabb = box;
    m->boundingSphere = box.getMinimalEnclosingSphere();
    ++mBuilt.meshes;
    if (mBake) {
        // THE BAKE'S STAGES, meshbake.cpp bakeStages' calls in its order (the cluster DAG, its level table, the cards at
        // the import default) — the same product an imported mesh carries
        const double t0 = nowMs();
        iris::MeshBake::buildClusterDag(m);
        iris::MeshBake::buildLevelTable(m);
        iris::MeshBake::buildCards(m, iris::kDefaultMaxCards);
        mBuilt.bakeMs += nowMs() - t0;
        // THE BAKE'S PRODUCTS, hashed: the level table and the DAG (its clusters and groups are padding-free PODs)
        for (const QVector<quint32> &level : m->lodIndices)
            mBakeHash.addData(QByteArrayView(reinterpret_cast<const char *>(level.constData()), level.size() * qsizetype(sizeof(quint32))));
        const iris::MeshClusterDag &dag = m->clusterDag;
        mBakeHash.addData(QByteArrayView(reinterpret_cast<const char *>(dag.clusters.constData()),
                                         dag.clusters.size() * qsizetype(sizeof(iris::MeshClusterDag::Cluster))));
        mBakeHash.addData(QByteArrayView(reinterpret_cast<const char *>(dag.groups.constData()),
                                         dag.groups.size() * qsizetype(sizeof(iris::MeshClusterDag::Group))));
        mBakeHash.addData(QByteArrayView(reinterpret_cast<const char *>(dag.vertices.constData()),
                                         dag.vertices.size() * qsizetype(sizeof(quint32))));
        mBakeHash.addData(dag.triangles);
    }
    return m;
}

iris::PbrMaterialPtr Builder::material(float r, float g, float b, float roughness, float metallic)
{
    auto mat = iris::PbrMaterial::create();
    mat->setValue("baseColor", QColor::fromRgbF(r, g, b));
    mat->setValue("roughness", roughness);
    mat->setValue("metallic", metallic);
    const float v[4] = { r, g, b, roughness };
    hashFloats(mHash, v, 4);
    if (metallic != 0.0f) hashFloats(mHash, &metallic, 1);   // a dielectric hashes as it always did
    mMatFacts.push_back({ mat.data(), MatFacts{ roughness, metallic } });
    return mat;
}

iris::PbrMaterialPtr Builder::emitter(float intensity)
{
    iris::PbrMaterialPtr mat = material(1.0f, 1.0f, 1.0f, 0.9f, 0.0f);
    mat->setValue("emissiveColor", QColor::fromRgbF(1.0, 1.0, 1.0));
    mat->setValue("emissiveIntensity", intensity);
    mHash.addData(QByteArray("emitter"));
    hashFloats(mHash, &intensity, 1);
    return mat;
}

iris::PbrMaterialPtr Builder::pureEmitter(float intensity)
{
    iris::PbrMaterialPtr mat = material(0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    mat->setSpecularColor(QColor::fromRgbF(0.0, 0.0, 0.0));
    mat->setValue("emissiveColor", QColor::fromRgbF(1.0, 1.0, 1.0));
    mat->setValue("emissiveIntensity", intensity);
    mHash.addData(QByteArray("pure-emitter"));
    hashFloats(mHash, &intensity, 1);
    return mat;
}

iris::PbrMaterialPtr Builder::cutout(float r, float g, float b, float roughness, int cells)
{
    iris::PbrMaterialPtr mat = material(r, g, b, roughness, 0.0f);
    // THE LEAF MASK: one session texture per cell count (the pixels are a function of it alone).
    const int res = 512;
    const QString guid = QStringLiteral("jahlab-leaf-mask-%1").arg(cells);
    iris::Texture2DPtr tex = iris::LiveTextures::find(guid);
    if (!tex) {
        tex = iris::LiveTextures::create(guid, QStringLiteral("leaf mask"), res, res, true);
        if (tex) {
            QImage img(res, res, QImage::Format_RGBA8888);
            img.fill(QColor(255, 255, 255, 0));
            Lcg rng(0x1eafu + unsigned(cells));
            const float cell = float(res) / float(cells);
            for (int cy = 0; cy < cells; ++cy)
                for (int cx = 0; cx < cells; ++cx) {
                    const float ox = (cx + rng.range(0.35f, 0.65f)) * cell, oy = (cy + rng.range(0.35f, 0.65f)) * cell;
                    const float ax = rng.range(0.30f, 0.48f) * cell, ay = rng.range(0.18f, 0.32f) * cell;
                    const float tilt = rng.range(0.0f, 3.14159265f), cs = std::cos(tilt), sn = std::sin(tilt);
                    const int x0 = int(ox - cell), x1 = int(ox + cell), y0 = int(oy - cell), y1 = int(oy + cell);
                    for (int y = std::max(0, y0); y <= std::min(res - 1, y1); ++y)
                        for (int x = std::max(0, x0); x <= std::min(res - 1, x1); ++x) {
                            const float dx = x + 0.5f - ox, dy = y + 0.5f - oy;
                            const float u = (dx * cs + dy * sn) / ax, v = (-dx * sn + dy * cs) / ay;
                            if (u * u + v * v <= 1.0f) img.setPixelColor(x, y, QColor(255, 255, 255, 255));
                        }
                }
            tex->writeLive(img);
        }
    }
    if (tex) mat->setBaseColorMap(tex);
    mat->setAlphaMode(1);
    mat->setAlphaCutoff(0.5f);
    for (auto &f : mMatFacts)
        if (f.first == mat.data()) f.second.cutout = true;
    mHash.addData(QByteArray("cutout"));
    mHash.addData(QByteArray::number(cells));
    return mat;
}

void Builder::setMovable(int index)
{
    if (index < 0 || size_t(index) >= mNodes.size()) return;
    mNodes[size_t(index)]->setMobility(iris::Mobility::Movable);
    mBuilt.objects[size_t(index)].mover = true;
    mHash.addData(QByteArray("movable"));
}

int Builder::instance(const iris::MeshPtr &m, const QString &name, const QString &role, const iris::Vec3 &pos,
                      float yaw, const iris::Vec3 &scale, const iris::PbrMaterialPtr &mat, bool castsShadow,
                      bool twoSided)
{
    auto node = iris::MeshNode::create();
    node->setName(name);
    node->setMesh(m);
    node->setLocalPos(pos);
    node->setLocalRot(iris::Quat::fromEulerAngles(0.0f, yaw, 0.0f));
    node->setLocalScale(scale);
    node->setMaterial(mat);
    node->setShadowCastingEnabled(castsShadow);
    if (twoSided) node->setFaceCullingMode(iris::FaceCullingMode::None);
    mBuilt.doc->getRootNode()->addChild(node);
    const float v[7] = { pos.x(), pos.y(), pos.z(), yaw, scale.x(), scale.y(), scale.z() };
    hashFloats(mHash, v, 7);
    mHash.addData(name.toUtf8());
    Object o;
    o.name = name;
    o.role = role;
    for (const auto &f : mMatFacts)
        if (f.first == mat.data()) {
            o.roughness = f.second.roughness;
            o.metallic = f.second.metallic;
            o.cutout = f.second.cutout;
        }
    o.twoSided = twoSided;
    mNodes.push_back(node);
    const iris::AABB b = m->getAABB();
    const iris::Vec3 c = (b.getMin() + b.getMax()) * 0.5f, h = (b.getMax() - b.getMin()) * 0.5f;
    boxBounds(c, h, pos, yaw, scale, o.boxMin, o.boxMax);
    mBuilt.objects.push_back(o);
    mBuilt.triangles += (unsigned long long)std::max(0, m->numFaces);
    ++mBuilt.instances;
    return int(mBuilt.objects.size()) - 1;
}

Built Builder::finish()
{
    worldmodes::setMode(mBuilt.doc, worldmodes::Mode::Epic);   // a new scene's tier (POST_CHAIN_SPEC §12 decision 8)
    addSkyAndLights(mBuilt.doc, true);
    mBuilt.doc->getRootNode()->applyStaticDefaults();
    mBuilt.hash = mHash.result().toHex();
    if (mBake) mBuilt.bakeHash = mBakeHash.result().toHex();
    mBuilt.buildMs = nowMs() - mStartMs;
    return mBuilt;
}

// ---- shared fixture pieces ---------------------------------------------------------------------------------------

int fixedBox(Builder &b, const iris::MeshPtr &unit, const QString &name, const iris::Vec3 &mn, const iris::Vec3 &mx,
             const iris::PbrMaterialPtr &mat)
{
    const iris::Vec3 size = mx - mn;
    const iris::Vec3 pos((mn.x() + mx.x()) * 0.5f, mn.y(), (mn.z() + mx.z()) * 0.5f);
    const int o = b.instance(unit, name, QStringLiteral("subject"), pos, 0.0f, size, mat);
    Feature f;
    f.kind = Feature::Fixed;
    f.object = o;
    f.centre = (mn + mx) * 0.5f;
    f.radius = 0.5f * size.length();
    f.triangles = 12;
    b.feature(f);
    return o;
}

iris::MeshPtr unitBox(Builder &b)
{
    Arrays a;
    a.box(iris::Vec3(0, 0.5f, 0), iris::Vec3(0.5f, 0.5f, 0.5f));
    return b.mesh(a);
}

// ---- the closed forms --------------------------------------------------------------------------------------------

double predictedTriangles(const Built &b, const Optics &o, const QString &role, const QSet<int> &excludedObjects)
{
    double total = 0.0;
    for (const Feature &f : b.features) {
        if (!role.isEmpty() && (f.object < 0 || b.objects[size_t(f.object)].role != role)) continue;
        if (excludedObjects.contains(f.object)) continue;
        switch (f.kind) {
        case Feature::Fixed:
            if (o.inFrustum(f.centre, f.radius)) total += f.triangles;
            break;
        case Feature::Shell: {
            if (!o.inFrustum(f.centre, f.radius)) break;
            const double d = std::max(0.1, double((f.centre - o.eye).length() - f.radius));
            if (double(f.error) >= o.allowed(d)) total += f.triangles;
            break;
        }
        case Feature::CurvedFace: {
            // THE CHORD RULE: an arc of radius R approximated within the sagitta e needs one chord per
            // 2 acos(1 - e / R) radians — so a face's segments go as 1 / sqrt(e), e the error the view affords at each
            // authored chord's own distance; never more than authored, never fewer than one while any is in view
            const float dth = f.theta / float(f.segments);
            double need = 0.0;
            bool any = false;
            for (unsigned i = 0; i < f.segments; ++i) {
                const float a = f.angle0 + (float(i) + 0.5f) * dth;
                const iris::Vec3 p = f.arcCentre + iris::Vec3(std::cos(a) * f.arcRadius, f.faceY, std::sin(a) * f.arcRadius);
                if (!o.inFrustum(p, f.radius)) continue;
                const double d = std::max(0.1, double((p - o.eye).length() - f.radius));
                const double e = o.allowed(d);
                // a face of a PROTRUDING band (error > 0: its depth off the surface behind it) goes with the band where
                // the depth is under the view's error — a moulding's whole chord drops, not only its curvature
                if (f.error > 0.0f && double(f.error) < e) continue;
                any = true;
                const double half = std::acos(std::max(-1.0, 1.0 - e / double(f.arcRadius)));
                need += std::min(1.0, double(dth) / (2.0 * std::max(1e-9, half)));
            }
            if (any) total += 2.0 * std::max(1.0, std::ceil(need - 1e-9));
            break;
        }
        case Feature::Canopy: {
            // THE REPRESENTED-SURFACE RULE IN A DENSE SOUP: a removed card is charged its distance to ANY remaining
            // surface — in a dense canopy the gap to its nearest neighbour (centimetres), not its own size — so the
            // canopy draws 2 triangles per card whose nearest-neighbour gap (the centre-to-centre distance, an upper
            // bound of the surface gap) is >= the view's error at the canopy: monotone in D, never a step
            if (!o.inFrustum(f.centre, f.radius) || !f.gaps) break;
            const double d = std::max(0.1, double((f.centre - o.eye).length() - f.radius));
            const float e = float(o.allowed(d));
            const auto first = std::lower_bound(f.gaps->begin(), f.gaps->end(), e);
            total += 2.0 * double(f.gaps->end() - first);
            break;
        }
        }
    }
    return total;
}

double measured(const Built &b, const Prediction &p, const std::vector<ObjectReading> &readings)
{
    const std::string &q = p.quantity;
    QHash<QString, QString> roleOf;
    for (const Object &o : b.objects) roleOf.insert(o.name, o.role);
    double v = 0.0;
    for (const ObjectReading &r : readings) {
        const auto it = roleOf.constFind(r.name);
        if (it == roleOf.constEnd()) continue;   // not the environment's (nothing else draws, but say so by name)
        if (p.excluded.contains(r.name)) continue;
        const QString &role = it.value();
        if (q == "triangles") v += double(r.drawnTriangles);
        else if (q == "objects" && role == "prop" && r.drawnTriangles > 0) v += 1.0;
        else if (q == "visibleObjects" && role == "building" && r.pixels > 0) v += 1.0;
        else if (q == "hiddenDrawn" && role == "building" && r.drawnTriangles > 0 && r.pixels == 0) v += 1.0;
    }
    return v;
}

const std::vector<double> &stops()
{
    static const std::vector<double> s = { 22.0, 88.0, 177.0, 354.0 };
    return s;
}

const Env *find(const std::string &name)
{
    for (const Env &e : catalogue())
        if (name == e.name) return &e;
    return nullptr;
}

}   // namespace envs
}   // namespace lab
