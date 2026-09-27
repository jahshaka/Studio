// pieces.mirror — the hop the other suites do not cover (HLMS_ADOPTION P5).
//
// shadergraph.emitter_parity proves the emitted GLSL is right, and pieces.spike
// proves the engine verb works. Between them sits the part the app actually
// depends on: a DOCUMENT material that carries a generated piece must reach the
// renderer through SceneMirror, and the mirror must push a clock so the surface
// moves. That is what this renders.
//
// The comparison is about MOTION, and the clock is FRAME-COUNTED: the mirror
// advances the shader clock by one grid step (1/60 s) per sync — the engine
// has no wall clock (trap 7) — unless a scrubbed timeline or a test pins it
// with setShaderTimeOverride. So what a real editor gets is "the surface
// changes on its own", by exactly one step a frame. The control is the same
// scene with a material that has no piece — its pixels must not move at all.
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "jahshaka/engine/Engine.h"

#include "bridge/enginematerialpreviewscene.h"
#include "irisgl/document/materials/pbrmaterial.h"

#include "modules/materials/core/bakeprogram.h"
#include "modules/materials/core/graphbaker.h"
#include "modules/materials/core/pieceemitter.h"
#include "modules/materials/graph/nodegraph.h"
#include "modules/materials/models/libraryv1.h"
#include "modules/materials/models/nodemodel.h"
#include "modules/materials/nodes/pbrmasternode.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

/// A pulsating-colour graph, and the material it emits to.
iris::PbrMaterialPtr pulsingMaterial(const QString &dir, QString *pathOut)
{
    auto lib = new LibraryV1();
    auto graph = new NodeGraph();
    graph->setNodeLibrary(lib);
    auto master = new PbrMasterNode();
    graph->addNode(master);
    graph->setMasterNode(master);

    auto colour = [&](double r, double g, double b) {
        auto n = lib->createNode("color");
        graph->addNode(n);
        QJsonObject obj; obj["r"] = r; obj["g"] = g; obj["b"] = b; obj["a"] = 1.0;
        n->deserializeWidgetValue(obj);
        return n;
    };
    auto mix = lib->createNode("lerp");
    graph->addNode(mix);
    auto pulse = lib->createNode("pulsate");
    graph->addNode(pulse);
    auto rate = lib->createNode("float");
    graph->addNode(rate);
    rate->deserializeWidgetValue(QJsonValue(6.0));
    graph->addConnection(rate, 0, pulse, 0);
    graph->addConnection(colour(0.05, 0.05, 0.6), 0, mix, 0);
    graph->addConnection(colour(0.95, 0.6, 0.05), 0, mix, 1);
    graph->addConnection(pulse, 0, mix, 2);
    graph->addConnection(mix, 0, master, 0);

    const auto emitted = materials::PieceEmitter::lower(graph);
    if (!emitted.accepted) { delete graph; return {}; }
    const QString path = materials::PieceEmitter::write(dir, emitted.pixelSource, false);
    if (pathOut) *pathOut = path;

    auto material = iris::PbrMaterial::create();
    material->setBaseColor(QColor::fromRgbF(0.5, 0.5, 0.5));
    material->setRoughnessFactor(0.6f);
    material->setMetallicFactor(0.0f);
    material->setCustomPiecePixel(path);
    delete graph;
    return material;
}

