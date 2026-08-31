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

#include "irisgl/irisglfwd.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "data/database/database.h"
#include "io/materialreader.h"
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

// What MaterialHelper::serializeWithBake writes for a graph whose Base Color
// folded to a constant: the graph itself plus the evaluated "pbrMaterial".
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

    // ---- 3. baked maps need a project root (no half-textured renders) ----
    {
        QJsonObject baked = definitionWithColour(0.1, 0.8, 0.1);
        QJsonObject pbrObj = baked["pbrMaterial"].toObject();
        QJsonObject values = pbrObj["values"].toObject();
        values["baseColorMap"] = QStringLiteral("BakedMaps/abc/baseColor.png");
        pbrObj["values"] = values;
        QJsonObject maps; maps["baseColorMap"] = QStringLiteral("BakedMaps/abc/baseColor.png");
        pbrObj["bakedMaps"] = maps;
        baked["pbrMaterial"] = pbrObj;

        CHECK(MaterialReader::shaderDefinitionAsPbr(baked, QString()).isNull(),
              "a baked-map graph with no open project is refused (no half-textured render)");
        CHECK(!MaterialReader::shaderDefinitionAsPbr(baked, QDir::currentPath()).isNull(),
              "the same graph converts once a project root exists");
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
        EngineThumbnailRenderer renderer(engine);
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

    engine->destroyView(primary);
    engine->destroyScene(primaryScene);
    engine.reset();
    db.closeDatabase();
    QFile::remove(dbPath);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
