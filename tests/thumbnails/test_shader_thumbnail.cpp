// GRAPH-MATERIAL THUMBNAILS (VISUAL_PARITY_SPEC item 5), RE-ANCHORED ON THE
// BUNDLE (MATERIAL_BUNDLE_SPEC phase 2).
//
// A material made in the Materials module is ONE row whose definition is its
// own file in the content-addressed store, with the graph as a payload of
// that definition — so its thumbnail is a MATERIAL render like any other
// material's, and the seam under test is
//
//     MaterialBundle::read -> MaterialReader::parseMaterialTyped
//                          -> EngineThumbnailRenderer::renderMaterial
//
// This suite used to drive `MaterialReader::parseShaderAsPbr` over a
// ModelTypes::Shader ROW — the module's old separate graph asset. That reader
// and both of its minting sites (the editor browser's "Create > Shader" and
// the `.shader` file importer) are DELETED in this phase, so the suite is
// anchored where the app actually reads: the definition a save writes.
//
// Real Database on a throwaway SQLite file; offscreen engine View; no UI.
#include <QGuiApplication>
#include <QByteArray>
#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <memory>

#include "../support/previewdump.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "data/database/database.h"
#include "io/materialreader.h"
#include "services/assetstorepaths.h"
#include "services/materialbundle.h"
#include <QJsonArray>
#include "bridge/enginethumbnailrenderer.h"
#include "jahshaka/engine/Engine.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// THE BACKDROP IS A PICTURE NOW (MATPREVIEW-ENV-1): a thumbnail is a subject
// in the ONE generated studio environment, so behind it is that room's neutral
// wall and not the view's flat clear colour. "Is this the background" is
// therefore neutrality — the room is neutral by construction, and this suite's
// subjects are a red graph and a blue one.
static bool isNeutral(QColor c, int tolerance = 14)
{
    return std::abs(c.red() - c.green()) <= tolerance
        && std::abs(c.green() - c.blue()) <= tolerance
        && std::abs(c.red() - c.blue()) <= tolerance;
}
static QColor centre(const QImage &img) { return img.pixelColor(img.width() / 2, img.height() / 2); }
static void show(const char *tag, const QImage &img)
{
    previewdump::save(tag, img);
    const QColor c = img.isNull() ? QColor() : centre(img), k = img.isNull() ? QColor() : img.pixelColor(2, 2);
    std::printf("    %-26s %dx%d centre %3d %3d %3d   corner %3d %3d %3d\n", tag, img.width(), img.height(),
                c.red(), c.green(), c.blue(), k.red(), k.green(), k.blue());
}
static int maxAbsDiff(const QImage &a, const QImage &b)
{
    if (a.size() != b.size()) return 255;
    int d = 0;
    for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) {
        const QColor p = a.pixelColor(x, y), q = b.pixelColor(x, y);
        d = std::max({d, std::abs(p.red() - q.red()), std::abs(p.green() - q.green()), std::abs(p.blue() - q.blue())});
    }
    return d;
}

