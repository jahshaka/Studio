// shadergraph.emitter_parity — THE DIFFERENTIAL ORACLE (HLMS_ADOPTION P5 §7.9).
//
// The shader graph now has TWO backends over one compiler: the CPU baker
// (BakeProgram::evaluate) and the GLSL emitter (PieceEmitter). This suite is
// the contract between them: for every fixture, the value the CPU computes and
// the surface the GPU renders from the emitted piece must be the same picture.
//
// HOW IT COMPARES, and why this shape rather than "read the pixel and undo the
// lighting": a piece that lands Base Color writes exactly what an ordinary PBR
// material with that albedo would have written (the landing reproduces the
// template's own metallic-workflow lines). So the reference is a REAL material
// with albedo = the CPU value, rendered by the same scene, and the comparison
// is pixel against pixel. Lighting, tonemapping, gamma and quantization are
// then identical on both sides by construction — they cancel instead of having
// to be modelled — and the test also catches a wrong LANDING, not just wrong
// arithmetic.
//
// THE `+ time*0` WRAPPER on every fixture is deliberate. The emitter refuses a
// graph whose sockets all fold to constants (the baker already lands those
// exactly, and a piece would buy a shader permutation and nothing else), so a
// fixture that tested `add(0.2, 0.3)` would test the refusal, not the addition.
// Adding `time * 0` makes the chain animated — accepted — and changes no value
// on either side, because both sides evaluate the SAME wrapped program.
//
// THE CONTROL at the end proves the comparison can fail: the same fixture
// rendered against a reference that is 0.05 off must be seen as different.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <QApplication>
#include <QDir>
#include <QJsonObject>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "modules/materials/core/bakeprogram.h"
#include "modules/materials/core/graphbaker.h"
#include "modules/materials/core/pieceemitter.h"
#include "modules/materials/graph/nodegraph.h"
#include "modules/materials/models/libraryv1.h"
#include "modules/materials/models/nodemodel.h"
#include "modules/materials/nodes/pbrmasternode.h"

using namespace jahshaka::engine;

