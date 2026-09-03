// PLY + STL importer suite — the two mesh formats added to the assimp
// allowlist (irisgl/CMakeLists.txt: ASSIMP_BUILD_PLY_IMPORTER /
// ASSIMP_BUILD_STL_IMPORTER, with ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF).
//
// The allowlist is the reason this suite exists at all. An importer that is
// not named in that file is COMPILED OUT, so its extension fails at ReadFile
// with assimp's generic "unknown file format" — silently, at runtime, on a
// user's file. Section 1 is therefore a build-configuration assertion as much
// as a format one: if someone drops the option, this suite goes red instead of
// the app going quietly deaf to .ply/.stl.
//
// Fixtures (tests/importer/fixtures/make_ply_stl_fixtures.py, ASCII, ~1 KB):
//   colored_quad.ply       2 triangles, per-vertex colours, NO normals, no UVs,
//                          no material — the scanner/photogrammetry shape.
//   tetra_normals.stl      4 facets whose `facet normal` records are real unit
//                          vectors (what most STL writers emit).
//   tetra_zeronormals.stl  the same tetrahedron with `facet normal 0 0 0` —
//                          what Blender emits, and what assimp's own STLLoader
//                          source calls out as needing the RemoveInvalidData
//                          step. The pair is what lets section 3 state where a
//                          shadeable STL's normals come from by MEASUREMENT.
//
// Sections:
//   1. The allowlist gate + what assimp hands us (raw vs canonical preset).
//   2. PLY through OUR paths: MeshNode::loadAsSceneFragment (direct load) and
//      AssetHelper::extractTexturesAndMaterialFromMesh (the import parse),
//      vertex colours reaching the engine-facing MeshData.
//   3. STL: normals under the canonical preset — including the zero-normal
//      file — no UVs, no material in the file, a usable default material.
//   4. The ONE import pipeline (assets.importFile's own AssetImportService):
//      both formats sniff as meshes and commit Object + Mesh rows.
//   5. Constants: the extension lists that drive every sniffer and dialog.
//   6. PIXELS: EngineThumbnailRenderer draws both formats — the thumbnail path
//      an imported model actually goes through — and the result is not the
//      background.
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>
#include <memory>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "irisgl/irisglfwd.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/import/importflags.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

#include "bridge/enginethumbnailrenderer.h"
#include "data/constants.h"
#include "data/database/database.h"
#include "services/assetcas.h"
#include "services/assethelper.h"
#include "services/assetstorepaths.h"
#include "services/import/assetimportservice.h"
#include "services/import/importtypes.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static QString fixture(const char *name)
{
    return QString(JAHSHAKA_TEST_SOURCE_DIR "/tests/importer/fixtures/") + name;
}

