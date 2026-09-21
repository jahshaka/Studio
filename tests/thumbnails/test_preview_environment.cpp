// THE STUDIO ENVIRONMENT, MEASURED (MATPREVIEW-ENV-1, owner review R9).
//
// One environment lights every preview surface, so the assertions are about
// what the picture it makes DOES, not about a hash: a chrome sphere shows the
// softboxes as bright blobs and never burns out; an 18% grey diffuse sphere
// lands on the grey card; a thumbnail of a material and the preview of the same
// material agree; a thumbnail is byte-identical from one render to the next;
// and the subject sits inside the frame at every dock shape, which is the half
// of R9 the owner reported as "clipped at the panel's right edge".
//
// TWO THINGS EVERY NUMBER HERE DEPENDS ON:
//
//  * AN OFFSCREEN READBACK IS NOT DISPLAY-ENCODED (cameralens.h records it: the
//    render window's target is sRGB and the hardware encodes the shader's
//    linear output, a plain readback is not). So every measurement is taken on
//    the sRGB-ENCODED picture — what the dock shows, and what the numbers in
//    the brief are in. The raw code is printed beside it.
//  * THE SILHOUETTE comes from an EMISSIVE GREEN subject, not from "the dark
//    pixels": the studio has a dim floor half, so darkness is not a subject.
//    Green against a neutral environment is separable at any exposure.
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QColor>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "../support/previewdump.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "jahshaka/engine/Engine.h"
#include "bridge/enginematerialpreviewscene.h"
#include "bridge/enginethumbnailrenderer.h"
#include "bridge/previewenvironment.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static iris::MaterialPtr pbr(QColor base, float metallic, float roughness)
{
    auto m = iris::PbrMaterial::create();
    m->setBaseColor(base);
    m->setMetallicFactor(metallic);
    m->setRoughnessFactor(roughness);
    return m.staticCast<iris::Material>();
}

/// The silhouette material: green whatever the light does to it.
static iris::MaterialPtr marker()
{
    auto m = iris::PbrMaterial::create();
    m->setBaseColor(QColor(0, 0, 0));
    m->setMetallicFactor(0.0f);
    m->setRoughnessFactor(1.0f);
    m->setEmissiveColor(QColor(0, 255, 0));
    m->setEmissiveIntensity(1.0f);
    return m.staticCast<iris::Material>();
}

/// One preview render at a given view size, as an 8-bit image.
static QImage previewShot(EngineMaterialPreviewScene &preview, Engine &engine,
                          View *view, const char *tag = "preview", int frames = 4)
{
    for (int i = 0; i < frames; ++i) {
        preview.step(1.0f / 60.0f, int(view->width()), int(view->height()));
        engine.renderOneFrame();
    }
    Image img;
    if (!view->readPixels(img)) return QImage();
    QImage out(int(img.width), int(img.height), QImage::Format_RGBA8888);
    for (unsigned y = 0; y < img.height; ++y)
        std::memcpy(out.scanLine(int(y)), &img.rgba[size_t(y) * img.width * 4u], img.width * 4u);
    previewdump::save(tag, out);
    return out;
}

static int luma(QRgb p) { return qRound(0.2126 * qRed(p) + 0.7152 * qGreen(p) + 0.0722 * qBlue(p)); }

/// The linear readback as a display would show it (sRGB encode, per channel).
static QImage toDisplay(const QImage &img)
{
    QImage out = img;
    for (int y = 0; y < out.height(); ++y)
        for (int x = 0; x < out.width(); ++x) {
            const QRgb p = img.pixel(x, y);
            int c[3] = { qRed(p), qGreen(p), qBlue(p) };
            for (int &v : c) {
                const double l = double(v) / 255.0;
                const double e = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
                v = int(std::lround(e * 255.0));
            }
            out.setPixel(x, y, qRgb(c[0], c[1], c[2]));
        }
    return out;
}

