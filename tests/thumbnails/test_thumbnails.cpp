// Thumbnails through the engine, main thread, offscreen: a cube.obj with a known
// DefaultMaterial colour renders to a QImage of the requested size whose centre is
// the material, not the background; a second colour differs; a third request
// identical to the first reproduces it (nothing leaks between requests).
#include <QGuiApplication>
#include <QColor>
#include <QImage>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <memory>
#include "irisgl/irisglfwd.h"
#include "irisgl/document/assets/mesh.h"          // MeshMaterialData
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "bridge/enginethumbnailrenderer.h"
#include "jahshaka/engine/Engine.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool isBackground(QColor c)
{
    const Colour bg = EngineThumbnailRenderer::backgroundColour();
    return std::abs(c.redF() - bg.r) < 0.04f && std::abs(c.greenF() - bg.g) < 0.04f && std::abs(c.blueF() - bg.b) < 0.04f;
}
static QColor centre(const QImage &img) { return img.pixelColor(img.width() / 2, img.height() / 2); }
static void show(const char *tag, const QImage &img)
{
    const QColor c = img.isNull() ? QColor() : centre(img), k = img.isNull() ? QColor() : img.pixelColor(2, 2);
    std::printf("    %-24s %dx%d centre %3d %3d %3d   corner %3d %3d %3d\n", tag, img.width(), img.height(),
                c.red(), c.green(), c.blue(), k.red(), k.green(), k.blue());
}
static QImage thumbnail(EngineThumbnailRenderer &r, QColor diffuse, QSize size)
{
    // Exactly what ThumbnailGenerator's Mesh path builds: a MeshNode with a DefaultMaterial.
    auto node = iris::MeshNode::create();
    node->setMesh(":assets/models/cube.obj");
    auto mat = iris::DefaultMaterial::create();
    mat->setDiffuseColor(diffuse);
    node->setMaterial(mat);
    return r.renderNode(node, size);
}
static int maxAbsDiff(const QImage &a, const QImage &b)
{
    int d = 0;
    for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) {
        const QColor p = a.pixelColor(x, y), q = b.pixelColor(x, y);
        d = std::max({d, std::abs(p.red() - q.red()), std::abs(p.green() - q.green()), std::abs(p.blue() - q.blue())});
    }
    return d;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    EngineConfig cfg; cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR; cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR; cfg.logFile = "test_thumbnails-ogre.log";
    std::string err;
    std::shared_ptr<Engine> engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine"); if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }
    // The app's main viewport exists before any thumbnail is asked for; mimic that
    // so the thumbs View is not the engine's first render target.
    View *primary = engine->createOffscreenView("primary", 64, 64, Colour(0, 0, 0));
    Scene *primaryScene = engine->createScene("primary");
    primary->setScene(primaryScene);

    {
        EngineThumbnailRenderer renderer(engine);
        const QSize size(96, 96);

        // 1. red cube
        QImage a = thumbnail(renderer, QColor(220, 30, 30), size); show("red cube", a);
        CHECK(!a.isNull(), "thumbnail is non-null");
        CHECK(a.size() == size, "thumbnail has the requested size");
        const QColor ca = centre(a);
        CHECK(!isBackground(ca), "centre pixel is not the background");
        CHECK(ca.red() > ca.green() + 40 && ca.red() > ca.blue() + 40, "centre is dominated by the material colour (red)");
        CHECK(isBackground(a.pixelColor(2, 2)), "corner is the background (cube framed inside the view)");

        // 2. blue cube differs
        QImage b = thumbnail(renderer, QColor(30, 30, 220), size); show("blue cube", b);
        const QColor cb = centre(b);
        CHECK(cb.blue() > cb.red() + 40 && cb.blue() > cb.green() + 40, "second thumbnail is dominated by blue");
        CHECK(maxAbsDiff(a, b) > 40, "two colours give two different thumbnails");

        // 3. red again == first (nothing leaked from the blue request)
        QImage c = thumbnail(renderer, QColor(220, 30, 30), size); show("red cube again", c);
        const int d = maxAbsDiff(a, c);
        std::printf("    max |a - c| = %d\n", d);
        CHECK(d <= 2, "third request identical to the first reproduces it (no leak across requests)");

        // 4. a different size resizes the shared offscreen view
        QImage e = thumbnail(renderer, QColor(30, 220, 30), QSize(48, 64)); show("green 48x64", e);
        CHECK(e.size() == QSize(48, 64), "a second size is honoured");
        CHECK(centre(e).green() > centre(e).red() + 40, "and renders the material");

        // 5. the material preview sphere path
        auto mat = iris::DefaultMaterial::create(); mat->setDiffuseColor(QColor(230, 200, 20));
        QImage m = renderer.renderMaterial(mat, size); show("material sphere", m);
        CHECK(!m.isNull() && m.size() == size, "material preview renders at the requested size");
        CHECK(centre(m).red() > centre(m).blue() + 40 && centre(m).green() > centre(m).blue() + 40, "material sphere shows the material colour");

        // 6. a huge model (ASSETS_AUDIT.md finding 3): a cube scaled x200 has a
        // world radius of ~346 (a cm-scaled Sketchfab glb) and is framed ~1000
        // units out — beyond the old fixed farClip of 500, which rendered a
        // uniform-background thumbnail. The clip planes must follow the framing.
        {
            auto giant = iris::MeshNode::create();
            giant->setMesh(":assets/models/cube.obj");
            auto gm = iris::DefaultMaterial::create();
            gm->setDiffuseColor(QColor(220, 30, 30));
            giant->setMaterial(gm);
            giant->setLocalScale(QVector3D(200.0f, 200.0f, 200.0f));
            QImage h = renderer.renderNode(giant, size); show("giant cube x200", h);
            const QColor ch = centre(h);
            CHECK(!isBackground(ch), "a huge model still renders (far plane follows the framing)");
            CHECK(ch.red() > ch.green() + 40 && ch.red() > ch.blue() + 40, "giant cube shows its material colour");
            CHECK(isBackground(h.pixelColor(2, 2)), "giant cube is framed inside the view");
        }

        // 7. a textured model must NOT come out greyscale: the Mesh path's material
        // factory keeps the diffuse map (it used to drop textures — grey thumbnails
        // for every imported model whose colour lives in its texture).
        {
            QImage tex(64, 64, QImage::Format_RGBA8888);
            tex.fill(QColor(255, 60, 0));                    // saturated orange
            const QString texPath = QStringLiteral("test_thumbnails_texture.png");
            CHECK(tex.save(texPath), "test texture written");

            iris::MeshMaterialData data;                     // what assimp reports for the model
            data.shininess = 8.0f;
            data.diffuseTexture = texPath;
            auto mat = EngineThumbnailRenderer::previewMaterialForMeshData(data);
            CHECK(!mat.isNull(), "mesh-data factory returns a material");

            auto node = iris::MeshNode::create();
            node->setMesh(":assets/models/cube.obj");
            node->setMaterial(mat);
            QImage t = renderer.renderNode(node, size); show("textured cube", t);
            const QColor ct = centre(t);
            CHECK(!isBackground(ct), "textured cube renders");
            const int variance = std::abs(ct.red() - ct.green()) + std::abs(ct.green() - ct.blue());
            std::printf("    centre channel variance = %d\n", variance);
            CHECK(variance > 60, "thumbnail shows the texture's colour, not greyscale");
            CHECK(ct.red() > ct.blue() + 60, "and the colour is the texture's (red-dominant)");
        }

        // 8. the primary view is untouched: still its own clear colour, nothing of the thumbs scene
        Image pimg; primary->readPixels(pimg);
        const Colour pc = pimg.at(32, 32);
        CHECK(pc.r < 0.05f && pc.g < 0.05f && pc.b < 0.05f, "the primary view did not render the thumbs scene");

        renderer.release();
    }
    engine->destroyView(primary);
    engine->destroyScene(primaryScene);
    engine.reset();
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
