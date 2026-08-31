// shadergraph.baker_parity — the Materials Evaluator's per-node numeric suite
// (SPECS/MATERIALS_EVALUATOR_SPEC.md section 6).
//
// Fixtures are the MATERIALS_NODES_AUDIT.md section 2 probe table plus one
// hand-computed case per op node, the sockethelper coercion rules, and the
// fake-fragment-context approximations. Uniform folds compare as exact
// doubles (the evaluator computes in doubles end-to-end).
//
// Documented contract changes vs the audit's seam-era table (the audit rows
// expected "unsupported" because no CPU math existed; folding is the point
// of this program):
//   - float -> add -> Base Color now FOLDS (was honest-unsupported).
//   - lerp(0,1,0.5) -> Roughness == 0.5 (the audit's "no CPU math" probe).
//   - color RGBA (socket 0) -> Metallic folds to .redF() — GLSL vec4->float
//     coercion takes the leading component (was honest-unsupported).
//   - pulsate/time fold at t=0 and are named in approximatedNodes.
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QImage>
#include <QJsonObject>
#include <QJsonValue>
#include <cmath>
#include <cstdio>

#include "modules/materials/graph/nodegraph.h"
#include "modules/materials/models/libraryv1.h"
#include "modules/materials/models/nodemodel.h"
#include "modules/materials/nodes/pbrmasternode.h"
#include "modules/materials/nodes/test.h"
#include "modules/materials/core/pbrgraphevaluator.h"
#include "modules/materials/core/bakeprogram.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool exact(double a, double b) { return a == b; }
static bool near(double a, double b, double eps = 1e-5) { return std::fabs(a - b) < eps; }

// One rig per probe: a graph with a PBR master and a library to build from.
struct Rig
{
    NodeGraph* graph;
    NodeModel* master;
    LibraryV1* lib;

    explicit Rig(bool pbr = true)
    {
        lib = new LibraryV1();
        graph = new NodeGraph();
        graph->setNodeLibrary(lib);
        master = pbr ? static_cast<NodeModel*>(new PbrMasterNode())
                     : static_cast<NodeModel*>(new SurfaceMasterNode());
        graph->addNode(master);
        graph->setMasterNode(master);
    }

    NodeModel* add(const QString& type)
    {
        auto node = lib->createNode(type);
        graph->addNode(node);
        return node;
    }
    NodeModel* addFloat(double v)
    {
        auto node = add("float");
        node->deserializeWidgetValue(QJsonValue(v));
        return node;
    }
    NodeModel* addVec(int n, double x, double y, double z = 0, double w = 0)
    {
        auto node = add(QStringLiteral("vector%1").arg(n));
        QJsonObject obj;
        obj["x"] = x; obj["y"] = y; obj["z"] = z; obj["w"] = w;
        node->deserializeWidgetValue(obj);
        return node;
    }
    NodeModel* addColor(double r, double g, double b, double a = 1.0)
    {
        auto node = add("color");
        QJsonObject obj;
        obj["r"] = r; obj["g"] = g; obj["b"] = b; obj["a"] = a;
        node->deserializeWidgetValue(obj);
        return node;
    }

    // master socket indices: 0 Base Color, 1 Metallic, 2 Roughness, 3 Normal,
    // 4 Occlusion, 5 Emissive, 6 Alpha, 7 Alpha Cutoff, 8/9 Vertex Offset/Extrusion
    void toMaster(NodeModel* from, int out, int masterIn)
    {
        graph->addConnection(from, out, master, masterIn);
    }

    PbrGraphEvaluator::Result eval() { return PbrGraphEvaluator::evaluate(graph); }
    QJsonObject info() { return PbrGraphEvaluator::bakeInfo(graph)["perSocket"].toObject(); }
};

