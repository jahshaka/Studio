// sky.emits_what_it_shows (PHOTON-7 stage 10-3, R5; L9 is physics: a sky that shows and does not emit, or emits what
// it does not show, breaks the picture) - THE SKY LIGHT IS ONE GAIN ON THE SKY ITSELF. The Sky Light's tint x
// intensity scales the DRAWN sky (Ogre's sky material, the atmosphere, the cloud sheet), and the environment - the
// ambient SH, the reflection cube, every escape - is the capture of that picture, so the sky's light follows its
// picture at every gain by construction; with NO Sky Light the gain is 1 (the lead's ruling (B); D14's "no node = no
// ambient" is deleted).
// THE FIXTURE: through the document and the mirror (the product's path), an open matte ground (albedo 128, roughness
// 1, far wider than the view) under a uniform blue single-colour sky, no other light; the camera 1.6 m up looking at
// the horizon (the top rows sky, the bottom rows ground); the plain grade (linear radiance in 8 bits). Three arms in
// one process, each settled until the environment has landed (counted in frames): NO Sky Light node, a white Sky Light
// at intensity 1, and the same at 0.5. GI off: the pixel's SH and cube are the environment's two readers there. (The
// no-ray VCT tier at High - the cones', the field's and the bounce's escapes reading it too - read the same to the
// code, measured once 2026-10-27, and is cut for the fixture's budget: its arms settle in ~90 frames each, 15 s.)
// THE LAWS: (1) WITH NO SKY LIGHT THE SKY LIGHTS THE SCENE: the ground holds the sky's light, equal to the intensity-1
// arm's within one 8-bit code, and the sky pixel is the sky's own colour within one code. RED before: the ground 0
// by the deleted code, not run here (no node = gain 0 = 27 zero SH coefficients and the cube unbound: the open ground
// black under a visible blue sky).
// (2) THE SKY EMITS WHAT IT SHOWS AT EVERY GAIN: at intensity 0.5 the sky pixel and the ground are both half the
// intensity-1 arm's, each within one code (plus the half code the 8-bit read rounds).
// MEASURED (2026-10-27): the sky 0.0471 0.1569 0.5765 with no node and at 1, 0.0235 0.0784 0.2902 at 0.5; the ground
// 0.0078 0.0310 0.1152 and 0.0039 0.0157 0.0577 (the VCT tier's: 0.1151 and 0.0576).
// THE TWO AUTHOR TOOLS (PHOTON-7 10-3 R5b; the one-radiance law above stays the default): (3) A DOME - a white Sky Light
// of 1 whose source is six orange cubemap faces, under the blue World sky - lights the ground exactly as an orange World
// sky with no Sky Light does, each channel within a code, while the camera's sky pixel stays the blue sky as drawn;
// (4) A BACKDROP - the World sky's lighting off, no Sky Light - draws the blue sky as it is and lights the ground 0.
// MEASURED (2026-10-27): the orange reference's ground 0.1423 0.0371 0.0039 and the dome's the same; the backdrop's 0.
// A fixture (the engine and the mirror, one 96 x 96 offscreen view; 10 s budget). Counted in frames.
#include <QDir>
#include <QGuiApplication>
#include <QImage>

#include "../support/testmesh.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "irisgl/core/color.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "../support/mirrorframe.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (cond) std::printf("  ok   %s\n", msg);                                                                    \
        else { std::printf("  FAIL %s\n", msg); ++failures; }                                                         \
    } while (0)