/// The subject's silhouette: an emissive-green material against a neutral
/// studio, so "is this the subject" needs no model of where a sphere is.
static std::vector<bool> silhouette(const QImage &img)
{
    std::vector<bool> mask(size_t(img.width()) * img.height(), false);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = img.pixel(x, y);
            mask[size_t(y) * img.width() + x] =
                qGreen(p) > 40 && qGreen(p) > qRed(p) * 5 / 4 && qGreen(p) > qBlue(p) * 5 / 4;
        }
    return mask;
}

static double meanOver(const QImage &img, const std::vector<bool> &mask)
{
    double sum = 0.0; long n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (mask[size_t(y) * img.width() + x]) { sum += luma(img.pixel(x, y)); ++n; }
    return n ? sum / double(n) : 0.0;
}

/// True when any silhouette pixel touches the outermost `band` rows/columns —
/// the subject is clipped (or about to be) at this aspect.
static bool touchesEdge(const QImage &img, const std::vector<bool> &mask, int band = 1)
{
    const int W = img.width(), H = img.height();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (mask[size_t(y) * W + x] &&
                (x < band || y < band || x >= W - band || y >= H - band))
                return true;
    return false;
}

static long countMask(const std::vector<bool> &m)
{
    long n = 0; for (bool b : m) if (b) ++n; return n;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_preview_environment-ogre.log";
    std::string err;
    std::shared_ptr<Engine> engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    // ---- 0. the environment's own constants ----
    {
        // WHAT IT COSTS TO EXIST, once per process: the picture and the nine
        // ambient bands are built together on first use, on the thread that
        // asked (the UI thread, at the first preview or the first thumbnail).
        // Printed rather than asserted — it is a number a lane changing the
        // size should see move, not a budget.
        QElapsedTimer built;
        built.start();
        const QImage &env = previewenv::image();
        previewenv::ambientSh();
        std::printf("    the environment was generated in %lld ms\n",
                    static_cast<long long>(built.elapsed()));
        CHECK(env.width() == previewenv::size().width() && env.height() == previewenv::size().height()
              && !env.isNull(), "the studio environment is generated at its stated size");
        const float *sh = previewenv::ambientSh();
        std::printf("    environment: %dx%d  mean radiance %.4f  EV %.4f stops (chain %.4f)  "
                    "%zu softboxes\n", env.width(), env.height(), double(sh[0]),
                    double(previewenv::exposureEv()), double(previewenv::exposureChain()),
                    previewenv::softboxes().size());
        CHECK(sh[0] > 0.0f && std::isfinite(previewenv::exposureEv()),
              "its ambient integral and its derived exposure are finite and positive");
        CHECK(previewenv::softboxes().size() >= 2, "it carries at least two softboxes");
    }

    const int W = 256, H = 256;
    View *view = engine->createOffscreenView("matpreview", unsigned(W), unsigned(H), Colour(0, 0, 0));
    CHECK(view != nullptr, "offscreen preview view");
    if (!view) return 1;

    double previewGreyMean = 0.0;
    {
        EngineMaterialPreviewScene preview(engine);
        CHECK(preview.attach(view), "the preview scene attached");

        // ---- 1. the backdrop the dock shows ----
        preview.setMaterial(pbr(QColor(118, 118, 118), 0.0f, 1.0f));
        const QImage plate = toDisplay(previewShot(preview, *engine, view, "backdrop-and-grey"));
        std::printf("    backdrop: corner %d, top-centre %d (display codes)\n",
                    luma(plate.pixel(2, 2)), luma(plate.pixel(W / 2, 2)));
        CHECK(luma(plate.pixel(2, 2)) > 60 && luma(plate.pixel(2, 2)) < 250,
              "the studio backdrop is a lit neutral, neither black nor blown");

        // ---- 2. a chrome sphere: the softboxes read, and nothing burns out ----
        preview.setMaterial(pbr(QColor(255, 255, 255), 1.0f, 0.05f));
        const QImage chrome = toDisplay(previewShot(preview, *engine, view, "chrome"));
        CHECK(!chrome.isNull(), "the chrome sphere rendered");

        // ---- 3. the silhouette, and the sphere is whole at every dock shape ----
        preview.setMaterial(marker());
        const QImage green = toDisplay(previewShot(preview, *engine, view, "silhouette"));
        const std::vector<bool> mask = silhouette(green);
        const long area = countMask(mask);
        std::printf("    silhouette: %ld px of %d (%.1f%% of the frame); framed at %.3f\n",
                    area, W * H, 100.0 * double(area) / double(W * H),
                    double(preview.framedDistance()));
        CHECK(area > (W * H) / 12, "the subject fills a real part of the frame");

        int maxChannel = 0;
        long clipped = 0;
        for (int y = 0; y < chrome.height(); ++y)
            for (int x = 0; x < chrome.width(); ++x) {
                if (!mask[size_t(y) * chrome.width() + x]) continue;   // the sphere only
                const QRgb p = chrome.pixel(x, y);
                const int m = std::max({ qRed(p), qGreen(p), qBlue(p) });
                maxChannel = std::max(maxChannel, m);
                if (m >= 250) ++clipped;
            }
        std::printf("    chrome sphere: max channel %d, %ld of %ld sphere px at or above 250 (%.2f%%)\n",
                    maxChannel, clipped, area, 100.0 * double(clipped) / double(std::max(1L, area)));
        CHECK(maxChannel < 250, "the chrome sphere's brightest reflection stays under 250/255");

        // the highlights themselves: bright blobs on BOTH sides of the sphere's
        // upper half — one softbox each, which a single point light cannot do.
        long brightLeft = 0, brightRight = 0;
        for (int y = 0; y < chrome.height() / 2; ++y)
            for (int x = 0; x < chrome.width(); ++x) {
                if (!mask[size_t(y) * chrome.width() + x]) continue;
                if (luma(chrome.pixel(x, y)) < 200) continue;
                (x < chrome.width() / 2 ? brightLeft : brightRight) += 1;
            }
        std::printf("    chrome highlights: %ld bright px left of centre, %ld right\n",
                    brightLeft, brightRight);
        CHECK(brightLeft > 20 && brightRight > 20,
              "both softboxes show as bright reflections in the sphere's upper half");

        // ---- 4. an ORDINARY material does not burn out anywhere ----
        preview.setMaterial(pbr(QColor(200, 200, 200), 0.0f, 0.35f));
        const QImage glossy = toDisplay(previewShot(preview, *engine, view, "glossy"));
        long glossyClipped = 0;
        int glossyMax = 0;
        for (int y = 0; y < glossy.height(); ++y)
            for (int x = 0; x < glossy.width(); ++x) {
                if (!mask[size_t(y) * glossy.width() + x]) continue;
                const QRgb p = glossy.pixel(x, y);
                const int m = std::max({ qRed(p), qGreen(p), qBlue(p) });
                glossyMax = std::max(glossyMax, m);
                if (m >= 250) ++glossyClipped;
            }
        std::printf("    plain glossy dielectric: max channel %d, %ld clipped px\n",
                    glossyMax, glossyClipped);
        CHECK(glossyClipped == 0, "an ordinary glossy material has no burnt-out pixel at all");

        // ---- 5. an 18% grey diffuse sphere reads as the grey card ----
        preview.setMaterial(pbr(QColor(118, 118, 118), 0.0f, 1.0f));
        const QImage grey = toDisplay(previewShot(preview, *engine, view, "grey-card"));
        previewGreyMean = meanOver(grey, mask);
        std::printf("    18%% grey diffuse sphere: mean %.1f/255 over the silhouette (display)\n",
                    previewGreyMean);
        CHECK(previewGreyMean >= 105.0 && previewGreyMean <= 130.0,
              "the 18% grey sphere lands on the grey card (105-130)");

        // ---- 6. the subject is inside the frame at every dock shape ----
        struct Shape { const char *name; int w, h; };
        const Shape shapes[] = { { "1:1", 256, 256 }, { "16:9", 320, 180 }, { "2.4:1", 384, 160 },
                                 { "tall 3:4", 192, 256 }, { "narrow 5:8", 160, 256 } };
        for (const Shape &sh : shapes) {
            view->resize(unsigned(sh.w), unsigned(sh.h));
            preview.setMaterial(marker());
            const QImage shot = toDisplay(previewShot(preview, *engine, view, sh.name));
            const std::vector<bool> m = silhouette(shot);
            const long a = countMask(m);
            const bool edge = touchesEdge(shot, m, 1);
            std::printf("    %-11s %3dx%3d: silhouette %5ld px (%.1f%%), edge %s\n", sh.name, sh.w, sh.h,
                        a, 100.0 * double(a) / double(sh.w * sh.h), edge ? "TOUCHED" : "clear");
            char msg[160];
            std::snprintf(msg, sizeof msg,
                          "the sphere is whole at %s (no silhouette pixel on the frame edge)", sh.name);
            CHECK(a > 200 && !edge, msg);
        }
        view->resize(unsigned(W), unsigned(H));
    }
    engine->destroyView(view);

    // ---- 7. the thumbnail: deterministic, and it agrees with the preview ----
    {
        auto loan = EngineThumbnailRenderer::borrow(engine, "the studio-environment test");
        CHECK(!!loan, "the shared thumbnail renderer was lent");
        if (loan) {
            const QImage raw1 = loan->renderMaterial(marker(), QSize(128, 128));
            previewdump::save("thumb-silhouette", raw1);
            const QImage raw2 = loan->renderMaterial(marker(), QSize(128, 128));
            CHECK(!raw1.isNull() && raw1 == raw2,
                  "two renders of the same material are byte-identical");
            const std::vector<bool> mask = silhouette(toDisplay(raw1));
            std::printf("    thumbnail silhouette: %ld px of %d (%.1f%%)\n", countMask(mask),
                        128 * 128, 100.0 * double(countMask(mask)) / double(128 * 128));
            CHECK(countMask(mask) > 128 * 128 / 12, "the thumbnail's sphere fills the tile");
            CHECK(!touchesEdge(raw1, mask, 1), "the thumbnail's sphere is whole in its tile");

            const QImage greyRaw = loan->renderMaterial(pbr(QColor(118, 118, 118), 0.0f, 1.0f),
                                                        QSize(128, 128));
            previewdump::save("thumb-grey-card", greyRaw);
            const QImage grey = toDisplay(greyRaw);
            const double thumbMean = meanOver(grey, mask);
            std::printf("    thumbnail 18%% grey: mean %.1f/255 (preview %.1f) — difference %.1f\n",
                        thumbMean, previewGreyMean, std::fabs(thumbMean - previewGreyMean));
            CHECK(thumbMean >= 105.0 && thumbMean <= 130.0,
                  "the thumbnail's 18% grey sphere lands on the grey card too");
            CHECK(std::fabs(thumbMean - previewGreyMean) <= 5.0,
                  "the thumbnail and the preview of one material agree within 5/255");

            const QImage chromeRaw = loan->renderMaterial(pbr(QColor(255, 255, 255), 1.0f, 0.05f),
                                                          QSize(128, 128));
            previewdump::save("thumb-chrome", chromeRaw);
            const QImage chrome = toDisplay(chromeRaw);
            int maxChannel = 0;
            for (int y = 0; y < chrome.height(); ++y)
                for (int x = 0; x < chrome.width(); ++x) {
                    if (!mask[size_t(y) * chrome.width() + x]) continue;
                    const QRgb p = chrome.pixel(x, y);
                    maxChannel = std::max({ maxChannel, qRed(p), qGreen(p), qBlue(p) });
                }
            std::printf("    thumbnail chrome: max channel %d\n", maxChannel);
            CHECK(maxChannel < 250, "the chrome thumbnail does not burn out either");
        }
    }
    EngineThumbnailRenderer::shutdown();

    std::printf(failures ? "\n%d FAILURES\n" : "\nall good\n", failures);
    return failures ? 1 : 0;
}
