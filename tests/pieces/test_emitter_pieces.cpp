// shadergraph.emitter_pieces — the emitter's NON-PIXEL contracts
// (HLMS_ADOPTION P5 §7.9): content-addressed naming, deduplication, the
// piece-collision allow-list, the fallback reasons a user is owed when their
// graph did not animate, and the baker hand-off.
//
// No engine, no display: everything here is about the bytes the emitter
// produces and the decisions it takes. The pixels are shadergraph.emitter_parity's
// job.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <cstdio>

#include "modules/materials/core/bakeprogram.h"
#include "modules/materials/core/graphbaker.h"
#include "modules/materials/core/pieceemitter.h"
#include "modules/materials/graph/nodegraph.h"
#include "modules/materials/models/libraryv1.h"
#include "modules/materials/models/nodemodel.h"
#include "modules/materials/nodes/pbrmasternode.h"

using materials::PieceEmitter;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

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
    ~Rig() { delete graph; }
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
    NodeModel *addColor(double r, double g, double b)
    {
        auto node = add("color");
        QJsonObject obj; obj["r"] = r; obj["g"] = g; obj["b"] = b; obj["a"] = 1.0;
        node->deserializeWidgetValue(obj);
        return node;
    }
    void connect(NodeModel *from, int out, NodeModel *to, int in)
    {
        graph->addConnection(from, out, to, in);
    }
    void toMaster(NodeModel *from, int out, int socket) { graph->addConnection(from, out, master, socket); }
    /// A pulsating colour: the smallest graph the emitter accepts.
    NodeModel *pulsingColour(double r, double g, double b, double rate = 3.0)
    {
        auto mix = add("lerp");
        connect(addColor(r, g, b), 0, mix, 0);
        connect(addColor(1.0, 1.0, 1.0), 0, mix, 1);
        auto pulse = add("pulsate");
        connect(addFloat(rate), 0, pulse, 0);
        connect(pulse, 0, mix, 2);
        return mix;
    }
};

