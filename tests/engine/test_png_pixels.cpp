// engine.png_pixels — THE PNG IS THE HELD PICTURE, PIXEL FOR PIXEL (MCP-ON-HOST-1 stage 3, the lead's ruling on the reader's
// G1; jahshaka/engine/ImageCodec.h). On the headless (NULL) engine - its Root registers the image codecs - encodePng is
// called directly and its PNG DECODED HERE, by this file's own decoder (the system zlib's inflate and the five PNG filters
// undone), never by the codec that encoded: decoding through the same codec would undo its own transforms and pass a picture
// whose R and B were swapped (FreeImage's BGR order) or whose rows were flipped (its bottom-up bitmap).
// THE LAWS: an asymmetric 4 x 4 whose alpha is below 255 decodes (colour type 6, RGBA) to its held bytes EXACTLY; its
// all-opaque twin decodes (colour type 2, RGB: the alpha 255 by the PNG's own rule) to its held bytes EXACTLY; a 6 x 4
// gradient downscaled by boxDownscale at maxEdge 3 (s = 2: 3 x 2) and encoded decodes to boxDownscale's own output EXACTLY
// (the downscaled picture's orientation). THE CONTROL: the two 4 x 4 pictures decode to DIFFERENT bytes (the decoder reads
// what it is given). No card, no display.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/ImageCodec.h"
#include "jahshaka/engine/Pictures.h"
#include "enginetesthelpers.h"   // the headless (NULL) engine's config

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int gFail = 0;
static void check(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
    std::fflush(stdout);
    if (!ok) ++gFail;
}

static std::uint32_t be32(const std::uint8_t *p)
{
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3];
}

/// A decoded PNG: RGBA rows, top-left origin (a type-2 picture's alpha filled with 255); `why` names a refusal.
struct Decoded {
    bool ok = false;
    unsigned width = 0, height = 0;
    int colour = -1;
    std::vector<std::uint8_t> rgba;
    std::string why;
};

