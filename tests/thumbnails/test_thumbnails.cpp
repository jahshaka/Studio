// Thumbnails through the engine, main thread, offscreen: a cube.obj with a known
// DefaultMaterial colour renders to a QImage of the requested size whose centre is
// the material, not the background; a second colour differs; a third request
// identical to the first reproduces it (nothing leaks between requests).
#include "../support/previewdump.h"

#include "tests/support/testmesh.h"
#include "irisgl/core/math/vec.h"
#include <QGuiApplication>
#include <QColor>
#include <QImage>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <memory>
#include <type_traits>
#include <QtGlobal>
#include "irisgl/irisglfwd.h"
#include "irisgl/document/assets/mesh.h"          // MeshMaterialData
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "bridge/enginethumbnailrenderer.h"
#include "jahshaka/engine/Engine.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// THE BACKDROP IS A PICTURE NOW (MATPREVIEW-ENV-1): a thumbnail is a subject
// in the ONE generated studio environment, so behind it is that room's neutral
// wall — a graded gradient — and not the view's flat clear colour. "Is this the
// background" is therefore NEUTRALITY: the room is neutral by construction (a
// studio that tints lies about a material's colour), and every coloured subject
// in this suite is not. A neutral subject (the white cube below) is separated
// by brightness instead, where it is measured.
static bool isNeutral(QColor c, int tolerance = 14)
{
    return std::abs(c.red() - c.green()) <= tolerance
        && std::abs(c.green() - c.blue()) <= tolerance
        && std::abs(c.red() - c.blue()) <= tolerance;
}
// A SECOND RENDERER CANNOT BE CONSTRUCTED — as a compile-time fact, which is
// what "one per process" has to be to stay true (THUMBS-1).
static_assert(!std::is_constructible<EngineThumbnailRenderer,
                                     std::shared_ptr<jahshaka::engine::Engine>>::value,
              "EngineThumbnailRenderer must not be publicly constructible: it is borrowed");

