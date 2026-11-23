// script.commands - THE PICTURE FAMILY's CASES (SCRIPT-IN-ENGINE-1 stage 2 B1; test_script_commands.cpp runs them, in
// its fixed order). The pictures are the HOST's (no engine verb: a held picture's reductions read it in place), so every
// law runs on the host with no engine: THE RADIANCE PICTURE (RULED 10: the held kind `radiance`, bound to ImageF) is made,
// reduced (mean, columnStep, hash - in linear radiance, against values worked by hand and an FNV-1a computed outside the
// engine), released (its handle stale after) and counted under THE SAME live-pictures cap as a display picture; its
// domain refused by name; a wrong-kind and a foreign handle FAIL. The display picture's reductions, which now share one
// body with the radiance's, keep their own hand-worked values.
// THE PNG FOR A VIEWER (MCP-ON-HOST-1 stage 3: engine.picture.png = ImageCodec.h's boxDownscale, then encodePng): the
// downscale's law byte-exact against hand-worked means; no upscale; the command on the headless engine answers a PNG whose
// signature, IHDR and every CRC-32 are read here (colour type 6 with an alpha below 255, type 2 when all-opaque); the
// range's two ends pass and the answer is the encoded size; its domain FAILS by name; a whole PNG's frame (pngComplete).
#include "script_commands.h"
#include "jahshaka/engine/ImageCodec.h"
#include "jahshaka/engine/Pictures.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace scriptcommands {

namespace {

Value bytesOf(const void *data, std::size_t n)
{
    Value::Bytes b(n);
    if (n) std::memcpy(b.data(), data, n);
    return Value::bytes(std::move(b));
}
Value made(int w, int h, Value rgba)
{
    return Value(Value::Object{{"width", Value(w)}, {"height", Value(h)}, {"rgba", std::move(rgba)}});
}
Value rect(int x0, int y0, int x1, int y1)
{
    return Value(Value::Object{{"x0", Value(x0)}, {"y0", Value(y0)}, {"x1", Value(x1)}, {"y1", Value(y1)}});
}
Value span(const char *axis, int a, int b, int from, int to)
{
    return Value(Value::Object{{"axis", Value(axis)}, {"a", Value(a)}, {"b", Value(b)}, {"from", Value(from)}, {"to", Value(to)}});
}
bool numbersAre(const Value &v, std::initializer_list<double> want)
{
    if (v.kind() != Kind::Array || v.asArray().size() != want.size()) return false;
    std::size_t i = 0;
    for (const double w : want)
        if (v.asArray()[i++].asNumber() != w) return false;
    return true;
}

/// A PNG's structure read on the CPU: its signature, every chunk's CRC-32 (a table CRC over the PNG's polynomial), IHDR.
struct PngWalk {
    bool signature = false, crcs = true, ended = false;
    int chunks = 0;
    std::uint32_t width = 0, height = 0;
    int depth = -1, colour = -1;
};
std::uint32_t crc32(const std::uint8_t *p, std::size_t n)
{
    static std::uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
std::uint32_t be32(const std::uint8_t *p)
{
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3];
}
PngWalk walkPng(const Value::Bytes &b)
{
    PngWalk w;
    static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    w.signature = b.size() >= 8 && std::memcmp(b.data(), sig, 8) == 0;
    std::size_t at = 8;
    while (w.signature && at + 12 <= b.size() && !w.ended) {
        const std::uint32_t n = be32(&b[at]);
        if (at + 12 + std::size_t(n) > b.size()) {
            w.crcs = false;
            break;
        }
        const std::uint8_t *type = &b[at + 4];
        w.crcs = w.crcs && crc32(type, 4 + std::size_t(n)) == be32(&b[at + 8 + n]);
        ++w.chunks;
        if (!std::memcmp(type, "IHDR", 4) && n == 13) {
            w.width = be32(type + 4);
            w.height = be32(type + 8);
            w.depth = type[12];
            w.colour = type[13];
        }
        w.ended = !std::memcmp(type, "IEND", 4);
        at += 12 + std::size_t(n);
    }
    return w;
}

}  // namespace