/// THIS ROW's DECODER: an 8-bit RGB or RGBA PNG, non-interlaced - its chunks walked, the IDATs inflated by zlib, each
/// row's filter (none, sub, up, average, Paeth: the PNG's five) undone.
static Decoded decodePng(const std::vector<std::uint8_t> &png)
{
    Decoded d;
    static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (png.size() < 8 || !std::equal(sig, sig + 8, png.begin())) return d.why = "no PNG signature", d;
    std::vector<std::uint8_t> idat;
    int depth = -1, interlace = -1;
    bool ended = false;
    for (std::size_t at = 8; at + 12 <= png.size() && !ended;) {
        const std::uint32_t n = be32(&png[at]);
        if (at + 12 + std::size_t(n) > png.size()) return d.why = "a chunk cut short", d;
        const std::string type(png.begin() + long(at) + 4, png.begin() + long(at) + 8);
        const std::uint8_t *data = &png[at + 8];
        if (type == "IHDR" && n == 13) {
            d.width = be32(data);
            d.height = be32(data + 4);
            depth = data[8];
            d.colour = data[9];
            interlace = data[12];
        } else if (type == "IDAT") {
            idat.insert(idat.end(), data, data + n);
        }
        ended = type == "IEND";
        at += 12 + std::size_t(n);
    }
    if (!ended || depth != 8 || (d.colour != 2 && d.colour != 6) || interlace != 0)
        return d.why = "not an 8-bit RGB(A) plain PNG ending in IEND (depth " + std::to_string(depth) + ", colour type " +
                       std::to_string(d.colour) + ")", d;
    const std::size_t bpp = d.colour == 6 ? 4 : 3, stride = std::size_t(d.width) * bpp;
    std::vector<std::uint8_t> raw(std::size_t(d.height) * (stride + 1));
    uLongf got = uLongf(raw.size());
    if (uncompress(raw.data(), &got, idat.data(), uLong(idat.size())) != Z_OK || got != raw.size())
        return d.why = "the IDATs do not inflate to height x (stride + 1) bytes", d;
    std::vector<std::uint8_t> prev(stride, 0), line(stride);
    for (unsigned y = 0; y < d.height; ++y) {
        const std::uint8_t filter = raw[y * (stride + 1)];
        const std::uint8_t *in = &raw[y * (stride + 1) + 1];
        for (std::size_t i = 0; i < stride; ++i) {
            const int a = i >= bpp ? line[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
            int pred = 0;
            if (filter == 1) pred = a;
            else if (filter == 2) pred = b;
            else if (filter == 3) pred = (a + b) / 2;
            else if (filter == 4) {
                const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                pred = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
            } else if (filter != 0) {
                return d.why = "row " + std::to_string(y) + ": filter " + std::to_string(filter) + " is none of the five", d;
            }
            line[i] = std::uint8_t(in[i] + pred);
        }
        for (unsigned x = 0; x < d.width; ++x) {
            d.rgba.insert(d.rgba.end(), &line[x * bpp], &line[x * bpp] + 3);
            d.rgba.push_back(bpp == 4 ? line[x * bpp + 3] : 255);
        }
        prev = line;
    }
    d.ok = true;
    return d;
}

/// The picture encoded by the engine's door and decoded here: its decode equals `want` (and its colour type `colour`).
static Decoded roundTrip(const Image &image, const char *what)
{
    std::vector<std::uint8_t> png;
    std::string why;
    if (!encodePng(image, png, why)) {
        check(false, std::string(what) + ": encodePng refused: " + why);
        return Decoded();
    }
    Decoded d = decodePng(png);
    if (!d.ok) check(false, std::string(what) + ": this row's decoder refused its PNG: " + d.why);
    return d;
}

int main()
{
    std::string bootError;
    const std::unique_ptr<Engine> engine =
        Engine::create(enginetest::testEngineConfig("test-png-pixels-ogre.log", enginetest::EngineOverrides().headless()),
                       bootError);
    check(engine != nullptr, "the headless (NULL) engine boots: its Root holds the image codecs (" + bootError + ")");
    if (engine) {
        // THE ASYMMETRIC 4 x 4: every channel of every pixel differs (R, G, B, A each a distinct function of x and y), so a
        // channel swap, a row flip or a column flip each moves bytes; its alpha below 255 (colour type 6)
        Image a;
        a.width = a.height = 4;
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 4; ++x)
                a.rgba.insert(a.rgba.end(), {std::uint8_t(10 + 40 * x + y), std::uint8_t(20 + 3 * x + 50 * y),
                                             std::uint8_t(200 - 7 * x - 30 * y), std::uint8_t(100 + 10 * x + 5 * y)});
        Image opaque = a;
        for (std::size_t i = 3; i < opaque.rgba.size(); i += 4) opaque.rgba[i] = 255;
        const Decoded da = roundTrip(a, "the 4 x 4"), dopaque = roundTrip(opaque, "the opaque 4 x 4");
        check(da.ok && da.colour == 6 && da.width == 4 && da.height == 4 && da.rgba == a.rgba,
              "the asymmetric 4 x 4 with alpha below 255 decodes (colour type " + std::to_string(da.colour) +
                  ") to its held RGBA EXACTLY: no channel order, no row order moved");
        check(dopaque.ok && dopaque.colour == 2 && dopaque.rgba == opaque.rgba,
              "its ALL-OPAQUE twin decodes (colour type " + std::to_string(dopaque.colour) +
                  ", RGB: alpha 255 by the PNG's own rule) to its held RGBA EXACTLY");
        check(da.ok && dopaque.ok && da.rgba != dopaque.rgba,
              "THE CONTROL: the two pictures decode to DIFFERENT bytes (the decoder reads what it is given)");
        // THE DOWNSCALED PICTURE's ORIENTATION: a 6 x 4 gradient at maxEdge 3 (s = 2: 3 x 2), encoded, decodes to
        // boxDownscale's own output
        Image g;
        g.width = 6;
        g.height = 4;
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 6; ++x)
                g.rgba.insert(g.rgba.end(), {std::uint8_t(30 * x), std::uint8_t(60 * y), std::uint8_t(5 * x + 40 * y), 255});
        const Image small = boxDownscale(g, 3);
        const Decoded ds = roundTrip(small, "the downscaled 3 x 2");
        check(small.width == 3 && small.height == 2 && ds.ok && ds.width == 3 && ds.height == 2 && ds.rgba == small.rgba,
              "a 6 x 4 gradient downscaled at maxEdge 3 to " + std::to_string(small.width) + " x " +
                  std::to_string(small.height) + " decodes to boxDownscale's own output EXACTLY (its orientation kept)");
    }
    if (gFail) std::printf("engine.png_pixels: FAILED (%d)\n", gFail);
    else std::printf("engine.png_pixels: PASSED\n");
    std::fflush(stdout);
    return gFail ? 1 : 0;
}