namespace {

int gFailures = 0;
int gChecks = 0;
QStringList gCoveredOps;

#define CHECK_MSG(cond, ...)                                                     \
    do {                                                                         \
        ++gChecks;                                                               \
        if (!(cond)) {                                                           \
            ++gFailures;                                                         \
            std::printf("    FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond);     \
            std::printf(__VA_ARGS__);                                            \
            std::printf("\n");                                                   \
        }                                                                        \
    } while (0)

std::unique_ptr<Engine> gEngine;
QString gDir;

EngineConfig testConfig() {
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_emitter_parity-ogre.log";
    return cfg;
}

// ------------------------------------------------------------------- graphs

/// One fixture graph: a PBR master plus whatever the fixture wires into it.
struct Rig
{
    NodeGraph *graph;
    NodeModel *master;
    LibraryV1 *lib;

    Rig()
    {
        lib = new LibraryV1();
        graph = new NodeGraph();
        graph->setNodeLibrary(lib);
        master = new PbrMasterNode();
        graph->addNode(master);
        graph->setMasterNode(master);
    }
    NodeModel *add(const QString &type)
    {
        auto node = lib->createNode(type);
        graph->addNode(node);
        return node;
    }
    NodeModel *addFloat(double v)
    {
        auto node = add("float");
        node->deserializeWidgetValue(QJsonValue(v));
        return node;
    }
    NodeModel *addVec(int n, double x, double y, double z = 0, double w = 0)
    {
        auto node = add(QStringLiteral("vector%1").arg(n));
        QJsonObject obj;
        obj["x"] = x; obj["y"] = y; obj["z"] = z; obj["w"] = w;
        node->deserializeWidgetValue(obj);
        return node;
    }
    NodeModel *addColor(double r, double g, double b, double a = 1.0)
    {
        auto node = add("color");
        QJsonObject obj;
        obj["r"] = r; obj["g"] = g; obj["b"] = b; obj["a"] = a;
        node->deserializeWidgetValue(obj);
        return node;
    }
    void connect(NodeModel *from, int out, NodeModel *to, int in)
    {
        graph->addConnection(from, out, to, in);
    }
    /// Wires `from`'s output `out` into Base Color THROUGH the animated no-op
    /// wrapper described in the file header.
    void toBaseColor(NodeModel *from, int out = 0)
    {
        auto zero = add("multiply");
        graph->addConnection(add("time"), 0, zero, 0);
        graph->addConnection(addFloat(0.0), 0, zero, 1);
        auto sum = add("add");
        graph->addConnection(from, out, sum, 0);
        graph->addConnection(zero, 0, sum, 1);
        graph->addConnection(sum, 0, master, 0);
    }
};

// ------------------------------------------------------------------- render

struct Px { int r, g, b; };
Px centre(const Image &img)
{
    const Colour c = img.at(img.width / 2, img.height / 2);
    return { int(std::lround(c.r * 255)), int(std::lround(c.g * 255)), int(std::lround(c.b * 255)) };
}

/// A scene with one lit cube whose material the test drives.
struct Probe
{
    Scene *scene = nullptr;
    View *view = nullptr;
    MaterialId material = 0;
    NodeId node = 0;
};

Probe makeProbe(Engine *e, const std::string &name)
{
    Probe p;
    p.view = e->createOffscreenView(name, 48, 48, Colour(0.0f, 0.0f, 0.0f));
    p.scene = e->createScene(name);
    if (!p.view || !p.scene) return p;
    p.scene->setAmbient(Colour(0.25f, 0.25f, 0.25f), Colour(0.2f, 0.2f, 0.2f));
    enginetest::addDirectionalLight(p.scene, Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
    const NodeId node = p.scene->createNode();
    p.node = node;
    const MeshId mesh = p.scene->createMesh(enginetest::unitCubeMesh());
    PbrParams params;
    params.albedo = Colour(0.5f, 0.5f, 0.5f);
    params.metalness = 0.0f;
    params.roughness = 0.6f;
    p.material = p.scene->createPbrMaterial(params);
    p.scene->attachMesh(node, mesh, p.material);
    enginetest::setNodeScale(p.scene, node, Vec3(1.4f, 1.4f, 1.4f));
    p.view->setScene(p.scene);
    enginetest::testCameraLookAt(p.view, Vec3(2.2f, 1.8f, 2.6f), Vec3(0.0f, 0.0f, 0.0f));
    return p;
}

void render(Engine *e, int frames = 2) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

Probe gEmitted, gReference;

// ---------------------------------------------------------------- the oracle

/// Runs one fixture end to end. `ops` names the op keys the fixture exercises,
/// for the coverage report.
void oracle(const QString &name, const QStringList &ops, const std::function<void(Rig &)> &build,
            double time = 0.4)
{
    Rig rig;
    build(rig);

    const auto compiled = materials::GraphBaker::compile(rig.graph);
    const auto emitted = materials::PieceEmitter::lower(compiled);
    if (!emitted.accepted) {
        ++gChecks; ++gFailures;
        QString why = emitted.fallbackReasons.value(QString());
        if (why.isEmpty() && !emitted.fallbackReasons.isEmpty())
            why = emitted.fallbackReasons.constBegin().value();
        std::printf("    FAIL %s: the emitter refused the fixture — %s\n",
                    qPrintable(name), qPrintable(why));
        return;
    }
    const QString path = materials::PieceEmitter::write(gDir, emitted.pixelSource, false);
    if (path.isEmpty()) {
        ++gChecks; ++gFailures;
        std::printf("    FAIL %s: could not write the piece\n", qPrintable(name));
        return;
    }

    // The CPU side: the SAME compiled program, at the same context the shader
    // sees (no UVs on this mesh, so the emitted piece reads uv (0,0) too).
    const materials::BakeProgram *program = nullptr;
    for (const auto &slot : compiled.sockets)
        if (slot.slot.socketName == QLatin1String("Base Color") && slot.connected)
            program = &slot.program;
    if (!program) {
        ++gChecks; ++gFailures;
        std::printf("    FAIL %s: the fixture did not reach Base Color\n", qPrintable(name));
        return;
    }
    materials::EvalContext ctx;
    ctx.u = 0.0; ctx.v = 0.0; ctx.time = time;
    const materials::Value cpu = program->evaluate(ctx).coerced(3);

    // GPU A: the emitted piece.
    gEmitted.scene->setMaterialCustomPiece(gEmitted.material, path.toStdString(),
                                           CustomPieceStage::PixelPreLights);
    gEmitted.scene->setShaderTime(float(time));
    // GPU B: an ordinary material whose albedo IS the CPU value.
    PbrParams ref;
    ref.albedo = Colour(float(cpu.x), float(cpu.y), float(cpu.z));
    ref.metalness = 0.0f;
    ref.roughness = 0.6f;
    gReference.scene->setPbrMaterial(gReference.material, ref);

    render(gEngine.get());
    Image a, b;
    if (!gEmitted.view->readPixels(a) || !gReference.view->readPixels(b)) {
        ++gChecks; ++gFailures;
        std::printf("    FAIL %s: readback failed\n", qPrintable(name));
        return;
    }
    const Px pa = centre(a), pb = centre(b);
    const int dr = std::abs(pa.r - pb.r), dg = std::abs(pa.g - pb.g), db = std::abs(pa.b - pb.b);
    const int worst = std::max(dr, std::max(dg, db));
    ++gChecks;
    if (worst > 2) {
        ++gFailures;
        std::printf("    FAIL %-22s cpu(%.4f %.4f %.4f) gpu %3d %3d %3d vs ref %3d %3d %3d "
                    "(worst %d/255)\n",
                    qPrintable(name), cpu.x, cpu.y, cpu.z, pa.r, pa.g, pa.b, pb.r, pb.g, pb.b, worst);
    } else {
        std::printf("    ok   %-22s cpu(%.4f %.4f %.4f) -> %3d %3d %3d  (worst %d/255)\n",
                    qPrintable(name), cpu.x, cpu.y, cpu.z, pa.r, pa.g, pa.b, worst);
    }
    for (const QString &op : ops)
        if (!gCoveredOps.contains(op)) gCoveredOps << op;

    gEmitted.scene->setMaterialCustomPiece(gEmitted.material, "", CustomPieceStage::PixelPreLights);
    delete rig.graph;
}

// ============================================================================
// THE PER-OP DIVERGENCE RULE (TORNADO-1, G3 — the lead's decision). fresnel,
// worldNormal and localNormal have no meaningful CPU-bake value: the baker
// evaluates them against an IDENTITY tangent frame (normal (0,0,1), view
// (0,0,1)), which is not any real surface. In a LIVE graph the piece computes
// the real value, so these rows compare the GPU against an ANALYTIC reference
// computed here from the fixture's own geometry — never against the baker.
// ============================================================================

struct V3 { double x, y, z; };
V3 sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3 add3(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V3 mul(V3 a, double k) { return { a.x * k, a.y * k, a.z * k }; }
double dot3(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
V3 norm(V3 a) { const double l = std::sqrt(dot3(a, a)); return { a.x / l, a.y / l, a.z / l }; }

/// The world ray through the centre() pixel of a 48x48 testCameraLookAt view
/// (vertical fov 45, square target), and where it meets the probe's cube
/// (half-extent 0.5 * 1.4, axis-aligned, at the origin): the hit point and the
/// face's outward normal.
struct Hit { V3 point, normal; bool ok = false; };
Hit centreHit(V3 eye)
{
    const V3 f = norm(sub({ 0, 0, 0 }, eye));
    const V3 r = norm(cross(f, { 0, 1, 0 }));
    const V3 u = cross(r, f);
    const double t = std::tan(45.0 * 0.5 * 3.14159265358979323846 / 180.0);
    const double px = 48 / 2, py = 48 / 2;
    const double nx = ((px + 0.5) / 48.0) * 2.0 - 1.0;
    const double ny = 1.0 - ((py + 0.5) / 48.0) * 2.0;
    const V3 d = norm(add3(f, add3(mul(r, nx * t), mul(u, ny * t))));
    Hit best;
    double bestT = 1e30;
    const double h = 0.7;
    const double eyeC[3] = { eye.x, eye.y, eye.z }, dC[3] = { d.x, d.y, d.z };
    for (int axis = 0; axis < 3; ++axis) {
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            if (std::abs(dC[axis]) < 1e-12) continue;
            const double tt = (sgn * h - eyeC[axis]) / dC[axis];
            if (tt <= 0 || tt >= bestT) continue;
            const V3 p = add3(eye, mul(d, tt));
            const double pc[3] = { p.x, p.y, p.z };
            bool inside = true;
            for (int k = 0; k < 3; ++k)
                if (k != axis && std::abs(pc[k]) > h) inside = false;
            if (!inside) continue;
            bestT = tt;
            best.point = p;
            best.normal = { axis == 0 ? double(sgn) : 0.0, axis == 1 ? double(sgn) : 0.0,
                            axis == 2 ? double(sgn) : 0.0 };
            best.ok = true;
        }
    }
    return best;
}

/// The Base Color oracle with an ANALYTIC reference at a chosen eye.
void oracleAnalytic(const QString &name, const QStringList &ops, const std::function<void(Rig &)> &build,
                    V3 eye, const std::function<V3(const Hit &, V3 eye)> &expected)
{
    const Vec3 eyeF(float(eye.x), float(eye.y), float(eye.z));
    enginetest::testCameraLookAt(gEmitted.view, eyeF, Vec3(0.0f, 0.0f, 0.0f));
    enginetest::testCameraLookAt(gReference.view, eyeF, Vec3(0.0f, 0.0f, 0.0f));
    const Hit hit = centreHit(eye);
    Rig rig;
    build(rig);
    const auto compiled = materials::GraphBaker::compile(rig.graph);
    const auto emitted = materials::PieceEmitter::lower(compiled);
    ++gChecks;
    if (!hit.ok || !emitted.accepted || !emitted.emittedSockets.contains(QStringLiteral("Base Color"))) {
        ++gFailures;
        std::printf("    FAIL %s: %s\n", qPrintable(name),
                    !hit.ok ? "the centre ray misses the cube"
                            : qPrintable(emitted.fallbackReasons.value(QStringLiteral("Base Color"))));
    } else {
        const QString path = materials::PieceEmitter::write(gDir, emitted.pixelSource, false);
        gEmitted.scene->setMaterialCustomPiece(gEmitted.material, path.toStdString(),
                                               CustomPieceStage::PixelPreLights);
        gEmitted.scene->setShaderTime(0.4f);
        const V3 want = expected(hit, eye);
        PbrParams ref;
        ref.albedo = Colour(float(want.x), float(want.y), float(want.z));
        ref.metalness = 0.0f;
        ref.roughness = 0.6f;
        gReference.scene->setPbrMaterial(gReference.material, ref);
        render(gEngine.get());
        Image a, b;
        gEmitted.view->readPixels(a);
        gReference.view->readPixels(b);
        const Px pa = centre(a), pb = centre(b);
        const int worst = std::max(std::abs(pa.r - pb.r), std::max(std::abs(pa.g - pb.g), std::abs(pa.b - pb.b)));
        if (worst > 2) {
            ++gFailures;
            std::printf("    FAIL %-22s analytic(%.4f %.4f %.4f) gpu %3d %3d %3d vs ref %3d %3d %3d (worst %d/255)\n",
                        qPrintable(name), want.x, want.y, want.z, pa.r, pa.g, pa.b, pb.r, pb.g, pb.b, worst);
        } else {
            std::printf("    ok   %-22s analytic(%.4f %.4f %.4f) -> %3d %3d %3d  (worst %d/255)\n",
                        qPrintable(name), want.x, want.y, want.z, pa.r, pa.g, pa.b, worst);
        }
        // THE CONTROL the rule owes: the baker's identity-context value would
        // have drawn something else here, or the row proves nothing.
        const auto *program = &compiled.sockets[0].program;
        materials::EvalContext ctx; ctx.time = 0.4;
        const auto cpu = program->evaluate(ctx).coerced(3);
        const double gap = std::max(std::abs(cpu.x - want.x), std::max(std::abs(cpu.y - want.y), std::abs(cpu.z - want.z)));
        ++gChecks;
        if (gap < 0.05) {
            ++gFailures;
            std::printf("    FAIL %s: the analytic value equals the baker's (%.3f) — the fixture cannot "
                        "tell the real value from the approximation\n", qPrintable(name), gap);
        }
        for (const QString &op : ops)
            if (!gCoveredOps.contains(op)) gCoveredOps << op;
        gEmitted.scene->setMaterialCustomPiece(gEmitted.material, "", CustomPieceStage::PixelPreLights);
    }
    delete rig.graph;
    enginetest::testCameraLookAt(gEmitted.view, Vec3(2.2f, 1.8f, 2.6f), Vec3(0.0f, 0.0f, 0.0f));
    enginetest::testCameraLookAt(gReference.view, Vec3(2.2f, 1.8f, 2.6f), Vec3(0.0f, 0.0f, 0.0f));
}

/// Rotates v by the unit quaternion q.
V3 rotate(const Quat &q, V3 v)
{
    const V3 qv{ q.x, q.y, q.z };
    const V3 t = mul(cross(qv, v), 2.0);
    return add3(add3(v, mul(t, q.w)), cross(qv, t));
}

/// THE VERTEX-STAGE NORMALS. A Vertex Offset of `normal * k` moves each face of
/// a ROTATED cube along that normal. The reference is the displacement done by
/// hand, on the CPU, in the mesh's own space — a second cube whose vertices are
/// already where the piece must put them — and the WHOLE image is compared
/// (a flat face's shading does not move, its silhouette does). The control is
/// the other normal's displacement, which must NOT match.
void vertexNormalOracle(const QString &name, bool local)
{
    const double k = 0.15, scale = 1.4;
    // R = Ry(30) * Rx(20)
    const double a = 30.0 * 3.14159265358979323846 / 360.0, b = 20.0 * 3.14159265358979323846 / 360.0;
    const Quat ry(0.0f, float(std::sin(a)), 0.0f, float(std::cos(a)));
    const Quat rx(float(std::sin(b)), 0.0f, 0.0f, float(std::cos(b)));
    const Quat q(ry.w * rx.x + ry.x * rx.w + ry.y * rx.z - ry.z * rx.y,
                 ry.w * rx.y - ry.x * rx.z + ry.y * rx.w + ry.z * rx.x,
                 ry.w * rx.z + ry.x * rx.y - ry.y * rx.x + ry.z * rx.w,
                 ry.w * rx.w - ry.x * rx.x - ry.y * rx.y - ry.z * rx.z);
    const Quat qi(-q.x, -q.y, -q.z, q.w);

    auto build = [&](Probe &p, const MeshData &mesh) {
        static int serial = 0;
        const std::string id = "vtx_" + std::to_string(++serial);
        p.view = gEngine->createOffscreenView(id, 48, 48, Colour(0.0f, 0.0f, 0.0f));
        p.scene = gEngine->createScene(id);
        p.scene->setAmbient(Colour(0.25f, 0.25f, 0.25f), Colour(0.2f, 0.2f, 0.2f));
        enginetest::addDirectionalLight(p.scene, Vec3(-0.5f, -0.7f, -0.5f), 3.14159f);
        p.node = p.scene->createNode();
        PbrParams params;
        params.albedo = Colour(0.5f, 0.5f, 0.5f);
        params.roughness = 0.6f;
        params.metalness = 0.0f;
        p.material = p.scene->createPbrMaterial(params);
        p.scene->attachMesh(p.node, p.scene->createMesh(mesh), p.material);
        p.scene->setNodeTransform(p.node, Vec3(0, 0, 0), q, Vec3(float(scale), float(scale), float(scale)));
        p.view->setScene(p.scene);
        enginetest::testCameraLookAt(p.view, Vec3(2.2f, 1.8f, 2.6f), Vec3(0.0f, 0.0f, 0.0f));
    };
    // the displaced reference mesh: world offset W = k * n (n world or local),
    // mesh-space displacement R^-1 W / scale
    auto displaced = [&](bool useLocal) {
        MeshData m = enginetest::unitCubeMesh();
        for (size_t i = 0; i < m.positions.size(); i += 3) {
            const V3 nl{ m.normals[i], m.normals[i + 1], m.normals[i + 2] };
            const V3 w = useLocal ? mul(nl, k) : mul(rotate(q, nl), k);
            const V3 dm = mul(rotate(qi, w), 1.0 / scale);
            m.positions[i] += float(dm.x);
            m.positions[i + 1] += float(dm.y);
            m.positions[i + 2] += float(dm.z);
        }
        return m;
    };

    Rig rig;
    auto n = rig.add(local ? "localNormal" : "worldNormal");
    auto kk = rig.add("multiply");
    rig.connect(n, 0, kk, 0);
    rig.connect(rig.addFloat(k), 0, kk, 1);
    auto zero = rig.add("multiply");
    rig.connect(rig.add("time"), 0, zero, 0);
    rig.connect(rig.addFloat(0.0), 0, zero, 1);
    auto sum = rig.add("add");
    rig.connect(kk, 0, sum, 0);
    rig.connect(zero, 0, sum, 1);
    rig.connect(sum, 0, rig.master, 7);   // Vertex Offset
    const auto emitted = materials::PieceEmitter::lower(materials::GraphBaker::compile(rig.graph));
    ++gChecks;
    if (!emitted.accepted || emitted.vertexSource.isEmpty()) {
        ++gFailures;
        std::printf("    FAIL %s: the emitter refused — %s\n", qPrintable(name),
                    qPrintable(emitted.fallbackReasons.value(QStringLiteral("Vertex Offset"))));
        delete rig.graph;
        return;
    }
    Probe live, want, other;
    build(live, enginetest::unitCubeMesh());
    build(want, displaced(local));
    build(other, displaced(!local));
    const QString path = materials::PieceEmitter::write(gDir, emitted.vertexSource, true);
    live.scene->setMaterialCustomPiece(live.material, path.toStdString(), CustomPieceStage::VertexPreTransform);
    render(gEngine.get(), 3);
    Image il, iw, io;
    live.view->readPixels(il);
    want.view->readPixels(iw);
    other.view->readPixels(io);
    auto differing = [](const Image &x, const Image &y) {
        int nDiff = 0;
        for (size_t i = 0; i + 3 < x.rgba.size() && i + 3 < y.rgba.size(); i += 4) {
            int w = 0;
            for (int c = 0; c < 3; ++c) w = std::max(w, std::abs(int(x.rgba[i + c]) - int(y.rgba[i + c])));
            if (w > 2) ++nDiff;
        }
        return nDiff;
    };
    const int dWant = differing(il, iw), dOther = differing(il, io);
    ++gChecks;
    // An edge pixel may flip between two computations of the same vertex
    // (world-space add vs a pre-displaced mesh): a handful, never a face.
    if (dWant > 6 || dOther < 40) {
        ++gFailures;
        std::printf("    FAIL %-22s %d pixels off the analytic displacement, %d off the other normal's\n",
                    qPrintable(name), dWant, dOther);
    } else {
        std::printf("    ok   %-22s %d pixels off the analytic displacement (control: %d off the other "
                    "normal's)\n", qPrintable(name), dWant, dOther);
    }
    const QString op = local ? QStringLiteral("localNormal") : QStringLiteral("worldNormal");
    if (!gCoveredOps.contains(op)) gCoveredOps << op;
    for (Probe *p : { &live, &want, &other }) {
        gEngine->destroyView(p->view);
        gEngine->destroyScene(p->scene);
    }
    delete rig.graph;
}

/// The sensitivity control: the same comparison against a reference that is
/// deliberately wrong must FAIL. Without this the suite could be green because
/// it cannot see anything.
void control_detects_a_wrong_reference()
{
    Rig rig;
    rig.toBaseColor(rig.addColor(0.30, 0.55, 0.80));
    const auto compiled = materials::GraphBaker::compile(rig.graph);
    const auto emitted = materials::PieceEmitter::lower(compiled);
    const QString path = materials::PieceEmitter::write(gDir, emitted.pixelSource, false);
    gEmitted.scene->setMaterialCustomPiece(gEmitted.material, path.toStdString(),
                                           CustomPieceStage::PixelPreLights);
    gEmitted.scene->setShaderTime(0.4f);
    PbrParams ref;
    ref.albedo = Colour(0.30f + 0.05f, 0.55f, 0.80f);   // 5% off on one channel
    ref.metalness = 0.0f;
    ref.roughness = 0.6f;
    gReference.scene->setPbrMaterial(gReference.material, ref);
    render(gEngine.get());
    Image a, b;
    gEmitted.view->readPixels(a);
    gReference.view->readPixels(b);
    const Px pa = centre(a), pb = centre(b);
    const int worst = std::max(std::abs(pa.r - pb.r),
                               std::max(std::abs(pa.g - pb.g), std::abs(pa.b - pb.b)));
    CHECK_MSG(worst > 2, "a 0.05 albedo error must be visible to this comparison (worst %d/255)",
              worst);
    std::printf("    control: a deliberate 0.05 error reads as %d/255\n", worst);
    gEmitted.scene->setMaterialCustomPiece(gEmitted.material, "", CustomPieceStage::PixelPreLights);
    delete rig.graph;
}

void report_op_coverage()
{
    const QStringList &all = materials::PieceEmitter::supportedOps();
    QStringList uncovered;
    for (const QString &op : all)
        if (!gCoveredOps.contains(op)) uncovered << op;
    std::printf("\n  OP COVERAGE: %d of %d emitter ops exercised by a rendered fixture\n",
                int(all.size() - uncovered.size()), int(all.size()));
    if (!uncovered.isEmpty())
        std::printf("  not rendered here: %s\n", qPrintable(uncovered.join(", ")));
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    gDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    QDir().mkpath(gDir);

    std::string error;
    gEngine = Engine::create(testConfig(), error);
    if (!gEngine) {
        std::printf("Engine::create failed: %s\n", error.c_str());
        return 1;
    }
    gEmitted = makeProbe(gEngine.get(), "emitted");
    gReference = makeProbe(gEngine.get(), "reference");
    if (!gEmitted.material || !gReference.material) {
        std::printf("could not build the probes\n");
        return 1;
    }

    std::printf("== emitter parity: CPU value vs rendered piece\n");

    oracle("literal color", { "color" }, [](Rig &r) {
        r.toBaseColor(r.addColor(0.30, 0.55, 0.80));
    });
    oracle("literal vector3", { "vector3" }, [](Rig &r) {
        r.toBaseColor(r.addVec(3, 0.2, 0.4, 0.6));
    });
    oracle("literal float splat", { "float" }, [](Rig &r) {
        r.toBaseColor(r.addFloat(0.35));
    });
    oracle("add", { "add" }, [](Rig &r) {
        auto op = r.add("add");
        r.connect(r.addVec(3, 0.1, 0.2, 0.3), 0, op, 0);
        r.connect(r.addVec(3, 0.2, 0.2, 0.2), 0, op, 1);
        r.toBaseColor(op);
    });
    oracle("subtract", { "subtract" }, [](Rig &r) {
        auto op = r.add("subtract");
        r.connect(r.addVec(3, 0.9, 0.8, 0.7), 0, op, 0);
        r.connect(r.addVec(3, 0.2, 0.3, 0.4), 0, op, 1);
        r.toBaseColor(op);
    });
    oracle("multiply", { "multiply" }, [](Rig &r) {
        auto op = r.add("multiply");
        r.connect(r.addVec(3, 0.8, 0.6, 0.4), 0, op, 0);
        r.connect(r.addVec(3, 0.5, 0.5, 0.5), 0, op, 1);
        r.toBaseColor(op);
    });
    oracle("vectorMultiply", { "vectorMultiply" }, [](Rig &r) {
        auto op = r.add("vectorMultiply");
        r.connect(r.addVec(3, 0.5, 0.5, 1.0), 0, op, 0);
        r.connect(r.addVec(3, 1.0, 0.5, 0.5), 0, op, 1);
        r.toBaseColor(op);
    });
    oracle("divide", { "divide" }, [](Rig &r) {
        auto op = r.add("divide");
        r.connect(r.addVec(3, 0.9, 0.6, 0.3), 0, op, 0);
        r.connect(r.addVec(3, 2.0, 2.0, 2.0), 0, op, 1);
        r.toBaseColor(op);
    });
    oracle("power", { "power" }, [](Rig &r) {
        auto op = r.add("power");
        r.connect(r.addVec(3, 0.5, 0.4, 0.9), 0, op, 0);
        r.connect(r.addVec(3, 2.0, 2.0, 2.0), 0, op, 1);
        r.toBaseColor(op);
    });
    oracle("sqrt", { "sqrt" }, [](Rig &r) {
        auto op = r.add("sqrt");
        r.connect(r.addVec(3, 0.25, 0.49, 0.64), 0, op, 0);
        r.toBaseColor(op);
    });
    oracle("min / max", { "min", "max" }, [](Rig &r) {
        auto lo = r.add("min");
        r.connect(r.addVec(3, 0.3, 0.9, 0.5), 0, lo, 0);
        r.connect(r.addVec(3, 0.6, 0.2, 0.5), 0, lo, 1);
        auto hi = r.add("max");
        r.connect(lo, 0, hi, 0);
        r.connect(r.addVec(3, 0.1, 0.1, 0.1), 0, hi, 1);
        r.toBaseColor(hi);
    });
    oracle("abs / negate", { "abs", "negate" }, [](Rig &r) {
        auto neg = r.add("negate");
        r.connect(r.addVec(3, 0.4, 0.6, 0.8), 0, neg, 0);
        auto op = r.add("abs");
        r.connect(neg, 0, op, 0);
        r.toBaseColor(op);
    });
    oracle("sign", { "sign" }, [](Rig &r) {
        auto op = r.add("sign");
        r.connect(r.addVec(3, 0.7, 0.7, 0.7), 0, op, 0);
        auto half = r.add("multiply");
        r.connect(op, 0, half, 0);
        r.connect(r.addVec(3, 0.5, 0.4, 0.3), 0, half, 1);
        r.toBaseColor(half);
    });
    oracle("ceil / floor", { "ceil", "floor" }, [](Rig &r) {
        auto c = r.add("ceil");
        r.connect(r.addVec(3, 0.2, 0.2, 0.2), 0, c, 0);   // -> 1
        auto f = r.add("floor");
        r.connect(r.addVec(3, 0.8, 0.8, 0.8), 0, f, 0);   // -> 0
        auto sum = r.add("add");
        r.connect(c, 0, sum, 0);
        r.connect(f, 0, sum, 1);
        auto scale = r.add("multiply");
        r.connect(sum, 0, scale, 0);
        r.connect(r.addVec(3, 0.4, 0.5, 0.6), 0, scale, 1);
        r.toBaseColor(scale);
    });
    oracle("round / trunc", { "round", "trunc" }, [](Rig &r) {
        auto ro = r.add("round");
        r.connect(r.addVec(3, 0.6, 0.6, 0.6), 0, ro, 0);  // -> 1
        auto tr = r.add("trunc");
        r.connect(r.addVec(3, 1.7, 1.7, 1.7), 0, tr, 0);  // -> 1
        auto sum = r.add("multiply");
        r.connect(ro, 0, sum, 0);
        r.connect(tr, 0, sum, 1);
        auto scale = r.add("multiply");
        r.connect(sum, 0, scale, 0);
        r.connect(r.addVec(3, 0.45, 0.55, 0.65), 0, scale, 1);
        r.toBaseColor(scale);
    });
    oracle("fraction", { "fraction" }, [](Rig &r) {
        auto op = r.add("fraction");
        r.connect(r.addVec(3, 2.25, 3.5, 4.75), 0, op, 0);
        r.toBaseColor(op);
    });
    oracle("oneminus", { "oneminus" }, [](Rig &r) {
        auto op = r.add("oneminus");
        r.connect(r.addVec(3, 0.25, 0.5, 0.75), 0, op, 0);
        r.toBaseColor(op);
    });
    oracle("sine", { "sine" }, [](Rig &r) {
        auto op = r.add("sine");
        r.connect(r.addVec(3, 0.5, 1.0, 1.5), 0, op, 0);
        r.toBaseColor(op);
    });
    oracle("step", { "step" }, [](Rig &r) {
        auto op = r.add("step");
        r.connect(r.addVec(3, 0.5, 0.5, 0.5), 0, op, 0);   // edge
        r.connect(r.addVec(3, 0.7, 0.2, 0.9), 0, op, 1);   // value
        auto scale = r.add("multiply");
        r.connect(op, 0, scale, 0);
        r.connect(r.addVec(3, 0.6, 0.6, 0.6), 0, scale, 1);
        r.toBaseColor(scale);
    });
    oracle("smoothstep", { "smoothstep" }, [](Rig &r) {
        auto op = r.add("smoothstep");
        r.connect(r.addVec(3, 0.0, 0.0, 0.0), 0, op, 0);
        r.connect(r.addVec(3, 1.0, 1.0, 1.0), 0, op, 1);
        r.connect(r.addVec(3, 0.25, 0.5, 0.75), 0, op, 2);
        r.toBaseColor(op);
    });
    oracle("clamp", { "clamp" }, [](Rig &r) {
        auto op = r.add("clamp");
        r.connect(r.addVec(3, 0.2, 0.2, 0.2), 0, op, 0);
        r.connect(r.addVec(3, 0.8, 0.8, 0.8), 0, op, 1);
        r.connect(r.addVec(3, 1.5, 0.5, 0.05), 0, op, 2);
        r.toBaseColor(op);
    });
    oracle("lerp", { "lerp" }, [](Rig &r) {
        auto op = r.add("lerp");
        r.connect(r.addVec(3, 0.0, 0.2, 0.4), 0, op, 0);
        r.connect(r.addVec(3, 1.0, 0.6, 0.8), 0, op, 1);
        r.connect(r.addFloat(0.25), 0, op, 2);
        r.toBaseColor(op);
    });
    oracle("reflect", { "reflect" }, [](Rig &r) {
        auto op = r.add("reflect");
        r.connect(r.addVec(3, 0.0, 0.0, 1.0), 0, op, 0);
        r.connect(r.addVec(3, 0.5, 0.0, -0.5), 0, op, 1);
        auto pos = r.add("abs");
        r.connect(op, 0, pos, 0);
        r.toBaseColor(pos);
    });
    oracle("dot", { "dot" }, [](Rig &r) {
        auto op = r.add("dot");
        r.connect(r.addVec(3, 0.5, 0.5, 0.0), 0, op, 0);
        r.connect(r.addVec(3, 0.5, 0.5, 0.0), 0, op, 1);
        r.toBaseColor(op);
    });
    oracle("length", { "length" }, [](Rig &r) {
        auto op = r.add("length");
        r.connect(r.addVec(3, 0.0, 0.6, 0.0), 0, op, 0);
        r.toBaseColor(op);
    });
    oracle("distance", { "distance" }, [](Rig &r) {
        auto op = r.add("distance");
        r.connect(r.addVec(3, 1.0, 0.0, 0.0), 0, op, 0);
        r.connect(r.addVec(3, 0.5, 0.0, 0.0), 0, op, 1);
        r.toBaseColor(op);
    });
    oracle("normalize", { "normalize" }, [](Rig &r) {
        auto op = r.add("normalize");
        r.connect(r.addVec(3, 0.0, 0.0, 2.0), 0, op, 0);
        r.toBaseColor(op);
    });
    oracle("splitvector", { "splitvector" }, [](Rig &r) {
        auto op = r.add("splitvector");
        r.connect(r.addVec(4, 0.1, 0.65, 0.3, 0.4), 0, op, 0);
        r.toBaseColor(op, 1);   // Y
    });
    oracle("composevector", { "composevector" }, [](Rig &r) {
        auto op = r.add("composevector");
        r.connect(r.addFloat(0.15), 0, op, 0);
        r.connect(r.addFloat(0.45), 0, op, 1);
        r.connect(r.addFloat(0.75), 0, op, 2);
        r.connect(r.addFloat(1.0), 0, op, 3);
        r.toBaseColor(op);
    });
    oracle("makeColor", { "makeColor" }, [](Rig &r) {
        auto op = r.add("makeColor");
        r.connect(r.addFloat(0.2), 0, op, 0);
        r.connect(r.addFloat(0.4), 0, op, 1);
        r.connect(r.addFloat(0.6), 0, op, 2);
        r.toBaseColor(op);
    });
    // The one `uv` node (MATERIAL_UV_NODES_SPEC D-3): bare coordinates, and a
    // transform.
    oracle("uv (bare coordinates, uv 0,0)", { "uv" }, [](Rig &r) {
        auto op = r.add("add");
        r.connect(r.add("uv"), 0, op, 0);
        r.connect(r.addVec(3, 0.3, 0.5, 0.7), 0, op, 1);
        r.toBaseColor(op);
    });
    oracle("uv (tiling + offset)", { "uv" }, [](Rig &r) {
        auto op = r.add("uv");
        r.connect(r.add("uv"), 0, op, 0);
        r.connect(r.addVec(2, 2.0, 2.0), 0, op, 1);
        r.connect(r.addVec(2, 0.4, 0.6), 0, op, 2);
        r.toBaseColor(op);
    });
    // ROTATION (D-5, spec I-8): the CPU rotates about (0.5,0.5) in degrees and
    // the piece has to produce the same number — this is the fixture that says
    // so. A CONNECTED rotation socket, so the emitter takes its non-zero form.
    oracle("uv (rotation 30 deg)", { "uv" }, [](Rig &r) {
        auto op = r.add("uv");
        r.connect(r.add("uv"), 0, op, 0);
        r.connect(r.addVec(2, 2.0, 2.0), 0, op, 1);
        r.connect(r.addVec(2, 0.1, 0.2), 0, op, 2);
        r.connect(r.addFloat(30.0), 0, op, 3);
        r.toBaseColor(op);
    });
    oracle("panner", { "panner" }, [](Rig &r) {
        auto op = r.add("panner");
        r.connect(r.addVec(2, 0.25, 0.25), 0, op, 0);
        r.connect(r.addVec(2, 0.5, 0.5), 0, op, 1);
        r.connect(r.add("time"), 0, op, 2);
        r.toBaseColor(op);
    });
    oracle("flipbook", { "flipbook" }, [](Rig &r) {
        auto op = r.add("flipbook");
        r.connect(r.addVec(2, 0.5, 0.5), 0, op, 0);
        r.connect(r.addFloat(4.0), 0, op, 1);
        r.connect(r.addFloat(4.0), 0, op, 2);
        r.connect(r.addFloat(1.0), 0, op, 3);
        r.connect(r.add("time"), 0, op, 4);
        r.toBaseColor(op);
    });
    oracle("normalintensity", { "normalintensity" }, [](Rig &r) {
        auto op = r.add("normalintensity");
        r.connect(r.addVec(3, 0.6, 0.0, 0.8), 0, op, 0);
        r.connect(r.addFloat(0.5), 0, op, 1);
        auto pos = r.add("abs");
        r.connect(op, 0, pos, 0);
        r.toBaseColor(pos);
    });
    oracle("combinenormals", { "combinenormals" }, [](Rig &r) {
        auto op = r.add("combinenormals");
        r.connect(r.addVec(3, 0.3, 0.0, 1.0), 0, op, 0);
        r.connect(r.addVec(3, 0.0, 0.4, 1.0), 0, op, 1);
        auto pos = r.add("abs");
        r.connect(op, 0, pos, 0);
        r.toBaseColor(pos);
    });
    oracle("time", { "time" }, [](Rig &r) {
        auto op = r.add("multiply");
        r.connect(r.add("time"), 0, op, 0);
        r.connect(r.addFloat(0.5), 0, op, 1);
        r.toBaseColor(op);
    }, 1.2);
    oracle("pulsate", { "pulsate" }, [](Rig &r) {
        auto op = r.add("pulsate");
        r.connect(r.addFloat(2.0), 0, op, 0);
        r.toBaseColor(op);
    }, 0.7);
    oracle("chain: pulsate -> lerp of two colours", { "pulsate", "lerp" }, [](Rig &r) {
        auto op = r.add("lerp");
        r.connect(r.addColor(0.1, 0.2, 0.7), 0, op, 0);
        r.connect(r.addColor(0.9, 0.6, 0.1), 0, op, 1);
        auto pulse = r.add("pulsate");
        r.connect(r.addFloat(3.0), 0, pulse, 0);
        r.connect(pulse, 0, op, 2);
        r.toBaseColor(op);
    }, 0.55);

    std::printf("\n== live-only ops vs an ANALYTIC reference (TORNADO-1, G3)\n");
    // fresnel(power 2) at the centre pixel: the +Z face from the standard eye.
    oracleAnalytic("fresnel (power 2)", { "fresnel" }, [](Rig &r) {
        auto op = r.add("fresnel");
        r.connect(r.addFloat(2.0), 0, op, 1);
        r.toBaseColor(op);
    }, V3{ 2.2, 1.8, 2.6 }, [](const Hit &h, V3 eye) {
        const double d = std::max(0.0, dot3(h.normal, norm(sub(eye, h.point))));
        const double f = std::pow(1.0 - d, 2.0);
        return V3{ f, f, f };
    });
    // abs(worldNormal) from an eye that sees the +X face: (1,0,0). The baker's
    // identity frame says (0,0,1); a view-space normal would be neither.
    oracleAnalytic("worldNormal (+X face)", { "worldNormal" }, [](Rig &r) {
        auto op = r.add("abs");
        r.connect(r.add("worldNormal"), 0, op, 0);
        r.toBaseColor(op);
    }, V3{ 2.6, 1.8, 2.2 }, [](const Hit &h, V3) {
        return V3{ std::abs(h.normal.x), std::abs(h.normal.y), std::abs(h.normal.z) };
    });
    vertexNormalOracle(QStringLiteral("worldNormal (vertex)"), false);
    vertexNormalOracle(QStringLiteral("localNormal (vertex)"), true);

    std::printf("\n== control\n");
    control_detects_a_wrong_reference();
    report_op_coverage();

    gEngine->destroyView(gEmitted.view);
    gEngine->destroyView(gReference.view);
    gEngine->destroyScene(gEmitted.scene);
    gEngine->destroyScene(gReference.scene);
    gEngine.reset();
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