void casesPicture(Fixture &f)
{
    Host &h = f.none;
    // TWO PIXELS OF RADIANCE, worked by hand: (0.5, 1.25, 3, 1) and (2.5, 0.25, 1, 0) - each exact in binary
    const float px[8] = {0.5f, 1.25f, 3.0f, 1.0f, 2.5f, 0.25f, 1.0f, 0.0f};
    const Result rc = h.call("engine.radiance.create", {made(2, 1, bytesOf(px, sizeof px))});
    const Value rad = rc.status == Status::Ok ? rc.value["radiance"] : Value();
    check(rc.status == Status::Ok && rc.value["width"] == Value(2) && rc.value["height"] == Value(1) && !rad.isNull(),
          "engine.radiance.create holds 2 x 1 floats as a radiance picture: {radiance, width, height} (" + rc.message + ")");
    if (rad.isNull()) return;
    const Result m = h.call("engine.radiance.mean", {rad, rect(0, 0, 2, 1)});
    const Result s = h.call("engine.radiance.columnStep", {rad, span("column", 0, 1, 0, 1)});
    const Result hs = h.call("engine.radiance.hash", {rad});
    check(m.status == Status::Ok && numbersAre(m.value, {1.5, 0.75, 2.0, 0.5}),
          "THE RADIANCE MEAN is each channel's mean in linear radiance: [1.5, 0.75, 2, 0.5] by hand (" +
              (m.value.kind() == Kind::Array ? std::to_string(m.value.asArray()[0].asNumber()) : m.message) + " for r)");
    check(s.status == Status::Ok && s.value.asNumber() == 5.0 / 3.0,
          "THE RADIANCE STEP is the mean absolute RGB step in radiance: (2 + 1 + 2) / 3 by hand (" +
              (s.status == Status::Ok ? std::to_string(s.value.asNumber()) : s.message) + ")");
    check(hs.status == Status::Ok && hs.value == Value("92891d44eb61453b"),
          "THE RADIANCE HASH is FNV-1a 64 over the held float bytes: 92891d44eb61453b, computed outside the engine (" +
              (hs.status == Status::Ok ? hs.value.asString() : hs.message) + ")");
    // the domain, by name
    check(failedWith(h.call("engine.radiance.create", {made(2, 1, bytesOf(px, 12))}),
                     "engine.radiance.create: rgba holds 12 bytes, not width x height x 4 floats = 32") &&
              failedWith(h.call("engine.radiance.create", {made(0, 1, bytesOf(px, 0))}), "radiance.width 0 is outside 1..16384 px") &&
              failedWith(h.call("engine.radiance.mean", {rad, rect(0, 0, 3, 1)}),
                         "engine.radiance.mean: the rectangle is empty or lies outside the picture") &&
              failedWith(h.call("engine.radiance.columnStep", {rad, span("row", 0, 1, 0, 2)}),
                         "engine.radiance.columnStep: the span lies outside the picture"),
          "THE RADIANCE DOMAIN, refused by name: rgba not width x height x 4 floats, a width of 0, a rectangle or a span "
          "outside the picture");
    // the kinds: a display picture is no radiance picture (and back), a number of no host's is no handle
    const unsigned char codes[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    const Result pc = h.call("engine.picture.create", {made(2, 1, bytesOf(codes, sizeof codes))});
    const Value pic = pc.status == Status::Ok ? pc.value["picture"] : Value();
    const Result wrong = h.call("engine.radiance.mean", {pic, rect(0, 0, 1, 1)});
    const Result back = h.call("engine.picture.hash", {rad});
    const Result foreign = h.call("engine.radiance.hash", {Value(std::int64_t(1) << 40 | 7)});
    check(failedWith(wrong, "is a picture handle, not a radiance handle") &&
              failedWith(back, "is a radiance handle, not a picture handle") && failedWith(foreign, "engine.radiance.hash: radiance:"),
          "A WRONG KIND OR A FOREIGN HANDLE FAILS: a picture where a radiance is declared (" + wrong.message +
              "), a radiance where a picture is, a number no handle of this host (" + foreign.message + ")");
    // THE DISPLAY PICTURE's reductions, one body with the radiance's: their own hand-worked values
    const Result pm = h.call("engine.picture.mean", {pic, rect(0, 0, 2, 1)});
    const Result ps = h.call("engine.picture.columnStep", {pic, span("column", 0, 1, 0, 1)});
    const Result ph = h.call("engine.picture.hash", {pic});
    check(numbersAre(pm.value, {30, 40, 50, 60}) && ps.status == Status::Ok && ps.value.asNumber() == 40.0 &&
              ph.value == Value("0363b2a3926bce45"),
          "the display picture's mean [30, 40, 50, 60], step 40 codes and hash 0363b2a3926bce45 (outside the engine) hold "
          "beside the radiance's");
    // RELEASED: the handle is stale; the bytes leave the cap
    const Result rel = h.call("engine.radiance.release", {rad});
    const Result after = h.call("engine.radiance.hash", {rad});
    check(rel.status == Status::Ok && rel.value == Value(true) && failedWith(after, "engine.radiance.hash: radiance:"),
          "engine.radiance.release: the handle is stale after (" + after.message + ")");
    h.call("engine.picture.release", {pic});

    // THE SAME CAP: a host whose live pictures hold at most 40 bytes - the 32-byte radiance and an 8-byte picture fit
    // together, one more of either FAILS by name, and a release gives its bytes back
    Host::Options capped;
    capped.pictureBytesCap = 40;
    Host c(capped);
    c.setConsole([](const std::string &) {});
    registerEngineCommands(c, nullptr, "no engine: the cap's law", false);
    const Result r1 = c.call("engine.radiance.create", {made(2, 1, bytesOf(px, sizeof px))});
    const Result p1 = c.call("engine.picture.create", {made(2, 1, bytesOf(codes, sizeof codes))});
    const Result over = c.call("engine.radiance.create", {made(1, 1, bytesOf(px, 16))});
    const Result overPic = c.call("engine.picture.create", {made(1, 1, bytesOf(codes, 4))});
    c.call("engine.radiance.release", {r1.value["radiance"]});
    const Result again = c.call("engine.radiance.create", {made(1, 1, bytesOf(px, 16))});
    check(r1.status == Status::Ok && p1.status == Status::Ok &&
              failedWith(over, "engine.radiance.create: the live pictures would hold 56 bytes, over the host's cap 40") &&
              failedWith(overPic, "the live pictures would hold 44 bytes, over the host's cap 40") && again.status == Status::Ok,
          "THE RADIANCE COUNTS UNDER THE PICTURES' ONE CAP: 32 + 8 bytes fit 40, a 16-byte radiance more FAILS (" +
              over.message + "), a 4-byte picture more FAILS; THE CONTROL: released, a radiance fits again");

    // THE PNG FOR A VIEWER (MCP-ON-HOST-1 stage 3): engine.picture.png = ImageCodec.h's boxDownscale, then encodePng
    // (i) THE DOWNSCALE LAW through boxDownscale directly, against values WORKED BY HAND (never a second implementation):
    //   34 x 3 at maxEdge 16: s = ceil(34 / 16) = 3, the output ceil(34 / 3) x ceil(3 / 3) = 12 x 1; blocks 0..10 are 3 x 3
    //   (9 pixels), block 11 the last column alone, 1 x 3 (3 pixels). The pixels: R = 3x + y, G = 0 but (0,0) = 4,
    //   (3,0) = 5 and (33,0) = 2, B = 7y, A = 255.
    //   R of block k < 11: the mean of 3x over x = 3k..3k+2 is 9k + 3, of y over 0..2 is 1: 9k + 4 exactly (sum 81k + 36);
    //   block 11: 3 * 33 + 1 = 100 (sum 300 / 3). G: block 0 sums 4 = 9 * 0 + 4 -> (4 + 4) / 9 = 0 (0.444 down); block 1
    //   sums 5 = 9 * 0 + 5 -> (5 + 4) / 9 = 1 (0.556 up); block 11 sums 2 = 3 * 0 + 2 -> (2 + 1) / 3 = 1 (0.667 up); the
    //   rest 0. B: (0 + 7 + 14) / 3 = 7 everywhere (sum 63 / 9, 21 / 3). A: 255.
    //   POINT SAMPLING (each block's first pixel) would read R = 9k, G = 4 / 5 / 2, B = 0: every channel but A differs.
    // THE EXACT HALF needs an EVEN count (a mean of 9 or 3 pixels never ends in .5: the lead's ruling (A), 2026-11-23):
    //   33 x 3 at maxEdge 17: s = 2, the output 17 x 2; row 0's blocks 0..15 are 2 x 2, block 16 the last column 1 x 2; row
    //   1's blocks 2 x 1 (the bottom row alone), its block 16 the 1 x 1 corner. The pixels: R = 2x + y, G = 1 where x + y
    //   is even (else 0), B = 10y + 1, A = 200 + y.
    //   Row 0, block k < 16 (x = 2k, 2k + 1; y = 0, 1): R sums 4k + (4k + 2) + (4k + 1) + (4k + 3) = 16k + 6 ->
    //   (16k + 6 + 2) / 4 = 4k + 2 (the mean 4k + 1.5: HALF UP); block 16 (x = 32; y = 0, 1): 64 + 65 = 129 ->
    //   (129 + 1) / 2 = 65 (64.5 up). G: 2 of 4 -> (2 + 2) / 4 = 1 (0.5 up); block 16: 1 of 2 -> 1 (0.5 up). B: (1 + 11) / 2
    //   = 6. A: (200 + 201) / 2 = 200.5 -> 201.
    //   Row 1 (y = 2), block k < 16: R = (4k + 2 + 4k + 4) / 2 = 4k + 3; G: one of 2 -> 1 (0.5 up); B 21; A 202. The corner
    //   (32, 2): R 66, G 1 (32 + 2 even), B 21, A 202.
    {
        Image a;
        a.width = 34;
        a.height = 3;
        a.rgba.resize(34 * 3 * 4);
        for (unsigned y = 0; y < 3; ++y)
            for (unsigned x = 0; x < 34; ++x) {
                unsigned char *p = &a.rgba[(y * 34 + x) * 4];
                p[0] = (unsigned char)(3 * x + y);
                p[1] = (x == 0 && y == 0) ? 4 : (x == 3 && y == 0) ? 5 : (x == 33 && y == 0) ? 2 : 0;
                p[2] = (unsigned char)(7 * y);
                p[3] = 255;
            }
        const unsigned char wantA[12 * 4] = {4, 0, 7, 255,   13, 1, 7, 255,  22, 0, 7, 255,  31, 0, 7, 255,
                                             40, 0, 7, 255,  49, 0, 7, 255,  58, 0, 7, 255,  67, 0, 7, 255,
                                             76, 0, 7, 255,  85, 0, 7, 255,  94, 0, 7, 255,  100, 1, 7, 255};
        const Image sa = boxDownscale(a, 16);
        Image b;
        b.width = 33;
        b.height = 3;
        b.rgba.resize(33 * 3 * 4);
        for (unsigned y = 0; y < 3; ++y)
            for (unsigned x = 0; x < 33; ++x) {
                unsigned char *p = &b.rgba[(y * 33 + x) * 4];
                p[0] = (unsigned char)(2 * x + y);
                p[1] = (x + y) % 2 == 0 ? 1 : 0;
                p[2] = (unsigned char)(10 * y + 1);
                p[3] = (unsigned char)(200 + y);
            }
        const unsigned char wantB[17 * 2 * 4] = {
            2, 1, 6, 201,   6, 1, 6, 201,   10, 1, 6, 201,  14, 1, 6, 201,  18, 1, 6, 201,  22, 1, 6, 201,
            26, 1, 6, 201,  30, 1, 6, 201,  34, 1, 6, 201,  38, 1, 6, 201,  42, 1, 6, 201,  46, 1, 6, 201,
            50, 1, 6, 201,  54, 1, 6, 201,  58, 1, 6, 201,  62, 1, 6, 201,  65, 1, 6, 201,
            3, 1, 21, 202,  7, 1, 21, 202,  11, 1, 21, 202, 15, 1, 21, 202, 19, 1, 21, 202, 23, 1, 21, 202,
            27, 1, 21, 202, 31, 1, 21, 202, 35, 1, 21, 202, 39, 1, 21, 202, 43, 1, 21, 202, 47, 1, 21, 202,
            51, 1, 21, 202, 55, 1, 21, 202, 59, 1, 21, 202, 63, 1, 21, 202, 66, 1, 21, 202};
        const Image sb = boxDownscale(b, 17);
        check(sa.width == 12 && sa.height == 1 && sa.rgba.size() == sizeof wantA &&
                  std::memcmp(sa.rgba.data(), wantA, sizeof wantA) == 0,
              "THE BOX DOWNSCALE, byte-exact against hand-worked means: 34 x 3 at 16 -> 12 x 1 (s 3: 4/9 rounds down, 5/9 "
              "and 2/3 up, the 1 x 3 edge block its own pixels) - got " + std::to_string(sa.width) + " x " +
                  std::to_string(sa.height));
        check(sb.width == 17 && sb.height == 2 && sb.rgba.size() == sizeof wantB &&
                  std::memcmp(sb.rgba.data(), wantB, sizeof wantB) == 0,
              "THE BOX DOWNSCALE's EXACT HALF: 33 x 3 at 17 -> 17 x 2 (s 2: the half rounds UP; the 1 x 2 column, the 2 x 1 "
              "row and the 1 x 1 corner each their own pixels) - got " + std::to_string(sb.width) + " x " +
                  std::to_string(sb.height));
        // (ii) NO UPSCALE: a 3 x 3 at maxEdge 16 is an equal copy
        Image c;
        c.width = c.height = 3;
        c.rgba.assign(a.rgba.begin(), a.rgba.begin() + 36);
        const Image sc = boxDownscale(c, 16);
        check(sc.width == 3 && sc.height == 3 && sc.rgba == c.rgba,
              "a 3 x 3 picture at maxEdge 16 is an EQUAL copy (never an upscale)");
    }

    // (iii) THROUGH THE COMMAND on the headless (NULL) engine - its Root registered the image codecs: a 4 x 4 picture
    //   whose alpha is below 255 -> {png, width 4, height 4}; the PNG's signature, its IHDR (4, 4, bit depth 8, colour
    //   type 6: RGBA) and EVERY chunk's CRC-32 read here; the same picture ALL-OPAQUE is written colour type 2 (RGB: its
    //   alpha 255 by the PNG's own rule - FreeImage writes an opaque 32-bit bitmap so); the range's two ends pass; the
    //   answer is the ENCODED size (a 20 x 2 picture at maxEdge 16 is s = 2: 10 x 1)
    // (iv) its domain, by name: a radiance handle is no picture; maxEdge 15 and 2049 are outside the declared 16..2048 (the
    //   command's ONE range check); a released picture is stale; with no engine, "the engine did not start"
    if (!f.engine) {
        check(false, "engine.picture.png's cases need the headless engine");
        return;
    }
    {
        Host &e = f.headless;
        unsigned char px[4 * 4 * 4];
        for (unsigned i = 0; i < sizeof px; ++i) px[i] = (unsigned char)(i * 13 + 7);
        const Result made4 = e.call("engine.picture.create", {made(4, 4, bytesOf(px, sizeof px))});
        const Value pic4 = made4.status == Status::Ok ? made4.value["picture"] : Value();
        const Result png = e.call("engine.picture.png", {pic4, Value(1024)});
        const Value::Bytes bytes = png.status == Status::Ok ? png.value["png"].asBytes() : Value::Bytes();
        const PngWalk w = walkPng(bytes);
        check(png.status == Status::Ok && png.value["width"] == Value(4) && png.value["height"] == Value(4) && w.signature &&
                  w.crcs && w.ended && w.width == 4 && w.height == 4 && w.depth == 8 && w.colour == 6 && w.chunks > 0,
              "engine.picture.png on the headless engine: {png, width 4, height 4}, the PNG's signature, IHDR 4 x 4 at 8 "
              "bits colour type 6 (" + std::to_string(w.width) + " x " + std::to_string(w.height) + ", " +
                  std::to_string(w.depth) + ", " + std::to_string(w.colour) + "), every CRC-32 true over " +
                  std::to_string(w.chunks) + " chunks (THE CONTROL: > 0) " + png.message);
        unsigned char opaque[sizeof px];
        std::memcpy(opaque, px, sizeof px);
        for (unsigned i = 3; i < sizeof opaque; i += 4) opaque[i] = 255;
        const Result madeOpaque = e.call("engine.picture.create", {made(4, 4, bytesOf(opaque, sizeof opaque))});
        const Result pngOpaque = e.call("engine.picture.png", {madeOpaque.value["picture"], Value(1024)});
        e.call("engine.picture.release", {madeOpaque.value["picture"]});
        const PngWalk wo = walkPng(pngOpaque.status == Status::Ok ? pngOpaque.value["png"].asBytes() : Value::Bytes());
        check(pngOpaque.status == Status::Ok && wo.signature && wo.crcs && wo.ended && wo.depth == 8 && wo.colour == 2,
              "the same picture ALL-OPAQUE is written colour type 2, RGB (" + std::to_string(wo.colour) +
                  "): its alpha 255 by the PNG's own rule " + pngOpaque.message);
        const Result at16 = e.call("engine.picture.png", {pic4, Value(16)});
        const Result at2048 = e.call("engine.picture.png", {pic4, Value(2048)});
        unsigned char wide[20 * 2 * 4] = {};
        const Result made20 = e.call("engine.picture.create", {made(20, 2, bytesOf(wide, sizeof wide))});
        const Result small = e.call("engine.picture.png", {made20.value["picture"], Value(16)});
        e.call("engine.picture.release", {made20.value["picture"]});
        check(at16.status == Status::Ok && at2048.status == Status::Ok && small.status == Status::Ok &&
                  small.value["width"] == Value(10) && small.value["height"] == Value(1) &&
                  walkPng(small.value["png"].asBytes()).width == 10,
              "maxEdge passes at exactly 16 and 2048; a 20 x 2 picture at 16 answers its ENCODED size 10 x 1 (" +
                  at16.message + at2048.message + small.message + ")");
        // maxEdge ABSENT is the declared default 1024 (callNamed: the outside client's path): a 2000 x 1 picture answers
        // 1000 x 1 (s = 2; a default of 2048 would answer 2000)
        const std::vector<unsigned char> line(2000 * 4, 9);
        const Result made2000 = e.call("engine.picture.create", {made(2000, 1, bytesOf(line.data(), line.size()))});
        const Result byDefault = e.callNamed("engine.picture.png", {{"picture", made2000.value["picture"]}});
        e.call("engine.picture.release", {made2000.value["picture"]});
        check(byDefault.status == Status::Ok && byDefault.value["width"] == Value(1000) && byDefault.value["height"] == Value(1),
              "maxEdge absent takes the declared default 1024: a 2000 x 1 picture answers 1000 x 1 (" + byDefault.message + ")");
        // A WHOLE PNG's FRAME (pngComplete, encodePng's last check): the real PNG passes; its signature and IHDR alone (a
        // save that failed after them leaves just that: Ogre's codec ignores the save's answer), the PNG without its IEND,
        // and the signature alone are refused
        const Value::Bytes head(bytes.begin(), bytes.begin() + std::min<std::size_t>(bytes.size(), 8 + 25));
        const Value::Bytes noEnd(bytes.begin(), bytes.end() - std::min<std::size_t>(bytes.size(), 12));
        const Value::Bytes signature(bytes.begin(), bytes.begin() + std::min<std::size_t>(bytes.size(), 8));
        check(pngComplete(bytes) && !pngComplete(head) && !pngComplete(noEnd) && !pngComplete(signature) &&
                  !pngComplete(Value::Bytes()),
              "pngComplete: the whole PNG (" + std::to_string(bytes.size()) + " bytes) passes; its signature + IHDR (" +
                  std::to_string(head.size()) + "), the PNG without IEND, the signature alone and nothing are refused");
        const float rgbaF[4] = {1, 1, 1, 1};
        const Result radMade = e.call("engine.radiance.create", {made(1, 1, bytesOf(rgbaF, sizeof rgbaF))});
        const Value rad1 = radMade.status == Status::Ok ? radMade.value["radiance"] : Value();
        const Result ofRadiance = e.call("engine.picture.png", {rad1, Value(1024)});
        const Result low = e.call("engine.picture.png", {pic4, Value(15)});
        const Result high = e.call("engine.picture.png", {pic4, Value(2049)});
        e.call("engine.radiance.release", {rad1});
        e.call("engine.picture.release", {pic4});
        const Result stale = e.call("engine.picture.png", {pic4, Value(1024)});
        const Result onNone = h.call("engine.picture.create", {made(1, 1, bytesOf(px, 4))});
        const Result noEngine = h.call("engine.picture.png", {onNone.value["picture"], Value(1024)});
        h.call("engine.picture.release", {onNone.value["picture"]});
        check(failedWith(ofRadiance, "is a radiance handle, not a picture handle") &&
                  failedWith(low, "engine.picture.png: maxEdge 15 is outside 16..2048 px") &&
                  failedWith(high, "maxEdge 2049 is outside 16..2048 px") && failedWith(stale, "engine.picture.png: picture: the picture handle") &&
                  stale.message.find("is stale") != std::string::npos &&
                  failedWith(noEngine, "engine.picture.png: the engine did not start"),
              "engine.picture.png's domain FAILS by name: a radiance handle (" + ofRadiance.message + "), maxEdge 15 and "
              "2049 (" + low.message + "), a released picture (" + stale.message + "), no engine (" + noEngine.message + ")");
    }
}

}  // namespace scriptcommands