/// Every piece NAME the source defines or undefines.
QSet<QString> definedPieces(const QString &source)
{
    QSet<QString> names;
    static const QRegularExpression re(QStringLiteral("@(?:un)?def?piece\\s*\\(\\s*([A-Za-z0-9_]+)"));
    // (the pattern above also matches @piece: "def?piece" makes the "def" part
    //  optional-tailed, so @piece / @defpiece / @undefpiece all land here)
    auto it = re.globalMatch(source);
    while (it.hasNext()) names.insert(it.next().captured(1));
    static const QRegularExpression plain(QStringLiteral("@piece\\s*\\(\\s*([A-Za-z0-9_]+)"));
    auto it2 = plain.globalMatch(source);
    while (it2.hasNext()) names.insert(it2.next().captured(1));
    return names;
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QTemporaryDir tmp;

    // ---------------------------------------------------------------- naming
    QString nameA, nameB;
    {
        Rig r;
        r.toMaster(r.pulsingColour(0.2, 0.4, 0.8), 0, 0);
        const auto first = PieceEmitter::lower(r.graph);
        CHECK(first.accepted, "an animated Base Color chain is accepted by the emitter");
        CHECK(!first.pixelSource.isEmpty(), "...and produces a pixel piece");
        CHECK(first.animated, "...reported as animated (the host must push the clock)");
        CHECK(first.emittedSockets == QStringList{ "Base Color" },
              "...owning exactly the Base Color socket");
        nameA = PieceEmitter::fileNameFor(first.pixelSource, false);

        // A SECOND, independently built graph with the same content must
        // produce the same bytes and therefore the same file: dedup is what
        // makes two identical materials share one shader permutation.
        Rig r2;
        r2.toMaster(r2.pulsingColour(0.2, 0.4, 0.8), 0, 0);
        const auto second = PieceEmitter::lower(r2.graph);
        CHECK(second.pixelSource == first.pixelSource,
              "the same graph emits byte-identical source (no node ids, no ordering wobble)");
        CHECK(PieceEmitter::fileNameFor(second.pixelSource, false) == nameA,
              "...and therefore the same content-addressed file name");
    }
    {
        // An EDIT is a different name. This is the whole cache-invalidation
        // story: the renderer keys its piece registry (and its disk cache) by
        // file name, so a changed graph can never collide with the old entry —
        // which also makes upstream's `&&`-instead-of-`||` revalidation bug
        // (OgreHlms.cpp:4112) unreachable for us.
        Rig r;
        r.toMaster(r.pulsingColour(0.2, 0.4, 0.9), 0, 0);   // 0.8 -> 0.9
        const auto edited = PieceEmitter::lower(r.graph);
        nameB = PieceEmitter::fileNameFor(edited.pixelSource, false);
        CHECK(nameB != nameA, "editing a node value changes the piece's file name");
        CHECK(nameA.endsWith(".piece_ps.glsl") && nameB.endsWith(".piece_ps.glsl"),
              "pixel pieces are named <hash>.piece_ps.glsl");
        CHECK(nameA.size() == 16 + QStringLiteral(".piece_ps.glsl").size(),
              "the name is a 16-hex content hash plus the suffix");
    }

    // ------------------------------------------------------- the write cache
    {
        Rig r;
        r.toMaster(r.pulsingColour(0.5, 0.1, 0.1), 0, 0);
        const auto res = PieceEmitter::lower(r.graph);
        const QString path = PieceEmitter::write(tmp.path(), res.pixelSource, false);
        CHECK(!path.isEmpty() && QFileInfo::exists(path), "write() lands the piece on disk");
        const QDateTime firstWrite = QFileInfo(path).lastModified();
        const QString again = PieceEmitter::write(tmp.path(), res.pixelSource, false);
        CHECK(again == path, "writing the same source again returns the same path");
        CHECK(QFileInfo(path).lastModified() == firstWrite,
              "...and does NOT rewrite the file (same name implies same content)");
        CHECK(!PieceEmitter::cacheDir().contains(QStringLiteral("BakedMaps")),
              "the piece cache is not the project's baked-map directory");

        // THE CACHE IS DISPOSABLE, and this is what that means: delete the
        // file and emit again — the same graph reproduces the same bytes, so
        // the same name comes back. Nothing anywhere has to remember a path.
        CHECK(QFile::remove(path), "the cached piece can be deleted");
        CHECK(!QFileInfo::exists(path), "...and it is gone");
        const QString regenerated = PieceEmitter::write(tmp.path(), res.pixelSource, false);
        CHECK(regenerated == path && QFileInfo::exists(path),
              "re-emitting a wiped cache restores the identical file (deterministic emission)");
    }

    // --------------------------------------------- the piece-collision rule
    {
        // Our Hlms library already owns custom_passBuffer, custom_VStoPS,
        // custom_ps_posExecution, custom_ps_uv_modifier_macros and an override
        // of DoAtmosphereNprSky, and a DUPLICATE piece name fails the whole
        // shader — one log line and a black frame. The emitter's output must
        // therefore define nothing but its two hooks. This is the gate for
        // that rule (HLMS_ADOPTION_SPEC §7.4).
        Rig r;
        r.toMaster(r.pulsingColour(0.3, 0.3, 0.6), 0, 0);
        auto offset = r.add("multiply");
        r.connect(r.add("time"), 0, offset, 0);
        r.connect(r.addFloat(0.1), 0, offset, 1);
        r.toMaster(offset, 0, 7);   // Vertex Offset
        const auto res = PieceEmitter::lower(r.graph);
        CHECK(res.accepted && !res.vertexSource.isEmpty(),
              "a Vertex Offset chain emits a VERTEX piece (the socket the baker never could)");
        const QSet<QString> allowed = { QStringLiteral("custom_ps_preLights"),
                                        QStringLiteral("custom_vs_preTransform") };
        const QSet<QString> ps = definedPieces(res.pixelSource);
        const QSet<QString> vs = definedPieces(res.vertexSource);
        CHECK(ps == QSet<QString>{ QStringLiteral("custom_ps_preLights") },
              "the pixel piece defines custom_ps_preLights and nothing else");
        CHECK(vs == QSet<QString>{ QStringLiteral("custom_vs_preTransform") },
              "the vertex piece defines custom_vs_preTransform and nothing else");
        CHECK((ps + vs).subtract(allowed).isEmpty(),
              "no generated piece can collide with the Hlms library's own names");
        CHECK(!res.pixelSource.contains(QStringLiteral("custom_VStoPS")) &&
                  !res.vertexSource.contains(QStringLiteral("custom_VStoPS")),
              "v1 emits no VS->PS interpolant (which would have to fight the fog piece)");
        CHECK(res.emittedSockets.contains(QStringLiteral("Vertex Offset")),
              "...and the vertex socket is reported as emitted");
    }

    // ------------------------------------------------------ fallback reasons
    {
        Rig r;   // a constant graph: the baker already lands it exactly
        r.toMaster(r.addColor(0.4, 0.4, 0.4), 0, 0);
        const auto res = PieceEmitter::lower(r.graph);
        CHECK(!res.accepted, "a constant-folding graph gets NO piece");
        CHECK(res.fallbackReasons.value(QStringLiteral("Base Color"))
                  .contains(QStringLiteral("folds to a constant")),
              "...and says why: a piece would add a permutation and change nothing");
    }
    {
        Rig r;   // fresnel: the CPU evaluates it against the fake context
        auto op = r.add("fresnel");
        r.connect(r.addFloat(2.0), 0, op, 1);
        r.toMaster(op, 0, 0);
        const auto res = PieceEmitter::lower(r.graph);
        CHECK(!res.accepted, "a fresnel chain stays on the baker");
        CHECK(res.fallbackReasons.value(QStringLiteral("Base Color"))
                  .contains(QStringLiteral("fake fragment context")),
              "...naming the fake fragment context as the reason");
    }
    {
        Rig r;   // a texture sampler: no semantic slot for a graph texture
        auto tex = r.add("texture");
        auto sampler = r.add("textureSampler");
        r.connect(tex, 0, sampler, 0);
        r.toMaster(sampler, 0, 0);
        const auto res = PieceEmitter::lower(r.graph);
        CHECK(!res.accepted, "a texture chain stays on the baker");
        CHECK(res.fallbackReasons.value(QStringLiteral("Base Color"))
                  .contains(QStringLiteral("samples a texture")),
              "...naming texture sampling as the reason");
    }
    {
        Rig r;   // Normal: no safe landing at this hook
        r.toMaster(r.pulsingColour(0.2, 0.2, 0.2), 0, 3);
        const auto res = PieceEmitter::lower(r.graph);
        CHECK(res.fallbackReasons.value(QStringLiteral("Normal")).contains(QStringLiteral("TANGENT")),
              "the Normal socket reports the tangent-space reason");
    }
    {
        Rig r;   // Emissive: the material buffer is read-only in the shader
        r.toMaster(r.pulsingColour(0.2, 0.2, 0.2), 0, 4);
        const auto res = PieceEmitter::lower(r.graph);
        CHECK(res.fallbackReasons.value(QStringLiteral("Emissive")).contains(QStringLiteral("READ-ONLY")),
              "the Emissive socket reports the read-only material buffer");
    }

    // ------------------------------------------------ the baker's hand-off
    {
        // A socket the piece owns must not also be baked: the piece overwrites
        // the surface after the maps are sampled, so a baked PNG would be work
        // nobody can see.
        // A UV-VARYING chain, because that is the one the baker turns into a
        // PNG. (An ANIMATED-but-uniform chain — a pulsating colour — folds to
        // its t=0 value instead, which is precisely the frozen clock the piece
        // exists to fix; the second block below pins that.)
        Rig r;
        auto sum = r.add("add");
        r.connect(r.add("texCoords"), 0, sum, 0);
        r.connect(r.addColor(0.2, 0.4, 0.8), 0, sum, 1);
        r.toMaster(sum, 0, 0);
        const auto compiled = materials::GraphBaker::compile(r.graph);
        const auto emitted = PieceEmitter::lower(compiled);
        // v1 SCOPE: a UV-varying chain with no clock in it stays on the baker
        // — the bake is exact and it is what web export consumes. So this
        // fixture must be REFUSED, by name.
        CHECK(!emitted.accepted, "a UV-varying-only Base Color chain stays on the baker");
        CHECK(emitted.fallbackReasons.value(QStringLiteral("Base Color"))
                  .contains(QStringLiteral("varies with UV but not with time")),
              "...and says so: the baker lands it exactly and export consumes its map");

        materials::GraphBaker::Options opts;
        opts.outputDir = tmp.path() + QStringLiteral("/bake");
        opts.relativePrefix = QStringLiteral("BakedMaps/x/");
        const auto withoutSkip = materials::GraphBaker::runCompiled(compiled, opts);
        // The hand-off itself is tested by DECLARING the socket emitted (which
        // is what an animated chain would do) and watching the bake disappear.
        opts.emittedSockets = QStringList{ QStringLiteral("Base Color") };
        const auto withSkip = materials::GraphBaker::runCompiled(compiled, opts);
        CHECK(withoutSkip.maps.contains(QStringLiteral("baseColorMap")),
              "without the emitter, a UV-varying Base Color bakes a map (the old behaviour)");
        CHECK(!withSkip.maps.contains(QStringLiteral("baseColorMap")),
              "with the piece owning the socket, the baker skips it entirely");
        CHECK(withSkip.eval.unsupportedNodes.isEmpty(),
              "...and does NOT report it unsupported — it is neither baked nor lost");
    }
    {
        // THE FROZEN CLOCK, stated as a test: an animated-but-uniform chain is
        // a CONSTANT to the baker (evaluated at t = 0) and a live surface to
        // the emitter. This is the single clearest statement of what P5 buys.
        Rig r;
        r.toMaster(r.pulsingColour(0.2, 0.4, 0.8), 0, 0);
        const auto compiled = materials::GraphBaker::compile(r.graph);
        materials::GraphBaker::Options opts;
        opts.bakeMaps = false;
        const auto folded = materials::GraphBaker::runCompiled(compiled, opts);
        CHECK(folded.eval.values.contains(QStringLiteral("baseColor")),
              "the baker folds a pulsating colour to ONE value (its t=0 sample)");
        CHECK(PieceEmitter::lower(compiled).animated,
              "the emitter takes the same chain as animated instead");
    }

    // ------------------------------------------------------- the op vocabulary
    {
        // The coverage table is part of the contract: a silently shrinking op
        // list would quietly move graphs back onto the baker.
        const QStringList &ops = PieceEmitter::supportedOps();
        CHECK(ops.size() >= 45, "the emitter lowers at least 45 op keys");
        for (const char *must : { "add", "lerp", "clamp", "pulsate", "time", "flipbook",
                                  "normalize", "smoothstep", "composevector" })
            CHECK(ops.contains(QString::fromLatin1(must)),
                  qPrintable(QStringLiteral("op '%1' is still lowered").arg(must)));
        for (const char *never : { "texture", "textureSampler", "texelsize",
                                   "propertyNormalSample", "worldNormal", "fresnel", "depth" })
            CHECK(!ops.contains(QString::fromLatin1(never)),
                  qPrintable(QStringLiteral("op '%1' is deliberately NOT lowered").arg(never)));
        CHECK(PieceEmitter::supportedSockets().size() == 5,
              "five master sockets have a piece landing (3 pixel, 2 vertex)");
    }

    std::printf("%s\n", failures == 0 ? "emitter_pieces: all checks passed"
                                      : "emitter_pieces: FAILURES");
    return failures == 0 ? 0 : 1;
}
