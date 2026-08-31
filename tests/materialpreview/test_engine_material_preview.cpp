// The Effects/Materials Display preview on the engine, headless: the graph
// evaluator's output type (iris::PbrMaterial) goes through
// EngineMaterialPreviewScene (matpreview Scene + SceneMirror + the primitive
// document) into an offscreen View. A red material must show red at the
// centre from the default orbit; switching primitives must keep showing it
// (the mirror re-attaches only on node/material change — the node-swap rule);
// a green material must show green; the background colour must reach the
// corner pixel; release() must detach cleanly.
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QImage>
#include <QJsonObject>
#include <QJsonValue>
#include <cmath>
#include <cstdio>
#include <string>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "jahshaka/engine/Engine.h"
#include "bridge/enginematerialpreviewscene.h"

// Materials Evaluator phase 3: a graph baked by GraphBaker must reach pixels.
#include "modules/materials/graph/nodegraph.h"
#include "modules/materials/models/libraryv1.h"
#include "modules/materials/models/nodemodel.h"
#include "modules/materials/nodes/pbrmasternode.h"
#include "modules/materials/nodes/test.h"
#include "modules/materials/core/graphbaker.h"
#include "modules/materials/core/pbrgraphevaluator.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// The preview's default background is the legacy grey (125,125,125): no hue.
static bool isGrey(const Colour &c)
{
    return std::fabs(c.r - c.g) < 0.05f && std::fabs(c.g - c.b) < 0.05f && c.r > 0.3f && c.r < 0.7f;
}
static bool isRed(const Colour &c)   { return c.r > 0.12f && c.r > c.b * 1.5f && c.r > c.g * 1.5f; }
static bool isGreen(const Colour &c) { return c.g > 0.12f && c.g > c.r * 1.5f && c.g > c.b * 1.5f; }
static bool isBlue(const Colour &c)  { return c.b > 0.12f && c.b > c.r * 1.5f && c.b > c.g * 1.5f; }
static void show(const char *tag, const Image &i, int x, int y)
{
    const Colour c = i.at(unsigned(x), unsigned(y)), k = i.at(2, 2);
    std::printf("    %-40s (%d,%d) %3.0f %3.0f %3.0f   corner %3.0f %3.0f %3.0f\n", tag, x, y,
                c.r*255, c.g*255, c.b*255, k.r*255, k.g*255, k.b*255);
}
static Image render(EngineMaterialPreviewScene &preview, Engine &engine, View *view, int frames = 3)
{
    for (int i = 0; i < frames; ++i) {
        preview.step(1.0f / 60.0f, int(view->width()), int(view->height()));
        engine.renderOneFrame();
    }
    Image img;
    view->readPixels(img);
    return img;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv); // widget-backed graph nodes need QApplication

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_engine_material_preview-ogre.log";
    std::string err;
    std::shared_ptr<Engine> engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    // The editor viewport exists first in the app; the Display preview is
    // another view and another scene on the same engine.
    const int W = 128, H = 128;
    View *editorView = engine->createOffscreenView("editor", 64, 64, Colour(0, 0, 0));
    Scene *editorScene = engine->createScene("editor");
    editorView->setScene(editorScene);
    View *view = engine->createOffscreenView("matpreview", W, H, Colour(0, 0, 1));
    CHECK(view != nullptr, "offscreen matpreview view");
    if (!view) return 1;

    const int CX = W / 2, CY = H / 2;
    {
        EngineMaterialPreviewScene preview(engine);
        CHECK(!!preview.document() && !!preview.camera(), "preview document and camera built without GL");
        CHECK(preview.attach(view), "matpreview scene attached to the view");
        CHECK(view->scene() == preview.engineScene(), "the view renders the MATPREVIEW scene");
        CHECK(preview.engineScene() != editorScene, "the matpreview scene is its own scene on the engine");

        // ---- 1. a red PbrMaterial (what the graph evaluator emits) on the sphere ----
        auto red = iris::PbrMaterial::create();
        red->setBaseColor(QColor(220, 30, 30));
        preview.setMaterial(red);
        Image img = render(preview, *engine, view);
        show("red PbrMaterial on the sphere", img, CX, CY);
        CHECK(isRed(img.at(CX, CY)), "centre is red (sphere carries the material)");
        CHECK(isGrey(img.at(2, 2)), "corner is the legacy grey background");

        // ---- 2. primitive switches keep the material ----
        CHECK(preview.setPreviewMesh(PreviewMesh::Cube), "cube primitive loaded");
        img = render(preview, *engine, view);
        show("same red material on the cube", img, CX, CY);
        CHECK(isRed(img.at(CX, CY)), "centre is still red after switching to the cube");

        // ---- 3. a green material replaces the red one ----
        auto green = iris::PbrMaterial::create();
        green->setBaseColor(QColor(25, 210, 30));
        preview.setMaterial(green);
        img = render(preview, *engine, view);
        show("green PbrMaterial on the cube", img, CX, CY);
        CHECK(isGreen(img.at(CX, CY)), "centre turns green with the new material");

        // ---- 4. the rest of the Model menu renders (green stays applied) ----
        CHECK(preview.setPreviewMesh(PreviewMesh::Plane), "plane primitive loaded");
        img = render(preview, *engine, view);
        show("plane", img, CX, CY);
        CHECK(isGreen(img.at(CX, CY)), "plane shows the material at the centre");

        CHECK(preview.setPreviewMesh(PreviewMesh::Cylinder), "cylinder primitive loaded");
        img = render(preview, *engine, view);
        show("cylinder", img, CX, CY);
        CHECK(isGreen(img.at(CX, CY)), "cylinder shows the material at the centre");

        CHECK(preview.setPreviewMesh(PreviewMesh::Capsule), "capsule primitive loaded");
        img = render(preview, *engine, view);
        show("capsule", img, CX, CY);
        CHECK(isGreen(img.at(CX, CY)), "capsule shows the material at the centre");

        // The torus has a hole at the centre; it just has to render.
        CHECK(preview.setPreviewMesh(PreviewMesh::Torus), "torus primitive loaded");
        img = render(preview, *engine, view);
        show("torus", img, CX, CY);
        CHECK(img.width == unsigned(W) && img.height == unsigned(H), "torus renders");

        // ---- 5. background colour (the Background menu) ----
        preview.setPreviewMesh(PreviewMesh::Sphere);
        preview.setBackground(QColor(10, 10, 200));
        img = render(preview, *engine, view);
        show("blue background, sphere", img, CX, CY);
        CHECK(isBlue(img.at(2, 2)), "corner takes the background colour");
        CHECK(isGreen(img.at(CX, CY)), "sphere still shows the material over it");

        // ---- 6. the orbit is the same path the mouse takes ----
        preview.orbit(180.0f, 0.0f);
        img = render(preview, *engine, view, 8);
        show("orbited 180 degrees", img, CX, CY);
        CHECK(isGreen(img.at(CX, CY)), "subject stays centred through a 180-degree orbit");

        // ---- 7. a BAKED map reaches pixels (Materials Evaluator phase 3) ----
        // A solid-red source sampled through math classifies Baked, GraphBaker
        // writes the PNG, and the resulting PbrMaterial must render red over
        // the blue background from step 5.
        {
            const QString bakeDir = QDir::current().absoluteFilePath("matpreview_bake_scratch");
            QDir(bakeDir).removeRecursively();
            const QString srcPath = bakeDir + "/red_src.png";
            QDir().mkpath(bakeDir);
            QImage src(8, 8, QImage::Format_RGBA8888);
            src.fill(QColor(230, 20, 20));
            CHECK(src.save(srcPath), "baked-map: red source texture written");

            LibraryV1 lib;
            NodeGraph graphDoc;
            graphDoc.setNodeLibrary(&lib);
            auto *graphMaster = new PbrMasterNode();
            graphDoc.addNode(graphMaster);
            graphDoc.setMasterNode(graphMaster);
            auto *tex = lib.createNode("texture");
            static_cast<TextureNode *>(tex)->setTexturePath(srcPath);
            graphDoc.addNode(tex);
            auto *sampler = lib.createNode("textureSampler");
            graphDoc.addNode(sampler);
            auto *uv = lib.createNode("texCoords");
            graphDoc.addNode(uv);
            auto *mul = lib.createNode("multiply");
            graphDoc.addNode(mul);
            auto *one = lib.createNode("float");
            one->deserializeWidgetValue(QJsonValue(1.0));
            graphDoc.addNode(one);
            graphDoc.addConnection(tex, 0, sampler, 0);
            graphDoc.addConnection(uv, 0, sampler, 1);
            graphDoc.addConnection(sampler, 0, mul, 0);
            graphDoc.addConnection(one, 0, mul, 1);
            graphDoc.addConnection(mul, 0, graphMaster, 0); // Base Color

            materials::GraphBaker::Options opts;
            opts.resolution = 64;
            opts.outputDir = bakeDir;
            opts.relativePrefix = bakeDir + "/";
            const auto baked = materials::GraphBaker::run(&graphDoc, opts);
            CHECK(baked.maps.contains("baseColorMap"), "baked-map: math chain baked a baseColorMap PNG");

            auto bakedMaterial = PbrGraphEvaluator::materialFromValues(baked.eval.values);
            CHECK(!!bakedMaterial && bakedMaterial->useBaseColorMap,
                  "baked-map: material carries the baked map");
            preview.setMaterial(bakedMaterial);
            img = render(preview, *engine, view, 8);
            show("BAKED baseColorMap on the sphere", img, CX, CY);
            CHECK(isRed(img.at(CX, CY)), "baked map reaches pixels: centre is the baked red");
            CHECK(isBlue(img.at(2, 2)), "background stays blue behind the baked-map sphere");
            QDir(bakeDir).removeRecursively();
        }

        preview.release();
        CHECK(view->scene() == nullptr, "release() detached the scene from the view");
    }

    engine->destroyView(view);
    editorView->setScene(nullptr);
    engine->destroyScene(editorScene);
    engine->destroyView(editorView);
    engine.reset();

    std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}