static double roughnessOf(Rig& rig) { return rig.eval().values["roughness"].toDouble(-999); }
static double metallicOf(Rig& rig) { return rig.eval().values["metallic"].toDouble(-999); }
static QJsonObject baseColorOf(Rig& rig) { return rig.eval().values["baseColor"].toObject(); }

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // ================= audit section 2 probe table =================
    {
        Rig r; r.toMaster(r.addFloat(0.25), 0, 2);
        CHECK(exact(roughnessOf(r), 0.25), "probe: float(0.25) -> Roughness == 0.25");
    }
    {
        Rig r; r.toMaster(r.addFloat(0.77), 0, 1);
        CHECK(exact(metallicOf(r), 0.77), "probe: float(0.77) -> Metallic == 0.77");
    }
    {
        Rig r; r.toMaster(r.addFloat(0.25), 0, 0);
        auto c = baseColorOf(r);
        CHECK(near(c["r"].toDouble(), 0.25, 1e-5) && near(c["g"].toDouble(), 0.25, 1e-5)
                  && near(c["b"].toDouble(), 0.25, 1e-5),
              "probe: float(0.25) -> Base Color is grayscale (0.25,0.25,0.25)");
    }
    {
        Rig r; r.toMaster(r.addFloat(0.5), 0, 7);
        auto res = r.eval();
        CHECK(exact(res.values["alphaCutoff"].toDouble(), 0.5) && res.values["alphaMode"].toInt() == 1,
              "probe: float(0.5) -> Alpha Cutoff == 0.5 with alphaMode=1");
    }
    {
        Rig r; r.toMaster(r.addFloat(1.5), 0, 1);
        CHECK(exact(metallicOf(r), 1.0), "probe: float(1.5) -> Metallic clamps to 1.0 (D6)");
    }
    {
        Rig r; r.toMaster(r.addFloat(0.5), 0, 3);
        auto res = r.eval();
        CHECK(res.unsupportedNodes.size() == 1 && res.unsupportedNodes.first() == "Normal <- float",
              "probe: float -> Normal is unsupported (map-only slot)");
    }
    {
        Rig r; r.toMaster(r.addColor(1.0, 0.5, 0.25), 0, 0);
        auto c = baseColorOf(r);
        CHECK(near(c["r"].toDouble(), 1.0, 1e-5) && near(c["g"].toDouble(), 0.5, 1e-5)
                  && near(c["b"].toDouble(), 0.25, 1e-5),
              "probe: color(1,.5,.25) -> Base Color (16-bit QColor quantization)");
    }
    {
        Rig r;
        auto col = r.addColor(1.0, 0.5, 0.25, 0.75);
        r.toMaster(col, 1, 1); // R -> Metallic
        r.toMaster(col, 2, 2); // G -> Roughness
        r.toMaster(col, 4, 6); // A -> Alpha
        auto res = r.eval();
        CHECK(near(res.values["metallic"].toDouble(), 1.0, 1e-5)
                  && near(res.values["roughness"].toDouble(), 0.5, 1e-5)
                  && near(res.values["alpha"].toDouble(), 0.75, 1e-5),
              "probe: color channel sockets R/G/A -> Metallic/Roughness/Alpha");
    }
    {
        // contract change: the audit-era evaluator reported this unsupported;
        // GLSL coerces vec4 -> float as .x, so it folds to red
        Rig r; r.toMaster(r.addColor(1.0, 0.5, 0.25), 0, 1);
        CHECK(near(metallicOf(r), 1.0, 1e-5), "probe: color RGBA (socket 0) -> Metallic folds to .r (vec4->float = .x)");
    }
    {
        Rig r; r.toMaster(r.addVec(3, 2.0, -1.0, 0.5), 0, 0);
        auto c = baseColorOf(r);
        CHECK(near(c["r"].toDouble(), 1.0, 1e-5) && near(c["g"].toDouble(), 0.0, 1e-5)
                  && near(c["b"].toDouble(), 0.5, 1e-5),
              "probe: vector3(2,-1,0.5) -> Base Color clamps to (1,0,0.5) (D4 fixed)");
    }
    {
        Rig r; r.toMaster(r.addVec(4, 0.1, 0.2, 0.3, 0.4), 0, 0);
        auto c = baseColorOf(r);
        CHECK(near(c["r"].toDouble(), 0.1, 1e-5) && near(c["g"].toDouble(), 0.2, 1e-5)
                  && near(c["b"].toDouble(), 0.3, 1e-5) && near(c["a"].toDouble(), 0.4, 1e-5),
              "probe: vector4(.1,.2,.3,.4) -> Base Color keeps fractions, w as alpha");
    }
    {
        Rig r; r.toMaster(r.addVec(2, 0.5, 0.25), 0, 0);
        auto res = r.eval();
        auto c = res.values["baseColor"].toObject();
        CHECK(res.unsupportedNodes.isEmpty()
                  && near(c["r"].toDouble(), 0.5, 1e-5) && near(c["g"].toDouble(), 0.25, 1e-5)
                  && near(c["b"].toDouble(), 0.0, 1e-5),
              "probe: vector2(.5,.25) -> Base Color folds to (x,y,0) (D5)");
    }
    {
        Rig r;
        auto tex = r.add("texture"); // no file chosen
        r.toMaster(tex, 0, 0);
        auto res = r.eval();
        CHECK(res.unsupportedNodes.isEmpty() && !res.values.contains("baseColorMap"),
              "probe: empty texture node -> silent no-op (no map, no warning)");
    }
    {
        // the audit's "float->add->Base Color unsupported" seam probe:
        // uniform chains fold now
        Rig r;
        auto add = r.add("add");
        r.graph->addConnection(r.addFloat(0.25), 0, add, 0);
        r.graph->addConnection(r.addFloat(0.5), 0, add, 1);
        r.toMaster(add, 0, 0);
        auto res = r.eval();
        auto c = res.values["baseColor"].toObject();
        CHECK(res.unsupportedNodes.isEmpty() && near(c["r"].toDouble(), 0.75, 1e-5),
              "probe: float -> add -> Base Color FOLDS (the phase-2 seam is closed)");
    }
    {
        // the audit's "no CPU math exists" probe now PASSES
        Rig r;
        auto lerp = r.add("lerp");
        r.graph->addConnection(r.addFloat(0.0), 0, lerp, 0);
        r.graph->addConnection(r.addFloat(1.0), 0, lerp, 1);
        r.graph->addConnection(r.addFloat(0.5), 0, lerp, 2);
        r.toMaster(lerp, 0, 2);
        CHECK(exact(roughnessOf(r), 0.5), "probe: lerp(0,1,0.5) -> Roughness == 0.5");
    }

    // ================= one case per op node =================
    auto binary = [](const char* type, double a, double b, double expected, const char* msg) {
        Rig r;
        auto op = r.add(type);
        r.graph->addConnection(r.addFloat(a), 0, op, 0);
        r.graph->addConnection(r.addFloat(b), 0, op, 1);
        r.toMaster(op, 0, 2);
        CHECK(exact(roughnessOf(r), expected), msg);
    };
    auto unary = [](const char* type, double a, double expected, const char* msg) {
        Rig r;
        auto op = r.add(type);
        r.graph->addConnection(r.addFloat(a), 0, op, 0);
        r.toMaster(op, 0, 2);
        CHECK(exact(roughnessOf(r), expected), msg);
    };

    binary("add", 0.25, 0.5, 0.75, "op: add(0.25, 0.5) == 0.75");
    binary("subtract", 0.75, 0.25, 0.5, "op: subtract(0.75, 0.25) == 0.5");
    binary("multiply", 0.5, 0.5, 0.25, "op: multiply(0.5, 0.5) == 0.25");
    binary("divide", 0.25, 0.5, 0.5, "op: divide(0.25, 0.5) == 0.5");
    binary("power", 0.5, 2.0, 0.25, "op: power(0.5, 2) == 0.25");
    binary("min", 0.3, 0.7, 0.3, "op: min(0.3, 0.7) == 0.3");
    binary("max", 0.3, 0.7, 0.7, "op: max(0.3, 0.7) == 0.7");
    unary("sqrt", 0.25, 0.5, "op: sqrt(0.25) == 0.5");
    unary("abs", -0.25, 0.25, "op: abs(-0.25) == 0.25");
    unary("sign", 0.5, 1.0, "op: sign(0.5) == 1");
    unary("ceil", 0.2, 1.0, "op: ceil(0.2) == 1");
    unary("floor", 0.7, 0.0, "op: floor(0.7) == 0");
    unary("round", 0.6, 1.0, "op: round(0.6) == 1");
    unary("trunc", 0.9, 0.0, "op: trunc(0.9) == 0");
    unary("fraction", 1.25, 0.25, "op: fraction(1.25) == 0.25 (GLSL fract)");
    unary("oneminus", 0.25, 0.75, "op: oneminus(0.25) == 0.75");
    unary("negate", -0.5, 0.5, "op: negate(-0.5) == 0.5");
    unary("sine", 1.0, std::sin(1.0), "op: sine(1.0) == std::sin(1.0), exact");

    {
        // divide by zero: GLSL semantics produce inf, the landing clamp caps it
        Rig r;
        auto op = r.add("divide");
        r.graph->addConnection(r.addFloat(1.0), 0, op, 0);
        r.graph->addConnection(r.addFloat(0.0), 0, op, 1);
        r.toMaster(op, 0, 1);
        CHECK(exact(metallicOf(r), 1.0), "op: divide(1, 0) -> inf, clamped to 1.0 at the FloatSlot");
    }
    {
        Rig r;
        auto op = r.add("step"); // step(Edge, Value): value < edge ? 0 : 1
        r.graph->addConnection(r.addFloat(0.5), 0, op, 0);
        r.graph->addConnection(r.addFloat(0.7), 0, op, 1);
        r.toMaster(op, 0, 2);
        CHECK(exact(roughnessOf(r), 1.0), "op: step(edge 0.5, value 0.7) == 1");
    }
    {
        Rig r;
        auto op = r.add("smoothstep");
        r.graph->addConnection(r.addFloat(0.0), 0, op, 0);
        r.graph->addConnection(r.addFloat(1.0), 0, op, 1);
        r.graph->addConnection(r.addFloat(0.25), 0, op, 2);
        r.toMaster(op, 0, 2);
        CHECK(exact(roughnessOf(r), 0.15625), "op: smoothstep(0, 1, 0.25) == 0.15625 (Hermite)");
    }
    {
        Rig r;
        auto op = r.add("clamp"); // sockets: Min, Max, Value; post-D7 clamp(Value, Min, Max)
        r.graph->addConnection(r.addFloat(0.2), 0, op, 0);
        r.graph->addConnection(r.addFloat(0.8), 0, op, 1);
        r.graph->addConnection(r.addFloat(1.5), 0, op, 2);
        r.toMaster(op, 0, 2);
        CHECK(exact(roughnessOf(r), 0.8), "op: clamp(value 1.5, min 0.2, max 0.8) == 0.8 (D7 order)");
    }
    {
        Rig r; // vectorMultiply == multiply on vec4
        auto op = r.add("vectorMultiply");
        r.graph->addConnection(r.addVec(3, 0.5, 0.5, 1.0), 0, op, 0);
        r.graph->addConnection(r.addVec(3, 1.0, 0.5, 0.5), 0, op, 1);
        r.toMaster(op, 0, 0);
        auto c = baseColorOf(r);
        CHECK(near(c["r"].toDouble(), 0.5, 1e-5) && near(c["g"].toDouble(), 0.25, 1e-5)
                  && near(c["b"].toDouble(), 0.5, 1e-5),
              "op: vectorMultiply((.5,.5,1),(1,.5,.5)) == (.5,.25,.5)");
    }
    {
        Rig r; // reflect(I, N) = I - 2*dot(N, I)*N; sockets Normal(0), Incident(1)
        auto op = r.add("reflect");
        r.graph->addConnection(r.addVec(3, 0.0, 0.0, 1.0), 0, op, 0);
        r.graph->addConnection(r.addVec(3, 1.0, 0.0, -1.0), 0, op, 1);
        r.toMaster(op, 0, 0);
        auto c = baseColorOf(r);
        CHECK(near(c["r"].toDouble(), 1.0, 1e-5) && near(c["g"].toDouble(), 0.0, 1e-5)
                  && near(c["b"].toDouble(), 1.0, 1e-5),
              "op: reflect(I=(1,0,-1), N=(0,0,1)) == (1,0,1)");
    }
    {
        Rig r; // dot on vec4 sockets: vec3 grows xyzz
        auto op = r.add("dot");
        r.graph->addConnection(r.addVec(3, 0.5, 0.5, 0.0), 0, op, 0);
        r.graph->addConnection(r.addVec(3, 0.5, 0.5, 0.0), 0, op, 1);
        r.toMaster(op, 0, 2);
        CHECK(exact(roughnessOf(r), 0.5), "op: dot((.5,.5,0)->xyzz twice) == 0.5");
    }
    {
        Rig r;
        auto op = r.add("length");
        r.graph->addConnection(r.addVec(3, 0.0, 0.6, 0.0), 0, op, 0);
        r.toMaster(op, 0, 2);
        // vector nodes store QVector3D floats, so the literal is float(0.6)
        CHECK(exact(roughnessOf(r), (double)0.6f), "op: length((0,.6,0,0)) == 0.6 (float-quantized literal)");
    }
    {
        Rig r;
        auto op = r.add("distance");
        r.graph->addConnection(r.addVec(3, 1.0, 0.0, 0.0), 0, op, 0);
        r.graph->addConnection(r.addVec(3, 0.5, 0.0, 0.0), 0, op, 1);
        r.toMaster(op, 0, 2);
        CHECK(exact(roughnessOf(r), 0.5), "op: distance((1,0,0),(0.5,0,0)) == 0.5 (two inputs, D8)");
    }
    {
        Rig r; // vec3 (0,0,2) grows to (0,0,2,2) at the vec4 socket - GLSL-exact
        auto op = r.add("normalize");
        r.graph->addConnection(r.addVec(3, 0.0, 0.0, 2.0), 0, op, 0);
        r.toMaster(op, 0, 0);
        auto c = baseColorOf(r);
        CHECK(near(c["b"].toDouble(), std::sqrt(0.5), 1e-5),
              "op: normalize on the coerced vec4 (0,0,2,2) -> z == 1/sqrt(2)");
    }
    {
        Rig r;
        auto op = r.add("splitvector");
        r.graph->addConnection(r.addVec(4, 0.1, 0.2, 0.3, 0.4), 0, op, 0);
        r.toMaster(op, 1, 2); // Y -> Roughness
        CHECK(exact(roughnessOf(r), (double)0.2f), "op: splitvector .y of (.1,.2,.3,.4) == 0.2 (float-quantized literal)");
    }
    {
        Rig r;
        auto op = r.add("composevector");
        r.graph->addConnection(r.addFloat(0.1), 0, op, 0);
        r.graph->addConnection(r.addFloat(0.2), 0, op, 1);
        r.graph->addConnection(r.addFloat(0.3), 0, op, 2);
        r.graph->addConnection(r.addFloat(0.4), 0, op, 3);
        r.toMaster(op, 0, 0);
        auto c = baseColorOf(r);
        CHECK(near(c["r"].toDouble(), 0.1, 1e-5) && near(c["g"].toDouble(), 0.2, 1e-5)
                  && near(c["b"].toDouble(), 0.3, 1e-5) && near(c["a"].toDouble(), 0.4, 1e-5),
              "op: composevector(.1,.2,.3,.4) == vec4 with W intact (D11 socket)");
    }
    {
        Rig r;
        auto op = r.add("makeColor");
        r.graph->addConnection(r.addFloat(0.2), 0, op, 0);
        r.graph->addConnection(r.addFloat(0.4), 0, op, 1);
        r.graph->addConnection(r.addFloat(0.6), 0, op, 2);
        r.toMaster(op, 0, 0);
        auto c = baseColorOf(r);
        CHECK(near(c["r"].toDouble(), 0.2, 1e-5) && near(c["g"].toDouble(), 0.4, 1e-5)
                  && near(c["b"].toDouble(), 0.6, 1e-5) && near(c["a"].toDouble(), 1.0, 1e-5),
              "op: makeColor(.2,.4,.6) == vec4(r,g,b,1)");
    }
    {
        Rig r; // defaults: Normal (0,0,1), Intensity 1 -> (0,0,1)
        auto op = r.add("normalintensity");
        r.toMaster(op, 0, 0);
        auto c = baseColorOf(r);
        CHECK(near(c["b"].toDouble(), 1.0, 1e-5) && near(c["r"].toDouble(), 0.0, 1e-5),
              "op: normalintensity(defaults) == (0,0,1)");
    }
    {
        Rig r; // defaults: (0,0,1)+(0,0,1) normalized == (0,0,1)
        auto op = r.add("combinenormals");
        r.toMaster(op, 0, 0);
        auto c = baseColorOf(r);
        CHECK(near(c["b"].toDouble(), 1.0, 1e-5), "op: combinenormals(defaults) == (0,0,1)");
    }
    {
        Rig r; // uniform-UV panner: uv + speed*time at t=0 == uv
        auto op = r.add("panner");
        r.graph->addConnection(r.addVec(2, 0.25, 0.25), 0, op, 0);
        r.toMaster(op, 0, 0);
        auto res = r.eval();
        auto c = res.values["baseColor"].toObject();
        CHECK(near(c["r"].toDouble(), 0.25, 1e-5) && near(c["g"].toDouble(), 0.25, 1e-5)
                  && near(c["b"].toDouble(), 0.0, 1e-5),
              "op: panner(uv=(.25,.25), t=0) == (.25,.25)");
        CHECK(res.animated, "op: panner's default Time input marks the result animated");
    }
    {
        Rig r; // flipbook frame 0 of a 2x2 sheet at uv (0,0): (0, 0.5)
        auto op = r.add("flipbook");
        r.graph->addConnection(r.addVec(2, 0.0, 0.0), 0, op, 0);
        r.graph->addConnection(r.addFloat(2.0), 0, op, 1);
        r.graph->addConnection(r.addFloat(2.0), 0, op, 2);
        r.graph->addConnection(r.addFloat(2.0), 0, op, 3);
        r.toMaster(op, 0, 0);
        auto c = baseColorOf(r);
        CHECK(near(c["r"].toDouble(), 0.0, 1e-5) && near(c["g"].toDouble(), 0.5, 1e-5),
              "op: flipbook(2x2, t=0, uv=(0,0)) == (0, 0.5) (D10 math)");
    }
    {
        Rig r; // texelsize with no texture: 0, not inf
        auto op = r.add("texelsize");
        r.toMaster(op, 3, 2); // 1/Width -> Roughness
        CHECK(exact(roughnessOf(r), 0.0), "op: texelsize(no texture) 1/W == 0");
    }

    // ================= fake fragment context (approximations) =================
    {
        Rig r;
        r.toMaster(r.add("worldNormal"), 0, 0);
        auto res = r.eval();
        auto c = res.values["baseColor"].toObject();
        CHECK(near(c["r"].toDouble(), 0.0, 1e-5) && near(c["g"].toDouble(), 0.0, 1e-5)
                  && near(c["b"].toDouble(), 1.0, 1e-5),
              "ctx: worldNormal == (0,0,1) tangent-space identity");
        CHECK(res.approximatedNodes.size() == 1 && res.approximatedNodes.first().contains("worldNormal"),
              "ctx: worldNormal named in approximatedNodes");
    }
    {
        Rig r;
        r.toMaster(r.add("localNormal"), 0, 2);
        CHECK(exact(roughnessOf(r), 0.0), "ctx: localNormal.x == 0 on a float slot");
    }
    {
        Rig r;
        r.toMaster(r.add("fresnel"), 0, 2);
        auto res = r.eval();
        CHECK(exact(res.values["roughness"].toDouble(), 0.0), "ctx: fresnel == 0 under the fake context");
        CHECK(!res.approximatedNodes.isEmpty() && res.approximatedNodes.first().contains("fresnel"),
              "ctx: fresnel named in approximatedNodes");
    }
    {
        Rig r;
        r.toMaster(r.add("depth"), 0, 2);
        CHECK(exact(roughnessOf(r), 0.0), "ctx: depth == 0 (near-plane convention)");
    }
    {
        Rig r;
        r.toMaster(r.add("time"), 0, 2);
        auto res = r.eval();
        CHECK(exact(res.values["roughness"].toDouble(), 0.0) && res.animated,
              "ctx: time == 0 at the bake parameter, animated=true");
    }
    {
        Rig r;
        r.toMaster(r.add("pulsate"), 0, 2);
        CHECK(exact(roughnessOf(r), 0.5), "ctx: pulsate == 0.5 at t=0");
    }

    // ================= coercion cases (sockethelper parity) =================
    {
        Rig r; // float -> vec4 splat inside add
        auto op = r.add("add");
        r.graph->addConnection(r.addFloat(0.25), 0, op, 0);
        r.graph->addConnection(r.addVec(3, 0.25, 0.5, 0.75), 0, op, 1);
        r.toMaster(op, 0, 0);
        auto c = baseColorOf(r);
        CHECK(near(c["r"].toDouble(), 0.5, 1e-5) && near(c["g"].toDouble(), 0.75, 1e-5)
                  && near(c["b"].toDouble(), 1.0, 1e-5),
              "coercion: float splats to vec4 (0.25 + (.25,.5,.75))");
    }
    {
        Rig r; // vec4 -> vec2 shrink through panner's UV socket
        auto op = r.add("panner");
        r.graph->addConnection(r.addVec(4, 0.1, 0.2, 0.3, 0.4), 0, op, 0);
        r.graph->addConnection(r.addVec(2, 0.0, 0.0), 0, op, 1); // speed 0
        r.toMaster(op, 0, 0);
        auto c = baseColorOf(r);
        CHECK(near(c["r"].toDouble(), 0.1, 1e-5) && near(c["g"].toDouble(), 0.2, 1e-5),
              "coercion: vec4 shrinks to vec2 leading components (v.xy)");
    }
    {
        Rig r; // vec2 -> vec4 grows xyyy through add
        auto op = r.add("add");
        r.graph->addConnection(r.addVec(2, 0.25, 0.5), 0, op, 0);
        r.toMaster(op, 0, 0); // B stays vec4(0)
        auto c = baseColorOf(r);
        CHECK(near(c["r"].toDouble(), 0.25, 1e-5) && near(c["g"].toDouble(), 0.5, 1e-5)
                  && near(c["b"].toDouble(), 0.5, 1e-5) && near(c["a"].toDouble(), 0.5, 1e-5),
              "coercion: vec2 grows to vec4 as v.xyyy");
    }

    // ================= texture sampling (uniform fold) =================
    {
        const QString texPath = QDir::current().absoluteFilePath("parity_2x2.png");
        QImage img(2, 2, QImage::Format_RGBA8888);
        img.setPixelColor(0, 0, QColor(255, 0, 0));
        img.setPixelColor(1, 0, QColor(0, 255, 0));
        img.setPixelColor(0, 1, QColor(0, 0, 255));
        img.setPixelColor(1, 1, QColor(255, 255, 255));
        CHECK(img.save(texPath), "sampling: 2x2 fixture written");

        Rig r;
        auto tex = r.add("texture");
        static_cast<TextureNode*>(tex)->setTexturePath(texPath);
        auto sampler = r.add("textureSampler");
        r.graph->addConnection(tex, 0, sampler, 0);
        r.graph->addConnection(r.addVec(2, 0.5, 0.5), 0, sampler, 1); // fixed center UV
        r.toMaster(sampler, 0, 0);
        auto res = r.eval();
        auto c = res.values["baseColor"].toObject();
        CHECK(res.unsupportedNodes.isEmpty(), "sampling: fixed-UV sampler chain folds");
        CHECK(near(c["r"].toDouble(), 0.5, 1.0 / 255) && near(c["g"].toDouble(), 0.5, 1.0 / 255)
                  && near(c["b"].toDouble(), 0.5, 1.0 / 255),
              "sampling: bilinear center of the 2x2 == average gray");
        QFile::remove(texPath);
    }
    {
        Rig r; // sampler with no texture: vec4(0), uniform, GLSL-documented
        auto sampler = r.add("textureSampler");
        r.toMaster(sampler, 0, 0);
        auto res = r.eval();
        auto c = res.values["baseColor"].toObject();
        CHECK(res.unsupportedNodes.isEmpty() && near(c["r"].toDouble(), 0.0, 1e-5)
                  && near(c["b"].toDouble(), 0.0, 1e-5),
              "sampling: unconnected sampler folds to vec4(0) black");
    }

    // ================= classification (graph.bakeInfo) =================
    {
        const QString texPath = QDir::current().absoluteFilePath("parity_tex.png");
        QImage img(4, 4, QImage::Format_RGBA8888);
        img.fill(QColor(200, 100, 50));
        img.save(texPath);

        Rig r;
        r.toMaster(r.addFloat(0.5), 0, 2); // uniform Roughness
        auto tex = r.add("texture");
        static_cast<TextureNode*>(tex)->setTexturePath(texPath);
        r.toMaster(tex, 0, 0);             // passthrough Base Color
        auto split = r.add("splitvector");
        r.graph->addConnection(r.add("texCoords"), 0, split, 0);
        r.toMaster(split, 0, 1);           // varying U -> Metallic
        r.toMaster(r.add("texCoords"), 0, 7); // varying Alpha Cutoff
        r.toMaster(r.addVec(3, 1, 1, 1), 0, 8); // Vertex Offset fed

        auto info = r.info();
        CHECK(info["Roughness"].toString() == "uniform", "bakeInfo: float -> Roughness is 'uniform'");
        CHECK(info["Base Color"].toString() == "passthrough", "bakeInfo: texture -> Base Color is 'passthrough'");
        CHECK(info["Metallic"].toString() == "baked", "bakeInfo: texCoords chain -> Metallic is 'baked'");
        CHECK(info["Alpha Cutoff"].toString() == "unsupported", "bakeInfo: varying Alpha Cutoff is 'unsupported'");
        CHECK(info["Vertex Offset"].toString() == "unsupported", "bakeInfo: fed Vertex Offset is 'unsupported'");
        CHECK(info["Emissive"].toString() == "unconnected", "bakeInfo: unconnected socket is 'unconnected'");
        QFile::remove(texPath);
    }
    {
        const QString texPath = QDir::current().absoluteFilePath("parity_tex2.png");
        QImage img(4, 4, QImage::Format_RGBA8888);
        img.fill(QColor(10, 20, 30));
        img.save(texPath);

        Rig r; // textureSampler(texture, default UV) with no math == passthrough
        auto tex = r.add("texture");
        static_cast<TextureNode*>(tex)->setTexturePath(texPath);
        auto sampler = r.add("textureSampler");
        r.graph->addConnection(tex, 0, sampler, 0);
        r.toMaster(sampler, 0, 0);
        auto info = r.info();
        CHECK(info["Base Color"].toString() == "passthrough",
              "bakeInfo: textureSampler(texture, bake UV) is 'passthrough'");
        auto res = r.eval();
        CHECK(res.values["baseColorMap"].toString() == texPath,
              "bakeInfo: the passthrough binds the source image path");
        QFile::remove(texPath);
    }

    // ================= legacy Surface master (Shininess inversion) ============
    {
        Rig r(false);
        r.graph->addConnection(r.addFloat(0.22), 0, r.master, 2); // Shininess
        auto res = r.eval();
        CHECK(near(res.values["roughness"].toDouble(), 0.78, 1e-9),
              "legacy: Shininess 0.22 -> roughness 0.78 (gloss inversion)");
    }
    {
        Rig r(false);
        r.graph->addConnection(r.addFloat(50.0), 0, r.master, 2); // Blinn exponent range
        auto res = r.eval();
        CHECK(near(res.values["roughness"].toDouble(), 0.5, 1e-9),
              "legacy: Shininess 50 treated as 0-100 exponent -> roughness 0.5");
    }

    std::printf(failures ? "FAILED: %d check(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