/// Renders for `seconds` of WALL clock, sampling the centre pixel, and returns
/// how far the samples spread. The mirror's clock is the wall clock unless a
/// caller pins it, so this is what a user sees.
int spread(EngineMaterialPreviewScene &preview, Engine &engine, View *view, int frames)
{
    int lo = 255, hi = 0;
    for (int f = 0; f < frames; ++f) {
        preview.step(1.0f / 60.0f, int(view->width()), int(view->height()));
        engine.renderOneFrame();
        Image img;
        if (view->readPixels(img)) {
            const Colour c = img.at(img.width / 2, img.height / 2);
            const int v = int(std::lround(c.r * 255));
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }
    std::printf("      samples r in [%d, %d]\n", lo, hi);
    return hi - lo;
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // widget-backed graph nodes need one
    QTemporaryDir tmp;

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_piece_mirror-ogre.log";
    std::string err;
    std::shared_ptr<Engine> engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("preview", 96, 96, Colour(0.1f, 0.1f, 0.1f));
    CHECK(view != nullptr, "offscreen view");
    if (!view) return 1;

    EngineMaterialPreviewScene preview(engine);
    CHECK(preview.attach(view), "preview scene + mirror attached");
    // The subject primitive is built by setPreviewMesh, not by attach: without
    // this the centre pixel is the preview's grey background and every
    // assertion below would be measuring nothing.
    CHECK(preview.setPreviewMesh(PreviewMesh::Cube), "cube primitive loaded");

    // WARM THE ENVIRONMENT FIRST (MATPREVIEW-ENV-1). The preview scene is lit
    // by a generated studio sky, and the engine captures that sky and convolves
    // it for reflections INSIDE the first rendered frames — so the first
    // fraction of a second after attach is the environment arriving, not an
    // animation. Measured: the control's "still" picture moved 5/255 over its
    // first 0.4 s and 0/255 once warm. The house rule, again: warm up, then
    // measure.
    for (int i = 0; i < 12; ++i) {
        preview.step(1.0f / 60.0f, int(view->width()), int(view->height()));
        engine->renderOneFrame();
    }

    // ---- the control FIRST: a plain material must not move ------------------
    auto plain = iris::PbrMaterial::create();
    plain->setBaseColor(QColor::fromRgbF(0.6, 0.3, 0.2));
    plain->setRoughnessFactor(0.6f);
    preview.setMaterial(plain);
    const int still = spread(preview, *engine, view, 24);
    std::printf("    plain material centre spread over 24 frames: %d/255\n", still);
    CHECK(still <= 2, "a material with no generated piece renders a still image");

    // ---- the subject: a graph material carrying a piece ---------------------
    QString piecePath;
    auto animated = pulsingMaterial(tmp.path(), &piecePath);
    CHECK(!animated.isNull(), "the emitter produced a piece for the pulsating graph");
    CHECK(!piecePath.isEmpty(), "...and wrote it to the cache directory");
    if (animated.isNull()) return 1;

    preview.setMaterial(animated);
    const int moving = spread(preview, *engine, view, 36);
    std::printf("    piece material centre spread over 36 frames: %d/255\n", moving);
    CHECK(moving >= 20,
          "the document material's piece reached the renderer through the mirror AND the "
          "mirror pushed a clock (the surface animates)");
    // THE CLOCK IS FRAME-COUNTED: N syncs advance it by exactly N grid steps,
    // however long the frames took on the wall.
    {
        const float before = preview.engineScene()->shaderTime();
        const int n = 30;
        for (int f = 0; f < n; ++f) {
            preview.step(1.0f / 60.0f, int(view->width()), int(view->height()));
            engine->renderOneFrame();
        }
        const float advanced = preview.engineScene()->shaderTime() - before;
        std::printf("    shader clock advanced %.6f s over %d frames\n", double(advanced), n);
        CHECK(std::fabs(advanced - float(n) / 60.0f) < 1e-4f,
              "the mirror's shader clock advances one 1/60 s grid step per frame (no wall clock)");
    }

    // ---- and back: dropping the piece stops the motion ----------------------
    animated->setCustomPiecePixel(QString());
    auto stopped = iris::PbrMaterial::create();
    stopped->setBaseColor(QColor::fromRgbF(0.6, 0.3, 0.2));
    stopped->setRoughnessFactor(0.6f);
    preview.setMaterial(stopped);
    const int stoppedSpread = spread(preview, *engine, view, 24);
    std::printf("    after swapping back to a plain material: %d/255\n", stoppedSpread);
    CHECK(stoppedSpread <= 2, "swapping back to a piece-less material stops the animation");

    preview.release();
    engine->destroyView(view);
    engine.reset();
    std::printf("%s\n", failures == 0 ? "pieces.mirror: all checks passed"
                                      : "pieces.mirror: FAILURES");
    return failures == 0 ? 0 : 1;
}
