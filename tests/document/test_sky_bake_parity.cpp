// document.sky_bake_parity — iris::bakeGradientSky's bytes against the Qt-typed bake it replaced (NOQT-DOCUMENT-1
// stage 3, ruling 4): the document's gradient sky is now an iris::Image from iris::SrgbColor stops; the picture the
// mirror uploads and the exporter writes may not move. THE REFERENCE is the old implementation, kept here verbatim
// in Qt's types (QColor stops, a Format_RGBA8888 QImage, qBound): for seeded stops and offsets (the clamped ends
// included) every byte of the new strip equals the reference's.
#include <QColor>
#include <QImage>
#include <QtGlobal>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>

#include "irisgl/document/scenegraph/skybake.h"
#include "app/qtstdgui.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// The bake as it was before the document left Qt (irisgl/document/scenegraph/skybake.cpp at NOQT-DOCUMENT-1's
/// stage 2), verbatim but for its name.
static QImage referenceBake(const QColor &top, const QColor &mid, const QColor &bottom, float offset, int width,
                            int height)
{
    const int W = std::max(1, width);
    const int H = std::max(2, height);
    const float middle = qBound(0.01f, offset, 0.99f);

    QImage strip(W, H, QImage::Format_RGBA8888);
    if (strip.isNull()) return strip;

    for (int r = 0; r < H; ++r) {
        const float pos = 1.0f - float(r) / float(H - 1);
        float t;
        const QColor *c0, *c1;
        if (pos <= middle) { t = pos / middle;                      c0 = &bottom; c1 = &mid; }
        else               { t = (pos - middle) / (1.0f - middle);  c0 = &mid;    c1 = &top; }

        const auto lerp8 = [t](float a, float b) {
            return (unsigned char)qBound(0.0f, (a + (b - a) * t) * 255.0f, 255.0f);
        };
        const unsigned char rr = lerp8(float(c0->redF()),   float(c1->redF()));
        const unsigned char gg = lerp8(float(c0->greenF()), float(c1->greenF()));
        const unsigned char bb = lerp8(float(c0->blueF()),  float(c1->blueF()));

        unsigned char *line = strip.scanLine(r);
        for (int x = 0; x < W; ++x) {
            unsigned char *p = line + size_t(x) * 4u;
            p[0] = rr; p[1] = gg; p[2] = bb; p[3] = 255;
        }
    }
    return strip;
}

int main()
{
    std::mt19937 rng(0x5b1e5b1eu);
    std::uniform_int_distribution<int> byte(0, 255);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float offsets[] = { 0.0f, 0.005f, 0.01f, 0.5f, 0.73f, 0.99f, 1.0f };
    long bytes = 0;
    int cases = 0, bad = 0;
    bool twoTexels = false;
    for (int i = 0; i < 200; ++i) {
        const QColor top(byte(rng), byte(rng), byte(rng)), mid(byte(rng), byte(rng), byte(rng)),
            bot(byte(rng), byte(rng), byte(rng));
        const float offset = i < int(sizeof offsets / sizeof offsets[0]) ? offsets[i] : unit(rng);
        const int w = 1 + i % 5, h = 2 + (i * 37) % 300;
        const iris::Image got = iris::bakeGradientSky(qtstd::toSrgbColor(top), qtstd::toSrgbColor(mid),
                                                      qtstd::toSrgbColor(bot), offset, w, h);
        const QImage want = referenceBake(top, mid, bot, offset, w, h);
        ++cases;
        const bool sized = !got.isNull() && got.width == want.width() && got.height == want.height();
        bool equal = sized;
        for (int y = 0; sized && y < want.height(); ++y) {
            const size_t row = size_t(want.width()) * 4u;
            if (std::memcmp(got.rgba.data() + size_t(y) * row, want.constScanLine(y), row) != 0) equal = false;
            bytes += long(row);
        }
        if (!equal && !bad++)
            std::printf("    first difference: case %d (offset %.6g, %dx%d)\n", i, double(offset), w, h);
        // THE CONTROL reads the REFERENCE: a gradient that is one colour everywhere would compare equal by accident
        if (std::memcmp(want.constScanLine(0), want.constScanLine(want.height() - 1), 4) != 0) twoTexels = true;
    }
    CHECK(bad == 0, "200 seeded gradients (stops, offsets with the clamped ends, sizes): every byte equals the "
                    "Qt-typed bake's");
    std::printf("    cases %d, bytes compared %ld\n", cases, bytes);
    CHECK(bytes > 0 && twoTexels, "the control: bytes were compared, and a strip held two distinct texels");

    if (failures) std::printf("\ndocument.sky_bake_parity: FAILED (%d)\n", failures);
    else std::printf("\ndocument.sky_bake_parity: PASSED\n");
    std::fflush(stdout);
    return failures ? 1 : 0;
}
