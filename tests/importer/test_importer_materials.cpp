// GLB MATERIAL-IMPORT suite (the material defects of 2026-09-08).
//
// The owner's report was "the GLB importer is losing the textures and the
// materials". The textures were BOUND; the models rendered BLACK, because of
// what the importer read into the shading factors:
//
//   1. A KHR_materials_pbrSpecularGlossiness model has no pbrMetallicRoughness
//      block at all — but assimp reports metallicFactor/roughnessFactor for
//      every glTF material anyway (its own struct defaults, 1.0/1.0), so the
//      importer's `if (assimp reported it)` test was always true and every
//      spec-gloss model imported as FULL METAL, FULL ROUGH. A metal with no
//      environment to reflect is black.
//   2. A material with NO workflow block at all landed on the same 1.0/1.0.
//      Policy (irisgl/document/assets/mesh.h): that is a dielectric.
//   3. KHR_materials_unlit was ignored, and unlit exports that carry their
//      artwork in the EMISSIVE slot (with a black base colour) therefore
//      imported as a black, lit surface.
//   4. emissiveIntensity was never set on an import, so an emissive colour or
//      map was multiplied by the document default 0 and emitted nothing.
//
// MATERIAL_GAPS_SPEC GAP 1 SUPERSEDES DEFECT 1's REMEDY (§2.5): spec-gloss is
// no longer CONVERTED to metallic-roughness, it imports into the renderer's own
// Specular workflow, losslessly, with its map bound instead of dropped. The
// conversion function stays — as the EXPORT fallback for targets with no
// workflow concept — and section 1 still asserts it. Two more materials join
// the fixture for the extensions the switch makes importable:
// KHR_materials_specular (-> Specular-as-Fresnel) and KHR_materials_ior.
//
// Fixture: fixtures/material_workflows.glb, seven quads with one material shape
// each (fixtures/make_material_fixtures.py documents them and regenerates it).
// Sections:
//   1. The conversion formula itself (unit).
//   2. The five materials through the REAL import path
//      (AssetHelper::extractTexturesAndMaterialFromMesh).
//   3. emissiveIntensity round trip: import -> SceneWriter blob ->
//      AssetHelper::updateNodeMaterial (the reopen path for a model asset).
//   4. PIXELS: the spec-gloss quad rendered as imported versus rendered with
//      the pre-fix reading (metallic 1 / roughness 1) of the SAME mesh, and
//      the unlit quad, through EngineThumbnailRenderer.
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/import/materialhelper.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

#include "bridge/enginethumbnailrenderer.h"
#include "io/scenewriter.h"
#include "services/assethelper.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static QString fixture(const char *name)
{
    return QString(JAHSHAKA_TEST_SOURCE_DIR "/tests/importer/fixtures/") + name;
}

