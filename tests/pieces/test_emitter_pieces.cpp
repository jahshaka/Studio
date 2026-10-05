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
#include "modules/materials/nodes/test.h"
#include <QImage>
#include <QJsonArray>
#include <cmath>

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
        // EMISSIVE (TORNADO-1, G2). It used to report the read-only material
        // buffer; the fork's custom_ps_emissive hook is its landing now — in a
        // LIVE graph. A static emissive keeps baking, with its reason.
        Rig r;
        r.toMaster(r.pulsingColour(0.2, 0.2, 0.2), 0, 4);
        const auto res = PieceEmitter::lower(r.graph);
        CHECK(res.emittedSockets.contains(QStringLiteral("Emissive")),
              "a pulsating Emissive is EMITTED (the fork's custom_ps_emissive hook)");
        CHECK(definedPieces(res.pixelSource) == QSet<QString>{ QStringLiteral("custom_ps_emissive") },
              "...into custom_ps_emissive and nothing else");
        CHECK(res.pixelSource.contains(QStringLiteral("finalColour += midf3_c(")),
              "...ADDING to the light DoEmissiveLight already accumulated");
        Rig s;
        s.toMaster(s.addColor(0.2, 0.2, 0.2), 0, 4);
        const auto still = PieceEmitter::lower(s.graph);
        CHECK(!still.live && !still.accepted && still.fallbackReasons.value(QStringLiteral("Emissive"))
                                                    .contains(QStringLiteral("STATIC")),
              "a STATIC emissive stays on the baker and says why");
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
        r.connect(r.add("uv"), 0, sum, 0);
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

    // ============================================== THE LIVE PATHS (TORNADO-1)
    // A noise image the fold fixtures sample.
    const QString noisePath = tmp.path() + QStringLiteral("/noise.png");
    {
        QImage noise(8, 8, QImage::Format_RGBA8888);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                noise.setPixelColor(x, y, QColor((x * 37 + y * 11) % 256, (x * 5 + y * 71) % 256, 90));
        noise.save(noisePath);
    }
    // texture( uv(tiling 1,2) -> panner(speed, time) ) — the Tornado's noise
    // lookup: the Texture node sampling itself at its UV input (D-2 option B1).
    // (Not textureSampler: its fold reads the UV at input index 1, but the
    // compiled op carries the UV at index 0 — a pre-existing mismatch that makes
    // a textureSampler never fold; reported, not changed here: changing it would
    // move STATIC graphs.)
    auto scrollingNoise = [&](Rig &r, double vx, double vy, bool withClock) {
        auto tex = r.add("texture");
        static_cast<TextureNode *>(tex)->setTexturePath(noisePath);
        auto tiling = r.add("vector2");
        QJsonObject t2; t2["x"] = 1.0; t2["y"] = 2.0;
        tiling->deserializeWidgetValue(t2);
        auto uv = r.add("uv");
        r.connect(tiling, 0, uv, 1);
        if (withClock) {
            auto speed = r.add("vector2");
            QJsonObject v2; v2["x"] = vx; v2["y"] = vy;
            speed->deserializeWidgetValue(v2);
            auto pan = r.add("panner");
            r.connect(uv, 0, pan, 0);
            r.connect(speed, 0, pan, 1);
            r.connect(pan, 0, tex, 0);
        }
        else {
            r.connect(uv, 0, tex, 0);
        }
        return tex;
    };

    // ---- G1: the animated UV fold
    {
        Rig r;
        auto sampler = scrollingNoise(r, -0.5, -0.25, true);
        auto tint = r.add("multiply");
        r.connect(sampler, 0, tint, 0);
        r.connect(r.addColor(1.0, 0.5, 0.035), 0, tint, 1);
        r.toMaster(tint, 0, 0);
        const auto compiled = materials::GraphBaker::compile(r.graph);
        CHECK(compiled.live, "G1: a panner on the clock makes the graph LIVE");
        if (!compiled.uvFold.valid)
            std::printf("      (fold reason: %s)\n", qPrintable(compiled.uvFold.reason));
        CHECK(compiled.uvFold.valid && compiled.uvFold.velocityX == -0.5 &&
                  compiled.uvFold.velocityY == -0.25 && compiled.uvFold.scaleX == 1.0 &&
                  compiled.uvFold.scaleY == 2.0,
              "G1: a constant-speed panner feeding the sampler folds to tiling (1,2) + velocity (-0.5,-0.25)");
        CHECK(!compiled.sockets[0].program.animated,
              "G1: the folded Base Color chain no longer reads the clock (the material scrolls)");
        materials::GraphBaker::Options opts;
        opts.outputDir = tmp.path() + QStringLiteral("/g1");
        opts.relativePrefix = QStringLiteral("x/");
        const auto baked = materials::GraphBaker::runCompiled(compiled, opts);
        const QJsonArray vel = baked.eval.values.value(QStringLiteral("textureVelocity")).toArray();
        CHECK(vel.size() == 2 && vel[0].toDouble() == -0.5 && vel[1].toDouble() == -0.25,
              "G1: the bake lands textureVelocity [-0.5, -0.25]");
        CHECK(baked.eval.values.value(QStringLiteral("textureScale")).toArray().size() == 2,
              "G1: ...beside the folded tiling");
        // THE FOLD IS EXACT: the baked map at the bake UV equals the unfolded
        // chain at uv*tiling + velocity*t, for any t — the shader adds
        // velocity*t after the transform, the panner added speed*t after it.
        const auto unfolded = [&] {
            // the same graph compiled with the panner's TIME pinned is not
            // possible through the graph, so evaluate the raw program instead
            Rig q;
            auto s2 = scrollingNoise(q, -0.5, -0.25, true);
            auto t2 = q.add("multiply");
            q.connect(s2, 0, t2, 0);
            q.connect(q.addColor(1.0, 0.5, 0.035), 0, t2, 1);
            q.toMaster(t2, 0, 0);
            return materials::BakeProgram::compile(q.master->inSockets[0], {});
        }();
        const auto &folded = compiled.sockets[0].program;
        double worst = 0.0;
        for (double t : { 0.0, 0.75, 1.5 }) {
            for (int i = 0; i < 16; ++i) {
                materials::EvalContext a, b;
                a.u = (i % 4 + 0.3) / 4.0; a.v = (i / 4 + 0.6) / 4.0; a.time = t;
                // the folded program at the TRANSFORMED uv
                b.u = a.u * 1.0 - 0.5 * t;
                b.v = a.v * 2.0 - 0.25 * t;
                const auto x = unfolded.evaluate(a), y = folded.evaluate(b);
                worst = std::max(worst, std::max(std::abs(x.x - y.x), std::abs(x.y - y.y)));
            }
        }
        CHECK(worst < 1e-9, qPrintable(QStringLiteral("G1: folded(uv*s + v*t) == unfolded(uv, t) "
                                                      "(worst %1)").arg(worst)));
    }
    {
        // A panner whose speed is COMPUTED, or whose time is not the plain
        // clock, cannot fold — it bakes, with its reason.
        Rig r;
        auto tex = r.add("texture");
        static_cast<TextureNode *>(tex)->setTexturePath(noisePath);
        auto pan = r.add("panner");
        auto twice = r.add("multiply");
        r.connect(r.add("time"), 0, twice, 0);
        r.connect(r.addFloat(2.0), 0, twice, 1);
        r.connect(twice, 0, pan, 2);
        r.connect(pan, 0, tex, 0);
        r.toMaster(tex, 0, 0);
        const auto compiled = materials::GraphBaker::compile(r.graph);
        CHECK(compiled.live && !compiled.uvFold.valid &&
                  compiled.uvFold.reason.contains(QStringLiteral("shader clock")),
              "G1: a panner on time*2 does not fold, and says why");
    }

    // ---- THE STATIC-GRAPH GUARD: a graph with no clock takes none of it
    {
        Rig r;
        auto sampler = scrollingNoise(r, 0.0, 0.0, false);
        auto tint = r.add("multiply");
        r.connect(sampler, 0, tint, 0);
        r.connect(r.addColor(1.0, 0.5, 0.035), 0, tint, 1);
        r.toMaster(tint, 0, 4);                       // Emissive
        auto rim = r.add("fresnel");
        r.connect(r.addFloat(2.0), 0, rim, 1);
        r.toMaster(rim, 0, 0);                        // Base Color
        const auto compiled = materials::GraphBaker::compile(r.graph);
        CHECK(!compiled.live, "guard: no clock anywhere -> the graph is STATIC");
        if (!compiled.uvFold.valid)
            std::printf("      (fold reason: %s)\n", qPrintable(compiled.uvFold.reason));
        CHECK(!compiled.sockets[4].split && !compiled.sockets[4].liveWhole,
              "guard: a static emissive is never split");
        CHECK(compiled.uvFold.valid && compiled.uvFold.velocityX == 0.0,
              "guard: its fold is the constant one it always was");
        const auto res = PieceEmitter::lower(compiled);
        CHECK(!res.live && !res.accepted && res.bakedReasons.isEmpty(),
              "guard: the emitter takes nothing and reports the BAKED route");
        CHECK(res.fallbackReasons.value(QStringLiteral("Base Color"))
                  .contains(QStringLiteral("fake fragment context")),
              "guard: a static fresnel keeps today's reason");
        materials::GraphBaker::Options opts;
        opts.outputDir = tmp.path() + QStringLiteral("/guard");
        opts.relativePrefix = QStringLiteral("x/");
        const auto baked = materials::GraphBaker::runCompiled(compiled, opts);
        CHECK(!baked.eval.values.contains(QStringLiteral("textureVelocity")),
              "guard: a static graph lands no velocity");
        CHECK(baked.maps.contains(QStringLiteral("emissiveMap")),
              "guard: its texture x colour emissive BAKES a map, as before");
    }

    // ---- the emissive SPLIT and its factor fold: the Tornado's emissive
    {
        Rig r;
        auto sampler = scrollingNoise(r, -0.5, -0.5, true);
        // noise x (colour x 6): the HDR constant is a constant CHAIN
        auto hot = r.add("multiply");
        r.connect(r.addColor(1.0, 0.5, 0.035), 0, hot, 0);
        r.connect(r.addFloat(6.0), 0, hot, 1);
        auto hdr = r.add("multiply");
        r.connect(sampler, 0, hdr, 0);
        r.connect(hot, 0, hdr, 1);
        auto rim = r.add("fresnel");
        r.connect(r.addFloat(1.17), 0, rim, 1);
        auto sum = r.add("add");
        r.connect(rim, 0, sum, 0);
        r.connect(hdr, 0, sum, 1);
        r.toMaster(sum, 0, 4);
        const auto compiled = materials::GraphBaker::compile(r.graph);
        const auto &em = compiled.sockets[4];
        CHECK(compiled.live && em.split, "split: fresnel + texture*colour splits (live term + map term)");
        bool liveHasTexture = false;
        for (const auto &op : em.live.ops)
            if (op.typeName == "textureSampler" || op.typeName == "texture") liveHasTexture = true;
        CHECK(!liveHasTexture, "split: the piece's half samples no texture");
        CHECK(em.hasFactor && std::abs(em.factor.x - 6.0) < 1e-3 && std::abs(em.factor.y - 3.0) < 1e-3,
              "split: the map half folds to map x constant (the factor fold), the constant = colour x 6");
        const auto res = PieceEmitter::lower(compiled);
        CHECK(res.emittedSockets.contains(QStringLiteral("Emissive")) && res.fallbackReasons.isEmpty(),
              "split: Emissive is EMITTED with NO fallback");
        CHECK(res.pixelSource.contains(QStringLiteral("pixelData.viewDir")),
              "split: the piece computes the REAL fresnel (the view direction)");
        materials::GraphBaker::Options opts;
        opts.outputDir = tmp.path() + QStringLiteral("/split");
        opts.relativePrefix = QStringLiteral("x/");
        opts.emittedSockets = res.emittedSockets;
        const auto withPiece = materials::GraphBaker::runCompiled(compiled, opts);
        opts.emittedSockets.clear();
        opts.outputDir = tmp.path() + QStringLiteral("/unsplit");
        const auto withoutPiece = materials::GraphBaker::runCompiled(compiled, opts);
        CHECK(withoutPiece.maps.contains(QStringLiteral("emissiveMap")),
              "split: a run whose emitter did NOT take Emissive bakes the WHOLE chain (today's path)");
        CHECK(withPiece.passthrough.contains(QStringLiteral("emissiveMap")),
              "split+factor: the noise binds as the emissive map itself");
        CHECK(withPiece.eval.values.value(QStringLiteral("emissiveIntensity")).toDouble() == 6.0,
              "split+factor: the HDR constant lands on emissiveIntensity (6), unclamped");
    }
    {
        // the factor fold proper: texture x constant directly under the sum
        Rig r;
        auto sampler = scrollingNoise(r, -0.5, -0.5, true);
        auto tint = r.add("multiply");
        r.connect(sampler, 0, tint, 0);
        r.connect(r.addColor(1.0, 0.5, 0.035), 0, tint, 1);
        auto rim = r.add("fresnel");
        r.connect(r.addFloat(1.0), 0, rim, 1);
        auto sum = r.add("add");
        r.connect(rim, 0, sum, 0);
        r.connect(tint, 0, sum, 1);
        r.toMaster(sum, 0, 4);
        const auto compiled = materials::GraphBaker::compile(r.graph);
        const auto res = PieceEmitter::lower(compiled);
        materials::GraphBaker::Options opts;
        opts.outputDir = tmp.path() + QStringLiteral("/factor");
        opts.relativePrefix = QStringLiteral("x/");
        opts.emittedSockets = res.emittedSockets;
        const auto baked = materials::GraphBaker::runCompiled(compiled, opts);
        const QJsonObject c = baked.eval.values.value(QStringLiteral("emissiveColor")).toObject();
        CHECK(compiled.sockets[4].hasFactor &&
                  baked.passthrough.value(QStringLiteral("emissiveMap")).toString() == noisePath,
              "factor: texture x colour lands the texture itself as the emissive map");
        CHECK(std::abs(c.value("g").toDouble() - 0.5) < 1e-3 &&
                  std::abs(c.value("b").toDouble() - 0.035) < 1e-3 &&
                  baked.eval.values.value(QStringLiteral("emissiveIntensity")).toDouble() == 1.0,
              "factor: ...with the colour (1, 0.5, 0.035) on the material");
        CHECK(!baked.maps.contains(QStringLiteral("emissiveMap")), "factor: and nothing is baked for it");
    }

    // ---- a LIVE graph's plain-texture Emissive is SERVED, not a fallback
    {
        Rig r;
        auto tex = r.add("texture");
        static_cast<TextureNode *>(tex)->setTexturePath(noisePath);
        r.toMaster(tex, 0, 4);                        // Emissive: a bare texture
        r.toMaster(r.pulsingColour(0.2, 0.4, 0.8), 0, 0);   // the clock lives elsewhere
        const auto res = PieceEmitter::lower(r.graph);
        CHECK(res.live && !res.fallbackReasons.contains(QStringLiteral("Emissive")) &&
                  res.bakedReasons.contains(QStringLiteral("Emissive")),
              "live graph: a plain-texture Emissive is reported BAKED (served exactly), not a fallback");
    }

    // ---- G3: the live-only ops lower in a live graph, with their stage limits
    {
        Rig r;
        auto rim = r.add("fresnel");
        r.connect(r.addFloat(2.0), 0, rim, 1);
        auto sum = r.add("add");
        r.connect(rim, 0, sum, 0);
        auto zero = r.add("multiply");
        r.connect(r.add("time"), 0, zero, 0);
        r.connect(r.addFloat(0.0), 0, zero, 1);
        r.connect(zero, 0, sum, 1);
        r.toMaster(sum, 0, 0);
        const auto res = PieceEmitter::lower(r.graph);
        CHECK(res.live && res.emittedSockets.contains(QStringLiteral("Base Color")),
              "G3: fresnel in a live graph lowers to its real value");
        Rig v;
        auto nrm = v.add("localNormal");
        auto k = v.add("multiply");
        v.connect(nrm, 0, k, 0);
        v.connect(v.add("time"), 0, k, 1);
        v.toMaster(k, 0, 7);
        const auto vres = PieceEmitter::lower(v.graph);
        CHECK(vres.emittedSockets.contains(QStringLiteral("Vertex Offset")) &&
                  vres.vertexSource.contains(QStringLiteral("jahLocalNormal")),
              "G3: localNormal lowers in the vertex stage");
        Rig p;
        auto pn = p.add("localNormal");
        auto pk = p.add("multiply");
        p.connect(pn, 0, pk, 0);
        p.connect(p.add("time"), 0, pk, 1);
        p.toMaster(pk, 0, 0);
        const auto pres = PieceEmitter::lower(p.graph);
        CHECK(pres.fallbackReasons.value(QStringLiteral("Base Color")).contains(QStringLiteral("VERTEX stage")),
              "G3: localNormal in the pixel stage is refused with the stage reason");
    }
    {
        // A vertex piece that reads no normal is byte-identical to before: no
        // normal preamble (its file name is a hash of its bytes).
        Rig r;
        auto offset = r.add("multiply");
        r.connect(r.add("time"), 0, offset, 0);
        r.connect(r.addFloat(0.1), 0, offset, 1);
        r.toMaster(offset, 0, 7);
        const auto res = PieceEmitter::lower(r.graph);
        CHECK(!res.vertexSource.contains(QStringLiteral("jahWorldNormal")),
              "guard: a vertex piece that reads no normal declares none");
    }

    // ------------------------------------------------------- the op vocabulary
    {
        // The coverage table is part of the contract: a silently shrinking op
        // list would quietly move graphs back onto the baker.
        const QStringList &ops = PieceEmitter::supportedOps();
        CHECK(ops.size() >= 44, "the emitter lowers at least 44 op keys (the retired texCoords/uvTransform aliases are gone)");
        for (const char *must : { "add", "lerp", "clamp", "pulsate", "time", "flipbook",
                                  "normalize", "smoothstep", "composevector" })
            CHECK(ops.contains(QString::fromLatin1(must)),
                  qPrintable(QStringLiteral("op '%1' is still lowered").arg(must)));
        for (const char *never : { "texture", "textureSampler", "texelsize",
                                   "propertyNormalSample", "depth" })
            CHECK(!ops.contains(QString::fromLatin1(never)),
                  qPrintable(QStringLiteral("op '%1' is deliberately NOT lowered").arg(never)));
        // THE PER-OP DIVERGENCE RULE (TORNADO-1, G3): lowered in a LIVE graph only.
        for (const char *liveOnly : { "fresnel", "worldNormal", "localNormal" })
            CHECK(ops.contains(QString::fromLatin1(liveOnly)) &&
                      materials::BakeProgram::liveOnlyOps().contains(QString::fromLatin1(liveOnly)),
                  qPrintable(QStringLiteral("op '%1' lowers in a live graph only").arg(liveOnly)));
        CHECK(PieceEmitter::supportedSockets().size() == 6,
              "six master sockets have a piece landing (4 pixel incl. Emissive, 2 vertex)");
    }

    std::printf("%s\n", failures == 0 ? "emitter_pieces: all checks passed"
                                      : "emitter_pieces: FAILURES");
    return failures == 0 ? 0 : 1;
}
