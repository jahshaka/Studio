// script.commands - THE PICTURE FAMILY's CASES (SCRIPT-IN-ENGINE-1 stage 2 B1; test_script_commands.cpp runs them, in
// its fixed order). The pictures are the HOST's (no engine verb: a held picture's reductions read it in place), so every
// law runs on the host with no engine: THE RADIANCE PICTURE (RULED 10: the held kind `radiance`, bound to ImageF) is made,
// reduced (mean, columnStep, hash - in linear radiance, against values worked by hand and an FNV-1a computed outside the
// engine), released (its handle stale after) and counted under THE SAME live-pictures cap as a display picture; its
// domain refused by name; a wrong-kind and a foreign handle FAIL. The display picture's reductions, which now share one
// body with the radiance's, keep their own hand-worked values.
#include "script_commands.h"

#include <cstring>

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
}

}  // namespace scriptcommands