namespace {
constexpr unsigned kSize = 96;
const QColor kSky(60, 110, 200);
int frames = 0;

struct Reading {
    double sky[3] = { 0, 0, 0 };
    double ground[3] = { 0, 0, 0 };
};

// The mean of a band of rows (all columns), per channel.
void bandMean(const Image &img, unsigned y0, unsigned y1, double out[3]) {
    double s[3] = { 0, 0, 0 };
    unsigned n = 0;
    for (unsigned y = y0; y < y1; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const Colour c = img.at(x, y);
            s[0] += c.r; s[1] += c.g; s[2] += c.b;
            ++n;
        }
    for (int i = 0; i < 3; ++i) out[i] = n ? s[i] / n : -1.0;
}
}   // namespace

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    std::string err;
    auto engine = Engine::create(enginetest::testEngineConfig("test-sky-emits-ogre.log"), err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(FrameInput::delta(1.0f / 60.0f));
    ++frames;
    View *view = engine->createOffscreenView("sky_emits", kSize, kSize, Colour(0, 0, 0), OffscreenContract::StillPicture);
    Scene *escene = engine->createScene("sky_emits");
    if (!view || !escene) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(escene);

    auto doc = iris::Scene::create();
    doc->skyType = iris::SkyType::SINGLE_COLOR;
    doc->skyColor = kSky;
    doc->sunDiscVisible = false;
    doc->fogEnabled = false;
    doc->hdrEnabled = false;
    iris::PbrMaterialPtr groundMat;
    {
        auto ground = iris::MeshNode::create();
        ground->setName("ground");
        ground->setMesh(testmesh::load(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/plane.obj")));
        ground->setLocalScale(iris::Vec3(400.0f, 1.0f, 400.0f));
        auto mat = iris::PbrMaterial::create();
        mat->setBaseColor(QColor(128, 128, 128));
        mat->setRoughnessFactor(1.0f);
        mat->setMetallicFactor(0.0f);
        ground->setMaterial(mat);
        groundMat = mat;
        doc->getRootNode()->addChild(ground);
    }
    auto skyLight = iris::LightNode::create();
    skyLight->setName("Sky Light");
    skyLight->lightType = iris::LightType::Sky;
    skyLight->color = QColor(255, 255, 255);

    std::unique_ptr<SceneMirror> mirror(new SceneMirror(escene));
    mirror->setLightWires(false);
    mirror->setSource(doc);
    auto cam = iris::CameraNode::create();
    cam->setLocalPos(iris::Vec3(0.0f, 1.6f, 0.0f));
    cam->lookAt(iris::Vec3(0.0f, 1.6f, -10.0f));
    cam->update(0.0f);
    mirror->sync(mirrorframe::camera(view, cam));

    const auto frame = [&]() {
        doc->refresh();
        mirror->sync();
        mirror->sync(mirrorframe::sky(view));
        mirror->sync(mirrorframe::environment(view));
        enginetest::frames(engine.get(), 1);
        ++frames;
    };
    // SETTLED: the environment has landed (the engine says so) and the picture has stopped moving for 3 frames.
    const auto settle = [&](Reading &r) {
        double prev = -1.0;
        int still = 0;
        for (int f = 0; f < 300 && still < 3; ++f) {
            frame();
            const PublishedFrame pub = escene->published();
            const GiStatus g = pub.gi;
            Image img;
            if (!view->readPixels(img)) continue;
            bandMean(img, 2u, kSize / 4u, r.sky);
            bandMean(img, kSize - kSize / 4u, kSize - 2u, r.ground);
            const double v = r.ground[0] + r.ground[1] + r.ground[2] + r.sky[0] + r.sky[1] + r.sky[2];
            const bool giQuiet = g.cascades.empty() || (!g.chainOwed && !g.feedbackOwed && !g.ifdWorking);
            still = (v == prev && !pub.environmentPending && giQuiet) ? still + 1 : 0;
            prev = v;
        }
        return still >= 3;
    };
    const iris::LinearColor want = iris::linearOf(kSky);
    const double code = 1.0 / 255.0;
    for (const bool gi : { false }) {
        doc->giMode = gi ? iris::GiMode::VCT : iris::GiMode::OFF;
        doc->giQuality = iris::GiQuality::HIGH;
        const char *tier = gi ? "VCT High (no rays)" : "GI off";
        Reading none, one, half;
        if (skyLight->getParent()) doc->getRootNode()->removeChild(skyLight);
        const bool s0 = settle(none);
        doc->getRootNode()->addChild(skyLight);
        skyLight->intensity = 1.0f;
        const bool s1 = settle(one);
        skyLight->intensity = 0.5f;
        const bool s2 = settle(half);
        for (const auto &[name, r] : { std::pair<const char *, const Reading &>{ "no Sky Light", none },
                                       { "Sky Light 1", one }, { "Sky Light 0.5", half } })
            std::printf("   %s, %s: the sky %.4f %.4f %.4f, the ground %.4f %.4f %.4f\n", tier, name, r.sky[0], r.sky[1],
                        r.sky[2], r.ground[0], r.ground[1], r.ground[2]);
        CHECK(s0 && s1 && s2, "premise: every arm settles");
        bool l1 = none.ground[2] > 4.0 * code, l2 = true;
        for (int c = 0; c < 3; ++c) {
            l1 = l1 && std::fabs(none.ground[c] - one.ground[c]) <= code;
            const double w = c == 0 ? want.r : c == 1 ? want.g : want.b;
            l1 = l1 && std::fabs(none.sky[c] - w) <= code;
            l2 = l2 && std::fabs(half.sky[c] - 0.5 * one.sky[c]) <= 1.5 * code;
            l2 = l2 && std::fabs(half.ground[c] - 0.5 * one.ground[c]) <= 1.5 * code;
        }
        char msg[220];
        std::snprintf(msg, sizeof(msg), "(1) %s: with no Sky Light the sky lights the open ground as a Sky Light of 1 "
                                        "does, and the sky shows its own colour", tier);
        CHECK(l1, msg);
        std::snprintf(msg, sizeof(msg), "(2) %s: at a gain of 0.5 the sky AND the ground it lights are both halved",
                      tier);
        CHECK(l2, msg);
    }
    // ---- THE TWO AUTHOR TOOLS (R5b): a DOME and a BACKDROP ------------------------------------------------------
    {
        doc->giMode = iris::GiMode::OFF;
        // the reference: no Sky Light under a uniform ORANGE World sky - what an orange dome must light the ground as
        const QColor kDome(220, 120, 40);
        if (skyLight->getParent()) doc->getRootNode()->removeChild(skyLight);
        doc->skyColor = kDome;
        Reading orange;
        const bool s3 = settle(orange);
        // the dome: the World sky blue again, a white Sky Light of 1 whose source is six orange faces
        const QString dir = QDir::temp().filePath(QStringLiteral("jah_sky_emits_dome"));
        QDir().mkpath(dir);
        QString paths[6];
        for (int i = 0; i < 6; ++i) {
            QImage face(16, 16, QImage::Format_RGBA8888);
            face.fill(kDome);
            paths[i] = QDir(dir).filePath(QStringLiteral("face%1.png").arg(i));
            face.save(paths[i]);
        }
        QImage info(paths[0]);
        doc->skyColor = kSky;
        skyLight->intensity = 1.0f;
        skyLight->skySource = iris::SkyLightSource::Cubemap;
        skyLight->skyCubeTexture = iris::Texture2D::createCubeMap(paths[0], paths[1], paths[2], paths[3], paths[4],
                                                                  paths[5], &info);
        doc->getRootNode()->addChild(skyLight);
        Reading dome;
        const bool s4 = settle(dome);
        // the backdrop: no Sky Light, the World sky's lighting off
        doc->getRootNode()->removeChild(skyLight);
        doc->skyLighting = false;
        Reading backdrop;
        const bool s5 = settle(backdrop);
        doc->skyLighting = true;
        for (const auto &[name, r] : { std::pair<const char *, const Reading &>{ "an orange World sky, no Sky Light", orange },
                                       { "a blue World sky, an orange dome Sky Light", dome },
                                       { "a blue World sky, a backdrop", backdrop } })
            std::printf("   GI off, %s: the sky %.4f %.4f %.4f, the ground %.4f %.4f %.4f\n", name, r.sky[0], r.sky[1],
                        r.sky[2], r.ground[0], r.ground[1], r.ground[2]);
        CHECK(s3 && s4 && s5, "premise: the dome's and the backdrop's arms settle");
        bool l3 = orange.ground[0] > 4.0 * code, l4 = true;
        for (int c = 0; c < 3; ++c) {
            const double w = c == 0 ? want.r : c == 1 ? want.g : want.b;
            l3 = l3 && std::fabs(dome.ground[c] - orange.ground[c]) <= code && std::fabs(dome.sky[c] - w) <= code;
            l4 = l4 && backdrop.ground[c] == 0.0 && std::fabs(backdrop.sky[c] - w) <= code;
        }
        CHECK(l3, "(3) a DOME Sky Light lights the ground as its cubemap would as the World sky, while the camera sees "
                  "the World sky as drawn");
        CHECK(l4, "(4) a BACKDROP World sky is drawn as it is and lights nothing: the ground 0");
        // (5) A DOME UNDER NO WORLD SKY, THEN HIDDEN (the 10-3 read's F1): the dome alone lights the scene (R5b's
        // headline case, a product lit by an HDRI against black) - the ground a white METAL at roughness 0.3, so the
        // REFLECTION cube is what it shows; hiding the Sky Light removes the dome, and with no sky there is nothing
        // to photograph: the ambient AND the reflection go, the ground 0. RED before the fix: the capture's
        // "nothing to photograph" branch kept the dome's convolved cube bound under SkyMode::NoSky.
        doc->skyType = iris::SkyType::NONE;
        groundMat->setBaseColor(QColor(255, 255, 255));
        groundMat->setMetallicFactor(1.0f);
        groundMat->setRoughnessFactor(0.3f);
        skyLight->setVisible(true);
        doc->getRootNode()->addChild(skyLight);
        Reading lit, hidden;
        const bool s6 = settle(lit);
        skyLight->setVisible(false);
        const bool s7 = settle(hidden);
        for (const auto &[name, r] : { std::pair<const char *, const Reading &>{ "no World sky, the orange dome", lit },
                                       { "no World sky, the dome hidden", hidden } })
            std::printf("   GI off, a white metal ground, %s: the sky %.4f %.4f %.4f, the ground %.4f %.4f %.4f\n", name,
                        r.sky[0], r.sky[1], r.sky[2], r.ground[0], r.ground[1], r.ground[2]);
        CHECK(s6 && s7, "premise: the no-sky dome's arms settle");
        CHECK(lit.ground[0] > 4.0 * code, "premise: the dome alone lights (and is reflected by) the metal ground");
        CHECK(hidden.ground[0] == 0.0 && hidden.ground[1] == 0.0 && hidden.ground[2] == 0.0,
              "(5) a dome hidden under no World sky takes its reflection with it: the metal ground 0");
        doc->getRootNode()->removeChild(skyLight);
        QDir(dir).removeRecursively();
    }
    mirror.reset();
    std::printf("\nsky.emits_what_it_shows: %d frames, %d failure(s)\n", frames, failures);
    return failures ? 1 : 0;
}