static bool nearly(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

/// The imported quad named `name` (one node per material in the fixture).
static iris::PbrMaterialPtr materialNamed(const iris::SceneNodePtr &root, const QString &name)
{
    if (!root) return iris::PbrMaterialPtr();
    if (root->getSceneNodeType() == iris::SceneNodeType::Mesh && root->name == name)
        return root.staticCast<iris::MeshNode>()->getMaterial().dynamicCast<iris::PbrMaterial>();
    for (auto child : root->children()) {
        auto found = materialNamed(child, name);
        if (found) return found;
    }
    return iris::PbrMaterialPtr();
}

static iris::MeshNodePtr meshNodeNamed(const iris::SceneNodePtr &root, const QString &name)
{
    if (!root) return iris::MeshNodePtr();
    if (root->getSceneNodeType() == iris::SceneNodeType::Mesh && root->name == name)
        return root.staticCast<iris::MeshNode>();
    for (auto child : root->children()) {
        auto found = meshNodeNamed(child, name);
        if (found) return found;
    }
    return iris::MeshNodePtr();
}

/// Mean luminance of a rendered tile, counting only what is not the background.
static double meanSubjectLuma(const QImage &img)
{
    const Colour bg = EngineThumbnailRenderer::backgroundColour();
    double sum = 0.0;
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (std::fabs(float(c.redF()) - bg.r) < 0.04f &&
                std::fabs(float(c.greenF()) - bg.g) < 0.04f &&
                std::fabs(float(c.blueF()) - bg.b) < 0.04f)
                continue;
            sum += 0.2126 * c.red() + 0.7152 * c.green() + 0.0722 * c.blue();
            ++n;
        }
    return n ? sum / n : 0.0;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // A document node IS an engine node (SCENEGRAPH_SPEC D2) and this suite
    // needs BOTH halves — imported documents AND pixels — so the document graph
    // is staged onto the ONE engine (Ogre::Root is a singleton), created first
    // and destroyed last.
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_importer_materials-ogre.log";
    std::string err;
    std::shared_ptr<Engine> engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    iris::graph::setStagingScene(
        reinterpret_cast<iris::graph::SceneHandle>(engine->documentGraphScene()));

    // ================= 1. the conversion formula =================
    // KHR_materials_pbrSpecularGlossiness appendix B, the same arithmetic
    // Blender and three.js run. The two ends of it are what matter here: a
    // BLACK specular colour cannot be metal (it is below the 4% every
    // dielectric has), a WHITE one can only be metal.
    {
        const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        const float black3[3] = { 0.0f, 0.0f, 0.0f };
        const float white3[3] = { 1.0f, 1.0f, 1.0f };
        QColor base;
        float metallic = -1.0f, roughness = -1.0f;

        // The tails-model input: specular [0,0,0], glossiness 0.0178.
        iris::MaterialHelper::specularGlossinessToMetallicRoughness(
            white, black3, 0.0177827941f, base, metallic, roughness);
        CHECK(nearly(metallic, 0.0f), "1: black specular converts to metallic 0 (a dielectric)");
        CHECK(nearly(roughness, 0.9822f, 2e-3f), "1: roughness is 1 - glossiness (0.982)");
        CHECK(base.red() > 240 && base.green() > 240 && base.blue() > 240,
              "1: a white diffuse factor stays a white base colour");

        iris::MaterialHelper::specularGlossinessToMetallicRoughness(
            white, white3, 0.75f, base, metallic, roughness);
        CHECK(nearly(metallic, 1.0f, 1e-2f), "1: white specular converts to metallic 1 (a metal)");
        CHECK(nearly(roughness, 0.25f), "1: ... with roughness 1 - 0.75");
    }

    // ================= 2. the five workflows through the real import =================
    QTemporaryDir tmp;
    iris::SceneNodePtr imported;
    {
        CHECK(tmp.isValid(), "2: temp dir for import");
        const QString model = QDir(tmp.path()).filePath("material_workflows.glb");
        CHECK(QFile::copy(fixture("material_workflows.glb"), model), "2: fixture copied");

        QStringList texNames, texPaths;
        bool hasEmbedded = false;
        imported = AssetHelper::extractTexturesAndMaterialFromMesh(model, texNames, texPaths,
                                                                   hasEmbedded, nullptr, tmp.path());
        CHECK(!imported.isNull(), "2: import produced a node");
        if (imported.isNull()) return 1;

        // --- DEFECT 1, NOW SUPERSEDED BY GAP 1 (MATERIAL_GAPS_SPEC §2.5).
        // Spec-gloss used to be CONVERTED to metallic-roughness because that
        // was the only workflow the renderer had. It now imports NATIVELY into
        // the renderer's own Specular workflow, which is lossless: the diffuse
        // factor is the base colour, specularFactor is kS, roughness is exactly
        // 1 - glossiness, and the spec-gloss MAP finally has a home (the shared
        // metallic/specular texture unit) instead of being dropped with a
        // warning. The conversion function is still built and still asserted in
        // section 1 — it is the EXPORT fallback now, for targets with no
        // workflow concept.
        auto specgloss = materialNamed(imported, "specgloss");
        CHECK(!specgloss.isNull(), "2: specgloss quad imports as a PbrMaterial");
        if (specgloss) {
            std::printf("    specgloss: workflow %d roughness %.3f base %s spec %s map %d\n",
                        specgloss->workflow, specgloss->roughnessFactor,
                        specgloss->baseColor.name().toUtf8().constData(),
                        specgloss->specularColor.name().toUtf8().constData(),
                        int(specgloss->useBaseColorMap));
            CHECK(specgloss->workflow == 1,
                  "2: spec-gloss imports NATIVELY into the Specular workflow (no conversion)");
            CHECK(nearly(specgloss->roughnessFactor, 0.9822f, 2e-3f),
                  "2: ... roughness is exactly 1 - glossiness");
            CHECK(specgloss->specularColor.red() == 0 && specgloss->specularColor.green() == 0 &&
                  specgloss->specularColor.blue() == 0,
                  "2: ... and specularFactor [0,0,0] arrives as kS verbatim, not as 'metallic 0'");
            CHECK(specgloss->useBaseColorMap,
                  "2: ... and its diffuse texture is bound as the base-colour map");
            CHECK(specgloss->shadingModel == 0, "2: ... as a LIT material");
        }

        // A spec-gloss material that really is metal keeps its WHITE kS rather
        // than being re-derived as metalness. The conversion's own metal arm is
        // still asserted in section 1; here the point is that nothing is
        // re-derived at all any more.
        auto specglossMetal = materialNamed(imported, "specgloss_metal");
        CHECK(!specglossMetal.isNull(), "2: specgloss_metal quad imports");
        if (specglossMetal) {
            CHECK(specglossMetal->workflow == 1, "2: it too imports in the Specular workflow");
            CHECK(specglossMetal->specularColor.red() > 250 &&
                  specglossMetal->specularColor.blue() > 250,
                  "2: specularFactor [1,1,1] arrives as a white kS");
            CHECK(nearly(specglossMetal->roughnessFactor, 0.25f),
                  "2: ... roughness 1 - 0.75");
        }

        // --- GAP 1: KHR_materials_specular -> Specular-as-Fresnel.
        // specularColorFactor is an F0 TINT (the pin's own docs call this
        // workflow "what most PBRs mean by specular"), so it lands on the
        // fresnel colour multiplied by specularFactor — not on kS.
        auto specExt = materialNamed(imported, "spec_ext");
        CHECK(!specExt.isNull(), "2: spec_ext quad imports");
        if (specExt) {
            std::printf("    spec_ext:  workflow %d useFresnelColor %d fresnel %s ior %.2f\n",
                        specExt->workflow, int(specExt->useFresnelColor),
                        specExt->fresnelColor.name().toUtf8().constData(), specExt->ior);
            CHECK(specExt->workflow == 2,
                  "2: KHR_materials_specular imports as Specular-as-Fresnel");
            CHECK(specExt->useFresnelColor,
                  "2: ... with F0 authored directly (specularColorFactor is an F0 tint)");
            // [1.0, 0.5, 0.25] * specularFactor 0.5 = [0.5, 0.25, 0.125]
            CHECK(std::abs(specExt->fresnelColor.red() - 128) <= 2 &&
                  std::abs(specExt->fresnelColor.green() - 64) <= 2 &&
                  std::abs(specExt->fresnelColor.blue() - 32) <= 2,
                  "2: ... F0 = specularColorFactor * specularFactor");
            CHECK(nearly(specExt->roughnessFactor, 0.3f),
                  "2: ... and the metallic-roughness base's roughness is kept");
        }

        // --- GAP 1: KHR_materials_ior, alone, on a metallic material.
        // Stored even though it is INERT while metallic — the same "values
        // survive the switch" rule clear coat follows, so switching workflow in
        // the editor restores what the file said.
        auto iorGlass = materialNamed(imported, "ior_glass");
        CHECK(!iorGlass.isNull(), "2: ior_glass quad imports");
        if (iorGlass) {
            CHECK(iorGlass->workflow == 0,
                  "2: KHR_materials_ior alone does NOT change the workflow");
            CHECK(nearly(iorGlass->ior, 1.8f),
                  "2: ... but the IOR is stored (inert while metallic, restored by a switch)");
        }

        // --- DEFECT 2: no workflow block at all is a DIELECTRIC, by policy.
        auto nopbr = materialNamed(imported, "nopbr");
        CHECK(!nopbr.isNull(), "2: block-less material still imports as a PbrMaterial");
        if (nopbr) {
            std::printf("    nopbr:     metallic %.3f roughness %.3f\n",
                        nopbr->metallicFactor, nopbr->roughnessFactor);
            CHECK(nearly(nopbr->metallicFactor, 0.0f),
                  "2: a material with NO pbr block imports as a dielectric (was metallic 1)");
            CHECK(nearly(nopbr->roughnessFactor, 0.5f), "2: ... at the neutral roughness 0.5");
        }

        // ... while a PRESENT block keeps glTF's own defaults: an omitted
        // metallicFactor IS 1.0 (lotus_elise.glb is a real metallic car).
        auto mrDefault = materialNamed(imported, "mr_default");
        CHECK(!mrDefault.isNull(), "2: metallic-roughness quad imports");
        if (mrDefault) {
            CHECK(nearly(mrDefault->metallicFactor, 1.0f),
                  "2: a PRESENT pbrMetallicRoughness block keeps glTF's metallicFactor default 1");
            CHECK(nearly(mrDefault->roughnessFactor, 0.4f), "2: ... and its authored roughness");
            CHECK(std::abs(mrDefault->baseColor.red() - 204) <= 2,
                  "2: ... and its authored base colour (0.8 -> 204)");
        }

        // --- DEFECT 3: KHR_materials_unlit.
        auto unlit = materialNamed(imported, "unlit");
        CHECK(!unlit.isNull(), "2: unlit quad imports");
        if (unlit) {
            std::printf("    unlit:     shadingModel %d base %s map %d emissiveIntensity %.2f\n",
                        unlit->shadingModel, unlit->baseColor.name().toUtf8().constData(),
                        int(unlit->useBaseColorMap), unlit->emissiveIntensity);
            CHECK(unlit->shadingModel == 1,
                  "3: KHR_materials_unlit imports as the UNLIT shading model");
            CHECK(unlit->useBaseColorMap,
                  "3: ... with the emissive artwork bound as the colour map "
                  "(Unlit consumes no emissive input)");
            CHECK(unlit->baseColor.red() > 240 && unlit->baseColor.green() > 240,
                  "3: ... and a black baseColorFactor yields to the emissive factor");
        }
    }

    // ================= 3. emissiveIntensity round trip =================
    // DEFECT 4. The import path used to set emissiveColor and leave the
    // intensity at the document default 0 — emission multiplied by zero. The
    // round trip is the one a model asset really takes on reopen: the material
    // is written into the asset blob by SceneWriter and rebuilt from it by
    // AssetHelper::updateNodeMaterial.
    {
        auto unlitNode = meshNodeNamed(imported, "unlit");
        CHECK(!unlitNode.isNull(), "4: emissive subject found");
        if (unlitNode) {
            auto mat = unlitNode->getMaterial().dynamicCast<iris::PbrMaterial>();
            CHECK(mat && nearly(mat->emissiveIntensity, 1.0f),
                  "4: an imported emissive material carries emissiveIntensity 1 (was 0)");
            CHECK(mat && mat->emissiveColor != QColor(Qt::black),
                  "4: ... with a non-black emissive colour");

            QJsonObject matObj;
            SceneWriter::writeSceneNodeMaterial(matObj, unlitNode->getMaterial(), false);
            const QJsonObject values = matObj.value(QStringLiteral("values")).toObject();
            CHECK(nearly(float(values.value(QStringLiteral("emissiveIntensity")).toDouble()), 1.0f),
                  "4: the saved material blob carries emissiveIntensity 1");
            CHECK(values.value(QStringLiteral("shadingModel")).toInt() == 1,
                  "4: ... and the unlit shading model");

            QJsonObject definition;
            definition[QStringLiteral("material")] = matObj;
            iris::SceneNodePtr reopened = iris::MeshNode::create();
            AssetHelper::updateNodeMaterial(reopened, definition, nullptr);
            auto rebuilt = reopened.staticCast<iris::MeshNode>()
                               ->getMaterial().dynamicCast<iris::PbrMaterial>();
            CHECK(rebuilt && nearly(rebuilt->emissiveIntensity, 1.0f),
                  "4: reopening the asset preserves emissiveIntensity (was 0 on every model)");
            CHECK(rebuilt && rebuilt->shadingModel == 1,
                  "4: ... and the unlit shading model");
        }
    }

    // ================= 4. PIXELS =================
    // The defect was visible, so the fix is proven visibly: the SAME imported
    // mesh, rendered with the material as imported and with the pre-fix
    // reading of it (metallic 1 / roughness 1), through the thumbnail path.
    {
        {
            View *primary = engine->createOffscreenView("primary", 64, 64, Colour(0, 0, 0));
            Scene *primaryScene = engine->createScene("primary");
            primary->setScene(primaryScene);
            {
                EngineThumbnailRenderer renderer(engine);

                auto node = meshNodeNamed(imported, "specgloss");
                CHECK(!node.isNull(), "5: spec-gloss subject found");
                if (node) {
                    auto mat = node->getMaterial().dynamicCast<iris::PbrMaterial>();
                    const QImage fixedShot = renderer.renderNode(node, QSize(96, 96));
                    const double fixedLuma = meanSubjectLuma(fixedShot);

                    // The pre-fix reading, on the same mesh and the same maps:
                    // the metallic workflow at full metal / full rough, which
                    // is what assimp's always-present keys used to produce.
                    mat->setValue(QStringLiteral("workflow"), 0);
                    mat->setValue(QStringLiteral("metallic"), 1.0f);
                    mat->setValue(QStringLiteral("roughness"), 1.0f);
                    const QImage brokenShot = renderer.renderNode(node, QSize(96, 96));
                    const double brokenLuma = meanSubjectLuma(brokenShot);
                    // Leave the document as it was imported.
                    mat->setValue(QStringLiteral("workflow"), 1);
                    mat->setValue(QStringLiteral("metallic"), 0.0f);
                    mat->setValue(QStringLiteral("roughness"), 0.9822f);

                    std::printf("    spec-gloss quad: imported luma %.1f, pre-fix (metal 1/rough 1) luma %.1f\n",
                                fixedLuma, brokenLuma);
                    CHECK(!fixedShot.isNull() && fixedShot.size() == QSize(96, 96),
                          "5: the spec-gloss quad renders");
                    // RE-PINNED (MATERIAL_GAPS_SPEC §2.5), and the movement is
                    // MEASURED AND EXPLAINED, not absorbed by a wider bound:
                    //   before 40.3  ->  after 39.4   (-0.9, -2.2%)
                    // The conversion path left kS at the datablock's default
                    // WHITE, so the surface kept a specular highlight the source
                    // file never asked for. The native Specular workflow carries
                    // the fixture's own specularFactor [0,0,0] — a black kS, no
                    // highlight. Darker BECAUSE the import is more faithful.
                    //
                    // A BAND, not a lowered floor: too dark still fails (that is
                    // the black-model defect this test exists for) and so does
                    // drifting brighter, which would mean kS had gone back to
                    // being invented.
                    CHECK(fixedLuma > 35.0 && fixedLuma < 45.0,
                          "5: the imported spec-gloss material is LIT, at the re-pinned value "
                          "(39.4 +/- 5; was 40.3 with the conversion's invented white kS)");
                    CHECK(fixedLuma > brokenLuma * 1.5,
                          "5: ... and is far brighter than the full-metal reading it used to get");
                }

                auto unlitNode = meshNodeNamed(imported, "unlit");
                CHECK(!unlitNode.isNull(), "5: unlit subject found");
                if (unlitNode) {
                    const QImage shot = renderer.renderNode(unlitNode, QSize(96, 96));
                    const double luma = meanSubjectLuma(shot);
                    std::printf("    unlit quad: luma %.1f\n", luma);
                    CHECK(luma > 40.0,
                          "5: the unlit quad renders its texture (was a black surface)");
                }
            }
            engine->destroyView(primary);
            engine->destroyScene(primaryScene);
        }
    }

    // Every document handle dies before the engine that owns it.
    imported.reset();
    iris::graph::setStagingScene(nullptr);
    engine.reset();

    std::printf(failures ? "\nFAILED: %d checks\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
