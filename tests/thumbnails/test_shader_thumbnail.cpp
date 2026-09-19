// Shader-asset thumbnails (VISUAL_PARITY_SPEC item 5): a SHADER asset is a
// stored graph definition, and it must thumbnail as the PbrMaterial the
// evaluator baked into it — on the same preview sphere a .material uses.
//
// The seam under test is MaterialReader::parseShaderAsPbr (src/io) driving
// EngineThumbnailRenderer::renderMaterial: definition -> material -> pixels.
// The route it replaces built a GLSL iris::CustomMaterial through
// material->generate(definition) — a pipeline MATERIALS_EVALUATOR phase 5
// deleted, which is why every shader tile was a generic file icon.
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

static bool isBackground(QColor c)
{
    const Colour bg = EngineThumbnailRenderer::backgroundColour();
    return std::abs(c.redF() - bg.r) < 0.04f && std::abs(c.greenF() - bg.g) < 0.04f
        && std::abs(c.blueF() - bg.b) < 0.04f;
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

// A LEGACY SHADER ROW's definition: the graph plus an evaluated
// "pbrMaterial" block. This is NOT what the app writes any more — a material
// is a BUNDLE and its definition is a `values` object at the top level
// (MATERIAL_BUNDLE_SPEC D-2, and `bundleDefinitionWithColour` below) — but
// ModelTypes::Shader rows still ARRIVE, from `AssetWidget::createShader` and
// from a `.shader` file on disk through ShaderImporter, so this shape and
// the reader that eats it are both live and both tested. Deleting them is
// phase 2's job, with those two minters.
static QJsonObject definitionWithColour(double r, double g, double b)
{
    QJsonObject colour;
    colour["r"] = r; colour["g"] = g; colour["b"] = b; colour["a"] = 1.0;

    QJsonObject values;
    values["baseColor"] = colour;
    values["metallic"]  = 0.0;
    values["roughness"] = 0.45;

    QJsonObject pbr;
    pbr["values"] = values;
    pbr["bakedMaps"] = QJsonObject();

    QJsonObject graph;                    // materialHasEffect looks for this key
    graph["materialGuid"] = QStringLiteral("derived-material-guid");

    QJsonObject definition;
    definition["name"] = QStringLiteral("test graph");
    definition["shadergraph"] = graph;
    definition["pbrMaterial"] = pbr;
    return definition;
}

// WHAT THE APP WRITES TODAY: a bundle definition. `values` at the top level,
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

    const QString shaderGuid = QStringLiteral("shader-guid-red");
    const QJsonObject definition = definitionWithColour(0.85, 0.10, 0.10);
    db.createAssetEntry(shaderGuid, "Red Graph", static_cast<int>(ModelTypes::Shader),
                        QString(), QString(), QString(), QString(), QByteArray(), QByteArray(),
                        QByteArray(), QJsonDocument(definition).toJson());
    CHECK(!db.fetchAssetData(shaderGuid).isEmpty(), "shader asset stored");

    // ---- 1. the seam: definition -> material, no engine involved ----
    MaterialReader reader;
    auto material = reader.parseShaderAsPbr(shaderGuid, &db);
    CHECK(!material.isNull(), "parseShaderAsPbr returns a material for an evaluated definition");
    auto pbr = material.dynamicCast<iris::PbrMaterial>();
    CHECK(!pbr.isNull(), "and it is a PbrMaterial (not a CustomMaterial stand-in)");
    if (pbr) {
        const QColor base = pbr->baseColor;
        std::printf("    baseColor = %d %d %d\n", base.red(), base.green(), base.blue());
        CHECK(base.red() > base.green() + 40 && base.red() > base.blue() + 40,
              "the graph's base colour survived the conversion");
    }

    // ---- 2. a pre-evaluator definition has nothing to render ----
    {
        QJsonObject old;                       // GLSL-era: shadergraph, no pbrMaterial
        old["shadergraph"] = QJsonObject();
        old["fragment_shader"] = QStringLiteral("something.frag");
        const QString oldGuid = QStringLiteral("shader-guid-legacy");
        db.createAssetEntry(oldGuid, "Legacy Graph", static_cast<int>(ModelTypes::Shader),
                            QString(), QString(), QString(), QString(), QByteArray(), QByteArray(),
                            QByteArray(), QJsonDocument(old).toJson());
        CHECK(reader.parseShaderAsPbr(oldGuid, &db).isNull(),
              "a pre-evaluator definition converts to nothing (callers show a fallback)");
        CHECK(reader.parseShaderAsPbr(QStringLiteral("no-such-guid"), &db).isNull(),
              "an unknown guid converts to nothing");
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
        QJsonObject baked = definitionWithColour(0.1, 0.8, 0.1);
        QJsonObject pbrObj = baked["pbrMaterial"].toObject();
        QJsonObject values = pbrObj["values"].toObject();
        values["baseColorMap"] = QStringLiteral("tex-baked-member-guid");
        pbrObj["values"] = values;
        QJsonObject maps; maps["baseColorMap"] = QStringLiteral("tex-baked-member-guid");
        pbrObj["bakedMaps"] = maps;
        baked["pbrMaterial"] = pbrObj;

        CHECK(!MaterialReader::shaderDefinitionAsPbr(baked, QString()).isNull(),
              "a baked-map graph converts with NO project open (its maps are store objects)");
        CHECK(!MaterialReader::shaderDefinitionAsPbr(baked, QDir::currentPath()).isNull(),
              "and with one");
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
        CHECK(!isBackground(c), "centre pixel is the material, not the background");
        CHECK(c.red() > c.green() + 40 && c.red() > c.blue() + 40,
              "the thumbnail shows the graph's colour (red-dominant)");
        CHECK(isBackground(img.pixelColor(2, 2)), "the sphere is framed inside the view");

        // ---- 5. it is NOT the grey fallback the old route produced ----
        QImage grey = renderer.renderMaterial(
            iris::DefaultMaterial::create().staticCast<iris::Material>(), size);
        show("default material", grey);
        const int d = maxAbsDiff(img, grey);
        std::printf("    max |graph - default| = %d\n", d);
        CHECK(d > 40, "the render differs from the grey default-material fallback");

        // ---- 5b. THE SHAPE THE MODULE ACTUALLY WRITES RENDERS (F1) ----
        //
        // Every graph material saved in the Materials module got a BLANK TILE:
        // the page asked the thumbnail queue for a SHADER render, that branch
        // reads the row blob through `parseShaderAsPbr`, and that refuses any
        // definition with no `pbrMaterial` key — which a bundle definition
        // does not have. The module asks for a MATERIAL render of the guid
        // now, which reads the bundle and parses it typed; this is that path,
        // end to end, on the engine.
        {
            AssetStorePaths::setRootOverride(QDir::current().filePath("bundle-store"));
            QDir().mkpath(AssetStorePaths::root());
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
            CHECK(!bundleImg.isNull() && !isBackground(bc),
                  "5b: the tile is NOT blank (the module's own save used to render nothing)");
            CHECK(bc.green() > bc.red() + 40 && bc.green() > bc.blue() + 40,
                  "5b: and it shows the material's colour, not black");
        }

        // ---- 6. a second graph gives a second picture (no cached leak) ----
        {
            const QString blueGuid = QStringLiteral("shader-guid-blue");
            db.createAssetEntry(blueGuid, "Blue Graph", static_cast<int>(ModelTypes::Shader),
                                QString(), QString(), QString(), QString(), QByteArray(), QByteArray(),
                                QByteArray(), QJsonDocument(definitionWithColour(0.1, 0.1, 0.85)).toJson());
            QImage blue = renderer.renderMaterial(reader.parseShaderAsPbr(blueGuid, &db), size);
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
