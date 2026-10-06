// document.color_json — ONE COLOUR ENCODING (audit D10).
//
// Two encodings shared the same {r, g, b, a} keys: SceneWriter::jsonColor wrote
// 0..255 INTEGERS (scenes, sky definitions, lights, particles, fog) and the
// materials graph baker's colorToJson wrote 0..1 FLOATS, so a reader that
// guessed the other form read black or white and said nothing. There is now one
// codec, irisgl/core/colorjson.h — 0..1 floats, the engine's encoding — and
// every writer and reader goes through it; SceneWriter::jsonColor,
// AssetIOBase::readColor, IrisUtils::readColor and the graph's private pair are
// gone. Forward-only: there is no reader of the integer form.
//
// Asserted: the round trip (every 8-bit value back exactly), the clamp, the
// absent object (invalid, so a reader can tell "not written" from black), the
// absent alpha (opaque), and that the DOCUMENT's own writer — a new Scene's sky
// blocks — writes floats in 0..1, never 255.
//
// Document only: the headless NULL render system, no display, no pixels.

#include <QGuiApplication>
#include <QJsonObject>
#include <cmath>
#include <cstdio>

#include "irisgl/core/colorjson.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); \
                              std::printf("\n"); } \
                         else { std::printf("FAIL: "); std::printf(__VA_ARGS__); \
                                std::printf("\n"); ++failures; } } while (0)

static bool isUnitFloat(const QJsonValue &v)
{
    return v.isDouble() && v.toDouble() >= 0.0 && v.toDouble() <= 1.0;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("document-color-json-ogre.log");

    // ---- the codec ----------------------------------------------------------
    const QJsonObject teal = iris::colorToJson(QColor(40, 160, 160, 128));
    CHECK(isUnitFloat(teal.value("r")) && isUnitFloat(teal.value("g")) &&
          isUnitFloat(teal.value("b")) && isUnitFloat(teal.value("a")),
          "colorToJson writes four 0..1 floats");
    CHECK(std::fabs(teal.value("g").toDouble() - 160.0 / 255.0) < 1e-6,
          "g of (40,160,160) is 160/255 = %.6f", teal.value("g").toDouble());
    int exact = 0;
    for (int v = 0; v < 256; ++v) {
        const QColor back = iris::colorFromJson(iris::colorToJson(QColor(v, 255 - v, v / 2, v)));
        if (back == QColor(v, 255 - v, v / 2, v)) ++exact;
    }
    CHECK(exact == 256, "every 8-bit value survives the round trip exactly (%d/256)", exact);
    CHECK(!iris::colorFromJson(QJsonObject()).isValid(),
          "an ABSENT colour reads as invalid, not as black");
    CHECK(iris::colorFromJson(QJsonObject(), Qt::red) == QColor(Qt::red),
          "...or as the caller's named default");
    const QColor opaque = iris::colorFromJson(QJsonObject{ { "r", 0.5 }, { "g", 0.5 }, { "b", 0.5 } });
    CHECK(opaque.alpha() == 255, "an absent alpha is opaque");
    const QColor clamped = iris::colorFromJson(QJsonObject{ { "r", 2.0 }, { "g", -1.0 }, { "b", 255 }, { "a", 1.0 } });
    CHECK(clamped == QColor(255, 0, 255, 255),
          "channels clamp to 0..1 — the integer form is NOT read as 0..255 (b 255 -> 1.0)");

    // ---- the document's own writer --------------------------------------------
    auto scene = iris::Scene::create();
    const QJsonObject sky = scene->skyData.value(QStringLiteral("SingleColor")).value("skyColor").toObject();
    CHECK(!sky.isEmpty() && isUnitFloat(sky.value("r")) && isUnitFloat(sky.value("a")),
          "a new scene's SingleColor block writes floats (r=%g a=%g)",
          sky.value("r").toDouble(), sky.value("a").toDouble());
    CHECK(iris::colorFromJson(sky) == scene->skyColor,
          "...that read back as the scene's own sky colour");

    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