static bool nearly(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

static iris::MaterialPtr defaultMaterialFor(iris::MeshPtr, iris::MeshMaterialData &)
{
    return iris::DefaultMaterial::create();
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // ================= 1. the allowlist gate =================
    // Compiled out, ReadFile returns null and GetErrorString says the format is
    // unknown. This is the whole change under test.
    {
        Assimp::Importer plyImp, stlImp;
        const aiScene *ply = plyImp.ReadFile(fixture("colored_quad.ply").toStdString(),
                                             iris::ImportFlags::Canonical);
        if (!ply) std::printf("    assimp: %s\n", plyImp.GetErrorString());
        CHECK(ply != nullptr, "1: the PLY importer is compiled in (ASSIMP_BUILD_PLY_IMPORTER)");
        const aiScene *stl = stlImp.ReadFile(fixture("tetra_normals.stl").toStdString(),
                                             iris::ImportFlags::Canonical);
        if (!stl) std::printf("    assimp: %s\n", stlImp.GetErrorString());
        CHECK(stl != nullptr, "1: the STL importer is compiled in (ASSIMP_BUILD_STL_IMPORTER)");
        if (!ply || !stl) return 1;

        CHECK(ply->mNumMeshes == 1 && ply->mMeshes[0]->mNumVertices == 4 &&
                  ply->mMeshes[0]->mNumFaces == 2,
              "1: the PLY quad is one mesh, 4 vertices, 2 triangles");
        CHECK(stl->mNumMeshes == 1 && stl->mMeshes[0]->mNumFaces == 4,
              "1: the STL tetrahedron is one mesh, 4 facets");
        // Neither format can carry a skin, so both take loadAsSceneFragment's
        // single-mesh shortcut (guard: mNumMeshes == 1 && mNumBones == 0).
        CHECK(ply->mMeshes[0]->mNumBones == 0 && stl->mMeshes[0]->mNumBones == 0,
              "1: neither format carries bones (the single-mesh shortcut applies)");

        // PLY has no normals ON DISK: the canonical preset is what supplies
        // them. Stated against the RAW parse so the claim is not a guess.
        Assimp::Importer rawImp;
        const aiScene *rawPly = rawImp.ReadFile(fixture("colored_quad.ply").toStdString(), 0);
        CHECK(rawPly && rawPly->mMeshes[0]->mNormals == nullptr,
              "1: the .ply file itself has NO normals (precondition)");
        CHECK(ply->mMeshes[0]->mNormals != nullptr,
              "1: ... and the canonical preset generates them");
    }

    // ================= 2. PLY through our paths =================
    {
        auto node = iris::MeshNode::loadAsSceneFragment(fixture("colored_quad.ply"),
                                                        defaultMaterialFor);
        CHECK(!node.isNull(), "2: the .ply loads through MeshNode::loadAsSceneFragment");
        auto meshNode = node.dynamicCast<iris::MeshNode>();
        CHECK(!meshNode.isNull(), "2: ... as a single MeshNode (no bones, one mesh)");
        if (meshNode) {
            auto mesh = meshNode->getMesh();
            CHECK(!mesh.isNull(), "2: the document mesh is built");
            CHECK(!meshNode->getMaterial().isNull(),
                  "2: a material is created even though the file declares none");

            MeshData md;
            CHECK(SceneMirror::toMeshData(mesh.data(), md), "2: toMeshData succeeds");
            const size_t nv = md.positions.size() / 3;
            CHECK(nv == 4, "2: 4 vertices reach MeshData");
            CHECK(md.indices.size() == 6, "2: two triangles reach MeshData");
            CHECK(md.normals.size() == nv * 3, "2: generated normals reach MeshData");
            // Neither format carries UVs, so CalcTangentSpace has nothing to
            // work from — the engine's buildMeshV2 generates tangents itself
            // (a normal-mapped HlmsPbs datablock throws without them).
            CHECK(md.uvs.empty(), "2: no UVs (the format has none) — tangents are the engine's job");
        }

        // VERTEX COLOURS, stated honestly. assimp reads PLY's colour property
        // (asserted below, so the fixture and the importer are both proven),
        // but they stop at the document boundary and always have: neither
        // iris::Mesh's aiMesh constructor nor the engine's MeshData has a
        // colour channel at all — VertexAttribUsage::Color exists in the enum
        // and nothing ever writes it, and MeshData is positions/normals/uvs/
        // tangents/indices/skin only. Carrying scanner colours would be an
        // engine feature (a vertex-colour vertex element plus the HlmsPbs
        // piece that multiplies it in), not an importer one. Recorded here so
        // the next reader does not go looking for a bug.
        {
            Assimp::Importer imp;
            const aiScene *s = imp.ReadFile(fixture("colored_quad.ply").toStdString(),
                                            iris::ImportFlags::Canonical);
            CHECK(s && s->mMeshes[0]->HasVertexColors(0),
                  "2: assimp DOES read the .ply's per-vertex colours");
            if (s && s->mMeshes[0]->HasVertexColors(0)) {
                const aiMesh *m = s->mMeshes[0];
                bool ok = m->mNumVertices == 4;
                for (unsigned v = 0; v < m->mNumVertices && ok; ++v) {
                    const float x = m->mVertices[v].x, y = m->mVertices[v].y;
                    const aiColor4D c = m->mColors[0][v];
                    // (-1,-1) red, (1,-1) green, (1,1) blue, (-1,1) yellow.
                    const bool red = x < 0 && y < 0, green = x > 0 && y < 0,
                               blue = x > 0 && y > 0, yellow = x < 0 && y > 0;
                    if ((red && !(nearly(c.r, 1) && nearly(c.g, 0) && nearly(c.b, 0))) ||
                        (green && !(nearly(c.r, 0) && nearly(c.g, 1) && nearly(c.b, 0))) ||
                        (blue && !(nearly(c.r, 0) && nearly(c.g, 0) && nearly(c.b, 1))) ||
                        (yellow && !(nearly(c.r, 1) && nearly(c.g, 1) && nearly(c.b, 0)))) {
                        std::printf("    vertex (%.0f,%.0f) colour (%.2f %.2f %.2f)\n",
                                    x, y, c.r, c.g, c.b);
                        ok = false;
                    }
                }
                CHECK(ok, "2: ... each corner carries the colour the fixture authored");
            }
            bool documentHasColour = false;
            if (meshNode) {
                for (const auto &vb : meshNode->getMesh()->getVertexBuffers()) {
                    if (!vb) continue;
                    const auto attribs = vb->vertexLayout.getAttribs();
                    if (!attribs.isEmpty() &&
                        attribs.first().usage == iris::VertexAttribUsage::Color)
                        documentHasColour = true;
                }
            }
            CHECK(!documentHasColour,
                  "2: ... and the DOCUMENT drops them (no vertex-colour channel exists; "
                  "carrying them is an engine feature, not an importer one)");
        }

        // The IMPORT parse (the pipeline's convert step calls exactly this).
        QTemporaryDir tmp;
        QStringList texNames, texPaths;
        bool hasEmbedded = false;
        QJsonObject stats;
        auto imported = AssetHelper::extractTexturesAndMaterialFromMesh(
            fixture("colored_quad.ply"), texNames, texPaths, hasEmbedded, &stats, tmp.path());
        CHECK(!imported.isNull(), "2: the .ply imports through AssetHelper (the pipeline's parse)");
        CHECK(texPaths.isEmpty() && !hasEmbedded, "2: ... with no textures to discover");
    }

    // ================= 3. STL: normals, no UVs, default material =================
    {
        // 3a. The ordinary case: the file's own facet normals survive.
        Assimp::Importer imp;
        const aiScene *s = imp.ReadFile(fixture("tetra_normals.stl").toStdString(),
                                        iris::ImportFlags::Canonical);
        CHECK(s && s->mMeshes[0]->mNormals, "3a: a normal-carrying STL keeps its facet normals");
        if (s) {
            const aiMesh *m = s->mMeshes[0];
            CHECK(m->mNumVertices == 12,
                  "3a: 12 vertices — faceted, because each facet's normal differs");
            CHECK(m->mTextureCoords[0] == nullptr, "3a: no UVs (the format has none)");
            CHECK(m->mTangents == nullptr, "3a: no tangents either (CalcTangentSpace needs UVs)");
            aiString name;
            s->mMaterials[m->mMaterialIndex]->Get(AI_MATKEY_NAME, name);
            CHECK(s->mNumMaterials == 1 && QString(name.C_Str()) == "DefaultMaterial",
                  "3a: assimp supplies one DefaultMaterial (the file declares none)");
        }

        // 3b. THE assertion: Blender writes `facet normal 0 0 0`, and an STL
        // whose normals are all zero is unshadeable — every lit pixel would be
        // black. MEASURED: the raw parse hands over 12 vertices with (0,0,0)
        // normals; under the canonical preset the degenerate normals are
        // dropped (FindInvalidData), the now-identical positions collapse
        // (JoinIdenticalVertices) and GenSmoothNormals computes real ones. That
        // is the whole reason the preset is not optional for this format.
        Assimp::Importer rawImp, prepImp;
        const aiScene *raw = rawImp.ReadFile(fixture("tetra_zeronormals.stl").toStdString(), 0);
        CHECK(raw && raw->mMeshes[0]->mNormals &&
                  raw->mMeshes[0]->mNormals[0].Length() < 1e-6f,
              "3b: raw, the zero-normal STL's normals really are (0,0,0) (precondition)");
        const aiScene *prep = prepImp.ReadFile(fixture("tetra_zeronormals.stl").toStdString(),
                                               iris::ImportFlags::Canonical);
        CHECK(prep && prep->mMeshes[0]->mNormals, "3b: under the canonical preset it has normals");
        if (prep) {
            const aiMesh *m = prep->mMeshes[0];
            CHECK(m->mNumVertices == 4,
                  "3b: ... and the vertices collapsed to 4 (identical positions, one smooth normal)");
            bool unit = m->mNumVertices > 0;
            for (unsigned v = 0; v < m->mNumVertices; ++v)
                if (std::fabs(m->mNormals[v].Length() - 1.0f) > 1e-3f) unit = false;
            CHECK(unit, "3b: every generated normal is unit length");
        }

        // 3c. Through our load path, both files.
        for (const char *f : { "tetra_normals.stl", "tetra_zeronormals.stl" }) {
            auto node = iris::MeshNode::loadAsSceneFragment(fixture(f), defaultMaterialFor);
            auto meshNode = node.dynamicCast<iris::MeshNode>();
            CHECK(!meshNode.isNull(),
                  QString("3c: %1 loads as a MeshNode").arg(f).toUtf8().constData());
            if (!meshNode) continue;
            CHECK(!meshNode->getMaterial().isNull(),
                  QString("3c: %1 gets the default material").arg(f).toUtf8().constData());
            MeshData md;
            CHECK(SceneMirror::toMeshData(meshNode->getMesh().data(), md),
                  QString("3c: %1 toMeshData").arg(f).toUtf8().constData());
            const size_t nv = md.positions.size() / 3;
            bool shadeable = nv > 0 && md.normals.size() == nv * 3;
            for (size_t i = 0; i < nv && shadeable; ++i) {
                const float x = md.normals[i * 3], y = md.normals[i * 3 + 1], z = md.normals[i * 3 + 2];
                if (std::fabs(std::sqrt(x * x + y * y + z * z) - 1.0f) > 1e-2f) shadeable = false;
            }
            CHECK(shadeable,
                  QString("3c: %1 reaches the engine with unit normals (it is shadeable)")
                      .arg(f).toUtf8().constData());
        }
    }

    // ================= 4. the ONE import pipeline =================
    {
        const QString cwd = QDir::currentPath();
        const QString dbPath = cwd + "/meshformats.db";
        const QString root = cwd + "/meshformats_store";
        QFile::remove(dbPath);
        QDir(root).removeRecursively();
        QDir().mkpath(root);

        Database db;
        CHECK(db.initializeDatabase(dbPath), "4: fresh database opened");
        db.createAllTables();
        AssetStorePaths::setRootOverride(root);
        QSqlDatabase conn = QSqlDatabase::database();

        AssetImportService service(&db, nullptr);
        for (const char *f : { "colored_quad.ply", "tetra_normals.stl" }) {
            ImportRequest request;
            request.sourcePath = fixture(f);
            // NO typeHint: the sniffers have to route it, which is the point —
            // MeshImporter::sniff tests Constants::MODEL_EXTS.
            const ImportResult result = service.import(request);
            CHECK(result.ok(),
                  QString("4: %1 imports through the ONE pipeline (assets.importFile's path): %2")
                      .arg(f, result.ok() ? QStringLiteral("ok") : result.error)
                      .toUtf8().constData());
            if (!result.ok()) continue;
            QSqlQuery q(conn);
            q.prepare("SELECT type FROM assets WHERE guid = ?");
            q.addBindValue(result.assetGuid);
            q.exec();
            CHECK(q.next() && q.value(0).toInt() == static_cast<int>(ModelTypes::Object),
                  QString("4: %1 lands as an Object row").arg(f).toUtf8().constData());
            CHECK(!result.meshGuid.isEmpty(),
                  QString("4: %1 gets a Mesh member row").arg(f).toUtf8().constData());
            CHECK(!AssetCas::resolveSource(conn, root, result.assetGuid).isEmpty(),
                  QString("4: %1's source blob is in the CAS").arg(f).toUtf8().constData());
            CHECK(!result.node.isNull(),
                  QString("4: %1 hands back the parsed fragment (the completion tail's node)")
                      .arg(f).toUtf8().constData());
        }
        AssetStorePaths::setRootOverride(QString());
        db.closeDatabase();
        QFile::remove(dbPath);
        QDir(root).removeRecursively();
    }

    // ================= 5. the extension lists =================
    // These drive every sniffer AND every file dialog; they are also the half
    // of the change the assimp allowlist cannot enforce.
    {
        CHECK(Constants::MODEL_EXTS.contains("ply") && Constants::MODEL_EXTS.contains("stl"),
              "5: ply and stl are model extensions");
        CHECK(!Constants::MODEL_EXTS.contains("bvh"),
              "5: bvh is NOT a model extension (it has no geometry; assimp would "
              "hand back a synthesised stick figure)");
        CHECK(Constants::ANIMATION_EXTS.contains("bvh"),
              "5: bvh IS an animation extension (the Avatar module's Load Animation dialog)");
        bool superset = true;
        for (const auto &ext : Constants::MODEL_EXTS)
            if (!Constants::ANIMATION_EXTS.contains(ext)) superset = false;
        CHECK(superset, "5: ANIMATION_EXTS is a superset of MODEL_EXTS "
                        "(a Mixamo clip ships as .fbx/.glb WITH a mesh)");
        CHECK(AssetHelper::getAssetTypeFromExtension("ply") == ModelTypes::Mesh &&
                  AssetHelper::getAssetTypeFromExtension("stl") == ModelTypes::Mesh,
              "5: the type map calls ply/stl meshes");
        CHECK(AssetHelper::getAssetTypeFromExtension("bvh") == ModelTypes::Undefined,
              "5: ... and refuses to type a .bvh as a library asset");
    }

    // ================= 6. PIXELS: the thumbnail path =================
    // An imported model's tile is drawn by EngineThumbnailRenderer::renderNode.
    // "Doesn't crash thumbnails" is worth little without the pixels, so this
    // renders both formats and asserts something other than the background is
    // on screen.
    {
        EngineConfig cfg;
        cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
        cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
        cfg.logFile = "test_importer_meshformats-ogre.log";
        std::string err;
        std::shared_ptr<Engine> engine = Engine::create(cfg, err);
        CHECK(engine != nullptr, "6: engine created");
        if (!engine) { std::printf("    %s\n", err.c_str()); }
        else {
            // The app's viewport exists before any thumbnail is asked for.
            View *primary = engine->createOffscreenView("primary", 64, 64, Colour(0, 0, 0));
            Scene *primaryScene = engine->createScene("primary");
            primary->setScene(primaryScene);
            {
                EngineThumbnailRenderer renderer(engine);
                const Colour bg = EngineThumbnailRenderer::backgroundColour();
                for (const char *f : { "colored_quad.ply", "tetra_normals.stl",
                                       "tetra_zeronormals.stl" }) {
                    auto node = iris::MeshNode::loadAsSceneFragment(fixture(f), defaultMaterialFor);
                    const QImage thumb = renderer.renderNode(node, QSize(96, 96));
                    CHECK(!thumb.isNull() && thumb.size() == QSize(96, 96),
                          QString("6: %1 renders a 96x96 thumbnail").arg(f).toUtf8().constData());
                    if (thumb.isNull()) continue;
                    int nonBackground = 0;
                    for (int y = 0; y < thumb.height(); ++y)
                        for (int x = 0; x < thumb.width(); ++x) {
                            const QColor c = thumb.pixelColor(x, y);
                            if (std::fabs(c.redF() - bg.r) > 0.04f ||
                                std::fabs(c.greenF() - bg.g) > 0.04f ||
                                std::fabs(c.blueF() - bg.b) > 0.04f)
                                ++nonBackground;
                        }
                    std::printf("    %-24s %d/%d non-background pixels, centre %d %d %d\n", f,
                                nonBackground, thumb.width() * thumb.height(),
                                thumb.pixelColor(48, 48).red(), thumb.pixelColor(48, 48).green(),
                                thumb.pixelColor(48, 48).blue());
                    CHECK(nonBackground > 200,
                          QString("6: %1 actually draws geometry (not an empty frame)")
                              .arg(f).toUtf8().constData());
                }
            }
            engine->destroyView(primary);
            engine->destroyScene(primaryScene);
            engine.reset();
        }
    }

    std::printf(failures ? "\nFAILED: %d checks\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
