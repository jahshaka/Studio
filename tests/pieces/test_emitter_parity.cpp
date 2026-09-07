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
    cfg.backend = Backend::Vulkan;
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
    oracle("texCoords (uv 0,0)", { "texCoords" }, [](Rig &r) {
        auto op = r.add("add");
        r.connect(r.add("texCoords"), 0, op, 0);
        r.connect(r.addVec(3, 0.3, 0.5, 0.7), 0, op, 1);
        r.toBaseColor(op);
    });
    oracle("uvTransform", { "uvTransform" }, [](Rig &r) {
        auto op = r.add("uvTransform");
        r.connect(r.add("texCoords"), 0, op, 0);
        r.connect(r.addVec(2, 2.0, 2.0), 0, op, 1);
        r.connect(r.addVec(2, 0.4, 0.6), 0, op, 2);
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