static QColor centre(const QImage &img) { return img.pixelColor(img.width() / 2, img.height() / 2); }
static void show(const char *tag, const QImage &img)
{
    previewdump::save(tag, img);
    const QColor c = img.isNull() ? QColor() : centre(img), k = img.isNull() ? QColor() : img.pixelColor(2, 2);
    std::printf("    %-24s %dx%d centre %3d %3d %3d   corner %3d %3d %3d\n", tag, img.width(), img.height(),
                c.red(), c.green(), c.blue(), k.red(), k.green(), k.blue());
}
static QImage thumbnail(EngineThumbnailRenderer &r, QColor diffuse, QSize size)
{
    // Exactly what ThumbnailGenerator's Mesh path builds: a MeshNode with a DefaultMaterial.
    auto node = iris::MeshNode::create();
    node->setMesh(testmesh::load(":assets/models/cube.obj"));
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
        // THE ONE RENDERER, BORROWED (THUMBS-1): the type has no public
        // constructor any more — a second thumbnail renderer would fail to
        // create its View and silently draw nothing.
        auto loan = EngineThumbnailRenderer::borrow(engine, "the thumbnails suite");
        CHECK(bool(loan), "the thumbnail renderer can be borrowed");
        if (!loan) return 1;
        EngineThumbnailRenderer &renderer = *loan;
        const QSize size(96, 96);

        // 0. THERE IS ONE RENDERER (THUMBS-1). A second live one was the
        // owner's grey tiles: two instances, one fixed View name, the second
        // failing createOffscreenView and returning a null image in silence.
        // The type has no public constructor, so a second one cannot be
        // written at all (the static_assert at the top of main), and a second
        // BORROW while this one is out is refused BY NAME rather than handed
        // the same Scene, View and mirror.
        {
            auto second = EngineThumbnailRenderer::borrow(engine, "a second borrower");
            CHECK(!second, "a second borrow while one is out is refused");
            CHECK(second.reason().contains("busy"),
                  "…and says why, naming the caller");
            std::printf("    refusal: %s\n", qUtf8Printable(second.reason()));
        }

        // 1. red cube
        QImage a = thumbnail(renderer, QColor(220, 30, 30), size); show("red cube", a);
        CHECK(!a.isNull(), "thumbnail is non-null");
        CHECK(a.size() == size, "thumbnail has the requested size");
        const QColor ca = centre(a);
        CHECK(!isNeutral(ca), "centre pixel is the material, not the neutral backdrop");
        CHECK(ca.red() > ca.green() + 40 && ca.red() > ca.blue() + 40, "centre is dominated by the material colour (red)");
        CHECK(isNeutral(a.pixelColor(2, 2)), "corner is the studio wall (cube framed inside the view)");

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
            giant->setMesh(testmesh::load(":assets/models/cube.obj"));
            auto gm = iris::DefaultMaterial::create();
            gm->setDiffuseColor(QColor(220, 30, 30));
            giant->setMaterial(gm);
            giant->setLocalScale(iris::Vec3(200.0f, 200.0f, 200.0f));
            QImage h = renderer.renderNode(giant, size); show("giant cube x200", h);
            const QColor ch = centre(h);
            CHECK(!isNeutral(ch), "a huge model still renders (far plane follows the framing)");
            CHECK(ch.red() > ch.green() + 40 && ch.red() > ch.blue() + 40, "giant cube shows its material colour");
            CHECK(isNeutral(h.pixelColor(2, 2)), "giant cube is framed inside the view");
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
            node->setMesh(testmesh::load(":assets/models/cube.obj"));
            node->setMaterial(mat);
            QImage t = renderer.renderNode(node, size); show("textured cube", t);
            const QColor ct = centre(t);
            CHECK(!isNeutral(ct), "textured cube renders");
            const int variance = std::abs(ct.red() - ct.green()) + std::abs(ct.green() - ct.blue());
            std::printf("    centre channel variance = %d\n", variance);
            CHECK(variance > 60, "thumbnail shows the texture's colour, not greyscale");
            CHECK(ct.red() > ct.blue() + 60, "and the colour is the texture's (red-dominant)");
        }

        // 7b. THE SECONDARY-SURFACE TONEMAP (owner report 2026-09-07, item 6).
        //
        // A thumbnail used to be a raw linear readback: everything above 1.0
        // clipped to 255, so a brightly-lit world photographed as a white card
        // while the viewport beside it, which tonemaps, rolled the highlight
        // off. bridge/secondarysurfacetonemap.h turns the deterministic filmic
        // grade on for this renderer.
        //
        // WHAT THIS CASE CAN AND CANNOT SHOW. renderNode's studio lighting is
        // tuned so an ordinary asset does not blow out — which is precisely why
        // the defect survived here and bit on SCENE thumbnails and screenshots
        // of real worlds instead. So the clipping proof lives where it can be
        // constructed: test_engine's fixed_exposure_tonemap case, which renders
        // a violently lit scene with the grade on and off and compares the
        // histograms. What THIS case pins is that the grade is applied at all
        // (the highlight rolls down measurably) and that it never introduces
        // clipping of its own.
        {
            auto hot = iris::MeshNode::create();
            hot->setMesh(testmesh::load(":assets/models/cube.obj"));
            auto hm = iris::DefaultMaterial::create();
            hm->setDiffuseColor(QColor(255, 255, 255));
            hot->setMaterial(hm);
            QImage w = renderer.renderNode(hot, size); show("white cube", w);

            // THE WHOLE PICTURE, environment included (MATPREVIEW-ENV-1): a
            // white cube is as neutral as the wall behind it, so there is no
            // filtering the subject out by colour — and there is no need to.
            // What the grade has to hold is that NOTHING in the frame clips,
            // the room's own softbox panels included.
            int saturated = 0, lit = 0, maxChannel = 0;
            for (int y = 0; y < w.height(); ++y) for (int x = 0; x < w.width(); ++x) {
                const QColor p = w.pixelColor(x, y);
                ++lit;
                maxChannel = std::max(maxChannel, std::max({ p.red(), p.green(), p.blue() }));
                if (p.red() >= 254 && p.green() >= 254 && p.blue() >= 254) ++saturated;
            }
            std::printf("    white cube: %d lit px, %d fully saturated, brightest channel %d\n",
                        lit, saturated, maxChannel);
            CHECK(lit > 200, "the white cube rendered");
            CHECK(saturated == 0 && maxChannel < 255,
                  "the tonemapped thumbnail does not clip: no pixel is flat white");
            // MEASURED, in the studio environment (MATPREVIEW-ENV-1): the
            // brightest thing in the frame is a softbox panel, and the grade
            // rolls it to the band below. Ungraded, that panel is radiance 1.0
            // times the exposure multiplier — far past 255 — so losing the
            // grade saturates and the case above fails first; losing the
            // EXPOSURE moves this number out of the band.
            CHECK(maxChannel > 140 && maxChannel < 220,
                  "and the filmic curve is actually applied (the brightest pixel rolls down "
                  "into the 140..220 band instead of clipping; measured 168)");
        }

        // 7b. THE OWNER'S FAILURE, MADE AUDIBLE (THUMBS-1). The renderer's View
        // name is fixed, so a View already holding that name — which is what a
        // SECOND renderer instance used to be — makes ensureResources fail.
        // Before the fix that produced a null QImage and NOTHING ELSE: no log
        // line of ours, no reason, no return value carrying one, and a grey
        // tile for the user with a Qt null-pixmap warning as the only clue.
        // Now the engine's own words come back through lastFailure().
        {
            EngineThumbnailRenderer &r = *loan;
            // Release the renderer's resources so the next render has to make
            // its View again — with the name taken by somebody else.
            r.release();
            View *squatter = engine->createOffscreenView("thumbs", 64, 64, Colour(0, 0, 0));
            CHECK(squatter != nullptr, "a second view may hold the name 'thumbs'");
            const QImage blocked = thumbnail(r, QColor(220, 30, 30), size);
            CHECK(blocked.isNull(), "…and the thumbnail render then produces nothing");
            std::printf("    reason: %s\n", qUtf8Printable(r.lastFailure()));
            CHECK(r.lastFailure().contains("already exists"),
                  "…and SAYS WHY, in the engine's own words (it used to say nothing at all)");
            if (squatter) engine->destroyView(squatter);
            // …and with the name free again it renders, so the failure was the
            // name and nothing else.
            const QImage recovered = thumbnail(r, QColor(220, 30, 30), size);
            CHECK(!recovered.isNull() && !isNeutral(centre(recovered)),
                  "with the name free again the same renderer draws the subject");
            CHECK(r.lastFailure().isEmpty(), "…and reports no failure");
        }

        // 8. the primary view is untouched: still its own clear colour, nothing of the thumbs scene
        Image pimg; primary->readPixels(pimg);
        const Colour pc = pimg.at(32, 32);
        CHECK(pc.r < 0.05f && pc.g < 0.05f && pc.b < 0.05f, "the primary view did not render the thumbs scene");

        renderer.release();
    }
    // The renderer is the PROCESS's now (THUMBS-1): the loan above only gave it
    // back, so it must be destroyed here — while the Engine is alive, like
    // EngineHost::shutdown() does in the app.
    EngineThumbnailRenderer::shutdown();
    engine->destroyView(primary);
    engine->destroyScene(primaryScene);
    engine.reset();
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