// WHAT THE APP WRITES: a bundle definition. `values` at the top level,
// colours spelled the document's way (MaterialBundle::normaliseColours does
// it at the write), read by MaterialReader::parseMaterialTyped — the ONE
// reader every material surface uses.
static QJsonObject bundleDefinitionWithColour(double r, double g, double b)
{
    QJsonObject rgba;
    rgba["r"] = r; rgba["g"] = g; rgba["b"] = b; rgba["a"] = 1.0;

    QJsonObject values;
    values["baseColor"] = rgba;          // the EVALUATOR's spelling, on purpose
    values["metallic"]  = 0.0;
    values["roughness"] = 0.45;

    QJsonObject graph;
    graph["nodes"] = QJsonArray();

    QJsonObject definition;
    definition["materialType"] = QStringLiteral("pbr");
    definition["name"] = QStringLiteral("bundle graph");
    definition["values"] = values;
    definition["shadergraph"] = graph;   // a graph material: the payload
    return definition;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    // ---- database: a throwaway file, never the user's library ----
    const QString dbPath = QDir::current().filePath("test_shader_thumbnail.db");
    QFile::remove(dbPath);
    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();

    // The store the definitions live in — a throwaway beside the database.
    AssetStorePaths::setRootOverride(QDir::current().filePath("bundle-store"));
    QDir().mkpath(AssetStorePaths::root());

    QString createError;
    const QString shaderGuid = MaterialBundle::create(
        &db, QStringLiteral("Red Graph"), bundleDefinitionWithColour(0.85, 0.10, 0.10),
        QByteArray(), &createError);
    CHECK(!shaderGuid.isEmpty(),
          qPrintable(QStringLiteral("a graph material bundle was written (%1)").arg(createError)));

    // ---- 1. the seam: definition -> material, no engine involved ----
    MaterialReader reader;
    auto material = reader.parseMaterialTyped(MaterialBundle::read(&db, shaderGuid), &db);
    CHECK(!material.isNull(), "a stored bundle definition reads back as a material");
    auto pbr = material.dynamicCast<iris::PbrMaterial>();
    CHECK(!pbr.isNull(), "and it is a PbrMaterial (not a CustomMaterial stand-in)");
    if (pbr) {
        const QColor base = pbr->baseColor;
        std::printf("    baseColor = %d %d %d\n", base.red(), base.green(), base.blue());
        CHECK(base.red() > base.green() + 40 && base.red() > base.blue() + 40,
              "the graph's base colour survived the conversion");
    }

    // ---- 2. a guid with no definition has nothing to render ----
    {
        CHECK(MaterialBundle::read(&db, QStringLiteral("no-such-guid")).isEmpty(),
              "an unknown guid has no definition (callers show a fallback)");
        CHECK(MaterialBundle::read(&db, QString()).isEmpty(), "and so does an empty guid");
    }

    // ---- 3. A BAKED-MAP GRAPH READS WITH NO PROJECT OPEN -----------------
    //
    // It used to be REFUSED there, and the refusal was right for what a bake
    // then was: a loose PNG under `<projectFolder>/BakedMaps/<guid>/`, named
    // in the definition by a project-RELATIVE path, which with no project
    // reached the loader as a literal relative string and rendered a
    // half-textured material. That also meant a graph material could never
    // preview in the LIBRARY.
    //
    // MATERIAL_BUNDLE_SPEC phase 1 removed the cause: a baked map is a MEMBER
    // TEXTURE row in the content-addressed store, named in the definition by
    // its guid like every other map, so it resolves with or without a project
    // and on any machine. The refusal is deleted with the thing it protected
    // against, and the same definition now converts either way.
    {
        QJsonObject baked = bundleDefinitionWithColour(0.1, 0.8, 0.1);
        QJsonObject values = baked["values"].toObject();
        values["baseColorMap"] = QStringLiteral("tex-baked-member-guid");
        baked["values"] = values;
        QJsonObject maps; maps["baseColorMap"] = QStringLiteral("tex-baked-member-guid");
        QJsonObject bake; bake["maps"] = maps;
        baked["bake"] = bake;

        MaterialReader noProject;
        CHECK(!noProject.parseMaterialTyped(baked, &db).isNull(),
              "a baked-map graph material converts with NO project open (its maps are store objects)");
        CHECK(MaterialBundle::memberGuids(baked).contains(QStringLiteral("tex-baked-member-guid")),
              "and the bake's map is a MEMBER of the bundle, named by guid");
    }

    // ---- engine: the preview sphere ----
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_shader_thumbnail-ogre.log";
    std::string err;
    std::shared_ptr<Engine> engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }
    View *primary = engine->createOffscreenView("primary", 64, 64, Colour(0, 0, 0));
    Scene *primaryScene = engine->createScene("primary");
    primary->setScene(primaryScene);

    {
        auto loan = EngineThumbnailRenderer::borrow(engine, "the shader-thumbnail suite");
        CHECK(bool(loan), "the thumbnail renderer can be borrowed");
        if (!loan) return 1;
        EngineThumbnailRenderer &renderer = *loan;
        const QSize size(96, 96);

        // ---- 4. the thumbnail: the graph's colour on the sphere ----
        QImage img = renderer.renderMaterial(material, size);
        show("red graph", img);
        CHECK(!img.isNull() && img.size() == size, "shader thumbnail renders at the requested size");
        const QColor c = centre(img);
        CHECK(!isNeutral(c), "centre pixel is the material, not the neutral backdrop");
        CHECK(c.red() > c.green() + 40 && c.red() > c.blue() + 40,
              "the thumbnail shows the graph's colour (red-dominant)");
        CHECK(isNeutral(img.pixelColor(2, 2)), "the sphere is framed inside the view");

        // ---- 5. it is NOT the grey fallback the old route produced ----
        QImage grey = renderer.renderMaterial(
            iris::DefaultMaterial::create().staticCast<iris::Material>(), size);
        show("default material", grey);
        const int d = maxAbsDiff(img, grey);
        std::printf("    max |graph - default| = %d\n", d);
        CHECK(d > 40, "the render differs from the grey default-material fallback");

        // ---- 5b. A SECOND BUNDLE, WRITTEN AND RENDERED THE SAME WAY (F1) ----
        //
        // Every graph material saved in the Materials module once got a BLANK
        // TILE: the page asked the thumbnail queue for a SHADER render, that
        // branch read the row blob through the deleted `parseShaderAsPbr`, and
        // that refused any definition with no `pbrMaterial` key — which a
        // bundle definition does not have. There is one render path now; this
        // is it, end to end, on the engine.
        {
            QString error;
            const QString bundleGuid = MaterialBundle::create(
                &db, QStringLiteral("Bundle Green"),
                bundleDefinitionWithColour(0.10, 0.85, 0.10), QByteArray(), &error);
            CHECK(!bundleGuid.isEmpty(),
                  qPrintable(QStringLiteral("5b: the bundle was written (%1)").arg(error)));

            const QJsonObject readBack = MaterialBundle::read(&db, bundleGuid);
            CHECK(readBack.value("values").toObject().value("baseColor").isString(),
                  "5b: the write normalised the colour to the document's spelling");

            MaterialReader bundleReader;
            auto bundleMaterial = bundleReader.parseMaterialTyped(readBack, &db);
            CHECK(!bundleMaterial.isNull(), "5b: a bundle definition parses typed");
            QImage bundleImg = renderer.renderMaterial(bundleMaterial, size);
            show("bundle graph", bundleImg);
            const QColor bc = centre(bundleImg);
            CHECK(!bundleImg.isNull() && !isNeutral(bc),
                  "5b: the tile is NOT blank (the module's own save used to render nothing)");
            CHECK(bc.green() > bc.red() + 40 && bc.green() > bc.blue() + 40,
                  "5b: and it shows the material's colour, not black");
        }

        // ---- 6. a second graph gives a second picture (no cached leak) ----
        {
            QString blueError;
            const QString blueGuid = MaterialBundle::create(
                &db, QStringLiteral("Blue Graph"), bundleDefinitionWithColour(0.1, 0.1, 0.85),
                QByteArray(), &blueError);
            CHECK(!blueGuid.isEmpty(), "a second bundle was written");
            QImage blue = renderer.renderMaterial(
                reader.parseMaterialTyped(MaterialBundle::read(&db, blueGuid), &db), size);
            show("blue graph", blue);
            const QColor cb = centre(blue);
            CHECK(cb.blue() > cb.red() + 40 && cb.blue() > cb.green() + 40, "the second graph is blue");
            CHECK(maxAbsDiff(img, blue) > 40, "two graphs give two different thumbnails");
        }

        // ---- 7. what assets.refreshThumbnail stores: a decodable PNG row ----
        {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            QPixmap::fromImage(img).save(&buffer, "PNG");
            CHECK(db.updateAssetThumbnail(shaderGuid, bytes), "thumbnail written to the asset row");
            const QByteArray stored = db.fetchAsset(shaderGuid).thumbnail;
            CHECK(!stored.isEmpty(), "the stored blob is not empty (it used to be QByteArray())");
            QImage decoded;
            CHECK(decoded.loadFromData(stored, "PNG"), "the stored blob decodes as a PNG");
            const QColor dc = centre(decoded);
            CHECK(dc.red() > dc.green() + 40 && dc.red() > dc.blue() + 40,
                  "and the stored picture still shows the graph's colour");
        }

        renderer.release();
    }
    // The renderer is the PROCESS's now (THUMBS-1): the loan above only gave it
    // back, so it must be destroyed here — while the Engine is alive, like
    // EngineHost::shutdown() does in the app.
    EngineThumbnailRenderer::shutdown();

    engine->destroyView(primary);
    engine->destroyScene(primaryScene);
    engine.reset();
    db.closeDatabase();
    QFile::remove(dbPath);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
